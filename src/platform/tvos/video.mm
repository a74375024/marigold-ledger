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
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

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

// Subtitles: VLC draws them over the picture itself. Its tracks (id = VLC's
// index), what the app asked for, and the style for the next stream.
struct TextState {
  std::vector<marigold::platform::Video::TextTrack> tracks;
  bool on = false;
  std::string wanted;
  int size = 1, look = 0;
};
TextState gText;  // guarded by gMu

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
  // The caption style VLC draws with (it reads these when the stream starts).
  int size = 1, look = 0;
  {
    std::lock_guard<std::mutex> lock(gMu);
    size = gText.size;
    look = gText.look;
    gText.tracks.clear();
  }
  static const int kScale[] = {80, 100, 130, 170};  // small, medium, large, extra large
  [media addOption:[NSString stringWithFormat:@":sub-text-scale=%d", kScale[std::clamp(size, 0, 3)]]];
  [media addOption:[NSString stringWithFormat:@":freetype-background-opacity=%d", look == 0 ? 200 : 0]];
  [media addOption:[NSString stringWithFormat:@":freetype-outline-thickness=%d", look == 1 ? 6 : 0]];
  [media addOption:[NSString stringWithFormat:@":freetype-shadow-opacity=%d", look == 2 ? 200 : 0]];
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
  // The stream's subtitle tracks (VLC lists closed captions once it sees
  // them), and the one asked for shown or none.
  {
    NSArray* names = p.videoSubTitlesNames;
    NSArray* indexes = p.videoSubTitlesIndexes;
    std::vector<marigold::platform::Video::TextTrack> tracks;
    for (NSUInteger i = 0; i < MIN(names.count, indexes.count); ++i) {
      int index = [indexes[i] intValue];
      if (index < 0) continue;  // "Disable"
      NSString* name = [names[i] isKindOfClass:NSString.class] ? names[i] : @"";
      marigold::platform::Video::TextTrack t;
      t.id = std::to_string(index);
      std::string n = name.UTF8String ?: "";
      size_t open = n.find('['), close = n.rfind(']');
      if (open != std::string::npos && close != std::string::npos && close > open) t.language = n.substr(open + 1, close - open - 1);
      size_t cc = n.find("Closed captions ");
      if (cc != std::string::npos) t.channel = std::atoi(n.c_str() + cc + 16);
      tracks.push_back(std::move(t));
    }
    int want = -1;
    if (gText.on && !tracks.empty()) {
      want = std::atoi(tracks.front().id.c_str());
      for (const auto& t : tracks)
        if (t.id == gText.wanted) want = std::atoi(t.id.c_str());
    }
    gText.tracks = std::move(tracks);
    if (p.currentVideoSubTitleIndex != want && (want >= 0 || p.currentVideoSubTitleIndex >= 0)) p.currentVideoSubTitleIndex = want;
  }
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

std::vector<Video::TextTrack> Video::textTracks() const {
  std::lock_guard<std::mutex> lock(gMu);
  return gText.tracks;
}

// Applied by the player's next tick (on the main thread).
void Video::selectText(const std::string& id, bool on) {
  std::lock_guard<std::mutex> lock(gMu);
  gText.on = on;
  gText.wanted = id;
}

std::vector<std::string> Video::textLines(uint64_t* version) const {
  if (version) *version = 0;
  return {};
}

bool Video::drawsText() { return true; }

int Video::systemCaptions() { return UIAccessibilityIsClosedCaptioningEnabled() ? 1 : 0; }

void Video::setTextStyle(int size, int look) {
  std::lock_guard<std::mutex> lock(gMu);
  gText.size = size;
  gText.look = look;
}

}  // namespace marigold::platform
