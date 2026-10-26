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
// The M0 diagnostics screen (Options › Help).
std::unique_ptr<Screen> makeDiagnostics(App& app);

}  // namespace marigold::app
