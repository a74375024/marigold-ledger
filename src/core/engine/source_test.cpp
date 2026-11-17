#include "core/engine/source_test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "core/engine/quality.hpp"
#include "core/engine/store.hpp"
#include "core/sources/xtream.hpp"
#include "core/util/json.hpp"

namespace marigold::engine::sourcetest {
namespace {

const char* const kShortMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string percent(double share) {
  return std::to_string(static_cast<int>(std::lround(std::clamp(share, 0.0, 1.0) * 100.0))) + "%";
}

char kindLetter(Kind k) {
  return k == Kind::Archive ? 'A' : k == Kind::Depth ? 'D' : k == Kind::Clock ? 'C' : k == Kind::Timing ? 'T' : 'L';
}

// The live outcome "o1080,60,1400": its height, frame rate and time to
// picture; nothing for "f" and the rest.
struct LiveResult {
  bool ok = false;
  int height = 0, fps = 0;
  uint32_t ms = 0;
};
LiveResult liveResult(const std::string& v) {
  LiveResult r;
  if (v.empty() || v[0] != 'o') return r;
  r.ok = true;
  int h = 0, f = 0;
  unsigned ms = 0;
  if (std::sscanf(v.c_str() + 1, "%d,%d,%u", &h, &f, &ms) == 3) {
    r.height = h;
    r.fps = f;
    r.ms = ms;
  }
  return r;
}

std::string daysText(uint16_t days) { return std::to_string(days) + (days == 1 ? " day" : " days"); }

// What a copy's name claims, in the words "(says HD)" uses: "HD", "4K", or ""
// (SD and nothing claim nothing to fall short of).
std::string claimWord(const std::string& claimed) {
  if (claimed.rfind("4K", 0) == 0) return "4K";
  if (claimed.rfind("FHD", 0) == 0 || claimed.rfind("HD", 0) == 0) return "HD";
  return "";
}

bool fallsShort(const std::string& word, int height) {
  if (height <= 0) return false;
  if (word == "4K") return height < 2000;
  if (word == "HD") return height < 1000;
  return false;
}

std::vector<std::string> splitHeaders(const std::string& lines) {
  std::vector<std::string> out;
  size_t at = 0;
  while (at < lines.size()) {
    size_t end = lines.find('\n', at);
    if (end == std::string::npos) end = lines.size();
    std::string line = lines.substr(at, end - at);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (!line.empty()) out.push_back(std::move(line));
    at = end + 1;
  }
  return out;
}

uint32_t medianOf(std::vector<uint32_t>& v) {  // sorts it
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

}  // namespace

// ------------------------------------------------------------------ plan

std::string key(const std::string& channelId, Kind kind) { return channelId + "#" + kindLetter(kind); }

size_t Plan::total() const {
  size_t n = 0;
  for (const auto& q : queues) n += q.size();
  return n;
}

Plan plan(const std::vector<scores::LineupRow>& rows, const std::vector<std::string>& providers,
          const std::function<bool(const std::string&)>& testable) {
  Plan p;
  p.providers = providers;
  p.queues.resize(providers.size());
  std::set<std::string> seen;
  for (size_t r = 0; r < rows.size(); ++r) {
    for (size_t c = 0; c < rows[r].copies.size(); ++c) {
      const scores::LineupCopy& copy = rows[r].copies[c];
      if (copy.feedId.empty() || !seen.insert(copy.feedId).second) continue;
      auto at = std::find(providers.begin(), providers.end(), copy.provider);
      if (at == providers.end()) continue;
      if (testable && !testable(copy.feedId)) continue;
      auto& q = p.queues[static_cast<size_t>(at - providers.begin())];
      if (copy.days > 0) {
        q.push_back({r, c, Kind::Archive, copy.feedId});
        q.push_back({r, c, Kind::Depth, copy.feedId});
      }
      q.push_back({r, c, Kind::Live, copy.feedId});
    }
  }
  return p;
}

uint32_t estimateSeconds(const Plan& p) {
  size_t longest = 0;
  for (const auto& q : p.queues) longest = std::max(longest, q.size());
  return static_cast<uint32_t>(longest * kItemMs / 1000);
}

// ------------------------------------------------------- same service

std::string normalName(std::string_view name) {
  std::string out;
  out.reserve(name.size());
  bool space = true;  // drops leading spaces
  for (char ch : name) {
    char c = ch;
    if (c == '|' || c == ':' || c == '\t') c = ' ';
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c == ' ') {
      if (!space) out += ' ';
      space = true;
      continue;
    }
    space = false;
    out += c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

std::vector<SameService> sameServices(const std::vector<scores::LineupRow>& rows,
                                      const std::vector<std::string>& providers, const scores::Book& book,
                                      time::Seconds now) {
  std::map<std::string, std::set<std::string>> names;
  for (const scores::LineupRow& r : rows)
    for (const scores::LineupCopy& c : r.copies) names[c.provider].insert(normalName(c.name));
  // A copy that mostly failed (plays and probes), per row and provider.
  auto fails = [&book, now](const scores::LineupRow& r, const std::string& provider, bool& known) {
    bool any = false;
    for (const scores::LineupCopy& c : r.copies) {
      if (c.provider != provider) continue;
      const scores::Record* rec = book.find(c.feedId);
      if (!rec) continue;
      scores::Record d = rec->at(now);
      if (d.starts() <= 0) continue;
      known = true;
      if (d.mostlyFails()) any = true;
    }
    return any;
  };
  std::vector<SameService> out;
  for (size_t i = 0; i < providers.size(); ++i)
    for (size_t j = i + 1; j < providers.size(); ++j) {
      const std::set<std::string>& a = names[providers[i]];
      const std::set<std::string>& b = names[providers[j]];
      if (a.empty() || b.empty()) continue;
      size_t common = 0;
      for (const std::string& n : a) common += b.count(n);
      double share = static_cast<double>(common) / static_cast<double>(std::min(a.size(), b.size()));
      if (share < kSameNames) continue;
      SameService s{providers[i], providers[j], share, std::nullopt};
      size_t failA = 0, failB = 0, both = 0;
      for (const scores::LineupRow& r : rows) {
        bool knownA = false, knownB = false;
        bool fa = fails(r, providers[i], knownA), fb = fails(r, providers[j], knownB);
        if (!knownA || !knownB) continue;
        failA += fa;
        failB += fb;
        both += fa && fb;
      }
      size_t either = failA + failB - both;
      if (either >= kFailuresToTell) {
        s.failureOverlap = static_cast<double>(both) / static_cast<double>(either);
        if (*s.failureOverlap < kSameFailures) continue;  // they fail apart: separate
      }
      out.push_back(std::move(s));
    }
  return out;
}

bool isSame(const std::vector<SameService>& same, std::string_view a, std::string_view b) {
  for (const SameService& s : same)
    if ((s.a == a && s.b == b) || (s.a == b && s.b == a)) return true;
  return false;
}

void preferSeparateBackup(std::vector<std::string>& ids,
                          const std::function<bool(std::string_view, std::string_view)>& same) {
  if (ids.size() < 3 || !same) return;
  std::string_view first = scores::providerOf(ids[0]);
  std::string_view second = scores::providerOf(ids[1]);
  if (second == first || !same(first, second)) return;
  for (size_t k = 2; k < ids.size(); ++k) {
    std::string_view p = scores::providerOf(ids[k]);
    if (p == first || same(first, p)) continue;
    std::rotate(ids.begin() + 1, ids.begin() + static_cast<long>(k), ids.begin() + static_cast<long>(k) + 1);
    return;
  }
}

// --------------------------------------------------------------- ranking

double rankScore(const scores::ProviderFigures& f) {
  double works = 0.5;
  if (f.startFailures && f.tested > 0) {
    // A light prior: a few starts that all worked aren't proof.
    double rate = (*f.startFailures * f.tested + 0.1) / (f.tested + 1);
    works = std::clamp(1.0 - kFailureWeight * rate, 0.0, 1.0);
  }
  double speed = 0.5;
  if (f.medianStartMs) speed = std::clamp(1.0 - (static_cast<double>(*f.medianStartMs) - 1500.0) / 8500.0, 0.0, 1.0);
  double picture = f.fullHdCopies.value_or(0.5);
  return 0.5 * works + 0.15 * speed + 0.15 * picture + 0.12 * f.lineup + 0.08 * f.replayWorks;
}

std::vector<size_t> rank(const scores::Rollup& r) {
  std::vector<size_t> order(r.providers.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&r](size_t a, size_t b) {
    return rankScore(r.providers[a]) > rankScore(r.providers[b]);
  });
  return order;
}

std::vector<Choice> choices(const scores::Rollup& r, const std::vector<SameService>& same) {
  std::map<std::string, double> works;
  for (const scores::ProviderFigures& f : r.providers)
    works[f.provider] = f.startFailures ? 1.0 - *f.startFailures : 0.5;
  std::vector<Choice> pairs;
  for (const scores::PairFigures& p : r.pairs) {
    Choice c;
    c.providers = {p.a, p.b};
    c.lineup = p.lineup;
    c.replay = p.replay;
    c.kind = isSame(same, p.a, p.b) ? Choice::Kind::Same : Choice::Kind::Separate;
    pairs.push_back(std::move(c));
  }
  // The best pair: separate services first, then the lineup in five-point
  // steps, then replay, then measured reliability.
  auto reliability = [&works](const Choice& c) {
    double s = 0;
    for (const std::string& p : c.providers) s += works[p];
    return s;
  };
  auto better = [&](const Choice& a, const Choice& b) {
    bool sa = a.kind == Choice::Kind::Separate, sb = b.kind == Choice::Kind::Separate;
    if (sa != sb) return sa;
    long la = std::lround(a.lineup * 20), lb = std::lround(b.lineup * 20);
    if (la != lb) return la > lb;
    if (std::fabs(a.replay - b.replay) > 1e-9) return a.replay > b.replay;
    return reliability(a) > reliability(b);
  };
  std::vector<Choice> out;
  if (!pairs.empty()) {
    size_t best = 0;
    for (size_t i = 1; i < pairs.size(); ++i)
      if (better(pairs[i], pairs[best])) best = i;
    pairs[best].best = true;
    out.push_back(pairs[best]);
    std::vector<Choice> rest;
    for (size_t i = 0; i < pairs.size(); ++i)
      if (i != best) rest.push_back(pairs[i]);
    std::stable_sort(rest.begin(), rest.end(), [](const Choice& a, const Choice& b) {
      if (std::fabs(a.lineup - b.lineup) > 1e-9) return a.lineup > b.lineup;
      return a.replay > b.replay;
    });
    out.insert(out.end(), rest.begin(), rest.end());
  }
  for (size_t i : rank(r)) {
    const scores::ProviderFigures& f = r.providers[i];
    Choice c;
    c.providers = {f.provider};
    c.lineup = f.lineup;
    c.replay = f.replay;
    c.kind = Choice::Kind::Alone;
    out.push_back(std::move(c));
  }
  return out;
}

Lost lost(const std::vector<scores::LineupRow>& rows, const std::vector<std::string>& kept, size_t limit) {
  Lost out;
  std::set<std::string> named;
  for (const scores::LineupRow& r : rows) {
    bool keeps = false;
    for (const scores::LineupCopy& c : r.copies)
      if (std::find(kept.begin(), kept.end(), c.provider) != kept.end()) keeps = true;
    if (keeps || r.name.empty() || !named.insert(r.name).second) continue;
    if (out.names.size() >= limit) {
      out.more = true;
      break;
    }
    out.names.push_back(r.name);
  }
  return out;
}

// ----------------------------------------------------------------- words

std::string estimateText(uint32_t seconds) {
  if (seconds < 3600) {
    uint32_t minutes = std::max<uint32_t>(10, (seconds + 599) / 600 * 10);
    return "About " + std::to_string(minutes) + " minutes";
  }
  uint32_t low = std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(seconds / 3600.0)));
  return "About " + std::to_string(low) + " to " + std::to_string(low + 1) + " hours";
}

std::string timeLeftText(uint32_t ms) {
  uint32_t minutes = (ms + 59999) / 60000;
  if (minutes <= 2) return "almost done";
  if (minutes < 60) return "about " + std::to_string(minutes) + " min left";
  uint32_t tens = (minutes + 5) / 10 * 10;  // to ten minutes
  uint32_t h = tens / 60, m = tens % 60;
  return "about " + std::to_string(h) + " h" + (m ? " " + std::to_string(m) + " min" : std::string()) + " left";
}

std::string testedText(time::Seconds t, time::Seconds now, const time::Zone& zone, bool clock24) {
  time::Civil l = zone.toLocal(t), n = zone.toLocal(now);
  int64_t dl = time::daysFromCivil(l.year, l.month, l.day), dn = time::daysFromCivil(n.year, n.month, n.day);
  char clock[16];
  if (clock24) {
    std::snprintf(clock, sizeof clock, "%02d:%02d", l.hour, l.minute);
  } else {
    int h = l.hour % 12;
    if (h == 0) h = 12;
    std::snprintf(clock, sizeof clock, "%d:%02d %s", h, l.minute, l.hour < 12 ? "AM" : "PM");
  }
  if (dl == dn) return std::string("today ") + clock;
  if (dl == dn - 1) return std::string("yesterday ") + clock;
  return std::string(kShortMonths[std::clamp(l.month, 1, 12) - 1]) + " " + std::to_string(l.day);
}

std::string testedDay(time::Seconds t, time::Seconds now, const time::Zone& zone) {
  std::string full = testedText(t, now, zone, true);
  size_t space = full.find(' ');
  if (full.rfind("today ", 0) == 0 || full.rfind("yesterday ", 0) == 0) return full.substr(0, space);
  return full;
}

std::string pictureText(int height, int fps) {
  if (height <= 0) return "";
  std::string lines = height >= 2000 ? std::string("4K") : std::to_string(height >= 1070 ? 1080 : height) + "p";
  return fps > 0 ? lines + " " + std::to_string(fps) : lines;
}

std::string secondsText(uint32_t ms) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f s", static_cast<double>(ms) / 1000.0);
  return buf;
}

std::string copyLine(const scores::Record* r, const std::string& claimed, uint16_t days, time::Seconds now) {
  if (!r) return "";
  scores::Record d = r->at(now);
  if (d.starts() <= 0) return "";
  if (d.mostlyFails()) return "Didn't start \xc2\xb7 tried last";
  std::vector<std::string> parts;
  std::string picture = pictureText(d.height, d.fps);
  if (!picture.empty()) {
    std::string word = claimWord(claimed);
    if (fallsShort(word, d.height)) picture += " (says " + word + ")";
    parts.push_back(picture);
  }
  if (std::optional<uint32_t> m = d.medianStartMs()) parts.push_back("starts in " + secondsText(*m));
  if (days > 0) {
    if (d.archiveOk == 0) parts.push_back("replay didn't play");
    else if (d.archiveOk == 1 && d.archiveDays == 0) parts.push_back("replay under " + daysText(days));
    else parts.push_back("replay " + daysText(d.archiveOk == 1 ? d.archiveDays : days));
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) out += (i ? " \xc2\xb7 " : "") + parts[i];
  return out;
}

std::string replayCell(const scores::ProviderFigures& f) {
  if (f.replayWorks <= 0) return "None";
  return percent(f.replayWorks) + (f.replayDays ? " \xc2\xb7 " + daysText(f.replayDays) : std::string());
}

// ------------------------------------------------------------------ runner

// The progress file, written on a worker; a burst of saves is written once.
struct Runner::Writer {
  std::mutex mu, fileMu;
  std::string pending;
  uint64_t seq = 0, pendingSeq = 0, written = 0;
};

Runner::Runner(Store& store, Host host)
    : store_(store), host_(std::move(host)), writer_(std::make_shared<Writer>()) {
  random_.seed(static_cast<uint32_t>(time::now()) ^ (host_.ms ? host_.ms() : 0u));
  load();
}

Runner::~Runner() {
  abortAll();
  if (dirty_) {
    // On this thread: the pool may be stopping. A write still queued is
    // older and is skipped.
    save(false);
    std::string body;
    uint64_t seq = 0;
    {
      std::lock_guard<std::mutex> lock(writer_->mu);
      body = writer_->pending;
      seq = writer_->pendingSeq;
    }
    std::lock_guard<std::mutex> lock(writer_->fileMu);
    if (host_.files.write(kFile, body)) writer_->written = seq;
  }
}

void Runner::logLine(const std::string& detail) {
  if (log) log("test", detail);
}

std::string Runner::providerName(const std::string& id) const {
  for (const auto& [sid, name] : store_.providerNames())
    if (sid == id) return name.empty() ? std::string("A provider") : name;
  return "A provider";
}

void Runner::load() {
  std::optional<std::string> text = host_.files.read(kFile);
  if (!text) return;
  std::optional<json::Value> v = json::parse(*text);
  if (!v || !v->isObject() || (*v)["v"].asInt() != 1) return;
  lastTested_ = (*v)["lastTested"].asInt();
  const json::Value& r = (*v)["run"];
  if (!r.isObject()) return;
  time::Seconds started = r["started"].asInt();
  if (started <= 0) return;
  if (time::now() - started > kRunKeptFor) {
    // Too old to carry on: dropped (what it found stays in the records).
    droppedOld_ = true;
    dirty_ = true;
    return;
  }
  run_.active = true;
  run_.started = started;
  for (const auto& [k, val] : r["done"].fields()) run_.done[k] = val.asString();
  for (const auto& [id, a] : r["accounts"].fields()) {
    Run::Account acc;
    acc.refusals = static_cast<int>(a["refusals"].asInt());
    acc.until = a["until"].asInt();
    acc.stopped = a["stopped"].asBool();
    acc.finished = a["finished"].asBool();
    run_.accounts[id] = acc;
  }
}

void Runner::save(bool now) {
  json::Value v = json::Value::object();
  v.set("v", 1);
  v.set("lastTested", static_cast<int64_t>(lastTested_));
  if (run_.active) {
    json::Value r = json::Value::object();
    r.set("started", static_cast<int64_t>(run_.started));
    json::Value done = json::Value::object();
    for (const auto& [k, val] : run_.done) done.set(k, val);
    r.set("done", std::move(done));
    json::Value accounts = json::Value::object();
    for (const auto& [id, a] : run_.accounts) {
      json::Value x = json::Value::object();
      x.set("refusals", a.refusals);
      x.set("until", static_cast<int64_t>(a.until));
      x.set("stopped", a.stopped);
      x.set("finished", a.finished);
      accounts.set(id, std::move(x));
    }
    r.set("accounts", std::move(accounts));
    v.set("run", std::move(r));
  }
  {
    std::lock_guard<std::mutex> lock(writer_->mu);
    writer_->pending = json::write(v);
    writer_->pendingSeq = ++writer_->seq;
  }
  if (!now) return;
  dirty_ = false;
  savedAt_ = ms_;
  std::shared_ptr<Writer> w = writer_;
  Files& files = host_.files;
  host_.pool.post(
      [w, &files] {
        std::string body;
        uint64_t seq = 0;
        {
          std::lock_guard<std::mutex> lock(w->mu);
          body = w->pending;
          seq = w->pendingSeq;
        }
        std::lock_guard<std::mutex> lock(w->fileMu);
        if (seq <= w->written) return;
        if (files.write(kFile, body)) w->written = seq;
      },
      nullptr, http::Priority::Background);
}

void Runner::saveNow() {
  if (dirty_) save(true);
}

bool Runner::ensurePlan() {
  if (planned_) return true;
  rows_ = store_.lineupRows();
  if (rows_.empty()) return false;
  std::vector<std::string> ids;
  for (const auto& [id, name] : store_.providerNames()) ids.push_back(id);
  plan_ = plan(rows_, ids, [this](const std::string& c) { return store_.directCopy(c); });
  slots_.clear();
  for (size_t i = 0; i < plan_.providers.size(); ++i) {
    Slot s;
    s.index = i;
    slots_.push_back(std::move(s));
    Run::Account& acc = run_.accounts[plan_.providers[i]];
    if (plan_.queues[i].empty()) acc.finished = true;
  }
  planned_ = true;
  return true;
}

uint32_t Runner::estimateNow() const {
  std::vector<std::string> ids;
  for (const auto& [id, name] : store_.providerNames()) ids.push_back(id);
  return estimateSeconds(plan(store_.lineupRows(), ids, [this](const std::string& c) { return store_.directCopy(c); }));
}

void Runner::start() {
  abortAll();
  run_ = Run{};
  run_.active = true;
  run_.started = time::now();
  planned_ = false;
  slots_.clear();
  waitingForPlay_ = false;
  resumeAt_ = 0;
  logLine("started");
  save(true);
}

void Runner::stop() {
  if (run_.active) endRun(true);
}

void Runner::abortAll() {
  for (Slot& s : slots_) {
    if (s.abort) s.abort->store(true);
    s.inFlight = false;
  }
}

void Runner::endRun(bool stopped) {
  abortAll();
  std::string sum = summary();
  run_.active = false;
  lastTested_ = time::now();
  ++ended_;
  logLine(stopped ? "stopped by you; what was found so far is kept" : "finished");
  if (!sum.empty()) logLine("summary: " + sum);
  save(true);
}

bool Runner::tick(uint32_t ms) {
  ms_ = ms;
  if (droppedOld_) {
    droppedOld_ = false;
    logLine("an unfinished test from more than two days ago was dropped");
    save(true);
  }
  if (!run_.active) return false;
  if (!ensurePlan()) return false;
  bool changed = false;
  if (playing && playing()) {
    if (!waitingForPlay_) {
      waitingForPlay_ = true;
      abortAll();
      logLine("paused while you watch");
      save(true);
      changed = true;
    }
    return changed;
  }
  if (waitingForPlay_) {
    waitingForPlay_ = false;
    resumeAt_ = ms + kResumeAfterPlayMs;
    logLine("carried on");
    changed = true;
  }
  if (resumeAt_) {
    if (static_cast<int32_t>(ms - resumeAt_) < 0) return changed;
    resumeAt_ = 0;
  }
  time::Seconds now = time::now();
  bool open = false;
  for (Slot& s : slots_) {
    const std::string& id = plan_.providers[s.index];
    Run::Account& acc = run_.accounts[id];
    if (acc.stopped || acc.finished) continue;
    open = true;
    if (s.inFlight) {
      if (ms - s.startedMs >= kLostAfterMs) {
        // It never answered (the requests were dropped): tried again once,
        // then counted as failed.
        if (s.abort) s.abort->store(true);
        s.inFlight = false;
        s.nextMs = ms + kGapMinMs;
        const auto& q = plan_.queues[s.index];
        if (s.at < q.size()) {
          const Item& item = q[s.at];
          std::string k = key(item.channelId, item.kind);
          if (++s.lost[k] >= 2) {
            run_.done[k] = "f";
            if (item.kind == Kind::Live) store_.recordProbe(item.channelId, scores::Probe{});
            dirty_ = true;
          }
        }
        changed = true;
      }
      continue;
    }
    if (acc.until > 0) {
      if (acc.until > now) continue;
      acc.until = 0;
      logLine(providerName(id) + ": trying again");
      dirty_ = true;
      changed = true;
    }
    if (static_cast<int32_t>(ms - s.nextMs) < 0) continue;
    next(s, ms);
    changed = true;
  }
  if (!open) {
    endRun(false);
    return true;
  }
  if (dirty_ && ms - savedAt_ >= 30000) save(true);
  return changed;
}

void Runner::next(Slot& s, uint32_t ms) {
  const std::string& id = plan_.providers[s.index];
  Run::Account& acc = run_.accounts[id];
  const std::vector<Item>& q = plan_.queues[s.index];
  while (s.at < q.size() && run_.done.count(key(q[s.at].channelId, q[s.at].kind))) ++s.at;
  if (s.at >= q.size()) {
    acc.finished = true;
    s.nowText.clear();
    logLine(providerName(id) + ": done");
    // Build 170: the clocks its replay copies' archives are on.
    std::vector<int> clocks;
    std::set<std::string> seen;
    std::optional<int> server;
    for (const Item& item : q) {
      if (item.kind != Kind::Archive || !seen.insert(item.channelId).second) continue;
      auto c = run_.done.find(key(item.channelId, Kind::Clock));
      if (c == run_.done.end() || c->second.size() < 2 || c->second[0] != 'o') continue;
      clocks.push_back(std::atoi(c->second.c_str() + 1));
      if (!server) server = store_.archiveClock(item.channelId).server;
    }
    std::string words = clocksText(clocks, server);
    if (!words.empty()) logLine(providerName(id) + ": archive clocks: " + words);
    // Build 172: how its archives start, in words.
    std::vector<archive::Timing> timings;
    seen.clear();
    for (const Item& item : q) {
      if (item.kind != Kind::Archive || !seen.insert(item.channelId).second) continue;
      auto t = run_.done.find(key(item.channelId, Kind::Timing));
      if (t == run_.done.end() || t->second.size() < 2 || t->second[0] != 't') continue;
      if (std::optional<archive::Timing> v = archive::decode(t->second.substr(1))) timings.push_back(*v);
    }
    std::string timing = archive::providerText(timings);
    if (!timing.empty()) logLine(providerName(id) + ": archive timing: " + timing);
    dirty_ = true;
    return;
  }
  const Item& item = q[s.at];
  if (!acc.logged) {
    acc.logged = true;
    logLine(providerName(id) + (s.at > 0 ? ": carried on" : ": started"));
  }
  std::string k = key(item.channelId, item.kind);
  // No archive two hours back: its edge isn't asked for.
  if (item.kind == Kind::Depth) {
    auto a = run_.done.find(key(item.channelId, Kind::Archive));
    if (a != run_.done.end() && a->second != "o") {
      run_.done[k] = "s";
      dirty_ = true;
      s.nextMs = ms;
      return;
    }
  }
  // Build 172: a copy whose archive played: its timing, in a turn of its
  // own before its next item.
  if ((item.kind == Kind::Depth || item.kind == Kind::Live) && !run_.done.count(key(item.channelId, Kind::Timing)) &&
      store_.catchupDays(item.channelId) > 0) {
    auto a = run_.done.find(key(item.channelId, Kind::Archive));
    if (a != run_.done.end() && a->second == "o") {
      readTiming(s, item, ms);
      return;
    }
  }
  // Build 170: a copy with catch-up: its own guide first, for its clock.
  if (item.kind == Kind::Archive && !run_.done.count(key(item.channelId, Kind::Clock)) &&
      store_.catchupDays(item.channelId) > 0) {
    readClock(s, item, ms);
    return;
  }
  probeItem(s, item, ms, ms);
}

void Runner::readClock(Slot& s, const Item& item, uint32_t ms) {
  const scores::LineupRow& row = rows_[item.row];
  std::string name = row.name.empty() ? std::string("a channel") : row.name;
  s.nowText = name + " \xc2\xb7 archive clock";
  s.inFlight = true;
  s.startedMs = ms;
  s.abort = std::make_shared<std::atomic<bool>>(false);
  std::shared_ptr<std::atomic<bool>> abort = s.abort;
  std::weak_ptr<bool> alive = alive_;
  size_t index = s.index;
  Item copy = item;
  store_.archiveTable(item.channelId, [this, alive, index, copy, abort, ms](const Store::ArchiveTableAnswer& a) {
    if (alive.expired() || index >= slots_.size()) return;
    Slot& slot = slots_[index];
    if (slot.abort != abort || !slot.inFlight) return;  // dropped meanwhile
    uint32_t at = host_.ms ? host_.ms() : ms_;
    if (abort->load()) {
      slot.inFlight = false;
      slot.nextMs = at + kGapMinMs;
      return;
    }
    if (a.refused) {
      // As a probe's refusal: the provider is left alone a while.
      probe::Outcome o;
      o.refused = true;
      o.why = a.why;
      finishItem(index, copy, o, at, ms);
      return;
    }
    run_.done[key(copy.channelId, Kind::Clock)] = a.clock ? "o" + std::to_string(*a.clock) : std::string("f");
    dirty_ = true;
    // Its archive probe straight after, in the clock just read.
    slot.inFlight = false;
    probeItem(slot, copy, at, ms);
  });
}

void Runner::readTiming(Slot& s, const Item& item, uint32_t ms) {
  const scores::LineupRow& row = rows_[item.row];
  std::string name = row.name.empty() ? std::string("a channel") : row.name;
  std::string k = key(item.channelId, Kind::Timing);
  int serial = ++s.serial;
  size_t index = s.index;
  Item copy = item;
  std::weak_ptr<bool> alive = alive_;
  s.nowText = name + " \xc2\xb7 replay timing";
  s.inFlight = true;
  s.startedMs = ms;
  s.abort = store_.measureArchive(
      item.channelId, kTimingBudgetMs,
      [this, alive, index, copy, serial, ms, k](const archive::Outcome& o) {
        if (alive.expired() || index >= slots_.size()) return;
        Slot& slot = slots_[index];
        if (!slot.inFlight || slot.serial != serial) return;  // dropped meanwhile
        uint32_t at = host_.ms ? host_.ms() : ms_;
        if (o.aborted || (slot.abort && slot.abort->load())) {
          slot.inFlight = false;
          slot.nextMs = at + kGapMinMs;
          return;
        }
        if (o.refused) {
          // As a probe's refusal: the provider is left alone a while.
          probe::Outcome p;
          p.refused = true;
          p.why = o.why;
          finishItem(index, copy, p, at, ms);
          return;
        }
        run_.done[k] = o.timing ? "t" + archive::encode(*o.timing) : std::string("f");
        dirty_ = true;
        slot.inFlight = false;
        slot.nextMs = at + kGapMinMs + static_cast<uint32_t>(random_() % (kGapMaxMs - kGapMinMs + 1));
      },
      clock);
  if (!s.abort) {
    // Nothing to measure (gone from the provider's list).
    s.inFlight = false;
    run_.done[k] = "x";
    dirty_ = true;
    s.nextMs = ms;
  }
}

void Runner::probeItem(Slot& s, const Item& item, uint32_t ms, uint32_t started) {
  std::string k = key(item.channelId, item.kind);
  std::optional<Feed> live = store_.copyFeed(item.channelId);
  probe::Request req;
  const scores::LineupRow& row = rows_[item.row];
  uint16_t days = row.copies[item.copy].days;
  if (live && item.kind == Kind::Live) {
    req.url = live->url;
    req.headers = splitHeaders(live->headers);
  } else if (live) {
    time::Seconds now = time::now();
    time::Seconds back = item.kind == Kind::Archive ? kArchiveBack
                                                    : std::max<time::Seconds>(kArchiveBack, days * 86400 - kEdgeMargin);
    std::optional<Store::CatchupFeed> c = store_.catchupFeed(*live, now - back, now);
    if (c) {
      req.url = c->feed.url;
      req.headers = splitHeaders(c->feed.headers);
    }
  }
  if (req.url.empty()) {
    // Gone from the provider's list, or no link: nothing to test.
    run_.done[k] = "x";
    dirty_ = true;
    s.nextMs = ms;
    return;
  }
  std::string name = row.name.empty() ? std::string("a channel") : row.name;
  if (item.kind == Kind::Live) {
    s.nowText = name + " \xc2\xb7 live";
  } else {
    time::Seconds back = item.kind == Kind::Archive ? kArchiveBack
                                                    : std::max<time::Seconds>(kArchiveBack, days * 86400 - kEdgeMargin);
    s.nowText = name + " \xc2\xb7 replay " + std::to_string(back / 3600) + " h back";
  }
  s.inFlight = true;
  s.startedMs = ms;
  s.abort = std::make_shared<std::atomic<bool>>(false);
  auto result = std::make_shared<probe::Outcome>();
  http::Pool& pool = host_.pool;
  probe::Clock probeClock = clock;
  std::shared_ptr<std::atomic<bool>> abort = s.abort;
  std::weak_ptr<bool> alive = alive_;
  size_t index = s.index;
  Item copy = item;
  pool.post([&pool, req, probeClock, abort, result] { *result = probe::run(pool, req, probeClock, abort.get()); },
            [this, alive, index, copy, result, abort, started] {
              if (alive.expired() || index >= slots_.size()) return;
              Slot& slot = slots_[index];
              if (slot.abort != abort || !slot.inFlight) return;  // dropped meanwhile
              uint32_t at = host_.ms ? host_.ms() : ms_;
              if (abort->load() || result->aborted) {
                slot.inFlight = false;
                slot.nextMs = at + kGapMinMs;
                return;
              }
              finishItem(index, copy, *result, at, started);
            },
            http::Priority::Background);
}

void Runner::finishItem(size_t index, const Item& item, const probe::Outcome& o, uint32_t ms, uint32_t started) {
  Slot& s = slots_[index];
  s.inFlight = false;
  const std::string& id = plan_.providers[index];
  Run::Account& acc = run_.accounts[id];
  uint32_t gap = kGapMinMs + static_cast<uint32_t>(random_() % (kGapMaxMs - kGapMinMs + 1));
  s.nextMs = ms + gap;
  if (o.refused) {
    ++acc.refusals;
    if (acc.refusals >= 2) {
      acc.stopped = true;
      logLine(providerName(id) + ": stopped, it refused again (" + o.why + ")");
    } else {
      acc.until = time::now() + kRefusalPauseMs / 1000;
      logLine(providerName(id) + ": paused, it refused a connection (" + o.why + "); trying again in 15 min");
    }
    save(true);
    return;
  }
  acc.refusals = 0;
  s.costMs += (ms - started) + gap;
  ++s.costN;
  std::string k = key(item.channelId, item.kind);
  if (item.kind != Kind::Live) {
    run_.done[k] = o.ok ? "o" : "f";
  } else {
    run_.done[k] = o.ok ? "o" + std::to_string(o.height) + "," + std::to_string(o.fps) + "," + std::to_string(o.pictureMs)
                        : "f";
    scores::Probe p;
    p.ok = o.ok;
    p.startMs = o.pictureMs;
    p.width = o.width;
    p.height = o.height;
    p.fps = o.fps;
    uint16_t days = rows_[item.row].copies[item.copy].days;
    auto a = run_.done.find(key(item.channelId, Kind::Archive));
    if (days > 0 && a != run_.done.end() && (a->second == "o" || a->second == "f")) {
      p.archiveOk = a->second == "o";
      auto d = run_.done.find(key(item.channelId, Kind::Depth));
      p.archiveDays = a->second == "o" && d != run_.done.end() && d->second == "o" ? days : 0;
    }
    store_.recordProbe(item.channelId, p);
  }
  dirty_ = true;
}

Runner::Status Runner::status() const {
  Status st;
  st.active = run_.active;
  st.waitingForPlay = waitingForPlay_;
  time::Seconds now = time::now();
  size_t done = 0, total = 0;
  uint32_t left = 0;
  bool anyLeft = false;
  for (const Slot& s : slots_) {
    Provider p;
    p.id = plan_.providers[s.index];
    p.name = providerName(p.id);
    const auto& q = plan_.queues[s.index];
    p.total = q.size();
    for (const Item& item : q) p.done += run_.done.count(key(item.channelId, item.kind));
    auto acc = run_.accounts.find(p.id);
    bool stopped = acc != run_.accounts.end() && acc->second.stopped;
    bool finished = acc != run_.accounts.end() && acc->second.finished;
    if (stopped) p.state = Provider::State::Stopped;
    else if (finished || p.done >= p.total) p.state = Provider::State::Done;
    else if (acc != run_.accounts.end() && acc->second.until > now) p.state = Provider::State::Paused;
    else if (s.inFlight) p.state = Provider::State::Testing;
    if (acc != run_.accounts.end()) p.until = acc->second.until;
    if (p.state == Provider::State::Testing || p.state == Provider::State::Waiting) p.now = s.nowText;
    done += p.done;
    total += stopped ? p.done : p.total;
    if (p.state != Provider::State::Stopped && p.state != Provider::State::Done) {
      uint32_t each = s.costN >= 3 ? s.costMs / s.costN : kItemMs;
      uint32_t mine = static_cast<uint32_t>(p.total - p.done) * each;
      if (p.state == Provider::State::Paused) mine += static_cast<uint32_t>(p.until - now) * 1000u;
      left = std::max(left, mine);
      anyLeft = true;
    }
    st.providers.push_back(std::move(p));
  }
  st.fraction = total ? static_cast<double>(done) / static_cast<double>(total) : 0;
  if (anyLeft) st.leftMs = left;
  st.found = foundSoFar();
  return st;
}

std::vector<std::string> Runner::foundSoFar() const {
  bool anyFail = false, archiveBroken = false;
  std::string shortWord;
  bool shortAt720 = true;
  // Each provider's archive depth on each row it was tested on (days; a
  // tenth for one that held two hours but not its edge).
  std::map<std::string, std::map<size_t, double>> depth;
  for (size_t r = 0; r < rows_.size(); ++r) {
    for (const scores::LineupCopy& c : rows_[r].copies) {
      auto live = run_.done.find(key(c.feedId, Kind::Live));
      if (live != run_.done.end()) {
        if (live->second == "f") anyFail = true;
        LiveResult lr = liveResult(live->second);
        std::string word = claimWord(quality::claimed(c.name));
        if (lr.ok && fallsShort(word, lr.height)) {
          if (shortWord.empty() || word == "HD") shortWord = word;
          if (word == "HD" && lr.height < 700) shortAt720 = false;
        }
      }
      if (c.days == 0) continue;
      auto a = run_.done.find(key(c.feedId, Kind::Archive));
      if (a == run_.done.end() || (a->second != "o" && a->second != "f")) continue;
      auto d = run_.done.find(key(c.feedId, Kind::Depth));
      bool edge = d != run_.done.end() && d->second == "o";
      if (a->second == "f") archiveBroken = true;
      double v = a->second == "o" ? (edge ? static_cast<double>(c.days) : 0.1) : 0.0;
      double& best = depth[c.provider][r];
      best = std::max(best, v);
    }
  }
  std::vector<std::string> out;
  if (anyFail) out.emplace_back("Some sources didn't start; they'll be tried last.");
  if (shortWord == "HD")
    out.emplace_back(shortAt720 ? "A few say HD in their name but play at 720p."
                                : "A few say HD in their name but play at a lower resolution.");
  else if (shortWord == "4K")
    out.emplace_back("A few say 4K in their name but play at 1080p or less.");
  // Replay further on one provider than another, on the rows both were
  // tested on.
  std::string further, shorter;
  double bestGap = 0.5 - 1e-9;
  for (const auto& [x, rowsX] : depth)
    for (const auto& [y, rowsY] : depth) {
      if (x == y) continue;
      double sum = 0;
      int n = 0;
      for (const auto& [r, dx] : rowsX) {
        auto dy = rowsY.find(r);
        if (dy == rowsY.end()) continue;
        sum += dx - dy->second;
        ++n;
      }
      if (n == 0) continue;
      double gap = sum / n;
      if (gap > bestGap) {
        bestGap = gap;
        further = x;
        shorter = y;
      }
    }
  if (!further.empty())
    out.push_back("Replay goes back further on " + providerName(further) + " than on " + providerName(shorter) +
                  " for the channels both carry.");
  if (archiveBroken) out.emplace_back("Replay didn't play on some sources that say they keep it.");
  if (out.size() > 3) out.resize(3);
  return out;
}

std::string Runner::summary() const {
  std::vector<std::string> parts;
  for (size_t i = 0; i < plan_.providers.size(); ++i) {
    const std::string& id = plan_.providers[i];
    size_t live = 0, ok = 0, measured = 0, hd = 0, archive = 0, archiveOk = 0;
    std::vector<uint32_t> ms;
    for (const Item& item : plan_.queues[i]) {
      auto d = run_.done.find(key(item.channelId, item.kind));
      if (d == run_.done.end()) continue;
      if (item.kind == Kind::Live) {
        if (d->second == "x") continue;
        ++live;
        LiveResult lr = liveResult(d->second);
        if (!lr.ok) continue;
        ++ok;
        if (lr.ms) ms.push_back(lr.ms);
        if (lr.height > 0) {
          ++measured;
          if (lr.height >= 1070) ++hd;
        }
      } else if (item.kind == Kind::Archive && (d->second == "o" || d->second == "f")) {
        ++archive;
        archiveOk += d->second == "o";
      }
    }
    auto acc = run_.accounts.find(id);
    bool refused = acc != run_.accounts.end() && acc->second.stopped;
    if (live == 0 && archive == 0) {
      if (refused) parts.push_back(providerName(id) + ": nothing tested (stopped: it refused)");
      continue;
    }
    std::string line = providerName(id);
    line += ':';
    if (live) {
      line += " started ";
      line += percent(static_cast<double>(ok) / static_cast<double>(live));
    }
    if (!ms.empty()) {
      line += ", picture ";
      line += secondsText(medianOf(ms));
      line += " median";
    }
    if (measured) {
      line += ", 1080p+ ";
      line += percent(static_cast<double>(hd) / static_cast<double>(measured));
    }
    if (archive) {
      line += ", replay worked ";
      line += percent(static_cast<double>(archiveOk) / static_cast<double>(archive));
    }
    if (refused) line += " (stopped: it refused)";
    parts.push_back(line);
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) out += (i ? "; " : "") + parts[i];
  return out;
}

std::string clocksText(const std::vector<int>& clocks, std::optional<int> server) {
  if (clocks.empty()) return "";
  std::map<int, size_t> counts;
  for (int c : clocks) ++counts[c];
  std::vector<std::pair<int, size_t>> order(counts.begin(), counts.end());
  std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  std::string out;
  if (order.size() == 1) {
    out = sources::xtream::offsetText(order[0].first) + " on all replay channels";
  } else {
    for (size_t i = 0; i < order.size(); ++i) {
      size_t n = order[i].second;
      const char* word = 2 * n > clocks.size() ? "most" : 2 * n == clocks.size() ? "half" : "some";
      if (i) out += ", ";
      out += sources::xtream::offsetText(order[i].first) + " (" + word + ")";
    }
  }
  if (server != order[0].first)
    out += "; server says " + (server ? sources::xtream::offsetText(*server) : std::string("nothing"));
  return out;
}

std::string headerLine(const Runner::Status& s) {
  std::string pct = percent(s.fraction);
  if (s.waitingForPlay) return pct + " \xc2\xb7 paused while you watch";
  if (!s.leftMs) return pct;
  return pct + " \xc2\xb7 " + timeLeftText(*s.leftMs);
}

std::string providerLine(const Runner::Provider& p, time::Seconds now) {
  using State = Runner::Provider::State;
  switch (p.state) {
    case State::Paused: {
      time::Seconds left = std::max<time::Seconds>(60, p.until - now);
      return "It refused a connection. Trying again in " + std::to_string((left + 59) / 60) +
             " min; the others carry on.";
    }
    case State::Stopped: return "It refused again, so it's left out of this test; the others carry on.";
    case State::Done: return "Done";
    case State::Testing:
    case State::Waiting: return p.now.empty() ? std::string("Waiting for its turn") : "Now: " + p.now;
  }
  return "";
}

std::string providerCorner(const Runner::Provider& p) {
  using State = Runner::Provider::State;
  switch (p.state) {
    case State::Paused: return "Paused";
    case State::Stopped: return "Stopped";
    case State::Done: return "Done";
    default: break;
  }
  return percent(p.total ? static_cast<double>(p.done) / static_cast<double>(p.total) : 0.0);
}

}  // namespace marigold::engine::sourcetest
