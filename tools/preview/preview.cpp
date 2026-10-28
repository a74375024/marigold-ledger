// marigold-preview: the Live Events board on a computer, for feedback.
//
//   marigold-preview channel-report.csv [page.html]
//
// Reads a channel report (Help › Export channel report) and gives its
// channels to the real engine as a playlist, as if a TV had signed in to
// them; schedules and scores come from the network as on the TV. When the
// board has settled it is written as one HTML page and opened. Nothing but
// schedule and score requests leaves the computer: the report stays where it
// is, and no stream is ever opened (the report has no stream links).
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/engine/store.hpp"
#include "core/net/http.hpp"
#include "core/util/json.hpp"
#include "core/util/time.hpp"
#include "preview_icons.hpp"
#include "preview_page.hpp"

using namespace marigold;

namespace {

constexpr const char* kPlaylist = "http://preview.invalid/playlist.m3u";

std::optional<std::string> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream s;
  s << in.rdbuf();
  return s.str();
}

std::vector<std::vector<std::string>> readCsv(const std::string& text) {
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string cell;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (quoted) {
      if (c != '"') {
        cell += c;
      } else if (i + 1 < text.size() && text[i + 1] == '"') {
        cell += '"';
        ++i;
      } else {
        quoted = false;
      }
    } else if (c == '"') {
      quoted = true;
    } else if (c == ',') {
      row.push_back(std::move(cell));
      cell.clear();
    } else if (c == '\n') {
      row.push_back(std::move(cell));
      cell.clear();
      rows.push_back(std::move(row));
      row.clear();
    } else if (c != '\r') {
      cell += c;
    }
  }
  if (!cell.empty() || !row.empty()) {
    row.push_back(std::move(cell));
    rows.push_back(std::move(row));
  }
  return rows;
}

// The engine's saved files, in a folder of this run's own (removed after),
// so nothing from an older report carries over.
class Folder final : public engine::Files {
 public:
  explicit Folder(std::string dir) : dir_(std::move(dir)) {
    std::filesystem::remove_all(dir_);
    std::filesystem::create_directories(dir_);
  }
  ~Folder() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }
  Folder(const Folder&) = delete;
  Folder& operator=(const Folder&) = delete;
  const std::string& dir() const override { return dir_; }
  std::string path(const std::string& name) const override { return dir_ + "/" + name; }
  std::optional<std::string> read(const std::string& name) const override { return readFile(path(name)); }
  bool write(const std::string& name, const std::string& content, bool) override {
    std::string tmp = path(name) + ".tmp";
    {
      std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
      out << content;
      if (!out) return false;
    }
    return std::rename(tmp.c_str(), path(name).c_str()) == 0;
  }

 private:
  std::string dir_;
};

std::string shellQuoted(const std::string& s) {
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

// Requests one at a time on this thread, through the system's curl. Only
// the playlist (answered here) and schedule / score sites are let through;
// everything else (provider guides, logo files, station lists) is offline.
class Requests final : public http::Pool {
 public:
  std::string playlist;
  int sent = 0;

  void submit(http::Request req, Done done) override {
    queue_.push_back([this, req = std::move(req), done = std::move(done)]() mutable {
      http::Response r = run(req);
      if (done) done(std::move(r));
    });
  }
  void post(std::function<void()> work, std::function<void()> done, http::Priority) override {
    queue_.push_back([work = std::move(work), done = std::move(done)] {
      if (work) work();
      if (done) done();
    });
  }
  http::Response fetchNow(http::Request req) override { return run(req); }
  void cancelAll() override { queue_.clear(); }
  void pause(unsigned) override {}
  bool drain() {
    bool any = !queue_.empty();
    while (!queue_.empty()) {
      auto job = std::move(queue_.front());
      queue_.pop_front();
      job();
    }
    return any;
  }

 private:
  static bool allowed(const std::string& url) {
    return url.rfind("https://", 0) == 0 &&
           (url.find(".espn.com/") != std::string::npos || url.find(".espncdn.com/") != std::string::npos);
  }
  http::Response run(http::Request& req) {
    http::Response r;
    std::string body;
    if (req.url == kPlaylist) {
      body = playlist;
      r.status = 200;
    } else if (allowed(req.url)) {
      ++sent;
      unsigned seconds = req.timeoutMs ? std::max(5u, req.timeoutMs / 1000) : 30;
      std::string cmd = "curl -s -L --compressed --max-time " + std::to_string(seconds) + " -w '\\n%{http_code}'";
      for (const std::string& h : req.headers) cmd += " -H " + shellQuoted(h);
      cmd += " " + shellQuoted(req.url);
      if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) body.append(buf, n);
        pclose(p);
      }
      size_t cut = body.rfind('\n');
      if (cut != std::string::npos) {
        r.status = std::atoi(body.c_str() + cut + 1);
        body.resize(cut);
      }
    }
    if (r.status == 0) {
      r.error = "offline";
    } else {
      r.ok = r.status >= 200 && r.status < 300;
      r.bytes = body.size();
      if (req.onChunk) {
        for (size_t at = 0; at < body.size(); at += 16384) {
          if (!req.onChunk(std::string_view(body).substr(at, 16384))) break;
        }
      } else {
        r.body = std::move(body);
      }
    }
    if (req.afterTransfer) req.afterTransfer(r);
    return r;
  }
  std::deque<std::function<void()>> queue_;
};

std::string hex(uint32_t c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%06X", c & 0xFFFFFFu);
  return buf;
}

const char* pillName(engine::EventTile::Pill p) {
  using P = engine::EventTile::Pill;
  switch (p) {
    case P::Live: return "live";
    case P::LiveOutline: return "liveOutline";
    case P::OnNow: return "onNow";
    case P::Delayed: return "delayed";
    case P::LikelyOver: return "likelyOver";
    case P::Postponed: return "postponed";
    case P::Final: return "final";
    case P::NoTime: return "noTime";
    case P::NotStarted: return "notStarted";
    case P::None: break;
  }
  return "";
}

json::Value tileJson(engine::Store& store, const engine::EventTile& t, int number) {
  json::Value v = json::Value::object();
  v.set("n", number);
  v.set("id", t.id);
  v.set("pill", pillName(t.pill));
  v.set("pillText", t.pillText);
  v.set("status", t.status);
  v.set("statusBold", t.statusBold);
  v.set("statusExtra", t.statusExtra);
  v.set("tag", t.tag);
  v.set("tagAlert", t.tagAlert);
  v.set("league", t.league);
  v.set("favorite", t.favorite);
  json::Value sides = json::Value::array();
  for (const engine::EventSide& s : t.sides) {
    json::Value o = json::Value::object();
    o.set("abbr", s.abbr);
    o.set("name", s.name);
    o.set("country", s.country);
    o.set("logo", s.logoUrl);
    o.set("color", hex(s.color));
    o.set("alt", hex(s.alt));
    o.set("rank", s.rank);
    o.set("score", s.score);
    o.set("pens", s.pens);
    o.set("home", s.home);
    o.set("leads", s.leads);
    o.set("trails", s.trails);
    o.set("ball", s.ball);
    o.set("plain", s.plain);
    json::Value flags = json::Value::array();
    for (const std::string& f : s.flags) flags.push(f);
    o.set("flags", flags);
    sides.push(o);
  }
  v.set("sides", sides);
  v.set("title", t.title);
  v.set("subtitle", t.subtitle);
  json::Value parts = json::Value::array();
  for (const engine::EventPart& p : t.parts) {
    json::Value o = json::Value::object();
    o.set("label", p.label);
    o.set("fill", p.fill);
    o.set("current", p.current);
    parts.push(o);
  }
  v.set("parts", parts);
  json::Value podium = json::Value::array();
  for (const engine::EventPodium& p : t.podium) {
    json::Value o = json::Value::object();
    o.set("code", p.code);
    o.set("color", hex(p.color));
    podium.push(o);
  }
  v.set("podium", podium);
  json::Value leaders = json::Value::array();
  for (const engine::EventLeader& l : t.leaders) {
    json::Value o = json::Value::object();
    o.set("pos", l.pos);
    o.set("name", l.name);
    o.set("score", l.score);
    o.set("thru", l.thru);
    o.set("flag", l.flagUrl);
    leaders.push(o);
  }
  v.set("leaders", leaders);
  v.set("icon", t.icon);
  v.set("symbol", t.symbol);
  v.set("channel", t.channelName);
  v.set("channelLogo", t.channelLogo);
  v.set("pill2", t.pill2);
  v.set("footer", t.footer);
  v.set("situation", t.situation);
  json::Value bases = json::Value::array();
  for (bool b : t.bases) bases.push(b);
  v.set("bases", bases);
  v.set("redZone", t.redZone);
  v.set("progress", t.progress);
  v.set("watchable", t.watchable);
  v.set("finished", t.finished);
  v.set("upcoming", t.upcoming);
  v.set("leagueChip", t.leagueChip);
  v.set("heroTitle", t.heroTitle);
  v.set("heroStatus", t.heroStatus);
  v.set("whereLine", t.whereLine);
  v.set("venue", t.venue);
  // The detail card: where to watch and how the tile came about.
  engine::WhereToWatch w = store.whereToWatch(t.id);
  json::Value where = json::Value::array();
  for (const auto& [section, sources] : w.sections) {
    json::Value o = json::Value::object();
    o.set("title", section);
    json::Value list = json::Value::array();
    for (const engine::WatchSource& s : sources) {
      json::Value src = json::Value::object();
      src.set("title", s.title);
      src.set("detail", s.detail);
      list.push(src);
    }
    o.set("sources", list);
    where.push(o);
  }
  v.set("where", where);
  v.set("whereNote", w.note);
  json::Value notes = json::Value::array();
  for (const std::string& n : store.eventNotes(t.id)) notes.push(n);
  v.set("notes", notes);
  return v;
}

std::string nowText(const time::Zone& zone) {
  time::Seconds now = time::now();
  time::Civil c = time::civilFromSeconds(now + zone.offsetAt(now));
  char buf[64];
  int hour = c.hour % 12 == 0 ? 12 : c.hour % 12;
  std::snprintf(buf, sizeof buf, "%d/%d %d:%02d %s", c.month, c.day, hour, c.minute, c.hour < 12 ? "AM" : "PM");
  return buf;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: marigold-preview channel-report.csv [page.html]\n");
    return 2;
  }
  std::string csvPath = argv[1];
  auto text = readFile(csvPath);
  if (!text) {
    std::fprintf(stderr, "Can't read %s\n", csvPath.c_str());
    return 1;
  }
  auto rows = readCsv(*text);
  if (rows.empty()) {
    std::fprintf(stderr, "%s is empty\n", csvPath.c_str());
    return 1;
  }
  std::map<std::string, size_t> col;
  for (size_t i = 0; i < rows[0].size(); ++i) col[rows[0][i]] = i;
  if (!col.count("name") || !col.count("group")) {
    std::fprintf(stderr, "%s isn't a channel report (no name and group columns)\n", csvPath.c_str());
    return 1;
  }
  auto cell = [&](const std::vector<std::string>& row, const char* name) -> std::string {
    auto it = col.find(name);
    return it != col.end() && it->second < row.size() ? row[it->second] : std::string();
  };
  // The channels as a playlist (names and groups only: the report has no links).
  std::string m3u = "#EXTM3U\n";
  std::set<std::string> lineupNames;
  int channels = 0;
  for (size_t r = 1; r < rows.size(); ++r) {
    const auto& row = rows[r];
    std::string name = cell(row, "name");
    if (name.empty()) continue;
    std::string group = cell(row, "group");
    for (char& c : group)
      if (c == '"') c = '\'';
    m3u += "#EXTINF:-1 group-title=\"" + group + "\"," + name + "\nhttp://preview.invalid/" +
           std::to_string(++channels) + ".ts\n";
    if (cell(row, "in_my_lineup") == "yes" && !cell(row, "entry").empty()) lineupNames.insert(cell(row, "entry"));
  }
  std::fprintf(stderr, "%d channels in %s\n", channels, csvPath.c_str());
  auto began = std::chrono::steady_clock::now();
  auto elapsed = [&] {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count());
  };

  Folder folder((std::filesystem::temp_directory_path() / ("marigold-preview-" + std::to_string(::getpid()))).string());
  Requests requests;
  requests.playlist = m3u;
  uint32_t ms = 0;
  static const time::Zone zone = time::Zone::device();
  engine::Host host{requests, folder};
  host.ms = [&] { return ms; };
  host.zone = []() -> const time::Zone& { return zone; };
  host.playingFull = [] { return false; };
  host.build = "preview";
  engine::Store store(host);
  auto settle = [&](int rounds) {
    for (int i = 0; i < rounds; ++i) {
      requests.drain();
      ms += 1000;
      store.tick(ms);
    }
    requests.drain();
  };
  engine::AccountForm form;
  form.kind = engine::AccountForm::Kind::M3U;
  form.name = "Channel report";
  form.server = kPlaylist;
  std::string problem = "(no answer)";
  store.addAccount(form, [&](std::string p) { problem = std::move(p); });
  std::fprintf(stderr, "Sorting the channels\xe2\x80\xa6\n");
  for (int i = 0; i < 60 && problem == "(no answer)"; ++i) settle(1);
  settle(3);
  if (!problem.empty()) {
    std::fprintf(stderr, "Couldn't load the channels: %s\n", problem.c_str());
    return 1;
  }
  // My Lineup as the report had it ("In your lineup" on the tiles).
  if (auto ch = store.channels()) {
    std::vector<std::string> ids;
    for (const auto& g : ch->all) {
      if (lineupNames.count(g.name) && !g.inLineup) ids.push_back(g.id);
    }
    if (!ids.empty()) store.addToLineup(ids);
  }
  std::fprintf(stderr, "Loading schedules and scores\xe2\x80\xa6 (%d s)\n", elapsed());
  store.eventsOpen(true);
  // Until the requests stop (the schedules, then a round of scores), a
  // minute and a half of the engine's time at least.
  int lastSent = -1, quiet = 0;
  for (int round = 0; round < 400 && !(round >= 90 && quiet >= 30); ++round) {
    settle(1);
    auto now = store.events();
    quiet = requests.sent == lastSent && now && !now->loading ? quiet + 1 : 0;
    lastSent = requests.sent;
    if (round % 30 == 0) std::fprintf(stderr, "  %d requests (%d s)\n", requests.sent, elapsed());
  }
  auto board = store.events();
  if (!board) {
    std::fprintf(stderr, "No board\n");
    return 1;
  }

  json::Value data = json::Value::object();
  data.set("made", nowText(zone));
  data.set("report", std::filesystem::path(csvPath).filename().string());
  data.set("scores", board->scoresOn);
  json::Value rowsOut = json::Value::array();
  int tiles = 0;
  for (const engine::EventRow& row : board->rows) {
    json::Value r = json::Value::object();
    r.set("id", row.id);
    r.set("title", row.title);
    r.set("live", row.live);
    r.set("count", row.count);
    json::Value groups = json::Value::array();
    int number = 0;
    for (const engine::EventGroup& g : row.groups) {
      json::Value go = json::Value::object();
      go.set("title", g.title);
      json::Value list = json::Value::array();
      for (const engine::EventTile& t : g.tiles) {
        list.push(tileJson(store, t, ++number));
        ++tiles;
      }
      go.set("tiles", list);
      groups.push(go);
    }
    r.set("groups", groups);
    rowsOut.push(r);
  }
  data.set("rows", rowsOut);

  std::string out = argc > 2 ? argv[2]
                             : (std::filesystem::path(csvPath).parent_path() / "marigold-live-events.html").string();
  if (std::filesystem::path(out).parent_path().empty()) out = "./" + out;
  std::string json = json::write(data);
  // Inside <script>: no "</" can end it early.
  std::string safe;
  safe.reserve(json.size());
  for (size_t i = 0; i < json.size(); ++i) {
    if (json[i] == '<' && i + 1 < json.size() && json[i + 1] == '/') safe += "<\\";
    else safe += json[i];
  }
  std::string page = preview::kPage;
  size_t at = page.find("__DATA__");
  page.replace(at, 8, safe);
  at = page.find("__ICONS__");
  page.replace(at, 9, preview::kIcons);
  {
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    f << page;
    if (!f) {
      std::fprintf(stderr, "Can't write %s\n", out.c_str());
      return 1;
    }
  }
  std::fprintf(stderr, "%zu rows, %d tiles, %d requests: %s\n", board->rows.size(), tiles, requests.sent, out.c_str());
  if (!std::getenv("MARIGOLD_PREVIEW_NO_OPEN")) {
#ifdef __APPLE__
    std::string open = "open " + shellQuoted(out);
#else
    std::string open = "xdg-open " + shellQuoted(out) + " >/dev/null 2>&1";
#endif
    if (std::system(open.c_str()) != 0) std::fprintf(stderr, "Open %s in a browser\n", out.c_str());
  }
  return 0;
}
