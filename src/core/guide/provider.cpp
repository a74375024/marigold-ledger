#include "core/guide/provider.hpp"

#include <algorithm>

#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide::provider {

std::string toJson(const Saved& saved) {
  json::Value v = json::Value::object();
  v.set("built", saved.built);
  v.set("channelCount", saved.channelCount);
  v.set("programs", guide::toJson(saved.programs));
  json::Value notes = json::Value::object();
  for (const auto& [k, n] : saved.notes) notes.set(k, n);
  v.set("notes", notes);
  v.set("guideChannels", saved.guideChannels);
  v.set("guideProgrammes", saved.guideProgrammes);
  v.set("bytes", saved.bytes);
  return json::write(v);
}

std::optional<Saved> savedFromJson(std::string_view text) {
  auto parsed = json::parse(text);
  if (!parsed || !parsed->isObject()) return std::nullopt;
  const json::Value& v = *parsed;
  if (!v["built"].isNumber() || !v["programs"].isObject()) return std::nullopt;
  Saved s;
  s.built = v["built"].asInt();
  s.channelCount = static_cast<int>(v["channelCount"].asInt());
  s.programs = programsFromJson(v["programs"]);
  for (const auto& [k, n] : v["notes"].fields()) s.notes[k] = n.asString();
  s.guideChannels = static_cast<int>(v["guideChannels"].asInt());
  s.guideProgrammes = static_cast<int>(v["guideProgrammes"].asInt());
  s.bytes = v["bytes"].asInt();
  return s;
}

std::optional<std::string> countryPrefix(const std::optional<std::string>& group) {
  if (!group) return std::nullopt;
  auto m = rx::first("^\\s*([A-Z]{2,4})\\s*\\|", *group);
  if (!m || !m->has(1)) return std::nullopt;
  return std::string(m->group(1));
}

bool inScope(const Channel& channel, const std::set<std::string>& countries) {
  if (!channel.epgId || channel.epgId->empty()) return false;
  if (auto prefix = countryPrefix(channel.group)) return countries.count(*prefix) > 0;
  return true;
}

std::string cacheKey(std::string_view profile, std::string_view sourceID) {
  std::string raw = std::string(profile) + "-" + std::string(sourceID);
  // CharacterSet.alphanumerics per code point, at most 90 (ASCII in practice).
  std::string out;
  size_t count = 0;
  for (size_t i = 0; i < raw.size() && count < 90; ++count) {
    unsigned char c = static_cast<unsigned char>(raw[i]);
    size_t len = c < 0x80             ? 1
                 : (c & 0xE0) == 0xC0 ? 2
                 : (c & 0xF0) == 0xE0 ? 3
                 : (c & 0xF8) == 0xF0 ? 4
                                      : 1;
    bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    out += alnum ? static_cast<char>(c) : '_';
    i += len;
  }
  return out;
}

std::string savedFileName(std::string_view key) { return "provider-" + std::string(key) + ".json"; }

bool needsRebuild(const Saved* saved, int channelCount, time::Seconds now) {
  if (!saved) return true;
  if (now - saved->built > kRefreshAge) return true;
  return channelCount > saved->channelCount + std::max(25, saved->channelCount / 10);
}

std::unordered_map<std::string, std::vector<std::string>> guideIds(
    const std::vector<Channel>& channels) {
  std::unordered_map<std::string, std::vector<std::string>> ids;
  for (const Channel& channel : channels) {
    if (!channel.epgId) continue;
    std::string_view epg = str::trimUnicode(*channel.epgId, true);
    if (epg.empty()) continue;
    ids[str::lower(epg)].push_back(channel.id);
  }
  return ids;
}

Outcome build(const std::vector<Channel>& channels, time::Seconds nowSec, const ReadExactFn& read,
              const std::function<void(const std::string&)>& progress) {
  Outcome out;
  auto ids = guideIds(channels);
  if (ids.empty()) {
    out.kind = Outcome::Kind::unavailable;
    out.message = "None of your channels have a guide id from the provider.";
    return out;
  }
  int now = static_cast<int>(nowSec / 60);
  Window window{now - 180, now + kKeepHours * 60};
  if (progress) progress("Downloading your provider's guide\xE2\x80\xA6");
  ReadFailure failure;
  std::optional<ExactRead> found = read ? read(ids, window, failure) : std::nullopt;
  if (!found) {
    static const int kNoGuide[] = {401, 403, 404, 405, 501};
    bool noGuide = std::find(std::begin(kNoGuide), std::end(kNoGuide), failure.httpStatus) !=
                   std::end(kNoGuide);
    if (noGuide) {
      out.kind = Outcome::Kind::unavailable;
      out.message = "This provider doesn't publish a full guide (" +
                    std::to_string(failure.httpStatus) + ").";
    } else {
      out.kind = Outcome::Kind::failed;
      out.message = failure.httpStatus
                        ? "The server answered " + std::to_string(failure.httpStatus) + "."
                        : failure.message;
    }
    return out;
  }
  if (found->guideChannels == 0 && found->programmes == 0) {
    out.kind = Outcome::Kind::unavailable;
    out.message = found->bytes < 512 ? "The provider's guide is empty."
                                     : "The provider didn't send a TV guide file.";
    return out;
  }
  out.kind = Outcome::Kind::built;
  out.saved.built = nowSec;
  out.saved.channelCount = static_cast<int>(channels.size());
  for (auto& [channelID, entry] : found->programs) {
    std::vector<RawProgram> sorted = entry.programs;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const RawProgram& a, const RawProgram& b) { return a.s < b.s; });
    out.saved.programs[channelID] = std::move(sorted);
    out.saved.notes[channelID] = "Provider guide: " + entry.note;
  }
  out.saved.guideChannels = found->guideChannels;
  out.saved.guideProgrammes = found->programmes;
  out.saved.bytes = found->bytes;
  return out;
}

}  // namespace marigold::guide::provider
