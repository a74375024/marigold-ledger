// Blue key: Options (tvOS Options panel, trailing, 760 wide).
#include "app/app.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {

std::unique_ptr<Screen> makeOptions(App& app) {
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, 760.0f, [&app](ui::ListPanel& p) {
    Store& s = app.store();
    Settings set = s.settings();
    auto ch = s.channels();
    p.title = "Options";

    p.items.push_back(ui::PanelItem::header("PROVIDERS"));
    std::vector<AccountSummary> accounts = s.accounts();
    ui::PanelItem acc{ui::PanelItem::Kind::Chevron, "Providers"};
    acc.value = accounts.empty() ? "None" : std::to_string(accounts.size());
    acc.select = [&app] { app.nav().push(makeAccounts(app, false)); };
    p.items.push_back(acc);
    ui::PanelItem reload{ui::PanelItem::Kind::Action, "Reload channels and listings"};
    reload.select = [&app] {
      app.store().refreshAll();
      app.toast("Reloading");
    };
    p.items.push_back(reload);

    p.items.push_back(ui::PanelItem::header("MY LINEUP"));
    ui::PanelItem setup{ui::PanelItem::Kind::Chevron, ch->lineupConfigured ? "Set Up My Lineup Again" : "Set Up My Lineup"};
    if (ch->lineupConfigured) setup.value = std::to_string(ch->lineup.size()) + " channels";
    setup.select = [&app] { app.nav().push(makeLineupSetup(app)); };
    p.items.push_back(setup);
    ui::PanelItem allTab{ui::PanelItem::Kind::Toggle, "All Channels tab"};
    allTab.on = set.allChannelsTab;
    allTab.select = [&app] {
      Settings n = app.store().settings();
      n.allChannelsTab = !n.allChannelsTab;
      app.store().setSettings(n);
    };
    p.items.push_back(allTab);

    p.items.push_back(ui::PanelItem::header("GUIDE"));
    ui::PanelItem preview{ui::PanelItem::Kind::Toggle, "Preview channels"};
    preview.detail = "The corner plays the channel you rest on";
    preview.on = set.previewChannels;
    preview.select = [&app] {
      Settings n = app.store().settings();
      n.previewChannels = !n.previewChannels;
      app.store().setSettings(n);
    };
    p.items.push_back(preview);
    ui::PanelItem fill{ui::PanelItem::Kind::Toggle, "Fill in missing listings"};
    fill.detail = "Outside guides for channels your provider has no listings for";
    fill.on = set.fillMissingListings;
    fill.select = [&app] {
      Settings n = app.store().settings();
      n.fillMissingListings = !n.fillMissingListings;
      app.store().setSettings(n);
    };
    p.items.push_back(fill);

    p.items.push_back(ui::PanelItem::header("LIVE EVENTS"));
    ui::PanelItem sports{ui::PanelItem::Kind::Chevron, "My Sports"};
    sports.select = [&app] { app.nav().push(makeMySports(app)); };
    p.items.push_back(sports);
    ui::PanelItem schedule{ui::PanelItem::Kind::Toggle, "Game schedule"};
    schedule.detail = "This week's games and where to watch them";
    schedule.on = set.scheduleEnabled;
    schedule.select = [&app] {
      Settings n = app.store().settings();
      n.scheduleEnabled = !n.scheduleEnabled;
      app.store().setSettings(n);
    };
    p.items.push_back(schedule);
    if (set.scheduleEnabled) {
      ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh schedule"};
      refresh.select = [&app] {
        app.store().refreshSchedule();
        app.toast("Refreshing the schedule");
      };
      p.items.push_back(refresh);
      ui::PanelItem unwatchable{ui::PanelItem::Kind::Toggle, "Games not in my channels"};
      unwatchable.on = set.showUnwatchable;
      unwatchable.select = [&app] {
        Settings n = app.store().settings();
        n.showUnwatchable = !n.showUnwatchable;
        app.store().setSettings(n);
      };
      p.items.push_back(unwatchable);
      ui::PanelItem scores{ui::PanelItem::Kind::Toggle, "Show scores"};
      scores.on = set.showScores;
      scores.select = [&app] {
        Settings n = app.store().settings();
        n.showScores = !n.showScores;
        app.store().setSettings(n);
      };
      p.items.push_back(scores);
    }

    bool anyXtream = false;
    for (const AccountSummary& a : accounts) anyXtream = anyXtream || a.kind == "Xtream";
    if (anyXtream) {
      p.items.push_back(ui::PanelItem::header("PLAYBACK"));
      ui::PanelItem ts{ui::PanelItem::Kind::Toggle, "Use MPEG-TS for Xtream"};
      ts.on = set.preferTS;
      ts.select = [&app] {
        Settings n = app.store().settings();
        n.preferTS = !n.preferTS;
        app.store().setSettings(n);
      };
      p.items.push_back(ts);
    }

    p.items.push_back(ui::PanelItem::header("HELP"));
    ui::PanelItem diag{ui::PanelItem::Kind::Chevron, "Remote and video test"};
    diag.select = [&app] { app.nav().push(makeDiagnostics(app)); };
    p.items.push_back(diag);
    ui::PanelItem about{ui::PanelItem::Kind::Note, "Marigold " + app.build().version + " (build " + app.build().build + ")"};
    p.items.push_back(about);
  });
}

}  // namespace marigold::app
