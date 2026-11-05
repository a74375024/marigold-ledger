// The Mac entry point: AppKit's app and one window. The window's content is
// the stage: the video view under the UI's Metal layer (drawn at 1920x1080
// and scaled to the window, black bars where its shape differs), and a hidden
// text field that the app's fields type into. It turns the keyboard, the
// trackpad and the mouse into the app's events (keys, typed characters, the
// pointer and the wheel) and the window's life into Shown, Hidden and
// Background, and starts the app thread once, which runs the same app::run as
// the TV apps. The other backends are the Apple TV's (src/platform/tvos),
// which build for the Mac too.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <VLCKit/VLCKit.h>
#include <os/log.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "app/launch.hpp"
#include "core/input/keys.hpp"
#include "core/util/log.hpp"
#include "platform/storage.hpp"
#include "platform/tvos/bridge.hpp"
#include "platform/video.hpp"
#include "platform/window.hpp"

#ifndef MARIGOLD_VERSION
#define MARIGOLD_VERSION "0.0.0"
#endif
#ifndef MARIGOLD_BUILD
#define MARIGOLD_BUILD "dev"
#endif

namespace {

constexpr CGFloat kLayoutW = 1920, kLayoutH = 1080;
// A trackpad's two-finger scroll: one step per this many points; a mouse
// wheel's: one per notch.
constexpr CGFloat kScrollStep = 36;
// A right-click holds OK this long (the app's hold is 700 ms).
constexpr double kRightClickHold = 0.8;
// The app thread gets this long to save when the Mac app quits.
constexpr double kQuitWait = 4.0;

std::atomic<bool> gAppRunning{false};
bool gTerminating = false;  // main thread: the app thread was asked to quit
bool gLogStderr = false;    // set before the app thread starts

os_log_t logger() {
  static os_log_t l = os_log_create("org.marigold.app", "app");
  return l;
}

void systemLog(marigold::log::Level level, const std::string& line) {
  using marigold::log::Level;
  os_log_type_t type = level == Level::Error ? OS_LOG_TYPE_ERROR
                       : level == Level::Debug ? OS_LOG_TYPE_DEBUG
                                               : OS_LOG_TYPE_DEFAULT;
  // The app's lines never carry a link (they are redacted before logging).
  os_log_with_type(logger(), type, "%{public}s", line.c_str());
  if (gLogStderr) std::fprintf(stderr, "%s\n", line.c_str());
}

std::string utf8(NSString* s) { return s ? std::string(s.UTF8String ?: "") : std::string(); }

void appThread(std::string assetDir, std::string dataDir) {
  using namespace marigold;
  log::setMirror(systemLog);
  app::prepareProcess();
  platform::Storage storage;
  app::openStorage(storage, dataDir);
  MLOG_I("app", "Marigold %s build %s (macOS), data in %s", MARIGOLD_VERSION, MARIGOLD_BUILD,
         storage.dir().empty() ? "(nowhere writable)" : storage.dir().c_str());
  int code = 1;
  {
    platform::Window window;
    if (window.open("Marigold")) {
      app::Build build;
      build.version = MARIGOLD_VERSION;
      build.build = MARIGOLD_BUILD;
      build.system = window.runtime();
      platform::Video video;
      code = app::run(window, storage, video, assetDir, std::move(build));
    }
  }
  MLOG_I("app", "app thread done (%d)", code);
  log::close();
  gAppRunning = false;
  // Done (⌘Q, the window closed, the app's own quit or an error): the Mac
  // app goes too.
  marigold::tvos::onMain([] { [NSApp terminate:nil]; });
}

marigold::platform::Event keyEvent(marigold::Key key, bool down, bool repeat) {
  marigold::platform::Event e;
  e.type = down ? marigold::platform::Event::Type::KeyDown : marigold::platform::Event::Type::KeyUp;
  e.key = key;
  e.repeat = repeat;
  return e;
}

void sendEvent(marigold::platform::Event::Type type) {
  marigold::platform::Event e;
  e.type = type;
  marigold::tvos::push(std::move(e));
}

}  // namespace

// The UI's view: its layer is the Metal layer the app thread draws on, always
// 1920x1080 pixels, stretched over the frame the stage gives it.
@interface MGMetalView : NSView
@end

@implementation MGMetalView
- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever;
  }
  return self;
}
- (CALayer*)makeBackingLayer {
  CAMetalLayer* layer = [CAMetalLayer layer];
  layer.device = MTLCreateSystemDefaultDevice();
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.framebufferOnly = YES;
  layer.opaque = NO;
  layer.drawableSize = CGSizeMake(kLayoutW, kLayoutH);
  return layer;
}
- (BOOL)isFlipped {
  return YES;
}
// The stage takes the mouse.
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

// VLC draws into a view of its own inside this one. It never takes the mouse.
@interface MGVideoView : NSView
@end

@implementation MGVideoView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
- (BOOL)isFlipped {
  return YES;
}
@end

// The field the app's text fields type into (accounts, the lineup's search).
// It sits out of sight; the app draws what it holds.
@interface MGHiddenField : NSTextField
@end

@implementation MGHiddenField
@end

@interface MGStageView : NSView <NSTextFieldDelegate>
@property(nonatomic, strong) MGVideoView* videoView;
@property(nonatomic, strong) MGMetalView* uiView;
@property(nonatomic, strong) MGHiddenField* field;
@property(nonatomic) BOOL fieldEditing;
@property(nonatomic) BOOL fieldConfirmed;
// Where the app wants the video, in its 1920x1080.
@property(nonatomic) NSRect videoRect;
// The 1920x1080 picture's place in the window, and its scale.
@property(nonatomic) NSRect fit;
@property(nonatomic) CGFloat scale;
@property(nonatomic) CGFloat scrollLeft;
@property(nonatomic, strong) NSTrackingArea* tracking;
@end

static MGStageView* gStage;
static NSWindow* gWindow;

@implementation MGStageView

- (instancetype)initWithFrame:(NSRect)frame {
  if ((self = [super initWithFrame:frame])) {
    self.wantsLayer = YES;
    self.layer.backgroundColor = NSColor.blackColor.CGColor;
    self.scale = 1;

    // VLC draws into this view; the app moves and sizes it (Video::setWindow).
    self.videoView = [[MGVideoView alloc] initWithFrame:NSMakeRect(0, 0, kLayoutW, kLayoutH)];
    self.videoView.wantsLayer = YES;
    self.videoView.layer.backgroundColor = NSColor.blackColor.CGColor;
    self.videoView.hidden = YES;
    [self addSubview:self.videoView];

    // The UI over it: transparent where the app clears a hole for the video.
    self.uiView = [[MGMetalView alloc] initWithFrame:NSMakeRect(0, 0, kLayoutW, kLayoutH)];
    [self addSubview:self.uiView];

    self.field = [[MGHiddenField alloc] initWithFrame:NSMakeRect(-4000, -4000, 200, 24)];
    self.field.delegate = self;
    self.field.bezeled = NO;
    self.field.drawsBackground = NO;
    self.field.alphaValue = 0;
    self.field.usesSingleLineMode = YES;
    self.field.cell.scrollable = YES;
    self.field.automaticTextCompletionEnabled = NO;
    [self addSubview:self.field];
    [self layoutStage];
  }
  return self;
}

- (BOOL)isFlipped {
  return YES;
}

- (BOOL)acceptsFirstResponder {
  return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}

- (void)setFrameSize:(NSSize)size {
  [super setFrameSize:size];
  [self layoutStage];
}

// The 1920x1080 picture, as large as fits, in the middle.
- (void)layoutStage {
  NSSize size = self.bounds.size;
  if (size.width <= 0 || size.height <= 0) return;
  CGFloat scale = std::min(size.width / kLayoutW, size.height / kLayoutH);
  CGFloat w = std::floor(kLayoutW * scale), h = std::floor(kLayoutH * scale);
  self.scale = scale;
  self.fit = NSMakeRect(std::floor((size.width - w) / 2), std::floor((size.height - h) / 2), w, h);
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  // The layer stretches its 1920x1080 picture over the frame.
  self.uiView.frame = self.fit;
  ((CAMetalLayer*)self.uiView.layer).drawableSize = CGSizeMake(kLayoutW, kLayoutH);
  [self placeVideo];
  [CATransaction commit];
}

- (void)placeVideo {
  NSRect r = self.videoRect;
  if (r.size.width <= 0 || r.size.height <= 0) r = NSMakeRect(0, 0, kLayoutW, kLayoutH);
  NSRect fit = self.fit;
  CGFloat s = self.scale;
  self.videoView.frame = NSMakeRect(fit.origin.x + r.origin.x * s, fit.origin.y + r.origin.y * s, r.size.width * s,
                                    r.size.height * s);
}

// ---- The pointer: the trackpad and the mouse ----

- (void)updateTrackingAreas {
  [super updateTrackingAreas];
  if (self.tracking) [self removeTrackingArea:self.tracking];
  self.tracking = [[NSTrackingArea alloc]
      initWithRect:NSZeroRect
           options:NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
             owner:self
          userInfo:nil];
  [self addTrackingArea:self.tracking];
}

// Where the pointer is in the app's 1920x1080.
- (NSPoint)appPoint:(NSEvent*)event {
  NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
  NSRect fit = self.fit;
  CGFloat s = self.scale > 0 ? self.scale : 1;
  return NSMakePoint((p.x - fit.origin.x) / s, (p.y - fit.origin.y) / s);
}

- (void)pointerAt:(NSEvent*)event type:(marigold::platform::Event::Type)type {
  NSPoint p = [self appPoint:event];
  marigold::platform::Event e;
  e.type = type;
  e.x = static_cast<float>(p.x);
  e.y = static_cast<float>(p.y);
  marigold::tvos::push(std::move(e));
}

- (void)mouseMoved:(NSEvent*)event {
  [self pointerAt:event type:marigold::platform::Event::Type::PointerMove];
}

- (void)mouseDragged:(NSEvent*)event {
  [self pointerAt:event type:marigold::platform::Event::Type::PointerMove];
}

- (void)mouseDown:(NSEvent*)event {
  // A click outside the field ends its typing (kept, not confirmed).
  if (self.fieldEditing) [self endField:NO];
  [self pointerAt:event type:marigold::platform::Event::Type::PointerMove];
  [self pointerAt:event type:marigold::platform::Event::Type::PointerDown];
}

- (void)mouseUp:(NSEvent*)event {
  [self pointerAt:event type:marigold::platform::Event::Type::PointerUp];
}

// A right-click (or a two-finger click) holds OK where the item has more
// (a channel's options).
- (void)rightMouseDown:(NSEvent*)event {
  if (self.fieldEditing) [self endField:NO];
  [self pointerAt:event type:marigold::platform::Event::Type::PointerMove];
  [self pointerAt:event type:marigold::platform::Event::Type::PointerDown];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(kRightClickHold * NSEC_PER_SEC)),
                 dispatch_get_main_queue(), ^{
                   sendEvent(marigold::platform::Event::Type::PointerUp);
                 });
}

- (void)scrollWheel:(NSEvent*)event {
  // A flick's coasting after the fingers lift moves nothing: lists move as
  // far as the fingers did.
  if (event.momentumPhase != NSEventPhaseNone) return;
  if (event.phase == NSEventPhaseBegan) self.scrollLeft = 0;
  CGFloat dy = event.scrollingDeltaY;
  if (!event.hasPreciseScrollingDeltas) dy *= kScrollStep;  // a wheel's notches
  self.scrollLeft += dy;
  while (std::fabs(self.scrollLeft) >= kScrollStep) {
    int dir = self.scrollLeft > 0 ? 1 : -1;
    self.scrollLeft -= dir * kScrollStep;
    marigold::platform::Event e;
    e.type = marigold::platform::Event::Type::Wheel;
    e.wheel = dir;  // +1: up
    marigold::tvos::push(std::move(e));
  }
}

// ---- The keyboard ----

- (void)keyDown:(NSEvent*)event {
  NSEventModifierFlags mods = event.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
  // ⌘ and ⌃ shortcuts are the menus'.
  if (mods & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) {
    [super keyDown:event];
    return;
  }
  marigold::Key key = marigold::keyFromMac(event.keyCode);
  if (key != marigold::Key::Unknown) {
    marigold::tvos::push(keyEvent(key, true, event.isARepeat));
    return;
  }
  marigold::platform::Event e;
  e.type = marigold::platform::Event::Type::Char;
  e.repeat = event.isARepeat;
  // Delete (and forward delete): the last character, or Back.
  if (event.keyCode == 0x33 || event.keyCode == 0x75) {
    e.text = "\b";
  } else {
    NSString* chars = event.characters;
    if (chars.length == 0) return;
    unichar first = [chars characterAtIndex:0];
    // Control characters and the function keys (F1, Home, ...) type nothing.
    if (first < 0x20 || first == 0x7F || (first >= 0xF700 && first <= 0xF8FF)) return;
    e.text = utf8(chars);
  }
  e.key = marigold::keyFromMacChar(e.text);
  marigold::tvos::push(std::move(e));
}

- (void)keyUp:(NSEvent*)event {
  marigold::Key key = marigold::keyFromMac(event.keyCode);
  if (key != marigold::Key::Unknown) marigold::tvos::push(keyEvent(key, false, false));
}

// ---- The hidden field (the app's text fields) ----

- (void)openField:(NSString*)value {
  self.fieldConfirmed = NO;
  self.field.stringValue = value;
  self.fieldEditing = YES;
  if ([self.window makeFirstResponder:self.field]) {
    // The caret at the end, nothing selected.
    NSText* editor = [self.window fieldEditor:NO forObject:self.field];
    if (editor) editor.selectedRange = NSMakeRange(value.length, 0);
    MLOG_I("keyboard", "open");
  } else {
    // The app must not wait for a field that isn't there.
    MLOG_W("keyboard", "the text field didn't open");
    [self endField:NO];
  }
}

- (void)endField:(BOOL)confirmed {
  if (!self.fieldEditing) return;
  self.fieldEditing = NO;
  self.fieldConfirmed = confirmed;
  [self sendText:YES];
  [self.window makeFirstResponder:self];
  MLOG_I("keyboard", "closed");
}

- (void)sendText:(BOOL)done {
  marigold::platform::Event e;
  e.type = marigold::platform::Event::Type::Text;
  e.text = utf8(self.field.stringValue);
  e.done = done;
  e.confirmed = done && self.fieldConfirmed;
  marigold::tvos::push(std::move(e));
}

- (void)controlTextDidChange:(NSNotification*)notification {
  if (self.fieldEditing) [self sendText:NO];
}

// Return and tab confirm (the next field); esc closes it as it is.
- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)command {
  if (command == @selector(insertNewline:) || command == @selector(insertTab:)) {
    [self endField:YES];
    return YES;
  }
  if (command == @selector(cancelOperation:)) {
    [self endField:NO];
    return YES;
  }
  return NO;
}

- (void)controlTextDidEndEditing:(NSNotification*)notification {
  // Focus went elsewhere (another window): the field closes as it is.
  if (self.fieldEditing) [self endField:NO];
}

// Started with -MarigoldKeyboardSelfTest YES (tools/mac-smoke.sh): opens and
// closes the field twice and checks the keys come back to the app each time.
- (void)keyboardSelfTest {
  __weak MGStageView* weakSelf = self;
  auto after = [](double seconds, void (^block)(void)) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(seconds * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), block);
  };
  __block bool ok = true;
  auto check = ^bool(const char* step, bool editing) {
    MGStageView* me = weakSelf;
    if (!me) return false;
    NSResponder* first = me.window.firstResponder;
    // While editing, the first responder is the field's editor (a text view
    // whose delegate is the field).
    bool fieldHas = [first isKindOfClass:NSTextView.class] &&
                    (__bridge void*)((NSTextView*)first).delegate == (__bridge void*)me.field;
    bool appHas = first == me;
    MLOG_I("keyboard", "self-test %s: field has the keys %d, app has the keys %d", step, fieldHas ? 1 : 0,
           appHas ? 1 : 0);
    return editing ? fieldHas && me.fieldEditing : appHas && !me.fieldEditing;
  };
  after(4, ^{ [weakSelf openField:@"first"]; });
  after(6, ^{
    ok = check("first open", true) && ok;
    [weakSelf endField:YES];
  });
  after(8, ^{
    ok = check("first close", false) && ok;
    [weakSelf openField:@"second"];
  });
  after(10, ^{
    ok = check("second open", true) && ok;
    [weakSelf endField:NO];
  });
  after(12, ^{
    ok = check("second close", false) && ok;
    if (ok) MLOG_I("keyboard", "self-test: ok");
    else MLOG_W("keyboard", "self-test: FAILED");
  });
}

// Started with -MarigoldVideoSelfTest <file> (tools/mac-smoke.sh): VLC as
// packed in the app plays the file into a view of its own, out of sight, and
// must show pictures. The app's own player is left alone.
- (void)videoSelfTest:(NSString*)file {
  NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 320, 180)];
  view.wantsLayer = YES;
  view.hidden = NO;
  view.alphaValue = 0.01;
  [self addSubview:view positioned:NSWindowBelow relativeTo:self.videoView];
  VLCMediaPlayer* player = [[VLCMediaPlayer alloc] init];
  player.drawable = view;
  player.media = [VLCMedia mediaWithURL:[NSURL fileURLWithPath:file]];
  [player.media addOption:@":input-repeat=10"];
  player.audio.muted = YES;
  [player play];
  MLOG_I("video", "self-test: playing the test file");
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(8 * NSEC_PER_SEC)), dispatch_get_main_queue(),
                 ^{
                   int shown = player.media ? player.media.statistics.displayedPictures : 0;
                   int decoded = player.media ? player.media.statistics.decodedVideo : 0;
                   CGSize size = player.videoSize;
                   MLOG_I("video", "self-test: %d pictures decoded, %d shown, %dx%d", decoded, shown,
                          static_cast<int>(size.width), static_cast<int>(size.height));
                   if (decoded > 0 && size.width > 0) MLOG_I("video", "self-test: ok");
                   else MLOG_W("video", "self-test: FAILED");
                   [player stop];
                   [view removeFromSuperview];
                 });
}

@end

// ---- The app and its window ----

@interface MGAppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation MGAppDelegate

- (void)buildMenus {
  NSMenu* bar = [[NSMenu alloc] init];

  NSMenuItem* appItem = [[NSMenuItem alloc] init];
  NSMenu* app = [[NSMenu alloc] initWithTitle:@"Marigold"];
  [app addItemWithTitle:@"About Marigold" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
  [app addItem:NSMenuItem.separatorItem];
  [app addItemWithTitle:@"Hide Marigold" action:@selector(hide:) keyEquivalent:@"h"];
  NSMenuItem* others = [app addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
  others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
  [app addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
  [app addItem:NSMenuItem.separatorItem];
  [app addItemWithTitle:@"Quit Marigold" action:@selector(terminate:) keyEquivalent:@"q"];
  appItem.submenu = app;
  [bar addItem:appItem];

  // The app's text fields take cut, copy and paste (a password, a link).
  NSMenuItem* editItem = [[NSMenuItem alloc] init];
  NSMenu* edit = [[NSMenu alloc] initWithTitle:@"Edit"];
  [edit addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
  NSMenuItem* redo = [edit addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"z"];
  redo.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagShift;
  [edit addItem:NSMenuItem.separatorItem];
  [edit addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
  [edit addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
  [edit addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
  [edit addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
  editItem.submenu = edit;
  [bar addItem:editItem];

  NSMenuItem* viewItem = [[NSMenuItem alloc] init];
  NSMenu* view = [[NSMenu alloc] initWithTitle:@"View"];
  [view addItemWithTitle:@"Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
  viewItem.submenu = view;
  [bar addItem:viewItem];

  NSMenuItem* windowItem = [[NSMenuItem alloc] init];
  NSMenu* window = [[NSMenu alloc] initWithTitle:@"Window"];
  [window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
  [window addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
  windowItem.submenu = window;
  [bar addItem:windowItem];
  NSApp.windowsMenu = window;

  NSApp.mainMenu = bar;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
  [self buildMenus];
  NSUserDefaults* defaults = NSUserDefaults.standardUserDefaults;
  gLogStderr = [defaults boolForKey:@"MarigoldLogToStderr"];

  NSRect screen = NSScreen.mainScreen ? NSScreen.mainScreen.visibleFrame : NSMakeRect(0, 0, 1440, 900);
  CGFloat w = std::min<CGFloat>(1280, screen.size.width * 0.8);
  NSRect frame = NSMakeRect(0, 0, w, std::floor(w * 9 / 16));
  gWindow = [[NSWindow alloc] initWithContentRect:frame
                                        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                  NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
  gWindow.title = @"Marigold";
  gWindow.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
  gWindow.backgroundColor = NSColor.blackColor;
  gWindow.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
  gWindow.contentMinSize = NSMakeSize(640, 360);
  gWindow.delegate = self;
  gWindow.releasedWhenClosed = NO;
  gStage = [[MGStageView alloc] initWithFrame:frame];
  gWindow.contentView = gStage;
  [gWindow center];
  // Where it was last time (size and place).
  [gWindow setFrameAutosaveName:@"Marigold"];
  [gWindow makeKeyAndOrderFront:nil];
  [gWindow makeFirstResponder:gStage];
  [NSApp activateIgnoringOtherApps:YES];
  if ([defaults boolForKey:@"MarigoldFullScreen"]) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [gWindow toggleFullScreen:nil];
    });
  }

  // The fonts and icons are in the app; the data goes in Application Support.
  std::string assets = utf8([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"assets"]);
  NSString* support =
      NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject;
  NSString* dir = [support stringByAppendingPathComponent:@"Marigold"];
  [NSFileManager.defaultManager createDirectoryAtPath:dir
                          withIntermediateDirectories:YES
                                           attributes:@{NSFilePosixPermissions : @0700}
                                                error:nil];
  gAppRunning = true;
  std::thread(appThread, assets, utf8(dir)).detach();

  if ([defaults boolForKey:@"MarigoldKeyboardSelfTest"]) [gStage keyboardSelfTest];
  NSString* testFile = [defaults stringForKey:@"MarigoldVideoSelfTest"];
  if (testFile.length) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(14 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     [gStage videoSelfTest:testFile];
                   });
  }
}

// Closing the window quits, as the app has no other.
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return YES;
}

// The app thread stops the video and saves first; then it asks again
// (appThread) and the app quits. Not NSTerminateLater: while AppKit waits for
// that reply it runs only its modal run loop mode, where the main queue the
// app thread needs (to stop the video) isn't served, and the quit hung
// (Build 139's smoke test).
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  if (!gAppRunning) return NSTerminateNow;
  if (!gTerminating) {
    gTerminating = true;
    sendEvent(marigold::platform::Event::Type::Quit);
    // An app thread that doesn't finish in time doesn't keep the app open.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(kQuitWait * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
                     if (gAppRunning) {
                       MLOG_W("app", "the app thread didn't finish in time: quitting anyway");
                       _exit(0);
                     }
                   });
  }
  return NSTerminateCancel;
}

- (void)applicationDidBecomeActive:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Shown);
}

// Hidden or in the Dock, nothing plays (as on a TV going to its home screen);
// in another app's shadow, it plays on.
- (void)applicationDidHide:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Hidden);
}

- (void)applicationDidUnhide:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Shown);
}

- (void)windowDidMiniaturize:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Hidden);
}

- (void)windowDidDeminiaturize:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Shown);
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
  [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"MarigoldFullScreen"];
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
  [NSUserDefaults.standardUserDefaults setBool:NO forKey:@"MarigoldFullScreen"];
}

- (void)macWillSleep:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Background);
}

- (void)macDidWake:(NSNotification*)notification {
  sendEvent(marigold::platform::Event::Type::Shown);
}

- (void)applicationWillFinishLaunching:(NSNotification*)notification {
  NSNotificationCenter* center = NSWorkspace.sharedWorkspace.notificationCenter;
  [center addObserver:self selector:@selector(macWillSleep:) name:NSWorkspaceWillSleepNotification object:nil];
  [center addObserver:self selector:@selector(macDidWake:) name:NSWorkspaceDidWakeNotification object:nil];
}

@end

namespace marigold::tvos {

void onMain(std::function<void()> f) {
  dispatch_async(dispatch_get_main_queue(), ^{
    f();
  });
}

void onMainSync(const std::function<void()>& f) {
  if (NSThread.isMainThread) {
    f();
    return;
  }
  dispatch_sync(dispatch_get_main_queue(), ^{
    f();
  });
}

CAMetalLayer* uiLayer() { return gStage ? (CAMetalLayer*)gStage.uiView.layer : nil; }
MGView* videoView() { return gStage.videoView; }

void placeVideo(double x, double y, double w, double h) {
  if (!gStage) return;
  gStage.videoRect = NSMakeRect(x, y, w, h);
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  [gStage placeVideo];
  [CATransaction commit];
}

void openKeyboard(const std::string& value) {
  NSString* v = [NSString stringWithUTF8String:value.c_str()] ?: @"";
  onMain([v] { [gStage openField:v]; });
}

void closeKeyboard() {
  onMain([] { [gStage endField:NO]; });
}

// Back on the guide: out of full screen (the app keeps running; ⌘Q quits).
void leaveToHome() {
  onMain([] {
    if (gWindow && (gWindow.styleMask & NSWindowStyleMaskFullScreen)) [gWindow toggleFullScreen:nil];
  });
}

}  // namespace marigold::tvos

int main(int argc, const char* argv[]) {
  @autoreleasepool {
    NSApplication* app = NSApplication.sharedApplication;
    MGAppDelegate* delegate = [[MGAppDelegate alloc] init];
    app.delegate = delegate;
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    (void)argc;
    (void)argv;
    // A plain kill (logging out, the smoke test) quits as ⌘Q does: the app
    // thread saves first.
    signal(SIGTERM, SIG_IGN);
    dispatch_source_t term =
        dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(term, ^{
      [NSApp terminate:nil];
    });
    dispatch_resume(term);
    [app run];
  }
  return 0;
}
