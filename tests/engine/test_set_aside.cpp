// Build 181, "This feed is wrong…": a feed set aside for its channel. A
// made-up playlist whose Discovery Channel has three copies, one of them
// carrying NBC Sports Bay Area's guide ("49ers Now"), and a History whose
// only listings come from one copy. Names of real networks; made-up
// programmes; every link on example.invalid.
#include <doctest.h>

#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/catalog/catalog.hpp"
#include "core/catalog/reference.hpp"
#include "core/engine/session.hpp"
#include "core/engine/set_aside.hpp"
#include "core/engine/store.hpp"
#include "core/util/time.hpp"
#include "engine/loaded_store.hpp"

using namespace marigold;
using namespace marigold::engine;

namespace {

constexpr const char* kList = "http://mock.example.invalid:8080/aside.m3u";
constexpr const char* kGuide = "http://mock.example.invalid:8080/aside.xml";

std::string stamp(time::Seconds t) {
  time::Civil c = time::civilFromSeconds(t);
  char b[32];
  std::snprintf(b, sizeof b, "%04d%02d%02d%02d%02d00 +0000", c.year, c.month, c.day, c.hour, c.minute);
  return b;
}

void answer(fake::Tv& tv) {
  tv.pool.answer(kList,
                 "#EXTM3U\n"
                 "#EXTINF:-1 tvg-id=\"DiscoveryChannel.us\" group-title=\"US Entertainment\",Discovery Channel\n"
                 "http://mock.example.invalid:8080/d1.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"DiscoveryChannel.us\" group-title=\"US Entertainment\",US: Discovery Channel HD\n"
                 "http://mock.example.invalid:8080/d2.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"NBCSportsBayArea.us\" group-title=\"|US| NFL TEAMS\",US: Discovery Channel FHD\n"
                 "http://mock.example.invalid:8080/d3.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"History.us\" group-title=\"US Entertainment\",History\n"
                 "http://mock.example.invalid:8080/h1.m3u8\n"
                 "#EXTINF:-1 group-title=\"US Entertainment\",US: History HD\n"
                 "http://mock.example.invalid:8080/h2.m3u8\n"
                 "#EXTINF:-1 tvg-id=\"NBCSportsBayArea.us\" group-title=\"US Sports\",NBC Sports Bay Area\n"
                 "http://mock.example.invalid:8080/n1.m3u8\n");
  time::Seconds hour = time::now() / 3600 * 3600;
  std::string xml =
      "<?xml version=\"1.0\"?>\n<tv>\n"
      "<channel id=\"DiscoveryChannel.us\"><display-name>Discovery Channel</display-name></channel>\n"
      "<channel id=\"History.us\"><display-name>History</display-name></channel>\n"
      "<channel id=\"NBCSportsBayArea.us\"><display-name>NBC Sports Bay Area</display-name></channel>\n";
  for (int i = -3; i < 12; ++i) {
    std::string when = "start=\"" + stamp(hour + i * 3600) + "\" stop=\"" + stamp(hour + (i + 1) * 3600) + "\"";
    xml += "<programme " + when + " channel=\"DiscoveryChannel.us\"><title>Gold Rush " + std::to_string(i + 4) +
           "</title></programme>\n";
    xml += "<programme " + when + " channel=\"History.us\"><title>Ancient Aliens " + std::to_string(i + 4) +
           "</title></programme>\n";
    xml += "<programme " + when + " channel=\"NBCSportsBayArea.us\"><title>" +
           std::string(i == 0 ? "49ers Now" : "Dubs Talk " + std::to_string(i + 4)) + "</title></programme>\n";
  }
  xml += "</tv>\n";
  tv.pool.answer(kGuide, xml);
}

AccountForm form() {
  AccountForm f = fake::m3uForm();
  f.server = kList;
  f.guideUrl = kGuide;
  return f;
}

std::string entryNamed(const Store& store, const std::string& name) {
  for (const GuideChannel& g : store.channels()->all)
    if (g.name == name) return g.id;
  return "";
}

std::vector<std::string> copiesOf(const Store& store, const std::string& entry) {
  std::vector<std::string> out;
  for (const Feed& f : store.feeds(entry)) out.push_back(f.channelId);
  return out;
}

bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

struct Loaded {
  fake::Tv tv;
  std::unique_ptr<Store> store;
  std::string discovery, history, bayArea;
  std::string d1, d2, d3, h1, h2;

  Loaded() {
    answer(tv);
    open();
    store->addAccount(form(), [](std::string) {});
    fake::settle(*store, tv);
    find();
  }
  void open() { store = std::make_unique<Store>(tv.host()); }
  // A restart: the store again from what is on file.
  void restart() {
    store.reset();
    open();
    fake::settle(*store, tv);
    find();
  }
  void find() {
    discovery = entryNamed(*store, "Discovery Channel");
    history = entryNamed(*store, "History");
    bayArea = entryNamed(*store, "NBC Sports Bay Area");
    store->addToLineup({discovery, history, bayArea});
    store->wantListings({discovery, history, bayArea});
    fake::settle(*store, tv);
    auto id = [&](const char* item) {
      for (const std::string& e : {discovery, history, bayArea})
        for (const std::string& c : copiesOfEntry(e))
          if (store->copyFeed(c) && store->copyFeed(c)->url.find(item) != std::string::npos) return c;
      return std::string();
    };
    d1 = id("/d1.");
    d2 = id("/d2.");
    d3 = id("/d3.");
    h1 = id("/h1.");
    h2 = id("/h2.");
  }
  // Every copy of an entry, set aside or not (Test all sources' rows).
  std::vector<std::string> copiesOfEntry(const std::string& entry) const {
    std::vector<std::string> out;
    for (const scores::LineupRow& row : store->lineupRows())
      if (row.entryId == entry)
        for (const scores::LineupCopy& c : row.copies) out.push_back(c.feedId);
    return out;
  }
  bool ready() const {
    return !discovery.empty() && !history.empty() && !bayArea.empty() && !d1.empty() && !d2.empty() &&
           !d3.empty() && !h1.empty() && !h2.empty();
  }
};

setaside::Record record(const std::string& entry, const std::string& copy, setaside::Reason why, int feed,
                        const std::string& named = "") {
  setaside::Record r;
  r.entryId = entry;
  r.copyId = copy;
  r.reason = why;
  r.feed = feed;
  r.named = named;
  return r;
}

std::vector<std::string> pastTitles(const Store& store, const std::string& entry) {
  std::vector<std::string> out;
  for (const Listing& l : store.pastListing(entry, time::now() - 3 * 3600)) out.push_back(l.title);
  return out;
}

bool anyStarts(const std::vector<std::string>& titles, const std::string& prefix) {
  for (const std::string& t : titles)
    if (t.rfind(prefix, 0) == 0) return true;
  return false;
}

}  // namespace

// ------------------------------------------------------------------ pure

TEST_CASE("set aside: reasons, rows and saved records") {
  using setaside::Reason;
  REQUIRE(setaside::reasons().size() == 7);
  CHECK(setaside::title(Reason::OtherChannel) == "It's a different channel");
  CHECK(setaside::detail(Reason::ReplayOther) == "Live is right; Rewind plays something else");
  CHECK(setaside::title(Reason::Listings) == "Its listings don't match what's on");
  CHECK(setaside::detail(Reason::Other).empty());
  CHECK(setaside::playable(Reason::Listings));
  for (Reason r : setaside::reasons()) {
    CHECK(setaside::fromKey(setaside::key(r)) == r);
    if (r != Reason::Listings) CHECK_FALSE(setaside::playable(r));
  }
  CHECK_FALSE(setaside::fromKey("nonsense"));

  setaside::Record r = record("e|r:kntv", "xt:1|31", Reason::OtherChannel, 3, "e|r:nbcsba");
  r.namedName = "NBC Sports Bay Area";
  r.entryName = "KNTV";
  r.at = 1795000000;
  r.server = "xt:1";
  r.item = "31";
  r.name = "US: NBC BAY AREA (KNTV)";
  r.group = "|US| \xe2\x9c\xaa NFL TEAMS";
  CHECK(setaside::rowLine(r, "Dream", "today") == "It's NBC Sports Bay Area \xc2\xb7 Dream \xc2\xb7 today");
  CHECK(setaside::feedName(r.feed) == "Feed 3");
  std::optional<setaside::Record> back = setaside::fromJson(json::parse(json::write(setaside::toJson(r))).value());
  REQUIRE(back);
  CHECK(back->entryId == r.entryId);
  CHECK(back->copyId == r.copyId);
  CHECK(back->reason == r.reason);
  CHECK(back->named == r.named);
  CHECK(back->namedName == r.namedName);
  CHECK(back->feed == 3);
  CHECK(back->at == r.at);
  CHECK(back->name == r.name);
  CHECK(back->group == r.group);
  setaside::Record city = record("e|r:ktvu", "xt:2|5", Reason::WrongCity, 5);
  CHECK(setaside::rowLine(city, "Trex", "Nov 16") == "Wrong city or feed \xc2\xb7 Trex \xc2\xb7 Nov 16");

  // Feed 3 set aside: the others keep their numbers, no second Feed 3.
  CHECK(setaside::feedNumbers(3, {3}) == std::vector<int>{1, 2, 4});
  CHECK(setaside::feedNumbers(2, {1, 2}) == std::vector<int>{3, 4});
  CHECK(setaside::feedNumbers(2, {}) == std::vector<int>{1, 2});
}

TEST_CASE("set aside 1.0.25: a replay's reasons, its scope saved; records from before load as whole or listings") {
  using setaside::Reason;
  using setaside::Scope;
  REQUIRE(setaside::replayReasons().size() == 5);
  std::vector<std::string> titles;
  for (Reason r : setaside::replayReasons()) {
    titles.push_back(setaside::title(r));
    CHECK(setaside::fromKey(setaside::key(r)) == r);
  }
  CHECK(titles == std::vector<std::string>{"It's a different channel", "It's at the wrong time",
                                           "It doesn't play well", "Its programme names are wrong", "Something else"});
  CHECK(setaside::replayDetail(Reason::OtherChannel) == "The replay shows another channel's programmes");
  CHECK(setaside::replayDetail(Reason::WrongTime) == "Off from the time picked, e.g. an hour early");
  CHECK(setaside::replayDetail(Reason::PlaysBadly) == "Stalls, freezes or won't open");
  CHECK(setaside::replayDetail(Reason::ReplayNames) == "The Rewind bar names other shows");
  CHECK(setaside::replayDetail(Reason::Other).empty());
  CHECK(setaside::scopeOf(Reason::ReplayNames, true) == Scope::ReplayListings);
  CHECK(setaside::scopeOf(Reason::WrongTime, true) == Scope::Replay);
  CHECK(setaside::scopeOf(Reason::Listings, false) == Scope::Listings);
  CHECK(setaside::scopeOf(Reason::WrongCity, false) == Scope::Whole);
  for (Scope s : {Scope::Whole, Scope::Listings, Scope::Replay, Scope::ReplayListings})
    CHECK(setaside::scopeFromKey(setaside::scopeKey(s)) == s);
  CHECK(setaside::scopeKey(Scope::ReplayListings) == "replay-listings");

  setaside::Record r = record("e|r:kntv", "xt:1|31", Reason::WrongTime, 2);
  r.scope = Scope::Replay;
  CHECK(setaside::asideName(r) == "Feed 2 \xc2\xb7 replay");
  CHECK(setaside::asideName(r, 4) == "Feed 4 \xc2\xb7 replay");
  json::Value saved = setaside::toJson(r);
  CHECK(saved["scope"].asString() == "replay");
  std::optional<setaside::Record> back = setaside::fromJson(json::parse(json::write(saved)).value());
  REQUIRE(back);
  CHECK(back->scope == Scope::Replay);
  CHECK(back->reason == Reason::WrongTime);
  // Saved by Build 181 (no scope): as its reason says.
  auto old = [](const char* reason) {
    json::Value v = json::Value::object();
    v.set("entry", "e|r:kntv");
    v.set("copy", "xt:1|31");
    v.set("reason", reason);
    v.set("feed", 3);
    return setaside::fromJson(v);
  };
  REQUIRE(old("listings"));
  CHECK(old("listings")->scope == Scope::Listings);
  REQUIRE(old("broken"));
  CHECK(old("broken")->scope == Scope::Whole);
  CHECK(setaside::asideName(*old("broken")) == "Feed 3");

  // The log line: "replay: " first; what isn't known is left out.
  r.name = "US: NBC BAY AREA (KNTV)";
  r.group = "|US| \xe2\x9c\xaa NFL TEAMS";
  CHECK(setaside::replayLogDetail(r, "20:42", "archive clock UTC (server says UTC; not measured)",
                                  "Jazz at Clippers", "49ers Now") ==
        "replay: it's at the wrong time \xc2\xb7 its name \"US: NBC BAY AREA (KNTV)\" in \"|US| \xe2\x9c\xaa NFL TEAMS\" "
        "\xc2\xb7 at 20:42 \xc2\xb7 archive clock UTC (server says UTC; not measured) \xc2\xb7 guide then: "
        "\"Jazz at Clippers\" \xc2\xb7 its own list then: \"49ers Now\"");
  r.group.clear();
  CHECK(setaside::replayLogDetail(r, "", "", "", "") == "replay: it's at the wrong time \xc2\xb7 its name \"US: NBC BAY AREA (KNTV)\"");
  CHECK(setaside::usedAgainDetail(r, "today") == "replay: it's at the wrong time, set aside today");
}

TEST_CASE("set aside: the log line names why, the provider's name and group, and what's on") {
  setaside::Record r = record("e|r:kntv", "xt:1|31", setaside::Reason::OtherChannel, 3);
  r.namedName = "NBC Sports Bay Area";
  r.name = "US: NBC BAY AREA (KNTV)";
  r.group = "|US| \xe2\x9c\xaa NFL TEAMS";
  CHECK(setaside::logDetail(r, "49ers Now", "Jazz at Clippers") ==
        "it's a different channel: NBC Sports Bay Area \xc2\xb7 its name \"US: NBC BAY AREA (KNTV)\" in "
        "\"|US| \xe2\x9c\xaa NFL TEAMS\" \xc2\xb7 on now: \"49ers Now\" \xc2\xb7 guide: \"Jazz at Clippers\"");
  // What isn't known is left out; the reason in plain words.
  r.reason = setaside::Reason::DoesntWork;
  r.group.clear();
  CHECK(setaside::logDetail(r, "", "") == "it doesn't work \xc2\xb7 its name \"US: NBC BAY AREA (KNTV)\"");
  CHECK(setaside::usedAgainDetail(r, "today") == "it doesn't work, set aside today");
}

TEST_CASE("set aside: a renumbered copy is found again by its account, name and group") {
  std::vector<Channel> list(2);
  list[0].id = "xt:1|900";
  list[0].serverId = "xt:1";
  list[0].name = "US: NBC BAY AREA (KNTV)";
  list[0].group = std::string("|US| NFL TEAMS");
  list[1].id = "xt:2|31";
  list[1].serverId = "xt:2";
  list[1].name = "US: NBC BAY AREA (KNTV)";
  list[1].group = std::string("|US| NFL TEAMS");
  auto find = [&](const std::string& id) {
    for (const Channel& c : list)
      if (c.id == id) return true;
    return false;
  };
  auto each = [&](const auto& f) {
    for (const Channel& c : list) f(c);
  };
  setaside::Record r = record("e|r:kntv", "xt:1|31", setaside::Reason::WrongCity, 3);
  r.server = "xt:1";
  r.name = "US: NBC BAY AREA (KNTV)";
  r.group = "|US| NFL TEAMS";
  // Gone under its old number: the same account's copy by name and group,
  // never another account's.
  CHECK(setaside::rematch(r, find, each) == "xt:1|900");
  r.group = "|US| LOCALS";
  CHECK(setaside::rematch(r, find, each).empty());
  r.copyId = "xt:2|31";
  CHECK(setaside::rematch(r, find, each) == "xt:2|31");
}

TEST_CASE("set aside: a copy moved to the channel it is") {
  catalog::CatalogResult r;
  r.entries.resize(2);
  r.entries[0].id = "e|kntv";
  r.entries[0].members = {"a|1", "a|2", "a|3"};
  r.entries[1].id = "e|nbcsba";
  r.entries[1].members = {"a|9"};
  for (const auto& e : r.entries)
    for (const std::string& m : e.members) r.entryOf[m] = e.id;
  std::map<std::string, std::string> moves{{"a|3", "e|nbcsba"}, {"a|7", "e|nbcsba"}, {"a|2", "e|gone"}};
  CHECK(catalog::copiesToMove(r, moves));
  CHECK(catalog::moveCopies(r, moves));
  CHECK(r.entries[0].members == std::vector<std::string>{"a|1", "a|2"});
  CHECK(r.entries[1].members == std::vector<std::string>{"a|9", "a|3"});
  CHECK(r.entryOf["a|3"] == "e|nbcsba");
  CHECK_FALSE(r.entryOf.count("a|7"));  // not in the list
  CHECK(r.entryOf["a|2"] == "e|kntv");  // no such channel
  CHECK_FALSE(catalog::copiesToMove(r, moves));
  CHECK_FALSE(catalog::moveCopies(r, moves));
  // And back.
  CHECK(catalog::moveCopies(r, {{"a|3", "e|kntv"}}));
  CHECK(r.entries[0].members == std::vector<std::string>{"a|1", "a|2", "a|3"});
  CHECK(r.entries[1].members == std::vector<std::string>{"a|9"});
}

// ------------------------------------------------------------------ store

TEST_CASE("set aside: not played on its channel, per channel only, kept across a restart, used again") {
  if (catalog::reference::builtIn().empty()) return;
  Loaded l;
  REQUIRE(l.ready());
  REQUIRE(copiesOf(*l.store, l.discovery).size() == 3);
  CHECK(has(copiesOf(*l.store, l.discovery), l.d3));

  setaside::Record kept = l.store->setAside(record(l.discovery, l.d3, setaside::Reason::WrongCity, 3));
  CHECK(kept.entryName == "Discovery Channel");
  CHECK(kept.name == "US: Discovery Channel FHD");
  CHECK(kept.group == "|US| NFL TEAMS");
  CHECK(kept.at > 0);
  fake::settle(*l.store, l.tv);
  std::vector<std::string> feeds = copiesOf(*l.store, l.discovery);
  CHECK(feeds.size() == 2);
  CHECK_FALSE(has(feeds, l.d3));
  CHECK(l.store->notPlayedOn(l.discovery, l.d3));
  CHECK(l.store->listingsSetAside(l.discovery, l.d3));
  // For that channel only: the copy itself still plays where it's asked
  // for, and is set aside for no other channel.
  CHECK(l.store->copyFeed(l.d3));
  CHECK_FALSE(l.store->notPlayedOn(l.history, l.d3));
  CHECK_FALSE(l.store->notPlayedOn(l.discovery, l.d1));
  REQUIRE(l.store->setAsides().size() == 1);
  CHECK(l.store->setAsidesFor(l.history).empty());
  std::string lists = l.tv.files.read("lists.json").value_or("");
  CHECK(lists.find("\"setAside\"") != std::string::npos);
  CHECK(lists.find("mock.example.invalid") == std::string::npos);  // never a link

  // A restart: still set aside.
  l.restart();
  REQUIRE(l.ready());
  REQUIRE(l.store->setAsides().size() == 1);
  CHECK(l.store->setAsides()[0].reason == setaside::Reason::WrongCity);
  CHECK(l.store->setAsides()[0].feed == 3);
  CHECK_FALSE(has(copiesOf(*l.store, l.discovery), l.d3));

  // Used again: one of its feeds once more, and on file.
  CHECK(l.store->useAgain(l.discovery, l.d3));
  CHECK_FALSE(l.store->useAgain(l.discovery, l.d3));
  CHECK(has(copiesOf(*l.store, l.discovery), l.d3));
  CHECK(l.store->setAsides().empty());
  fake::settle(*l.store, l.tv);
  l.restart();
  CHECK(l.store->setAsides().empty());
  CHECK(has(copiesOf(*l.store, l.discovery), l.d3));
}

TEST_CASE("set aside: its listings only: still played, its listings never used for the channel") {
  if (catalog::reference::builtIn().empty()) return;
  Loaded l;
  REQUIRE(l.ready());
  // History's listings are its first copy's alone.
  CHECK(anyStarts(pastTitles(*l.store, l.history), "Ancient Aliens"));
  CHECK(l.store->guideOnNow(l.history, time::now()).rfind("Ancient Aliens", 0) == 0);

  l.store->setAside(record(l.history, l.h1, setaside::Reason::Listings, 1));
  fake::settle(*l.store, l.tv);
  // Still one of History's feeds...
  CHECK(has(copiesOf(*l.store, l.history), l.h1));
  CHECK_FALSE(l.store->notPlayedOn(l.history, l.h1));
  // ...but its listings are gone from the guide and from Rewind's past.
  CHECK_FALSE(anyStarts(pastTitles(*l.store, l.history), "Ancient Aliens"));
  CHECK(l.store->guideOnNow(l.history, time::now()).empty());
  for (const Store::GuideOption& o : l.store->guideOptions(l.history))
    CHECK(o.now.rfind("Ancient Aliens", 0) != 0);

  // Discovery: the copy with Bay Area's guide is a candidate the choice
  // offers ("Listings look wrong?") until its listings are set aside.
  auto offers49ers = [&] {
    for (const Store::GuideOption& o : l.store->guideOptions(l.discovery))
      if (o.now == "49ers Now") return true;
    return false;
  };
  CHECK(offers49ers());
  l.store->setAside(record(l.discovery, l.d3, setaside::Reason::Listings, 3));
  fake::settle(*l.store, l.tv);
  CHECK_FALSE(offers49ers());
  CHECK(has(copiesOf(*l.store, l.discovery), l.d3));

  // Used again: History's listings are back.
  l.store->useAgain(l.history, l.h1);
  fake::settle(*l.store, l.tv);
  CHECK(anyStarts(pastTitles(*l.store, l.history), "Ancient Aliens"));
}

TEST_CASE("set aside: named as the channel it is, it becomes that channel's, across a restart") {
  if (catalog::reference::builtIn().empty()) return;
  Loaded l;
  REQUIRE(l.ready());
  // "Which channel is it?": the channel whose guide shows what this copy's
  // own listing has on now.
  CHECK(l.store->copyOnNow(l.d3, time::now()) == "49ers Now");
  std::vector<std::string> same = l.store->sameShowNow(l.d3, l.discovery, time::now());
  REQUIRE_FALSE(same.empty());
  CHECK(same.front() == l.bayArea);
  CHECK_FALSE(has(same, l.discovery));
  CHECK(l.store->sameShowNow(l.d1, l.discovery, time::now()).empty());

  setaside::Record r = l.store->setAside(record(l.discovery, l.d3, setaside::Reason::OtherChannel, 3, l.bayArea));
  CHECK(r.namedName == "NBC Sports Bay Area");
  fake::settle(*l.store, l.tv);
  CHECK_FALSE(has(copiesOf(*l.store, l.discovery), l.d3));
  CHECK(has(copiesOf(*l.store, l.bayArea), l.d3));
  CHECK(l.store->entryOfChannel(l.d3) == l.bayArea);
  // Set aside for Discovery only: on Bay Area it plays.
  CHECK_FALSE(l.store->notPlayedOn(l.bayArea, l.d3));

  l.restart();
  REQUIRE(l.ready());
  CHECK(has(copiesOf(*l.store, l.bayArea), l.d3));
  CHECK(l.store->entryOfChannel(l.d3) == l.bayArea);

  // Used again: back on Discovery.
  CHECK(l.store->useAgain(l.discovery, l.d3));
  fake::settle(*l.store, l.tv);
  CHECK(has(copiesOf(*l.store, l.discovery), l.d3));
  CHECK_FALSE(has(copiesOf(*l.store, l.bayArea), l.d3));
  CHECK(l.store->entryOfChannel(l.d3) == l.discovery);
}

// ------------------------------------------------------------------ session

namespace {

struct FakePlayer final : Player {
  struct Shared {
    std::vector<std::string> plays;
    PlayerState state;
  };
  explicit FakePlayer(std::shared_ptr<Shared> s) : s_(std::move(s)) {}
  void play(const Feed& f, bool) override { s_->plays.push_back(f.channelId); }
  void stop() override {}
  PlayerState state() const override { return s_->state; }
  std::shared_ptr<Shared> s_;
};

struct Rig : Loaded {
  std::shared_ptr<FakePlayer::Shared> player = std::make_shared<FakePlayer::Shared>();
  std::unique_ptr<Session> session;
  Rig() { session = std::make_unique<Session>(*store, tv.host(), std::make_unique<FakePlayer>(player), Session::Hooks{}); }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 250) {
      tv.ms += 250;
      session->tick(tv.ms);
      tv.pool.drain();
    }
  }
  const report::PlaybackLog::Entry* last(const std::string& event) const {
    const report::PlaybackLog::Entry* out = nullptr;
    for (const auto& e : session->log().entries())
      if (e.event == event) out = &e;
    return out;
  }
};

}  // namespace

TEST_CASE("set aside: the copy playing gives way to the next feed at once, and the log says why") {
  if (catalog::reference::builtIn().empty()) return;
  Rig r;
  REQUIRE(r.ready());
  REQUIRE(r.session->tune(r.discovery));
  r.run(500);
  r.player->state.picture = true;
  r.run(500);
  // Feed 3 (the copy with Bay Area's guide) plays.
  size_t at = r.session->feeds().size();
  for (size_t i = 0; i < r.session->feeds().size(); ++i)
    if (r.session->feeds()[i].channelId == r.d3) at = i;
  REQUIRE(at < r.session->feeds().size());
  REQUIRE(r.session->useFeed(at));
  r.run(500);
  REQUIRE(r.session->playingChannel() == r.d3);
  std::string after = r.session->feeds()[(at + 1) % r.session->feeds().size()].channelId;

  Session::SetAsideOutcome out =
      r.session->setAside(record(r.discovery, r.d3, setaside::Reason::OtherChannel, 3, r.bayArea));
  CHECK(out.switched);
  CHECK_FALSE(out.alone);
  CHECK(out.playing == after);
  r.run(500);
  CHECK(r.session->playingChannel() == after);
  CHECK(r.player->plays.back() == after);
  for (const Feed& f : r.session->feeds()) CHECK(f.channelId != r.d3);

  // The log: channel, feed and provider, why in plain words, never a link.
  const report::PlaybackLog::Entry* e = r.last("set aside");
  REQUIRE(e);
  CHECK(e->channel == "Discovery Channel");
  CHECK(e->feed == "Feed 3");
  CHECK(e->provider == "Test playlist");
  CHECK(e->detail ==
        "it's a different channel: NBC Sports Bay Area \xc2\xb7 its name \"US: Discovery Channel FHD\" in "
        "\"|US| NFL TEAMS\" \xc2\xb7 on now: \"49ers Now\" \xc2\xb7 guide: \"" +
            r.store->guideOnNow(r.discovery, time::now()) + "\"");
  CHECK(e->detail.find("Gold Rush") != std::string::npos);
  std::string csv = r.session->log().csv(time::Zone::utc());
  CHECK(csv.find("http") == std::string::npos);
  CHECK(csv.find("example.invalid") == std::string::npos);
  CHECK(csv.find("set aside") != std::string::npos);

  // OK again to undo: used again, and it plays again.
  CHECK(r.session->useAgain(r.discovery, r.d3));
  r.run(500);
  CHECK(r.session->playingChannel() == r.d3);
  const report::PlaybackLog::Entry* again = r.last("used again");
  REQUIRE(again);
  CHECK(again->channel == "Discovery Channel");
  CHECK(again->feed == "Feed 3");
  CHECK(again->detail == "it's a different channel: NBC Sports Bay Area, set aside today");
}

TEST_CASE("set aside: a backup set aside doesn't stop what plays; the only feed plays on") {
  if (catalog::reference::builtIn().empty()) return;
  Rig r;
  REQUIRE(r.ready());
  REQUIRE(r.session->tune(r.discovery));
  r.run(500);
  std::string playing = r.session->playingChannel();
  std::string backup;
  for (const Feed& f : r.session->feeds())
    if (f.channelId != playing) backup = f.channelId;
  REQUIRE_FALSE(backup.empty());
  Session::SetAsideOutcome out = r.session->setAside(record(r.discovery, backup, setaside::Reason::WrongLanguage, 2));
  CHECK_FALSE(out.switched);
  CHECK_FALSE(out.alone);
  r.run(500);
  // What plays plays on (opened again with the feeds read again), and no
  // backup switch can land on the copy set aside.
  CHECK(r.session->playingChannel() == playing);
  CHECK(r.player->plays.back() == playing);
  for (const Feed& f : r.session->feeds()) CHECK(f.channelId != backup);
  size_t plays = r.player->plays.size();

  // Its listings only: it keeps playing.
  out = r.session->setAside(record(r.discovery, playing, setaside::Reason::Listings, 1));
  CHECK_FALSE(out.switched);
  CHECK(r.session->playingChannel() == playing);
  r.run(500);
  CHECK(r.player->plays.size() == plays);  // nothing opened again

  // Bay Area has one feed: set aside, it plays on, and the outcome says so.
  REQUIRE(r.session->tune(r.bayArea));
  r.run(500);
  std::string only = r.session->playingChannel();
  REQUIRE(copiesOf(*r.store, r.bayArea).size() == 1);
  out = r.session->setAside(record(r.bayArea, only, setaside::Reason::DoesntWork, 1));
  CHECK(out.alone);
  CHECK_FALSE(out.switched);
  CHECK(r.session->playingChannel() == only);
}
