// A store on the pretend TV with a made-up playlist account loaded.
#pragma once

#include <string>
#include <utility>

#include "core/engine/store.hpp"
#include "engine/fake_host.hpp"

namespace fake {

constexpr const char* kPlaylist = "http://mock.example.invalid:8080/playlist.m3u";

inline marigold::engine::AccountForm m3uForm() {
  marigold::engine::AccountForm f;
  f.kind = marigold::engine::AccountForm::Kind::M3U;
  f.name = "Test playlist";
  f.server = kPlaylist;
  return f;
}

// Lets everything settle: queued work runs, then the engine ticks as the
// app's frame loop would, ten seconds on (past every short delay).
inline void settle(marigold::engine::Store& store, Tv& tv) {
  for (int i = 0; i < 100; ++i) {
    tv.pool.drain();
    tv.ms += 100;
    store.tick(tv.ms);
  }
  tv.pool.drain();
}

// Adds the playlist account and lets everything it starts settle.
inline std::string addPlaylist(marigold::engine::Store& store, Tv& tv) {
  std::string problem = "(not answered)";
  store.addAccount(m3uForm(), [&](std::string p) { problem = std::move(p); });
  settle(store, tv);
  return problem;
}

}  // namespace fake
