// Red key: every feed behind a channel, best first, by quality, East/West
// and the provider's name (never a link). Picking one switches in place.
#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {

std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId) {
  std::string id = entryId;
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f, [&app, id](ui::ListPanel& p) {
    auto ch = app.store().channels();
    const GuideChannel* g = ch->find(id);
    p.subtitle = "SOURCES";
    p.title = g ? g->name : "Sources";
    Playback& pb = app.playback();
    std::vector<Feed> feeds = pb.entry() == id ? pb.feeds() : app.store().feeds(id);
    if (feeds.empty()) {
      p.items.push_back(ui::PanelItem::note("No feeds for this channel right now."));
      return;
    }
    for (size_t i = 0; i < feeds.size(); ++i) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, i == 0 ? "Main feed" : "Feed " + std::to_string(i + 1)};
      it.detail = feeds[i].label;
      it.on = pb.entry() == id && pb.feedIndex() == i;
      it.select = [&app, id, i] {
        Playback& play = app.playback();
        if (play.entry() != id && !play.tune(id)) return;
        play.useFeed(i);
      };
      p.items.push_back(it);
    }
    p.hint = "Whichever you pick, the others stay as automatic backups.";
  });
}

}  // namespace marigold::app
