// marigold-channeldir: builds the channel directory the app carries
// (catalog/directory.hpp) from Wikipedia's channel lists, each listed
// article's redirects and Wikidata facts, and iptv-org's database for the
// channels already listed (catalog/directory_sources.hpp). CI runs it at
// most weekly and the build packs the file into the app, so a TV never
// contacts these sites. Requests go through the system's curl, one at a
// time, with the contact address Wikimedia asks automated clients for.
//
//   marigold-channeldir data/channel-directory.json
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "core/catalog/directory.hpp"
#include "core/catalog/directory_sources.hpp"
#include "core/guide/stations.hpp"
#include "core/util/strings.hpp"

using namespace marigold;
using namespace marigold::catalog::directory;

namespace {

constexpr const char* kUserAgent =
    "marigold-build/1.0 (https://github.com/a74375024/marigold-ledger; weekly build data, one request at a time)";

std::string shellQuoted(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// One answer's body; a 429 or 503 waits and tries again. `spaced`: wait
// between requests (Wikimedia's sites).
std::optional<std::string> fetch(const std::string& url, bool spaced) {
  static bool first = true;
  if (spaced && !first) pause(guide::stations::kRequestSpacingMs);
  first = false;
  std::string cmd = "curl -s -L --compressed --max-time 120 -A " + shellQuoted(kUserAgent) + " -w '\\n%{http_code}' " +
                    shellQuoted(url);
  static const int kWaitS[] = {5, 15, 30, 60};
  for (int attempt = 0; attempt <= 4; ++attempt) {
    std::string body;
    if (FILE* p = popen(cmd.c_str(), "r")) {
      char buf[65536];
      size_t n;
      while ((n = std::fread(buf, 1, sizeof buf, p)) > 0) body.append(buf, n);
      pclose(p);
    }
    int status = 0;
    size_t cut = body.rfind('\n');
    if (cut != std::string::npos) {
      status = std::atoi(body.c_str() + cut + 1);
      body.resize(cut);
    }
    if ((status == 429 || status == 503 || status == 0) && attempt < 4) {
      std::fprintf(stderr, "  HTTP %d, waiting %d s\n", status, kWaitS[attempt]);
      pause(kWaitS[attempt] * 1000);
      continue;
    }
    if (status != 200) {
      std::fprintf(stderr, "  %s: HTTP %d\n", url.substr(0, 120).c_str(), status);
      return std::nullopt;
    }
    return body;
  }
  return std::nullopt;
}

std::string today() {
  std::time_t t = std::time(nullptr);
  char out[16];
  std::strftime(out, sizeof out, "%Y-%m-%d", std::gmtime(&t));
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: marigold-channeldir <out.json>\n");
    return 2;
  }
  sources::Sources src;

  // The lists (the first title of each that loads).
  std::set<std::string> titles;
  for (const sources::ListPage& page : sources::listPages()) {
    for (const std::string& title : page.titles) {
      auto body = fetch(guide::stations::wikiRequestUrl(title), true);
      auto text = body ? guide::stations::wikitextFromResponse(*body) : std::nullopt;
      if (!text) continue;
      std::vector<sources::Row> rows = sources::parseList(*text);
      std::map<std::string, int> bySection;
      for (const sources::Row& r : rows) {
        ++bySection[r.section];
        if (!r.title.empty()) titles.insert(r.title);
      }
      std::fprintf(stderr, "%s: %s, %zu rows\n", page.list.c_str(), title.c_str(), rows.size());
      // A page that gave little: how it's laid out, to fix its reading.
      if (rows.size() < 20) {
        size_t tablesSeen = 0, bullets = 0;
        for (std::string_view line : marigold::str::split(*text, '\n')) {
          if (marigold::str::startsWith(line, "{|")) ++tablesSeen;
          if (marigold::str::startsWith(line, "*")) ++bullets;
        }
        std::fprintf(stderr, "    %zu bytes, %zu tables, %zu bullets; it starts:\n%s\n", text->size(), tablesSeen, bullets,
                     text->substr(0, 1500).c_str());
      }
      for (const auto& [section, n] : bySection) std::fprintf(stderr, "    %-50s %d\n", section.c_str(), n);
      src.rows[page.list] = std::move(rows);
      break;
    }
  }

  // Each linked article: where it leads, its redirects, its Wikidata item.
  std::vector<std::string> all(titles.begin(), titles.end());
  for (size_t at = 0; at < all.size(); at += 50) {
    std::vector<std::string> batch(all.begin() + static_cast<long>(at),
                                   all.begin() + static_cast<long>(std::min(all.size(), at + 50)));
    std::string more;
    for (int round = 0; round < 40; ++round) {
      auto body = fetch(sources::pagesRequestUrl(batch, more), true);
      auto answer = body ? sources::parsePagesAnswer(*body) : std::nullopt;
      if (!answer) break;
      for (auto& [from, to] : answer->leadsTo) src.leadsTo[from] = to;
      for (auto& [title, facts] : answer->pages) {
        sources::PageFacts& into = src.pages[title];
        into.redirects.insert(into.redirects.end(), facts.redirects.begin(), facts.redirects.end());
        if (!facts.wikidata.empty()) into.wikidata = facts.wikidata;
      }
      if (answer->continueQuery.empty()) break;
      more = answer->continueQuery;
    }
  }
  size_t redirects = 0;
  std::set<std::string> items;
  for (const auto& [title, facts] : src.pages) {
    redirects += facts.redirects.size();
    if (!facts.wikidata.empty()) items.insert(facts.wikidata);
  }
  std::fprintf(stderr, "%zu linked titles, %zu articles, %zu redirects, %zu Wikidata items\n", all.size(),
               src.pages.size(), redirects, items.size());

  std::vector<std::string> ids(items.begin(), items.end());
  for (size_t at = 0; at < ids.size(); at += 50) {
    std::vector<std::string> batch(ids.begin() + static_cast<long>(at),
                                   ids.begin() + static_cast<long>(std::min(ids.size(), at + 50)));
    if (auto body = fetch(sources::entitiesRequestUrl(batch), true)) {
      for (auto& [id, facts] : sources::parseEntities(*body)) src.entities[id] = std::move(facts);
    }
  }
  std::fprintf(stderr, "%zu Wikidata items read\n", src.entities.size());

  auto channels = fetch(std::string(sources::kIptvOrgBase) + "channels.csv", false);
  auto feeds = fetch(std::string(sources::kIptvOrgBase) + "feeds.csv", false);
  if (channels && feeds) src.iptv = sources::parseIptvOrg(*channels, *feeds);
  std::fprintf(stderr, "%zu iptv-org channels\n", src.iptv.size());

  Data data = sources::assemble(src, today());
  std::map<std::string, int> byList;
  size_t withLanguage = 0, closed = 0, withIptv = 0, aliases = 0, withWikidata = 0;
  for (const Entry& e : data.entries) {
    ++byList[e.list];
    if (!e.language.empty()) ++withLanguage;
    if (!e.closed.empty()) ++closed;
    if (!e.iptvOrg.empty()) ++withIptv;
    if (!e.wikidata.empty()) ++withWikidata;
    aliases += e.aliases.size();
  }
  std::fprintf(stderr, "%zu entries:", data.entries.size());
  for (const auto& [list, n] : byList) std::fprintf(stderr, " %s %d,", list.c_str(), n);
  std::fprintf(stderr, "\n  %zu other names, %zu with a language, %zu shut down, %zu with Wikidata, %zu with iptv-org\n",
               aliases, withLanguage, closed, withWikidata, withIptv);
  std::fprintf(stderr, "  %zu names only other countries have\n", data.elsewhere.size());

  // Far fewer than the lists hold means something went wrong: keep the
  // last good file instead of shipping a thin one.
  if (data.entries.size() < 300) {
    std::fprintf(stderr, "too little read; not writing %s\n", argv[1]);
    return 1;
  }
  std::ofstream out(argv[1], std::ios::binary);
  out << toJson(data);
  if (!out) {
    std::fprintf(stderr, "couldn't write %s\n", argv[1]);
    return 1;
  }
  std::fprintf(stderr, "wrote %s\n", argv[1]);
  return 0;
}
