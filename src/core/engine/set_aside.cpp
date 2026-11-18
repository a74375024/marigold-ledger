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
    // Build 182: a replay's.
    {Reason::WrongTime, "time", "It's at the wrong time", "Off from the time picked, e.g. an hour early",
     "it's at the wrong time"},
    {Reason::PlaysBadly, "plays", "It doesn't play well", "Stalls, freezes or won't open", "it doesn't play well"},
    {Reason::ReplayNames, "names", "Its programme names are wrong", "The Rewind bar names other shows",
     "its programme names are wrong"},
};

const Words& words(Reason r) {
  for (const Words& w : kWords)
    if (w.reason == r) return w;
  return kWords[6];
}

struct ScopeWord {
  Scope scope;
  const char* key;
};
const ScopeWord kScopes[] = {
    {Scope::Whole, "whole"}, {Scope::Listings, "listings"}, {Scope::Replay, "replay"},
    {Scope::ReplayListings, "replay-listings"}};

std::string quoted(std::string_view s) { return "\"" + std::string(s) + "\""; }

}  // namespace

const std::vector<Reason>& reasons() {
  static const std::vector<Reason> kAll = {Reason::OtherChannel, Reason::ReplayOther, Reason::WrongCity,
                                           Reason::WrongLanguage, Reason::Listings,    Reason::DoesntWork,
                                           Reason::Other};
  return kAll;
}

const std::vector<Reason>& replayReasons() {
  static const std::vector<Reason> kAll = {Reason::OtherChannel, Reason::WrongTime, Reason::PlaysBadly,
                                           Reason::ReplayNames, Reason::Other};
  return kAll;
}

std::string replayDetail(Reason r) {
  if (r == Reason::OtherChannel) return "The replay shows another channel's programmes";
  return words(r).detail;
}

std::string scopeKey(Scope s) {
  for (const ScopeWord& w : kScopes)
    if (w.scope == s) return w.key;
  return "whole";
}

std::optional<Scope> scopeFromKey(std::string_view k) {
  for (const ScopeWord& w : kScopes)
    if (k == w.key) return w.scope;
  return std::nullopt;
}

Scope scopeOf(Reason r, bool replay) {
  if (replay) return r == Reason::ReplayNames ? Scope::ReplayListings : Scope::Replay;
  return r == Reason::Listings ? Scope::Listings : Scope::Whole;
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
  v.set("scope", scopeKey(r.scope));
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
  // Build 182: saved before it had a scope: as its reason says (live).
  std::optional<Scope> scope = scopeFromKey(v["scope"].asString());
  r.scope = scope ? *scope : scopeOf(*reason, false);
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
  std::string out = (isReplay(r.scope) ? "replay: " : "") + plain(r.reason);
  if (r.reason == Reason::OtherChannel && !r.namedName.empty()) out += ": " + r.namedName;
  if (!day.empty()) out += ", set aside " + std::string(day);
  return out;
}

std::string replayLogDetail(const Record& r, std::string_view at, std::string_view clock,
                            std::string_view guideThen, std::string_view ownThen) {
  std::string out = "replay: " + plain(r.reason);
  if (!r.name.empty()) {
    out += kDot + std::string("its name ") + quoted(r.name);
    if (!r.group.empty()) out += " in " + quoted(r.group);
  }
  if (!at.empty()) out += kDot + std::string("at ") + std::string(at);
  if (!clock.empty()) out += kDot + std::string(clock);
  if (!guideThen.empty()) out += kDot + std::string("guide then: ") + quoted(guideThen);
  if (!ownThen.empty()) out += kDot + std::string("its own list then: ") + quoted(ownThen);
  return out;
}

std::string asideName(const Record& r, int number) {
  std::string out = feedName(number > 0 ? number : r.feed);
  return isReplay(r.scope) ? out + kDot + "replay" : out;
}

}  // namespace marigold::engine::setaside
