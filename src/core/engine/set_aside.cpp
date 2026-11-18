#include "core/engine/set_aside.hpp"

#include <algorithm>

namespace marigold::engine::setaside {
namespace {

constexpr const char* kDot = " \xc2\xb7 ";

struct Words {
  Reason reason;
  const char* key;
  const char* title;
  const char* detail;
  const char* plain;
};

const Words kWords[] = {
    {Reason::OtherChannel, "other-channel", "It's a different channel", "Shows another channel's programmes",
     "it's a different channel"},
    {Reason::ReplayOther, "replay", "Its replay is a different channel", "Live is right; Rewind plays something else",
     "its replay is a different channel"},
    {Reason::WrongCity, "city", "Wrong city or feed", "Another city's station, or East instead of West",
     "wrong city or feed"},
    {Reason::WrongLanguage, "language", "Wrong language", "Same channel, another language", "wrong language"},
    {Reason::Listings, "listings", "Its listings don't match what's on", "The picture is right, the guide isn't",
     "its listings don't match what's on"},
    {Reason::DoesntWork, "broken", "It doesn't work", "Black, frozen, no sound, or never starts", "it doesn't work"},
    {Reason::Other, "other", "Something else", "", "something else"},
};

const Words& words(Reason r) {
  for (const Words& w : kWords)
    if (w.reason == r) return w;
  return kWords[6];
}

std::string quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

}  // namespace

const std::vector<Reason>& reasons() {
  static const std::vector<Reason> kAll = {Reason::OtherChannel, Reason::ReplayOther, Reason::WrongCity,
                                           Reason::WrongLanguage, Reason::Listings,    Reason::DoesntWork,
                                           Reason::Other};
  return kAll;
}

std::string title(Reason r) { return words(r).title; }
std::string detail(Reason r) { return words(r).detail; }
std::string plain(Reason r) { return words(r).plain; }
std::string key(Reason r) { return words(r).key; }

std::optional<Reason> fromKey(std::string_view k) {
  for (const Words& w : kWords)
    if (k == w.key) return w.reason;
  return std::nullopt;
}

json::Value toJson(const Record& r) {
  json::Value v = json::Value::object();
  v.set("entry", r.entryId);
  v.set("copy", r.copyId);
  v.set("reason", key(r.reason));
  if (!r.named.empty()) v.set("named", r.named);
  if (!r.namedName.empty()) v.set("namedName", r.namedName);
  if (!r.entryName.empty()) v.set("entryName", r.entryName);
  if (r.feed > 0) v.set("feed", r.feed);
  v.set("at", static_cast<int64_t>(r.at));
  v.set("server", r.server);
  v.set("item", r.item);
  v.set("name", r.name);
  if (!r.group.empty()) v.set("group", r.group);
  return v;
}

std::optional<Record> fromJson(const json::Value& v) {
  Record r;
  r.entryId = v["entry"].asString();
  r.copyId = v["copy"].asString();
  std::optional<Reason> reason = fromKey(v["reason"].asString());
  if (r.entryId.empty() || r.copyId.empty() || !reason) return std::nullopt;
  r.reason = *reason;
  r.named = v["named"].asString();
  r.namedName = v["namedName"].asString();
  r.entryName = v["entryName"].asString();
  r.feed = static_cast<int>(v["feed"].asInt(0));
  r.at = static_cast<time::Seconds>(v["at"].asInt(0));
  r.server = v["server"].asString();
  r.item = v["item"].asString();
  r.name = v["name"].asString();
  r.group = v["group"].asString();
  return r;
}

std::string feedName(int number) { return number > 0 ? "Feed " + std::to_string(number) : std::string("A feed"); }

std::vector<int> feedNumbers(size_t count, const std::vector<int>& taken) {
  std::vector<int> out;
  out.reserve(count);
  for (int n = 1; out.size() < count; ++n)
    if (std::find(taken.begin(), taken.end(), n) == taken.end()) out.push_back(n);
  return out;
}

std::string reasonLine(const Record& r) {
  if (r.reason == Reason::OtherChannel && !r.namedName.empty()) return "It's " + r.namedName;
  return title(r.reason);
}

std::string rowLine(const Record& r, std::string_view provider, std::string_view day) {
  std::string out = reasonLine(r);
  if (!provider.empty()) out += kDot + std::string(provider);
  if (!day.empty()) out += kDot + std::string(day);
  return out;
}

std::string logDetail(const Record& r, std::string_view onNow, std::string_view guideNow) {
  std::string out = plain(r.reason);
  if (r.reason == Reason::OtherChannel && !r.namedName.empty()) out += ": " + r.namedName;
  if (!r.name.empty()) {
    out += kDot + std::string("its name ") + quoted(r.name);
    if (!r.group.empty()) out += " in " + quoted(r.group);
  }
  if (!onNow.empty()) out += kDot + std::string("on now: ") + quoted(onNow);
  if (!guideNow.empty()) out += kDot + std::string("guide: ") + quoted(guideNow);
  return out;
}

std::string usedAgainDetail(const Record& r, std::string_view day) {
  std::string out = plain(r.reason);
  if (r.reason == Reason::OtherChannel && !r.namedName.empty()) out += ": " + r.namedName;
  if (!day.empty()) out += ", set aside " + std::string(day);
  return out;
}

}  // namespace marigold::engine::setaside
