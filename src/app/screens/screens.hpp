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
}  // namespace marigold::ui

namespace marigold::app {

// Full screen playback of whatever Playback holds.
std::unique_ptr<Screen> makePlayer(App& app);
// Left in the player: the channels of the flip list, with what's on.
std::unique_ptr<Screen> makeMiniGuide(App& app);
// Program detail card for a show that has not started.
std::unique_ptr<Screen> makeProgramDetail(App& app, const std::string& entryId, const Listing& l);
// Channel search; `pick` gets the chosen entry id.
std::unique_ptr<Screen> makeSearch(App& app, std::function<void(const std::string&)> pick);
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
void openAllSettings(App& app, std::optional<SettingsCategory> inside = std::nullopt);
// A category's rows and its one-line summary (settings_rows.cpp). Advanced
// is only listed when one of its rows applies to this TV and its providers.
void settingsRows(App& app, SettingsCategory category, ui::ListPanel& p);
std::string settingsSummary(App& app, SettingsCategory category);
bool settingsCategoryShown(App& app, SettingsCategory category);
// Settings › My Lineup › Edit My Lineup (also in the guide's Quick settings).
std::unique_ptr<Screen> makeEditLineup(App& app);
// "Getting around": the remote's keys. The player shows it once, the first
// time a channel plays; Settings › Help & info shows it again (`again`).
std::unique_ptr<Screen> makeGettingAround(App& app);
void drawGettingAround(ui::Ctx& c, const RemoteKeys& keys, bool again, int hitOk);
// Accounts list and the add-account form. `firstRun`: the form, as the
// remote's way through first-run setup's Provider step. makeAddProvider:
// only the form (Settings › Providers › Add a provider); Back closes it.
std::unique_ptr<Screen> makeAccounts(App& app, bool firstRun);
std::unique_ptr<Screen> makeAddProvider(App& app);
// One provider: its state and Remove (which asks first).
std::unique_ptr<Screen> makeProvider(App& app, const std::string& accountId);
// First-run setup (onboarding.cpp): the welcome (the root until a provider
// is added), signing in from a phone, and the summary at the end.
std::unique_ptr<Screen> makeWelcome(App& app);
// The provider from a phone on the home network. `firstRun` carries on to
// My Lineup's Area step; otherwise it closes once the channels are in.
std::unique_ptr<Screen> makePhoneSignIn(App& app, bool firstRun);
std::unique_ptr<Screen> makeSetupDone(App& app);
// My Lineup setup: Area, Languages, Channels. `onboarding` is first-run
// setup's steps 2-4, which carries on to My Sports.
std::unique_ptr<Screen> makeLineupSetup(App& app, bool onboarding = false);
// One of its steps as a single page that saves as you go (Settings › My
// Lineup › Your area, Countries & languages, Kinds of channels).
enum class LineupStep { Area, Countries, Kinds };
std::unique_ptr<Screen> makeLineupStep(App& app, LineupStep step);
// Sources for a channel (red key).
std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId);
// Subtitles (the player's Aa): Off or a language the stream carries, and
// the style.
std::unique_ptr<Screen> makeSubtitles(App& app);
// Live Events: the board, the left rail's Live Events section over the
// guide. `go` gets what its strip or menu picks for the guide: a guide
// section once the board has closed (`column`: land on its neighborhoods),
// or Settings (the guide's openOptions). `sports` opens on the SPORTS column.
std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(rail::Item, bool)> go, bool sports = false);
// A game's detail card (upcoming, finished or not watchable).
std::unique_ptr<Screen> makeEventDetail(App& app, const std::string& eventId);
// Every channel and feed for one game.
std::unique_ptr<Screen> makeWhereToWatch(App& app, const std::string& eventId);
// Left in the player while watching a game: that sport's games.
std::unique_ptr<Screen> makeEventGuide(App& app);
// Down while watching a race: its cameras.
std::unique_ptr<Screen> makeCameraPanel(App& app);
// My Sports setup: leagues, favourite teams, review. `onboarding` is
// first-run setup's Sports and Teams steps, which end on the summary.
// `teams` starts on the teams (Settings › Live Events › My teams).
std::unique_ptr<Screen> makeMySports(App& app, bool onboarding = false, bool teams = false);
// Settings › Help & info › Send a report to your phone: share a file with a
// phone on the home network.
enum class ExportKind { ChannelReport, PlaybackLog, LiveEvents };
std::unique_ptr<Screen> makeExport(App& app, ExportKind kind);
// The platform's diagnostics screen (Settings › Help & info ›
// Diagnostics): the key and video test on webOS, the system and video state
// on Android and Apple TV.
std::unique_ptr<Screen> makeDiagnostics(App& app);
// What it is on this platform, under its row.
const char* diagnosticsDetail();

}  // namespace marigold::app
