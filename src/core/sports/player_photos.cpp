#include "core/sports/player_photos.hpp"

#include <algorithm>
#include <memory>

#include "core/sports/text.hpp"

namespace marigold::sports::player_photos {

namespace {

std::optional<std::string> lookup(const std::map<std::string, Entry>& entries, std::string_view name) {
  auto entry = entries.find(key(name));
  if (entry == entries.end() || entry->second.url.empty()) return std::nullopt;
  return entry->second.url;
}

}  // namespace

std::string key(std::string_view name) {
  return std::string(str::trimUnicode(str::replaceAll(str::lower(str::foldDiacritics(name)), "-", " ")));
}

sources::Request searchRequest(std::string_view name) {
  // Swift percent-encodes with .urlQueryAllowed; this encodes everything but
  // the unreserved characters, which the server reads the same way.
  sources::Request r;
  r.url = std::string(kBase) + sources::percentEncode(str::replaceAll(str::foldDiacritics(name), " ", "_"));
  r.timeoutSeconds = kTimeoutSeconds;
  return r;
}

std::optional<std::string> parse(std::string_view body, const std::string& wanted) {
  auto root = json::parse(body);
  if (!root) return std::nullopt;
  for (const json::Value& player : (*root)["player"].items()) {
    // Tennis players, and racing drivers for the F1 cameras panel.
    const json::Value& sport = player["strSport"];
    const json::Value& name = player["strPlayer"];
    if (!sport.isString() || !name.isString()) continue;
    std::string lower = str::lower(sport.asString());
    if ((lower != "tennis" && lower != "motorsport") || key(name.asString()) != wanted) continue;
    for (const char* field : {"strCutout", "strThumb"}) {
      const json::Value& url = player[field];
      if (url.isString() && str::startsWith(url.asString(), "https://")) return url.asString();
    }
  }
  return std::nullopt;
}

std::string toJson(const Saved& saved) {
  json::Value v = json::Value::object();
  json::Value entries = json::Value::object();
  for (const auto& [k, entry] : saved.entries) {
    json::Value item = json::Value::object();
    item.set("url", entry.url);
    item.set("at", static_cast<int64_t>(entry.at));
    entries.set(k, std::move(item));
  }
  v.set("entries", std::move(entries));
  if (saved.requestDay) v.set("requestDay", *saved.requestDay);
  v.set("requestCount", saved.requestCount);
  return json::write(v);
}

std::optional<Saved> savedFromJson(std::string_view text) {
  auto root = json::parse(text);
  if (!root || !root->isObject()) return std::nullopt;
  Saved saved;
  for (const auto& [k, item] : (*root)["entries"].fields()) {
    if (!item["url"].isString() || !item["at"].isNumber()) continue;
    saved.entries[k] = Entry{item["url"].asString(), static_cast<Seconds>(item["at"].asInt())};
  }
  if ((*root)["requestDay"].isString()) saved.requestDay = (*root)["requestDay"].asString();
  saved.requestCount = static_cast<int>((*root)["requestCount"].asInt());
  return saved;
}

std::optional<std::string> PlayerPhotos::photo(std::string_view name) const { return lookup(saved_.entries, name); }

std::string PlayerPhotos::smallUrl(const std::string& url) {
  // URL.appendingPathComponent("small"): before any query or fragment.
  size_t cut = url.find_first_of("?#");
  std::string path = url.substr(0, cut);
  std::string rest = cut == std::string::npos ? "" : url.substr(cut);
  return path + (str::endsWith(path, "/") ? "small" : "/small") + rest;
}

bool PlayerPhotos::request(const std::vector<std::string>& names, Seconds now) {
  for (const std::string& name : names) {
    std::string k = key(name);
    if (k.find(' ') == std::string::npos || queued_.count(k)) continue;
    auto entry = saved_.entries.find(k);
    if (entry != saved_.entries.end()) {
      Seconds life = entry->second.url.empty() ? kMissLife : kFoundLife;
      if (now - entry->second.at < life) continue;
    }
    queued_.insert(k);
    queue_.push_back(name);
  }
  if (draining_ || queue_.empty() || now < pausedUntil_) return false;
  draining_ = true;
  asked_ = 0;
  return true;
}

std::optional<PlayerPhotos::Request> PlayerPhotos::nextRequest(Seconds now, const time::Zone& zone) {
  if (!draining_ || awaiting_) return std::nullopt;
  while (!queue_.empty()) {
    std::string name = queue_.front();
    std::string k = key(name);
    // Saved meanwhile (another pass): nothing to ask.
    auto entry = saved_.entries.find(k);
    if (entry != saved_.entries.end() && now - entry->second.at < kMissLife) {
      queue_.erase(queue_.begin());
      queued_.erase(k);
      continue;
    }
    std::string today = text::isoDay(now, zone);
    if (saved_.requestDay != today) {
      saved_.requestDay = today;
      saved_.requestCount = 0;
    }
    if (saved_.requestCount >= kDailyCap) {
      // Done for today: nothing until tomorrow.
      pausedUntil_ = text::startOfDay(now, zone) + 86'400;
      break;
    }
    // At least 6 s between searches, across passes too.
    Seconds wait = lastRequestAt_ ? std::max<Seconds>(0, kSpacing - (now - *lastRequestAt_)) : 0;
    lastRequestAt_ = now + wait;
    asked_ += 1;
    saved_.requestCount += 1;
    dirty_ = true;
    awaiting_ = true;
    awaitingName_ = name;
    return Request{name, k, searchRequest(name), wait};
  }
  draining_ = false;
  return std::nullopt;
}

void PlayerPhotos::onResponse(int status, std::string_view body, Seconds now) {
  if (!awaiting_) return;
  awaiting_ = false;
  std::string k = key(awaitingName_);
  if (status != 200) {
    // Rate limited or offline: pause; the name stays queued.
    pausedUntil_ = now + kFailurePause;
    draining_ = false;
    return;
  }
  std::optional<std::string> found = parse(body, k);
  saved_.entries[k] = Entry{found.value_or(""), now};
  auto at = std::find(queue_.begin(), queue_.end(), awaitingName_);
  if (at != queue_.end()) queue_.erase(at);
  queued_.erase(k);
  dirty_ = true;
  if (found) revision_ += 1;
}

std::optional<Saved> PlayerPhotos::persist(Seconds now) {
  if (!dirty_) return std::nullopt;
  dirty_ = false;
  for (auto it = saved_.entries.begin(); it != saved_.entries.end();) {
    it = now - it->second.at < kKeep ? std::next(it) : saved_.entries.erase(it);
  }
  return saved_;
}

}  // namespace marigold::sports::player_photos
