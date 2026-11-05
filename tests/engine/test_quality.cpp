// What a feed's rows say of its picture (Build 136): measured, else the
// name's or group's claim, else nothing.
#include <doctest.h>

#include <string>

#include "core/engine/quality.hpp"

using namespace marigold::engine::quality;

TEST_CASE("quality: the picture of a playback log video line") {
  auto p = fromFacts("1920x1080 59.94p \xc2\xb7 h264 High 8-bit \xc2\xb7 aac");
  REQUIRE(p);
  CHECK(p->height == 1080);
  CHECK(p->fps == 60);
  CHECK_FALSE(p->interlaced);
  CHECK(text(*p) == "1080p \xc2\xb7 60");
  CHECK(text(*fromFacts("1280x720 50p \xc2\xb7 h264 Main 8-bit")) == "720p \xc2\xb7 50");
  CHECK(text(*fromFacts("1920x1088 29.97i \xc2\xb7 mpeg2")) == "1080i \xc2\xb7 30");
  CHECK(text(*fromFacts("3840x2160 50p \xc2\xb7 hevc Main10")) == "4K \xc2\xb7 50");
  // The player's own facts carry no frame rate.
  CHECK(text(*fromFacts("1280x720 \xc2\xb7 h264 \xc2\xb7 aac")) == "720p");
  CHECK(text(*fromFacts("720x576 25i")) == "576i \xc2\xb7 25");
  // No size: nothing measured; more than 4K is never shown.
  CHECK_FALSE(fromFacts("h264 High 8-bit \xc2\xb7 aac"));
  CHECK_FALSE(fromFacts(""));
  CHECK(text(*fromFacts("7680x4320 60p")).empty());
  CHECK(text(Picture{}).empty());
}

TEST_CASE("quality: what a name or its group claims") {
  CHECK(claimed("US: Dream Channel FHD") == "FHD");
  CHECK(claimed("US: Dream Channel 1080p") == "FHD");
  CHECK(claimed("US: Dream Channel UHD") == "4K");
  CHECK(claimed("UK| Sports Main 4K") == "4K");
  CHECK(claimed("US: Dream Channel SD") == "SD");
  CHECK(claimed("US: Dream Channel HD") == "HD");
  CHECK(claimed("US: Dream Channel 720") == "HD");
  // Unmarked: nothing (it used to read "HD").
  CHECK(claimed("US: Dream Channel").empty());
  CHECK(claimed("US: Dream Channel", "US| Entertainment").empty());
  // Superscript tags, stuck to the name or not.
  CHECK(claimed("UK| Sports Main \xe1\xb4\xb4\xe1\xb4\xb0") == "HD");                       // ᴴᴰ
  CHECK(claimed("UK| Sports Main\xe1\xb5\x81\xe1\xb4\xb4\xe1\xb4\xb0") == "4K");            // ᵁᴴᴰ
  CHECK(claimed("UK| Sports Main \xe1\xb6\xa0\xe1\xb4\xb4\xe1\xb4\xb0") == "FHD");          // ᶠᴴᴰ
  CHECK(claimed("UK| Sports Main \xe1\xb4\xb4\xe1\xb4\xb0 \xe2\x81\xb6\xe2\x81\xb0\xe1\xb6\xa0\xe1\xb5\x96\xcb\xa2") ==
        "HD \xc2\xb7 60");  // ᴴᴰ ⁶⁰ᶠᵖˢ
  // Frame rates: 50 and 60 only.
  CHECK(claimed("US: Sports Channel FHD 60fps") == "FHD \xc2\xb7 60");
  CHECK(claimed("US: Sports Channel 1080p50") == "FHD \xc2\xb7 50");
  CHECK(claimed("US: Sports Channel 50 FPS") == "50 fps");
  CHECK(claimed("US: Sports Channel 30fps").empty());
  // The group speaks when the name doesn't; the name wins.
  CHECK(claimed("US: Dream Channel", "US| Movies FHD") == "FHD");
  CHECK(claimed("US: Dream Channel SD", "US| Movies FHD") == "SD");
  CHECK(claimed("US: Dream Channel FHD", "US| Movies \xe2\x81\xb6\xe2\x81\xb0\xe1\xb6\xa0\xe1\xb5\x96\xcb\xa2") ==
        "FHD \xc2\xb7 60");
  // Never 8K, RAW or VIP, and not a state after a town.
  CHECK(claimed("LIVE | Team A vs Team B | 8K EXCLUSIVE | RAW").empty());
  CHECK(claimed("US: Dream Channel \xe1\xb4\xbf\xe1\xb4\xac\xe1\xb5\x82 \xe2\xb1\xbd\xe1\xb4\xb5\xe1\xb4\xbe").empty());
  CHECK(claimed("US: KAAA (Sioux Falls, SD)").empty());
}

TEST_CASE("quality: measured pictures are kept per channel, the newest 2000") {
  Memory m;
  CHECK(m.remember("xt:a|1", Picture{1080, 60, false}));
  CHECK_FALSE(m.remember("xt:a|1", Picture{1080, 60, false}));  // the same: nothing to save
  CHECK(m.remember("xt:a|1", Picture{720, 60, false}));
  CHECK(m.find("xt:a|1")->height == 720);
  CHECK_FALSE(m.find("xt:a|2"));
  // Nothing worth showing isn't kept.
  CHECK_FALSE(m.remember("xt:a|3", Picture{}));
  CHECK_FALSE(m.remember("", Picture{1080, 30, false}));

  for (int i = 0; i < 2100; ++i) m.remember("xt:b|" + std::to_string(i), Picture{1080, 30, false});
  CHECK(m.size() == Memory::kCap);
  CHECK_FALSE(m.find("xt:a|1"));     // the oldest went first
  CHECK_FALSE(m.find("xt:b|99"));
  CHECK(m.find("xt:b|100"));
  // Measured again: newest, so it stays when others come.
  m.remember("xt:b|100", Picture{1080, 60, false});
  m.remember("xt:c|1", Picture{1080, 60, false});
  CHECK(m.find("xt:b|100"));
  CHECK_FALSE(m.find("xt:b|101"));

  // Saved and read back the same, oldest first; ids and numbers only.
  std::string saved = m.toJson();
  CHECK(saved.find("://") == std::string::npos);
  Memory back = Memory::fromJson(saved);
  CHECK(back.size() == m.size());
  CHECK(back.find("xt:b|100")->fps == 60);
  CHECK(back.toJson() == saved);
  CHECK(Memory::fromJson("not json").size() == 0);
  CHECK(Memory::fromJson("{\"v\":2,\"pictures\":[[\"x\",1080,60,0]]}").size() == 0);
  CHECK(Memory::fromJson("{\"v\":1,\"pictures\":[[1080],[\"x\",720,0,1]]}").find("x")->interlaced);
}
