// Build 153: "Listings look wrong?" (hold OK on a channel, or its Sources
// panel). The channel's distinct schedules as what each has on now and
// next, the one shown checked; picking one keeps it for this channel until
// it is gone. Automatic goes back to the listings most guides agree on. No
// sources or providers are named: only what each schedule says is on.
#include <memory>
#include <string>

#include "app/app.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/ui.hpp"

namespace marigold::app {

std::unique_ptr<Screen> makeGuideChoice(App& app, const std::string& entryId) {
  std::string id = entryId;
  auto panel = std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f, [&app, id](ui::ListPanel& p) {
    auto ch = app.store().channels();
    const GuideChannel* g = ch->find(id);
    p.subtitle = "LISTINGS";
    p.title = g ? g->name : "Listings";
    std::vector<Store::GuideOption> options = app.store().guideOptions(id);
    if (options.empty()) {
      p.items.push_back(ui::PanelItem::note("This channel's listings are still being checked. Try again in a moment."));
      return;
    }
    bool picked = app.store().guidePicked(id);
    p.items.push_back(ui::PanelItem::header("SHOW THESE LISTINGS"));
    ui::PanelItem automatic{ui::PanelItem::Kind::Check, "Automatic"};
    automatic.detail = "The listings most guides agree on";
    automatic.on = !picked;
    automatic.select = [&app, id] {
      app.store().pickGuide(id, "");
      app.toast("Listings picked automatically");
    };
    p.items.push_back(automatic);
    const time::Zone& z = app.zone();
    ui::Ctx tmp{app.renderer(), app.textRenderer(), nullptr, nullptr, time::now(), 0, &z};
    tmp.clock24 = app.store().settings().clock24;
    for (const Store::GuideOption& o : options) {
      std::string next = o.next.empty() ? std::string() : "Next \xc2\xb7 " + o.next + " " + ui::clock(tmp, o.nextStart);
      ui::PanelItem it{ui::PanelItem::Kind::Check, o.now.empty() ? next : "On now \xc2\xb7 " + o.now};
      if (!o.now.empty()) it.detail = next;
      it.on = picked && o.current;
      if (!picked && o.current) it.tag = "SHOWN";
      std::string key = o.key;
      it.select = [&app, id, key] {
        app.store().pickGuide(id, key);
        app.toast("These listings will show for this channel");
      };
      p.items.push_back(it);
    }
    if (options.size() == 1)
      p.items.push_back(ui::PanelItem::note("No other listings were found for this channel."));
    p.hint = "Pick the listings that match what's on. Back closes.";
  });
  panel->liveRows = true;  // the check moves once the new listings are in
  return panel;
}

}  // namespace marigold::app
