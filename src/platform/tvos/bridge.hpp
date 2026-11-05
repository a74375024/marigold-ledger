// tvOS and macOS: what the backends share inside this folder. UIKit (AppKit
// on the Mac) lives on the main thread (main.mm: the scene or window, its
// views and the system keyboard or text field); the app runs on its own
// thread, as on Android, and reaches the main thread only through these
// calls. The Mac app's main.mm is src/platform/macos/main.mm.
#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include <functional>
#include <string>

#include "platform/window.hpp"

#ifdef __OBJC__
@class CAMetalLayer;
@class NSURL;
#if TARGET_OS_OSX
@class NSView;
using MGView = NSView;
#else
@class UIView;
using MGView = UIView;
#endif
#else
using CAMetalLayer = void;
using NSURL = void;
using MGView = void;
#endif

namespace marigold::tvos {

// Any thread: queues an event for the app thread (Window::nextEvent).
void push(platform::Event e);

// Runs `f` on the main thread and returns at once.
void onMain(std::function<void()> f);
// Runs `f` on the main thread and waits for it (never from the main thread).
void onMainSync(const std::function<void()>& f);

// The views, made by the view controller before the app thread starts.
// The UI layer (CAMetalLayer, 1920x1080, transparent) sits over the video
// view, which VLC draws into.
CAMetalLayer* uiLayer();
MGView* videoView();

// The system keyboard over the app (a hidden text field). Text events carry
// the whole value as it changes, then `done`.
void openKeyboard(const std::string& value);
void closeKeyboard();

// A link as NSURL, encoding what a URL can't hold (spaces and the like, which
// lists carry and other players accept). Nil when it isn't a link.
NSURL* urlFrom(const std::string& link);

// The Back button on the guide: the app goes to the Home screen, as the
// system does at the top screen (on the Mac: leaves full screen).
void leaveToHome();

#if TARGET_OS_OSX
// The Mac: where the app placed the video, in its 1920x1080 points (the
// window scales them to its size).
void placeVideo(double x, double y, double w, double h);
#endif

}  // namespace marigold::tvos
