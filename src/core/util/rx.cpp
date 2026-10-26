#include "core/util/rx.hpp"

#include <pcre2.h>

#include <memory>
#include <mutex>
#include <unordered_map>

#include "core/util/log.hpp"

namespace marigold::rx {
namespace {

struct CodeDeleter {
  void operator()(pcre2_code* c) const { pcre2_code_free(c); }
};
struct DataDeleter {
  void operator()(pcre2_match_data* d) const { pcre2_match_data_free(d); }
};
using Code = std::unique_ptr<pcre2_code, CodeDeleter>;
using Data = std::unique_ptr<pcre2_match_data, DataDeleter>;

struct Cache {
  std::mutex mu;
  std::unordered_map<std::string, Code> codes;  // nullptr = failed to compile
};

Cache& cache() {
  static Cache c;
  return c;
}

// Compiled patterns live until exit; the returned pointer stays valid.
const pcre2_code* compiled(std::string_view pattern, bool icase) {
  std::string key(icase ? "i:" : "s:");
  key += pattern;
  Cache& c = cache();
  std::lock_guard<std::mutex> lock(c.mu);
  auto it = c.codes.find(key);
  if (it != c.codes.end()) return it->second.get();
  uint32_t options = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;
  if (icase) options |= PCRE2_CASELESS;
  int err = 0;
  PCRE2_SIZE offset = 0;
  pcre2_code* code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
                                   options, &err, &offset, nullptr);
  if (!code) {
    PCRE2_UCHAR msg[160];
    pcre2_get_error_message(err, msg, sizeof msg);
    MLOG_E("rx", "pattern does not compile at %zu: %s", static_cast<size_t>(offset),
           reinterpret_cast<const char*>(msg));
  }
  auto [pos, inserted] = c.codes.emplace(std::move(key), Code(code));
  (void)inserted;
  return pos->second.get();
}

Data dataFor(const pcre2_code* code) {
  return Data(pcre2_match_data_create_from_pattern(code, nullptr));
}

// One match at `start`. Returns false at the end or on error.
bool matchAt(const pcre2_code* code, pcre2_match_data* data, std::string_view text, size_t start,
             uint32_t flags, Match& out) {
  int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), start, flags,
                       data, nullptr);
  if (rc < 0) return false;
  PCRE2_SIZE* ov = pcre2_get_ovector_pointer(data);
  uint32_t pairs = pcre2_get_ovector_count(data);
  out.groups.assign(pairs, std::nullopt);
  for (uint32_t i = 0; i < pairs; ++i) {
    if (ov[2 * i] == PCRE2_UNSET) continue;
    out.groups[i] = text.substr(ov[2 * i], ov[2 * i + 1] - ov[2 * i]);
  }
  out.begin = ov[0];
  out.end = ov[1];
  return true;
}

}  // namespace

bool valid(std::string_view pattern, bool caseInsensitive) {
  return compiled(pattern, caseInsensitive) != nullptr;
}

bool test(std::string_view pattern, std::string_view text, bool caseInsensitive) {
  const pcre2_code* code = compiled(pattern, caseInsensitive);
  if (!code) return false;
  Data data = dataFor(code);
  return pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), 0, 0,
                     data.get(), nullptr) >= 0;
}

std::optional<Match> first(std::string_view pattern, std::string_view text, bool caseInsensitive) {
  const pcre2_code* code = compiled(pattern, caseInsensitive);
  if (!code) return std::nullopt;
  Data data = dataFor(code);
  Match m;
  if (!matchAt(code, data.get(), text, 0, 0, m)) return std::nullopt;
  return m;
}

std::vector<Match> all(std::string_view pattern, std::string_view text, bool caseInsensitive) {
  std::vector<Match> out;
  const pcre2_code* code = compiled(pattern, caseInsensitive);
  if (!code) return out;
  Data data = dataFor(code);
  size_t start = 0;
  while (start <= text.size()) {
    Match m;
    if (!matchAt(code, data.get(), text, start, 0, m)) break;
    out.push_back(m);
    if (m.end == m.begin) {
      // Empty match: step past one UTF-8 code point to avoid looping.
      size_t next = m.end + 1;
      while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) ++next;
      start = next;
    } else {
      start = m.end;
    }
  }
  return out;
}

std::string replace(std::string_view text, std::string_view pattern, std::string_view templ,
                    bool caseInsensitive) {
  const pcre2_code* code = compiled(pattern, caseInsensitive);
  if (!code) return std::string(text);
  std::vector<Match> matches = all(pattern, text, caseInsensitive);
  if (matches.empty()) return std::string(text);
  std::string out;
  out.reserve(text.size());
  size_t last = 0;
  for (const Match& m : matches) {
    out.append(text.substr(last, m.begin - last));
    for (size_t i = 0; i < templ.size(); ++i) {
      char c = templ[i];
      if (c == '\\' && i + 1 < templ.size()) {
        out += templ[++i];
      } else if (c == '$' && i + 1 < templ.size() && templ[i + 1] >= '0' && templ[i + 1] <= '9') {
        out.append(m.group(static_cast<size_t>(templ[++i] - '0')));
      } else if (c == '$' && i + 2 < templ.size() && templ[i + 1] == '{') {
        size_t close = templ.find('}', i + 2);
        if (close == std::string_view::npos) {
          out += c;
          continue;
        }
        size_t n = 0;
        for (size_t k = i + 2; k < close; ++k) n = n * 10 + static_cast<size_t>(templ[k] - '0');
        out.append(m.group(n));
        i = close;
      } else {
        out += c;
      }
    }
    last = m.end;
  }
  out.append(text.substr(last));
  return out;
}

}  // namespace marigold::rx
