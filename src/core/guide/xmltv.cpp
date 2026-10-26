#include "core/guide/xmltv.hpp"

#include <algorithm>
#include <cstdint>

#include "core/guide/genre.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide {
namespace {

std::optional<std::string> clean(const std::string& value) {
  std::string_view trimmed = str::trimUnicode(value, true);
  if (trimmed.empty()) return std::nullopt;
  return std::string(trimmed);
}

// The first `n` code points of `s`, and whether there were more.
std::string prefixCodePoints(std::string_view s, size_t n, bool& cut) {
  size_t i = 0, count = 0;
  while (i < s.size() && count < n) {
    ++i;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    ++count;
  }
  cut = i < s.size();
  return std::string(s.substr(0, i));
}

}  // namespace

XmltvReader::XmltvReader(
    std::shared_ptr<const Targets> targets, bool locals, Window window,
    std::optional<std::unordered_map<std::string, std::vector<std::string>>> exact,
    bool stationNames)
    : targets_(targets ? std::move(targets) : std::make_shared<const Targets>()),
      locals_(locals),
      window_(window),
      exact_(std::move(exact)),
      stationNames_(stationNames) {}

XmltvReader::XmltvReader(std::shared_ptr<const Targets> targets, bool locals, Window window,
                         bool stationNames)
    : XmltvReader(std::move(targets), locals, window, std::nullopt, stationNames) {}

XmltvReader XmltvReader::forSource(const Source& source, std::shared_ptr<const Targets> targets,
                                   Window window) {
  return XmltvReader(std::move(targets), source.locals, window, std::nullopt, source.stationNames);
}

XmltvReader XmltvReader::exact(std::unordered_map<std::string, std::vector<std::string>> ids,
                               Window window) {
  return XmltvReader(nullptr, false, window, std::move(ids), false);
}

void XmltvReader::feed(std::string_view chunk) { tokenizer_.feed(chunk, *this); }

void XmltvReader::finish() {
  if (finished_) return;
  finished_ = true;
  tokenizer_.finish(*this);
  settleKeepList();
}

void XmltvReader::settleKeepList() {
  if (keep_) return;
  std::map<std::string, std::vector<Match>> list;
  if (exact_) {
    for (const auto& [id, users] : *exact_) {
      std::vector<Match>& matches = list[id];
      for (const std::string& user : users) matches.push_back({user, "by guide id", 0});
    }
    for (const auto& [id, names] : channelNames_)
      keepNames_[id] = names.empty() ? id : names.front();
  } else {
    for (const std::string& id : channelOrder_) {
      const std::vector<std::string>& names = channelNames_[id];
      std::vector<Match> matches =
          targets_->matches(names, id, locals_, stationNames_, /*deriveWest=*/true);
      if (!matches.empty()) {
        list[id] = std::move(matches);
        keepNames_[id] = names.empty() ? id : names.front();
      }
    }
  }
  keep_ = std::move(list);
  channelNames_.clear();
  channelOrder_.clear();
}

void XmltvReader::startElement(std::string_view element, const xml::Attributes& attributes) {
  if (element == "channel") {
    guideChannels_ += 1;
    const std::string* id = attributes.find("id");
    currentChannel_ = id ? std::optional<std::string>(*id) : std::nullopt;
    if (exact_ && currentChannel_) {
      // Only the names of channels the user has are worth keeping.
      std::string key = str::lower(*currentChannel_);
      currentChannel_ = exact_->count(key) ? std::optional<std::string>(key) : std::nullopt;
    }
    // Names are only read until the keep list is settled (Swift kept
    // collecting them after that but never read them again).
    if (keep_) currentChannel_.reset();
    if (currentChannel_) {
      auto [it, inserted] = channelNames_.try_emplace(*currentChannel_);
      if (inserted) channelOrder_.push_back(*currentChannel_);
      it->second.clear();
    }
  } else if (element == "display-name") {
    collecting_ = currentChannel_.has_value();
    text_.clear();
  } else if (element == "programme") {
    settleKeepList();
    counter_ += 1;
    programmes_ += 1;
    if (counter_ % 4000 == 0 && pause_) pause_();
    const std::string* rawChannel = attributes.find("channel");
    std::string id = rawChannel ? *rawChannel : std::string();
    if (exact_) id = str::lower(id);
    const std::string* startText = attributes.find("start");
    const std::string* stopText = attributes.find("stop");
    int start = 0, end = 0;
    bool timed = false;
    if (startText && stopText) {
      std::optional<int> s = minutes(*startText), e = minutes(*stopText);
      timed = s.has_value() && e.has_value();
      start = s.value_or(0);
      end = e.value_or(0);
    }
    if (!keep_->count(id) || !timed || !(end > start) || !(end > window_.low) ||
        !(start < window_.high)) {
      programmeChannel_.reset();
      return;
    }
    programmeChannel_ = std::move(id);
    programmeStart_ = start;
    programmeEnd_ = end;
    title_.reset();
    subtitle_.reset();
    desc_.reset();
    categories_.clear();
    flags_ = 0;
  } else if (element == "title" || element == "sub-title" || element == "desc" ||
             element == "category") {
    collecting_ = programmeChannel_.has_value();
    text_.clear();
  } else if (element == "new" || element == "premiere") {
    if (programmeChannel_) flags_ |= 32;
  } else if (element == "live") {
    if (programmeChannel_) flags_ |= 16;
  }
}

void XmltvReader::characters(std::string_view text) {
  if (!collecting_ || text_.size() >= kMaxText) return;
  text_.append(text.substr(0, kMaxText - text_.size()));
}

void XmltvReader::endElement(std::string_view element) {
  if (element == "display-name") {
    if (currentChannel_) {
      std::string_view value = str::trimUnicode(text_, true);
      if (!value.empty()) channelNames_[*currentChannel_].emplace_back(value);
    }
  } else if (element == "channel") {
    currentChannel_.reset();
  } else if (element == "title") {
    if (programmeChannel_ && !title_) title_ = clean(text_);
  } else if (element == "sub-title") {
    if (programmeChannel_) subtitle_ = clean(text_);
  } else if (element == "desc") {
    // Read here; kept at the end of the programme only for what's on soon
    // or for sports (whose description names the venue, how Live Events
    // finds which game a station has). Descriptions are most of the size.
    if (programmeChannel_) {
      if (auto value = clean(text_)) {
        bool longer = false, ignored = false;
        prefixCodePoints(*value, kMaxDescription, longer);
        if (longer) {
          desc_ = prefixCodePoints(*value, kMaxDescription - 1, ignored) + "\xE2\x80\xA6";
        } else {
          desc_ = std::move(*value);
        }
      }
    }
  } else if (element == "category") {
    if (programmeChannel_) {
      if (auto value = clean(text_)) categories_.push_back(str::lower(*value));
    }
  } else if (element == "programme") {
    if (programmeChannel_ && title_) {
      int f = flags_ | genre::flags(str::join(categories_, " "));
      if ((f & genre::kGenreBits) == 0) f |= genre::flagsForTitle(*title_, subtitle_);
      bool keepDesc = programmeStart_ < window_.low + 180 + 8 * 60 || (f & genre::kSports) != 0;
      RawProgram p;
      p.s = programmeStart_;
      p.m = std::max(1, programmeEnd_ - programmeStart_);
      p.t = *title_;
      p.st = subtitle_;
      if (keepDesc) p.d = desc_;
      p.f = f;
      found_[*programmeChannel_].push_back(std::move(p));
    }
    programmeChannel_.reset();
  }
  collecting_ = false;
}

std::map<std::string, SourceResult::Entry> XmltvReader::result() {
  std::map<std::string, SourceResult::Entry> byUser;
  if (!keep_) return byUser;
  auto nameOf = [&](const std::string& id) -> const std::string& {
    auto it = keepNames_.find(id);
    return it == keepNames_.end() ? id : it->second;
  };
  auto isLowPower = [&](const std::string& id) {
    auto it = keepNames_.find(id);
    std::string text = str::upper((it == keepNames_.end() ? std::string() : it->second) + " " + id);
    return rx::test("-(LD|LP|CD|CA)\\b", text);
  };
  // Pass 1: winners (same scoring as before), without copying anything.
  struct Win { const std::string* xmltv; int shift; std::string how; int score; };
  std::unordered_map<std::string, Win> best;
  for (const auto& [xmltvID, users] : *keep_) {
    auto programs = found_.find(xmltvID);
    if (programs == found_.end() || programs->second.empty()) continue;
    int base = static_cast<int>(programs->second.size()) + (isLowPower(xmltvID) ? 0 : 100000);
    for (const Match& user : users) {
      int score = base + (user.shift == 0 ? 1000000 : 0);
      auto it = best.find(user.id);
      if (it != best.end() && score <= it->second.score) continue;
      best[user.id] = Win{&xmltvID, user.shift, user.how, score};
    }
  }
  // Pass 2: each used guide channel becomes one schedule (moved, not
  // copied); a shifted feed gets one shifted copy per (channel, shift).
  std::map<std::pair<const std::string*, int>, Schedule> made;
  for (auto& [userID, win] : best) {
    Schedule& sched = made[{win.xmltv, 0}];
    if (!sched) {
      auto& list = found_[*win.xmltv];
      list.shrink_to_fit();
      sched = std::make_shared<const std::vector<RawProgram>>(std::move(list));
    }
    Schedule use = sched;
    if (win.shift != 0) {
      Schedule& shifted = made[{win.xmltv, win.shift}];
      if (!shifted) {
        std::vector<RawProgram> copy = *sched;
        for (RawProgram& p : copy) p.s += win.shift;
        shifted = std::make_shared<const std::vector<RawProgram>>(std::move(copy));
      }
      use = shifted;
    }
    SourceResult::Entry entry;
    entry.programs = std::move(use);
    entry.note = nameOf(*win.xmltv) + " (" + win.how + ")";
    byUser[userID] = std::move(entry);
  }
  found_.clear();  // losers go now
  return byUser;
}

SourceResult XmltvReader::sourceResult() {
  SourceResult out;
  out.programs = result();
  out.guideChannels = guideChannels_;
  out.programmes = programmes_;
  std::vector<int> ends;
  for (const auto& [id, entry] : out.programs) {
    if (!entry.programs || entry.programs->empty()) continue;
    int end = entry.programs->front().end();
    for (const RawProgram& p : *entry.programs) end = std::max(end, p.end());
    ends.push_back(end);
  }
  std::sort(ends.begin(), ends.end());
  if (!ends.empty()) out.horizonMedian = ends[ends.size() / 2];
  return out;
}

std::optional<int> XmltvReader::minutes(std::string_view raw) {
  if (raw.size() < 12) return std::nullopt;
  auto number = [&](size_t from, size_t count) -> std::optional<int> {
    int value = 0;
    for (size_t i = from; i < from + count; ++i) {
      if (i >= raw.size() || raw[i] < '0' || raw[i] > '9') return std::nullopt;
      value = value * 10 + (raw[i] - '0');
    }
    return value;
  };
  auto year = number(0, 4), month = number(4, 2), day = number(6, 2), hour = number(8, 2),
       minute = number(10, 2);
  if (!year || !month || !day || !hour || !minute) return std::nullopt;
  // Days from civil (Howard Hinnant's algorithm), as in Swift.
  int64_t y = *month <= 2 ? *year - 1 : *year;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t mp = (*month + 9) % 12;
  int64_t doy = (153 * mp + 2) / 5 + *day - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t days = era * 146097 + doe - 719468;
  int64_t total = days * 1440 + *hour * 60 + *minute;
  // Offset like "+0200" / "-0500": the last sign after the digits.
  size_t plus = raw.find_last_of("+-");
  if (plus != std::string_view::npos && plus > 12 && raw.size() >= plus + 5) {
    auto oh = number(plus + 1, 2), om = number(plus + 3, 2);
    if (oh && om) {
      int offset = *oh * 60 + *om;
      total += raw[plus] == '+' ? -offset : offset;
    }
  }
  if (total < INT32_MIN || total > INT32_MAX) return std::nullopt;
  return static_cast<int>(total);
}

}  // namespace marigold::guide
