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
