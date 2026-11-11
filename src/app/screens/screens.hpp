// Factories for the screens, so screens can open each other without
// including each other's headers.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "app/screen.hpp"
#include "app/store/store.hpp"
#include "core/input/left_rail.hpp"

namespace marigold::ui {
class ListPanel;
struct Ctx;
struct PanelItem;
}  // namespace marigold::ui

namespace marigold::sports {
struct MySportsPrefs;
}

namespace marigold::app {

// Full screen playback of whatever Playback holds.
std::unique_ptr<Screen> makePlayer(App& app);
// Left in the player: the list the channel playing came from, with what's
// on (Build 116: its neighborhoods and the lists on Left). `list`: open on
// that list's neighborhoods instead (the games rail's lists picked it).
std::unique_ptr<Screen> makeMiniGuide(App& app, std::optional<rail::Item> list = std::nullopt);
// Program detail card for a show that has not started.
std::unique_ptr<Screen> makeProgramDetail(App& app, const std::string& entryId, const Listing& l);
// Search (Build 113), a section of the left rail over the guide or Live
// Events: it plays what is picked itself. `go` gets what its strip or menu
// picks once Search has closed.
std::unique_ptr<Screen> makeSearch(App& app, std::function<void(rail::Item)> go);
// Settings (settings_screen.cpp). Quick settings: the Blue key, the
// player's Settings button and Blue on Live Events open a short side panel
// for where you are (the picture or board still showing), ending with All
// settings and Help & info.
enum class QuickPlace { Guide, Player, LiveEvents };
std::unique_ptr<Screen> makeQuickSettings(App& app, QuickPlace place);
// All settings, full screen: seven categories on the left, the focused
// one's rows in the middle, what the focused row does on the right. This
// is the one way in: the gear (and anything else that opens Settings
// directly) calls it with no category; Quick settings' last rows open it
// inside one.
enum class SettingsCategory { Watching, Guide, MyLineup, LiveEvents, Providers, Advanced, Help };
// `at`: a group's header in that category, the focus on its first row
// (the empty Live Events board opens kShowOnBoard).
void openAllSettings(App& app, std::optional<SettingsCategory> inside = std::nullopt, const std::string& at = {});

// A page inside Settings (Build 144). Panels float only over the picture
// (Sources, Aa, Quick settings): every Settings page opens in Settings'
// own middle column, one level deeper, with the categories dimmed on the
// left, "SETTINGS › PROVIDERS › BACKUP TV" over its title and the focused
// row explained on the right. Back goes up a level, to the row it was
// opened from. A row's select opens one with SettingsPages::open.
struct SettingsPage {
  std::string title;  // over the rows ("Backup TV")
  std::string crumb;  // its part of the line above when shorter ("Send a report")
  // The rows, built again after every change (toggles show their new state).
  std::function<void(ui::ListPanel&)> build;
  bool liveRows = false;  // built again once a second (live state)
  // The right column drawn by the page itself (a report's code) for the
  // focused row (may be null); false: the row's about, as everywhere.
  std::function<bool(ui::Ctx&, float x, float y, float w, const ui::PanelItem*)> info;
  // Something on it that moves by itself: true when it needs drawing, and
  // when it next will (the spinner while a report builds).
  std::function<bool(uint32_t ms)> tick;
  std::function<uint32_t()> wakeAt;
};
class SettingsPages {
 public:
  virtual ~SettingsPages() = default;
  // One level deeper, once the key that asked for it is done.
  virtual void open(SettingsPage page) = 0;
  // Back a level, to the row it was opened from (a removed provider's page).
  virtual void close() = 0;
};
using SettingsPageMaker = std::function<SettingsPage(App&, SettingsPages&)>;
// Settings opened at one of its pages from outside it (Quick settings' Edit
// My Lineup, the first-run providers list): `category`, on its row titled
// `row`, with `page` open over it. Back from that page (or removing the
// provider on it) closes Settings, back to where it was opened from.
void openSettingsAt(App& app, SettingsCategory category, const std::string& row, SettingsPageMaker page);
inline constexpr const char* kShowOnBoard = "SHOW ON THE BOARD";
// Settings › Live Events › Show on the board (Build 119): Games not on my
// channels, More events, Time not listed and Event channels; also opened
// from the board's Quick settings. `about`: with the right column's text.
void showOnBoardRows(App& app, ui::ListPanel& p, bool about);
// A category's rows and its one-line summary (settings_rows.cpp). Advanced
// is only listed when one of its rows applies to this TV and its providers.
// `pages` opens the pages its rows lead to.
void settingsRows(App& app, SettingsCategory category, ui::ListPanel& p, SettingsPages& pages);
std::string settingsSummary(App& app, SettingsCategory category);
bool settingsCategoryShown(App& app, SettingsCategory category);
// Settings › My Lineup › Edit My Lineup, opened from the guide's Quick
// settings (openSettingsAt).
void openEditLineup(App& app);
// "Getting around": the remote's keys. The player shows it once, the first
// time a channel plays; Settings › Help & info shows it again (`again`).
std::unique_ptr<Screen> makeGettingAround(App& app);
void drawGettingAround(ui::Ctx& c, const RemoteKeys& keys, bool again, int hitOk);
// Accounts list and the add-account form. `firstRun`: the form, as the
// remote's way through first-run setup's Provider step. makeAddProvider:
// only the form (Settings › Providers › Add a provider); Back closes it.
std::unique_ptr<Screen> makeAccounts(App& app, bool firstRun);
std::unique_ptr<Screen> makeAddProvider(App& app);
// Settings › Providers › one provider (its sign-in, membership and Remove,
// which asks on its row first), opened from the providers list outside
// Settings (openSettingsAt).
void openProviderPage(App& app, const std::string& accountId);
// First-run setup (onboarding.cpp): the welcome (the root until a provider
// is added), signing in from a phone, "Getting your guide ready" after the
// teams (it waits until the guide is usable, then carries on by itself) and
// the summary at the end.
std::unique_ptr<Screen> makeWelcome(App& app);
// The provider from a phone on the home network. `firstRun` carries on to
// My Lineup's Area step; otherwise it closes once the channels are in.
std::unique_ptr<Screen> makePhoneSignIn(App& app, bool firstRun);
std::unique_ptr<Screen> makeSetupGuide(App& app);
// `guideReady`: reached through that wait (My Lineup and the guide are in).
std::unique_ptr<Screen> makeSetupDone(App& app, bool guideReady = false);
// My Lineup setup: Area, Languages, Channels. `onboarding` is first-run
// setup's steps 2-4, which carries on to My Sports.
std::unique_ptr<Screen> makeLineupSetup(App& app, bool onboarding = false);
// One of its steps as a single page that saves as you go (Settings › My
// Lineup › Your area, Countries & languages, Kinds of channels).
enum class LineupStep { Area, Countries, Kinds };
std::unique_ptr<Screen> makeLineupStep(App& app, LineupStep step);
// Sources for a channel (red key).
std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId);
// "Listings look wrong?": the channel's distinct schedules, to pick the
// right one (Build 153).
std::unique_ptr<Screen> makeGuideChoice(App& app, const std::string& entryId);
// Subtitles (the player's Aa): Off or a language the stream carries, the
// timing and the style.
std::unique_ptr<Screen> makeSubtitles(App& app);
// The Caption timing row (Aa and Settings): `ms` now, `pick` gets the new one.
ui::PanelItem captionTimingRow(int ms, std::function<void(int)> pick);
// Live Events: the board, the left rail's Live Events section over the
// guide. `go` gets what its strip or menu picks for the guide: a guide
// section once the board has closed (`column`: land on its neighborhoods),
// or Settings (the guide's openOptions). `sports` opens on the SPORTS column.
std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(rail::Item, bool)> go, bool sports = false);
// A game's detail card (upcoming, finished or not watchable).
std::unique_ptr<Screen> makeEventDetail(App& app, const std::string& eventId);
// Every channel and feed for one game.
std::unique_ptr<Screen> makeWhereToWatch(App& app, const std::string& eventId);
// Left in the player while watching a game: that sport's games. `sports`
// opens it on its SPORTS column (the guide's lists picked Live Events).
std::unique_ptr<Screen> makeEventGuide(App& app, bool sports = false);
// Down while watching a race: its cameras.
std::unique_ptr<Screen> makeCameraPanel(App& app);
// My Sports setup: leagues, favourite teams, review. `onboarding` is
// first-run setup's Sports and Teams steps, which end on the summary.
// `teams` starts on the teams (Settings › Live Events › My teams).
std::unique_ptr<Screen> makeMySports(App& app, bool onboarding = false, bool teams = false);
// My Sports' pages (Build 127, sports_picks.cpp). They edit a copy of
// `prefs`; Back hands it to `done` (My Sports keeps it in its draft, the
// board saves it). Soccer: which competitions, a rail of groups and a grid.
// College: each college sport's Top 25, conferences, schools and "Every
// Division I game", opened on `league` ("NCAAF").
using PicksDone = std::function<void(const sports::MySportsPrefs&)>;
std::unique_ptr<Screen> makeSoccerPicks(App& app, const sports::MySportsPrefs& prefs, PicksDone done);
std::unique_ptr<Screen> makeCollegePicks(App& app, const sports::MySportsPrefs& prefs, const std::string& league,
                                         PicksDone done);
// The picks the board goes by: the saved ones, or until My Sports is set
// up, the defaults for your languages (with anything already saved).
sports::MySportsPrefs boardPicks(App& app);
// The same pages from Live Events' "Choose competitions" / "Choose schools"
// tiles: they start from boardPicks() and save on Back when changed.
void openSoccerPicks(App& app);
void openCollegePicks(App& app, const std::string& league);
// A pick's name for lists ("Premier League", "USA men and women", "Golf").
std::string pickName(const std::string& id);
// A short note at the bottom of the screen with an Undo button, which has
// the focus: OK undoes, Back or any other key closes it, and it closes by
// itself after a few seconds (Hide on a game card: "Liga MX hidden").
std::unique_ptr<Screen> makeUndoNote(App& app, std::string text, std::function<void()> undo);
// Settings › Help & info › Send a report to your phone: a report shared
// with a phone on the home network (report_share.hpp).
enum class ExportKind { ChannelReport, PlaybackLog, LiveEvents };
// The platform's diagnostics screen (Settings › Help & info ›
// Diagnostics): the key and video test on webOS, the system and video state
// on Android and Apple TV.
std::unique_ptr<Screen> makeDiagnostics(App& app);
// What it is on this platform, under its row.
const char* diagnosticsDetail();

}  // namespace marigold::app
