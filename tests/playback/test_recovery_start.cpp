// Recovery: tuning, the one-stream rule, opens and startup failures.
#include "playback/recovery_rig.hpp"

using namespace rig;
using marigold::playback::kSettleMs;
using marigold::playback::kStartupTimeoutMs;
using marigold::playback::kXtreamStartupTimeoutMs;
using marigold::playback::kXtreamRetryDelayMs;
using Phase = Recovery::Phase;

TEST_CASE("a tune opens after the settle delay, closing what is open first") {
  Rig t;
  Actions a = t.tune({xt("a"), xt("b")});
  CHECK(logged(a, "tune"));
  CHECK(logDetail(a, "tune") == "2 version(s)");
  CHECK_FALSE(has(a, Kind::Play));
  CHECK(t.r.phase() == Phase::Waiting);
  CHECK_FALSE(has(t.until(kSettleMs - 10), Kind::Play));
  Actions open = t.until(kSettleMs);
  REQUIRE(open.size() == 2);
  CHECK(open[0].kind == Kind::CloseStream);
  CHECK(open[1].kind == Kind::Play);
  CHECK(open[1].feed == 0);
  CHECK(t.r.isCurrent(open[1].token));
  CHECK(t.r.phase() == Phase::Opening);
}

TEST_CASE("a tune can start with a chosen feed") {
  Rig t;
  marigold::playback::TuneOptions o;
  o.start = 1;
  t.tune({xt("a"), xt("b"), xt("c")}, o);
  t.untilOpen();
  CHECK(t.feed == 1);
}

TEST_CASE("a superseded tune never opens anything") {
  Rig t;
  t.tune({emby("a1"), emby("a2")});
  t.wait(200);
  t.tune({emby("b1")});
  t.wait(100);
  t.tune({xt("c1"), xt("c2")});
  t.wait(2000);
  CHECK(t.opens == 1);
  CHECK(t.r.feeds().size() == 2);
  CHECK(t.r.feeds()[0].id == "c1");
  CHECK(t.r.isCurrent(t.token));
}

TEST_CASE("an Emby open must report back before the next one starts") {
  Rig t;
  t.tune({emby("a")});
  t.untilOpen();
  uint64_t first = t.token;
  t.wait(50);
  t.tune({emby("b")});
  CHECK_FALSE(t.r.isCurrent(first));
  // The server is still opening "a": nothing else opens meanwhile.
  t.wait(3000);
  CHECK(t.opens == 1);
  // "a" opens late: close it, never hand it to the player.
  Actions late = t.take(t.r.streamHanded(t.now, first));
  REQUIRE(late.size() == 1);
  CHECK(late[0].kind == Kind::CloseStream);
  Actions next = t.untilOpen(100);
  CHECK(t.opens == 2);
  CHECK(t.r.feeds()[t.feed].id == "b");
  CHECK(t.r.isCurrent(t.token));
}

TEST_CASE("a stale open that failed just frees the slot") {
  Rig t;
  t.tune({emby("a")});
  t.untilOpen();
  uint64_t first = t.token;
  t.tune({emby("b")});
  t.wait(1000);
  CHECK(t.opens == 1);
  CHECK(t.take(t.r.openFailed(t.now, first, "refused")).empty());
  t.untilOpen(100);
  CHECK(t.opens == 2);
}

TEST_CASE("stop closes the stream, and an open still in flight is closed when it lands") {
  Rig t;
  t.tune({emby("a")});
  t.untilOpen();
  uint64_t tok = t.token;
  Actions s = t.take(t.r.stop(t.now));
  CHECK(has(s, Kind::CloseStream));
  CHECK(t.r.phase() == Phase::Idle);
  CHECK(t.take(t.r.streamHanded(t.now, tok)).size() == 1);
  CHECK(t.wait(30000).empty());
  CHECK(t.take(t.r.stop(t.now)).empty());
}

TEST_CASE("player reports before the stream is handed over are ignored") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  CHECK(t.take(t.r.firstFrame(t.now)).empty());
  CHECK(t.take(t.r.playerError(t.now, "boom")).empty());
  CHECK(t.take(t.r.ended(t.now)).empty());
  CHECK(t.take(t.r.bufferingStarted(t.now)).empty());
  CHECK(t.take(t.r.position(t.now, 5)).empty());
  CHECK_FALSE(t.r.sawFirstFrame());
  // A token that was never issued is not current.
  CHECK_FALSE(t.r.isCurrent(t.token + 7));
  CHECK_FALSE(t.r.isCurrent(0));
}

TEST_CASE("no first frame within 20 s of the hand-over moves to the next feed") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.wait(4000);  // the server took a while to open it
  Actions h = t.hand();
  CHECK(logged(h, "play"));
  Ms handed = t.now;
  CHECK_FALSE(has(t.until(handed + kStartupTimeoutMs - 10), Kind::RecordFeed));
  Actions fail = t.until(handed + kStartupTimeoutMs);
  const Action* rec = first(fail, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK(rec->feed == 0);
  CHECK_FALSE(rec->worked);
  CHECK(logDetail(fail, "start failed") == "no picture after 20s");
  CHECK(notes(fail) == std::vector<std::string>{"Switching to a backup feed…"});
  CHECK_FALSE(has(fail, Kind::Play));
  Actions open = t.until(t.now + kSettleMs);
  const Action* play = first(open, Kind::Play);
  REQUIRE(play);
  CHECK(play->feed == 1);
}

TEST_CASE("the backup's first picture shows a toast and records the feed") {
  Rig t;
  t.tune({emby("a"), emby("b"), emby("c")});
  t.untilOpen();
  t.hand();
  t.untilKind(Kind::RecordFeed);
  t.untilOpen();
  Actions h = t.hand();
  CHECK(logged(h, "swapped in player"));
  Actions p = t.picture();
  const Action* toast = first(p, Kind::Toast);
  REQUIRE(toast);
  CHECK(toast->text == "Playing a backup feed (2 of 3)");
  const Action* rec = first(p, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK(rec->feed == 1);
  CHECK(rec->worked);
  CHECK(notes(p) == std::vector<std::string>{""});
  CHECK(t.r.note().empty());
  // Only once.
  CHECK(t.take(t.r.firstFrame(t.now)).empty());
}

TEST_CASE("surfing skips to the next feed without a backup toast") {
  Rig t;
  marigold::playback::TuneOptions o;
  o.surf = true;
  t.tune({emby("e1"), emby("e2")}, o);
  t.untilOpen();
  t.hand();
  Actions fail = t.untilKind(Kind::RecordFeed);
  CHECK(notes(fail) == std::vector<std::string>{"Skipping to the next feed…"});
  t.untilOpen();
  t.hand();
  CHECK_FALSE(has(t.picture(), Kind::Toast));
}

TEST_CASE("an Xtream feed that won't start is retried twice, 2 s apart, before the backup") {
  Rig t;
  t.tune({xt("a"), xt("b")});
  t.untilOpen();
  for (int attempt = 1; attempt <= 2; ++attempt) {
    t.hand();
    Ms handed = t.now;
    Actions fail = t.until(handed + kXtreamStartupTimeoutMs);
    // The note only goes out when it changes.
    CHECK(notes(fail) == (attempt == 1 ? std::vector<std::string>{"Retrying…"} : std::vector<std::string>{}));
    CHECK(t.r.note() == "Retrying…");
    CHECK(logDetail(fail, "retry") == "same feed in 2s (attempt " + std::to_string(attempt) + " of 2)");
    CHECK_FALSE(has(fail, Kind::RecordFeed));
    Ms failedAt = t.now;
    Actions open = t.untilOpen();
    const Action* re = first(open, Kind::Reconnect);
    REQUIRE(re);
    CHECK(re->feed == 0);
    CHECK(t.now == failedAt + kXtreamRetryDelayMs);  // no second settle
  }
  t.hand();
  Actions third = t.until(t.now + kXtreamStartupTimeoutMs);
  const Action* rec = first(third, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK_FALSE(rec->worked);
  CHECK(notes(third) == std::vector<std::string>{"Switching to a backup feed…"});
  Actions open = t.untilOpen();
  CHECK(first(open, Kind::Play)->feed == 1);
}

TEST_CASE("M3U feeds get the Xtream startup retries") {
  Rig t;
  t.tune({m3u("a"), m3u("b")});
  t.untilOpen();
  t.hand();
  Actions fail = t.until(t.now + kStartupTimeoutMs);
  CHECK(logged(fail, "retry"));
  CHECK(has(t.untilOpen(), Kind::Reconnect));
}

TEST_CASE("an error before the first picture fails the start after a 3 s grace") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  t.wait(1000);
  Ms errorAt = t.now;
  Actions e = t.take(t.r.playerError(t.now, "decoder refused"));
  CHECK(logDetail(e, "player error") == "decoder refused");
  // A second report of the same error does not push the deadline back.
  t.wait(2000);
  t.take(t.r.playerError(t.now, "decoder refused"));
  CHECK_FALSE(has(t.until(errorAt + 2990), Kind::RecordFeed));
  Actions fail = t.until(errorAt + 3000);
  CHECK(has(fail, Kind::RecordFeed));
  CHECK(logDetail(fail, "start failed") == "error: decoder refused");
}

TEST_CASE("a picture within the startup grace cancels the failure") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  t.take(t.r.playerError(t.now, "glitch"));
  t.wait(1000);
  t.picture();
  CHECK_FALSE(has(t.wait(30000), Kind::RecordFeed));
  CHECK(t.r.feedIndex() == 0);
  CHECK(t.opens == 1);
}

TEST_CASE("buffering before the first picture is just the start") {
  Rig t;
  t.tune({emby("a"), emby("b")});
  t.untilOpen();
  t.hand();
  Ms handed = t.now;
  CHECK(t.take(t.r.bufferingStarted(t.now)).empty());
  Actions waiting = t.until(handed + kStartupTimeoutMs - 10);
  CHECK(notes(waiting).empty());
  CHECK(has(t.until(handed + kStartupTimeoutMs), Kind::RecordFeed));
}

TEST_CASE("the stream ending before a picture fails the start") {
  Rig t;
  t.tune({emby("only")});
  t.untilOpen();
  t.hand();
  t.take(t.r.ended(t.now));
  Actions fail = t.wait(3000);
  CHECK(giveUpMessage(fail) == "This channel stopped working. Ended before a picture.");
  CHECK(t.r.phase() == Phase::Failed);
}

TEST_CASE("a feed that can't be opened gives way to the next at once") {
  Rig t;
  t.tune({emby("a"), emby("b"), emby("c")});
  t.untilOpen();
  uint64_t tok = t.token;
  Actions f = t.take(t.r.openFailed(t.now, tok, "Server said no"));
  const Action* rec = first(f, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK(rec->feed == 0);
  CHECK_FALSE(rec->worked);
  const Action* play = first(f, Kind::Play);
  REQUIRE(play);
  CHECK(play->feed == 1);
  CHECK_FALSE(t.r.isCurrent(tok));
  CHECK(t.r.isCurrent(play->token));
  CHECK(notes(f).empty());
  t.take(t.r.openFailed(t.now, t.token, "Server said no"));
  Actions last = t.take(t.r.openFailed(t.now, t.token, "Server said no"));
  CHECK(giveUpMessage(last) == "None of this channel's 3 versions could be opened right now. Server said no");
  CHECK(has(last, Kind::CloseStream));
  CHECK(t.r.phase() == Phase::Failed);
}

TEST_CASE("open failures don't use the recovery budget") {
  Rig t;
  std::vector<FeedInfo> feeds;
  for (int i = 0; i < 9; ++i) feeds.push_back(emby("f" + std::to_string(i)));
  t.tune(feeds);
  t.untilOpen();
  Actions a;
  for (int i = 0; i < 9; ++i) a = t.take(t.r.openFailed(t.now, t.token, ""));
  CHECK(giveUpMessage(a) ==
        "None of this channel's 9 versions could be opened right now. This channel couldn't be opened right now.");
}

TEST_CASE("open failure messages for one feed and for surfing") {
  SUBCASE("one feed: its own reason") {
    Rig t;
    t.tune({emby("a")});
    t.untilOpen();
    CHECK(giveUpMessage(t.take(t.r.openFailed(t.now, t.token, "Sign in to this server again."))) ==
          "Sign in to this server again.");
  }
  SUBCASE("surfing") {
    Rig t;
    marigold::playback::TuneOptions o;
    o.surf = true;
    t.tune({emby("a"), emby("b")}, o);
    t.untilOpen();
    t.take(t.r.openFailed(t.now, t.token, "x"));
    CHECK(giveUpMessage(t.take(t.r.openFailed(t.now, t.token, "x"))) == "None of the next feeds are live right now.");
  }
}

TEST_CASE("a channel with no feeds gives up at once") {
  Rig t;
  Actions a = t.tune({});
  CHECK(giveUpMessage(a) == "This channel couldn't be opened right now.");
  CHECK(has(a, Kind::CloseStream));
  CHECK(t.wait(5000).empty());
}

TEST_CASE("an Xtream feed has 10 s for a picture; Emby keeps 20") {
  CHECK(marigold::playback::startupTimeout(marigold::playback::FeedKind::Xtream) == 10000);
  CHECK(marigold::playback::startupTimeout(marigold::playback::FeedKind::Emby) == kStartupTimeoutMs);
  Rig t;
  t.tune({xt("a"), xt("b")});
  t.untilOpen();
  t.hand();
  Ms handed = t.now;
  CHECK_FALSE(logged(t.until(handed + kXtreamStartupTimeoutMs - 10), "start failed"));
  Actions fail = t.until(handed + kXtreamStartupTimeoutMs);
  CHECK(logDetail(fail, "start failed") == "no picture after 10s");
}

TEST_CASE("an error meaning this TV can't play the feed moves on at once, with no retries") {
  CHECK(marigold::playback::cannotPlayHere("601 Resource Allocation Error"));
  CHECK_FALSE(marigold::playback::cannotPlayHere("203 AV Type Not Founded"));
  CHECK_FALSE(marigold::playback::cannotPlayHere("202 Media Not Found"));
  Rig t;
  t.tune({xt("a"), xt("b")});
  t.untilOpen();
  t.hand();
  Actions a = t.take(t.r.playerError(t.now, "601 Resource Allocation Error"));
  CHECK(logDetail(a, "start failed") == "this TV can't play this version (601 Resource Allocation Error)");
  CHECK_FALSE(logged(a, "retry"));
  const Action* rec = first(a, Kind::RecordFeed);
  REQUIRE(rec);
  CHECK_FALSE(rec->worked);
  CHECK(rec->unplayable);
  CHECK(rec->feed == 0);
  CHECK(logged(a, "switch"));
  Actions open = t.untilOpen();
  CHECK(first(open, Kind::Play)->feed == 1);
}

TEST_CASE("a direct feed doesn't wait for an Emby open still on its way") {
  Rig t;
  t.tune({emby("a")});
  t.untilOpen();
  uint64_t stale = t.token;
  t.wait(650);
  t.tune({xt("b")});
  // The server is still opening "a", but "b" opens after its settle.
  Actions open = t.until(t.now + kSettleMs);
  const Action* play = first(open, Kind::Play);
  REQUIRE(play);
  CHECK(t.r.feeds()[play->feed].id == "b");
  t.hand();
  // "a" opens late: only its server stream is closed (the token says so),
  // and "b" keeps playing.
  Actions late = t.take(t.r.streamHanded(t.now, stale));
  const Action* close = first(late, Kind::CloseStream);
  REQUIRE(close);
  CHECK(close->token == stale);
  CHECK(t.r.phase() == Phase::Playing);
  CHECK(t.r.feeds()[t.r.feedIndex()].id == "b");
}

TEST_CASE("an Emby open still waits for the one before it, even after a direct feed") {
  Rig t;
  t.tune({emby("a")});
  t.untilOpen();
  t.wait(100);
  t.tune({xt("b")});
  t.untilOpen();
  t.hand();
  t.tune({emby("c")});
  t.wait(3000);
  // "a" hasn't reported back: "c" doesn't open yet.
  CHECK(t.opens == 2);
}

TEST_CASE("a tune with no settle opens at once") {
  Rig t;
  marigold::playback::TuneOptions o;
  o.settle = 0;
  t.take(t.r.tune(t.now, {xt("a")}, o));
  Ms tuned = t.now;
  CHECK(has(t.untilOpen(kSettleMs), Kind::Play));
  CHECK(t.now - tuned < kSettleMs);  // the first tick, not after a settle
}

TEST_CASE("a direct start with no data at all is retried after 6 s, where the player reports loading") {
  using marigold::playback::kNoDataMs;
  {
    Rig t;
    t.tune({xt("a"), xt("b")});
    t.untilOpen();
    t.hand();
    Ms handed = t.now;
    for (Ms at = handed; at < handed + kNoDataMs - 10; at += 100) {
      t.r.loading(false);
      CHECK_FALSE(logged(t.until(at + 100 > handed + kNoDataMs - 10 ? handed + kNoDataMs - 10 : at + 100), "start failed"));
    }
    t.r.loading(false);
    Actions fail = t.until(handed + kNoDataMs);
    CHECK(logDetail(fail, "start failed") == "no data after 6s");
    CHECK(logged(fail, "retry"));
  }
  {
    // Data came (loadCompleted / buffered): the full timeout applies.
    Rig t;
    t.tune({xt("a"), xt("b")});
    t.untilOpen();
    t.hand();
    Ms handed = t.now;
    t.r.loading(true);
    CHECK_FALSE(logged(t.until(handed + kNoDataMs + 1000), "start failed"));
  }
  {
    // A player that doesn't report loading (tvOS): the full timeout applies.
    Rig t;
    t.tune({xt("a"), xt("b")});
    t.untilOpen();
    t.hand();
    Ms handed = t.now;
    CHECK_FALSE(logged(t.until(handed + kNoDataMs + 1000), "start failed"));
  }
  {
    // Emby opens on the server first and is slower: never cut early.
    Rig t;
    t.tune({emby("a"), emby("b")});
    t.untilOpen();
    t.hand();
    Ms handed = t.now;
    t.r.loading(false);
    CHECK_FALSE(logged(t.until(handed + kNoDataMs + 1000), "start failed"));
  }
}

TEST_CASE("an Emby quick start that fails gets one more try on the same feed (the normal open)") {
  Rig t;
  marigold::playback::FeedInfo a{"a", marigold::playback::FeedKind::Emby, true};
  marigold::playback::FeedInfo b{"b", marigold::playback::FeedKind::Emby, true};
  t.tune({a, b});
  t.untilOpen();
  t.hand();
  Actions fail = t.until(t.now + kStartupTimeoutMs);
  CHECK(logged(fail, "retry"));
  CHECK(logDetail(fail, "retry") == "same feed in 0s (attempt 1 of 1)");
  Actions open = t.untilOpen();
  const Action* re = first(open, Kind::Reconnect);
  REQUIRE(re);
  CHECK(re->feed == 0);
  // The second failure moves on.
  t.hand();
  Actions again = t.until(t.now + kStartupTimeoutMs);
  CHECK(logged(again, "switch"));
}

TEST_CASE("an Emby quick start the server refuses falls back to the normal open at once") {
  Rig t;
  marigold::playback::FeedInfo a{"a", marigold::playback::FeedKind::Emby, true};
  t.tune({a});
  t.untilOpen();
  t.hand();
  // "300 Network Error" a moment after the hand-over: no grace, no pause.
  Actions err = t.take(t.r.playerError(t.now, "300 Network Error"));
  CHECK(logged(err, "start failed"));
  CHECK(logDetail(err, "retry") == "same feed in 0s (attempt 1 of 1)");
  Ms failed = t.now;
  Actions open = t.untilOpen();
  CHECK(first(open, Kind::Reconnect));
  CHECK(t.now - failed <= marigold::playback::kSettleMs);
}

TEST_CASE("a network error before the picture on a direct feed is retried at once") {
  Rig t;
  t.tune({xt("a"), xt("b")});
  t.untilOpen();
  t.hand();
  Actions err = t.take(t.r.playerError(t.now, "300 Network Error"));
  CHECK(logged(err, "start failed"));
  CHECK(logged(err, "retry"));
  // Other errors still get the grace.
  Rig u;
  u.tune({xt("a")});
  u.untilOpen();
  u.hand();
  CHECK_FALSE(logged(u.take(u.r.playerError(u.now, "100 Playing error")), "start failed"));
}
