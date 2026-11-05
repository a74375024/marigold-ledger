// tvOS and macOS: the video player is VLC (TVVLCKit; VLCKit on the Mac),
// drawing into the video view under the UI. It plays what Apple's own player
// can't: continuous MPEG-TS over HTTP, MPEG-2 and MP2, as well as HLS. VLC
// lives on the main thread; a timer there keeps a snapshot of its state that
// state() reads, as on Android.
#include "platform/video.hpp"

#include <TargetConditionals.h>
#import <AVFoundation/AVFoundation.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#import <IOKit/pwr_mgt/IOPMLib.h>
#import <MediaAccessibility/MediaAccessibility.h>
#import <VLCKit/VLCKit.h>
#else
#import <TVVLCKit/TVVLCKit.h>
#import <UIKit/UIKit.h>
#endif
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "core/captions/caption_style.hpp"
#include "core/util/log.hpp"
#include "core/util/redact.hpp"
#include "platform/tvos/agent.hpp"
#include "platform/tvos/bridge.hpp"

// TVVLCKit 3 has its text renderer's setters without declaring them. This
// one sizes the letters as the picture's height over the number given
// (freetype-rel-fontsize), on the player and on the picture playing at once.
@interface VLCMediaPlayer (MGTextRenderer)
- (void)setTextRendererFontSize:(NSNumber*)fontSize;
@end

namespace {

struct Snapshot {
  bool active = false, ready = false, playing = false, failed = false, ended = false, picture = false;
  bool waiting = false;  // no new pictures yet, or they stopped coming
  std::chrono::steady_clock::time_point waitingSince;
  double position = 0;
  int width = 0, height = 0;
  std::string error;
  // The picture check: the video tracks VLC lists (-1 not yet), pictures
  // that stopped being displayed while the clock runs, and what plays.
  int videoTracks = -1;
  bool stalled = false;
  std::string facts;
};

double secondsNow() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A VLC codec fourcc as the playback log names it ("h264", "aac").
std::string codecName(uint32_t fourcc) {
  char c[5] = {static_cast<char>(fourcc & 0xFF), static_cast<char>((fourcc >> 8) & 0xFF),
               static_cast<char>((fourcc >> 16) & 0xFF), static_cast<char>((fourcc >> 24) & 0xFF), 0};
  std::string s(c);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
  if (s == "mpgv" || s == "mp2v") return "mpeg2";
  if (s == "mp4a") return "aac";
  if (s == "a52") return "ac3";
  if (s == "mpga") return "mp2";
  return s;
}

// What plays, from the stream's track information: "1920x1080 · h264 ·
// aac" ("" before VLC knows the video track).
std::string factsOf(VLCMedia* media) {
  std::string video, audio;
  int w = 0, h = 0;
  for (id item in media.tracksInformation) {
    if (![item isKindOfClass:NSDictionary.class]) continue;
    NSDictionary* t = item;
    NSString* type = t[VLCMediaTracksInformationType];
    uint32_t fourcc = [t[VLCMediaTracksInformationCodec] unsignedIntValue];
    if ([type isEqualToString:VLCMediaTracksInformationTypeVideo] && video.empty()) {
      video = codecName(fourcc);
      w = [t[VLCMediaTracksInformationVideoWidth] intValue];
      h = [t[VLCMediaTracksInformationVideoHeight] intValue];
    } else if ([type isEqualToString:VLCMediaTracksInformationTypeAudio] && audio.empty()) {
      audio = codecName(fourcc);
    }
  }
  if (video.empty()) return {};
  // HEVC on an Apple TV without its hardware decoder plays in software, if
  // at all: said, for the log.
  if (video == "hevc" && !VTIsHardwareDecodeSupported(kCMVideoCodecType_HEVC)) video += " (no hardware decoder)";
  std::string out;
  if (w > 0 && h > 0) out = std::to_string(w) + "x" + std::to_string(h) + " \xc2\xb7 ";
  out += video;
  if (!audio.empty()) out += " \xc2\xb7 " + audio;
  return out;
}

std::mutex gMu;
Snapshot gState;

// Subtitles: VLC draws them over the picture itself (VLCKit gives the app
// no caption text to draw). Its tracks (id = VLC's index), what the app
// asked for, the style, the size VLC was given last and what the log said
// last.
struct TextState {
  std::vector<marigold::platform::Video::TextTrack> tracks;
  bool on = false;
  std::string wanted;
  int size = 1, look = 0;
  int delayMs = 0;  // Caption timing: only later (VLC can't show captions from the picture before it)
  int sizeSet = -1;
  std::string note;
};
TextState gText;  // guarded by gMu

// VLC's letters for a caption size, as they look where the app draws them;
// false when this VLCKit can't size them (left at its own, a tenth of the
// picture's height).
bool sizeText(VLCMediaPlayer* p, int size) {
  if (![p respondsToSelector:@selector(setTextRendererFontSize:)]) return false;
  [p setTextRendererFontSize:@(marigold::captions::vlcRelativeFontSize(size))];
  return true;
}

// Search's corner preview plays without sound (applied by the tick, since
// VLC's audio output only exists once the stream plays).
bool gMuted = false;  // guarded by gMu

// No screen saver or display sleep over a picture. Main thread.
void keepAwake(bool on) {
#if TARGET_OS_OSX
  static IOPMAssertionID assertion = 0;
  if (on && !assertion) {
    if (IOPMAssertionCreateWithName(kIOPMAssertionTypePreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
                                    CFSTR("Playing live TV"), &assertion) != kIOReturnSuccess)
      assertion = 0;
  } else if (!on && assertion) {
    IOPMAssertionRelease(assertion);
    assertion = 0;
  }
#else
  UIApplication.sharedApplication.idleTimerDisabled = on ? YES : NO;
#endif
}

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
// The picture check: the displayed-picture count last seen, and since when
// (and from which clock time, ms) it hasn't moved; 0 = it moves.
@property(nonatomic) int lastShown;
@property(nonatomic) double stillSince;
@property(nonatomic) int stillFrom;
@end

@implementation MGPlayer

- (instancetype)init {
  if ((self = [super init])) {
    VLCLibrary.sharedLibrary.loggers = @[ [[MGVlcLog alloc] init] ];
    self.player = [[VLCMediaPlayer alloc] init];
    self.player.delegate = self;
    self.player.drawable = marigold::tvos::videoView();
#if !TARGET_OS_OSX
    [AVAudioSession.sharedInstance setCategory:AVAudioSessionCategoryPlayback error:nil];
#endif
  }
  return self;
}

- (void)play:(NSURL*)url options:(NSArray<NSString*>*)options {
  [self.timer invalidate];
  VLCMedia* media = [VLCMedia mediaWithURL:url];
  for (NSString* o in options) [media addOption:o];
  // The caption style VLC draws with. Its text renderer belongs to the
  // player, not the stream, so it never reads the stream's options: the
  // letters' size is set on the player (and changed live: tick). The box
  // behind 608/708 captions is an option of VLC's caption decoder, which
  // reads it as the stream starts.
  int size = 1, look = 0;
  std::string note;
  {
    std::lock_guard<std::mutex> lock(gMu);
    size = gText.size;
    look = gText.look;
    gText.tracks.clear();
    gText.sizeSet = size;
    note = marigold::captions::vlcCaptionNote(size, look, gText.delayMs);
    if (note == gText.note) note.clear();
    else gText.note = note;
  }
  [media addOption:marigold::captions::vlcCaptionBox(look) ? @":cc-opaque" : @":no-cc-opaque"];
  sizeText(self.player, size);
  if (!note.empty()) MLOG_I("video", "captions: %s", note.c_str());
  self.lastTime = -1;
  self.lastShown = -1;
  self.stillSince = 0;
  {
    std::lock_guard<std::mutex> lock(gMu);
    gState = Snapshot{};
    gState.active = true;
    gState.waiting = true;
    gState.waitingSince = std::chrono::steady_clock::now();
  }
  self.player.media = media;
  marigold::tvos::videoView().hidden = NO;
  keepAwake(true);
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
  keepAwake(false);
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
  // The picture check: displayed pictures must keep coming while the clock
  // runs (2 s and 1.5 s of clock without one is a frozen picture).
  bool stalled = false;
  if (shown > 0 && p.state == VLCMediaPlayerStatePlaying) {
    if (shown != self.lastShown) {
      self.lastShown = shown;
      self.stillSince = 0;
    } else if (self.stillSince == 0) {
      self.stillSince = secondsNow();
      self.stillFrom = now;
    } else {
      stalled = secondsNow() - self.stillSince >= 2.0 && now - self.stillFrom >= 1500;
    }
  } else {
    self.stillSince = 0;
  }
  // The video tracks VLC lists, once it has found the stream's tracks.
  int videoTracks = -1;
  if (p.state == VLCMediaPlayerStatePlaying) {
    int video = 0, audio = 0;
    for (NSNumber* i in p.videoTrackIndexes)
      if (i.intValue >= 0) ++video;  // not "Disable"
    for (NSNumber* i in p.audioTrackIndexes)
      if (i.intValue >= 0) ++audio;
    if (video > 0 || audio > 0) videoTracks = video;
  }
  bool needFacts = false;
  int textSize = -1;  // a caption size to give VLC now
  {
    std::lock_guard<std::mutex> lock(gMu);
    needFacts = gState.active && gState.picture && gState.facts.empty();
    if (gText.size != gText.sizeSet) textSize = gText.sizeSet = gText.size;
  }
  // A new caption size shows at once: VLC draws its next lines with it.
  if (textSize >= 0) sizeText(p, textSize);
  std::string facts = needFacts && p.media ? factsOf(p.media) : std::string();
  std::lock_guard<std::mutex> lock(gMu);
  if (!gState.active) return;
  gState.stalled = stalled;
  gState.videoTracks = videoTracks;
  if (!facts.empty()) gState.facts = facts;
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
    // VLC's subtitle delay is in microseconds, positive later.
    NSInteger delay = static_cast<NSInteger>(gText.delayMs) * 1000;
    if (p.currentVideoSubTitleDelay != delay) p.currentVideoSubTitleDelay = delay;
    // The log: the style VLC was given, when it changes (values only).
    std::string note = marigold::captions::vlcCaptionNote(gText.size, gText.look, gText.delayMs);
    if (note != gText.note) {
      gText.note = note;
      MLOG_I("video", "captions: %s", note.c_str());
    }
  }
  if (p.audio && p.audio.muted != gMuted) p.audio.muted = gMuted;
  // Waiting: the clock stands (before a picture, sound alone with the clock
  // running isn't waiting: the picture check judges it).
  bool waiting = gState.picture ? !advancing && p.state != VLCMediaPlayerStatePaused : !advancing;
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
#if TARGET_OS_OSX
  // The window scales the app's 1920x1080 to its size (main.mm).
  double x = static_cast<double>(r.x), y = static_cast<double>(r.y);
  double w = static_cast<double>(std::max(1.0f, r.w)), h = static_cast<double>(std::max(1.0f, r.h));
  tvos::onMain([x, y, w, h] { tvos::placeVideo(x, y, w, h); });
#else
  CGRect frame = CGRectMake(static_cast<CGFloat>(r.x), static_cast<CGFloat>(r.y),
                            static_cast<CGFloat>(std::max(1.0f, r.w)), static_cast<CGFloat>(std::max(1.0f, r.h)));
  tvos::onMain([frame] { tvos::videoView().frame = frame; });
#endif
}

}  // namespace

bool Video::start() {
  tvos::onMainSync([] {
    if (gPlayer) return;
    gPlayer = [[MGPlayer alloc] init];
    // VLCKit gives no caption text: VLC draws the captions, styled from the
    // settings where it can be.
    bool sized = [gPlayer.player respondsToSelector:@selector(setTextRendererFontSize:)];
    MLOG_I("video", "captions: drawn by VLC; size %s, box from the look, timing as its subtitle delay",
           sized ? "from the settings" : "VLC's own (this VLCKit can't set it)");
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

void Video::setMuted(bool muted) {
  if (muted == muted_) return;
  muted_ = muted;
  {
    std::lock_guard<std::mutex> lock(gMu);
    gMuted = muted;
  }
  // At once on the stream playing (the tick keeps it so).
  if (started_) tvos::onMain([muted] { gPlayer.player.audio.muted = muted; });
}

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
  s.videoTracks = gState.videoTracks;
  s.framesStalled = gState.stalled;
  s.facts = gState.facts;
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

#if TARGET_OS_OSX
// System Settings > Accessibility > Captions: "Prefer closed captions and SDH".
int Video::systemCaptions() {
  return MACaptionAppearanceGetDisplayType(kMACaptionAppearanceDomainUser) == kMACaptionAppearanceDisplayTypeAlwaysOn
             ? 1
             : 0;
}
#else
int Video::systemCaptions() { return UIAccessibilityIsClosedCaptioningEnabled() ? 1 : 0; }
#endif

void Video::setTextStyle(int size, int look) {
  std::lock_guard<std::mutex> lock(gMu);
  gText.size = size;
  gText.look = look;
}

// Applied by the player's next tick (on the main thread).
void Video::setTextDelay(int ms) {
  std::lock_guard<std::mutex> lock(gMu);
  gText.delayMs = std::max(ms, 0);
}

}  // namespace marigold::platform
