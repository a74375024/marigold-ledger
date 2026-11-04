// Search as you type (Build 114), shared by the Search screen and the
// player's search (Build 132): find() on every letter, findShows() once
// typing pauses (kShowsDelayMs; at once for one character), merge() into
// the results. What a query found on an index is kept: the pause's pass
// looks only through the shows, and the shows found stay while the query
// and the index's shows do (a data refresh doesn't look again).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/engine/search.hpp"

namespace marigold::engine::search {

// Shows are looked for once typing pauses this long.
constexpr uint32_t kShowsDelayMs = 450;

// The same query for what it finds (the shows kept for it).
bool sameQuery(const Query& a, const Query& b);

// What is typed, parsed against the index's titles (a query that is
// itself a title is left as typed). `exactOnly`: "Search for … instead".
Query parseFor(const std::shared_ptr<const Index>& index, const std::string& text, bool exactOnly);

class Live {
 public:
  // What a pass looked through, and how long it took (the timing log).
  struct Pass {
    bool found = false, shows = false;
    double foundMs = 0, showsMs = 0;
  };

  // Runs `q` on `index`. `shows`: the guide's shows too (a query under two
  // characters always has them); without, a longer query's shows are due
  // kShowsDelayMs after `ms` unless they already are. `keep`: what this
  // query found on this index stays (the pause's pass, a refresh).
  // `o.shows` is set here.
  Pass run(const std::shared_ptr<const Index>& index, const Query& q, Options o, bool shows, bool keep, uint32_t ms);

  const Results& results() const { return results_; }
  // The last pass had the shows (the top result is final).
  bool showsIn() const { return showsIn_; }
  // When the shows' pass is due (0: none waits). A new query typed drops
  // it (typed()): its shows wait for a pause in the typing.
  uint32_t showsDue() const { return showsAt_; }
  void typed() { showsAt_ = 0; }
  const std::shared_ptr<const Index>& index() const { return index_; }

 private:
  std::shared_ptr<const Index> index_;  // the last pass's (Found and Results point into it)
  std::optional<Found> find_;
  std::optional<FoundShows> shows_;
  std::shared_ptr<const Index> showsIndex_;
  Query showsQuery_;
  bool showsScoped_ = false;
  Results results_;
  bool showsIn_ = false;
  uint32_t showsAt_ = 0;
};

}  // namespace marigold::engine::search
