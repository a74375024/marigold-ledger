#include "core/guide/provider.hpp"

#include <algorithm>
#include <unordered_map>

#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::guide::provider {

bool writeSaved(const Saved& saved, const json::Writer::Sink& sink) {
  // Keys in sorted order: the bytes json::write() gave for the whole tree.
  json::Writer w(sink);
  w.beginObject();
  w.key("built");
  w.value(saved.built);
  w.key("bytes");
  w.value(saved.bytes);
  w.key("channelCount");
  w.value(saved.channelCount);
  w.key("guideChannels");
  w.value(saved.guideChannels);
  w.key("guideProgrammes");
  w.value(saved.guideProgrammes);
  ListIndex index = writeLists(w, saved.programs);
  w.key("notes");
  w.beginObject();
  for (const auto& [k, note] : saved.notes) {
    w.key(k);
    w.value(note);
  }
  w.endObject();
  writePrograms(w, saved.programs, index);
  w.endObject();
  return w.finish();
}

std::string toJson(const Saved& saved) {
  std::string out;
  writeSaved(saved, [&out](std::string_view chunk) {
    out.append(chunk.data(), chunk.size());
    return true;
  });
  return out;
}

namespace {

template <typename Feed>
std::optional<Saved> readSavedWith(const Feed& feed) {
  Saved s;
  bool builtOk = false;
  ProgramsReader programs;
  json::DocumentReader reader(
      {"lists", "programs", "notes"},
      [&](const std::string& key, json::Value&& v) {
        if (key == "built") {
          builtOk = v.isNumber();
          s.built = v.asInt();
        } else if (key == "channelCount") {
          s.channelCount = static_cast<int>(v.asInt());
        } else if (key == "guideChannels") {
          s.guideChannels = static_cast<int>(v.asInt());
        } else if (key == "guideProgrammes") {
          s.guideProgrammes = static_cast<int>(v.asInt());
        } else if (key == "bytes") {
          s.bytes = v.asInt();
        }
        return true;
      },
      [&](const std::string& key, const std::string& member, json::Value&& v) {
        if (key == "notes") s.notes[member] = v.asString();
        else programs.part(key, member, std::move(v));
        return true;
      });
  if (!feed(reader) || !reader.finish() || !builtOk || !programs.sawPrograms()) return std::nullopt;
  s.programs = programs.take();
  return s;
}

}  // namespace

std::optional<Saved> savedFromJson(std::string_view text) {
  return readSavedWith([text](json::DocumentReader& r) { return r.feed(text); });
}

std::optional<Saved> readSaved(std::istream& in) {
  return readSavedWith([&in](json::DocumentReader& r) { return json::feedAll(in, r); });
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
  // Channels with the same guide id share one list, sorted once.
  auto byStart = [](const RawProgram& a, const RawProgram& b) { return a.s < b.s; };
  std::unordered_map<const void*, Schedule> sorted;
  SharedPrograms programs;
  for (auto& [channelID, entry] : found->programs) {
    if (!entry.programs) continue;
    Schedule& list = sorted[entry.programs.get()];
    if (!list) {
      if (std::is_sorted(entry.programs->begin(), entry.programs->end(), byStart)) {
        list = entry.programs;
      } else {
        std::vector<RawProgram> copy = *entry.programs;
        std::stable_sort(copy.begin(), copy.end(), byStart);
        list = std::make_shared<const std::vector<RawProgram>>(std::move(copy));
      }
    }
    programs[channelID] = list;
    out.saved.notes[channelID] = "Provider guide: " + entry.note;
  }
  found->programs.clear();
  sorted.clear();
  out.saved.programs = share(std::move(programs));
  out.saved.guideChannels = found->guideChannels;
  out.saved.guideProgrammes = found->programmes;
  out.saved.bytes = found->bytes;
  return out;
}

}  // namespace marigold::guide::provider
