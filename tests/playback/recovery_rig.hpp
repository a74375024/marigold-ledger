// A fake clock and recorder around playback::Recovery for the tests.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "core/playback/recovery.hpp"
#include "doctest.h"

namespace rig {

using marigold::playback::Action;
using marigold::playback::Actions;
using marigold::playback::FeedInfo;
using marigold::playback::FeedKind;
using marigold::playback::Ms;
using marigold::playback::Recovery;
using Kind = Action::Kind;

inline FeedInfo xt(const std::string& id) { return {id, FeedKind::Xtream}; }
inline FeedInfo emby(const std::string& id) { return {id, FeedKind::Emby}; }
inline FeedInfo m3u(const std::string& id) { return {id, FeedKind::Playlist}; }

inline bool has(const Actions& a, Kind k) {
  return std::any_of(a.begin(), a.end(), [k](const Action& x) { return x.kind == k; });
}
inline int count(const Actions& a, Kind k) {
  return static_cast<int>(std::count_if(a.begin(), a.end(), [k](const Action& x) { return x.kind == k; }));
}
inline const Action* first(const Actions& a, Kind k) {
  for (const Action& x : a)
    if (x.kind == k) return &x;
  return nullptr;
}
inline std::vector<std::string> notes(const Actions& a) {
  std::vector<std::string> out;
  for (const Action& x : a)
    if (x.kind == Kind::Note) out.push_back(x.text);
  return out;
}
inline bool logged(const Actions& a, const std::string& event) {
  return std::any_of(a.begin(), a.end(), [&](const Action& x) { return x.kind == Kind::Log && x.text == event; });
}
inline std::string logDetail(const Actions& a, const std::string& event) {
  for (const Action& x : a)
    if (x.kind == Kind::Log && x.text == event) return x.detail;
  return {};
}
inline std::string giveUpMessage(const Actions& a) {
  const Action* g = first(a, Kind::GiveUp);
  return g ? g->text : std::string();
}

struct Rig {
  Recovery r;
  Ms now = 0;
  Ms step = 10;
  Actions all;
  uint64_t token = 0;  // of the last Play / Reconnect
  size_t feed = Action::npos;
  int opens = 0;

  // Records `a` and checks the invariants every action list must keep.
  Actions take(Actions a) {
    for (size_t i = 0; i < a.size(); ++i) {
      const Action& x = a[i];
      if (x.kind == Kind::Play || x.kind == Kind::Reconnect) {
        // Every open first closes the open one.
        REQUIRE(i > 0);
        CHECK(a[i - 1].kind == Kind::CloseStream);
        CHECK(x.token != 0);
        token = x.token;
        feed = x.feed;
        ++opens;
      }
      CHECK(x.text.find("://") == std::string::npos);
      CHECK(x.detail.find("://") == std::string::npos);
      CHECK(x.text.find("example.invalid") == std::string::npos);
      CHECK(x.detail.find("example.invalid") == std::string::npos);
    }
    all.insert(all.end(), a.begin(), a.end());
    return a;
  }

  Actions tune(std::vector<FeedInfo> feeds, marigold::playback::TuneOptions o = {}) {
    return take(r.tune(now, std::move(feeds), o));
  }
  // Moves the clock to `t`, ticking every `step` ms.
  Actions until(Ms t) {
    Actions got;
    while (now < t) {
      now = std::min(now + step, t);
      Actions a = take(r.tick(now));
      got.insert(got.end(), a.begin(), a.end());
    }
    return got;
  }
  Actions wait(Ms ms) { return until(now + ms); }
  // Ticks until a Play / Reconnect comes out (at most `limit` ms).
  Actions untilOpen(Ms limit = 60000) {
    Actions got;
    Ms end = now + limit;
    while (now < end) {
      now += step;
      Actions a = take(r.tick(now));
      got.insert(got.end(), a.begin(), a.end());
      if (has(a, Kind::Play) || has(a, Kind::Reconnect)) return got;
    }
    FAIL("no open within the limit");
    return got;
  }
  Actions hand() { return take(r.streamHanded(now, token)); }
  Actions picture() { return take(r.firstFrame(now)); }
  // Opens what is due, hands it to the player and shows a picture.
  Actions playNext() {
    Actions got = untilOpen();
    Actions h = hand();
    Actions p = picture();
    got.insert(got.end(), h.begin(), h.end());
    got.insert(got.end(), p.begin(), p.end());
    return got;
  }
  // Ticks until an action of kind `k` comes out (at most `limit` ms).
  Actions untilKind(Kind k, Ms limit = 60000) {
    Actions got;
    Ms end = now + limit;
    while (now < end) {
      now += step;
      Actions a = take(r.tick(now));
      got.insert(got.end(), a.begin(), a.end());
      if (has(a, k)) return got;
    }
    FAIL("action never came");
    return got;
  }
};

}  // namespace rig
