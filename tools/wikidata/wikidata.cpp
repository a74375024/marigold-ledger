// marigold-wikidata: reads Wikipedia's US TV-market list, every market's
// station list and the networks' affiliate lists, and writes the data the
// app carries (catalog/wiki_data.hpp) as JSON. CI runs it at most weekly
// and the build packs the file into the app, so a TV never contacts
// Wikipedia. Requests go through the system's curl, one at a time, with
// the contact address Wikimedia asks automated clients for.
//
//   marigold-wikidata data/wiki-data.json
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/catalog/markets.hpp"
#include "core/catalog/wiki_data.hpp"
#include "core/guide/stations.hpp"

using namespace marigold;

namespace {

constexpr const char* kUserAgent =
    "marigold-build/1.0 (https://github.com/a74375024/marigold-ledger; weekly build data, one request at a time)";

std::string shellQuoted(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// One page's wikitext; a 429 or 503 waits and tries again.
std::optional<std::string> wikitext(const std::string& title) {
  static bool first = true;
  if (!first) pause(guide::stations::kRequestSpacingMs);
  first = false;
  std::string cmd = "curl -s -L --compressed --max-time 60 -A " + shellQuoted(kUserAgent) + " -w '\\n%{http_code}' " +
                    shellQuoted(guide::stations::wikiRequestUrl(title));
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
      std::fprintf(stderr, "  %s: HTTP %d, waiting %d s\n", title.c_str(), status, kWaitS[attempt]);
      pause(kWaitS[attempt] * 1000);
      continue;
    }
    if (status != 200) {
      std::fprintf(stderr, "  %s: HTTP %d\n", title.c_str(), status);
      return std::nullopt;
    }
    return guide::stations::wikitextFromResponse(body);
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
    std::fprintf(stderr, "usage: marigold-wikidata <out.json>\n");
    return 2;
  }
  catalog::wiki::Data data;
  data.made = today();

  // The market list, then each market's station list.
  auto list = wikitext(catalog::markets::kListPage);
  if (list) data.markets = catalog::markets::parseMarkets(*list);
  std::fprintf(stderr, "%zu markets\n", data.markets.size());
  size_t withStations = 0;
  for (const catalog::markets::Market& m : data.markets) {
    if (!m.templateName) continue;
    if (auto page = wikitext(*m.templateName)) {
      std::vector<std::string> calls = catalog::markets::callsignsInWikitext(*page);
      if (!calls.empty()) {
        data.callsigns[catalog::markets::nameKey(m.name)] = std::move(calls);
        ++withStations;
      }
    }
  }
  std::fprintf(stderr, "%zu markets with their stations\n", withStations);

  // Each network's affiliate list (the first of its pages that loads).
  std::vector<std::pair<std::string, std::string>> texts;
  for (const guide::stations::Page& page : guide::stations::pages()) {
    for (const std::string& title : page.titles) {
      if (auto text = wikitext(title)) {
        texts.push_back({page.network, std::move(*text)});
        break;
      }
    }
  }
  data.stations = guide::stations::collect(texts);
  std::fprintf(stderr, "%zu stations from %zu networks\n", data.stations.size(), texts.size());

  // Far fewer than Wikipedia lists means something went wrong: keep the
  // last good file instead of shipping a thin one.
  if (data.markets.size() < 150 || withStations < 100 || data.stations.size() < 500) {
    std::fprintf(stderr, "too little read; not writing %s\n", argv[1]);
    return 1;
  }
  std::ofstream out(argv[1], std::ios::binary);
  out << catalog::wiki::toJson(data);
  if (!out) {
    std::fprintf(stderr, "couldn't write %s\n", argv[1]);
    return 1;
  }
  std::fprintf(stderr, "wrote %s\n", argv[1]);
  return 0;
}
