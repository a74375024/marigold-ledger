// In-app updates (Build 148): the check's pace, what counts as newer, the
// file for this device, the digest, the card once a day, and nothing at all
// where the device can't update itself.
#include "core/engine/updates.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/util/sha256.hpp"
#include "doctest.h"
#include "engine/fake_host.hpp"

namespace up = marigold::engine::updates;
using marigold::engine::Host;
using marigold::engine::UpdateInstall;
using Phase = up::Updates::Phase;

namespace {

constexpr const char* kApkUrl = "https://github.com/a74375024/marigold-ledger/releases/download/v1.0.2/marigold-android.apk";
constexpr const char* kDmgUrl = "https://github.com/a74375024/marigold-ledger/releases/download/v1.0.2/marigold-mac.dmg";
constexpr marigold::time::Seconds kDay = 86400;
// 2026-11-06 12:00 UTC.
constexpr marigold::time::Seconds kNoon = 1793966400;

std::string asset(const std::string& name, const std::string& url, const std::string& digest, size_t size) {
  std::string s = "{\"name\":\"" + name + "\",\"browser_download_url\":\"" + url + "\",\"size\":" +
                  std::to_string(size) + ",\"state\":\"uploaded\",\"content_type\":\"application/octet-stream\"";
  if (!digest.empty()) s += ",\"digest\":\"" + digest + "\"";
  return s + "}";
}

std::string release(const std::string& tag, const std::vector<std::string>& assets, bool prerelease = false) {
  std::string s = "{\"tag_name\":\"" + tag + "\",\"name\":\"Marigold\",\"draft\":false,\"prerelease\":" +
                  (prerelease ? "true" : "false") + ",\"body\":\"\",\"assets\":[";
  for (size_t i = 0; i < assets.size(); ++i) s += (i ? "," : "") + assets[i];
  return s + "]}";
}

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

// A device that updates itself (Android), with a pretend installer.
struct Device {
  fake::Tv tv;
  marigold::time::Seconds now = kNoon;
  std::vector<std::string> installed;  // what the installer was handed (its bytes)
  UpdateInstall answer{true, false, "", ""};
  std::string apk;

  Device() {
    apk = "pretend apk ";
    for (int i = 0; i < 3000; ++i) apk += static_cast<char>('a' + i % 26);
  }
  std::string file() const { return tv.files.dir() + "/marigold-android.apk"; }
  Host host(bool capable = true) {
    Host h = tv.host();
    if (capable) {
      h.updateAsset = "marigold-android.apk";
      h.updateFile = file();
      h.installUpdate = [this](const std::string& path) {
        installed.push_back(readFile(path));
        return answer;
      };
    }
    return h;
  }
  std::unique_ptr<up::Updates> make(const std::string& version = "1.0.1", bool capable = true) {
    return up::Updates::make(host(capable), version, [this] { return now; });
  }
  void publish(const std::string& tag, const std::string& digest) {
    tv.pool.answer(up::kLatestUrl, release(tag, {asset("marigold-lg-webos.ipk", kApkUrl, "", 10),
                                                 asset("marigold-android.apk", kApkUrl, digest, apk.size()),
                                                 asset("marigold-mac.dmg", kDmgUrl, "sha256:" + std::string(64, '0'), 5)}));
    tv.pool.answer(kApkUrl, apk);
  }
  void publish(const std::string& tag = "v1.0.2") { publish(tag, "sha256:" + marigold::sha256::hex(apk)); }
  int checks() { return tv.pool.asked[up::kLatestUrl]; }
  // Past the first wait, quiet, and everything answered.
  void settle(up::Updates& u, bool quiet = true) {
    u.tick(tv.ms, quiet);
    tv.pool.drain();
  }
};

}  // namespace

TEST_CASE("updates: versions compare as numbers, plain releases only") {
  CHECK(up::newer("1.0.10", "1.0.9"));
  CHECK_FALSE(up::newer("1.0.9", "1.0.10"));
  CHECK(up::newer("v1.0.2", "1.0.1"));
  CHECK(up::newer("1.1.0", "1.0.99"));
  CHECK(up::newer("2.0.0", "1.12.30"));
  CHECK_FALSE(up::newer("1.0.1", "1.0.1"));
  CHECK_FALSE(up::newer("v1.0.1", "1.0.1"));
  CHECK_FALSE(up::newer("1.0.0", "1.0.1"));
  // Prereleases, other tags and garbage are never newer.
  for (const char* tag : {"1.0.2-beta", "v1.0.2-rc.1", "1.0.2+5", "1.0", "1.0.2.1", "latest", "", "v", "1..2",
                          "1.0.x", " 1.0.2", "1.0.2 ", "-1.0.2", "1.0.-2", "nightly-2026-11-06", "1234567.0.0"}) {
    CAPTURE(tag);
    CHECK_FALSE(up::parseVersion(tag));
    CHECK_FALSE(up::newer(tag, "1.0.1"));
  }
  CHECK_FALSE(up::newer("1.0.2", "garbage"));
  auto v = up::parseVersion("v1.0.10");
  REQUIRE(v);
  CHECK(v->major == 1);
  CHECK(v->minor == 0);
  CHECK(v->patch == 10);
}

TEST_CASE("updates: the release's file for this device") {
  std::string digest = "sha256:" + std::string(64, 'a');
  std::string body = release("v1.0.2", {asset("marigold-lg-webos.ipk", "https://x.test/w.ipk", digest, 7),
                                        asset("marigold-android.apk", kApkUrl, digest, 11000000),
                                        asset("marigold-apple-tv.ipa", "https://x.test/t.ipa", digest, 9),
                                        asset("marigold-mac.dmg", kDmgUrl, "sha256:" + std::string(64, 'B'), 50000000)});
  auto apk = up::parseRelease(body, "marigold-android.apk");
  REQUIRE(apk);
  CHECK(apk->version == "1.0.2");
  CHECK(apk->url == kApkUrl);
  CHECK(apk->sha256 == std::string(64, 'a'));
  CHECK(apk->size == 11000000);
  auto dmg = up::parseRelease(body, "marigold-mac.dmg");
  REQUIRE(dmg);
  CHECK(dmg->url == kDmgUrl);
  CHECK(dmg->sha256 == std::string(64, 'b'));  // lower case
  CHECK(dmg->size == 50000000);
  // No file under that name (nor one merely like it).
  CHECK_FALSE(up::parseRelease(body, "marigold-android"));
  CHECK_FALSE(up::parseRelease(body, "marigold.apk"));
  CHECK_FALSE(up::parseRelease(release("v1.0.2", {}), "marigold-android.apk"));
}

TEST_CASE("updates: prereleases, drafts, other tags and odd answers are ignored") {
  std::string digest = "sha256:" + std::string(64, 'a');
  std::vector<std::string> files = {asset("marigold-android.apk", kApkUrl, digest, 10)};
  CHECK_FALSE(up::parseRelease(release("v1.0.2", files, true), "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease(release("v1.0.2-beta", files), "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease(release("nightly", files), "marigold-android.apk"));
  std::string draft = release("v1.0.2", files);
  draft.replace(draft.find("\"draft\":false"), 13, "\"draft\":true");
  CHECK_FALSE(up::parseRelease(draft, "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease("", "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease("not json", "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease("[]", "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease("{\"message\":\"Not Found\"}", "marigold-android.apk"));
  // A plain http link (or none) is no file to fetch.
  CHECK_FALSE(up::parseRelease(release("v1.0.2", {asset("marigold-android.apk", "http://x.test/a.apk", digest, 10)}),
                               "marigold-android.apk"));
  CHECK_FALSE(up::parseRelease(release("v1.0.2", {asset("marigold-android.apk", "", digest, 10)}),
                               "marigold-android.apk"));
  // A digest that isn't SHA-256 (or none): no digest.
  for (std::string odd : {std::string(), "md5:" + std::string(32, 'a'), "sha256:" + std::string(63, 'a'),
                          "sha256:" + std::string(64, 'g'), std::string(64, 'a'), "sha512:" + std::string(64, 'a')}) {
    CAPTURE(odd);
    auto r = up::parseRelease(release("v1.0.2", {asset("marigold-android.apk", kApkUrl, odd, 10)}),
                              "marigold-android.apk");
    REQUIRE(r);
    CHECK(r->sha256.empty());
  }
}

TEST_CASE("updates: a device that can't update itself makes no request at all") {
  Device d;
  d.publish();
  // webOS and the Apple TV: nothing given.
  CHECK(d.make("1.0.1", false) == nullptr);
  // Half given is nothing either.
  Host noInstaller = d.host();
  noInstaller.installUpdate = nullptr;
  CHECK_FALSE(noInstaller.canSelfUpdate());
  CHECK(up::Updates::make(noInstaller, "1.0.1") == nullptr);
  Host noFile = d.host();
  noFile.updateFile.clear();
  CHECK(up::Updates::make(noFile, "1.0.1") == nullptr);
  // Even built by hand, it never asks, never offers, never saves.
  up::Updates u(d.host(false), "1.0.1", [&d] { return d.now; });
  for (int day = 0; day < 3; ++day) {
    for (uint32_t ms : {0u, 20000u, 60000u, 4000000u}) {
      d.tv.ms = ms + static_cast<uint32_t>(day) * 1000u;
      u.tick(d.tv.ms, true);
      d.tv.pool.drain();
    }
    d.now += kDay;
  }
  u.update();
  d.tv.pool.drain();
  CHECK(d.tv.pool.asked.empty());
  CHECK_FALSE(u.available());
  CHECK_FALSE(u.cardDue());
  CHECK(d.installed.empty());
  CHECK_FALSE(d.tv.files.read(up::kStateFile));
}

TEST_CASE("updates: once shortly after launch, when quiet, then once a day") {
  Device d;
  d.publish();
  auto u = d.make();
  REQUIRE(u);
  // Not in the first seconds, nor while something is pressed or loading.
  d.tv.ms = up::kFirstCheckMs - 1;
  d.settle(*u);
  CHECK(d.checks() == 0);
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u, false);
  CHECK(d.checks() == 0);
  d.settle(*u);
  CHECK(d.checks() == 1);
  CHECK(u->available());
  // The rest of the day: no more, however long it runs.
  for (uint32_t ms = up::kFirstCheckMs; ms < 12u * 3600u * 1000u; ms += 600000) {
    d.tv.ms = ms;
    d.settle(*u);
  }
  CHECK(d.checks() == 1);
  // Started again the same day: not again.
  auto again = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*again);
  CHECK(d.checks() == 1);
  CHECK(again->available());  // remembered
  // The next day: once more (looked at within a minute).
  d.now += kDay;
  d.tv.ms += 60000;
  d.settle(*again);
  d.settle(*again);
  CHECK(d.checks() == 2);
}

TEST_CASE("updates: a check with no answer is tried again an hour later") {
  Device d;  // nothing published: the network is down
  auto u = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  CHECK(d.checks() == 1);
  CHECK_FALSE(u->available());
  d.tv.ms += up::kRetryMs - 1;
  d.settle(*u);
  CHECK(d.checks() == 1);
  d.tv.ms += 1;
  d.publish();
  d.settle(*u);
  CHECK(d.checks() == 2);
  CHECK(u->available());
  // Turned away (rate limit): not again that day.
  Device busy;
  busy.tv.pool.answer(up::kLatestUrl, "{\"message\":\"API rate limit exceeded\"}", 403);
  auto v = busy.make();
  busy.tv.ms = up::kFirstCheckMs;
  busy.settle(*v);
  busy.tv.ms += 2 * up::kRetryMs;
  busy.settle(*v);
  CHECK(busy.checks() == 1);
}

TEST_CASE("updates: only a newer plain release with a digest is offered") {
  for (const char* tag : {"v1.0.1", "v1.0.0", "v1.0.2-beta", "nightly"}) {
    CAPTURE(tag);
    Device d;
    d.publish(tag);
    auto u = d.make();
    d.tv.ms = up::kFirstCheckMs;
    d.settle(*u);
    CHECK(d.checks() == 1);
    CHECK_FALSE(u->available());
    CHECK_FALSE(u->cardDue());
  }
  // 1.0.10 is after 1.0.9.
  Device ten;
  ten.publish("v1.0.10");
  auto u = ten.make("1.0.9");
  ten.tv.ms = up::kFirstCheckMs;
  ten.settle(*u);
  CHECK(u->available());
  // No digest: not offered.
  Device bare;
  bare.publish("v1.0.2", "");
  auto b = bare.make();
  bare.tv.ms = up::kFirstCheckMs;
  bare.settle(*b);
  CHECK_FALSE(b->available());
  // A release without this device's file: nothing.
  Device other;
  other.tv.pool.answer(up::kLatestUrl, release("v1.0.2", {asset("marigold-mac.dmg", kDmgUrl, "sha256:" + std::string(64, 'a'), 5)}));
  auto o = other.make();
  other.tv.ms = up::kFirstCheckMs;
  other.settle(*o);
  CHECK_FALSE(o->available());
}

TEST_CASE("updates: the card at most once a day; Not now waits for tomorrow") {
  Device d;
  d.publish();
  auto u = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  REQUIRE(u->available());
  CHECK(u->cardDue());
  u->cardShown();  // shown, then Not now (or left to hide)
  CHECK_FALSE(u->cardDue());
  CHECK(u->available());  // Settings' row stays
  // Started again the same day: still not.
  auto again = d.make();
  CHECK_FALSE(again->cardDue());
  CHECK(again->available());
  // The next day it may show again.
  d.now += kDay;
  CHECK(again->cardDue());
  again->cardShown();
  CHECK_FALSE(again->cardDue());
  // A newer release the same day: its own card.
  d.publish("v1.0.3");
  d.now += kDay;
  d.tv.ms += 60000;
  again->cardShown();
  d.settle(*again);
  d.settle(*again);
  CHECK(again->available());
  CHECK(again->cardDue());
}

TEST_CASE("updates: the running version caught up: nothing waits") {
  Device d;
  d.publish();
  auto u = d.make("1.0.1");
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  REQUIRE(u->available());
  // After the update, the same day: the saved 1.0.2 isn't newer than 1.0.2.
  auto after = d.make("1.0.2");
  CHECK_FALSE(after->available());
  CHECK_FALSE(after->cardDue());
}

TEST_CASE("updates: Update downloads, checks the digest and hands the file over") {
  Device d;
  d.publish();
  auto u = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  REQUIRE(u->available());
  u->update();
  CHECK(u->phase() == Phase::Updating);
  d.tv.pool.drain();
  REQUIRE(d.installed.size() == 1);
  CHECK(d.installed[0] == d.apk);
  CHECK(u->phase() == Phase::Idle);
  CHECK_FALSE(u->quitWanted());
  // The Mac: the app quits for its helper.
  Device mac;
  mac.publish();
  mac.answer.quit = true;
  auto m = mac.make();
  mac.tv.ms = up::kFirstCheckMs;
  mac.settle(*m);
  m->update();
  mac.tv.pool.drain();
  CHECK(m->quitWanted());
  // An earlier download is gone at the next start.
  CHECK(std::filesystem::exists(mac.file()));
  auto next = mac.make("1.0.2");
  CHECK_FALSE(std::filesystem::exists(mac.file()));
}

TEST_CASE("updates: a file that doesn't match its digest is refused") {
  Device d;
  d.publish("v1.0.2", "sha256:" + marigold::sha256::hex("something else"));
  auto u = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  REQUIRE(u->available());
  d.tv.ms = 100000;
  u->update();
  d.tv.pool.drain();
  CHECK(d.installed.empty());
  CHECK(u->phase() == Phase::Failed);
  CHECK(u->failText() == up::kFailedText);
  CHECK(u->failedUntil() == 100000 + up::kFailedMs);
  CHECK_FALSE(std::filesystem::exists(d.file()));
  CHECK_FALSE(u->cardDue());  // not while the line shows
  // The line goes after a few seconds; the update is still offered.
  d.tv.ms = 100000 + up::kFailedMs - 1;
  CHECK_FALSE(u->tick(d.tv.ms, true));
  CHECK(u->phase() == Phase::Failed);
  d.tv.ms += 1;
  CHECK(u->tick(d.tv.ms, true));
  CHECK(u->phase() == Phase::Idle);
  CHECK(u->available());
  // Chosen already today (Settings' row): no card about it until tomorrow.
  CHECK_FALSE(u->cardDue());
  d.now += kDay;
  CHECK(u->cardDue());
}

TEST_CASE("updates: refused without a digest, cut short, or when the installer says no") {
  // Checked with a digest; by the time Update is chosen the release has none.
  Device d;
  d.publish();
  auto u = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  REQUIRE(u->available());
  d.publish("v1.0.2", "");
  u->update();
  d.tv.pool.drain();
  CHECK(u->phase() == Phase::Failed);
  CHECK(d.installed.empty());
  CHECK(d.tv.pool.asked[kApkUrl] == 0);  // not even downloaded

  // The server sends less than the release says.
  Device cut;
  cut.publish();
  auto c = cut.make();
  cut.tv.ms = up::kFirstCheckMs;
  cut.settle(*c);
  cut.tv.pool.answer(kApkUrl, cut.apk.substr(0, 100));
  c->update();
  cut.tv.pool.drain();
  CHECK(c->phase() == Phase::Failed);
  CHECK(cut.installed.empty());

  // The download fails outright.
  Device gone;
  gone.publish();
  auto g = gone.make();
  gone.tv.ms = up::kFirstCheckMs;
  gone.settle(*g);
  gone.tv.pool.answer(kApkUrl, "", 404);
  g->update();
  gone.tv.pool.drain();
  CHECK(g->phase() == Phase::Failed);
  CHECK(gone.installed.empty());

  // The installer refuses (Android: signed with another key; the Mac: a
  // folder it may not change, with its own line).
  Device refused;
  refused.publish();
  refused.answer = {false, false, "signed with another key", ""};
  auto r = refused.make();
  refused.tv.ms = up::kFirstCheckMs;
  refused.settle(*r);
  r->update();
  refused.tv.pool.drain();
  CHECK(refused.installed.size() == 1);
  CHECK(r->phase() == Phase::Failed);
  CHECK(r->failText() == up::kFailedText);
  CHECK_FALSE(r->quitWanted());
  Device locked;
  locked.publish();
  locked.answer = {false, true, "folder not writable", "Couldn't update. Move Marigold to Applications first."};
  auto l = locked.make();
  locked.tv.ms = up::kFirstCheckMs;
  locked.settle(*l);
  l->update();
  locked.tv.pool.drain();
  CHECK(l->phase() == Phase::Failed);
  CHECK(l->failText() == "Couldn't update. Move Marigold to Applications first.");
  CHECK_FALSE(l->quitWanted());
}

TEST_CASE("updates: every request dropped: the update ends as failed, a late answer changes nothing") {
  Device d;
  d.publish();
  auto u = d.make();
  d.tv.ms = up::kFirstCheckMs;
  d.settle(*u);
  d.tv.pool.hold(kApkUrl);
  u->update();
  d.tv.pool.drain();
  CHECK(u->phase() == Phase::Updating);
  u->abandoned();
  CHECK(u->phase() == Phase::Failed);
  d.tv.pool.letGo(kApkUrl);
  d.tv.pool.drain();
  CHECK(d.installed.empty());
  CHECK(u->phase() == Phase::Failed);
  // A check dropped likewise is tried again later, not stuck.
  Device e;
  e.publish();
  auto v = e.make();
  e.tv.pool.hold(up::kLatestUrl);
  e.tv.ms = up::kFirstCheckMs;
  e.settle(*v);
  v->abandoned();
  e.tv.pool.letGo(up::kLatestUrl);
  e.tv.pool.drain();
  CHECK_FALSE(v->available());
  e.tv.ms += up::kRetryMs;
  e.settle(*v);
  CHECK(e.checks() == 2);
  CHECK(v->available());
}
