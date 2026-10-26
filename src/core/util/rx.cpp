#include "core/util/rx.hpp"

#include <pcre2.h>

#include <memory>
#include <mutex>
#include <map>

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

struct Entry {
  Code code;           // nullptr = failed to compile
  uint32_t pairs = 0;  // capture groups + 1
};

// Ordered maps with a transparent comparator: a lookup takes the pattern as
// a string_view, so the hot path neither allocates nor hashes the (sometimes
// kilobyte-long) pattern text.
struct Cache {
  std::mutex mu;
  std::map<std::string, Entry, std::less<>> exact, caseless;
};

Cache& cache() {
  static Cache c;
  return c;
}

// Compiled patterns live until exit; the returned entry stays valid.
const Entry& compiled(std::string_view pattern, bool icase) {
  Cache& c = cache();
  std::lock_guard<std::mutex> lock(c.mu);
  auto& codes = icase ? c.caseless : c.exact;
  auto it = codes.find(pattern);
  if (it != codes.end()) return it->second;
  uint32_t options = PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF;
  if (icase) options |= PCRE2_CASELESS;
  int err = 0;
  PCRE2_SIZE offset = 0;
  pcre2_code* code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data()), pattern.size(),
                                   options, &err, &offset, nullptr);
  Entry entry;
  if (!code) {
    PCRE2_UCHAR msg[160];
    pcre2_get_error_message(err, msg, sizeof msg);
    MLOG_E("rx", "pattern does not compile at %zu: %s", static_cast<size_t>(offset),
           reinterpret_cast<const char*>(msg));
  } else {
    uint32_t groups = 0;
    pcre2_pattern_info(code, PCRE2_INFO_CAPTURECOUNT, &groups);
    entry.pairs = groups + 1;
  }
  entry.code = Code(code);
  auto [pos, inserted] = codes.emplace(std::string(pattern), std::move(entry));
  (void)inserted;
  return pos->second;
}

// One match block per thread, reused across calls: PCRE2 keeps its
// backtracking frames in it, so a fresh block per call cost a heap
// allocation per match. Grown when a pattern has more groups.
pcre2_match_data* scratch(uint32_t pairs) {
  struct Holder {
    Data data;
    uint32_t pairs = 0;
  };
  thread_local Holder holder;
  if (!holder.data || holder.pairs < pairs) {
    holder.pairs = pairs < 16 ? 16 : pairs;
    holder.data = Data(pcre2_match_data_create(holder.pairs, nullptr));
  }
  return holder.data.get();
}

// One match at `start`. Returns false at the end or on error. Only the
// pattern's own groups are read (the block may be larger).
bool matchAt(const Entry& entry, pcre2_match_data* data, std::string_view text, size_t start,
             uint32_t flags, Match& out) {
  int rc = pcre2_match(entry.code.get(), reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(),
                       start, flags, data, nullptr);
  if (rc < 0) return false;
  PCRE2_SIZE* ov = pcre2_get_ovector_pointer(data);
  uint32_t pairs = entry.pairs;
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
  return compiled(pattern, caseInsensitive).code != nullptr;
}

bool test(std::string_view pattern, std::string_view text, bool caseInsensitive) {
  const Entry& entry = compiled(pattern, caseInsensitive);
  if (!entry.code) return false;
  return pcre2_match(entry.code.get(), reinterpret_cast<PCRE2_SPTR>(text.data()), text.size(), 0,
                     0, scratch(entry.pairs), nullptr) >= 0;
}

std::optional<Match> first(std::string_view pattern, std::string_view text, bool caseInsensitive) {
  const Entry& entry = compiled(pattern, caseInsensitive);
  if (!entry.code) return std::nullopt;
  Match m;
  if (!matchAt(entry, scratch(entry.pairs), text, 0, 0, m)) return std::nullopt;
  return m;
}

std::vector<Match> all(std::string_view pattern, std::string_view text, bool caseInsensitive) {
  std::vector<Match> out;
  const Entry& entry = compiled(pattern, caseInsensitive);
  if (!entry.code) return out;
  pcre2_match_data* data = scratch(entry.pairs);
  size_t start = 0;
  while (start <= text.size()) {
    Match m;
    if (!matchAt(entry, data, text, start, 0, m)) break;
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
  if (!compiled(pattern, caseInsensitive).code) return std::string(text);
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
