#include "core/catalog/types.hpp"

namespace marigold::catalog {

const char* rawValue(LineupCategory c) {
  switch (c) {
    case LineupCategory::local:
      return "local";
    case LineupCategory::news:
      return "news";
    case LineupCategory::sports:
      return "sports";
    case LineupCategory::entertainment:
      return "entertainment";
    case LineupCategory::movies:
      return "movies";
    case LineupCategory::kids:
      return "kids";
    case LineupCategory::music:
      return "music";
    case LineupCategory::spanish:
      return "spanish";
    case LineupCategory::free:
      return "free";
    case LineupCategory::international:
      return "international";
    case LineupCategory::radio:
      return "radio";
  }
  return "entertainment";
}

std::optional<LineupCategory> categoryFromRaw(std::string_view raw) {
  for (LineupCategory c : kAllCategories) {
    if (raw == rawValue(c)) return c;
  }
  return std::nullopt;
}

const char* title(LineupCategory c) {
  switch (c) {
    case LineupCategory::local:
      return "Local stations";
    case LineupCategory::news:
      return "News";
    case LineupCategory::sports:
      return "Sports networks";
    case LineupCategory::entertainment:
      return "Entertainment";
    case LineupCategory::movies:
      return "Movies & premium";
    case LineupCategory::kids:
      return "Kids & family";
    case LineupCategory::music:
      return "Music";
    case LineupCategory::spanish:
      return "Spanish-language";
    case LineupCategory::free:
      return "Free streaming channels";
    case LineupCategory::international:
      return "International";
    case LineupCategory::radio:
      return "Radio";
  }
  return "";
}

const char* detail(LineupCategory c) {
  switch (c) {
    case LineupCategory::local:
      return "ABC, CBS, NBC, FOX, PBS and other stations in your market";
    case LineupCategory::news:
      return "CNN, Fox News, MSNBC, CNBC, Weather and more";
    case LineupCategory::sports:
      return "ESPN, FS1, NFL Network, regional sports for your area";
    case LineupCategory::entertainment:
      return "Cable networks: USA, TNT, HGTV, Food, Discovery\xE2\x80\xA6";
    case LineupCategory::movies:
      return "HBO, Showtime, Starz, Cinemax, MGM+, TCM\xE2\x80\xA6";
    case LineupCategory::kids:
      return "Disney, Nickelodeon, Cartoon Network, PBS Kids\xE2\x80\xA6";
    case LineupCategory::music:
      return "MTV, CMT, BET Jams and music channels";
    case LineupCategory::spanish:
      return "Univision, Telemundo, ESPN Deportes and other US Spanish channels";
    case LineupCategory::free:
      return "Pluto TV, Roku, Samsung TV Plus, Plex, Tubi channels";
    case LineupCategory::international:
      return "Channels from other countries";
    case LineupCategory::radio:
      return "Audio-only stations";
  }
  return "";
}

int numberBlock(LineupCategory c) {
  switch (c) {
    case LineupCategory::local:
      return 1;
    case LineupCategory::news:
      return 100;
    case LineupCategory::sports:
      return 200;
    case LineupCategory::entertainment:
      return 300;
    case LineupCategory::movies:
      return 600;
    case LineupCategory::kids:
      return 700;
    case LineupCategory::music:
      return 750;
    case LineupCategory::spanish:
      return 800;
    case LineupCategory::free:
      return 1000;
    case LineupCategory::international:
      return 2000;
    case LineupCategory::radio:
      return 3000;
  }
  return 0;
}

const char* symbol(LineupCategory c) {
  switch (c) {
    case LineupCategory::local:
      return "antenna.radiowaves.left.and.right";
    case LineupCategory::news:
      return "newspaper";
    case LineupCategory::sports:
      return "sportscourt";
    case LineupCategory::entertainment:
      return "tv";
    case LineupCategory::movies:
      return "film";
    case LineupCategory::kids:
      return "teddybear";
    case LineupCategory::music:
      return "music.note";
    case LineupCategory::spanish:
      return "globe.americas";
    case LineupCategory::free:
      return "play.tv";
    case LineupCategory::international:
      return "globe";
    case LineupCategory::radio:
      return "radio";
  }
  return "";
}

const char* genreTitle(LineupCategory c) {
  switch (c) {
    case LineupCategory::local:
      return "Local";
    case LineupCategory::news:
      return "News";
    case LineupCategory::sports:
      return "Sports";
    case LineupCategory::entertainment:
      return "Entertainment";
    case LineupCategory::movies:
      return "Movies & Premium";
    case LineupCategory::kids:
      return "Kids & Family";
    case LineupCategory::music:
      return "Music";
    case LineupCategory::spanish:
      return "Spanish";
    case LineupCategory::free:
      return "Free Streaming";
    case LineupCategory::international:
      return "International";
    case LineupCategory::radio:
      return "Radio";
  }
  return "";
}

int rank(LineupCategory c) { return static_cast<int>(c); }

const std::set<LineupCategory>& defaultOnCategories() {
  static const std::set<LineupCategory> kOn = {
      LineupCategory::local,         LineupCategory::news,   LineupCategory::sports,
      LineupCategory::entertainment, LineupCategory::movies, LineupCategory::kids,
      LineupCategory::music};
  return kOn;
}

const char* rawValue(ChannelKind k) {
  switch (k) {
    case ChannelKind::channel:
      return "channel";
    case ChannelKind::event:
      return "event";
    case ChannelKind::placeholder:
      return "placeholder";
    case ChannelKind::separator:
      return "separator";
    case ChannelKind::loop247:
      return "loop247";
    case ChannelKind::fast:
      return "fast";
    case ChannelKind::radio:
      return "radio";
  }
  return "channel";
}

std::optional<ChannelKind> kindFromRaw(std::string_view raw) {
  for (ChannelKind k :
       {ChannelKind::channel, ChannelKind::event, ChannelKind::placeholder, ChannelKind::separator,
        ChannelKind::loop247, ChannelKind::fast, ChannelKind::radio}) {
    if (raw == rawValue(k)) return k;
  }
  return std::nullopt;
}

time::Seconds LiveEvent::expectedLength() const {
  const std::string& l = league;
  if (l == "NFL" || l == "College Football") return 3 * 3600 + 45 * 60;
  if (l == "MLB" || l == "MiLB") return 3 * 3600 + 15 * 60;
  if (l == "Soccer" || l == "NBA" || l == "WNBA" || l == "NHL" || l == "College Basketball" ||
      l == "MLS") {
    return 2 * 3600 + 30 * 60;
  }
  if (l == "Fighting") return 5 * 3600;
  if (l == "Motorsport" || l == "Golf") return 4 * 3600;
  return 2 * 3600 + 30 * 60;
}

bool LiveEvent::isLive(time::Seconds now) const {
  if (!start) return false;
  return *start <= now && now < *start + expectedLength();
}

}  // namespace marigold::catalog
