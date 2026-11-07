// Settings' rows, category by category (the tree in docs/m4-test.md,
// "Settings (Build 112)"), their one-line summaries, and the pages they
// open in Settings' middle column (Build 144: in place, no side panels):
// Edit My Lineup, Channel by channel, Groups in All Channels, Guide
// listings, a provider, Home city, Send a report to your phone, Status and
// Hidden. Every row shows its current value; a row that can't be used is
// dimmed with the reason, never hidden; small choices are inline and only
// lists open a page; what can't be undone asks on its own row first. No row
// ever shows a number of channels.
#include <algorithm>
#include <set>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/report_share.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"
#include "app/ui/panel.hpp"
#include "core/catalog/lineup.hpp"
#include "core/sports/my_sports.hpp"
#include "core/util/memory.hpp"
#include "core/util/strings.hpp"
#include "platform/ts_proxy.hpp"

namespace marigold::app {
namespace {

const char* const kSizes[] = {"Small", "Medium", "Large", "Extra large"};
const char* const kLooks[] = {"Box", "Outline", "Shadow"};
const char* const kSetUpFirst = "Set up My Lineup first";

SettingsPage page(std::string title, std::function<void(ui::ListPanel&)> build) {
  SettingsPage pg;
  pg.title = std::move(title);
  pg.build = std::move(build);
  return pg;
}

void toggleSetting(App& app, bool Settings::*field) {
  Settings n = app.store().settings();
  n.*field = !(n.*field);
  app.store().setSettings(n);
}

ui::PanelItem settingToggle(App& app, std::string title, std::string detail, bool Settings::*field,
                            std::string about = "") {
  ui::PanelItem it{ui::PanelItem::Kind::Toggle, std::move(title)};
  it.detail = std::move(detail);
  it.about = std::move(about);
  it.on = app.store().settings().*field;
  it.select = [&app, field] { toggleSetting(app, field); };
  return it;
}

ui::PanelItem choiceRow(std::string title, std::vector<std::string> choices, int on, std::function<void(int)> pick) {
  ui::PanelItem it{ui::PanelItem::Kind::Choice, std::move(title)};
  it.choices = std::move(choices);
  it.choice = on;
  it.pick = std::move(pick);
  return it;
}

ui::PanelItem chevron(std::string title, std::string detail, std::string value, std::function<void()> select) {
  ui::PanelItem it{ui::PanelItem::Kind::Chevron, std::move(title)};
  it.detail = std::move(detail);
  it.value = std::move(value);
  it.select = std::move(select);
  return it;
}

void dim(ui::PanelItem& it, bool usable, std::string why) {
  if (usable) return;
  it.enabled = false;
  it.why = std::move(why);
}

// Captions: saved, and the player told (the picture shows the change).
void changeCaptions(App& app, const std::function<void(Settings&)>& edit) {
  Settings s = app.store().settings();
  edit(s);
  app.store().setSettings(s);
  app.playback().subtitlesChanged();
}

// "San Francisco–Oakland–San Jose" -> "San Francisco" (the guide's
// neighborhood for that area, with two or more).
std::string shortMarket(const std::string& name) { return catalog::shortMarketName(name); }

// "NFL, NBA, MLB +1": the first few, then how many more.
std::string firstFew(const std::vector<std::string>& names, size_t few) {
  std::string out;
  for (size_t i = 0; i < names.size() && i < few; ++i) out += (i ? ", " : "") + names[i];
  if (names.size() > few) out += " +" + std::to_string(names.size() - few);
  return out;
}

std::string countryName(const std::string& code) {
  if (code == "US") return "US English";
  if (code == "US-ES") return "US Spanish";
  return Store::marketName(code);
}

std::string countriesSummary(const std::set<std::string>& countries) {
  std::vector<std::string> names;
  // US English first, as the page lists them.
  for (const char* first : {"US", "US-ES"})
    if (countries.count(first)) names.push_back(countryName(first));
  for (const std::string& c : countries)
    if (c != "US" && c != "US-ES") names.push_back(countryName(c));
  return names.empty() ? "None" : firstFew(names, 2);
}

std::string areaSummary(const catalog::LineupPrefs& prefs) {
  std::vector<std::string> names;
  for (const catalog::MarketChoice& m : prefs.markets) names.push_back(shortMarket(m.name));
  return names.empty() ? "None" : firstFew(names, 2);
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

// What Automatic stands for here, and why ("San Francisco gets the West
// feeds").
struct AutoCoast {
  catalog::Coast coast;
  std::string why;
};
AutoCoast autoCoast(const Store& s) {
  catalog::LineupPrefs a = s.lineupPrefs();
  a.coast = catalog::Coast::automatic;
  AutoCoast out{catalog::resolveCoast(a), ""};
  const catalog::MarketChoice* home = a.home();
  std::optional<bool> west = home ? catalog::marketIsWest(home->name) : std::nullopt;
  if (west) out.why = shortMarket(home->name) + (*west ? " gets the West feeds, so shows air at their Pacific times."
                                                       : " gets the East feeds, so shows air at their Eastern times.");
  else out.why = "From this TV's time zone.";
  return out;
}

std::vector<std::string> leagueNames(const sports::MySportsPrefs& my) {
  std::vector<std::string> names;
  for (const std::string& id : my.leagues) names.push_back(pickName(id));
  return names;
}

std::string teamsSummary(App& app, const sports::MySportsPrefs& my) {
  if (my.teams.empty()) return "None";
  std::vector<std::string> names;
  for (const std::string& key : my.teams) {
    size_t bar = key.find('|');
    if (bar == std::string::npos) continue;
    std::string league = key.substr(0, bar), id = key.substr(bar + 1);
    for (const sports::SportsTeam& t : app.store().teamsOf(league))
      if (t.id == id) names.push_back(t.name);
  }
  // Team names come with the rosters; until then, how many.
  if (names.size() != my.teams.size())
    return std::to_string(my.teams.size()) + (my.teams.size() == 1 ? " team" : " teams");
  return firstFew(names, 2);
}

// ------------------------------------------------------------ pages

// What was added or removed by hand, and the lineup itself (no counts).
SettingsPage editLineupPage(App& app) {
  return page("Edit My Lineup", [&app](ui::ListPanel& p) {
    Store& s = app.store();
    auto ch = s.channels();
    catalog::LineupPrefs prefs = s.lineupPrefs();
    enum class Group { Added, Removed, Lineup };
    auto row = [&](const std::string& id, Group group) {
      const GuideChannel* g = ch->find(id);
      if (!g) return;
      bool add = group == Group::Removed;
      ui::PanelItem it{ui::PanelItem::Kind::Action, g->name};
      it.detail = g->section;
      it.value = add ? "Add back" : "Remove";
      switch (group) {
        case Group::Added:
          it.aboutHeading = "You added it";
          it.about = "It stays in My Lineup whatever your area and kinds of channels suggest. OK takes it out again.";
          break;
        case Group::Removed:
          it.aboutHeading = "You removed it";
          it.about = "It stays out of My Lineup whatever your area and kinds of channels suggest. OK adds it back.";
          break;
        case Group::Lineup:
          it.aboutHeading = "In My Lineup";
          it.about = "OK takes it out. It stays out until you add it back here or from the guide.";
          break;
      }
      it.select = [&app, id] { app.store().toggleLineup(id); };
      p.items.push_back(it);
    };
    std::vector<std::string> added, removed;
    for (const std::string& id : prefs.added)
      if (ch->find(id)) added.push_back(id);
    for (const std::string& id : prefs.removed)
      if (ch->find(id)) removed.push_back(id);
    p.items.push_back(ui::PanelItem::header("ADDED BY YOU"));
    if (added.empty()) p.items.push_back(ui::PanelItem::note("Hold OK on a channel in the guide to add it."));
    for (const std::string& id : added) row(id, Group::Added);
    p.items.push_back(ui::PanelItem::header("REMOVED BY YOU"));
    if (removed.empty()) p.items.push_back(ui::PanelItem::note("Channels you remove show here, to add back."));
    for (const std::string& id : removed) row(id, Group::Removed);
    p.items.push_back(ui::PanelItem::header("MY LINEUP"));
    for (int i : ch->lineup) row(ch->all[static_cast<size_t>(i)].id, Group::Lineup);
  });
}

// East / West / Both for each channel the provider has in both versions.
SettingsPage channelByChannelPage(App& app) {
  return page("Channel by channel", [&app](ui::ListPanel& p) {
    Store& s = app.store();
    auto ch = s.channels();
    catalog::Coast set = s.lineupPrefs().coast;
    std::string rest = set == catalog::Coast::automatic ? std::string("Automatic: ") + coastLabel(s.resolvedCoast())
                                                        : std::string(coastLabel(set));
    p.items.push_back(ui::PanelItem::note("Left / Right picks. A channel set here keeps its choice; the rest follow "
                                          "East/West feeds (" + rest + ")."));
    std::vector<std::string> pairs = s.coastPairs();
    if (pairs.empty()) p.items.push_back(ui::PanelItem::note("None of your channels have both versions right now."));
    for (const std::string& id : pairs) {
      const GuideChannel* g = ch->find(id);
      if (!g) continue;
      int on = -1;
      switch (s.channelCoast(id)) {
        case catalog::Coast::east: on = 0; break;
        case catalog::Coast::west: on = 1; break;
        case catalog::Coast::both: on = 2; break;
        case catalog::Coast::automatic: break;
      }
      ui::PanelItem it = choiceRow(g->name, {"East", "West", "Both"}, on, [&app, id](int k) {
        static const catalog::Coast kPick[] = {catalog::Coast::east, catalog::Coast::west, catalog::Coast::both};
        app.store().setChannelCoast(id, kPick[k]);
      });
      it.detail = on < 0 ? g->section + " \xc2\xb7 Not in My Lineup" : g->section;
      it.about = "East, West or both versions of this channel in My Lineup. It keeps this choice whatever East/West "
                 "feeds is set to.";
      p.items.push_back(it);
    }
  });
}

SettingsPage groupsPage(App& app) {
  return page("Groups in All Channels", [&app](ui::ListPanel& p) {
    p.items.push_back(ui::PanelItem::note(
        "Unchecked groups are left out of All Channels. Search (outside your lineup) and the guide filter still "
        "find them."));
    for (const GroupSummary& g : app.store().groups()) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, g.name};
      it.on = !g.hidden;
      it.about = g.hidden ? "Left out of All Channels. OK shows it there again."
                          : "Shown in All Channels. OK leaves it out; Search and the guide filter still find it.";
      std::string name = g.name;
      bool hidden = g.hidden;
      it.select = [&app, name, hidden] { app.store().setGroupHidden(name, !hidden); };
      p.items.push_back(it);
    }
  });
}

SettingsPage guideListingsPage(App& app) {
  SettingsPage pg = page("Guide listings", [&app](ui::ListPanel& p) {
    Store& s = app.store();
    GuideListingsStatus st = s.guideListings();
    bool fill = s.settings().fillMissingListings;
    p.items.push_back(settingToggle(app, "Fill in missing listings",
                                    "Free TV listings for channels your provider has no guide for",
                                    &Settings::fillMissingListings,
                                    "Your provider's guide comes first. Free TV listings fill in channels it has no "
                                    "guide for."));
    p.items.push_back(ui::PanelItem::note(st.outsideLine));
    p.items.push_back(ui::PanelItem::header("LISTINGS TO USE"));
    for (const GuideListingsStatus::Shard& sh : st.shards) {
      ui::PanelItem it{ui::PanelItem::Kind::Toggle, sh.title};
      it.detail = sh.detail;
      it.on = sh.on;
      std::string id = sh.id;
      bool on = sh.on;
      it.select = [&app, id, on] { app.store().setGuideShard(id, !on); };
      dim(it, fill, "Turn on Fill in missing listings");
      p.items.push_back(it);
    }
    p.items.push_back(ui::PanelItem::header(""));
    ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh listings now"};
    refresh.about = "Reads the listings again now. Help & info \xe2\x80\xba Status has each file.";
    refresh.select = [&app] {
      // Never a second build while one runs.
      app.toast(app.store().refreshListings() ? "Refreshing listings" : "The guide is already loading");
    };
    p.items.push_back(refresh);
  });
  pg.liveRows = true;
  return pg;
}

// Home city with more areas than fit on one row of choices.
SettingsPage homeCityPage(App& app) {
  return page("Home city", [&app](ui::ListPanel& p) {
    Store& s = app.store();
    p.items.push_back(ui::PanelItem::note("Its stations come first, in the guide and on games."));
    std::string home = s.homeMarketName();
    for (const catalog::MarketChoice& m : s.lineupPrefs().markets) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, shortMarket(m.name)};
      it.on = m.name == home;
      it.about = "Its stations come first, in the guide and on games.";
      std::string name = m.name;
      it.select = [&app, name] { app.store().setHomeMarket(name); };
      p.items.push_back(it);
    }
  });
}

// One provider (Build 144): its kind and sign-in, the membership's end and
// streams at once (when the provider says), then Remove, which asks on its
// row first.
SettingsPage providerPage(App& app, SettingsPages& pages, const std::string& id, const std::string& name) {
  return page(name, [&app, &pages, id](ui::ListPanel& p) {
    std::vector<AccountSummary> all = app.store().accounts();
    auto a = std::find_if(all.begin(), all.end(), [&](const AccountSummary& x) { return x.id == id; });
    if (a == all.end()) {
      p.items.push_back(ui::PanelItem::note("This provider was removed."));
      return;
    }
    time::Seconds now = time::now();
    const membership::Membership& m = a->membership;
    bool ended = membership::stage(m, now, app.zone()) == membership::Stage::Ended;
    ui::PanelItem kind{ui::PanelItem::Kind::Note, a->kind};
    kind.detail = ended && a->signInFailed ? std::string("Can't sign in: the membership has ended")
                  : a->state.empty()      ? std::string("Signing in\xe2\x80\xa6")
                                          : a->state;
    p.items.push_back(kind);
    membership::Text note = membership::pageNote(m, now, app.zone());
    if (!note.title.empty()) {
      ui::PanelItem it{ui::PanelItem::Kind::Note, note.title};
      it.detail = note.detail;
      it.warn = membership::warns(m, now, app.zone());
      p.items.push_back(it);
    }
    std::string streams = membership::streamsNote(m);
    if (!streams.empty()) {
      ui::PanelItem it{ui::PanelItem::Kind::Note, "Streams at once"};
      it.detail = streams;
      p.items.push_back(it);
    }
    p.items.push_back(ui::PanelItem::header(""));
    ui::PanelItem remove{ui::PanelItem::Kind::Action, "Remove this provider"};
    remove.detail = "Its channels leave the guide; My Lineup keeps the rest";
    remove.aboutLabel = str::upperAscii(a->name);
    remove.aboutHeading = "Remove this provider";
    remove.about = "Its login stays on this TV until you remove it; the password is never shown.";
    remove.okLabel = "Remove\xe2\x80\xa6";
    ui::PanelItem::Ask ask;
    ask.title = "Remove " + a->name + "?";
    ask.line = "OK to remove \xc2\xb7 Back to keep it";
    ask.ok = "Remove";
    ask.back = "Keep it";
    ask.heading = "Its channels leave the guide";
    ask.about = "My Lineup keeps the rest, from your other providers. You can add " + a->name +
                " again later with its login.";
    remove.ask = std::move(ask);
    remove.select = [&app, &pages, id] {
      app.store().removeAccount(id);
      pages.close();
      app.toast("Provider removed");
    };
    p.items.push_back(remove);
  });
}

// The three reports on one page, and clearing the playback log (Build 144:
// OK on a report shares it, its code beside the list).
SettingsPage reportsPage(App& app) {
  auto share = std::make_shared<ReportShare>(app);
  SettingsPage pg = page("Send a report to your phone", [&app, share](ui::ListPanel& p) {
    p.items.push_back(ui::PanelItem::note(
        "Pick a report and scan the code with your phone. The link works for 10 minutes, only on your home network. "
        "No passwords, links or server addresses are in any report."));
    struct Report {
      ExportKind kind;
      const char* detail;
      const char* about;
    };
    static const Report kReports[] = {
        {ExportKind::ChannelReport, "A spreadsheet of your channels: how each was sorted, its feeds",
         "A spreadsheet of every channel: how it was sorted, its feeds, and where its listings come from."},
        {ExportKind::PlaybackLog, "What happened when channels were tuned",
         "What happened each time a channel was tuned: feeds tried, stalls, reconnects and errors. No stream links."},
        {ExportKind::LiveEvents, "Why each event is on the board, or left out",
         "A spreadsheet of every event in your provider's event channels: what it was read as, and the tile it's on "
         "or why it was left out."},
    };
    for (size_t i = 0; i < 3; ++i) {
      const Report& r = kReports[i];
      bool on = share->kind() == r.kind;
      ui::PanelItem it{ui::PanelItem::Kind::Action, reportName(r.kind)};
      it.detail = on ? share->rowLine() : std::string(r.detail);
      if (on && share->sharing() && i + 1 < 3) it.next = "Next report";
      it.okLabel = on && (share->sharing() || share->building()) ? "Stop sharing" : "Share";
      it.about = std::string(r.about) + "\nOK shares it: a code to scan with your phone.";
      it.preview = "report";  // its code in the right column while shared
      ExportKind kind = r.kind;
      it.select = [share, kind] { share->toggle(kind); };
      p.items.push_back(it);
    }
    p.items.push_back(ui::PanelItem::header(""));
    ui::PanelItem clear{ui::PanelItem::Kind::Action, "Clear playback log"};
    clear.detail = "The log starts again from now";
    clear.about = "Forgets what happened when channels were tuned. The log starts again from now.";
    clear.okLabel = "Clear\xe2\x80\xa6";
    ui::PanelItem::Ask ask;
    ask.title = "Clear the playback log?";
    ask.line = "OK to clear \xc2\xb7 Back to keep it";
    ask.ok = "Clear";
    ask.back = "Keep it";
    ask.heading = "What happened when channels were tuned is forgotten";
    ask.about = "The log starts again from now. It can't be brought back.";
    clear.ask = std::move(ask);
    clear.select = [&app] {
      app.playback().clearLog();
      app.toast("Playback log cleared");
    };
    p.items.push_back(clear);
  });
  pg.crumb = "Send a report";
  pg.liveRows = true;
  pg.info = [share](ui::Ctx& c, float x, float y, float w, const ui::PanelItem* f) {
    if (!f || f->preview != "report" || !share->kind() || f->title != reportName(*share->kind())) return false;
    share->drawInfo(c, x, y, w);
    return true;
  };
  pg.tick = [share](uint32_t ms) { return share->tick(ms); };
  pg.wakeAt = [share] { return share->wakeAt(); };
  return pg;
}

// The guide files and memory: what the old Guide Listings page and the
// notes at the end of the list said (never how many channels).
SettingsPage statusPage(App& app) {
  SettingsPage pg = page("Status", [&app](ui::ListPanel& p) {
    GuideListingsStatus st = app.store().guideListings();
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
    p.items.push_back(ui::PanelItem::note(st.outsideLine));
    for (const std::string& line : st.sourceLines) p.items.push_back(ui::PanelItem::note(line));
    std::string inUse = memory::describe(memory::read());
    if (!inUse.empty()) {
      p.items.push_back(ui::PanelItem::header("MEMORY"));
      p.items.push_back(ui::PanelItem::note("In use: " + inUse));
    }
  });
  pg.liveRows = true;
  return pg;
}

// ------------------------------------------------------------ categories

void watchingRows(App& app, ui::ListPanel& p, SettingsPages& /*pages*/) {
  const Settings& set = app.store().settings();
  bool apple = Playback::playerDrawsSubtitles();
  p.items.push_back(ui::PanelItem::header("SUBTITLES"));
  auto language = [&](bool spanish) {
    SubtitleChoice c = subtitleChoice(set, spanish);
    ui::PanelItem it{ui::PanelItem::Kind::Toggle, spanish ? "Spanish-language channels" : "English-language channels"};
    it.on = c.on;
    if (c.on) it.detail = c.language.empty() ? "In each channel's own language" : "In " + c.language;
    else it.detail = spanish ? "Univision, Telemundo and the rest" : "Off until you turn them on";
    it.about = std::string("Subtitles stay on for every ") + (spanish ? "Spanish" : "English") +
               "-language channel that has them, until you turn them off.\nPick a language from Aa in the player.";
    it.select = [&app, spanish] {
      changeCaptions(app, [spanish](Settings& s) {
        SubtitleChoice now = subtitleChoice(s, spanish);
        now.on = !now.on;
        setSubtitleChoice(s, spanish, now);
      });
    };
    p.items.push_back(it);
  };
  language(false);
  language(true);
  ui::PanelItem size = choiceRow("Caption size", {kSizes[0], kSizes[1], kSizes[2], kSizes[3]},
                                 std::clamp(set.subtitleSize, 0, 3),
                                 [&app](int k) { changeCaptions(app, [k](Settings& s) { s.subtitleSize = k; }); });
  size.about = std::string("Left and Right change it; the preview follows.") +
               (apple ? "\nOn Apple TV it shows from the next caption line." : "");
  size.preview = "captions";
  p.items.push_back(size);
  ui::PanelItem look = choiceRow("Caption look", {kLooks[0], kLooks[1], kLooks[2]}, std::clamp(set.subtitleLook, 0, 2),
                                 [&app](int k) { changeCaptions(app, [k](Settings& s) { s.subtitleLook = k; }); });
  look.about = std::string("What sits behind the letters: a dark box, a dark edge round each letter, or a soft "
                           "shadow.") +
               (apple ? "\nOn Apple TV Outline and Shadow both edge the letters, and turning the box on or off "
                        "restarts the picture for a moment."
                      : "");
  look.preview = "captions";
  p.items.push_back(look);
  // Apple TV's player draws the captions itself, with its own letters and
  // position.
  ui::PanelItem letters =
      choiceRow("Letters", {"As sent", "Sentence case"}, set.subtitleSentenceCase ? 1 : 0,
                [&app](int k) { changeCaptions(app, [k](Settings& s) { s.subtitleSentenceCase = k == 1; }); });
  letters.detail = "All-capitals captions as normal sentences";
  letters.about = "Sentence case turns live captions in capitals into normal sentences. It's easier to read but can "
                  "get names wrong.";
  letters.preview = "captions";
  dim(letters, !apple, "Apple TV shows captions as sent");
  p.items.push_back(letters);
  ui::PanelItem position =
      choiceRow("Position", {"Bottom", "Top"}, set.subtitleTop ? 1 : 0,
                [&app](int k) { changeCaptions(app, [k](Settings& s) { s.subtitleTop = k == 1; }); });
  position.about = "Top keeps the captions off a score or a ticker at the bottom of the picture.";
  position.preview = "captions";
  dim(position, !apple, "Apple TV places captions itself");
  p.items.push_back(position);
  ui::PanelItem timing = captionTimingRow(set.captionTiming, [&app](int ms) {
    changeCaptions(app, [ms](Settings& s) { setCaptionTiming(s, "", ms); });
  });
  timing.detail = set.captionTimingByChannel.empty() ? "For every channel" : "Channels given their own keep it";
  timing.about = std::string("Later holds captions back; earlier shows them sooner. Live captions are typed as "
                             "people speak, so they often come a few seconds after the words.\nAa in the player "
                             "can give a channel its own timing.") +
                 (apple ? "\nOn Apple TV captions can only be held back: earlier shows them on time." : "");
  p.items.push_back(timing);
  if (set.systemCaptions >= 0)
    p.items.push_back(ui::PanelItem::note("The TV's own captions switch also turns subtitles on and off."));
  p.items.push_back(ui::PanelItem::header("CHANGING CHANNELS"));
  ui::PanelItem up = choiceRow("Up arrow in the player", {"Channel above", "Higher number"}, set.upGoesHigher ? 1 : 0,
                               [&app](int k) {
                                 Settings n = app.store().settings();
                                 n.upGoesHigher = k == 1;
                                 app.store().setSettings(n);
                               });
  up.detail = "CH+ always goes to the higher number";
  up.about = "Channel above: Up goes to the channel above in the list, as it does in the guide.\nHigher number: Up "
             "goes to higher numbers, as CH+ does.";
  p.items.push_back(up);
}

void guideRows(App& app, ui::ListPanel& p, SettingsPages& pages) {
  Store& s = app.store();
  const Settings& set = s.settings();
  p.items.push_back(settingToggle(app, "Preview channels", "Rest on a channel to hear it in the corner",
                                  &Settings::previewChannels,
                                  "The focused channel plays in the guide's corner after a moment. Off keeps the "
                                  "guide quiet."));
  ui::PanelItem clock = choiceRow("Clock", {"12-hour", "24-hour"}, set.clock24 ? 1 : 0, [&app](int k) {
    Settings n = app.store().settings();
    n.clock24 = k == 1;
    app.store().setSettings(n);
  });
  clock.about = "Times everywhere: the guide, Live Events and the player.";
  p.items.push_back(clock);
  p.items.push_back(settingToggle(app, "Recent searches", "What you picked in Search, under its keyboard",
                                  &Settings::recentSearches,
                                  "Search keeps the names of the last few things you picked from it, to pick again "
                                  "in one press. Off forgets them and keeps no more."));
  p.items.push_back(settingToggle(app, "All Channels tab", "Every channel in your countries", &Settings::allChannelsTab,
                                  "A tab in the guide with every channel of your countries and languages, by the "
                                  "provider's groups."));
  bool anyHidden = false;
  if (set.allChannelsTab)
    for (const GroupSummary& g : s.groups()) anyHidden = anyHidden || g.hidden;
  ui::PanelItem groups = chevron("Groups in All Channels", "The provider's groups the tab shows",
                                 anyHidden ? "Some hidden" : "All shown", [&app, &pages] { pages.open(groupsPage(app)); });
  groups.about = "Unchecked groups are left out of All Channels. Search (outside your lineup) and the guide filter "
                 "still find them.";
  dim(groups, set.allChannelsTab, "Turn on All Channels tab");
  p.items.push_back(groups);
  ui::PanelItem listings = chevron("Guide listings", "Your provider's guide and free listings",
                                   set.fillMissingListings ? "Fill in on" : "Provider's only",
                                   [&app, &pages] { pages.open(guideListingsPage(app)); });
  listings.about = "Your provider's guide comes first. Free TV listings fill in channels it has no guide for.";
  p.items.push_back(listings);
}

void lineupRows(App& app, ui::ListPanel& p, SettingsPages& pages) {
  Store& s = app.store();
  auto ch = s.channels();
  catalog::LineupPrefs prefs = s.lineupPrefs();
  bool configured = ch->lineupConfigured;
  ui::PanelItem edit = chevron("Edit My Lineup", "Added by you, removed by you, the whole list", "",
                               [&app, &pages] { pages.open(editLineupPage(app)); });
  edit.about = "Add a channel back, or take one out. Your own changes stay as suggestions change.";
  dim(edit, configured, kSetUpFirst);
  p.items.push_back(edit);
  ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh suggestions"};
  refresh.detail = "Picks up channels that now have listings. Your changes stay";
  refresh.about = "Re-checks which channels have listings and picks up new ones. Channels you added or removed stay "
                  "as they are.";
  refresh.select = [&app] {
    // While the guide loads the suggestions would miss its listings.
    app.toast(app.store().refreshSuggestions() ? "Refreshing My Lineup" : "The guide is already loading");
  };
  dim(refresh, configured, kSetUpFirst);
  p.items.push_back(refresh);
  ui::PanelItem area = chevron("Your area", "Brings in your local stations", areaSummary(prefs),
                               [&app] { app.nav().push(makeLineupStep(app, LineupStep::Area)); });
  area.about = "Your local stations (ABC, CBS, NBC, FOX, PBS\xe2\x80\xa6) and regional sports join My Lineup. Pick "
               "more than one if you split your time; the first is home for Live Events.";
  dim(area, configured, kSetUpFirst);
  p.items.push_back(area);
  ui::PanelItem countries = chevron("Countries & languages", "", countriesSummary(prefs.countries),
                                    [&app] { app.nav().push(makeLineupStep(app, LineupStep::Countries)); });
  countries.about = "Which channels My Lineup, All Channels and Live Events include. Everything else is still in "
                    "Search.";
  p.items.push_back(countries);
  size_t kinds = prefs.categories.size();
  ui::PanelItem kindsRow =
      chevron("Kinds of channels",
              "News, sports, movies\xe2\x80\xa6 and \xe2\x80\x9conly channels with a guide\xe2\x80\x9d",
              std::to_string(kinds) + (kinds == 1 ? " kind" : " kinds"),
              [&app] { app.nav().push(makeLineupStep(app, LineupStep::Kinds)); });
  kindsRow.about = "What My Lineup suggests. Each channel appears once, however many copies your providers have.";
  dim(kindsRow, configured, kSetUpFirst);
  p.items.push_back(kindsRow);
  AutoCoast automatic = autoCoast(s);
  static const catalog::Coast kCoasts[] = {catalog::Coast::automatic, catalog::Coast::east, catalog::Coast::west,
                                           catalog::Coast::both};
  int on = 0;
  for (int i = 0; i < 4; ++i)
    if (prefs.coast == kCoasts[i]) on = i;
  ui::PanelItem coast = choiceRow("East/West feeds", {"Automatic", "East", "West", "Both"}, on,
                                  [&app](int k) { app.store().setCoast(kCoasts[k]); });
  if (prefs.coast == catalog::Coast::automatic)
    coast.detail = std::string("Automatic here: ") + coastLabel(automatic.coast);
  else if (prefs.coast == catalog::Coast::both) coast.detail = "Both versions in My Lineup";
  else coast.detail = coastLabel(prefs.coast) + std::string(" feeds");
  coast.about = automatic.why + "\nFor channels your provider has in both versions (HBO, HBO West). Choose per "
                                "channel with Channel by channel.";
  p.items.push_back(coast);
  std::vector<std::string> pairs = s.coastPairNames();
  std::vector<std::string> some(pairs.begin(), pairs.begin() + std::min<size_t>(pairs.size(), 4));
  std::string names = str::join(some, ", ") + (pairs.size() > 4 ? "\xe2\x80\xa6" : "");
  ui::PanelItem each = chevron("Channel by channel", names, "", [&app, &pages] { pages.open(channelByChannelPage(app)); });
  each.about = "East, West or both for each channel your provider has twice. A channel set here keeps its choice.";
  dim(each, !pairs.empty(), "None of your channels have both versions right now");
  p.items.push_back(each);
  p.items.push_back(ui::PanelItem::header("START OVER"));
  ui::PanelItem again = chevron(configured ? "Set up My Lineup again" : "Set up My Lineup",
                                "Walks through area, languages and kinds of channels", "",
                                [&app] { app.nav().push(makeLineupSetup(app)); });
  again.about = "The three steps from the start. Nothing changes until the last one.";
  p.items.push_back(again);
}

// Settings › Live Events › Hidden (Build 127): what the board leaves out,
// each with why and Show again (back in its row on the board).
SettingsPage hiddenPage(App& app) {
  SettingsPage pg = page("Hidden from Live Events", [&app](ui::ListPanel& p) {
    p.items.push_back(ui::PanelItem::note("Shown again means back in its row on the board."));
    std::vector<Store::HiddenItem> items = app.store().hiddenFromBoard();
    if (items.empty()) p.items.push_back(ui::PanelItem::note("Nothing is hidden."));
    for (const Store::HiddenItem& h : items) {
      ui::PanelItem it{ui::PanelItem::Kind::Action, h.name};
      it.detail = h.why;
      it.value = "Show again";
      it.about = "OK puts it back in its row on the board.";
      std::string id = h.id, name = h.name;
      it.select = [&app, id, name] {
        app.store().showOnBoard(id);
        app.toast(name + " is back on the board");
      };
      p.items.push_back(it);
    }
    p.hint = "Hide a league from any game card.";
  });
  pg.crumb = "Hidden";
  return pg;
}

void eventsRows(App& app, ui::ListPanel& p, SettingsPages& pages) {
  Store& s = app.store();
  const Settings& set = s.settings();
  const sports::MySportsPrefs& my = s.mySports();
  bool schedule = set.scheduleEnabled;
  p.items.push_back(settingToggle(app, "Game schedule", "Schedules, logos and TV networks", &Settings::scheduleEnabled,
                                  "Looks up the week's games for your sports, with team logos and the networks "
                                  "showing them. Off leaves the provider's event channels only."));
  ui::PanelItem scores = settingToggle(app, "Show scores", "Off keeps live and final, without numbers",
                                       &Settings::showScores,
                                       "Off hides scores, sets and podiums everywhere; games still say live or final.");
  dim(scores, schedule, "Turn on Game schedule");
  p.items.push_back(scores);
  p.items.push_back(ui::PanelItem::header(kShowOnBoard));
  showOnBoardRows(app, p, true);
  // Build 127: everything hidden from the board, in one list.
  std::vector<std::string> hidden;
  for (const Store::HiddenItem& h : s.hiddenFromBoard()) hidden.push_back(h.name);
  ui::PanelItem hide = chevron("Hidden", "Leagues and sports left off the board",
                               hidden.empty() ? "None" : firstFew(hidden, 2), [&app, &pages] { pages.open(hiddenPage(app)); });
  hide.about = "Leagues hidden from a game card, and sports that stay off the board unless you turn them on. "
               "Show again puts one back in its row.";
  p.items.push_back(hide);
  p.items.push_back(ui::PanelItem::header(""));
  std::vector<std::string> leagues = leagueNames(my);
  ui::PanelItem mine = chevron("My leagues", "", leagues.empty() ? "Not set" : firstFew(leagues, 3),
                               [&app] { app.nav().push(makeMySports(app)); });
  mine.about = "Each pick gets its own row in Live Events, and the schedule is only looked up for these. "
               "Soccer's competitions and the college sports are chosen from their tiles.";
  p.items.push_back(mine);
  ui::PanelItem teams = chevron("My teams", "", teamsSummary(app, my),
                                [&app] { app.nav().push(makeMySports(app, false, true)); });
  teams.about = "Your teams' games lead Live Events.";
  dim(teams, !my.leagues.empty(), "Pick your leagues first");
  p.items.push_back(teams);
  ui::PanelItem remind{ui::PanelItem::Kind::Toggle, "Remind me before my teams play"};
  remind.on = my.reminders;
  remind.tag = "NEW";
  remind.about = "A card in the corner before each of your teams' games, with a button to watch.";
  remind.select = [&app] {
    sports::MySportsPrefs n = app.store().mySports();
    n.reminders = !n.reminders;
    app.store().setMySports(n);
  };
  dim(remind, !my.teams.empty(), "Pick your teams first");
  p.items.push_back(remind);
  // Home city: a choice of your areas.
  std::vector<catalog::MarketChoice> markets = s.lineupPrefs().markets;
  std::string home = s.homeMarketName();
  ui::PanelItem city;
  if (markets.size() >= 2 && markets.size() <= 3) {
    std::vector<std::string> names;
    int on = 0;
    for (size_t i = 0; i < markets.size(); ++i) {
      names.push_back(shortMarket(markets[i].name));
      if (markets[i].name == home) on = static_cast<int>(i);
    }
    city = choiceRow("Home city", names, on, [&app, markets](int k) {
      app.store().setHomeMarket(markets[static_cast<size_t>(k)].name);
    });
  } else if (markets.size() > 3) {
    city = chevron("Home city", "", shortMarket(home), [&app, &pages] { pages.open(homeCityPage(app)); });
  } else {
    city = ui::PanelItem{ui::PanelItem::Kind::Action, "Home city"};
    city.value = home.empty() ? "None" : shortMarket(home);
    dim(city, false, markets.empty() ? "Pick your area in My Lineup" : "Pick a second area in My Lineup to choose");
  }
  if (city.enabled) city.detail = "Its stations come first, in the guide and on games";
  city.about =
      "With more than one area in My Lineup: the one whose stations are listed first, in the guide and on games.";
  p.items.push_back(city);
  ui::PanelItem refresh{ui::PanelItem::Kind::Action, "Refresh schedule"};
  refresh.about = "Reads every league's schedule again, now.";
  refresh.select = [&app] {
    app.store().refreshSchedule();
    app.toast("Refreshing the schedule");
  };
  dim(refresh, schedule, "Turn on Game schedule");
  p.items.push_back(refresh);
}

void providersRows(App& app, ui::ListPanel& p, SettingsPages& pages) {
  std::vector<AccountSummary> accounts = app.store().accounts();
  if (accounts.empty())
    p.items.push_back(ui::PanelItem::note("No providers yet. Add the one your channels come from."));
  time::Seconds now = time::now();
  for (const AccountSummary& a : accounts) {
    std::string id = a.id, name = a.name;
    // Build 144: the membership's end on its second line, amber in the
    // last week and once it has ended.
    ui::PanelItem it = chevron(a.name, membership::accountLine(a.kind, a.state, a.membership, now, app.zone()), "",
                               [&app, &pages, id, name] { pages.open(providerPage(app, pages, id, name)); });
    it.warn = membership::warns(a.membership, now, app.zone());
    membership::Text info = membership::info(a.membership, a.signInFailed, now, app.zone());
    it.aboutHeading = info.title;
    it.about = (info.detail.empty() ? std::string() : info.detail + "\n") +
               "Its login stays on this TV; the password is never shown. Open it to remove the provider.";
    p.items.push_back(it);
  }
  ui::PanelItem add = chevron("Add a provider", "Xtream, Emby / Jellyfin or an M3U playlist", "",
                              [&app] { app.nav().push(makeAddProvider(app)); });
  add.about = "Channels from every provider you add are merged into one guide.";
  p.items.push_back(add);
  ui::PanelItem phone = chevron("Add from your phone", "Type the server and password on your phone", "",
                                [&app] { app.nav().push(makePhoneSignIn(app, false)); });
  phone.about = "Scan a code with your phone and fill in the form there. It works only on your home network.";
  p.items.push_back(phone);
  p.items.push_back(ui::PanelItem::header(""));
  ui::PanelItem reload{ui::PanelItem::Kind::Action, "Reload channels and listings"};
  reload.detail = "Reads every provider again";
  reload.about = "Reads every provider's channels and the listings again, now. Marigold also does this by itself.";
  reload.select = [&app] {
    // Not while the provider's guide loads (a reload would start it over);
    // during the missing listings it reloads, and they start again after.
    if (app.store().providerGuideLoading()) {
      app.toast("The guide is already loading");
      return;
    }
    app.store().refreshAll();
    app.toast("Reloading");
  };
  dim(reload, !accounts.empty(), "Add a provider first");
  p.items.push_back(reload);
}

struct AdvancedApplies {
  bool relay = false, xtream = false, emby = false;
  bool any() const { return relay || xtream || emby; }
};
AdvancedApplies advancedApplies(App& app) {
  AdvancedApplies a;
  for (const AccountSummary& x : app.store().accounts()) {
    a.xtream = a.xtream || x.kind == "Xtream";
    a.emby = a.emby || x.kind == "Emby / Jellyfin";
  }
  a.relay = platform::TsProxy::wanted();
  return a;
}

void advancedRows(App& app, ui::ListPanel& p, SettingsPages& /*pages*/) {
  AdvancedApplies a = advancedApplies(app);
  if (a.relay)
    p.items.push_back(settingToggle(app, "Play through Marigold",
                                    "Needed for subtitles to start at once. Turn off if channels start worse",
                                    &Settings::playThroughApp,
                                    "Channels go through Marigold on the way to the picture, so captions can be "
                                    "read. A change applies from the next channel."));
  if (a.xtream) {
    int on = app.store().settings().preferTS ? 0 : 1;
    ui::PanelItem format = choiceRow("Xtream stream format", {"MPEG-TS", "HLS"}, on, [&app](int k) {
      Settings n = app.store().settings();
      n.preferTS = k == 0;
      app.store().setSettings(n);
    });
    format.detail = "Try the other one if Xtream channels stutter";
    format.about = "How Xtream channels are asked for. MPEG-TS suits most TVs.";
    p.items.push_back(format);
  }
  if (a.emby)
    p.items.push_back(settingToggle(app, "Experimental: Emby / Jellyfin quick start",
                                    "Starts channels without waiting for the server to open them first",
                                    &Settings::embyQuickStart,
                                    "A test: Emby / Jellyfin channels start from the server's direct link. If the "
                                    "server refuses, they start the usual way."));
  p.items.push_back(ui::PanelItem::note(std::string("A channel's \xe2\x80\x9c" "Always start with\xe2\x80\x9d is in ") +
                                        (app.remoteKeys().colour ? "Sources (the red key)." : "Sources.")));
}

// Build 148: while an update waits (only where the app installs itself),
// the row that installs it; the rows are rebuilt every second, so it shows
// how it goes.
void updateRow(App& app, ui::ListPanel& p) {
  namespace up = engine::updates;
  using Phase = up::Updates::Phase;
  up::Updates* u = app.updates();
  if (!u || (!u->available() && u->phase() == Phase::Idle)) return;
  ui::PanelItem row{ui::PanelItem::Kind::Action, up::kAvailableText};
  row.about = "OK downloads the update and installs it.";
  switch (u->phase()) {
    case Phase::Updating:
      row.detail = up::kUpdatingText;
      row.busy = true;
      break;
    case Phase::Failed:
      row.detail = u->failText();
      row.warn = true;
      break;
    case Phase::Idle:
      row.okLabel = "Update";
      row.select = [&app] {
        if (up::Updates* x = app.updates()) x->update();
      };
      break;
  }
  p.items.push_back(row);
}

void helpRows(App& app, ui::ListPanel& p, SettingsPages& pages) {
  updateRow(app, p);
  ui::PanelItem around = chevron("Getting around", "The remote\xe2\x80\x99s keys, as shown the first time", "",
                                 [&app] { app.nav().push(makeGettingAround(app)); });
  around.tag = "NEW";
  around.about = "The card from the first time you watched: the keys for the guide, channels, subtitles and more.";
  p.items.push_back(around);
  ui::PanelItem report = chevron("Send a report to your phone", "Channel report, playback log or Live Events report",
                                 "", [&app, &pages] { pages.open(reportsPage(app)); });
  report.about = "Pick a report and scan the code with your phone. The link works for 10 minutes, only on your home "
                 "network.\nNo passwords, links or server addresses are in any report.";
  p.items.push_back(report);
  ui::PanelItem diag =
      chevron("Diagnostics", diagnosticsDetail(), "", [&app] { app.nav().push(makeDiagnostics(app)); });
  diag.about = "For checking a TV: what it runs on and how the picture is set up.";
  p.items.push_back(diag);
  ui::PanelItem status = chevron("Status", "Guide listings files, memory in use", "",
                                 [&app, &pages] { pages.open(statusPage(app)); });
  status.about = "When your provider's guide and the free listings were last read, and how much memory Marigold "
                 "uses.";
  p.items.push_back(status);
  p.items.push_back(ui::PanelItem::header("ABOUT"));
  ui::PanelItem version{ui::PanelItem::Kind::Action, "Version"};
  version.value = app.build().version + " (build " + app.build().build + ")";
  version.about = app.build().system.empty() ? "Marigold" : "Marigold on " + app.build().system + ".";
  p.items.push_back(version);
  ui::PanelItem memory{ui::PanelItem::Kind::Action, "Memory in use"};
  std::string inUse = memory::describe(memory::read());
  memory.value = inUse.empty() ? "Not known on this TV" : inUse;
  memory.about = "Marigold's memory now, and the most it has used since it opened.";
  p.items.push_back(memory);
}

}  // namespace

void showOnBoardRows(App& app, ui::ListPanel& p, bool about) {
  bool schedule = app.store().settings().scheduleEnabled;
  auto row = [&](const char* title, const char* detail, bool Settings::*field, const char* text) {
    ui::PanelItem it = settingToggle(app, title, detail, field, about ? text : "");
    p.items.push_back(it);
    return &p.items.back();
  };
  dim(*row("Games not on my channels", "Dimmed at the end of each row", &Settings::showUnwatchable,
           "Games none of your channels show, so you know they're on. Off leaves them out."),
      schedule, "Turn on Game schedule");
  row("More events", "Sports you don't follow", &Settings::boardMoreEvents,
      "A row at the end of the board for sports you don't follow in My Sports.\nOff: they aren't on the board, "
      "but Search still finds them and they play from there.");
  // Build 125: tiles come from the schedule; the rest only in a row of their
  // own, with this one switch.
  ui::PanelItem* unscheduled =
      row("Games without a schedule", "A row of events no schedule lists", &Settings::boardUnscheduled,
          "Events your provider's event channels carry that aren't on any game schedule, such as smaller leagues "
          "and youth games. They get a row of their own at the end of the board, never a sport's row or Live "
          "now. Replays, shows and old slots are still left out.\nOff: the board has scheduled games only. "
          "Search still finds the rest.");
  if (!schedule) {
    // Game schedule off: the board is the provider's events.
    unscheduled->on = true;
    dim(*unscheduled, false, "Always shown while Game schedule is off");
  }
  row("Time not listed", "Events with no start time", &Settings::boardUntimed,
      "Events your channels carry without a start time, so the board can't place them by the clock. They play "
      "when you pick them, if they're on.\nOff: they aren't on the board, but Search still finds them.");
  ui::PanelItem* channels =
      row("Event channels", "Channels that show one event at a time", &Settings::boardEventChannels,
          "Groups of channels your provider sets up for live events, each showing one event at a time. Pick one to "
          "start watching, then use Up and Down to go through the rest of the group.");
  // Game schedule off: the board is the provider's events, so they show.
  if (!schedule) {
    channels->on = true;
    dim(*channels, false, "Always shown while Game schedule is off");
  }
}

bool settingsCategoryShown(App& app, SettingsCategory category) {
  return category != SettingsCategory::Advanced || advancedApplies(app).any();
}

void settingsRows(App& app, SettingsCategory category, ui::ListPanel& p, SettingsPages& pages) {
  switch (category) {
    case SettingsCategory::Watching: watchingRows(app, p, pages); break;
    case SettingsCategory::Guide: guideRows(app, p, pages); break;
    case SettingsCategory::MyLineup: lineupRows(app, p, pages); break;
    case SettingsCategory::LiveEvents: eventsRows(app, p, pages); break;
    case SettingsCategory::Providers: providersRows(app, p, pages); break;
    case SettingsCategory::Advanced: advancedRows(app, p, pages); break;
    case SettingsCategory::Help: helpRows(app, p, pages); break;
  }
}

std::string settingsSummary(App& app, SettingsCategory category) {
  Store& s = app.store();
  const Settings& set = s.settings();
  switch (category) {
    case SettingsCategory::Watching: {
      std::string subs = set.subtitles && set.subtitlesEs ? "Subtitles on"
                         : set.subtitles                  ? "English subtitles on"
                         : set.subtitlesEs                ? "Spanish subtitles on"
                                                          : "Subtitles off";
      return subs + " \xc2\xb7 " + kSizes[std::clamp(set.subtitleSize, 0, 3)] + ", " +
             kLooks[std::clamp(set.subtitleLook, 0, 2)];
    }
    case SettingsCategory::Guide:
      return std::string(set.previewChannels ? "Preview on" : "Preview off") + " \xc2\xb7 " +
             (set.clock24 ? "24-hour clock" : "12-hour clock");
    case SettingsCategory::MyLineup: {
      if (!s.channels()->lineupConfigured) return "Not set up yet";
      catalog::LineupPrefs prefs = s.lineupPrefs();
      std::string where = prefs.markets.empty() ? std::string("No local area") : shortMarket(s.homeMarketName());
      std::string feeds = prefs.coast == catalog::Coast::automatic
                              ? std::string(coastLabel(s.resolvedCoast())) + " feeds (automatic)"
                          : prefs.coast == catalog::Coast::both ? std::string("Both feeds")
                                                                : std::string(coastLabel(prefs.coast)) + " feeds";
      return where + " \xc2\xb7 " + feeds;
    }
    case SettingsCategory::LiveEvents: {
      if (!set.scheduleEnabled) return "Game schedule off";
      std::vector<std::string> leagues = leagueNames(s.mySports());
      std::string what = leagues.empty() ? std::string("My Sports not set") : firstFew(leagues, 4);
      return what + " \xc2\xb7 " + (set.showScores ? "Scores on" : "Scores off");
    }
    case SettingsCategory::Providers: {
      std::vector<AccountSummary> accounts = s.accounts();
      if (accounts.empty()) return "None yet";
      std::vector<std::string> failing;
      bool ended = false;
      for (const AccountSummary& a : accounts) {
        if (!str::startsWith(a.state, "Couldn")) continue;
        failing.push_back(a.name);
        ended = membership::stage(a.membership, time::now(), app.zone()) == membership::Stage::Ended;
      }
      if (failing.empty())
        return accounts.size() == 1 ? accounts.front().name + " \xc2\xb7 Signed in" : std::string("All signed in");
      if (failing.size() == 1) return failing.front() + (ended ? " has ended" : " couldn't sign in");
      return "Some couldn't sign in";
    }
    case SettingsCategory::Advanced: return "Playback options for your providers";
    case SettingsCategory::Help: return "Getting around, reports, version";
  }
  return "";
}

void openEditLineup(App& app) {
  openSettingsAt(app, SettingsCategory::MyLineup, "Edit My Lineup",
                 [](App& a, SettingsPages&) { return editLineupPage(a); });
}

void openProviderPage(App& app, const std::string& accountId) {
  std::string name;
  for (const AccountSummary& a : app.store().accounts())
    if (a.id == accountId) name = a.name;
  openSettingsAt(app, SettingsCategory::Providers, name, [accountId, name](App& a, SettingsPages& pages) {
    return providerPage(a, pages, accountId, name);
  });
}

}  // namespace marigold::app
