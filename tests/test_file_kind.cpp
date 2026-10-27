// Which files an Apple TV must keep for good: what the user made, never
// what is rebuilt from the sources.
#include <doctest.h>

#include "core/engine/host.hpp"

using marigold::engine::FileKind;
using marigold::engine::fileKind;

TEST_CASE("file kinds: user data is kept, passwords are secret, the rest is cache") {
  CHECK(fileKind("accounts.json") == FileKind::Keep);
  CHECK(fileKind("accounts-secrets.json") == FileKind::Secret);
  CHECK(fileKind("settings.json") == FileKind::Keep);
  CHECK(fileKind("lineup.json") == FileKind::Keep);
  CHECK(fileKind("lists.json") == FileKind::Keep);
  CHECK(fileKind("reminders.json") == FileKind::Keep);
  CHECK(fileKind("my-sports.json") == FileKind::Keep);
  CHECK(fileKind("game-sources.json") == FileKind::Keep);
  CHECK(fileKind("followed-drivers.json") == FileKind::Keep);
  CHECK(fileKind("live-events.json") == FileKind::Cache);
  CHECK(fileKind("sport-feeds.json") == FileKind::Cache);
  CHECK(fileKind("race-data.json") == FileKind::Cache);
  CHECK(fileKind("playback-log.json") == FileKind::Cache);
  CHECK(fileKind("marigold.log") == FileKind::Cache);
  CHECK(fileKind("lineup.json.tmp") == FileKind::Cache);
}
