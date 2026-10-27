// Building the catalog for 60,000 varied synthetic channels.
#include <chrono>
#include <cstdint>
#include <unordered_set>

#include "catalog/helpers.hpp"
#include "core/catalog/admission.hpp"
#include "core/catalog/catalog.hpp"
#include "core/catalog/work.hpp"
#include "doctest.h"

using namespace marigold;
using namespace marigold::catalog;

// GCC says it is sanitizing with __SANITIZE_*__, clang with __has_feature.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define MARIGOLD_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define MARIGOLD_SANITIZED 1
#endif
#endif
#ifndef MARIGOLD_SANITIZED
#define MARIGOLD_SANITIZED 0
#endif

namespace {

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  uint32_t next() {
    s = s * 6364136223846793005ull + 1442695040888963407ull;
    return static_cast<uint32_t>(s >> 33);
  }
  size_t pick(size_t n) { return next() % n; }
};

const char* const kWords[] = {
    "Alpha",   "Bravo",   "Cedar", "Delta",  "Ember",  "Falcon", "Granite", "Harbor", "Indigo",
    "Juniper", "Kestrel", "Lumen", "Meadow", "Nimbus", "Orchid", "Prairie", "Quartz", "Raven",
    "Sable",   "Tundra",  "Umber", "Vale",   "Willow", "Xenon",  "Yonder",  "Zephyr"};
const char* const kKinds[] = {"Channel", "Network", "TV",    "Plus", "Movies",   "News",
                              "Kids",    "Sports",  "Music", "Life", "Classics", "Cinema"};
const char* const kPrefixes[] = {
    "US: ", "US| ", "USA ", "(US) (A) ", "AT&T: ", "", "UK| ", "CA: ", "MX| ", "(Pluto) "};
const char* const kQualities[] = {"", " HD", " FHD", " 4K", " SD", " ᴿᴬᵂ", " (RK)", " West"};
const char* const kNetworks[] = {"NBC", "CBS", "ABC", "FOX", "CW", "PBS"};

std::vector<Channel> synthetic(size_t count) {
  Rng rng;
  std::vector<Channel> list;
  list.reserve(count);
  const auto& cores = coreChannels();
  for (size_t i = 0; i < count; ++i) {
    std::string id = std::to_string(i);
    std::string name;
    std::optional<std::string> group;
    std::string server = i % 7 == 0 ? "jf:2" : "xt:1";
    size_t kind = rng.pick(100);
    if (kind < 15) {
      // Core networks in every spelling.
      name = std::string(kPrefixes[rng.pick(4)]) + cores[rng.pick(cores.size())].aliases[0] +
             kQualities[rng.pick(8)];
      group = "US| Entertainment " + std::to_string(rng.pick(20));
    } else if (kind < 25) {
      // Locals with call signs.
      std::string sign = std::string(1, "KW"[rng.pick(2)]);
      for (int k = 0; k < 3; ++k) sign += static_cast<char>('A' + rng.pick(26));
      name = std::string("US: ") + kNetworks[rng.pick(6)] + " " + std::to_string(2 + rng.pick(60)) +
             " (" + sign + ")";
      group = "US| Locals " + std::to_string(rng.pick(30));
    } else if (kind < 40) {
      // Numbered event families.
      static const char* const kFamilies[] = {"ESPN+", "PPV", "NCAAF", "DAZN", "NBA PASS"};
      size_t f = rng.pick(5);
      std::string slot = std::to_string(1 + rng.pick(150));
      if (slot.size() < 2) slot = "0" + slot;
      name = std::string(kFamilies[f]) + " " + slot;
      if (rng.pick(3) != 0) {
        name += ": Team " + std::string(kWords[rng.pick(26)]) + " vs Team " + kWords[rng.pick(26)] +
                " " + std::to_string(1 + rng.pick(11)) + ":30 PM ET";
      }
      group = std::string("US| ") + kFamilies[f];
    } else if (kind < 45) {
      name = "Team " + std::string(kWords[rng.pick(26)]) + " vs Team " + kWords[rng.pick(26)] +
             " " + std::to_string(1 + rng.pick(11)) + "PM";
      group = "PPV Events " + std::to_string(rng.pick(5));
    } else if (kind < 50) {
      name = std::string(kWords[rng.pick(26)]) + " Classics 24/7";
      group = "24/7 Channels";
    } else if (kind < 52) {
      name = rng.pick(2) ? "#### " + std::string(kWords[rng.pick(26)]) + " ####"
                         : "NO EVENT STREAMING";
      group = "US| Misc";
    } else {
      // Ordinary channels: many distinct clusters, several variants each.
      name = std::string(kPrefixes[rng.pick(10)]) + kWords[rng.pick(26)] + " " +
             kWords[rng.pick(26)] + " " + kKinds[rng.pick(12)] + kQualities[rng.pick(8)];
      group = std::string(name.substr(0, 2) == "UK" ? "UK| " : "US| ") + kKinds[rng.pick(12)];
    }
    if (rng.pick(10) == 0) group.reset();
    list.push_back(testdata::channel(id, name, group, server));
  }
  return list;
}

}  // namespace

TEST_CASE("catalog for 60,000 synthetic channels") {
  std::vector<Channel> raw = synthetic(60000);
  auto started = std::chrono::steady_clock::now();
  std::vector<Channel> channels = annotated(raw);
  CatalogResult r =
      build(channels, testdata::wednesdayNoon(),
            {{"KABC", "ABC", {"springfield"}, "7"}, {"WXYZ", "NBC", {"capital"}, "4"}});
  double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  MESSAGE("catalog build for 60000 channels: " << seconds << " s, " << r.entries.size()
                                               << " entries, " << r.families.size() << " families, "
                                               << r.events.size() << " events");
  CHECK(r.traits.size() == channels.size());
  CHECK(r.entries.size() > 1000);
  CHECK(r.entries.size() < channels.size());
  CHECK(r.families.size() >= 5);
  CHECK(r.events.size() > 50);
  // Every channel is in exactly one place: an entry, a family, or hidden.
  std::unordered_set<std::string> placed;
  size_t members = 0, feeds = 0;
  for (const EntryInfo& e : r.entries) {
    members += e.members.size();
    placed.insert(e.members.begin(), e.members.end());
  }
  for (const EventFamily& f : r.families) {
    feeds += f.feeds.size();
    placed.insert(f.feeds.begin(), f.feeds.end());
  }
  CHECK(placed.size() == members + feeds);
  size_t hidden = 0;
  for (const auto& [id, t] : r.traits) {
    if (!t.familyKey && (t.kind == ChannelKind::placeholder || t.kind == ChannelKind::separator)) {
      ++hidden;
    }
  }
  CHECK(members + feeds + hidden == channels.size());
  // A core network collects its variants into one entry.
  size_t cnn = 0;
  for (const EntryInfo& e : r.entries) cnn += e.id == "e|core:CNN" ? 1 : 0;
  CHECK(cnn == 1);
#if !MARIGOLD_SANITIZED
  // About 2 s in an optimized desktop build; sanitizers make it 15-40x slower.
  CHECK(seconds < 30);
#endif

  Admission admitted = admitGroups(channels);
  CHECK(admitted.channels.size() <= kMaxChannels);
  CHECK(admitted.cutGroups > 0);
}
