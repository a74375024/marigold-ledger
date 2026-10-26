#include "core/util/memory.hpp"
#include "doctest.h"

using namespace marigold;

TEST_CASE("memory: a reading of this process, and how it reads") {
  memory::Reading r = memory::read();
  CHECK(r.rssKB > 0);
  CHECK(r.peakKB >= r.rssKB);
  CHECK(memory::describe({212 * 1024, 340 * 1024}) == "212 MB (peak 340 MB)");
  CHECK(memory::describe({1536, 0}) == "2 MB");
  CHECK(memory::describe({}).empty());
  memory::configure();
  memory::release();  // never fails
}
