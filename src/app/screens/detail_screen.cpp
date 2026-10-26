// Program detail card for a show that has not started (tvOS: centred card,
// logo, LATER badge, title, summary, overview, a Watch pill).
#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/model/channel.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

class DetailScreen : public Screen {
 public:
  DetailScreen(App& app, std::string entryId, Listing l)
      : Screen(app), entry_(std::move(entryId)), listing_(std::move(l)) {}

  bool opaque() const override { return false; }

  bool key(const KeyEvent& e) override {
    if (e.key == Key::Ok) {
      app_.nav().pop();
      Playback& p = app_.playback();
      if (p.tune(entry_)) {
        p.setFull();
        app_.nav().push(makePlayer(app_));
      } else {
        app_.toast("This channel has no working feed right now");
      }
      return true;
    }
    return e.key != Key::Back;
  }

  void draw(ui::Ctx& c) override {
    ui::scrim(c, 0.6f);
    Rect card{(gfx::kLayoutWidth - 1100) * 0.5f, 220, 1100, 640};
    c.g.fill(card, theme::hex(0x1C1F25, 0.98f), 34);
    auto ch = app_.store().channels();
    const GuideChannel* g = ch->find(entry_);
    float x = card.x + 60, y = card.y + 50, w = card.w - 120;
    if (g) ui::logo(c, g->logoUrl, g->name, {x, y, 150, 60});
    ui::chip(c, "LATER", x + 170, y + 16, 18, theme::white(0.6f), theme::white(0.85f), true);
    y += 90;
    ui::label(c, listing_.title, x, y, Weight::Bold, 46, theme::white(), w);
    y += 70;
    std::string summary = g ? g->name : std::string();
    std::string day = ui::dayWord(c, listing_.start);
    summary += " \xc2\xb7 " + (day.empty() ? std::string() : day + " ") + ui::range(c, listing_.start, listing_.end, true);
    if (listing_.genres & kGenreSports) summary += " \xc2\xb7 Sports";
    else if (listing_.genres & kGenreMovie) summary += " \xc2\xb7 Movie";
    else if (listing_.genres & kGenreNews) summary += " \xc2\xb7 News";
    if (listing_.live) summary += " \xc2\xb7 Live";
    ui::label(c, summary, x, y, Weight::Regular, 24, theme::white(0.75f), w);
    y += 48;
    if (!listing_.episode.empty()) {
      ui::label(c, listing_.episode, x, y, Weight::Medium, 22, theme::white(0.85f), w);
      y += 38;
    }
    ui::paragraph(c, listing_.overview, x, y, w, Weight::Regular, 22, theme::white(0.7f), 3);
    std::string watch = "Watch " + (g ? g->name : std::string("channel")) + " Now";
    float pw = ui::chipWidth(c, watch, 24) + 40;
    Rect pill{x, card.y + card.h - 140, pw, 70};
    c.g.fill(pill, theme::white(), 35);
    ui::labelIn(c, watch, pill, Weight::Bold, 24, theme::black(), Align::Center);
    ui::label(c, "Back closes", x, card.y + card.h - 54, Weight::Regular, 18, theme::white(0.5f));
  }

 private:
  std::string entry_;
  Listing listing_;
};

}  // namespace

std::unique_ptr<Screen> makeProgramDetail(App& app, const std::string& entryId, const Listing& l) {
  return std::make_unique<DetailScreen>(app, entryId, l);
}

}  // namespace marigold::app
