// tvOS: the video player is VLC (TVVLCKit), drawing into the video view under
// the UI. It plays what Apple's own player can't: continuous MPEG-TS over
// HTTP, MPEG-2 and MP2, as well as HLS. VLC lives on the main thread; a timer
// there keeps a snapshot of its state that state() reads, as on Android.
#include "platform/video.hpp"

#import <AVFoundation/AVFoundation.h>
#import <TVVLCKit/TVVLCKit.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <chrono>
#include <mutex>

#include "core/util/log.hpp"
#include "core/util/redact.hpp"
#include "platform/tvos/agent.hpp"
#include "platform/tvos/bridge.hpp"

namespace {

struct Snapshot {
  bool active = false, ready = false, playing = false, failed = false, ended = false, picture = false;
  bool waiting = false;  // no new pictures yet, or they stopped coming
  std::chrono::steady_clock::time_point waitingSince;
  double position = 0;
  int width = 0, height = 0;
  std::string error;
};

std::mutex gMu;
Snapshot gState;

}  // namespace

// VLC's own errors, in the app's log (redacted: they can name the link).
@interface MGVlcLog : NSObject <VLCLogging>
@property(readwrite, nonatomic) VLCLogLevel level;
@end

@implementation MGVlcLog
- (void)handleMessage:(NSString*)message logLevel:(VLCLogLevel)level context:(VLCLogContext*)context {
  if (level != kVLCLogLevelError) return;
  std::string line = marigold::redact(message.UTF8String ?: "");
  MLOG_W("video", "vlc: %s", line.c_str());
}
@end

@interface MGPlayer : NSObject <VLCMediaPlayerDelegate>
@property(nonatomic, strong) VLCMediaPlayer* player;
@property(nonatomic, strong) NSTimer* timer;
@property(nonatomic) int lastTime;
@end

@implementation MGPlayer

- (instancetype)init {
  if ((self = [super init])) {
    VLCLibrary.sharedLibrary.loggers = @[ [[MGVlcLog alloc] init] ];
    self.player = [[VLCMediaPlayer alloc] init];
    self.player.delegate = self;
    self.player.drawable = marigold::tvos::videoView();
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryPlayback error:nil];
  }
  return self;
}

- (void)play:(NSURL*)url options:(NSArray<NSString*>*)options {
  [self.timer invalidate];
  VLCMedia* media = [VLCMedia mediaWithURL:url];
  for (NSString* o in options) [media addOption:o];
  self.lastTime = -1;
  {
    std::lock_guard<std::mutex> lock(gMu);
    gState = Snapshot{};
    gState.active = true;
    gState.waiting = true;
    gState.waitingSince = std::chrono::steady_clock::now();
  }
  self.player.media = media;
  marigold::tvos::videoView().hidden = NO;
  UIApplication.sharedApplication.idleTimerDisabled = YES;  // no screen saver over a picture
  [self.player play];
  __weak MGPlayer* weakSelf = self;
  self.timer = [NSTimer scheduledTimerWithTimeInterval:0.25
                                               repeats:YES
                                                 block:^(NSTimer*) {
                                                   [weakSelf tick];
                                                 }];
}

- (void)stop {
  [self.timer invalidate];
  self.timer = nil;
  [self.player stop];
  self.player.media = nil;
  marigold::tvos::videoView().hidden = YES;
  UIApplication.sharedApplication.idleTimerDisabled = NO;
  std::lock_guard<std::mutex> lock(gMu);
  gState = Snapshot{};
}

// Position, size and whether pictures still come, four times a second.
- (void)tick {
  VLCMediaPlayer* p = self.player;
  int now = p.time.intValue;  // ms since the stream started
  CGSize size = p.videoSize;
  int shown = p.media ? p.media.statistics.displayedPictures : 0;
  bool advancing = now > self.lastTime && self.lastTime >= 0;
  self.lastTime = now;
  std::lock_guard<std::mutex> lock(gMu);
  if (!gState.active) return;
  gState.position = now > 0 ? now / 1000.0 : 0.0;
  if (size.width > 0 && size.height > 0) {
    gState.width = static_cast<int>(size.width);
    gState.height = static_cast<int>(size.height);
  }
  if (shown > 0) gState.picture = true;
  bool waiting = !gState.picture || (!advancing && p.state != VLCMediaPlayerStatePaused);
  if (waiting && !gState.waiting) gState.waitingSince = std::chrono::steady_clock::now();
  gState.waiting = waiting && !gState.failed && !gState.ended;
}

- (void)mediaPlayerStateChanged:(NSNotification*)notification {
  VLCMediaPlayerState st = self.player.state;
  std::lock_guard<std::mutex> lock(gMu);
  if (!gState.active) return;
  switch (st) {
    case VLCMediaPlayerStatePlaying:
      gState.ready = gState.playing = true;
      break;
    case VLCMediaPlayerStatePaused:
      gState.playing = false;
      break;
    case VLCMediaPlayerStateEnded:
      gState.ended = true;
      gState.playing = gState.waiting = false;
      break;
    case VLCMediaPlayerStateError:
      gState.failed = true;
      gState.playing = gState.waiting = false;
      gState.error = "playback error";
      break;
    default:
      break;
  }
}

@end

namespace marigold::platform {
namespace {

MGPlayer* gPlayer;

// "Name: value" lines to VLC's options: it takes the user agent and the
// referrer (other headers aren't passed to its HTTP module).
NSArray<NSString*>* optionsFor(const std::string& headers) {
  NSMutableArray<NSString*>* out = [NSMutableArray array];
  std::string agent = marigold::tvos::neutralUserAgent();
  size_t start = 0;
  while (start < headers.size()) {
    size_t end = headers.find('\n', start);
    std::string line = headers.substr(start, end == std::string::npos ? std::string::npos : end - start);
    start = end == std::string::npos ? headers.size() : end + 1;
    size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0) continue;
    NSString* name = [@(line.substr(0, colon).c_str()) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    NSString* value = [@(line.substr(colon + 1).c_str()) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
    if (!name || !value) continue;
    if ([name caseInsensitiveCompare:@"User-Agent"] == NSOrderedSame) agent = value.UTF8String ? std::string(value.UTF8String) : marigold::tvos::neutralUserAgent();
    else if ([name caseInsensitiveCompare:@"Referer"] == NSOrderedSame)
      [out addObject:[@":http-referrer=" stringByAppendingString:value]];
  }
  [out addObject:[@":http-user-agent=" stringByAppendingString:@(agent.c_str())]];
  [out addObject:@":http-reconnect"];
  // Live TV: start after half a second of video (VLC waits a full second).
  [out addObject:@":network-caching=500"];
  return out;
}

void place(gfx::Rect r) {
  CGRect frame = CGRectMake(static_cast<CGFloat>(r.x), static_cast<CGFloat>(r.y),
                            static_cast<CGFloat>(std::max(1.0f, r.w)), static_cast<CGFloat>(std::max(1.0f, r.h)));
  tvos::onMain([frame] { tvos::videoView().frame = frame; });
}

}  // namespace

bool Video::start() {
  tvos::onMainSync([] {
    if (!gPlayer) gPlayer = [[MGPlayer alloc] init];
  });
  started_ = gPlayer != nil;
  return started_;
}

bool Video::play(const std::string& url, const std::string& headers, int loadMode) {
  if (url.empty() || !started_) return false;
  NSURL* link = tvos::urlFrom(url);
  if (!link) {
    MLOG_W("video", "play: not a link");
    return false;
  }
  NSArray<NSString*>* options = optionsFor(headers);
  tvos::onMain([link, options] { [gPlayer play:link options:options]; });
  MLOG_I("video", "play (mode %d): sent", loadMode);
  return true;
}

void Video::stop() {
  if (started_) tvos::onMain([] { [gPlayer stop]; });
}

void Video::pump() {}
void Video::setWindow(gfx::Rect r) { place(r); }
void Video::reapplyWindow(gfx::Rect r) { place(r); }

void Video::shutdown() {
  if (started_) tvos::onMainSync([] { [gPlayer stop]; });
  started_ = false;
}

VideoState Video::state() const {
  VideoState s;
  s.registered = started_;
  std::lock_guard<std::mutex> lock(gMu);
  s.active = gState.active;
  s.ready = gState.ready;
  s.playing = gState.playing && !gState.waiting;
  s.failed = gState.failed;
  s.ended = gState.ended;
  s.decoderSeen = gState.picture;
  if (gState.waiting)
    s.bufferingMs = static_cast<unsigned>(std::max<int64_t>(
        1, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gState.waitingSince)
               .count()));
  s.position = gState.position;
  s.width = gState.width;
  s.height = gState.height;
  s.error = redact(gState.error);
  return s;
}

}  // namespace marigold::platform
