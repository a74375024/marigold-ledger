// Recovery's picture check (videoCheck) and sources that keep closing
// (sourceClosed): how long each verdict must hold and what it leads to.
#include <doctest.h>

#include <string>
#include <vector>

#include "playback/recovery_rig.hpp"

using namespace rig;
using marigold::media::Verdict;
using marigold::playback::TuneOptions;

namespace {

// Starts feeds and plays the first: open, hand, picture.
Rig started(std::vector<FeedInfo> feeds, TuneOptions o = {}) {
  Rig r;
  o.settle = 0;
  r.tune(std::move(feeds), o);
  r.playNext();
  return r;
}

// Reports `v` every 250 ms for `ms`, as the session does every frame (the
// opens that fall due are left to untilOpen()).
Actions hold(Rig& r, Verdict v, Ms ms, const std::string& detail = {}) {
  Actions got;
  Ms end = r.now + ms;
  while (r.now < end) {
    r.now = std::min<Ms>(r.now + 250, end);
    Actions a = r.take(r.r.videoCheck(r.now, v, detail));
    got.insert(got.end(), a.begin(), a.end());
    if (has(a, Kind::NoPicture) || has(a, Kind::GiveUp)) break;
  }
  return got;
}

std::string recordedWhy(const Actions& a) {
  for (const Action& x : a)
    if (x.kind == Kind::RecordFeed && !x.worked) return x.detail;
  return {};
}

FeedInfo radio(const std::string& id) {
  FeedInfo f = xt(id);
  f.expectVideo = false;
  return f;
}

}  // namespace

TEST_CASE("picture check: a codec this TV can't decode goes the can't-play way at once") {
  Rig r = started({xt("a"), xt("b")});
  // Half a second: a flag left from the last stream never counts.
  Actions a = hold(r, Verdict::Unsupported, 250, "h264 High 4:2:2 10-bit");
  CHECK_FALSE(has(a, Kind::RecordFeed));
  a = hold(r, Verdict::Unknown, 250);
  a = hold(r, Verdict::Unsupported, 750, "h264 High 4:2:2 10-bit");
  const Action* rec = first(a, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK(rec->unplayable);
  CHECK(logDetail(a, "picture check") == "unsupported 0 s; h264 High 4:2:2 10-bit");
  r.untilOpen();
  CHECK(r.feed == 1);
}

TEST_CASE("picture check: scrambled for 3 s goes to the next feed, no retry") {
  Rig r = started({xt("a"), xt("b")});
  Actions a = hold(r, Verdict::Scrambled, 2500);
  CHECK_FALSE(has(a, Kind::RecordFeed));
  a = hold(r, Verdict::Scrambled, 1000, "100% of packets scrambled");
  CHECK(recordedWhy(a) == "scrambled");
  r.untilOpen();
  CHECK(r.feed == 1);
  CHECK(r.opens == 2);  // no retry of the same feed
}

TEST_CASE("picture check: no video, or tables only, for 6 s goes to the next feed") {
  for (Verdict v : {Verdict::NoVideo, Verdict::TablesOnly}) {
    Rig r = started({xt("a"), xt("b")});
    Actions a = hold(r, v, 5750, "PMT audio 1");
    CHECK_FALSE(has(a, Kind::RecordFeed));
    a = hold(r, v, 500, "PMT audio 1");
    REQUIRE(has(a, Kind::RecordFeed));
    if (v == Verdict::NoVideo) CHECK(logDetail(a, "picture check") == "no video track (PMT audio 1)");
    r.untilOpen();
    CHECK(r.feed == 1);
    CHECK(r.opens == 2);
  }
}

TEST_CASE("picture check: sound without a frame for 8 s is a failed start with one retry") {
  Rig r;
  r.tune({xt("a"), xt("b")}, {0, false, 0});
  r.untilOpen();
  r.hand();
  Actions a = hold(r, Verdict::NoFrame, 7750);
  CHECK_FALSE(logged(a, "start failed"));
  a = hold(r, Verdict::NoFrame, 500);
  CHECK(logged(a, "retry"));
  r.untilOpen();
  CHECK(r.feed == 0);
  r.hand();
  a = hold(r, Verdict::NoFrame, 8500);
  CHECK(recordedWhy(a) == "no picture");
  r.untilOpen();
  CHECK(r.feed == 1);
  CHECK(r.opens == 3);  // one retry, not the usual two
}

TEST_CASE("picture check: black at the start gets one reconnect on a direct feed, then the next") {
  Rig r = started({xt("a"), xt("b")});
  Actions a = hold(r, Verdict::Black, 7750, "I 432 B, 16 kbps");
  CHECK_FALSE(has(a, Kind::Reconnect));
  a = hold(r, Verdict::Black, 500, "I 432 B, 16 kbps");
  CHECK(logDetail(a, "picture check") == "black 8 s; I 432 B, 16 kbps");
  a = r.untilOpen();
  CHECK(has(a, Kind::Reconnect));
  CHECK(r.feed == 0);
  r.hand();
  r.picture();
  a = hold(r, Verdict::Black, 8250);
  CHECK(recordedWhy(a) == "black picture");
  r.untilOpen();
  CHECK(r.feed == 1);
}

TEST_CASE("picture check: black at the start of a server feed goes straight to the next") {
  Rig r = started({emby("a"), emby("b")});
  hold(r, Verdict::Black, 8250);
  Actions a = r.untilOpen();
  CHECK(has(a, Kind::Play));
  CHECK(r.feed == 1);
}

TEST_CASE("picture check: black or frozen mid-stream waits 15 s, reconnects once, then the next") {
  for (Verdict v : {Verdict::Black, Verdict::Frozen}) {
    Rig r = started({emby("a"), emby("b")});
    hold(r, Verdict::Picture, 30000);
    Actions a = hold(r, v, 14750);
    CHECK_FALSE(has(a, Kind::RecordFeed));
    CHECK_FALSE(has(a, Kind::Reconnect));
    hold(r, v, 500);
    a = r.untilOpen();
    CHECK(has(a, Kind::Reconnect));
    r.hand();
    r.picture();
    // Back at the start: 8 s, and its one reconnect is spent.
    a = hold(r, v, 8250);
    CHECK(recordedWhy(a) == (v == Verdict::Black ? "black picture" : "frozen picture"));
    r.untilOpen();
    CHECK(r.feed == 1);
  }
}

TEST_CASE("picture check: a fade through black (3 s) never acts") {
  Rig r = started({xt("a"), xt("b")});
  for (int i = 0; i < 5; ++i) {
    hold(r, Verdict::Picture, 20000);
    Actions a = hold(r, Verdict::Black, 3000);
    a = hold(r, Verdict::Static, 1000);
    CHECK_FALSE(has(a, Kind::RecordFeed));
  }
  CHECK(r.opens == 1);
}

TEST_CASE("picture check: buffering and pause suspend the count") {
  Rig r = started({xt("a"), xt("b")});
  hold(r, Verdict::Black, 6000);
  r.take(r.r.bufferingStarted(r.now));
  Actions a = hold(r, Verdict::Black, 5000);
  r.take(r.r.bufferingEnded(r.now));
  a = hold(r, Verdict::Black, 6000);
  CHECK_FALSE(has(a, Kind::Reconnect));
  CHECK_FALSE(logged(a, "picture check"));
  r.take(r.r.setPaused(r.now, true));
  a = hold(r, Verdict::Black, 20000);
  CHECK_FALSE(logged(a, "picture check"));
  r.take(r.r.setPaused(r.now, false));
  a = hold(r, Verdict::Black, 8250);
  CHECK(logged(a, "picture check"));
}

TEST_CASE("picture check: radio is never checked") {
  Rig r = started({radio("a"), radio("b")});
  Actions a = hold(r, Verdict::NoVideo, 60000);
  a = hold(r, Verdict::Black, 60000);
  CHECK_FALSE(has(a, Kind::RecordFeed));
  CHECK(r.opens == 1);
}

TEST_CASE("picture check: a static game flips to a source not tried yet; a channel only notes it") {
  TuneOptions game;
  game.game = true;
  Rig g = started({xt("a"), xt("b"), xt("c")}, game);
  hold(g, Verdict::Picture, 2000);
  Actions a = hold(g, Verdict::Static, 44750);
  CHECK_FALSE(logged(a, "picture check"));
  a = hold(g, Verdict::Static, 500, "P 318 B, I 137 KB, 625 kbps");
  CHECK(logDetail(a, "picture check") == "static 45 s; P 318 B, I 137 KB, 625 kbps");
  CHECK(notes(a) == std::vector<std::string>{"No picture on this source — trying 2 of 3"});
  g.untilOpen();
  CHECK(g.feed == 1);

  Rig c = started({xt("a"), xt("b")});
  a = hold(c, Verdict::Static, 120000);
  CHECK(logged(a, "picture check"));
  CHECK_FALSE(has(a, Kind::RecordFeed));
  CHECK(c.opens == 1);

  // A game whose every source was tried: noted, kept.
  Rig one = started({xt("a")}, game);
  a = hold(one, Verdict::Static, 60000);
  CHECK(logged(a, "picture check"));
  CHECK(one.opens == 1);
}

TEST_CASE("picture check: no feed with a picture leaves the sound playing and asks") {
  Rig r = started({xt("a"), xt("b")});
  hold(r, Verdict::NoVideo, 6250);
  r.untilOpen();
  r.hand();
  Actions a = hold(r, Verdict::NoVideo, 6250, "PMT audio 1");
  const Action* np = first(a, Kind::NoPicture);
  REQUIRE(np);
  CHECK(np->text.find("The sound plays on") != std::string::npos);
  CHECK_FALSE(has(a, Kind::GiveUp));
  CHECK_FALSE(has(a, Kind::CloseStream));
  CHECK(r.r.listening());
  CHECK(r.r.sawFirstFrame());
  // Nothing is checked any more; the stall watchdog watches the sound.
  a = hold(r, Verdict::Black, 60000);
  CHECK_FALSE(has(a, Kind::RecordFeed));
  CHECK(r.r.phase() == marigold::playback::Recovery::Phase::Playing);
}

TEST_CASE("picture check: scrambled everywhere gives up as before") {
  Rig r = started({xt("a"), xt("b")});
  hold(r, Verdict::Scrambled, 3250);
  r.untilOpen();
  r.hand();
  Actions a = hold(r, Verdict::Scrambled, 3250);
  CHECK(has(a, Kind::GiveUp));
  CHECK_FALSE(has(a, Kind::NoPicture));
}

TEST_CASE("picture check: within the budget of 6 actions in 180 s") {
  std::vector<FeedInfo> feeds;
  for (int i = 0; i < 10; ++i) feeds.push_back(xt("f" + std::to_string(i)));
  Rig r = started(feeds);
  int actions = 0;
  bool gaveUp = false;
  for (int i = 0; i < 12 && !gaveUp; ++i) {
    Actions a = hold(r, Verdict::Black, 8500);
    gaveUp = has(a, Kind::GiveUp);
    if (gaveUp) break;
    r.untilOpen();
    ++actions;
    r.hand();
    r.picture();
  }
  CHECK(gaveUp);
  CHECK(actions == 6);
}

TEST_CASE("sources that keep closing: three short connections around the picture") {
  Rig r = started({xt("a"), xt("b")});
  Actions a;
  for (int i = 0; i < 2; ++i) {
    r.wait(2400);
    a = r.take(r.r.sourceClosed(r.now, 2400, 310 * 1024, true));
    CHECK_FALSE(has(a, Kind::RecordFeed));
  }
  CHECK(logDetail(a, "relay") == "closed after 2 s, 310 KB; reopened it (2 times)");
  r.wait(2400);
  a = r.take(r.r.sourceClosed(r.now, 2400, 300 * 1024, true));
  CHECK(logDetail(a, "source keeps closing") == "4 connections in 7 s");
  CHECK(recordedWhy(a) == "the source keeps closing");
  a = r.untilOpen();
  CHECK(r.feed == 1);
  CHECK(r.opens == 2);  // no retries, no reconnects
}

TEST_CASE("sources that keep closing: not with one long connection, nor empty ones") {
  Rig r = started({xt("a"), xt("b")});
  r.wait(12000);
  r.take(r.r.sourceClosed(r.now, 12000, 9 << 20, true));
  for (int i = 0; i < 4; ++i) {
    r.wait(2000);
    Actions a = r.take(r.r.sourceClosed(r.now, 2000, 100 * 1024, true));
    CHECK_FALSE(has(a, Kind::RecordFeed));
  }
  Rig e = started({xt("a"), xt("b")});
  for (int i = 0; i < 4; ++i) {
    e.wait(1500);
    Actions a = e.take(e.r.sourceClosed(e.now, 0, 0, true));
    CHECK_FALSE(has(a, Kind::RecordFeed));
  }
  // Late in the stream (15 s after the picture) it's the relay's own business.
  Rig l = started({xt("a"), xt("b")});
  l.wait(16000);
  for (int i = 0; i < 3; ++i) {
    l.wait(2000);
    Actions a = l.take(l.r.sourceClosed(l.now, 2000, 100 * 1024, true));
    CHECK_FALSE(has(a, Kind::RecordFeed));
  }
}

TEST_CASE("sources that keep closing: a relay that gave up never earned reconnects") {
  Rig r = started({xt("a"), xt("b")});
  r.wait(20000);
  for (int i = 0; i < 6; ++i) {
    r.wait(6000);
    r.take(r.r.sourceClosed(r.now, 6000, 1 << 20, i < 5));
  }
  Actions a = r.take(r.r.ended(r.now));
  CHECK(logged(a, "source keeps closing"));
  CHECK_FALSE(logged(a, "reconnect"));
  a = r.untilOpen();
  CHECK(has(a, Kind::Play));
  CHECK(r.feed == 1);

  // One connection of 30 s or more: a stream that played, reconnected.
  Rig p = started({xt("a"), xt("b")});
  p.wait(40000);
  p.take(p.r.sourceClosed(p.now, 40000, 20 << 20, true));
  for (int i = 0; i < 3; ++i) {
    p.wait(6000);
    p.take(p.r.sourceClosed(p.now, 6000, 1 << 20, i < 2));
  }
  a = p.take(p.r.ended(p.now));
  CHECK(logged(a, "reconnect"));
  CHECK_FALSE(logged(a, "source keeps closing"));
}
