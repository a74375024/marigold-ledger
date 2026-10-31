// The tvOS entry point: UIKit's app and scene. The scene's view controller
// holds the views (the video under, the UI's Metal layer over it, fixed at
// 1920x1080) and a hidden text field for the system keyboard. It turns the
// remote's presses and swipes into key events (a long swipe glides several
// steps, a flick coasts on) and the scene's life into
// Shown, Hidden and Background, and starts the app thread once, which runs
// the same app::run as webOS and Android.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#include <os/log.h>

#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "app/launch.hpp"
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

// Held arrows repeat as they do on the other remotes.
constexpr NSTimeInterval kRepeatDelay = 0.40;
constexpr NSTimeInterval kRepeatEvery = 0.08;

// Swipes glide (panned:). Distances are fractions of the view along the
// gesture's axis: the remote's touch surface maps onto the view.
// A resting or wobbling thumb that moves less than this sends nothing.
constexpr CGFloat kGlideDeadZone = 0.06;
// One step per this much travel past the dead zone (the first at dead zone + one step).
constexpr CGFloat kGlideStep = 1.0 / 3.0;
// A release at least this fast (views per second) coasts on, after a step by distance.
constexpr CGFloat kCoastSpeed = 2.0;
// The release speed that gives the longest coast.
constexpr CGFloat kCoastSpeedMost = 5.0;
// Steps a coast adds, from the slowest flick to the fastest.
constexpr int kCoastFewest = 2;
constexpr int kCoastMost = 4;
// The first coast step this long after release.
constexpr NSTimeInterval kCoastFirst = 0.06;
// Each later coast step waits this much longer than the one before (it slows down).
constexpr double kCoastSlowing = 1.35;
// How far apart UIKit may report a swipe and the pan of the same gesture.
constexpr CFTimeInterval kSwipePanGap = 0.25;

// Each swipe gesture's number (never 0: 0 is a move from no gesture). Main
// thread only.
uint32_t nextGlide() {
  static uint32_t last = 0;
  if (++last == 0) last = 1;
  return last;
}

os_log_t logger() {
  static os_log_t l = os_log_create("org.marigold.app", "app");
  return l;
}

void systemLog(marigold::log::Level level, const std::string& line) {
  using marigold::log::Level;
  // Info as the default level: the system keeps those (its own info level
  // lives in memory only).
  os_log_type_t type = level == Level::Error ? OS_LOG_TYPE_ERROR
                       : level == Level::Debug ? OS_LOG_TYPE_DEBUG
                                               : OS_LOG_TYPE_DEFAULT;
  // The app's lines never carry a link (they are redacted before logging).
  os_log_with_type(logger(), type, "%{public}s", line.c_str());
}

std::string utf8(NSString* s) { return s ? std::string(s.UTF8String ?: "") : std::string(); }

void appThread(std::string assetDir, std::string dataDir) {
  using namespace marigold;
  log::setMirror(systemLog);
  app::prepareProcess();
  platform::Storage storage;
  app::openStorage(storage, dataDir);
  MLOG_I("app", "Marigold %s build %s (tvOS), data in %s", MARIGOLD_VERSION, MARIGOLD_BUILD,
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
  // tvOS apps don't quit themselves; this only follows an error.
  marigold::tvos::onMain([code] { exit(code); });
}

marigold::platform::Event keyEvent(marigold::Key key, bool down, bool repeat) {
  marigold::platform::Event e;
  e.type = down ? marigold::platform::Event::Type::KeyDown : marigold::platform::Event::Type::KeyUp;
  e.key = key;
  e.repeat = repeat;
  return e;
}

}  // namespace

// The UI's view: its layer is the Metal layer the app thread draws on.
@interface MGUIView : UIView
@end

@implementation MGUIView
+ (Class)layerClass {
  return [CAMetalLayer class];
}
@end

// The hidden field the system keyboard edits. Never focusable: when the
// keyboard closes, tvOS would leave focus on it, and the focused field would
// then take the remote's presses (arrows would move nothing and a click would
// edit it instead of reaching the app), so no other field could be filled.
@interface MGHiddenField : UITextField
@end

@implementation MGHiddenField
- (BOOL)canBecomeFocused {
  return NO;
}
@end

// The axis a glide is locked to once it leaves the dead zone.
enum class GlideAxis : uint8_t { None, Sideways, UpDown };

@interface MGViewController : UIViewController <UITextFieldDelegate, UIGestureRecognizerDelegate>
@property(nonatomic, strong) UIView* videoView;
@property(nonatomic, strong) MGUIView* uiView;
@property(nonatomic, strong) MGHiddenField* field;
@property(nonatomic) BOOL fieldConfirmed;
@property(nonatomic, strong) NSTimer* repeatTimer;
@property(nonatomic) marigold::Key repeatKey;
// The current swipe gesture: its number, axis, direction (+1 right or down,
// -1 left or up, 0 not yet) and the moves it sent.
@property(nonatomic) uint32_t glideId;
@property(nonatomic) GlideAxis glideAxis;
@property(nonatomic) int glideDir;
@property(nonatomic) int glideSent;
// The pan itself travelled a step (a swipe recogniser's move alone never coasts).
@property(nonatomic) BOOL glidePanned;
@property(nonatomic) BOOL panTracking;
@property(nonatomic) CFTimeInterval panEndedAt;
// When a swipe came before its pan began (that pan keeps the swipe's gesture).
@property(nonatomic) CFTimeInterval swipeLeadAt;
@property(nonatomic, strong) NSTimer* coastTimer;
@property(nonatomic) int coastLeft;
@property(nonatomic) NSTimeInterval coastGap;
@end

static MGViewController* gController;

@implementation MGViewController

- (void)loadView {
  CGRect full = CGRectMake(0, 0, 1920, 1080);
  UIView* root = [[UIView alloc] initWithFrame:UIScreen.mainScreen.bounds];
  root.backgroundColor = UIColor.blackColor;

  // VLC draws into this view; the app moves and sizes it (Video::setWindow).
  self.videoView = [[UIView alloc] initWithFrame:full];
  self.videoView.backgroundColor = UIColor.blackColor;
  self.videoView.userInteractionEnabled = NO;
  self.videoView.hidden = YES;
  [root addSubview:self.videoView];

  // The UI over it: transparent where the app clears a hole for the video.
  self.uiView = [[MGUIView alloc] initWithFrame:full];
  self.uiView.backgroundColor = UIColor.clearColor;
  self.uiView.opaque = NO;
  self.uiView.userInteractionEnabled = NO;
  CAMetalLayer* layer = (CAMetalLayer*)self.uiView.layer;
  layer.device = MTLCreateSystemDefaultDevice();
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  layer.framebufferOnly = YES;
  layer.opaque = NO;
  layer.contentsScale = 1.0;  // drawn at 1080p and scaled by the TV, as on Android
  layer.drawableSize = CGSizeMake(1920, 1080);
  [root addSubview:self.uiView];

  // The system keyboard opens over the app for this field; it is never seen.
  self.field = [[MGHiddenField alloc] initWithFrame:CGRectMake(-100, -100, 10, 10)];
  self.field.alpha = 0.01;
  self.field.delegate = self;
  self.field.autocorrectionType = UITextAutocorrectionTypeNo;
  self.field.autocapitalizationType = UITextAutocapitalizationTypeNone;
  self.field.spellCheckingType = UITextSpellCheckingTypeNo;
  self.field.returnKeyType = UIReturnKeyDone;
  [self.field addTarget:self action:@selector(fieldChanged) forControlEvents:UIControlEventEditingChanged];
  [root addSubview:self.field];

  // Swipes on the touch surface move, as the arrows do (the first Siri
  // Remote has no clickpad ring to press).
  const UISwipeGestureRecognizerDirection directions[] = {
      UISwipeGestureRecognizerDirectionUp, UISwipeGestureRecognizerDirectionDown,
      UISwipeGestureRecognizerDirectionLeft, UISwipeGestureRecognizerDirectionRight};
  for (UISwipeGestureRecognizerDirection d : directions) {
    UISwipeGestureRecognizer* swipe = [[UISwipeGestureRecognizer alloc] initWithTarget:self
                                                                                action:@selector(swiped:)];
    swipe.direction = d;
    [root addGestureRecognizer:swipe];
  }
  // A pan on the same surface glides: a long swipe moves several steps and a
  // flick coasts on. Indirect touches are the remote's; presses stay with
  // pressesBegan. It runs alongside the swipes, which still give a quick
  // short swipe its one move (swiped:).
  UIPanGestureRecognizer* pan = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(panned:)];
  pan.allowedTouchTypes = @[ @(UITouchTypeIndirect) ];
  pan.allowedPressTypes = @[];
  pan.delegate = self;
  [root addGestureRecognizer:pan];
  MLOG_I("remote", "touch surface: a step per %.2f of it, dead zone %.2f, flicks coast %d-%d",
         static_cast<double>(kGlideStep), static_cast<double>(kGlideDeadZone), kCoastFewest, kCoastMost);
  self.view = root;
  // The 1920x1080 views fill a 4K screen the same way (tvOS lays out in points).
  [self layoutForScreen];
}

// The controller takes the remote's presses whenever the keyboard is closed.
- (BOOL)canBecomeFirstResponder {
  return YES;
}

- (void)viewDidAppear:(BOOL)animated {
  [super viewDidAppear:animated];
  [self becomeFirstResponder];
  if ([NSUserDefaults.standardUserDefaults boolForKey:@"MarigoldKeyboardSelfTest"]) [self keyboardSelfTest];
}

- (void)viewDidLayoutSubviews {
  [super viewDidLayoutSubviews];
  [self layoutForScreen];
}

- (void)layoutForScreen {
  CGSize size = self.view.bounds.size;
  if (size.width <= 0 || size.height <= 0) return;
  CGFloat sx = size.width / 1920.0, sy = size.height / 1080.0;
  self.uiView.transform = CGAffineTransformIdentity;
  self.uiView.frame = CGRectMake(0, 0, 1920, 1080);
  if (sx != 1.0 || sy != 1.0) {
    self.uiView.transform = CGAffineTransformMakeScale(sx, sy);
    self.uiView.center = CGPointMake(size.width / 2, size.height / 2);
  }
}

// ---- The remote ----

- (BOOL)handlePresses:(NSSet<UIPress*>*)presses down:(BOOL)down {
  BOOL used = NO;
  for (UIPress* p in presses) {
    marigold::Key key = marigold::keyFromTvos(static_cast<int>(p.type));
    if (key == marigold::Key::Unknown) continue;
    used = YES;
    // A press (Back among them) stops a coasting swipe.
    if (down) [self stopCoast];
    marigold::tvos::push(keyEvent(key, down, false));
    if (key == marigold::Key::Up || key == marigold::Key::Down || key == marigold::Key::Left ||
        key == marigold::Key::Right) {
      if (down) [self startRepeat:key];
      else if (key == self.repeatKey) [self stopRepeat];
    }
  }
  return used;
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
  // Back included: the app decides (Back on the guide goes Home, leaveToHome).
  if (![self handlePresses:presses down:YES]) [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
  if (![self handlePresses:presses down:NO]) [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event {
  if (![self handlePresses:presses down:NO]) [super pressesCancelled:presses withEvent:event];
}

// ---- Swipes and glides ----

// The pan and the swipes see the same touches.
- (BOOL)gestureRecognizer:(UIGestureRecognizer*)gestureRecognizer
    shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer*)otherGestureRecognizer {
  return YES;
}

// A recognised swipe is the floor: a quick short swipe the pan hasn't
// stepped for yet still moves once. One gesture never moves twice for it.
- (void)swiped:(UISwipeGestureRecognizer*)swipe {
  // While the system keyboard is up it has the remote.
  if (self.field.isFirstResponder) return;
  CFTimeInterval now = CACurrentMediaTime();
  if (!self.panTracking && now - self.panEndedAt > kSwipePanGap) {
    // No pan for this swipe (yet): it starts its own gesture.
    [self stopCoast];
    [self newGlide];
    self.swipeLeadAt = now;
  }
  if (self.glideSent > 0) return;
  UISwipeGestureRecognizerDirection d = swipe.direction;
  self.glideAxis = d == UISwipeGestureRecognizerDirectionUp || d == UISwipeGestureRecognizerDirectionDown
                       ? GlideAxis::UpDown
                       : GlideAxis::Sideways;
  self.glideDir = d == UISwipeGestureRecognizerDirectionUp || d == UISwipeGestureRecognizerDirectionLeft ? -1 : 1;
  [self glideMove];
}

- (void)panned:(UIPanGestureRecognizer*)pan {
  UIGestureRecognizerState state = pan.state;
  if (state == UIGestureRecognizerStateBegan) {
    [self stopCoast];
    self.panTracking = YES;
    if (CACurrentMediaTime() - self.swipeLeadAt > kSwipePanGap) [self newGlide];
    self.swipeLeadAt = 0;
  }
  if (state == UIGestureRecognizerStateBegan || state == UIGestureRecognizerStateChanged) {
    if (!self.field.isFirstResponder) [self panMovedBy:[pan translationInView:pan.view]];
    return;
  }
  if (state != UIGestureRecognizerStateEnded && state != UIGestureRecognizerStateCancelled &&
      state != UIGestureRecognizerStateFailed)
    return;
  self.panTracking = NO;
  self.panEndedAt = CACurrentMediaTime();
  // A flick coasts on, the way it was going.
  if (state != UIGestureRecognizerStateEnded || self.field.isFirstResponder || !self.glidePanned) return;
  CGPoint v = [pan velocityInView:pan.view];
  CGSize size = [self glideSurface];
  CGFloat speed = (self.glideAxis == GlideAxis::Sideways ? v.x / size.width : v.y / size.height) * self.glideDir;
  if (speed < kCoastSpeed) return;
  CGFloat t = (speed - kCoastSpeed) / (kCoastSpeedMost - kCoastSpeed);
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  [self startCoast:kCoastFewest + static_cast<int>(std::lround(t * (kCoastMost - kCoastFewest)))];
}

// The pan's travel so far: locks the axis past the dead zone and the
// direction at the first step, then sends a move per step.
- (void)panMovedBy:(CGPoint)travel {
  CGSize size = [self glideSurface];
  CGFloat x = travel.x / size.width, y = travel.y / size.height;
  if (self.glideAxis == GlideAxis::None) {
    if (std::fabs(x) < kGlideDeadZone && std::fabs(y) < kGlideDeadZone) return;
    self.glideAxis = std::fabs(x) >= std::fabs(y) ? GlideAxis::Sideways : GlideAxis::UpDown;
  }
  CGFloat along = self.glideAxis == GlideAxis::Sideways ? x : y;
  if (self.glideDir == 0) {
    if (std::fabs(along) < kGlideDeadZone + kGlideStep) return;
    self.glideDir = along > 0 ? 1 : -1;
  }
  CGFloat past = along * self.glideDir - kGlideDeadZone;
  if (past < kGlideStep) return;
  self.glidePanned = YES;
  int steps = static_cast<int>(past / kGlideStep);
  while (self.glideSent < steps) [self glideMove];
}

// The remote's touch surface, as the view it maps onto.
- (CGSize)glideSurface {
  CGSize size = self.view.bounds.size;
  if (size.width <= 0 || size.height <= 0) size = CGSizeMake(1920, 1080);
  return size;
}

- (void)newGlide {
  self.glideId = nextGlide();
  self.glideAxis = GlideAxis::None;
  self.glideDir = 0;
  self.glideSent = 0;
  self.glidePanned = NO;
}

// One move of the current gesture. Marked as a swipe: the player only takes
// them sideways while its controls are up (a slanted swipe along the buttons
// isn't a flip). The glide number lets a grid stop the glide at its edge.
- (void)glideMove {
  if (self.glideAxis == GlideAxis::None || self.glideDir == 0) return;
  marigold::Key key = self.glideAxis == GlideAxis::Sideways
                          ? (self.glideDir > 0 ? marigold::Key::Right : marigold::Key::Left)
                          : (self.glideDir > 0 ? marigold::Key::Down : marigold::Key::Up);
  marigold::platform::Event down = keyEvent(key, true, false);
  down.swipe = true;
  down.glide = self.glideId;
  marigold::tvos::push(down);
  marigold::tvos::push(keyEvent(key, false, false));
  self.glideSent += 1;
}

- (void)startCoast:(int)steps {
  [self stopCoast];
  self.coastLeft = steps;
  self.coastGap = kCoastFirst;
  [self scheduleCoast];
}

- (void)scheduleCoast {
  __weak MGViewController* weakSelf = self;
  self.coastTimer = [NSTimer scheduledTimerWithTimeInterval:self.coastGap
                                                    repeats:NO
                                                      block:^(NSTimer*) {
                                                        [weakSelf coastNow];
                                                      }];
}

- (void)coastNow {
  self.coastTimer = nil;
  if (self.coastLeft <= 0 || self.field.isFirstResponder) {
    [self stopCoast];
    return;
  }
  self.coastLeft -= 1;
  [self glideMove];
  if (self.coastLeft > 0) {
    self.coastGap *= kCoastSlowing;
    [self scheduleCoast];
  }
}

- (void)stopCoast {
  [self.coastTimer invalidate];
  self.coastTimer = nil;
  self.coastLeft = 0;
}

- (void)startRepeat:(marigold::Key)key {
  [self stopRepeat];
  self.repeatKey = key;
  __weak MGViewController* weakSelf = self;
  self.repeatTimer = [NSTimer scheduledTimerWithTimeInterval:kRepeatDelay
                                                     repeats:NO
                                                       block:^(NSTimer*) {
                                                         [weakSelf repeatNow];
                                                       }];
}

- (void)repeatNow {
  marigold::tvos::push(keyEvent(self.repeatKey, true, true));
  __weak MGViewController* weakSelf = self;
  self.repeatTimer = [NSTimer scheduledTimerWithTimeInterval:kRepeatEvery
                                                     repeats:YES
                                                       block:^(NSTimer*) {
                                                         MGViewController* me = weakSelf;
                                                         if (me) marigold::tvos::push(keyEvent(me.repeatKey, true, true));
                                                       }];
}

- (void)stopRepeat {
  [self.repeatTimer invalidate];
  self.repeatTimer = nil;
  self.repeatKey = marigold::Key::Unknown;
}

// ---- The system keyboard ----

- (void)openKeyboard:(NSString*)value {
  [self stopRepeat];
  [self stopCoast];
  self.fieldConfirmed = NO;
  self.field.text = value;
  if ([self.field becomeFirstResponder]) {
    MLOG_I("keyboard", "open");
  } else {
    // The app must not wait for a keyboard that isn't there.
    MLOG_W("keyboard", "the system keyboard didn't open");
    [self sendText:YES];
  }
}

- (void)closeKeyboard {
  if (self.field.isFirstResponder) [self.field resignFirstResponder];
}

- (void)sendText:(BOOL)done {
  marigold::platform::Event e;
  e.type = marigold::platform::Event::Type::Text;
  e.text = utf8(self.field.text);
  e.done = done;
  e.confirmed = done && self.fieldConfirmed;
  marigold::tvos::push(std::move(e));
}

- (void)fieldChanged {
  [self sendText:NO];
}

- (BOOL)textFieldShouldReturn:(UITextField*)textField {
  self.fieldConfirmed = YES;
  [textField resignFirstResponder];
  return NO;
}

- (void)textFieldDidEndEditing:(UITextField*)textField {
  [self sendText:YES];
  MLOG_I("keyboard", "closed");
  // The remote's presses come back to the app, for the next field.
  dispatch_async(dispatch_get_main_queue(), ^{
    [self becomeFirstResponder];
  });
}

// Started with -MarigoldKeyboardSelfTest YES (tools/tvos-smoke.sh): opens
// and closes the keyboard twice and checks the field never keeps the focus.
- (void)keyboardSelfTest {
  __weak MGViewController* weakSelf = self;
  auto after = [](double seconds, void (^block)(void)) {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(seconds * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), block);
  };
  __block bool ok = true;
  // False when the field has the focus (the bug this guards against).
  auto check = ^bool(const char* step) {
    MGViewController* me = weakSelf;
    if (!me) return false;
    UIFocusSystem* focus = [UIFocusSystem focusSystemForEnvironment:me];
    bool fieldFocused = focus.focusedItem == me.field;
    bool fieldEditing = me.field.isFirstResponder;
    MLOG_I("keyboard", "self-test %s: field editing %d, field focused %d, app has the remote %d", step,
           fieldEditing ? 1 : 0, fieldFocused ? 1 : 0, me.isFirstResponder ? 1 : 0);
    return !fieldFocused;
  };
  after(4, ^{ [weakSelf openKeyboard:@"first"]; });
  after(7, ^{
    ok = check("first open") && ok;
    ok = ok && weakSelf.field.isFirstResponder;
    [weakSelf closeKeyboard];
  });
  after(10, ^{
    ok = check("first close") && ok;
    ok = ok && !weakSelf.field.isFirstResponder && weakSelf.isFirstResponder;
    [weakSelf openKeyboard:@"second"];
  });
  after(13, ^{
    ok = check("second open") && ok;
    ok = ok && weakSelf.field.isFirstResponder;
    [weakSelf closeKeyboard];
  });
  after(16, ^{
    ok = check("second close") && ok;
    ok = ok && !weakSelf.field.isFirstResponder && weakSelf.isFirstResponder;
    if (ok) MLOG_I("keyboard", "self-test: ok");
    else MLOG_W("keyboard", "self-test: FAILED");
  });
}

@end

// ---- The scene and the app ----

@interface MGSceneDelegate : UIResponder <UIWindowSceneDelegate>
@property(nonatomic, strong) UIWindow* window;
@end

@implementation MGSceneDelegate

- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session options:(UISceneConnectionOptions*)options {
  UIWindowScene* windowScene = (UIWindowScene*)scene;
  self.window = [[UIWindow alloc] initWithWindowScene:windowScene];
  gController = [[MGViewController alloc] init];
  self.window.rootViewController = gController;
  [self.window makeKeyAndVisible];
  [gController loadViewIfNeeded];

  static bool started = false;
  if (started) return;  // a new scene in the same process: the app is still running
  started = true;
  std::string assets = utf8([NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"assets"]);
  // Only Caches is writable on an Apple TV; Storage keeps what the user made
  // elsewhere too (storage.mm).
  NSString* caches = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES).firstObject;
  std::thread(appThread, assets, utf8(caches)).detach();
}

- (void)send:(marigold::platform::Event::Type)type {
  marigold::platform::Event e;
  e.type = type;
  marigold::tvos::push(std::move(e));
}

- (void)sceneDidBecomeActive:(UIScene*)scene {
  [self send:marigold::platform::Event::Type::Shown];
}

- (void)sceneWillResignActive:(UIScene*)scene {
  [gController stopRepeat];
  [gController stopCoast];
  [self send:marigold::platform::Event::Type::Hidden];
}

- (void)sceneDidEnterBackground:(UIScene*)scene {
  // A few seconds for the app thread to stop the video and save before the
  // system suspends the process.
  [gController stopCoast];
  __block UIBackgroundTaskIdentifier task = UIBackgroundTaskInvalid;
  task = [UIApplication.sharedApplication beginBackgroundTaskWithExpirationHandler:^{
    [UIApplication.sharedApplication endBackgroundTask:task];
    task = UIBackgroundTaskInvalid;
  }];
  [self send:marigold::platform::Event::Type::Background];
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
    if (task != UIBackgroundTaskInvalid) [UIApplication.sharedApplication endBackgroundTask:task];
    task = UIBackgroundTaskInvalid;
  });
}

@end

@interface MGAppDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation MGAppDelegate

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options {
  UISceneConfiguration* config = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
  config.delegateClass = [MGSceneDelegate class];
  return config;
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

CAMetalLayer* uiLayer() { return gController ? (CAMetalLayer*)gController.uiView.layer : nil; }
UIView* videoView() { return gController.videoView; }

void openKeyboard(const std::string& value) {
  NSString* v = [NSString stringWithUTF8String:value.c_str()] ?: @"";
  onMain([v] { [gController openKeyboard:v]; });
}

void closeKeyboard() {
  onMain([] { [gController closeKeyboard]; });
}

void leaveToHome() {
  onMain([] {
    // What the system does for Back on an app's top screen. (An App Store
    // app would hand the press back to UIKit instead; this app is installed
    // directly.)
    UIApplication* app = UIApplication.sharedApplication;
    SEL suspend = NSSelectorFromString(@"suspend");
    if ([app respondsToSelector:suspend]) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
      [app performSelector:suspend];
#pragma clang diagnostic pop
    } else {
      MLOG_W("window", "can't go to the Home screen");
    }
  });
}

}  // namespace marigold::tvos

int main(int argc, char* argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([MGAppDelegate class]));
  }
}
