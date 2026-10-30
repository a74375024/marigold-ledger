// Screens and the stack that holds them. The top screen gets the keys; a
// screen that is not opaque (a panel, a card) is drawn over the ones below.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/ui/ui.hpp"
#include "core/input/keys.hpp"
#include "platform/window.hpp"

namespace marigold::app {

class App;

struct KeyEvent {
  Key key = Key::Unknown;
  bool repeat = false;     // held down (arrows auto-repeat)
  bool longPress = false;  // OK held for 700 ms
  bool wheel = false;      // Up/Down from the pointer's wheel (LG)
  bool swipe = false;      // an arrow from a clickpad swipe (Apple TV)
};

class Screen {
 public:
  explicit Screen(App& app) : app_(app) {}
  virtual ~Screen() = default;
  Screen(const Screen&) = delete;
  Screen& operator=(const Screen&) = delete;

  // True when the key was used. Back that is not used pops the screen.
  virtual bool key(const KeyEvent& e) = 0;
  virtual void draw(ui::Ctx& c) = 0;
  // Pointer moved over a region this screen registered.
  virtual void hover(int /*id*/) {}
  // The system keyboard sent the whole field value (or closed).
  virtual void text(const std::string& /*value*/, bool /*closed*/, bool /*confirmed*/) {}
  // Called on the top screen once per loop; true while animating (the app
  // then draws every frame instead of waiting for events).
  virtual bool tick(uint32_t /*ms*/) { return false; }
  virtual bool opaque() const { return true; }
  // A corner preview may keep playing under this screen (the guide). Under
  // any other full screen it stops.
  virtual bool keepsPreview() const { return false; }
  // Pushed or uncovered again.
  virtual void shown() {}
  // Every event before the app's own handling (the webOS key test screen
  // reads its native SDL event). True when used.
  virtual bool rawEvent(const platform::Event& /*e*/) { return false; }
  // OK held does something here (a hold menu): OK then acts on release, to
  // tell a press from a hold. Elsewhere it acts as it is pressed.
  virtual bool holdsOk() const { return false; }

 protected:
  App& app_;
};

class Navigator {
 public:
  void push(std::unique_ptr<Screen> s);
  // Pops the top screen at the end of the current event (safe from key()).
  void pop();
  void popTo(const Screen* s);  // pops everything above s
  void replaceAll(std::unique_ptr<Screen> s);
  Screen* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }
  // The topmost opaque screen: what fills the screen under any panels.
  const Screen* topOpaque() const;
  bool empty() const { return stack_.empty(); }
  size_t size() const { return stack_.size(); }

  // App loop: draws from the last opaque screen up.
  void draw(ui::Ctx& c);
  // Applies queued pops; returns true if the stack changed.
  bool settle();

 private:
  std::vector<std::unique_ptr<Screen>> stack_;
  std::vector<std::unique_ptr<Screen>> graveyard_;  // popped, freed after the event
  int pendingPops_ = 0;
  const Screen* popTarget_ = nullptr;
  std::unique_ptr<Screen> pendingReplace_;
  std::vector<std::unique_ptr<Screen>> pendingPush_;
};

}  // namespace marigold::app
