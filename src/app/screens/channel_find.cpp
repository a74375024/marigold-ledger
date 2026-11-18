// Build 181, "Which channel is it?" › Find the channel… (screens.hpp,
// makeChannelFind): Search's keyboard on the left (app/ui/keyboard, its
// keys core/input/search_keys), and on the right the channels whose names
// hold every word typed, My Lineup first. OK on one picks it; Back leaves
// without one. Never a count.
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/app.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/keyboard.hpp"
#include "app/ui/panel.hpp"
#include "core/engine/replay.hpp"
#include "core/input/search_keys.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

namespace kb = ui::keyboard;
using search_keys::Grid;

constexpr int kHitKeys = 1;         // + the keyboard's ids
constexpr int kHitRows = ui::ListPanel::kHitBase;  // the results' rows (after the keys')
constexpr size_t kShownAtMost = 40;  // the first ones, best first

class ChannelFind : public Screen {
 public:
  ChannelFind(App& app, std::string heading, std::string exclude, std::function<void(const std::string&)> picked)
      : Screen(app), heading_(std::move(heading)), exclude_(std::move(exclude)), picked_(std::move(picked)) {
    RemoteKeys platform = app_.window().remoteKeys();
    grid_.setSpeak(app_.window().keyboardAvailable() && !(platform == RemoteKeys::fireTv()) && !platform.typing);
    find();
  }

  const char* name() const override { return "channel find"; }

  bool key(const KeyEvent& e) override {
    if (e.key == Key::Back) return false;  // pops: no channel named
    return results_ ? resultsKey(e) : keyboardKey(e);
  }

  void hover(int id) override {
    Grid::Spot spot;
    if (kb::spotFor(id, kHitKeys, grid_.page(), spot)) {
      grid_.set(spot);
      results_ = false;
      return;
    }
    if (id >= kHitRows && !ids_.empty()) {
      results_ = true;
      list_.hover(id);
    }
  }

  void text(const std::string& value, bool closed, bool) override {
    // The TV's own keyboard (🎤 Speak): its text into the field.
    if (value != query_) setQuery(value);
    if (closed) grid_.set({Grid::Spot::Area::Speak, 0, 0});
  }

  bool typed(const std::string& ch) override {
    if (ch.empty()) return false;
    if (ch == "\b") {
      if (query_.empty()) return false;
      setQuery(search_keys::dropLast(query_));
      return true;
    }
    if (static_cast<unsigned char>(ch[0]) < 0x20) return false;
    setQuery(query_ + ch);
    return true;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, ui::theme::hex(0x0E1117));
    kb::Look look;
    look.query = query_;
    look.focused = !results_;
    look.tab = !ids_.empty();
    kb::draw(c, grid_, look, kHitKeys);
    ui::Rect r{840, 48, gfx::kLayoutWidth - 840 - 96, gfx::kLayoutHeight - 96};
    list_.draw(c, r, kHitRows, results_);
  }

 private:
  bool keyboardKey(const KeyEvent& e) {
    switch (e.key) {
      case Key::Up:
      case Key::Down:
      case Key::Left:
      case Key::Right: {
        Grid::Move m = grid_.move(e.key, e.glide);
        if (m.edge == Grid::Edge::Right && !ids_.empty()) results_ = true;
        return true;
      }
      case Key::Ok: {
        search_keys::Press p = search_keys::press(grid_, query_);
        if (p.what == search_keys::Press::Do::Speak) app_.openKeyboard(query_);
        else if (p.what == search_keys::Press::Do::Text) setQuery(p.query);
        return true;
      }
      default:
        return true;
    }
  }

  bool resultsKey(const KeyEvent& e) {
    if (e.key == Key::Left) {
      results_ = false;
      return true;
    }
    return list_.key(e.key) || true;
  }

  void setQuery(const std::string& q) {
    query_ = q;
    find();
  }

  // Every word typed, in a channel's name (letters and digits, any case).
  void find() {
    std::vector<std::string> words;
    for (std::string_view w : str::split(query_, ' ')) {
      std::string k = engine::replay::titleKey(w);
      if (!k.empty()) words.push_back(k);
    }
    ids_.clear();
    std::vector<std::string> others;
    std::shared_ptr<const Channels> ch = app_.store().channels();
    if (!words.empty()) {
      for (const GuideChannel& g : ch->all) {
        if (g.id == exclude_ || g.radio) continue;
        std::string name = engine::replay::titleKey(g.name);
        bool all = std::all_of(words.begin(), words.end(),
                               [&](const std::string& w) { return name.find(w) != std::string::npos; });
        if (!all) continue;
        (g.inLineup ? ids_ : others).push_back(g.id);
      }
      ids_.insert(ids_.end(), others.begin(), others.end());
      if (ids_.size() > kShownAtMost) ids_.resize(kShownAtMost);
    }
    list_.items.clear();
    list_.subtitle = heading_;
    list_.title = "Find the channel";
    if (words.empty()) {
      list_.items.push_back(ui::PanelItem::note("Type a few letters of its name."));
    } else if (ids_.empty()) {
      list_.items.push_back(ui::PanelItem::note("No channel by that name."));
      results_ = false;
    }
    for (const std::string& id : ids_) {
      const GuideChannel* g = ch->find(id);
      if (!g) continue;
      ui::PanelItem it{ui::PanelItem::Kind::Action, g->name};
      it.detail = g->inLineup ? "In My Lineup" : g->section;
      std::string picked = id;
      it.select = [this, picked] {
        // Its panel takes it from here (and closes this screen).
        auto done = picked_;
        done(picked);
      };
      list_.items.push_back(it);
    }
    list_.focusFirst();
  }

  std::string heading_, exclude_;
  std::function<void(const std::string&)> picked_;
  Grid grid_;
  std::string query_;
  std::vector<std::string> ids_;
  ui::ListPanel list_;
  bool results_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeChannelFind(App& app, std::string heading, std::string exclude,
                                        std::function<void(const std::string&)> picked) {
  return std::make_unique<ChannelFind>(app, std::move(heading), std::move(exclude), std::move(picked));
}

}  // namespace marigold::app
