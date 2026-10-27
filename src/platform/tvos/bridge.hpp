// tvOS: what the backends share inside this folder. UIKit lives on the main
// thread (main.mm: the scene, the view controller, its views and the system
// keyboard); the app runs on its own thread, as on Android, and reaches the
// main thread only through these calls.
#pragma once

#include <functional>
#include <string>

#include "platform/window.hpp"

#ifdef __OBJC__
@class CAMetalLayer;
@class NSURL;
@class UIView;
#else
using CAMetalLayer = void;
using NSURL = void;
using UIView = void;
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
UIView* videoView();

// The system keyboard over the app (a hidden text field). Text events carry
// the whole value as it changes, then `done`.
void openKeyboard(const std::string& value);
void closeKeyboard();

// A link as NSURL, encoding what a URL can't hold (spaces and the like, which
// lists carry and other players accept). Nil when it isn't a link.
NSURL* urlFrom(const std::string& link);

// The Back button on the guide: the app goes to the Home screen, as the
// system does at the top screen.
void leaveToHome();

}  // namespace marigold::tvos
