// The data folder kept small (disk.hpp): which names are core dumps and
// unfinished writes, when a big write is skipped for lack of space, what
// the prune removes, and the store doing it on the pretend TV.
#include <doctest.h>

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/engine/disk.hpp"
#include "core/engine/store.hpp"
#include "core/guide/outside.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"

using namespace marigold;
using namespace marigold::engine;
namespace fs = std::filesystem;

namespace {

constexpr uint64_t MB = disk::kMB;

void put(const std::string& path, size_t bytes = 10, int64_t mtime = 0) {
  fs::create_directories(fs::path(path).parent_path());
  std::ofstream(path, std::ios::binary) << std::string(bytes, 'x');
  if (mtime) fs::last_write_time(path, fs::file_time_type::clock::now() - std::chrono::hours(mtime));
}

bool has(const std::string& path) { return fs::exists(path); }

std::vector<std::string> named(const std::string& dir, const std::string& prefix) {
  std::vector<std::string> out;
  for (const auto& e : fs::directory_iterator(dir)) {
    std::string name = e.path().filename().string();
    if (name.rfind(prefix, 0) == 0) out.push_back(name);
  }
  return out;
}

disk::Entry entry(std::string name, uint64_t bytes, int64_t mtime = 0) {
  return disk::Entry{std::move(name), bytes, mtime};
}

bool removes(const disk::Plan& p, const std::string& name) {
  return std::find(p.remove.begin(), p.remove.end(), name) != p.remove.end();
}

// A guide file for the harbor playlist below.
constexpr const char* kList = "http://mock.example.invalid:8080/harbor.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/harbor.xml";

void answerHarbor(fake::Tv& tv) {
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"harbor.news\" group-title=\"Harbor TV\",Harbor News HD\n"
                 "http://mock.example.invalid:8080/a.m3u8\n");
  std::string xml =
      "<?xml version=\"1.0\"?>\n<tv>\n<channel id=\"harbor.news\"><display-name>Harbor News</display-name></channel>\n";
  time::Seconds hour = time::now() / 3600 * 3600;
  auto stamp = [](time::Seconds t) {
    time::Civil c = time::civilFromSeconds(t);
    char b[32];
    std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
    return std::string(b);
  };
  for (int i = -1; i < 12; ++i)
    xml += "<programme start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) +
           "\" channel=\"harbor.news\"><title>Harbor Report " + std::to_string(i + 2) + "</title></programme>\n";
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
}

void addHarbor(Store& store) {
  AccountForm form = fake::m3uForm();
  form.server = kList;
  form.guideUrl = kGuide;
  store.addAccount(form, [](std::string) {});
}

}  // namespace

TEST_CASE("disk: core dumps are core and core.<pid> only") {
  CHECK(disk::isCoreDump("core"));
  CHECK(disk::isCoreDump("core.12345"));
  CHECK(disk::isCoreDump("core.1"));
  CHECK_FALSE(disk::isCoreDump("corefile"));
  CHECK_FALSE(disk::isCoreDump("core.txt"));
  CHECK_FALSE(disk::isCoreDump("core."));
  CHECK_FALSE(disk::isCoreDump("core.123a"));
  CHECK_FALSE(disk::isCoreDump("mycore.123"));
  CHECK_FALSE(disk::isCoreDump("Core.123"));
  CHECK_FALSE(disk::isCoreDump(""));
}

TEST_CASE("disk: unfinished writes by name") {
  CHECK(disk::isStaleTemp("lists.json.tmp"));
  CHECK(disk::isStaleTemp("live-events.json.tmp12"));  // events_build's own names
  CHECK(disk::isStaleTemp("us-locals.part"));          // a guide download
  CHECK_FALSE(disk::isStaleTemp(".tmp"));
  CHECK_FALSE(disk::isStaleTemp(".part"));
  CHECK_FALSE(disk::isStaleTemp("settings.json"));
  CHECK_FALSE(disk::isStaleTemp("notes.tmpl"));
  CHECK_FALSE(disk::isStaleTemp("tmp"));
  CHECK_FALSE(disk::isStaleTemp("a.tmp.json"));
}

TEST_CASE("disk: what each file is") {
  CHECK(disk::familyOf("catalog-0a1b2c3d4e5f6071-first.json") == disk::Family::catalog);
  CHECK(disk::familyOf("catalog-0a1b2c3d4e5f6071-full.json") == disk::Family::catalog);
  CHECK(disk::familyOf("listings-default_xt_ab12.json") == disk::Family::listings);
  CHECK(disk::familyOf("provider-default_xt_ab12.json") == disk::Family::provider);
  CHECK(disk::familyOf("guides/us-locals") == disk::Family::guide);
  CHECK(disk::familyOf("logos/0123abcd") == disk::Family::logo);
  CHECK(disk::familyOf("guides/us-locals.part") == disk::Family::other);  // a temp: launch only
  CHECK(disk::familyOf("catalog-0a1b-full.json.tmp") == disk::Family::other);
  for (const char* other : {"accounts.json", "accounts-secrets.json", "settings.json", "lineup.json", "marigold.log",
                            "playback-log.jsonl", "crash.txt", "m3u-streams.json", "catalog.json", "elsewhere/x"})
    CHECK(disk::familyOf(other) == disk::Family::other);
}

TEST_CASE("disk: a big write needs its size and the margin free") {
  disk::Limits l{400 * MB, 100 * MB, 1 * MB, 4 * MB};
  CHECK(disk::roomFor(300 * MB, 54 * MB, l));
  CHECK(disk::roomFor(154 * MB, 54 * MB, l));  // exactly the margin left
  CHECK_FALSE(disk::roomFor(153 * MB, 54 * MB, l));
  CHECK_FALSE(disk::roomFor(20 * MB, 54 * MB, l));  // not even the file
  CHECK_FALSE(disk::roomFor(99 * MB, 0, l));         // already inside the margin
  CHECK(disk::roomFor(std::nullopt, 54 * MB, l));    // unknown: allowed
}

TEST_CASE("disk: the keeper skips big writes on a nearly full partition, once logged") {
  uint64_t free = 120 * MB;
  int asked = 0;
  disk::Keeper k("/data", [&](const std::string& dir) -> std::optional<uint64_t> {
    CHECK(dir == "/data");
    ++asked;
    return free;
  });
  CHECK(k.roomFor("/data/settings.json", 4096));  // small: not even looked at
  CHECK(asked == 0);
  CHECK(k.roomFor("/data/provider-a.json", 15 * MB));
  CHECK_FALSE(k.roomFor("/data/guides/us-locals", 54 * MB));
  CHECK(asked == 2);
  CHECK(k.stillRoom("/data/guides/us-locals"));
  free = 90 * MB;
  CHECK_FALSE(k.stillRoom("/data/guides/us-locals"));
  free = 2000 * MB;
  CHECK(k.roomFor("/data/guides/us-locals", 54 * MB));  // room again: written again

  disk::Keeper nowhere("", [](const std::string&) -> std::optional<uint64_t> { return 0; });
  CHECK(nowhere.roomFor("x", 500 * MB));  // no data folder: nothing to protect
}

TEST_CASE("disk: the plan removes what has no owner, then logos and spare guides while over budget") {
  std::vector<disk::Entry> files = {
      entry("accounts.json", 1000),
      entry("settings.json", 1000),
      entry("marigold.log", 1 * MB),
      entry("catalog-aaaa-first.json", 1 * MB),
      entry("catalog-aaaa-full.json", 3 * MB),
      entry("catalog-bbbb-full.json", 3 * MB),          // an old provider set's list
      entry("provider-default_xt_gone.json", 20 * MB),  // a removed provider's guide
      entry("provider-default_xt_here.json", 20 * MB),
      entry("listings-default_xt_here.json", 10 * MB),
      entry("guides/us-locals", 54 * MB, 100),
      entry("guides/uk", 60 * MB, 100),                       // an area no longer chosen
      entry("guides/backup--epgtalk-us", 30 * MB, 50),  // a backup source
      entry("logos/old", 2 * MB, 10),
      entry("logos/new", 2 * MB, 90),
  };
  disk::Owners o;
  o.known = true;
  o.keep = {"catalog-aaaa-first.json", "catalog-aaaa-full.json", "provider-default_xt_here.json",
            "listings-default_xt_here.json", "guides/us-locals", "guides/backup--epgtalk-us"};
  o.spare = {"guides/backup--epgtalk-us"};

  SUBCASE("in budget: only what has no owner") {
    disk::Plan p = disk::plan(files, o, 400 * MB);
    CHECK(p.remove.size() == 3);
    CHECK(removes(p, "catalog-bbbb-full.json"));
    CHECK(removes(p, "provider-default_xt_gone.json"));
    CHECK(removes(p, "guides/uk"));
    CHECK(p.unowned == 3);
    CHECK(p.after == p.before - 83 * MB);
    CHECK_FALSE(p.over);
  }
  SUBCASE("over budget: the oldest logo first, then the spare guide") {
    disk::Plan p = disk::plan(files, o, 100 * MB);
    // 206 MB in all; 123 MB once the unowned go; the old logo makes 121,
    // the new one 119, the backup guide 89.
    REQUIRE(p.remove.size() == 6);
    CHECK(p.remove[3] == "logos/old");
    CHECK(p.remove[4] == "logos/new");
    CHECK(p.remove[5] == "guides/backup--epgtalk-us");
    CHECK_FALSE(p.over);
  }
  SUBCASE("in use is never removed, even over budget") {
    disk::Plan p = disk::plan(files, o, 50 * MB);
    CHECK(p.over);
    CHECK_FALSE(removes(p, "guides/us-locals"));
    CHECK_FALSE(removes(p, "provider-default_xt_here.json"));
    CHECK_FALSE(removes(p, "accounts.json"));
    CHECK_FALSE(removes(p, "marigold.log"));
  }
  SUBCASE("owners unknown: nothing removed for want of one") {
    o.known = false;
    disk::Plan p = disk::plan(files, o, 400 * MB);
    CHECK(p.remove.empty());
    CHECK(p.unowned == 0);
  }
  SUBCASE("logos only as far as needed") {
    disk::Plan p = disk::plan(files, o, 122 * MB);
    REQUIRE(p.remove.size() == 4);
    CHECK(p.remove.back() == "logos/old");
    CHECK(p.after == 121 * MB + 2000);
  }
}

TEST_CASE("disk: launch removes core dumps and unfinished writes, nothing else") {
  fake::Files folder;
  const std::string d = folder.dir();
  put(d + "/core");
  put(d + "/core.12345", 4096);
  put(d + "/corefile");
  put(d + "/core.txt");
  fs::create_directories(d + "/core.777");  // a folder: left alone
  put(d + "/settings.json");
  put(d + "/lists.json.tmp");
  put(d + "/live-events.json.tmp3");
  put(d + "/guides/us-locals.part");
  put(d + "/guides/us-locals");
  put(d + "/logos/ab12.tmp");
  put(d + "/logos/ab12");

  disk::Swept cores = disk::removeCoreDumps(d);
  CHECK(cores.files == 2);
  CHECK(cores.bytes == 4096 + 10);
  CHECK_FALSE(has(d + "/core"));
  CHECK_FALSE(has(d + "/core.12345"));
  CHECK(has(d + "/corefile"));
  CHECK(has(d + "/core.txt"));
  CHECK(has(d + "/core.777"));

  disk::Swept temps = disk::removeStaleTemps(d);
  CHECK(temps.files == 4);
  CHECK_FALSE(has(d + "/lists.json.tmp"));
  CHECK_FALSE(has(d + "/live-events.json.tmp3"));
  CHECK_FALSE(has(d + "/guides/us-locals.part"));
  CHECK_FALSE(has(d + "/logos/ab12.tmp"));
  CHECK(has(d + "/guides/us-locals"));
  CHECK(has(d + "/logos/ab12"));
  CHECK(has(d + "/settings.json"));

  CHECK(disk::removeCoreDumps("").files == 0);
  CHECK(disk::removeStaleTemps(d + "/nowhere").files == 0);
}

TEST_CASE("disk: the keeper's prune on a real folder") {
  fake::Files folder;
  const std::string d = folder.dir();
  put(d + "/settings.json");
  put(d + "/provider-default_xt_gone.json", 2000);
  put(d + "/provider-default_xt_here.json", 2000);
  put(d + "/logos/a", 3000, 20);
  put(d + "/logos/b", 3000, 10);
  disk::Owners o;
  o.known = true;
  o.keep = {"provider-default_xt_here.json"};
  disk::Keeper k(d, {}, disk::Limits{8000, 100 * MB, 1 * MB, 4 * MB});
  disk::Plan p = k.prune(o);
  CHECK(p.before == 10010);
  CHECK(p.after == 5010);
  CHECK_FALSE(has(d + "/provider-default_xt_gone.json"));
  CHECK_FALSE(has(d + "/logos/a"));  // used longest ago
  CHECK(has(d + "/logos/b"));
  CHECK(has(d + "/provider-default_xt_here.json"));
  CHECK(has(d + "/settings.json"));
  CHECK(disk::list(d).size() == 3);
}

TEST_CASE("disk: launch removes what an older session left without an owner") {
  fake::Tv tv;
  const std::string d = tv.files.dir();
  std::string guides = d + "/" + disk::kGuidesDir + "/";
  std::ofstream(d + "/settings.json") << "{}";  // the default guide areas
  put(d + "/marigold.log");
  put(d + "/provider-default_xt_gone.json");
  put(d + "/listings-default_xt_gone.json");
  put(d + "/catalog-0123456789abcdef-full.json");
  put(guides + "uk");  // not chosen by default
  put(guides + "us-locals");
  {
    Store store(tv.host());
    tv.pool.drain();
    CHECK_FALSE(has(d + "/provider-default_xt_gone.json"));
    CHECK_FALSE(has(d + "/listings-default_xt_gone.json"));
    CHECK_FALSE(has(d + "/catalog-0123456789abcdef-full.json"));
    CHECK_FALSE(has(guides + "uk"));
    CHECK(has(guides + "us-locals"));
    CHECK(has(d + "/settings.json"));
    CHECK(has(d + "/marigold.log"));
  }

  // Missing listings switched off: the outside guide files have no use.
  std::ofstream(d + "/settings.json") << R"({"fillMissingListings":false})";
  {
    Store store(tv.host());
    tv.pool.drain();
    CHECK_FALSE(has(guides + "us-locals"));
  }
}

TEST_CASE("disk: an accounts file that doesn't read keeps every cache") {
  fake::Tv tv;
  const std::string d = tv.files.dir();
  std::ofstream(d + "/accounts.json") << "{ not json";
  put(d + "/provider-default_xt_maybe.json");
  Store store(tv.host());
  tv.pool.drain();
  CHECK(has(d + "/provider-default_xt_maybe.json"));
}

TEST_CASE("disk: on a nearly full partition the provider guide is used from memory, not saved") {
  fake::Tv tv;
  answerHarbor(tv);
  Host host = tv.host();
  host.freeSpace = [](const std::string&) -> std::optional<uint64_t> { return 60 * MB; };
  Store store(std::move(host));
  addHarbor(store);
  fake::settle(store, tv);
  REQUIRE(store.guideListings().providers.size() == 1);
  CHECK(store.listings()->byChannel.size() == 1);  // Harbor News has its listings
  CHECK(named(tv.files.dir(), "provider-").empty());
  CHECK(named(tv.files.dir(), "catalog-").empty());
  for (const auto& e : fs::recursive_directory_iterator(tv.files.dir()))
    CHECK_FALSE(disk::isStaleTemp(e.path().filename().string()));
  // Settings and accounts are small: always written.
  CHECK(fs::exists(tv.files.path("accounts.json")));
}

TEST_CASE("disk: with room the provider guide is saved, and goes with its provider") {
  fake::Tv tv;
  answerHarbor(tv);
  Host host = tv.host();
  host.freeSpace = [](const std::string&) -> std::optional<uint64_t> { return 4000 * MB; };
  Store store(std::move(host));
  addHarbor(store);
  fake::settle(store, tv);
  REQUIRE(named(tv.files.dir(), "provider-").size() == 1);
  CHECK(named(tv.files.dir(), "catalog-").size() == 2);

  REQUIRE(store.accounts().size() == 1);
  store.removeAccount(store.accounts().front().id);
  fake::settle(store, tv);
  CHECK(named(tv.files.dir(), "provider-").empty());
  CHECK(named(tv.files.dir(), "catalog-").empty());
}
