// Factories for the screens, so screens can open each other without
// including each other's headers.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "app/screen.hpp"
#include "app/store/store.hpp"

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
// Accounts list and the add-account form. `firstRun`: the form, as the
// remote's way through first-run setup's Provider step.
std::unique_ptr<Screen> makeAccounts(App& app, bool firstRun);
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
// Sources for a channel (red key).
std::unique_ptr<Screen> makeSources(App& app, const std::string& entryId);
// Subtitles (the player's Subtitles pill): Off or a language the stream
// carries; Caption style; the Options page; and Options' one-word summary.
std::unique_ptr<Screen> makeSubtitles(App& app);
std::unique_ptr<Screen> makeCaptionStyle(App& app);
std::unique_ptr<Screen> makeSubtitleSettings(App& app);
std::string subtitleSummary(App& app);
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
// My Sports setup: leagues, favourite teams, review. `onboarding` is
// first-run setup's Sports and Teams steps, which end on the summary.
std::unique_ptr<Screen> makeMySports(App& app, bool onboarding = false);
// Options › Help: share a file with a phone on the home network.
enum class ExportKind { ChannelReport, PlaybackLog, LiveEvents };
std::unique_ptr<Screen> makeExport(App& app, ExportKind kind);
// The platform's diagnostics screen (Options › Help): the key and video
// test on webOS, the system and video state on Android.
std::unique_ptr<Screen> makeDiagnostics(App& app);
// Its name in Options › Help (each platform has its own screen).
const char* diagnosticsTitle();

}  // namespace marigold::app
