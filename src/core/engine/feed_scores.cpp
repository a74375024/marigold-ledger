#include "core/engine/feed_scores.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "core/engine/quality.hpp"
#include "core/util/json.hpp"

namespace marigold::engine::scores {
namespace {

double decayFactor(time::Seconds from, time::Seconds to) {
  if (to <= from || from <= 0) return 1.0;
  return std::pow(0.5, static_cast<double>(to - from) / static_cast<double>(kHalfLife));
}

// Weights are saved in hundredths.
int64_t hundredths(double v) { return std::llround(v * 100.0); }
double fromHundredths(const json::Value& v) { return static_cast<double>(v.asInt()) / 100.0; }

std::string percent(double share) {
  return std::to_string(static_cast<int>(std::lround(std::clamp(share, 0.0, 1.0) * 100.0))) + "%";
}

std::string oneDecimal(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.1f", v);
  return buf;
}

uint32_t median(std::vector<uint32_t> v) {
  std::sort(v.begin(), v.end());
  size_t n = v.size();
  return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

}  // namespace

Record Record::at(time::Seconds now) const {
  Record r = *this;
  double f = decayFactor(seen, now);
  if (f < 1.0) {
    r.plays *= f;
    r.failures *= f;
    r.stalls *= f;
    r.playedS *= f;
    r.probes *= f;
    r.probeFailures *= f;
  }
  return r;
}

double Record::starts() const { return plays + failures + kProbeWeight * (probes + probeFailures); }

bool Record::mostlyFails() const {
  return failures + kProbeWeight * probeFailures > plays + kProbeWeight * probes;
}

std::optional<uint32_t> Record::medianStartMs() const {
  if (startMs.empty()) return std::nullopt;
  return median(startMs);
}

std::optional<double> Record::stallsPerHour() const {
  if (playedS < 60) return std::nullopt;
  // Under half an hour played, as half an hour: one stall in a short play
  // isn't twelve an hour.
  return stalls / (std::max(playedS, 1800.0) / 3600.0);
}

Record& Book::touch(const std::string& feedId, time::Seconds now) {
  auto it = byId_.find(feedId);
  if (it == byId_.end()) {
    it = byId_.emplace(feedId, Kept{}).first;
  } else {
    bySeq_.erase(it->second.seq);
    Record decayed = it->second.record.at(now);
    it->second.record = std::move(decayed);
  }
  it->second.seq = ++seq_;
  it->second.record.seen = std::max(it->second.record.seen, now);
  bySeq_[seq_] = feedId;
  while (byId_.size() > kCap && !bySeq_.empty()) {
    if (bySeq_.begin()->second == feedId) break;
    byId_.erase(bySeq_.begin()->second);
    bySeq_.erase(bySeq_.begin());
  }
  return byId_.find(feedId)->second.record;
}

void Book::started(const std::string& feedId, uint32_t ms, time::Seconds now) {
  if (feedId.empty()) return;
  Record& r = touch(feedId, now);
  r.plays += 1;
  r.startMs.push_back(ms);
  if (r.startMs.size() > kSamples) r.startMs.erase(r.startMs.begin(), r.startMs.end() - kSamples);
}

void Book::failed(const std::string& feedId, time::Seconds now) {
  if (feedId.empty()) return;
  touch(feedId, now).failures += 1;
}

void Book::stalled(const std::string& feedId, time::Seconds now) {
  if (feedId.empty()) return;
  touch(feedId, now).stalls += 1;
}

void Book::played(const std::string& feedId, uint32_t ms, time::Seconds now) {
  if (feedId.empty() || ms < 1000) return;
  touch(feedId, now).playedS += static_cast<double>(ms) / 1000.0;
}

void Book::picture(const std::string& feedId, int width, int height, int fps, time::Seconds now) {
  if (feedId.empty() || height <= 0) return;
  Record& r = touch(feedId, now);
  r.width = std::max(0, width);
  r.height = height;
  r.fps = std::max(0, fps);
}

void Book::recordProbe(const std::string& feedId, const Probe& p, time::Seconds now) {
  if (feedId.empty()) return;
  Record& r = touch(feedId, now);
  r.probedAt = now;
  if (p.ok) {
    r.probes += 1;
    r.startMs.push_back(p.startMs);
    if (r.startMs.size() > kSamples) r.startMs.erase(r.startMs.begin(), r.startMs.end() - kSamples);
    if (p.height > 0) {
      r.width = std::max(0, p.width);
      r.height = p.height;
      r.fps = std::max(0, p.fps);
    }
  } else {
    r.probeFailures += 1;
  }
  if (p.archiveOk) {
    r.archiveOk = *p.archiveOk ? 1 : 0;
    r.archiveDays = p.archiveDays;
    r.archiveAt = now;
  }
}

void Book::each(const std::function<void(const std::string&, const Record&)>& fn) const {
  for (const auto& [seq, id] : bySeq_) fn(id, byId_.at(id).record);
}

const Record* Book::find(const std::string& feedId) const {
  auto it = byId_.find(feedId);
  return it == byId_.end() ? nullptr : &it->second.record;
}

double scoreOf(const Record& r) {
  // Starting at all counts most (like the source reliability's (ok + 1) /
  // (ok + fail + 2)); then how fast, how smoothly and how sharp.
  double ok = r.plays + kProbeWeight * r.probes;
  double bad = r.failures + kProbeWeight * r.probeFailures;
  double works = (ok + 1) / (ok + bad + 2);
  double speed = 0.5;
  if (std::optional<uint32_t> m = r.medianStartMs())
    speed = std::clamp(1.0 - (static_cast<double>(*m) - 1500.0) / 8500.0, 0.0, 1.0);
  double smooth = 0.75;
  if (std::optional<double> s = r.stallsPerHour()) smooth = 1.0 / (1.0 + *s / 2.0);
  double sharp = 0.5;
  if (r.height >= 1070) sharp = r.fps >= 49 ? 1.0 : 0.9;
  else if (r.height >= 700) sharp = r.fps >= 49 ? 0.7 : 0.6;
  else if (r.height > 0) sharp = 0.3;
  return 0.55 * works + 0.2 * speed + 0.15 * smooth + 0.1 * sharp;
}

std::optional<double> Book::score(const std::string& feedId, time::Seconds now) const {
  const Record* r = find(feedId);
  if (!r) return std::nullopt;
  Record d = r->at(now);
  if (d.starts() < kEnoughStarts && d.probes + d.probeFailures < kEnoughProbes) return std::nullopt;
  return scoreOf(d);
}

std::string Book::toJson() const {
  json::Value list = json::Value::array();
  for (const auto& [seq, id] : bySeq_) {
    const Record& r = byId_.at(id).record;
    json::Value row = json::Value::array();
    row.push(id);
    row.push(static_cast<int64_t>(r.seen));
    row.push(hundredths(r.plays));
    row.push(hundredths(r.failures));
    row.push(hundredths(r.stalls));
    row.push(static_cast<int64_t>(std::llround(r.playedS)));
    json::Value ms = json::Value::array();
    for (uint32_t m : r.startMs) ms.push(static_cast<int64_t>(m));
    row.push(std::move(ms));
    row.push(r.width);
    row.push(r.height);
    row.push(r.fps);
    row.push(hundredths(r.probes));
    row.push(hundredths(r.probeFailures));
    row.push(r.archiveOk);
    row.push(static_cast<int>(r.archiveDays));
    row.push(static_cast<int64_t>(r.archiveAt));
    row.push(static_cast<int64_t>(r.probedAt));
    list.push(std::move(row));
  }
  json::Value v = json::Value::object();
  v.set("v", 1);
  v.set("rolledUp", static_cast<int64_t>(rolledUpAt));
  v.set("feeds", std::move(list));
  return json::write(v);
}

Book Book::fromJson(std::string_view text) {
  Book b;
  std::optional<json::Value> v = json::parse(text);
  if (!v || !v->isObject() || (*v)["v"].asInt() != 1) return b;
  b.rolledUpAt = (*v)["rolledUp"].asInt();
  const json::Value::Array& rows = (*v)["feeds"].items();
  // The newest kCap (rows are oldest first).
  size_t from = rows.size() > kCap ? rows.size() - kCap : 0;
  for (size_t i = from; i < rows.size(); ++i) {
    const json::Value& row = rows[i];
    if (!row.isArray() || !row[size_t(0)].isString() || row[size_t(0)].asString().empty()) continue;
    Record r;
    r.seen = row[size_t(1)].asInt();
    r.plays = fromHundredths(row[size_t(2)]);
    r.failures = fromHundredths(row[size_t(3)]);
    r.stalls = fromHundredths(row[size_t(4)]);
    r.playedS = static_cast<double>(row[size_t(5)].asInt());
    for (const json::Value& m : row[size_t(6)].items())
      if (r.startMs.size() < kSamples) r.startMs.push_back(static_cast<uint32_t>(std::max<int64_t>(0, m.asInt())));
    r.width = static_cast<int>(row[size_t(7)].asInt());
    r.height = static_cast<int>(row[size_t(8)].asInt());
    r.fps = static_cast<int>(row[size_t(9)].asInt());
    r.probes = fromHundredths(row[size_t(10)]);
    r.probeFailures = fromHundredths(row[size_t(11)]);
    r.archiveOk = static_cast<int>(row[size_t(12)].asInt(-1));
    r.archiveDays = static_cast<uint16_t>(std::clamp<int64_t>(row[size_t(13)].asInt(), 0, 3650));
    r.archiveAt = row[size_t(14)].asInt();
    r.probedAt = row[size_t(15)].asInt();
    std::string id = row[size_t(0)].asString();
    auto old = b.byId_.find(id);
    if (old != b.byId_.end()) b.bySeq_.erase(old->second.seq);
    b.byId_[id] = Kept{std::move(r), ++b.seq_};
    b.bySeq_[b.seq_] = id;
  }
  return b;
}

void orderMeasured(std::vector<std::string>& ids, size_t keep,
                   const std::function<std::optional<double>(const std::string&)>& score) {
  if (!score || keep >= ids.size()) return;
  // The measured copies' places, and the copies in them.
  std::vector<size_t> places;
  std::vector<std::pair<size_t, double>> items;  // (catalog position, score)
  for (size_t i = keep; i < ids.size(); ++i) {
    if (std::optional<double> s = score(ids[i])) {
      places.push_back(i);
      items.emplace_back(i, *s);
    }
  }
  if (items.size() < 2) return;
  // The order stays unless a copy is clearly better. Not a strict ordering,
  // so an insertion sort (as orderedByReliability) rather than std::sort.
  auto before = [](const std::pair<size_t, double>& a, const std::pair<size_t, double>& b) {
    if (std::fabs(a.second - b.second) > kClearlyBetter) return a.second > b.second;
    return a.first < b.first;
  };
  for (size_t i = 1; i < items.size(); ++i)
    for (size_t j = i; j > 0 && before(items[j], items[j - 1]); --j) std::swap(items[j], items[j - 1]);
  std::vector<std::string> moved;
  moved.reserve(items.size());
  for (const auto& item : items) moved.push_back(ids[item.first]);
  for (size_t k = 0; k < places.size(); ++k) ids[places[k]] = std::move(moved[k]);
}

std::string_view providerOf(std::string_view feedId) {
  size_t bar = feedId.find('|');
  return bar == std::string_view::npos ? feedId : feedId.substr(0, bar);
}

namespace {
Rollup rollupOf(const Book& book, const std::vector<std::string>& providers, const std::vector<Row>& rows,
                const std::vector<LineupRow>* lineup, time::Seconds now) {
  Rollup out;
  struct Sums {
    double plays = 0, failures = 0, stalls = 0, playedS = 0, hdPlays = 0, measuredPlays = 0;
    std::vector<uint32_t> startMs;
    // Test all sources: probes too, and the measured copies.
    double tested = 0, testedBad = 0;
    int measuredCopies = 0, hdCopies = 0;
  };
  std::map<std::string, Sums> sums;
  for (const std::string& p : providers) sums[p];
  book.each([&](const std::string& id, const Record& r) {
    auto s = sums.find(std::string(providerOf(id)));
    if (s == sums.end()) return;
    Record d = r.at(now);
    s->second.plays += d.plays;
    s->second.failures += d.failures;
    s->second.stalls += d.stalls;
    s->second.playedS += d.playedS;
    if (d.height > 0) {
      s->second.measuredPlays += d.plays;
      if (d.height >= 1070) s->second.hdPlays += d.plays;
    }
    s->second.startMs.insert(s->second.startMs.end(), d.startMs.begin(), d.startMs.end());
    s->second.tested += d.starts();
    s->second.testedBad += d.failures + kProbeWeight * d.probeFailures;
    if (d.height > 0) {
      ++s->second.measuredCopies;
      if (d.height >= 1070) ++s->second.hdCopies;
    }
  });
  double total = static_cast<double>(rows.size());
  auto share = [total](size_t n) { return total > 0 ? static_cast<double>(n) / total : 0.0; };
  // Per row: which providers have a copy, and which a copy with replay.
  std::vector<std::set<std::string>> has(rows.size()), replays(rows.size());
  for (size_t i = 0; i < rows.size(); ++i)
    for (const RowCopy& c : rows[i]) {
      has[i].insert(c.provider);
      if (c.replay) replays[i].insert(c.provider);
    }
  for (const std::string& p : providers) {
    ProviderFigures f;
    f.provider = p;
    const Sums& s = sums[p];
    f.plays = s.plays;
    if (!s.startMs.empty()) f.medianStartMs = median(s.startMs);
    if (s.plays + s.failures > 0) f.failureRate = s.failures / (s.plays + s.failures);
    if (s.playedS >= 60) f.stallsPerHour = s.stalls / (std::max(s.playedS, 1800.0) / 3600.0);
    if (s.measuredPlays > 0) f.fullHd = s.hdPlays / s.measuredPlays;
    size_t covered = 0, alone = 0, replay = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
      if (has[i].count(p)) {
        ++covered;
        if (has[i].size() == 1) ++alone;
      }
      if (replays[i].count(p)) ++replay;
    }
    f.lineup = share(covered);
    f.only = share(alone);
    f.replay = share(replay);
    // Test all sources: probes and plays together; the archive as the probes found it.
    f.tested = s.tested;
    if (s.tested > 0) f.startFailures = s.testedBad / s.tested;
    if (s.measuredCopies > 0) f.fullHdCopies = static_cast<double>(s.hdCopies) / static_cast<double>(s.measuredCopies);
    if (lineup) {
      size_t works = 0;
      std::vector<uint16_t> depths;
      for (const LineupRow& row : *lineup) {
        bool any = false;
        for (const LineupCopy& c : row.copies) {
          if (c.provider != p || c.days == 0) continue;
          const Record* rec = c.feedId.empty() ? nullptr : book.find(c.feedId);
          if (rec && rec->archiveOk == 0) continue;
          any = true;
          uint16_t days = rec && rec->archiveOk == 1 && rec->archiveDays > 0 ? rec->archiveDays : c.days;
          if (days > 0) depths.push_back(days);
        }
        if (any) ++works;
      }
      f.replayWorks = share(works);
      if (!depths.empty()) {
        std::sort(depths.begin(), depths.end());
        f.replayDays = depths[depths.size() / 2];
      }
    }
    out.providers.push_back(std::move(f));
  }
  for (size_t a = 0; a < providers.size(); ++a)
    for (size_t b = a + 1; b < providers.size(); ++b) {
      PairFigures pf;
      pf.a = providers[a];
      pf.b = providers[b];
      size_t covered = 0, replay = 0;
      for (size_t i = 0; i < rows.size(); ++i) {
        if (has[i].count(pf.a) || has[i].count(pf.b)) ++covered;
        if (replays[i].count(pf.a) || replays[i].count(pf.b)) ++replay;
      }
      pf.lineup = share(covered);
      pf.replay = share(replay);
      out.pairs.push_back(std::move(pf));
    }
  return out;
}

}  // namespace

Rollup rollup(const Book& book, const std::vector<std::string>& providers, const std::vector<Row>& rows,
              time::Seconds now) {
  return rollupOf(book, providers, rows, nullptr, now);
}

Rollup rollupLineup(const Book& book, const std::vector<std::string>& providers, const std::vector<LineupRow>& rows,
                    time::Seconds now) {
  std::vector<Row> plain;
  plain.reserve(rows.size());
  for (const LineupRow& r : rows) {
    Row row;
    for (const LineupCopy& c : r.copies) row.push_back({c.provider, c.days > 0});
    plain.push_back(std::move(row));
  }
  return rollupOf(book, providers, plain, &rows, now);
}

std::vector<std::pair<std::string, std::string>> rollupLines(
    const Rollup& r, const std::function<std::string(const std::string&)>& name) {
  auto nameOf = [&name](const std::string& id) {
    std::string n = name ? name(id) : std::string();
    return n.empty() ? std::string("a provider") : n;
  };
  std::vector<std::pair<std::string, std::string>> out;
  for (const ProviderFigures& f : r.providers) {
    std::vector<std::string> parts;
    if (f.plays <= 0 && !f.failureRate) {
      parts.emplace_back("no plays yet");
    } else {
      parts.push_back("plays " + oneDecimal(f.plays));
      if (f.medianStartMs) parts.push_back("picture " + oneDecimal(*f.medianStartMs / 1000.0) + " s median");
      if (f.failureRate) parts.push_back("start failures " + percent(*f.failureRate));
      if (f.stallsPerHour) parts.push_back("stalls " + oneDecimal(*f.stallsPerHour) + "/h");
      if (f.fullHd) parts.push_back("1080p+ " + percent(*f.fullHd));
    }
    std::string detail;
    for (size_t i = 0; i < parts.size(); ++i) detail += (i ? ", " : "") + parts[i];
    detail += "; lineup " + percent(f.lineup) + ", only here " + percent(f.only) + ", replay " + percent(f.replay);
    out.emplace_back(nameOf(f.provider), std::move(detail));
  }
  for (const PairFigures& p : r.pairs)
    out.emplace_back(nameOf(p.a) + " + " + nameOf(p.b),
                     "together: lineup " + percent(p.lineup) + ", replay " + percent(p.replay));
  return out;
}

Cells cellsOf(const Record* r, time::Seconds now) {
  Cells c;
  if (!r) return c;
  Record d = r->at(now);
  if (std::optional<uint32_t> m = d.medianStartMs()) c.startMs = std::to_string(*m);
  c.failures = oneDecimal(d.failures + kProbeWeight * d.probeFailures);
  if (std::optional<double> s = d.stallsPerHour()) c.stallsPerHour = oneDecimal(*s);
  if (d.height > 0) {
    quality::Picture p;
    p.height = d.height;
    p.fps = d.fps;
    c.picture = quality::text(p);
  }
  c.plays = oneDecimal(d.plays);
  return c;
}

}  // namespace marigold::engine::scores
