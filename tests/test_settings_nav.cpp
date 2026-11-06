// Build 144: Settings' pages open in place, one level deeper each time.
#include <string>
#include <vector>

#include "core/input/hints.hpp"
#include "core/input/settings_nav.hpp"
#include "doctest.h"

namespace nav = marigold::settings_nav;
using marigold::RemoteKeys;
using nav::Back;

namespace {
std::string labels(const marigold::hints::Bar& b) {
  std::string out;
  for (const auto& h : b.keys) out += (out.empty() ? "" : " | ") + h.cap + " " + h.label;
  return out;
}
}  // namespace

TEST_CASE("settings nav: the line over the title says where you are") {
  CHECK(nav::crumb({}) == "SETTINGS");
  CHECK(nav::crumb({"Providers"}) == "SETTINGS \xe2\x80\xba PROVIDERS");
  CHECK(nav::crumb({"Providers", "Backup TV"}) == "SETTINGS \xe2\x80\xba PROVIDERS \xe2\x80\xba BACKUP TV");
  CHECK(nav::crumb({"Help & info", "Send a report"}) ==
        "SETTINGS \xe2\x80\xba HELP & INFO \xe2\x80\xba SEND A REPORT");
  CHECK(nav::crumb({"My Lineup", "", "Edit My Lineup"}) ==
        "SETTINGS \xe2\x80\xba MY LINEUP \xe2\x80\xba EDIT MY LINEUP");
  // Only ASCII letters change: "Español" keeps its ñ.
  CHECK(nav::crumb({"Espa\xc3\xb1ol"}) == "SETTINGS \xe2\x80\xba ESPA\xc3\xb1OL");
}

TEST_CASE("settings nav: Back steps up one level at a time") {
  // A row asking first stops asking, whatever the level.
  CHECK(nav::back(true, 0, true) == Back::Disarm);
  CHECK(nav::back(true, 2, true) == Back::Disarm);
  // Pages: up one level (to the row it was opened from), nested too.
  CHECK(nav::back(false, 2, true) == Back::UpOneLevel);
  CHECK(nav::back(false, 1, true) == Back::UpOneLevel);
  // The category's rows: the categories; the categories: Settings closes.
  CHECK(nav::back(false, 0, true) == Back::ToCategories);
  CHECK(nav::back(false, 0, false) == Back::Close);
  // Opened at a page from outside (Quick settings' Edit My Lineup): Back
  // from that page closes Settings; a page opened from it still goes up.
  CHECK(nav::back(false, 1, true, 1) == Back::Close);
  CHECK(nav::back(false, 2, true, 1) == Back::UpOneLevel);
  CHECK(nav::back(true, 1, true, 1) == Back::Disarm);
}

TEST_CASE("settings nav: a row asks on itself, and only while it has the focus") {
  nav::Arm arm;
  CHECK_FALSE(arm.armed());
  arm.arm(4, "Remove this provider");
  CHECK(arm.armed());
  CHECK(arm.on(4));
  CHECK_FALSE(arm.on(3));
  // Rebuilt once a second with the same rows: still asking.
  arm.keepIf(4, "Remove this provider");
  CHECK(arm.on(4));
  // The rows changed under it (another row in its place): not any more.
  arm.keepIf(4, "Clear playback log");
  CHECK_FALSE(arm.armed());
  // Moving away.
  arm.arm(2, "Clear playback log");
  arm.keepIf(3, "Live Events report");
  CHECK_FALSE(arm.armed());
  arm.arm(2, "Clear playback log");
  arm.clear();
  CHECK_FALSE(arm.on(2));
}

TEST_CASE("settings nav: back from a task on the row it was opened from") {
  std::vector<std::string> before = {"Home TV", "Backup TV", "Add a provider", "Add from your phone"};
  // A provider was added above "Add a provider": the focus moves with it.
  std::vector<std::string> after = {"Home TV", "Backup TV", "Sports playlist", "Add a provider",
                                    "Add from your phone"};
  CHECK(nav::refocus(after, 2, "Add a provider") == 3);
  // Nothing moved.
  CHECK(nav::refocus(before, 1, "Backup TV") == 1);
  // Gone (removed elsewhere): the same place.
  CHECK(nav::refocus({"Home TV", "Add a provider"}, 1, "Backup TV") == 1);
  // A name twice (Edit My Lineup: added by you, and in My Lineup): the one
  // nearest where it was.
  CHECK(nav::refocus({"Golf", "Tennis", "KVTX", "Golf"}, 3, "Golf") == 3);
  CHECK(nav::refocus({"Golf", "Tennis", "KVTX", "Golf"}, 1, "Golf") == 0);
  CHECK(nav::refocus({"A", "B"}, 1, "") == 1);
}

TEST_CASE("settings nav: the key hints on a page and while a row asks") {
  using marigold::hints::settingsPage;
  CHECK(labels(settingsPage(RemoteKeys::lg(), "Remove\xe2\x80\xa6", "Providers")) ==
        "OK Remove\xe2\x80\xa6 | Back Providers");
  CHECK(labels(settingsPage(RemoteKeys::lg(), "Remove", "Keep it")) == "OK Remove | Back Keep it");
  CHECK(labels(settingsPage(RemoteKeys::lg(), "Stop sharing", "Help & info", false, "Next report")) ==
        "OK Stop sharing | \xe2\x96\xbc Next report | Back Help & info");
  CHECK(labels(settingsPage(RemoteKeys::lg(), "Select", "My Lineup", true)) ==
        "\xe2\x97\x80\xe2\x96\xb6 Change | OK Select | Back My Lineup");
  // A dimmed row: OK does nothing, so no OK.
  CHECK(labels(settingsPage(RemoteKeys::lg(), "", "Guide")) == "Back Guide");
  CHECK(settingsPage(RemoteKeys::appleTv(), "Open", "Providers").keys[0].cap == "Click");
  // The category's rows: OK's word for the row ("Open" on a chevron).
  CHECK(labels(marigold::hints::settings(RemoteKeys::lg(), true, false, "Open")) == "OK Open | Back Categories");
}
