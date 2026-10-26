// Recovery: the budget, one round through the feeds, Sources picks, messages.
#include "playback/recovery_rig.hpp"

using namespace rig;
using marigold::playback::kStallTimeoutMs;
using marigold::playback::kStartupTimeoutMs;
using marigold::playback::safeMessage;
using Phase = Recovery::Phase;

namespace {

std::vector<FeedInfo> embyFeeds(int n) {
  std::vector<FeedInfo> out;
  for (int i = 0; i < n; ++i) out.push_back(emby("e" + std::to_string(i)));
  return out;
}

// Opens what is due and hands it over, with no picture: returns what
// happens when the start times out.
Actions failStart(Rig& t) {
  t.untilOpen();
  t.hand();
  return t.wait(kStartupTimeoutMs);
}

// Opens what is due, plays it, then stalls for good.
Actions playThenStall(Rig& t, Ms played = 0) {
  t.playNext();
  if (played) t.wait(played);
  t.take(t.r.bufferingStarted(t.now));
  return t.wait(kStallTimeoutMs);
}

}  // namespace

TEST_CASE("one round through the feeds, then it gives up") {
  Rig t;
  t.tune(embyFeeds(3));
  failStart(t);
  failStart(t);
  Actions last = failStart(t);
  CHECK(giveUpMessage(last) == "None of this channel's 3 versions are working right now.");
  CHECK(has(last, Kind::CloseStream));
  CHECK(t.r.phase() == Phase::Failed);
  CHECK(t.opens == 3);
  CHECK(t.r.note().empty());
  CHECK(t.wait(60000).empty());
}

TEST_CASE("a single feed that stops working says why") {
  Rig t;
  t.tune({emby("only")});
  Actions a = failStart(t);
  CHECK(giveUpMessage(a) == "This channel stopped working. No picture after 20s.");
}

TEST_CASE("six recoveries in 180 s, then it stops: dropping") {
  Rig t;
  t.tune(embyFeeds(4));
  // Each Emby feed: one reconnect, then a switch. Two recoveries each.
  for (int feed = 0; feed < 3; ++feed) {
    CHECK(logged(playThenStall(t), "reconnect"));
    CHECK(logged(playThenStall(t), "switch"));
  }
  Actions a = playThenStall(t);
  CHECK(giveUpMessage(a) == "This channel keeps dropping. Try another version from Choose Feed, or try again later.");
  CHECK(t.now < 180000);
}

TEST_CASE("six recoveries in 180 s, then it stops: failing to start") {
  Rig t;
  t.tune({xt("a"), xt("b"), xt("c")});
  Actions a;
  for (int i = 0; i < 7; ++i) a = failStart(t);
  CHECK(giveUpMessage(a) ==
        "This channel keeps failing to start. Try another version from Choose Feed, or try again later.");
}

TEST_CASE("six recoveries in 180 s, then it stops: failing") {
  Rig t;
  t.tune(embyFeeds(8));
  Actions a;
  for (int i = 0; i < 7; ++i) a = failStart(t);
  CHECK(giveUpMessage(a) == "This channel keeps failing. Try another version from Choose Feed, or try again later.");
}

TEST_CASE("recoveries older than 180 s don't count") {
  Rig t;
  t.tune(embyFeeds(5));
  Actions a;
  // About one recovery a minute: never six inside the window.
  for (int i = 0; i < 10; ++i) a = playThenStall(t, 50000);
  CHECK(giveUpMessage(a) == "None of this channel's 5 versions are working right now.");
}

TEST_CASE("a new channel starts with a fresh budget") {
  Rig t;
  t.tune({xt("a"), xt("b")});
  failStart(t);
  failStart(t);
  CHECK(logged(failStart(t), "switch"));
  t.tune({xt("a"), xt("b")});
  Actions a = failStart(t);
  CHECK(logged(a, "retry"));
  CHECK(logDetail(a, "retry").find("attempt 1 of 2") != std::string::npos);
}

TEST_CASE("a feed picked in Sources wraps round the list before giving up") {
  Rig t;
  t.tune(embyFeeds(3));
  t.playNext();
  Actions p = t.take(t.r.pick(t.now, 2));
  CHECK(notes(p) == std::vector<std::string>{"Switching…"});
  CHECK(logDetail(p, "you picked") == "version 3 of 3");
  Actions open = t.untilOpen();
  CHECK(first(open, Kind::Play)->feed == 2);
  t.hand();
  Actions f = t.wait(kStartupTimeoutMs);
  CHECK(notes(f) == std::vector<std::string>{"The feed you picked stopped working. Trying another…"});
  CHECK(first(t.untilOpen(), Kind::Play)->feed == 0);
  t.hand();
  t.wait(kStartupTimeoutMs);
  CHECK(first(t.untilOpen(), Kind::Play)->feed == 1);
  t.hand();
  Actions last = t.wait(kStartupTimeoutMs);
  CHECK(giveUpMessage(last) == "None of this channel's 3 versions are working right now.");
}

TEST_CASE("a feed picked in Sources gets two more reconnects") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  t.take(t.r.pick(t.now, 1));
  for (int i = 1; i <= 3; ++i) {
    Actions r = playThenStall(t);
    CHECK(logDetail(r, "reconnect").find("attempt " + std::to_string(i) + " of 3") != std::string::npos);
  }
  Actions m = playThenStall(t);
  CHECK(notes(m).back() == "The feed you picked stopped working. Trying another…");
  CHECK(first(t.untilOpen(), Kind::Play)->feed == 0);
}

TEST_CASE("picking the feed that is already playing does nothing") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  CHECK(t.take(t.r.pick(t.now, 0)).size() > 0);  // not playing yet: restarts it
  t.playNext();
  CHECK(t.take(t.r.pick(t.now, 0)).empty());
  CHECK(t.take(t.r.pick(t.now, 5)).empty());
  Rig idle;
  CHECK(idle.take(idle.r.pick(idle.now, 0)).empty());
}

TEST_CASE("a pick from the failure card starts the channel again") {
  Rig t;
  t.tune({emby("a")});
  failStart(t);
  CHECK(t.r.phase() == Phase::Failed);
  Actions p = t.take(t.r.pick(t.now, 0));
  CHECK(logged(p, "tune"));
  t.untilOpen();
  CHECK(t.r.phase() == Phase::Opening);
  t.hand();
  t.picture();
  CHECK(t.r.phase() == Phase::Playing);
}

TEST_CASE("links in player errors never reach a log line or the card") {
  Rig t;
  t.tune({emby("only")});
  t.untilOpen();
  t.hand();
  Actions e = t.take(t.r.playerError(t.now, "Failed to open https://example.invalid/live/u/p/1.ts?id=7"));
  CHECK(logDetail(e, "player error") == "Failed to open the stream");
  Actions a = t.wait(3000);
  CHECK(giveUpMessage(a) == "This channel stopped working. Error: Failed to open the stream.");
}

TEST_CASE("links in open errors never reach the card") {
  Rig t;
  t.tune({emby("only")});
  t.untilOpen();
  Actions a = t.take(t.r.openFailed(t.now, t.token, "HTTP 500 from http://example.invalid:8096/emby?api_key=x"));
  CHECK(giveUpMessage(a) == "HTTP 500 from the stream");
}

TEST_CASE("safeMessage drops links and login values") {
  CHECK(safeMessage("rtmp://example.invalid/a b") == "the stream b");
  std::string s = safeMessage("bad login password=hunter22");
  CHECK(s.find("hunter22") == std::string::npos);
  CHECK(s.find("password=") != std::string::npos);
  CHECK(safeMessage("decoder error 203") == "decoder error 203");
}

TEST_CASE("stop logs how long the channel was watched") {
  Rig t;
  t.tune({emby("a")});
  t.playNext();
  t.wait(42000);
  Actions s = t.take(t.r.stop(t.now));
  CHECK(logDetail(s, "stop") == "watched 42s");
  CHECK(has(s, Kind::CloseStream));
  CHECK_FALSE(t.r.active());
}

TEST_CASE("notes are cleared when a new channel is tuned") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.playNext();
  t.take(t.r.bufferingStarted(t.now));
  t.wait(4000);
  CHECK(t.r.note() == "Buffering…");
  Actions a = t.tune({emby("c")});
  CHECK(notes(a) == std::vector<std::string>{""});
  // The stall of the old channel is gone with it.
  t.playNext();
  CHECK_FALSE(logged(t.wait(20000), "reconnect"));
}
