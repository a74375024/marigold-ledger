// Channel search (full screen). Scoring follows the tvOS search: exact 95,
// prefix 80, call-sign prefix 75, word prefix 70, contains 50, neighborhood
// 20; +5 favourite, +10 in My Lineup; at most 80 results.
#include <algorithm>

#include "app/app.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

constexpr size_t kMaxResults = 80;
constexpr float kRowH = 84;

std::string fold(const std::string& s) { return str::lowerAscii(str::foldDiacritics(s)); }

bool wordPrefix(const std::string& hay, const std::string& q) {
  for (size_t i = 0; i < hay.size(); ++i) {
    if ((i == 0 || hay[i - 1] == ' ' || hay[i - 1] == '-' || hay[i - 1] == '(') && hay.compare(i, q.size(), q) == 0)
      return true;
  }
  return false;
}

class SearchScreen : public Screen {
 public:
  SearchScreen(App& app, std::function<void(const std::string&)> pick) : Screen(app), pick_(std::move(pick)) {}

  void shown() override {
    if (!openedOnce_) {
      openedOnce_ = true;
      if (!app_.openKeyboard(query_)) app_.toast("This TV has no on-screen keyboard");
    }
  }

  void text(const std::string& value, bool closed, bool) override {
    if (value != query_) {
      query_ = value;
      search();
    }
    if (closed && !results_.empty()) focus_ = 0;
  }

  bool key(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Up:
        if (focus_ >= 0) --focus_;
        return true;
      case Key::Down:
        if (focus_ + 1 < static_cast<int>(results_.size())) ++focus_;
        return true;
      case Key::Ok:
        if (focus_ < 0) {
          app_.openKeyboard(query_);
        } else if (focus_ < static_cast<int>(results_.size())) {
          std::string id = results_[static_cast<size_t>(focus_)];
          auto pick = pick_;
          app_.nav().pop();
          if (pick) pick(id);
        }
        return true;
      case Key::Back:
        if (app_.keyboardOpen()) {
          app_.closeKeyboard();
          return true;
        }
        return false;
      default:
        return true;
    }
  }

  void hover(int id) override {
    if (id == 1) focus_ = -1;
    else if (id >= 100 && id - 100 < static_cast<int>(results_.size())) focus_ = id - 100;
  }

  void draw(ui::Ctx& c) override {
    float x = 80, y = 60;
    ui::label(c, "Search", x, y, Weight::Bold, 44, theme::white());
    y += 80;
    Rect field{x, y, gfx::kLayoutWidth - 160, 76};
    c.hit(field, 1);
    bool ff = focus_ < 0;
    c.g.fill(field, ff ? theme::white() : theme::white(0.1f), 38);
    std::string shown = query_.empty() ? "Channel name or call sign" : query_;
    ui::labelIn(c, shown, {field.x + 36, field.y, field.w - 72, field.h}, Weight::Medium, 28,
                ff ? theme::black(query_.empty() ? 0.45f : 1) : theme::white(query_.empty() ? 0.5f : 1));
    y += 110;
    if (query_.size() < 1) {
      ui::label(c, "Type with the TV keyboard; results appear as you type.", x, y, Weight::Regular, 24,
                theme::white(0.6f));
      return;
    }
    ui::label(c, "CHANNELS (" + std::to_string(results_.size()) + ")", x, y, Weight::Bold, 16, theme::white(0.55f));
    y += 34;
    auto ch = app_.store().channels();
    auto li = app_.store().listings();
    float visibleH = gfx::kLayoutHeight - y - 60;
    int perPage = static_cast<int>(visibleH / (kRowH + 8));
    int first = std::clamp(focus_ - perPage / 2, 0, std::max(0, static_cast<int>(results_.size()) - perPage));
    for (int i = first; i < static_cast<int>(results_.size()) && i < first + perPage; ++i) {
      const GuideChannel* g = ch->find(results_[static_cast<size_t>(i)]);
      if (!g) continue;
      Rect r{x, y, gfx::kLayoutWidth - 160, kRowH};
      c.hit(r, 100 + i);
      bool f = i == focus_;
      c.g.fill(r, f ? theme::white() : theme::white(0.07f), theme::kRowRadius);
      ui::logo(c, g->logoUrl, g->name, {r.x + 20, r.y + 14, 100, 56}, f);
      ui::label(c, g->name, r.x + 150, r.y + 12, Weight::Bold, 25, f ? theme::black() : theme::white(), r.w - 180);
      std::string sub = g->inLineup ? "My Lineup \xc2\xb7 " + g->section : g->section;
      if (g->feeds > 1) sub += " \xc2\xb7 " + std::to_string(g->feeds) + " feeds";
      if (const std::vector<Listing>* v = li->forChannel(g->id)) {
        for (const Listing& l : *v) {
          if (l.isOn(c.now)) {
            sub += " \xc2\xb7 Now: " + l.title;
            break;
          }
        }
      }
      ui::label(c, sub, r.x + 150, r.y + 48, Weight::Medium, 19, f ? theme::black(0.6f) : theme::white(0.6f), r.w - 180);
      y += kRowH + 8;
    }
  }

 private:
  void search() {
    results_.clear();
    focus_ = -1;
    std::string q = fold(std::string(str::trim(query_)));
    if (q.empty()) return;
    auto ch = app_.store().channels();
    std::vector<std::pair<int, int>> scored;  // score, index
    for (size_t i = 0; i < ch->all.size(); ++i) {
      const GuideChannel& g = ch->all[i];
      std::string name = fold(g.name);
      int score = 0;
      if (name == q) score = 95;
      else if (name.compare(0, q.size(), q) == 0) score = 80;
      else if (g.style == GuideChannel::Style::Local && fold(g.subtitle).compare(0, q.size(), q) == 0) score = 75;
      else if (wordPrefix(name, q)) score = 70;
      else if (name.find(q) != std::string::npos) score = 50;
      else if (fold(g.section).find(q) != std::string::npos) score = 20;
      if (!score) continue;
      if (g.favorite) score += 5;
      if (g.inLineup) score += 10;
      scored.push_back({score, static_cast<int>(i)});
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < scored.size() && i < kMaxResults; ++i)
      results_.push_back(ch->all[static_cast<size_t>(scored[i].second)].id);
  }

  std::function<void(const std::string&)> pick_;
  std::string query_;
  std::vector<std::string> results_;
  int focus_ = -1;  // -1 = the field
  bool openedOnce_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeSearch(App& app, std::function<void(const std::string&)> pick) {
  return std::make_unique<SearchScreen>(app, std::move(pick));
}

}  // namespace marigold::app
