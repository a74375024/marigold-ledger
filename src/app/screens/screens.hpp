// Factories for the screens, so screens can open each other without
// including each other's headers.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "app/screen.hpp"
#include "app/store/model.hpp"

namespace marigold::app {

// Full screen playback of whatever Playback holds.
std::unique_ptr<Screen> makePlayer(App& app);
// Left in the player: the channels of the flip list, with what's on.
std::unique_ptr<Screen> makeMiniGuide(App& app);
// Program detail card for a show that has not started.
std::unique_ptr<Screen> makeProgramDetail(App& app, const std::string& entryId, const Listing& l);
// Channel search; `pick` gets the chosen entry id.
std::unique_ptr<Screen> makeSearch(App& app, std::function<void(const std::string&)> pick);
// Options (blue key).
std::unique_ptr<Screen> makeOptions(App& app);
// Accounts list and the add-account form. On first run it is the root.
std::unique_ptr<Screen> makeAccounts(App& app, bool firstRun);
// My Lineup setup wizard.
std::unique_ptr<Screen> makeLineupSetup(App& app);
// Sources for a channel (red key).
std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId);
// Live Events: the board (Live Events tab). `pickTab` gets the guide tab to
// show when the board closes (0 My Lineup, 1 Favorites, 2 Recent).
std::unique_ptr<Screen> makeEventsBoard(App& app, std::function<void(int)> pickTab);
// A game's detail card (upcoming, finished or not watchable).
std::unique_ptr<Screen> makeEventDetail(App& app, const std::string& eventId);
// Every channel and feed for one game.
std::unique_ptr<Screen> makeWhereToWatch(App& app, const std::string& eventId);
// Left in the player while watching a game: that sport's games.
std::unique_ptr<Screen> makeEventGuide(App& app);
// Down while watching a race: its cameras.
std::unique_ptr<Screen> makeCameraPanel(App& app);
// My Sports setup: leagues, favourite teams, review.
std::unique_ptr<Screen> makeMySports(App& app);
// Options › Help: share a file with a phone on the home network.
enum class ExportKind { ChannelReport, PlaybackLog };
std::unique_ptr<Screen> makeExport(App& app, ExportKind kind);
// The M0 diagnostics screen (Options › Help).
std::unique_ptr<Screen> makeDiagnostics(App& app);

}  // namespace marigold::app
