// Blue key: Options (tvOS Options panel, trailing, 760 wide), and its pages:
// Edit My Lineup, Countries & Languages, provider groups and Guide Listings.
#include <set>

#include "app/app.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "core/catalog/lineup.hpp"
#include "core/sports/my_sports.hpp"
#include "core/util/memory.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {
namespace {

constexpr float kWidth = 760.0f;

std::unique_ptr<PanelScreen> page(App& app, std::function<void(ui::ListPanel&)> build) {
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, kWidth, std::move(build));
}

void toggleSetting(App& app, bool Settings::*field) {
  Settings n = app.store().settings();
  n.*field = !(n.*field);
  app.store().setSettings(n);
}

ui::PanelItem settingToggle(App& app, std::string title, std::string detail, bool Settings::*field) {
  ui::PanelItem it{ui::PanelItem::Kind::Toggle, std::move(title)};
  it.detail = std::move(detail);
  it.on = app.store().settings().*field;
  it.select = [&app, field] { toggleSetting(app, field); };
  return it;
}

std::string countriesSummary(const std::set<std::string>& countries) {
  if (countries.size() == 1 && countries.count("US")) return "US English";
  return std::to_string(countries.size()) + " selected";
}

// "San Francisco–Oakland–San Jose" -> "San Francisco".
std::string shortMarket(const std::string& name) {
  size_t cut = name.find("\xe2\x80\x93");
  if (cut == std::string::npos) cut = name.find('-');
  return cut == std::string::npos ? name : name.substr(0, cut);
}

// Changes to My Lineup after setup (LineupEditView): what was added or
// removed by hand, refreshing the suggestions, and the lineup itself.
std::unique_ptr<Screen> makeEditLineup(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    Store& s = app.store();
    auto ch = s.channels();
    catalog::LineupPrefs prefs = s.lineupPrefs();
    p.subtitle = "OPTIONS";
    p.title = "Edit My Lineup";
    std::string markets;
    for (const catalog::MarketChoice& m : prefs.markets) markets += (markets.empty() ? "" : ", ") + shortMarket(m.name);
    ui::PanelItem market{ui::PanelItem::Kind::Note, "TV market: " + (markets.empty() ? std::string("None") : markets)};
    p.items.push_back(market);
    ui::PanelItem rebuild{ui::PanelItem::Kind::Action, "Refresh Suggestions"};
    rebuild.detail = "Re-checks which channels have listings and picks up new ones. Your own changes are kept.";
    rebuild.select = [&app] {
      app.store().refreshSuggestions();
      app.toast("Refreshing My Lineup");
    };
    p.items.push_back(rebuild);

    // East/West one channel at a time (as adding or removing each version).
    std::vector<std::string> pairs = s.coastPairs();
    if (!pairs.empty()) {
      p.items.push_back(ui::PanelItem::header("EAST & WEST (" + std::to_string(pairs.size()) +
                                              (pairs.size() == 1 ? " CHANNEL HAS BOTH)" : " CHANNELS HAVE BOTH)")));
      for (const std::string& id : pairs) {
        const GuideChannel* g = ch->find(id);
        if (!g) continue;
        ui::PanelItem it{ui::PanelItem::Kind::Choice, g->name};
        it.choices = {"East", "West", "Both"};
        switch (s.channelCoast(id)) {
          case catalog::Coast::east: it.choice = 0; break;
          case catalog::Coast::west: it.choice = 1; break;
          case catalog::Coast::both: it.choice = 2; break;
          case catalog::Coast::automatic: break;
        }
        it.detail = it.choice < 0 ? g->section + " \xc2\xb7 Not in My Lineup" : g->section;
        it.pick = [&app, id](int k) {
          static const catalog::Coast kPick[] = {catalog::Coast::east, catalog::Coast::west, catalog::Coast::both};
          app.store().setChannelCoast(id, kPick[k]);
        };
        p.items.push_back(it);
      }
      bool west = s.resolvedCoast() == catalog::Coast::west;
      bool both = s.resolvedCoast() == catalog::Coast::both;
      p.items.push_back(ui::PanelItem::note(
          "Left / Right picks. A channel set here keeps its choice; the rest follow Options \xe2\x80\xba East/West "
          "Feeds (" + std::string(both ? "Both" : west ? "West" : "East") + " here)."));
    }

    auto row = [&](const std::string& id, bool add) {
      const GuideChannel* g = ch->find(id);
      if (!g) return;
      ui::PanelItem it{ui::PanelItem::Kind::Action, g->name};
      it.detail = g->section;
      it.value = add ? "Add back" : "Remove";
      it.select = [&app, id] { app.store().toggleLineup(id); };
      p.items.push_back(it);
    };
    std::vector<std::string> added, removed;
    for (const std::string& id : prefs.added)
      if (ch->find(id)) added.push_back(id);
    for (const std::string& id : prefs.removed)
      if (ch->find(id)) removed.push_back(id);
    p.items.push_back(ui::PanelItem::header("ADDED BY YOU (" + std::to_string(added.size()) + ")"));
    if (added.empty()) p.items.push_back(ui::PanelItem::note("Hold OK on a channel in the guide to add it."));
    for (const std::string& id : added) row(id, false);
    p.items.push_back(ui::PanelItem::header("REMOVED BY YOU (" + std::to_string(removed.size()) + ")"));
    for (const std::string& id : removed) row(id, true);
    p.items.push_back(ui::PanelItem::header("MY LINEUP (" + std::to_string(ch->lineup.size()) + ")"));
    for (int i : ch->lineup) row(ch->all[static_cast<size_t>(i)].id, false);
  });
}

const char* coastLabel(catalog::Coast c) {
  switch (c) {
    case catalog::Coast::east: return "East";
    case catalog::Coast::west: return "West";
    case catalog::Coast::both: return "Both";
    case catalog::Coast::automatic: break;
  }
  return "Automatic";
}

std::string coastSummary(const Store& s) {
  catalog::Coast set = s.lineupPrefs().coast;
  if (set != catalog::Coast::automatic) return coastLabel(set);
  return std::string("Automatic: ") + coastLabel(s.resolvedCoast());
}

// East/West Feeds: which version of a channel the provider has twice
// (HBO, HBO West) My Lineup keeps.
std::unique_ptr<Screen> makeCoast(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    Store& s = app.store();
    catalog::LineupPrefs prefs = s.lineupPrefs();
    p.subtitle = "OPTIONS";
    p.title = "East/West Feeds";
    p.items.push_back(ui::PanelItem::note(
        "For channels your provider has in both versions. West feeds air three hours after East."));
    auto choice = [&](catalog::Coast c, std::string title, std::string detail) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, std::move(title)};
      it.detail = std::move(detail);
      it.on = prefs.coast == c;
      it.select = [&app, c] { app.store().setCoast(c); };
      p.items.push_back(it);
    };
    std::string why;
    catalog::Coast resolved = catalog::resolveCoast([&] {
      catalog::LineupPrefs a = prefs;
      a.coast = catalog::Coast::automatic;
      return a;
    }());
    const catalog::MarketChoice* home = prefs.home();
    std::optional<bool> marketWest = home ? catalog::marketIsWest(home->name) : std::nullopt;
    if (marketWest) {
      why = shortMarket(home->name) + (*marketWest ? " gets the West feeds" : " gets the East feeds");
    } else {
      why = "From this TV's time zone";
    }
    choice(catalog::Coast::automatic, "Automatic", std::string(coastLabel(resolved)) + " here: " + why);
    choice(catalog::Coast::east, "East feeds", "Shows at their Eastern times");
    choice(catalog::Coast::west, "West feeds", "Shows at their Pacific times");
    choice(catalog::Coast::both, "Both", "Keep both versions in My Lineup");
    std::vector<std::string> pairs = s.coastPairNames();
    if (pairs.empty()) {
      p.items.push_back(ui::PanelItem::note("None of your channels have both versions right now."));
    } else {
      std::string count = std::to_string(pairs.size()) + (pairs.size() == 1 ? " channel has" : " channels have");
      p.items.push_back(ui::PanelItem::note(count + " both here. The other version is one press away in Sources and "
                                            "Hold OK in the guide."));
      // Every one of them, East / West / Both each.
      ui::PanelItem each{ui::PanelItem::Kind::Chevron, "Choose Channel by Channel"};
      each.value = std::to_string(pairs.size());
      each.detail = str::join(std::vector<std::string>(pairs.begin(), pairs.begin() + std::min<size_t>(pairs.size(), 6)),
                              ", ") + (pairs.size() > 6 ? "\xe2\x80\xa6" : "");
      each.select = [&app] { app.nav().push(makeEditLineup(app)); };
      p.items.push_back(each);
    }
  });
}

std::unique_ptr<Screen> makeCountries(App& app) {
  // Sections as they were when the page opened, so rows don't jump.
  auto atOpen = std::make_shared<std::set<std::string>>(app.store().lineupPrefs().countries);
  return page(app, [&app, atOpen](ui::ListPanel& p) {
    Store& s = app.store();
    std::set<std::string> current = s.lineupPrefs().countries;
    p.subtitle = "OPTIONS";
    p.title = "Countries & Languages";
    p.items.push_back(ui::PanelItem::note(
        "Which channels My Lineup, All Channels and Live Events include. Everything else is still in Search."));
    std::vector<MarketSummary> all = s.countries(current);
    auto add = [&](const MarketSummary& m) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, m.name};
      it.detail = std::to_string(m.count) + " channels";
      it.on = current.count(m.code) > 0;
      std::string code = m.code;
      it.select = [&app, code] {
        std::set<std::string> c = app.store().lineupPrefs().countries;
        if (c.count(code)) {
          if (c.size() <= 1) {
            app.toast("Keep at least one");
            return;
          }
          c.erase(code);
        } else {
          c.insert(code);
        }
        app.store().setLineupCountries(c);
      };
      p.items.push_back(it);
    };
    bool any = false;
    for (const MarketSummary& m : all) {
      if (!atOpen->count(m.code)) continue;
      if (!any) p.items.push_back(ui::PanelItem::header("SELECTED"));
      any = true;
      add(m);
    }
    any = false;
    for (const MarketSummary& m : all) {
      if (atOpen->count(m.code)) continue;
      if (!any) p.items.push_back(ui::PanelItem::header("MORE"));
      any = true;
      add(m);
    }
  });
}

std::unique_ptr<Screen> makeGroups(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    p.subtitle = "OPTIONS";
    p.title = "Provider Groups";
    p.items.push_back(ui::PanelItem::note(
        "Unchecked groups are left out of All Channels. They can still be found with Search or the guide filter."));
    for (const GroupSummary& g : app.store().groups()) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, g.name};
      it.on = !g.hidden;
      it.value = std::to_string(g.count);
      std::string name = g.name;
      bool hidden = g.hidden;
      it.select = [&app, name, hidden] { app.store().setGroupHidden(name, !hidden); };
      p.items.push_back(it);
    }
  });
}

std::unique_ptr<Screen> makeGuideListings(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    Store& s = app.store();
    GuideListingsStatus st = s.guideListings();
    p.subtitle = "OPTIONS";
    p.title = "Guide Listings";
    if (!st.providers.empty()) {
      p.items.push_back(ui::PanelItem::header("YOUR PROVIDER'S GUIDE"));
      for (const GuideListingsStatus::Provider& pr : st.providers) {
        ui::PanelItem it{ui::PanelItem::Kind::Note, pr.name};
        it.detail = pr.line;
        p.items.push_back(it);
      }
      p.items.push_back(ui::PanelItem::note(
          "The provider's guide file is downloaded about twice a day while Marigold is open (pausing while you "
          "watch). It comes first; outside listings only fill channels it doesn't cover."));
    }
    p.items.push_back(ui::PanelItem::header("OUTSIDE LISTINGS"));
    p.items.push_back(settingToggle(app, "Fill in missing listings",
                                    "Free TV listings for channels your provider has no guide for",
                                    &Settings::fillMissingListings));
    p.items.push_back(ui::PanelItem::note(st.outsideLine));
    if (s.settings().fillMissingListings) {
      p.items.push_back(ui::PanelItem::header("LISTINGS TO USE"));
      for (const GuideListingsStatus::Shard& sh : st.shards) {
        ui::PanelItem it{ui::PanelItem::Kind::Toggle, sh.title};
        it.detail = sh.detail;
        it.on = sh.on;
        std::string id = sh.id;
        bool on = sh.on;
        it.select = [&app, id, on] { app.store().setGuideShard(id, !on); };
        p.items.push_back(it);
      }
      if (!st.sourceLines.empty()) {
        p.items.push_back(ui::PanelItem::header("FILES"));
        for (const std::string& line : st.sourceLines) p.items.push_back(ui::PanelItem::note(line));
      }
    }
    ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh listings now"};
    refresh.select = [&app] {
      app.store().refreshListings();
      app.toast("Refreshing listings");
    };
    p.items.push_back(refresh);
  });
}

}  // namespace

std::unique_ptr<Screen> makeOptions(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    Store& s = app.store();
    Settings set = s.settings();
    auto ch = s.channels();
    catalog::LineupPrefs prefs = s.lineupPrefs();
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
    if (ch->lineupConfigured) {
      ui::PanelItem edit{ui::PanelItem::Kind::Chevron, "Edit My Lineup"};
      edit.value = std::to_string(ch->lineup.size()) + " channels";
      edit.select = [&app] { app.nav().push(makeEditLineup(app)); };
      p.items.push_back(edit);
    }
    ui::PanelItem coast{ui::PanelItem::Kind::Chevron, "East/West feeds"};
    coast.value = coastSummary(s);
    coast.select = [&app] { app.nav().push(makeCoast(app)); };
    p.items.push_back(coast);
    ui::PanelItem countries{ui::PanelItem::Kind::Chevron, "Countries & Languages"};
    countries.value = countriesSummary(prefs.countries);
    countries.select = [&app] { app.nav().push(makeCountries(app)); };
    p.items.push_back(countries);
    ui::PanelItem setup{ui::PanelItem::Kind::Chevron, ch->lineupConfigured ? "Set Up My Lineup Again" : "Set Up My Lineup"};
    setup.select = [&app] { app.nav().push(makeLineupSetup(app)); };
    p.items.push_back(setup);
    p.items.push_back(settingToggle(app, "All Channels tab", "Every channel in your countries", &Settings::allChannelsTab));
    if (set.allChannelsTab) {
      int hidden = 0;
      for (const GroupSummary& g : s.groups()) hidden += g.hidden ? 1 : 0;
      ui::PanelItem groups{ui::PanelItem::Kind::Chevron, "Provider groups"};
      groups.value = hidden ? std::to_string(hidden) + " hidden" : "All shown";
      groups.select = [&app] { app.nav().push(makeGroups(app)); };
      p.items.push_back(groups);
    }

    p.items.push_back(ui::PanelItem::header("GUIDE"));
    p.items.push_back(settingToggle(app, "Preview channels", "Rest on a channel to hear it in the corner",
                                    &Settings::previewChannels));
    ui::PanelItem listings{ui::PanelItem::Kind::Chevron, "Guide Listings"};
    listings.select = [&app] { app.nav().push(makeGuideListings(app)); };
    p.items.push_back(listings);
    ui::PanelItem clock{ui::PanelItem::Kind::Chevron, "Clock"};
    clock.value = set.clock24 ? "24 hours" : "12 hours";
    clock.select = [&app] { toggleSetting(app, &Settings::clock24); };
    p.items.push_back(clock);

    p.items.push_back(ui::PanelItem::header("LIVE EVENTS"));
    ui::PanelItem sports{ui::PanelItem::Kind::Chevron, "My Sports"};
    sports.detail = "Leagues and favorite teams";
    const sports::MySportsPrefs& my = s.mySports();
    if (my.configured) sports.value = std::to_string(my.leagues.size()) + (my.leagues.size() == 1 ? " league" : " leagues");
    sports.select = [&app] { app.nav().push(makeMySports(app)); };
    p.items.push_back(sports);
    if (prefs.markets.size() > 1) {
      ui::PanelItem home{ui::PanelItem::Kind::Chevron, "Home city"};
      home.detail = "Its stations are listed first on games";
      home.value = shortMarket(s.homeMarketName());
      home.select = [&app] { app.store().cycleHomeMarket(); };
      p.items.push_back(home);
    }
    p.items.push_back(settingToggle(app, "Game schedule", "Schedule, logos and TV networks", &Settings::scheduleEnabled));
    if (set.scheduleEnabled) {
      ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh schedule"};
      refresh.select = [&app] {
        app.store().refreshSchedule();
        app.toast("Refreshing the schedule");
      };
      p.items.push_back(refresh);
      p.items.push_back(settingToggle(app, "Games not in my channels", "Show them dimmed at the end of each row",
                                      &Settings::showUnwatchable));
      p.items.push_back(settingToggle(app, "Show scores", "Off keeps live and final status, without numbers",
                                      &Settings::showScores));
    }

    bool anyXtream = false;
    for (const AccountSummary& a : accounts) anyXtream = anyXtream || a.kind == "Xtream";
    if (anyXtream) {
      p.items.push_back(ui::PanelItem::header("PLAYBACK"));
      p.items.push_back(settingToggle(app, "Use MPEG-TS for Xtream", "Try this if Xtream channels stutter", &Settings::preferTS));
    }

    p.items.push_back(ui::PanelItem::header("HELP"));
    ui::PanelItem report{ui::PanelItem::Kind::Chevron, "Export Channel Report"};
    report.detail = "A spreadsheet of your channels, to a phone on your Wi-Fi";
    report.select = [&app] { app.nav().push(makeExport(app, ExportKind::ChannelReport)); };
    p.items.push_back(report);
    ui::PanelItem log{ui::PanelItem::Kind::Chevron, "Export Playback Log"};
    log.detail = "What happened when channels were tuned";
    log.select = [&app] { app.nav().push(makeExport(app, ExportKind::PlaybackLog)); };
    p.items.push_back(log);
    ui::PanelItem diag{ui::PanelItem::Kind::Chevron, diagnosticsTitle()};
    diag.select = [&app] { app.nav().push(makeDiagnostics(app)); };
    p.items.push_back(diag);
    std::string inUse = memory::describe(memory::read());
    if (!inUse.empty()) p.items.push_back(ui::PanelItem::note("Memory in use: " + inUse));
    ui::PanelItem about{ui::PanelItem::Kind::Note, "Marigold " + app.build().version + " (build " + app.build().build + ")"};
    p.items.push_back(about);
  });
}

}  // namespace marigold::app
