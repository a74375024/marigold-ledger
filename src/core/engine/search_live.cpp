#include "core/engine/search_live.hpp"

#include <chrono>

namespace marigold::engine::search {
namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

}  // namespace

bool sameQuery(const Query& a, const Query& b) {
  return a.text == b.text && a.tokens == b.tokens && a.when == b.when && a.exactOnly == b.exactOnly &&
         a.digits == b.digits && a.regional == b.regional;
}

Query parseFor(const std::shared_ptr<const Index>& index, const std::string& text, bool exactOnly) {
  std::shared_ptr<const Index> keep = index;
  Query q = parse(text, [keep](const std::string& f) { return keep && keep->isTitle(f); });
  q.exactOnly = exactOnly;
  return q;
}

Live::Pass Live::run(const std::shared_ptr<const Index>& index, const Query& q, Options o, bool shows, bool keep,
                     uint32_t ms) {
  Pass pass;
  bool newIndex = index != index_;
  index_ = index;
  if (!index_) {
    results_ = Results{};
    return pass;
  }
  // Another index's shows: looked for again.
  if (showsIndex_ && showsIndex_->showTitles.get() != index_->showTitles.get()) {
    showsIndex_.reset();
    shows_.reset();
  }
  if (!shows && q.length() >= 2 && !showsAt_) showsAt_ = ms + kShowsDelayMs;
  else if (shows) showsAt_ = 0;
  o.shows = shows || q.length() < 2;
  if (!keep || newIndex || !find_ || !sameQuery(find_->query, q) || find_->options.scoped != o.scoped) {
    Clock::time_point t = Clock::now();
    find_ = find(*index_, q, o);
    pass.found = true;
    pass.foundMs = msSince(t);
  }
  FoundShows none;
  const FoundShows* found = &none;
  if (o.shows && q.length() >= 2) {
    if (!shows_ || !showsIndex_ || !sameQuery(showsQuery_, q) || showsScoped_ != o.scoped) {
      Clock::time_point t = Clock::now();
      shows_ = findShows(*index_, q, o);
      showsIndex_ = index_;
      showsQuery_ = q;
      showsScoped_ = o.scoped;
      pass.shows = true;
      pass.showsMs = msSince(t);
    }
    found = &*shows_;
  }
  results_ = merge(*index_, *find_, *found);
  showsIn_ = o.shows;
  return pass;
}

}  // namespace marigold::engine::search
