// Build 153 benchmark, skipped by default (run with --no-skip
// -tc="bench: guide choice*" on a Release build): a row's listings by
// channel identity against the old per-row pick, for 600 rows of
// reference channels with realistic candidates (two providers' guides, an
// outside listing, a listing kept apart, a wrong guide id on every fifth
// row, a West feed on every fourth). 48 hours of hourly programmes each.
// Prints the time of each lap and the bytes a store keeps per weighed row.
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "core/catalog/reference.hpp"
#include "core/catalog/work.hpp"
#include "core/guide/choice.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::guide;

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

int64_t heapNow() {
#if defined(__GLIBC__)
  struct mallinfo2 m = mallinfo2();
  return static_cast<int64_t>(m.uordblks + m.hblkhd);
#else
  return 0;
#endif
}

Schedule schedule(int seed, int from, int shift = 0) {
  std::vector<RawProgram> list;
  for (int i = 0; i < 51; ++i) {
    RawProgram p;
    p.s = from + i * 60 + shift;
    p.m = 60;
    p.t = "Show" + std::to_string(seed) + "x" + std::to_string(i);
    if (i % 2) p.d = Text("About " + p.t);
    list.push_back(std::move(p));
  }
  return std::make_shared<const std::vector<RawProgram>>(std::move(list));
}

std::string squash(const std::string& s) {
  std::string out;
  for (char c : s)
    if (std::isalnum(static_cast<unsigned char>(c))) out += c;
  return out;
}

}  // namespace

TEST_CASE("bench: guide choice for 600 rows" * doctest::skip()) {
  const auto& data = catalog::reference::builtIn();
  REQUIRE(data.channels.size() >= 600);
  const time::Seconds nowSec = time::now() / 1800 * 1800;
  const int now = static_cast<int>(nowSec / 60);
  struct RowData {
    std::string id;
    std::vector<choice::MemberGuide> members;
  };
  std::vector<RowData> rows;
  guide::SharedPrograms provider, outside;
  std::set<std::string> copyIds;
  std::vector<std::vector<std::string>> copiesOf;
  for (int r = 0; r < 600; ++r) {
    const catalog::reference::Channel& ch = data.channels[static_cast<size_t>(r)];
    bool west = r % 4 == 3;
    RowData row;
    row.id = "e|r:" + ch.id + (west ? "~west" : "");
    Schedule s = schedule(r, now - 180);
    Schedule same = schedule(r, now - 180);  // another provider's copy of it
    Schedule outsideList = schedule(r, now - 180);
    std::vector<std::string> copies;
    auto copy = [&](const std::string& server, int n, const std::string& epg, Schedule list, const std::string& name) {
      choice::MemberGuide m;
      m.id = server + "|" + std::to_string(r) + "-" + std::to_string(n);
      m.serverId = server;
      m.xtream = true;
      m.epgId = epg;
      m.provider = std::move(list);
      m.providerNote = "Provider guide: " + name + " (by guide id)";
      copies.push_back(m.id);
      provider[m.id] = m.provider;
      return m;
    };
    std::string upper = ch.name;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    choice::MemberGuide a = copy("xt:1", 0, squash(ch.name) + ".us", s, "US: " + upper + (west ? " WEST" : "") + " HD");
    a.outside = west ? schedule(r, now - 180) : outsideList;  // East listing for a West row
    a.outsideNote = "US national: " + ch.name + (west ? " East" : "") + " HD (by name)";
    if (r % 3 == 0) a.others.emplace_back(schedule(r + 1000, now - 180), "US sports: " + ch.name + " (by name)");
    outside[a.id] = a.outside;
    row.members.push_back(a);
    row.members.push_back(copy("xt:1", 1, squash(ch.name) + ".us", s, "US: " + upper + " HD"));
    row.members.push_back(copy("xt:2", 2, squash(ch.name) + "hd.us", same, "|US| " + upper));
    if (r % 5 == 0) row.members.push_back(copy("xt:1", 3, "WCBS.us", schedule(r + 2000, now - 180), "US: " + upper));
    rows.push_back(std::move(row));
    copiesOf.push_back(copies);
    copyIds.insert(copies.begin(), copies.end());
  }

  // Before: every copy's merged listing, then each row's best copy.
  auto t0 = Clock::now();
  catalog::ProgramLists merged =
      catalog::mergedListings(copyIds, std::unordered_map<std::string, std::vector<Program>>(), provider, outside, nowSec);
  double mergeMs = msSince(t0);
  auto t1 = Clock::now();
  int picked = 0;
  for (const auto& copies : copiesOf) {
    std::vector<const std::vector<Program>*> lists;
    for (const std::string& c : copies) {
      auto it = merged.find(c);
      if (it != merged.end()) lists.push_back(it->second.get());
    }
    picked += catalog::bestGuide(lists, nowSec) >= 0 ? 1 : 0;
  }
  double pickMs = msSince(t1);

  // The store's gathering on the UI thread: each row's copies (list
  // pointers and notes), as a copy of what was made above.
  auto tg = Clock::now();
  std::vector<RowData> gathered = rows;
  double gatherMs = msSince(tg);
  gathered.clear();

  // After: candidates, the decision and the row's listing (cold: names
  // read through the reference for the first time; then warm).
  size_t kept = 0, rejected = 0, shifted = 0;
  double coldMs = 0, warmMs = 0, listingMs = 0;
  for (int pass = 0; pass < 2; ++pass) {
    auto t2 = Clock::now();
    double lists = 0;
    for (const RowData& row : rows) {
      std::vector<choice::Candidate> cands = choice::candidates(row.members);
      choice::Decision d = choice::decide(choice::rowOf(row.id, std::string("US")), cands, now, nowSec);
      auto t3 = Clock::now();
      std::vector<Program> list = choice::listing(d, cands, nowSec);
      lists += msSince(t3);
      if (pass == 1) {
        kept += d.options.size();
        rejected += d.rejected.empty() ? 0 : 1;
        shifted += d.shift != 0 ? 1 : 0;
        CHECK_FALSE(list.empty());
      }
    }
    (pass == 0 ? coldMs : warmMs) = msSince(t2);
    if (pass == 1) listingMs = lists;
  }
  // What a store keeps per weighed row: its options (key, two list
  // pointers, the channel id and a shift) and the two report lines.
  int64_t before = heapNow();
  std::vector<std::pair<std::string, std::string>> lines;
  // As Store::Impl::RowGuide::Option.
  struct Slim {
    std::string key, channelId;
    Schedule raw;
    std::shared_ptr<const std::vector<Program>> extra;
    int shift = 0;
    bool outside = false;
  };
  std::vector<std::vector<Slim>> slim;
  for (const RowData& row : rows) {
    std::vector<choice::Candidate> cands = choice::candidates(row.members);
    choice::Decision d = choice::decide(choice::rowOf(row.id, std::string("US")), cands, now, nowSec);
    std::vector<Slim> mine;
    for (const choice::Option& o : d.options) {
      Slim c;
      c.key = cands[o.index].key;
      c.channelId = cands[o.index].channelId;
      c.raw = cands[o.index].raw;
      c.shift = o.shift;
      mine.push_back(std::move(c));
    }
    slim.push_back(std::move(mine));
    lines.emplace_back(d.why, d.rejected);
  }
  int64_t bytes = heapNow() - before;
  MESSAGE("600 rows, " << copyIds.size() << " copies: gathering " << gatherMs << " ms; before " << mergeMs << " ms merging copies + " << pickMs
                       << " ms picking (" << picked << " rows); after " << coldMs << " ms cold / " << warmMs
                       << " ms warm (of which " << listingMs << " ms building listings); " << kept << " options, "
                       << rejected << " rows set something aside, " << shifted << " shifted; kept per row "
                       << (bytes / 600) << " bytes (" << bytes / 1024 << " KiB for 600)");
}
