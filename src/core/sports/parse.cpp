#include "core/sports/parse.hpp"

#include <algorithm>
#include <set>

#include "core/sports/text.hpp"
#include "core/util/json_stream.hpp"

namespace marigold::sports::parse {

bool forEachEvent(std::string_view body, const std::function<void(const json::Value&)>& fn) {
  json::ArrayLimits limits;
  limits.maxElementBytes = kMaxEventBytes;
  json::ArrayReader reader(
      [&](json::Value&& element) {
        fn(element);
        return true;
      },
      {"events"}, limits);
  if (!reader.feed(body)) return false;
  if (!reader.finish()) return false;
  return reader.foundArray();
}

namespace {

// Captures scalars of the root's "week" and "season" objects.
class TopFieldsHandler : public json::StreamHandler {
 public:
  TopFields fields;

  bool onNull() override { return scalar(std::nullopt); }
  bool onBool(bool) override { return scalar(std::nullopt); }
  bool onNumber(double value) override { return scalar(text::text(json::Value(value))); }
  bool onString(std::string_view value) override { return scalar(text::text(json::Value(value))); }
  bool onKey(std::string_view key) override {
    key_.assign(key);
    return true;
  }
  bool onStartObject() override { return open(true); }
  bool onStartArray() override { return open(false); }
  bool onEndObject() override { return close(); }
  bool onEndArray() override { return close(); }

 private:
  struct Frame {
    bool object;
    std::string key;
  };
  bool open(bool object) {
    stack_.push_back({object, key_});
    key_.clear();
    return true;
  }
  bool close() {
    if (!stack_.empty()) stack_.pop_back();
    key_.clear();
    return true;
  }
  bool scalar(std::optional<std::string> value) {
    if (stack_.size() == 2 && stack_[0].object && stack_[1].object) {
      if (stack_[1].key == "week" && key_ == "number") fields.weekNumber = value;
      if (stack_[1].key == "season" && key_ == "type") fields.seasonType = value;
    }
    key_.clear();
    return true;
  }
  std::vector<Frame> stack_;
  std::string key_;
};

}  // namespace

TopFields topFields(std::string_view body) {
  TopFieldsHandler handler;
  json::StreamReader reader(handler);
  reader.feed(body);
  reader.finish();
  return handler.fields;
}

std::optional<std::string> countryFromFlag(const std::optional<std::string>& href) {
  if (!href) return std::nullopt;
  std::vector<std::string> path = text::components(*href, "/");
  std::string file = text::components(path.back(), ".").front();
  size_t n = text::count(file);
  if (n < 2 || n > 3 || !text::isAllLetters(file)) return std::nullopt;
  return str::upper(file);
}

std::optional<SportsTeam> team(const json::Value& item) {
  if (!item.isObject()) return std::nullopt;
  auto id = text::text(item["id"]);
  auto name = text::text(item["displayName"]);
  if (!name) name = text::text(item["name"]);
  if (!id || !name) return std::nullopt;
  SportsTeam t;
  t.id = *id;
  t.name = *name;
  for (const char* key : {"shortDisplayName", "name"}) {
    auto value = text::text(item[key]);
    if (value && *value != *name &&
        std::find(t.alternates.begin(), t.alternates.end(), *value) == t.alternates.end()) {
      t.alternates.push_back(*value);
    }
  }
  t.badge = text::text(item["logo"]);
  if (!t.badge && item["logos"].isArray()) t.badge = text::text(item["logos"][size_t{0}]["href"]);
  t.shortName = text::text(item["abbreviation"]);
  t.location = text::text(item["location"]);
  t.color = hexColor(text::text(item["color"]));
  t.altColor = hexColor(text::text(item["alternateColor"]));
  return t;
}

std::optional<int> pollRank(const std::optional<std::string>& value) {
  if (!value) return std::nullopt;
  auto number = text::toInt(*value);
  if (!number || *number < 1 || *number > 25) return std::nullopt;
  return number;
}

std::optional<std::string> hexColor(const std::optional<std::string>& value) {
  if (!value) return std::nullopt;
  std::string trimmed = str::startsWith(*value, "#") ? value->substr(1) : *value;
  if (trimmed.size() != 6) return std::nullopt;
  for (char c : trimmed) {
    bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (!hex) return std::nullopt;
  }
  return str::upperAscii(trimmed);
}

std::optional<SportsTeam> fighter(const json::Value& competitor) {
  const json::Value& athlete = competitor["athlete"].isObject() ? competitor["athlete"] : competitor["team"];
  if (!athlete.isObject()) return std::nullopt;
  auto name = text::text(athlete["displayName"]);
  if (!name) name = text::text(athlete["fullName"]);
  if (!name) name = text::text(athlete["name"]);
  if (!name) return std::nullopt;
  auto id = text::text(athlete["id"]);
  if (!id) id = text::text(competitor["id"]);
  if (!id) id = name;
  auto flag = text::text(athlete["flag"]["href"]);
  auto headshot = text::text(athlete["headshot"]["href"]);
  SportsTeam t;
  t.id = "f" + *id;
  t.name = *name;
  if (auto shortName = text::text(athlete["shortName"]); shortName && *shortName != *name) {
    t.alternates.push_back(*shortName);
  }
  t.badge = headshot ? headshot : flag;
  t.country = countryFromFlag(flag);
  return t;
}

std::vector<std::string> networks(const json::Value& competition) {
  std::vector<std::string> national, other;
  for (const json::Value& item : competition["broadcasts"].items()) {
    if (!item.isObject()) continue;
    std::vector<std::string> names;
    for (const json::Value& n : item["names"].items()) {
      if (auto value = text::text(n)) names.push_back(*value);
    }
    auto market = text::text(item["market"]);
    auto& into = market && str::lower(*market) == "national" ? national : other;
    into.insert(into.end(), names.begin(), names.end());
  }
  for (const json::Value& item : competition["geoBroadcasts"].items()) {
    if (!item.isObject()) continue;
    auto name = text::text(item["media"]["shortName"]);
    if (!name) continue;
    auto market = text::text(item["market"]["type"]);
    (market && str::lower(*market) == "national" ? national : other).push_back(*name);
  }
  std::vector<std::string> out;
  std::set<std::string> seen;
  for (auto* list : {&national, &other}) {
    for (const std::string& n : *list) {
      if (seen.insert(str::lower(n)).second) out.push_back(n);
    }
  }
  return out;
}

std::optional<std::string> venue(const json::Value& item) {
  if (!item.isObject()) return std::nullopt;
  auto name = text::text(item["fullName"]);
  if (!name) return std::nullopt;
  auto city = text::text(item["address"]["city"]);
  if (city && name->find(*city) == std::string::npos) return *name + ", " + *city;
  return name;
}

std::vector<const json::Value*> tennisCompetitions(const json::Value& event) {
  std::vector<const json::Value*> out;
  const json::Value& groupings = event["groupings"];
  if (groupings.isArray() && groupings.size() > 0) {
    for (const json::Value& g : groupings.items()) {
      for (const json::Value& c : g["competitions"].items()) {
        if (c.isObject()) out.push_back(&c);
      }
    }
    return out;
  }
  for (const json::Value& c : event["competitions"].items()) {
    if (c.isObject()) out.push_back(&c);
  }
  return out;
}

std::optional<std::string> cardPartOfText(std::string_view input) {
  std::string value = str::lower(input);
  bool early = value.find("early") != std::string::npos;
  bool prelim = value.find("prelim") != std::string::npos;
  if (early && prelim) return std::string("early");
  if (prelim) return std::string("prelims");
  if (value.find("main") != std::string::npos) return std::string("main");
  return std::nullopt;
}

std::optional<std::string> cardPart(const json::Value& competition) {
  std::vector<std::string> texts;
  const json::Value& segment = competition["cardSegment"];
  if (segment.isObject()) {
    for (const char* key : {"description", "name", "abbreviation", "shortName"}) {
      if (auto v = text::text(segment[key])) texts.push_back(*v);
    }
  } else if (auto v = text::text(segment)) {
    texts.push_back(*v);
  }
  const json::Value& type = competition["type"];
  if (type.isObject()) {
    for (const char* key : {"text", "abbreviation", "slug", "description"}) {
      if (auto v = text::text(type[key])) texts.push_back(*v);
    }
  }
  return cardPartOfText(str::join(texts, " "));
}

std::optional<int64_t> boutMinute(const json::Value& bout) {
  auto start = text::date(text::text(bout["startDate"]));
  if (!start) start = text::date(text::text(bout["date"]));
  if (!start) return std::nullopt;
  return *start / 60;
}

std::vector<std::string> partKeys(const std::vector<json::Value>& bouts) {
  std::vector<std::optional<std::string>> named;
  bool anyPart = false;
  for (const json::Value& bout : bouts) {
    named.push_back(cardPart(bout));
    anyPart = anyPart || named.back().has_value();
  }
  std::map<int64_t, std::string> byTime;
  if (!anyPart) {
    std::set<int64_t> minutes;
    for (const json::Value& bout : bouts) {
      if (auto m = boutMinute(bout)) minutes.insert(*m);
    }
    if (minutes.size() >= 2) {
      int position = 0;
      for (auto it = minutes.rbegin(); it != minutes.rend(); ++it, ++position) {
        byTime[*it] = position == 0 ? "main" : (position == 1 ? "prelims" : "early");
      }
    }
  }
  std::vector<std::string> keys;
  for (size_t i = 0; i < bouts.size(); ++i) {
    if (anyPart) {
      keys.push_back(named[i].value_or("main"));
    } else if (byTime.empty()) {
      keys.emplace_back();
    } else {
      auto minute = boutMinute(bouts[i]);
      auto it = minute ? byTime.find(*minute) : byTime.end();
      keys.push_back(it != byTime.end() ? it->second : "main");
    }
  }
  return keys;
}

}  // namespace marigold::sports::parse
