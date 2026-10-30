// Recovery: stalls, errors and the end of a playing stream.
#include "playback/recovery_rig.hpp"

using namespace rig;
using marigold::playback::kErrorGraceMs;
using marigold::playback::kSettleMs;
using marigold::playback::kStallNoteDelayMs;
using marigold::playback::kStallTimeoutMs;
using marigold::playback::kXtreamReconnectDelayMs;

namespace {

// Plays for `ms`: the position moves with the clock, the buffer stays a few
// seconds ahead (unless `bufferEnd` is given).
Actions playFor(Rig& t, Ms ms, double& pos, double bufferEnd = -1) {
  Actions got;
  Ms end = t.now + ms;
  while (t.now < end) {
    t.now += t.step;
    pos += static_cast<double>(t.step) / 1000.0;
    Actions a = t.take(t.r.position(t.now, pos, bufferEnd < 0 ? pos + 4 : bufferEnd));
    Actions b = t.take(t.r.tick(t.now));
    got.insert(got.end(), a.begin(), a.end());
    got.insert(got.end(), b.begin(), b.end());
  }
  return got;
}

// Holds the picture still for `ms`, still reporting the same position.
Actions freezeFor(Rig& t, Ms ms, double pos) {
  Actions got;
  Ms end = t.now + ms;
  while (t.now < end) {
    t.now += t.step;
    Actions a = t.take(t.r.position(t.now, pos, pos + 4));
    Actions b = t.take(t.r.tick(t.now));
    got.insert(got.end(), a.begin(), a.end());
    got.insert(got.end(), b.begin(), b.end());
  }
  return got;
}

}  // namespace

TEST_CASE("a stall shows Buffering after 3 s and reconnects the same feed after 10 s") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  t.wait(5000);
  Ms stalled = t.now;
  Actions s = t.take(t.r.bufferingStarted(t.now));
  CHECK(logged(s, "stall"));
  CHECK(notes(t.until(stalled + kStallNoteDelayMs - 10)).empty());
  CHECK(notes(t.until(stalled + kStallNoteDelayMs)) == std::vector<std::string>{"Buffering…"});
  CHECK_FALSE(logged(t.until(stalled + kStallTimeoutMs - 10), "reconnect"));
  Actions r = t.until(stalled + kStallTimeoutMs);
  CHECK(notes(r) == std::vector<std::string>{"Reconnecting…"});
  CHECK(logDetail(r, "reconnect") == "stall, not recovered after 10s; attempt 1 of 1; played 15s");
  CHECK_FALSE(has(r, Kind::RecordFeed));
  // Emby reconnects without an extra pause.
  Actions open = t.until(t.now + kSettleMs);
  const Action* re = first(open, Kind::Reconnect);
  REQUIRE(re);
  CHECK(re->feed == 0);
}

TEST_CASE("Emby gets one reconnect, then the next feed") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  t.take(t.r.bufferingStarted(t.now));
  t.untilOpen();  // the reconnect
  t.hand();
  t.picture();
  t.take(t.r.bufferingStarted(t.now));
  Actions fail = t.wait(kStallTimeoutMs);
  const Action* rec = first(fail, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK_FALSE(rec->worked);
  CHECK(notes(fail).back() == "Switching to a backup feed…");
  Actions open = t.untilOpen();
  CHECK(first(open, Kind::Play)->feed == 1);
}

TEST_CASE("Xtream gets two reconnects, each after a 1 s pause") {
  Rig t;
  t.tune({xt("a"), xt("b")});
  t.playNext();
  for (int i = 1; i <= 2; ++i) {
    t.take(t.r.bufferingStarted(t.now));
    Actions r = t.wait(kStallTimeoutMs);
    CHECK(logDetail(r, "reconnect").find("attempt " + std::to_string(i) + " of 2") != std::string::npos);
    Ms at = t.now;
    t.untilOpen();
    CHECK(t.now == at + kXtreamReconnectDelayMs);  // no second settle
    t.hand();
    t.picture();
  }
  t.take(t.r.bufferingStarted(t.now));
  Actions fail = t.wait(kStallTimeoutMs);
  CHECK(has(fail, Kind::RecordFeed));
  CHECK(first(t.untilOpen(), Kind::Play)->feed == 1);
}

TEST_CASE("a frozen picture with no buffering report reconnects 10 s after the last new frame") {
  Rig t;
  t.step = 100;  // every step moves the picture by more than the noise step
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  double pos = 100;
  Actions start = playFor(t, 2000, pos);
  // The moving position is the picture even without the first-frame signal.
  CHECK(t.r.sawFirstFrame());
  CHECK(has(start, Kind::RecordFeed));
  playFor(t, 5000, pos);
  Ms last = t.now;
  Actions f = freezeFor(t, kStallNoteDelayMs - 100, pos);
  CHECK(notes(f).empty());
  f = freezeFor(t, 100, pos);
  CHECK(notes(f) == std::vector<std::string>{"Buffering…"});
  CHECK(logged(f, "stall"));
  f = freezeFor(t, kStallTimeoutMs - kStallNoteDelayMs - 100, pos);
  CHECK_FALSE(logged(f, "reconnect"));
  f = freezeFor(t, 100, pos);
  CHECK(t.now == last + kStallTimeoutMs);
  CHECK(logged(f, "reconnect"));
  CHECK(notes(f) == std::vector<std::string>{"Reconnecting…"});
}

TEST_CASE("new video before the deadline means it recovered by itself") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  double pos = 10;
  playFor(t, 3000, pos);
  t.take(t.r.bufferingStarted(t.now));
  freezeFor(t, 5000, pos);
  CHECK(t.r.note() == "Buffering…");
  Actions back = playFor(t, 100, pos);
  CHECK(logged(back, "recovered by itself"));
  CHECK(notes(back) == std::vector<std::string>{""});
  t.take(t.r.bufferingEnded(t.now));
  Actions later = playFor(t, 30000, pos);
  CHECK_FALSE(logged(later, "reconnect"));
  CHECK(t.opens == 1);
}

TEST_CASE("buffering ending counts as playing again where no position is reported") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  t.take(t.r.bufferingStarted(t.now));
  t.wait(4000);
  Actions end = t.take(t.r.bufferingEnded(t.now));
  CHECK(logged(end, "recovered by itself"));
  CHECK(logDetail(end, "recovered by itself") == "4.0s after stall");
  CHECK_FALSE(logged(t.wait(20000), "reconnect"));
}

TEST_CASE("buffering ending is not enough while the position stays frozen") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  double pos = 10;
  playFor(t, 3000, pos);
  t.take(t.r.bufferingStarted(t.now));
  freezeFor(t, 4000, pos);
  CHECK(t.take(t.r.bufferingEnded(t.now)).empty());
  CHECK(logged(freezeFor(t, 7000, pos), "reconnect"));
}

TEST_CASE("an error while playing gets 6 s to clear by itself") {
  SUBCASE("it doesn't: reconnect") {
    Rig t;
    t.tune({emby("a"), emby("b")});
    t.playNext();
    Ms at = t.now;
    t.take(t.r.playerError(t.now, "pipeline error"));
    CHECK(notes(t.until(at + kStallNoteDelayMs)) == std::vector<std::string>{"Buffering…"});
    CHECK_FALSE(logged(t.until(at + kErrorGraceMs - 10), "reconnect"));
    Actions r = t.until(at + kErrorGraceMs);
    CHECK(logDetail(r, "reconnect").rfind("error: pipeline error, not recovered after 6s", 0) == 0);
  }
  SUBCASE("it does: nothing happens") {
    Rig t;
    t.tune({emby("a"), emby("b")});
    t.untilOpen();
    t.hand();
    double pos = 0;
    playFor(t, 2000, pos);
    t.take(t.r.playerError(t.now, "pipeline error"));
    Actions a = playFor(t, 20000, pos);
    CHECK(logged(a, "recovered by itself"));
    CHECK_FALSE(logged(a, "reconnect"));
  }
}

TEST_CASE("the stream ending while playing reconnects after 6 s") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  Actions e = t.take(t.r.ended(t.now));
  CHECK(logged(e, "stream ended"));
  Actions r = t.wait(kErrorGraceMs);
  CHECK(logDetail(r, "reconnect").rfind("the stream ended, not recovered after 6s", 0) == 0);
}

TEST_CASE("a second problem keeps the first deadline and the latest cause") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  Ms at = t.now;
  t.take(t.r.bufferingStarted(t.now));
  t.wait(2000);
  t.take(t.r.playerError(t.now, "late error"));
  Actions r = t.until(at + kStallTimeoutMs);
  CHECK(logDetail(r, "reconnect").rfind("error: late error, not recovered after 10s", 0) == 0);
}

TEST_CASE("a source that stopped sending is reconnected while the picture still runs") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  double pos = 90;
  playFor(t, 1000, pos);
  // The buffer end stops at 108 s; the position keeps moving towards it.
  Actions a = playFor(t, 14000, pos, 108);
  CHECK_FALSE(logged(a, "reconnect"));
  Actions b = playFor(t, 1200, pos, 108);
  CHECK(logDetail(b, "reconnect").rfind("the source stopped sending", 0) == 0);
  CHECK(notes(b) == std::vector<std::string>{"Reconnecting…"});
}

TEST_CASE("a growing buffer is never a dry source") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  double pos = 0;
  CHECK_FALSE(logged(playFor(t, 60000, pos), "reconnect"));
}

TEST_CASE("pausing never counts as a stall") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  double pos = 5;
  playFor(t, 2000, pos);
  t.take(t.r.setPaused(t.now, true));
  Actions paused = freezeFor(t, 60000, pos);
  CHECK(notes(paused).empty());
  CHECK_FALSE(logged(paused, "reconnect"));
  t.take(t.r.setPaused(t.now, false));
  Ms resumed = t.now;
  // After the pause the clock starts again: frozen for 10 s is a stall.
  Actions f = freezeFor(t, kStallTimeoutMs, pos);
  CHECK(logged(f, "reconnect"));
  CHECK(t.now == resumed + kStallTimeoutMs);
}

TEST_CASE("the first position sample and a backward jump are not new video") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  CHECK(t.take(t.r.position(t.now, 50)).empty());
  t.now += 100;
  CHECK(t.take(t.r.position(t.now, 10)).empty());  // live window slid back
  t.now += 100;
  CHECK(t.take(t.r.position(t.now, 10.04)).empty());  // under the noise step
  CHECK_FALSE(t.r.sawFirstFrame());
  t.now += 100;
  CHECK(has(t.take(t.r.position(t.now, 10.1)), Kind::RecordFeed));
  CHECK(t.r.sawFirstFrame());
}

TEST_CASE("a feed that has played a while earns one more reconnect") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  // Two stalls after more than 60 s each: allowance 2.
  for (int i = 1; i <= 2; ++i) {
    t.wait(61000);
    t.take(t.r.bufferingStarted(t.now));
    Actions r = t.wait(kStallTimeoutMs);
    CHECK(logDetail(r, "reconnect").find("attempt " + std::to_string(i) + " of 2") != std::string::npos);
    t.untilOpen();
    t.hand();
    t.picture();
  }
  t.wait(61000);
  t.take(t.r.bufferingStarted(t.now));
  CHECK(has(t.wait(kStallTimeoutMs), Kind::RecordFeed));
}

TEST_CASE("a healthy run over 120 s earns a fresh set of reconnects") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  for (int i = 0; i < 2; ++i) {
    t.wait(61000);
    t.take(t.r.bufferingStarted(t.now));
    t.wait(kStallTimeoutMs);
    t.untilOpen();
    t.hand();
    t.picture();
  }
  // Two reconnects used; a 121 s run starts the count again.
  t.wait(121000);
  t.take(t.r.bufferingStarted(t.now));
  Actions r = t.wait(kStallTimeoutMs);
  CHECK(logDetail(r, "reconnect").find("attempt 1 of 2") != std::string::npos);
  CHECK_FALSE(has(r, Kind::RecordFeed));
}

TEST_CASE("buffering right after the first picture is the start, not a stall") {
  Rig t;
  t.tune({xt("a")});
  t.untilOpen();
  t.hand();
  t.take(t.r.firstFrame(t.now));
  t.wait(300);
  Actions b = t.take(t.r.bufferingStarted(t.now));
  CHECK_FALSE(logged(b, "stall"));
  t.wait(400);
  Actions e = t.take(t.r.bufferingEnded(t.now));
  CHECK_FALSE(logged(e, "recovered by itself"));
  // One that lasts long enough to show "Buffering…" is logged as a stall.
  Rig u;
  u.tune({xt("a")});
  u.untilOpen();
  u.hand();
  u.take(u.r.firstFrame(u.now));
  u.wait(300);
  u.take(u.r.bufferingStarted(u.now));
  CHECK(logged(u.wait(4000), "stall"));
  // Later buffering is a stall as before.
  Rig v;
  v.tune({xt("a")});
  v.untilOpen();
  v.hand();
  v.take(v.r.firstFrame(v.now));
  v.wait(5000);
  CHECK(logged(v.take(v.r.bufferingStarted(v.now)), "stall"));
}
