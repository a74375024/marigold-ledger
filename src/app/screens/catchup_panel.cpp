// Build 163, the catch-up test (test only): the player's "Test catch-up"
// button opens this small side panel over the picture. The copy playing,
// its provider and the archive it claims; three moments to ask it for: ten
// minutes ago, the start of the programme on now (when the guide knows it
// and it started more than two minutes ago), and yesterday at this time.
// Picking one plays the archive from then (Session's catch-up test); the
// player shows how it went.
#include <cstdio>
#include <memory>
#include <string>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/engine/views.hpp"

namespace marigold::app {
namespace {

using Moment = engine::Session::CatchupMoment;

constexpr time::Seconds kProgrammeStartedAtLeast = 120;

// "7:52 PM" (or "19:52"), in the viewer's clock and zone.
std::string clockText(App& app, time::Seconds t) {
  time::Civil l = app.zone().toLocal(t);
  char buf[24];
  if (app.store().settings().clock24) {
    std::snprintf(buf, sizeof buf, "%02d:%02d", l.hour, l.minute);
  } else {
    int h = l.hour % 12;
    std::snprintf(buf, sizeof buf, "%d:%02d %s", h == 0 ? 12 : h, l.minute, l.hour < 12 ? "AM" : "PM");
  }
  return buf;
}

time::Seconds toMinute(time::Seconds t) { return t - ((t % 60) + 60) % 60; }

// Closes the panel and plays the archive from `from`.
void start(App& app, Moment moment, time::Seconds from) {
  app.nav().pop();
  if (!app.playback().startCatchupTest(moment, from)) app.toast("Catch-up can\xe2\x80\x99t be tested on this copy right now", true);
}

}  // namespace

std::unique_ptr<Screen> makeCatchupPanel(App& app) {
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 680.0f, [&app](ui::ListPanel& p) {
    Playback& pb = app.playback();
    p.subtitle = "TEST";
    p.title = "Test catch-up";
    p.hint = "Back returns to live. Every test is written to the playback log, worked or not, so one export tells "
             "us which channels really go back.";
    uint16_t days = pb.catchupDays();
    const std::vector<Feed>& feeds = pb.feeds();
    size_t i = pb.feedIndex();
    if (!days || i >= feeds.size()) {
      p.items.push_back(ui::PanelItem::note("The copy playing keeps no catch-up."));
      return;
    }
    // "<channel> · <provider>, version i of n · archive: N days".
    auto ch = app.store().channels();
    std::string guideId = ch->find(pb.entry()) ? pb.entry() : app.store().entryOfChannel(feeds[i].channelId);
    const GuideChannel* g = ch->find(guideId);
    std::string channel = g ? g->name : std::string("This channel");
    std::string provider = app.store().providerName(feeds[i].channelId);
    std::string line = channel + " \xc2\xb7 " + (provider.empty() ? std::string("Provider") : provider) +
                       ", version " + std::to_string(i + 1) + " of " + std::to_string(feeds.size()) +
                       " \xc2\xb7 archive: " + std::to_string(days) + (days == 1 ? " day" : " days");
    p.items.push_back(ui::PanelItem::note(line));

    time::Seconds now = time::now();
    ui::PanelItem ten{ui::PanelItem::Kind::Action, "10 minutes ago"};
    ten.detail = "Plays from " + clockText(app, toMinute(now - 600));
    ten.select = [&app] { start(app, Moment::TenMinutesAgo, time::now() - 600); };
    p.items.push_back(ten);

    // The programme on now, when the guide knows it and it began a while ago.
    if (auto listings = app.store().listings(); listings && g) {
      engine::views::NowNext on = engine::views::nowAndNext(*listings, g->id, now);
      if (on.now && now - on.now->start > kProgrammeStartedAtLeast &&
          now - on.now->start <= static_cast<time::Seconds>(days) * 86400) {
        ui::PanelItem show{ui::PanelItem::Kind::Action, "Start of this programme"};
        show.detail = on.now->title + " from " + clockText(app, on.now->start);
        time::Seconds from = on.now->start;
        show.select = [&app, from] { start(app, Moment::ProgrammeStart, from); };
        p.items.push_back(show);
      }
    }

    if (days >= 1) {
      ui::PanelItem yesterday{ui::PanelItem::Kind::Action, "Yesterday, same time"};
      yesterday.detail = "Plays from " + clockText(app, toMinute(now - 86400)) + " yesterday";
      yesterday.select = [&app] { start(app, Moment::Yesterday, time::now() - 86400); };
      p.items.push_back(yesterday);
    }
  });
  panel->liveRows = true;  // the times move on with the clock
  return panel;
}

}  // namespace marigold::app
