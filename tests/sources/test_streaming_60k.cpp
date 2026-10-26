// A 60,000-channel provider, streamed in 64 KiB chunks the way the HTTP
// layer hands bodies over. Checks that the readers keep one element (or one
// line) at a time, whatever the body size, and reports how long it took.
#include <algorithm>
#include <chrono>
#include <functional>
#include <set>
#include <string>

#include "core/sources/m3u.hpp"
#include "core/sources/xtream.hpp"
#include "doctest.h"
#include "sources/mock_provider.hpp"

using namespace marigold;
using namespace marigold::sources;

namespace {
constexpr size_t kChannels = 60000;
constexpr size_t kChunk = 64 * 1024;

double msSince(std::chrono::steady_clock::time_point start) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}  // namespace

TEST_CASE("streaming: 60,000 Xtream channels in 64 KiB chunks with bounded memory") {
  mock::Provider provider(kChannels);
  xtream::CategoryNames categories = xtream::parseCategories(200, provider.categoriesJson());
  REQUIRE(categories.size() == provider.categories().size());

  size_t received = 0, radio = 0, bodyBytes = 0, chunks = 0, largestChunk = 0;
  uint64_t idChecksum = 0;
  bool idsClean = true;
  xtream::StreamsReader reader(200, "xt:acc1", categories, [&](Channel&& c) {
    // The callback keeps nothing: what the reader retains is all that is held.
    ++received;
    if (c.isRadio) ++radio;
    if (c.id.find('/') != std::string::npos || c.id.find("example") != std::string::npos) idsClean = false;
    idChecksum += std::hash<std::string>()(c.id);
    return true;
  });

  // Only the reader's own time is counted, not the mock generating the body.
  double ms = 0;
  bool fed = true;
  provider.liveStreamsChunks(kChunk, [&](std::string_view chunk) {
    bodyBytes += chunk.size();
    ++chunks;
    largestChunk = std::max(largestChunk, chunk.size());
    auto start = std::chrono::steady_clock::now();
    fed = reader.feed(chunk) && fed;
    ms += msSince(start);
  });
  auto start = std::chrono::steady_clock::now();
  std::optional<Error> error = reader.finish();
  ms += msSince(start);

  REQUIRE(fed);
  REQUIRE_FALSE(error);
  CHECK(received == provider.usableCount());
  CHECK(reader.channels() == provider.usableCount());
  CHECK(reader.skipped() == kChannels - provider.usableCount());
  CHECK(radio > 0);
  CHECK(idsClean);
  CHECK(idChecksum != 0);
  CHECK(largestChunk == kChunk);
  // One element at a time: the largest channel object built is well under
  // 2 KiB, and the token buffer never grew past the longest string.
  CHECK(reader.peakElementBytes() < 2048);
  CHECK(reader.peakTokenBytes() < 512);
  MESSAGE("Xtream get_live_streams: " << kChannels << " channels, " << bodyBytes / 1024 << " KiB in " << chunks
                                      << " chunks, parsed in " << static_cast<long>(ms) << " ms; peak element "
                                      << reader.peakElementBytes() << " B, peak token "
                                      << reader.peakTokenBytes() << " B");
}

TEST_CASE("streaming: 60,000 M3U entries in 64 KiB chunks") {
  mock::Provider provider(kChannels);
  size_t received = 0, maxStreamBytes = 0, bodyBytes = 0;
  std::set<std::string> ids;
  m3u::Parser parser("pl1", [&](Channel&& c, m3u::StreamInfo&& s) {
    ++received;
    ids.insert(c.itemId);  // ids only (8-byte hashes as text), to check uniqueness
    maxStreamBytes = std::max(maxStreamBytes, s.url.size());
    return true;
  });
  double ms = 0;
  provider.m3uChunks(kChunk, true, [&](std::string_view chunk) {
    bodyBytes += chunk.size();
    auto start = std::chrono::steady_clock::now();
    parser.feed(chunk);
    ms += msSince(start);
  });
  std::optional<Error> error = parser.finish();
  REQUIRE_FALSE(error);
  CHECK(received == kChannels);
  CHECK(ids.size() == kChannels);
  CHECK(parser.longLines() == 0);
  CHECK(maxStreamBytes < m3u::kMaxLineBytes);
  MESSAGE("M3U playlist: " << kChannels << " entries, " << bodyBytes / 1024 << " KiB, parsed in "
                           << static_cast<long>(ms) << " ms");
}
