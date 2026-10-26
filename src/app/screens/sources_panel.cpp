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
    std::string pinned = app.store().pinnedFeed(id);
    p.items.push_back(ui::PanelItem::header("WATCH WITH"));
    for (size_t i = 0; i < feeds.size(); ++i) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, i == 0 ? "Main feed" : "Feed " + std::to_string(i + 1)};
      it.detail = feeds[i].label;
      if (feeds[i].channelId == pinned) it.detail += " \xc2\xb7 Always first";
      it.on = pb.entry() == id && pb.feedIndex() == i;
      it.select = [&app, id, i] {
        Playback& play = app.playback();
        if (play.entry() != id && !play.tune(id)) return;
        play.useFeed(i);
      };
      p.items.push_back(it);
    }
    if (feeds.size() > 1 && app.store().channels()->find(id)) {
      // "Always start with" (pinned feed): Automatic, or one version.
      p.items.push_back(ui::PanelItem::header("ALWAYS START WITH"));
      ui::PanelItem automatic{ui::PanelItem::Kind::Check, "Automatic"};
      automatic.detail = "The most reliable feed first";
      automatic.on = pinned.empty();
      automatic.select = [&app, id] {
        app.store().pinFeed(id, "");
        app.toast("Picks its best feed automatically");
      };
      p.items.push_back(automatic);
      for (size_t i = 0; i < feeds.size(); ++i) {
        ui::PanelItem it{ui::PanelItem::Kind::Check, i == 0 ? "Main feed" : "Feed " + std::to_string(i + 1)};
        it.detail = feeds[i].label;
        it.on = feeds[i].channelId == pinned;
        std::string channel = feeds[i].channelId;
        it.select = [&app, id, channel] {
          app.store().pinFeed(id, channel);
          app.toast("Will start with this feed");
        };
        p.items.push_back(it);
      }
    }
    p.hint = "Whichever you pick, the others stay as automatic backups.";
  });
}

}  // namespace marigold::app
