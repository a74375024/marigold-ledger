#include "core/engine/quality.hpp"

#include <cmath>
#include <cstdlib>
#include <utility>

#include "core/util/json.hpp"
#include "core/util/rx.hpp"
#include "core/util/strings.hpp"

namespace marigold::engine::quality {
namespace {

// Superscript letters and digits providers tag names with ("ESPN ᴴᴰ",
// "ᵁᴴᴰ ⁶⁰ᶠᵖˢ"), and the ASCII they stand for.
const std::pair<const char*, const char*> kSuperscripts[] = {
    {"\xe2\x81\xb0", "0"}, {"\xc2\xb9", "1"},     {"\xc2\xb2", "2"},     {"\xc2\xb3", "3"},
    {"\xe2\x81\xb4", "4"}, {"\xe2\x81\xb5", "5"}, {"\xe2\x81\xb6", "6"}, {"\xe2\x81\xb7", "7"},
    {"\xe2\x81\xb8", "8"}, {"\xe2\x81\xb9", "9"},
    // Capital modifier letters (U+1D2C...), and ⱽ (U+2C7D).
    {"\xe1\xb4\xac", "A"}, {"\xe1\xb4\xae", "B"}, {"\xe1\xb4\xb0", "D"}, {"\xe1\xb4\xb1", "E"},
    {"\xe1\xb4\xb3", "G"}, {"\xe1\xb4\xb4", "H"}, {"\xe1\xb4\xb5", "I"}, {"\xe1\xb4\xb6", "J"},
    {"\xe1\xb4\xb7", "K"}, {"\xe1\xb4\xb8", "L"}, {"\xe1\xb4\xb9", "M"}, {"\xe1\xb4\xba", "N"},
    {"\xe1\xb4\xbc", "O"}, {"\xe1\xb4\xbe", "P"}, {"\xe1\xb4\xbf", "R"}, {"\xe1\xb5\x80", "T"},
    {"\xe1\xb5\x81", "U"}, {"\xe2\xb1\xbd", "V"}, {"\xe1\xb5\x82", "W"},
    // Small modifier letters.
    {"\xe1\xb5\x83", "a"}, {"\xe1\xb5\x87", "b"}, {"\xe1\xb6\x9c", "c"}, {"\xe1\xb5\x88", "d"},
    {"\xe1\xb5\x89", "e"}, {"\xe1\xb6\xa0", "f"}, {"\xe1\xb5\x8d", "g"}, {"\xca\xb0", "h"},
    {"\xe2\x81\xb1", "i"}, {"\xca\xb2", "j"},     {"\xe1\xb5\x8f", "k"}, {"\xcb\xa1", "l"},
    {"\xe1\xb5\x90", "m"}, {"\xe2\x81\xbf", "n"}, {"\xe1\xb5\x92", "o"}, {"\xe1\xb5\x96", "p"},
    {"\xca\xb3", "r"},     {"\xcb\xa2", "s"},     {"\xe1\xb5\x97", "t"}, {"\xe1\xb5\x98", "u"},
    {"\xe1\xb5\x9b", "v"}, {"\xca\xb7", "w"},     {"\xcb\xa3", "x"},     {"\xca\xb8", "y"},
    {"\xe1\xb6\xbb", "z"}};

// The same ranges catalog::cleanName strips as markers.
constexpr const char* kSuperscriptClass =
    "[\\x{1D2C}-\\x{1D6A}\\x{1D78}-\\x{1DBF}\\x{2070}-\\x{209F}\\x{2C7D}\\x{02B0}-\\x{02FF}"
    "\\x{00B2}\\x{00B3}\\x{00B9}]";

bool hasNonAscii(std::string_view s) {
  for (char c : s)
    if (static_cast<unsigned char>(c) >= 0x80) return true;
  return false;
}

// Lower-case ASCII words: superscript runs become words of their own
// ("ESPNᴴᴰ" -> "espn hd").
std::string plain(std::string_view text) {
  std::string s(text);
  if (hasNonAscii(s)) {
    std::string run = std::string("(?<!") + kSuperscriptClass + ")(?=" + kSuperscriptClass + ")";
    std::string end = std::string("(?<=") + kSuperscriptClass + ")(?!" + kSuperscriptClass + ")";
    s = rx::replace(s, run, " ");
    s = rx::replace(s, end, " ");
    for (const auto& [from, to] : kSuperscripts) s = str::replaceAll(s, from, to);
  }
  return str::lowerAscii(s);
}

// "4K", "FHD", "HD", "SD" or "". Never 8K (an "8K" tag is a provider's
// boast, not a picture this TV gets).
std::string resolutionClaim(const std::string& s) {
  if (rx::test("\\b(uhd|4k|2160[pi]?)\\b", s)) return "4K";
  if (rx::test("\\b(fhd|1080[pi]?(50|60)?)\\b", s)) return "FHD";
  if (rx::test("\\b(hd|720[pi]?(50|60)?)\\b", s)) return "HD";
  // Not a state after a town ("Sioux Falls, SD").
  if (rx::test("(?<!,)(?<!, )\\bsd\\b", s)) return "SD";
  return {};
}

// 50 or 60 (frames a second), or 0.
int fpsClaim(const std::string& s) {
  if (auto m = rx::first("\\b(50|60)\\s?fps\\b", s)) return m->group(1) == "50" ? 50 : 60;
  if (auto m = rx::first("\\b(?:2160|1080|720)[pi](50|60)\\b", s)) return m->group(1) == "50" ? 50 : 60;
  return 0;
}

}  // namespace

std::optional<Picture> fromFacts(std::string_view facts) {
  auto m = rx::first("^\\s*(\\d{2,5})x(\\d{2,5})(?:\\s+(\\d{1,3}(?:\\.\\d+)?)([pi]))?", facts);
  if (!m) return std::nullopt;
  Picture p;
  p.height = std::atoi(std::string(m->group(2)).c_str());
  if (p.height <= 0) return std::nullopt;
  if (m->has(3)) {
    double rate = std::atof(std::string(m->group(3)).c_str());
    if (rate > 0 && rate <= 240) p.fps = static_cast<int>(std::lround(rate));
    p.interlaced = m->group(4) == "i";
  }
  return p;
}

std::string text(const Picture& p) {
  if (p.height <= 0 || p.height > 2200) return {};
  std::string out;
  if (p.height >= 2000) {
    out = "4K";
  } else {
    int h = p.height;
    if (h >= 1070 && h <= 1100) h = 1080;  // 1088: coded with padding
    else if (h >= 712 && h <= 730) h = 720;
    out = std::to_string(h) + (p.interlaced ? "i" : "p");
  }
  if (p.fps > 0 && p.fps <= 120) out += " \xc2\xb7 " + std::to_string(p.fps);
  return out;
}

std::string claimed(std::string_view name, std::string_view group) {
  std::string n = plain(name);
  std::string g = group.empty() ? std::string() : plain(group);
  std::string resolution = resolutionClaim(n);
  if (resolution.empty() && !g.empty()) resolution = resolutionClaim(g);
  int fps = fpsClaim(n);
  if (fps == 0 && !g.empty()) fps = fpsClaim(g);
  if (fps == 0) return resolution;
  if (resolution.empty()) return std::to_string(fps) + " fps";
  return resolution + " \xc2\xb7 " + std::to_string(fps);
}

bool Memory::remember(const std::string& channelId, const Picture& p) {
  if (channelId.empty() || text(p).empty()) return false;
  bool changed = true;
  auto it = byId_.find(channelId);
  if (it != byId_.end()) {
    changed = it->second.picture != p;
    bySeq_.erase(it->second.seq);
    it->second = Kept{p, ++seq_};
  } else {
    byId_[channelId] = Kept{p, ++seq_};
  }
  bySeq_[seq_] = channelId;
  while (byId_.size() > kCap && !bySeq_.empty()) {
    byId_.erase(bySeq_.begin()->second);
    bySeq_.erase(bySeq_.begin());
  }
  return changed;
}

std::optional<Picture> Memory::find(const std::string& channelId) const {
  auto it = byId_.find(channelId);
  if (it == byId_.end()) return std::nullopt;
  return it->second.picture;
}

std::string Memory::toJson() const {
  json::Value list = json::Value::array();
  for (const auto& [seq, id] : bySeq_) {
    const Picture& p = byId_.at(id).picture;
    json::Value row = json::Value::array();
    row.push(id);
    row.push(p.height);
    row.push(p.fps);
    row.push(p.interlaced ? 1 : 0);
    list.push(std::move(row));
  }
  json::Value v = json::Value::object();
  v.set("v", 1);
  v.set("pictures", std::move(list));
  return json::write(v);
}

Memory Memory::fromJson(std::string_view textIn) {
  Memory m;
  std::optional<json::Value> v = json::parse(textIn);
  if (!v || !v->isObject() || (*v)["v"].asInt() != 1) return m;
  for (const json::Value& row : (*v)["pictures"].items()) {
    if (!row.isArray() || !row[size_t(0)].isString()) continue;
    Picture p;
    p.height = static_cast<int>(row[size_t(1)].asInt());
    p.fps = static_cast<int>(row[size_t(2)].asInt());
    p.interlaced = row[size_t(3)].asInt() != 0;
    m.remember(row[size_t(0)].asString(), p);
  }
  return m;
}

}  // namespace marigold::engine::quality
