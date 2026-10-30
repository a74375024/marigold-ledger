// Program detail card for a show that has not started (tvOS
// LiveProgramDetailCard: logo, LATER badge, title, summary, overview, Remind
// Me and Watch pills, and the show's other airings).
#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/model/channel.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

class DetailScreen : public Screen {
 public:
  DetailScreen(App& app, std::string entryId, Listing l)
      : Screen(app), entry_(std::move(entryId)), listing_(std::move(l)) {
    airings_ = app_.store().otherAirings(entry_, listing_);
  }

  bool opaque() const override { return false; }

  bool key(const KeyEvent& e) override {
    int rows = static_cast<int>(airings_.size());
    switch (e.key) {
      case Key::Left:
        if (focus_ == 1) focus_ = 0;
        return true;
      case Key::Right:
        if (focus_ == 0) focus_ = 1;
        return true;
      case Key::Down:
        if (focus_ < 2 && rows > 0) focus_ = 2;
        else if (focus_ >= 2 && focus_ - 2 + 1 < rows) ++focus_;
        return true;
      case Key::Up:
        if (focus_ > 2) --focus_;
        else if (focus_ == 2) focus_ = 0;
        return true;
      case Key::Ok:
        if (focus_ == 0) {
          bool on = app_.store().toggleShowReminder(entry_, listing_);
          app_.toast(on ? "We'll remind you when it starts" : "Reminder removed");
        } else if (focus_ == 1) {
          watch();
        } else if (focus_ - 2 < rows) {
          const ProgramHit& h = airings_[static_cast<size_t>(focus_ - 2)];
          app_.store().toggleShowReminder(h.entryId, h.listing);
        }
        return true;
      case Key::Back: return false;
      default: return true;
    }
  }

  void hover(int id) override {
    if (id >= 1 && id < 2 + static_cast<int>(airings_.size()) + 1) focus_ = id - 1;
  }

  void draw(ui::Ctx& c) override {
    ui::scrim(c, 0.6f);
    float extra = airings_.empty() ? 0 : 60 + 70 * static_cast<float>(airings_.size());
    float h = 640 + extra;
    Rect card{(gfx::kLayoutWidth - 1100) * 0.5f, std::max(40.0f, (gfx::kLayoutHeight - h) * 0.5f), 1100, h};
    c.g.fill(card, theme::hex(0x1C1F25, 0.98f), 34);
    Store& s = app_.store();
    auto ch = s.channels();
    const GuideChannel* g = ch->find(entry_);
    float x = card.x + 60, y = card.y + 50, w = card.w - 120;
    if (g) ui::logo(c, g->logoUrl, g->name, {x, y, 150, 60});
    ui::chip(c, "LATER", x + w - 90, y + 16, 18, theme::white(0.6f), theme::white(0.85f), true);
    y += 90;
    ui::label(c, listing_.title, x, y, Weight::Bold, 46, theme::white(), w);
    y += 70;
    ui::label(c, summary(c, g ? g->name : std::string(), listing_), x, y, Weight::Regular, 24, theme::white(0.75f), w);
    y += 48;
    if (!listing_.episode.empty()) {
      ui::label(c, listing_.episode, x, y, Weight::Medium, 22, theme::white(0.85f), w);
      y += 38;
    }
    ui::paragraph(c, listing_.overview, x, y, w, Weight::Regular, 22, theme::white(0.7f), 3);

    // Remind Me, Watch.
    float py = card.y + 470;
    bool reminded = s.hasShowReminder(entry_, listing_.start);
    std::string remind = reminded ? "Reminder Set" : "Remind Me";
    std::string watchText = "Watch " + (g ? g->name : std::string("channel")) + " Now";
    float px = x;
    int i = 0;
    for (const std::string& text : {remind, watchText}) {
      float pw = ui::chipWidth(c, text, 24) + 40;
      Rect pill{px, py, pw, 70};
      c.hit(pill, 1 + i);
      bool f = focus_ == i;
      c.g.fill(pill, f ? theme::white() : theme::white(0.12f), 35);
      ui::labelIn(c, text, pill, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      px += pw + 16;
      ++i;
    }
    float ay = py + 100;
    if (!airings_.empty()) {
      ui::label(c, "OTHER AIRINGS", x, ay, Weight::Bold, 16, theme::white(0.55f));
      ay += 34;
      for (size_t k = 0; k < airings_.size(); ++k) {
        const ProgramHit& hgt = airings_[k];
        const GuideChannel* ag = ch->find(hgt.entryId);
        Rect row{x, ay, w, 62};
        c.hit(row, 3 + static_cast<int>(k));
        bool f = focus_ == 2 + static_cast<int>(k);
        c.g.fill(row, f ? theme::white() : theme::white(0.07f), theme::kRowRadius);
        Color fg = f ? theme::black() : theme::white();
        ui::label(c, ag ? ag->name : std::string(), row.x + 24, row.y + 16, Weight::Bold, 23, fg, 360);
        std::string day = ui::dayWord(c, hgt.listing.start);
        ui::label(c, (day.empty() ? "Today" : day) + " \xc2\xb7 " + ui::range(c, hgt.listing.start, hgt.listing.end, true),
                  row.x + 400, row.y + 18, Weight::Medium, 21, f ? theme::black(0.7f) : theme::white(0.7f), 420);
        if (s.hasShowReminder(hgt.entryId, hgt.listing.start))
          ui::labelIn(c, "Reminder Set", {row.x, row.y, row.w - 24, row.h}, Weight::Bold, 20, fg, Align::Right);
        ay += 70;
      }
    }
    ui::label(c, "A reminder shows over whatever you're watching when the show starts, with one click to switch.", x,
              card.y + card.h - 54, Weight::Regular, 18, theme::white(0.5f), w);
  }

 private:
  static std::string summary(ui::Ctx& c, const std::string& channel, const Listing& l) {
    std::string out = channel;
    std::string day = ui::dayWord(c, l.start);
    out += " \xc2\xb7 " + (day.empty() ? std::string() : day + " ") + ui::range(c, l.start, l.end, true);
    if (l.genres & kGenreSports) out += " \xc2\xb7 Sports";
    else if (l.genres & kGenreMovie) out += " \xc2\xb7 Movie";
    else if (l.genres & kGenreNews) out += " \xc2\xb7 News";
    if (l.live) out += " \xc2\xb7 Live";
    return out;
  }

  void watch() {
    app_.nav().pop();
    Playback& p = app_.playback();
    // From the guide in the player: tunes there (the guide closes itself),
    // no second player.
    bool watching = p.active() && p.full();
    if (p.tune(entry_)) {
      p.setFull();
      if (!watching) app_.nav().push(makePlayer(app_));
    } else {
      app_.toast("This channel has no working feed right now");
    }
  }

  std::string entry_;
  Listing listing_;
  std::vector<ProgramHit> airings_;
  int focus_ = 0;  // 0 Remind Me, 1 Watch, 2.. other airings
};

}  // namespace

std::unique_ptr<Screen> makeProgramDetail(App& app, const std::string& entryId, const Listing& l) {
  return std::make_unique<DetailScreen>(app, entryId, l);
}

}  // namespace marigold::app
