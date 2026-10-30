// Loading the channel list without a wait screen: the guide header says
// when the list was updated (or couldn't be), a list an older version saved
// opens the guide until it's sorted again, and the sort reports how far it
// has got.
#include <doctest.h>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "catalog/helpers.hpp"
#include "core/catalog/cache.hpp"
#include "core/catalog/catalog.hpp"
#include "core/engine/store.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/gunzip.hpp"
#include "core/util/json.hpp"
#include "engine/loaded_store.hpp"
#include "sources/mock_provider.hpp"

using marigold::engine::LoadProgress;
using marigold::engine::Store;
using marigold::engine::StoreStatus;
using fake::addPlaylist;
using fake::kPlaylist;
using fake::settle;

namespace {

// The queued work done, then one frame.
void frame(Store& store, fake::Tv& tv, uint32_t ms = 100) {
  tv.pool.drain();
  tv.ms += ms;
  store.tick(tv.ms);
}

std::vector<std::string> savedLists(const fake::Tv& tv) {
  std::vector<std::string> names;
  for (const auto& f : std::filesystem::directory_iterator(tv.files.dir())) {
    std::string name = f.path().filename().string();
    if (name.rfind("catalog-", 0) == 0) names.push_back(name);
  }
  return names;
}

// One queued step at a time with a frame after each, as the TV draws: true
// if any frame said "ready" with no channels (the guide's "No channels").
bool saidNoChannels(Store& store, fake::Tv& tv) {
  bool said = false;
  for (int i = 0; i < 2000; ++i) {
    size_t steps = tv.pool.drain(1);
    tv.ms += 16;
    store.tick(tv.ms);
    auto shown = store.channels();
    if (store.status().phase == StoreStatus::Phase::Ready && (!shown || shown->all.empty())) said = true;
    if (steps == 0 && i > 400) break;
  }
  return said;
}

// A saved list as text (the full one is saved gzipped).
std::string savedText(const fake::Tv& tv, const std::string& name) {
  std::string out;
  marigold::gz::Gunzip unzip([&out](std::string_view c) {
    out.append(c);
    return true;
  });
  std::string raw = tv.files.read(name).value_or("");
  unzip.feed(raw);
  unzip.finish();
  return out;
}

}  // namespace

TEST_CASE("loading: the guide never says 'No channels' between the sort and the rows") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  {
    Store store(tv.host());
    store.addAccount(fake::m3uForm(), [](std::string) {});
    CHECK_FALSE(saidNoChannels(store, tv));
    CHECK(store.status().phase == StoreStatus::Phase::Ready);
    REQUIRE(store.channels());
    CHECK(store.channels()->all.size() > 50);
  }
  // The next launch, from the list saved on this TV.
  Store again(tv.host());
  CHECK_FALSE(saidNoChannels(again, tv));
  CHECK(again.status().phase == StoreStatus::Phase::Ready);
  REQUIRE(again.channels());
  CHECK(again.channels()->all.size() > 50);
}

TEST_CASE("loading: the first-screen file holds the rows; an empty one (older versions) waits for the full") {
  using marigold::catalog::cache::fromJson;
  using marigold::catalog::cache::toJson;
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  {
    Store store(tv.host());
    REQUIRE(addPlaylist(store, tv).empty());
  }
  std::string firstName;
  for (const std::string& name : savedLists(tv))
    if (name.find("-first") != std::string::npos) firstName = name;
  REQUIRE_FALSE(firstName.empty());
  std::string text = tv.files.read(firstName).value_or("");
  auto doc = marigold::json::parse(text);
  REQUIRE(doc);
  auto first = fromJson(text, (*doc)["signature"].asString());
  REQUIRE(first);
  CHECK(first->catalog.entries.size() > 20);
  // As Build 95 and older wrote it: channels, no entries.
  first->catalog.entries.clear();
  tv.files.write(firstName, toJson(*first), false);
  Store again(tv.host());
  CHECK_FALSE(saidNoChannels(again, tv));
  CHECK(again.status().phase == StoreStatus::Phase::Ready);
  REQUIRE(again.channels());
  CHECK(again.channels()->all.size() > 50);
}

TEST_CASE("loading: a changed list sorts in the background, then the header says it's updated") {
  fake::Tv tv;
  mock::Provider before(200), after(260);
  tv.pool.answer(kPlaylist, before.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  CHECK(store.status().mark == StoreStatus::Mark::None);
  CHECK(store.loadProgress().stage == LoadProgress::Stage::Done);
  CHECK(store.status().received == 0);

  tv.pool.answer(kPlaylist, after.m3u());
  store.refreshAll();
  tv.ms += 100;
  store.tick(tv.ms);
  CHECK(store.status().mark == StoreStatus::Mark::Busy);
  CHECK(store.status().line == "Updating your channel list\xe2\x80\xa6");
  frame(store, tv);
  CHECK(store.status().mark == StoreStatus::Mark::Done);
  CHECK(store.status().line == "Channel list updated");
  // A few seconds, then it goes.
  frame(store, tv, 2800);
  CHECK(store.status().mark == StoreStatus::Mark::Done);
  frame(store, tv, 300);
  CHECK(store.status().mark == StoreStatus::Mark::None);
  CHECK(store.status().line.empty());
}

TEST_CASE("loading: an unchanged list says nothing when it's done") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  store.refreshAll();
  frame(store, tv);
  frame(store, tv);
  CHECK(store.status().mark == StoreStatus::Mark::None);
}

TEST_CASE("loading: a list that couldn't be updated stays, and the header says so for a few seconds") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  auto shown = store.channels();
  tv.pool.answer(kPlaylist, "down", 503);
  store.refreshAll();
  frame(store, tv);
  frame(store, tv);
  CHECK(store.status().mark == StoreStatus::Mark::Failed);
  CHECK(store.status().line.find("Couldn't update the channel list") == 0);
  CHECK(store.channels().get() == shown.get());
  frame(store, tv, 4100);
  CHECK(store.status().mark == StoreStatus::Mark::None);
}

TEST_CASE("loading: a list an older version saved opens the guide, then is sorted again") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  {
    Store store(tv.host());
    REQUIRE(addPlaylist(store, tv).empty());
  }
  // As the previous version left them.
  std::string now = "\"version\":" + std::to_string(marigold::catalog::cache::kVersion);
  std::string older = "\"version\":" + std::to_string(marigold::catalog::cache::kVersion - 1);
  std::vector<std::string> lists = savedLists(tv);
  REQUIRE(lists.size() == 2);
  for (const std::string& name : lists) {
    std::string text = savedText(tv, name);
    REQUIRE(text.find(now) != std::string::npos);
    text.replace(text.find(now), now.size(), older);
    tv.files.write(name, text, false);
  }
  // The provider down at first: the guide has only the old list to show.
  tv.pool.answer(kPlaylist, "down", 503);
  Store again(tv.host());
  settle(again, tv);
  CHECK(again.status().phase == StoreStatus::Phase::Ready);
  REQUIRE(again.channels());
  CHECK(again.channels()->all.size() > 50);
  for (const std::string& name : savedLists(tv)) CHECK(savedText(tv, name).find(older) != std::string::npos);
  // Then the same download as before, sorted again by this version (not
  // "unchanged") and saved as its own.
  tv.pool.answer(kPlaylist, provider.m3u());
  again.refreshAll();
  settle(again, tv);
  for (const std::string& name : savedLists(tv)) CHECK(savedText(tv, name).find(now) != std::string::npos);
}

TEST_CASE("loading: the sort says how far it has got") {
  using namespace marigold;
  std::vector<Channel> list;
  for (int i = 0; i < 600; ++i) list.push_back(testdata::channel(std::to_string(i), "US: Channel " + std::to_string(i)));
  catalog::BuildProgress progress;
  CHECK(progress.pass.load() == catalog::BuildProgress::notStarted);
  catalog::build(list, testdata::utc(2026, 10, 29, 12, 0), {}, nullptr, &progress);
  CHECK(progress.pass.load() == catalog::BuildProgress::finished);
}

TEST_CASE("loading: a provider added later merges into the list on screen") {
  fake::Tv tv;
  mock::Provider first(200), second(120);
  tv.pool.answer(kPlaylist, first.m3u());
  const char* kSecond = "http://mock2.example.invalid:8080/playlist.m3u";
  tv.pool.answer(kSecond, second.m3u());
  Store store(tv.host());
  REQUIRE(addPlaylist(store, tv).empty());
  auto before = store.channels();
  REQUIRE(before);
  size_t rows = before->all.size(), copies = before->rawChannels;

  marigold::engine::AccountForm f = fake::m3uForm();
  f.name = "Second playlist";
  f.server = kSecond;
  std::string problem = "(not answered)";
  store.addAccount(f, [&](std::string p) { problem = std::move(p); });
  // The account is checked, then the lists load: the old one stays meanwhile.
  for (int i = 0; i < 3 && store.status().mark != StoreStatus::Mark::Busy; ++i) {
    tv.ms += 100;
    store.tick(tv.ms);
    CHECK(store.channels()->all.size() == rows);
    tv.pool.drain();
  }
  CHECK(problem.empty());
  // (One drain may already have sorted it; either way the list was never empty.)
  for (int i = 0; i < 20 && store.status().line != "Channels added"; ++i) {
    CHECK(store.status().phase == StoreStatus::Phase::Ready);
    CHECK_FALSE(store.channels()->all.empty());
    if (store.status().mark == StoreStatus::Mark::Busy)
      CHECK(store.status().line == "Adding channels from Second playlist\xe2\x80\xa6");
    frame(store, tv);
  }
  CHECK(store.status().line == "Channels added");
  CHECK(store.status().mark == StoreStatus::Mark::Done);
  // The second provider's copies joined the rows (the same channels here).
  CHECK(store.channels()->rawChannels > copies);
  CHECK(store.channels()->all.size() >= rows);
}

TEST_CASE("loading: a refresh where only event names changed keeps the rows and doesn't rewrite the saved list") {
  using marigold::engine::AccountForm;
  fake::Tv tv;
  mock::Provider provider(400);
  // An Xtream account (its channel ids don't change when a slot is renamed).
  marigold::sources::Account a;
  a.kind = marigold::sources::AccountKind::Xtream;
  a.serverUrl = std::string(mock::kHost);
  a.userName = std::string(mock::kUser);
  a.password = std::string(mock::kPass);
  auto client = marigold::sources::xtream::Client::make(a);
  REQUIRE(client);
  const std::string infoURL = client.value().accountInfoRequest().url;
  const std::string categoriesURL = client.value().categoriesRequest().url;
  const std::string streamsURL = client.value().streamsRequest().url;
  tv.pool.answer(infoURL, R"({"user_info":{"auth":1,"status":"Active"},"server_info":{}})");
  tv.pool.answer(categoriesURL, provider.categoriesJson());
  std::string before = provider.liveStreamsJson();
  tv.pool.answer(streamsURL, before);
  Store store(tv.host());
  AccountForm form;
  form.kind = AccountForm::Kind::Xtream;
  form.name = "Mock";
  form.server = a.serverUrl;
  form.user = a.userName;
  form.password = a.password;
  std::string problem = "(not answered)";
  store.addAccount(form, [&](std::string p) { problem = std::move(p); });
  settle(store, tv);
  REQUIRE(problem.empty());
  auto shown = store.channels();
  REQUIRE(shown);
  size_t rows = shown->all.size();
  REQUIRE(rows > 20);
  std::map<std::string, std::string> saved;
  for (const std::string& name : savedLists(tv)) saved[name] = tv.files.read(name).value_or("");
  REQUIRE(saved.size() == 2);
  // The same list with the games in the event slots renamed.
  std::string after = before;
  size_t renamed = 0;
  for (size_t at = after.find("7:30 PM ET"); at != std::string::npos; at = after.find("7:30 PM ET", at + 1)) {
    after.replace(at, 10, "8:00 PM ET");
    ++renamed;
  }
  REQUIRE(renamed > 0);
  tv.pool.answer(streamsURL, after);
  store.refreshAll();
  settle(store, tv);
  REQUIRE(store.channels());
  CHECK(store.channels()->all.size() == rows);
  CHECK(store.status().phase == StoreStatus::Phase::Ready);
  // No "Channel list updated" for renamed games, and the saved list waits.
  CHECK(store.status().mark != StoreStatus::Mark::Done);
  for (const auto& [name, text] : saved) CHECK(tv.files.read(name).value_or("") == text);
}

TEST_CASE("loading: the full saved list is gzipped, the first-screen one plain") {
  fake::Tv tv;
  mock::Provider provider(200);
  tv.pool.answer(kPlaylist, provider.m3u());
  {
    Store store(tv.host());
    REQUIRE(addPlaylist(store, tv).empty());
  }
  for (const std::string& name : savedLists(tv)) {
    std::string raw = tv.files.read(name).value_or("");
    REQUIRE(raw.size() > 2);
    bool gz = static_cast<unsigned char>(raw[0]) == 0x1f && static_cast<unsigned char>(raw[1]) == 0x8b;
    CHECK(gz == (name.find("-full") != std::string::npos));
    CHECK(savedText(tv, name).find("\"catalog\"") != std::string::npos);
  }
  // And it reads back: the next launch opens on it.
  tv.pool.answer(kPlaylist, "down", 503);
  Store again(tv.host());
  settle(again, tv);
  REQUIRE(again.channels());
  CHECK(again.channels()->all.size() > 50);
}

TEST_CASE("loading: the player's settings come back next launch; one old subtitles switch covers both languages") {
  fake::Tv tv;
  {
    Store store(tv.host());
    marigold::engine::Settings s = store.settings();
    s.subtitles = true;
    s.subtitlesEs = false;
    s.subtitleLanguageEs = "English";
    s.subtitleSize = 3;  // extra large
    s.upGoesHigher = true;
    s.systemCaptions = 1;
    store.setSettings(s);
  }
  {
    Store again(tv.host());
    const marigold::engine::Settings& s = again.settings();
    CHECK(s.subtitles);
    CHECK_FALSE(s.subtitlesEs);
    CHECK(s.subtitleLanguageEs == "English");
    CHECK(s.subtitleSize == 3);
    CHECK(s.upGoesHigher);
    CHECK(s.systemCaptions == 1);
  }
  // A settings file from before the two choices: its one switch is both.
  tv.files.write("settings.json", R"({"subtitles": true, "subtitleSize": 2})", false);
  Store old(tv.host());
  CHECK(old.settings().subtitles);
  CHECK(old.settings().subtitlesEs);
  CHECK_FALSE(old.settings().upGoesHigher);
  CHECK(old.settings().systemCaptions == -1);
}
