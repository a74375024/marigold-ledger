// Settings › Help & info › Send a report to your phone: sharing a report
// on the home network (tvOS LiveTVExportView). Build 144: in place, in
// Settings' right column, instead of a card over everything.
#include "app/screens/report_share.hpp"

#include <algorithm>
#include <cstdio>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/report/share.hpp"
#include "core/util/strings.hpp"

namespace marigold::app {

using ui::Weight;
namespace theme = ui::theme;

const char* reportName(ExportKind kind) {
  switch (kind) {
    case ExportKind::ChannelReport: return "Channel report";
    case ExportKind::PlaybackLog: return "Playback log";
    case ExportKind::LiveEvents: return "Live Events report";
  }
  return "";
}

ReportShare::~ReportShare() { share_.stop(); }

void ReportShare::toggle(ExportKind kind) {
  if (kind_ == kind && (building_ || sharing())) stop();
  else start(kind);
}

void ReportShare::stop() {
  share_.stop();
  status_ = platform::LanShare::Status{};
  qr_.reset();
  problem_.clear();
  building_ = false;
  kind_.reset();
  ++serial_;
}

void ReportShare::start(ExportKind kind) {
  stop();
  kind_ = kind;
  if (kind == ExportKind::PlaybackLog) {
    serve(app_.playback().log().csv(app_.zone()));
    return;
  }
  building_ = true;
  std::weak_ptr<bool> alive = alive_;
  uint64_t serial = serial_;
  auto built = [this, alive, serial](std::shared_ptr<std::string> csv) {
    if (alive.expired() || serial != serial_) return;
    building_ = false;
    if (!csv) {
      problem_ = "Couldn't build the report.";
      app_.invalidate();
      return;
    }
    serve(std::move(*csv));
  };
  if (kind == ExportKind::LiveEvents) app_.store().buildLiveEventsReport(built);
  else app_.store().buildChannelReport(built);
}

void ReportShare::serve(std::string body) {
  platform::LanShare::Options opts;
  opts.filename = std::string(kind_ == ExportKind::PlaybackLog  ? report::share::kPlaybackLogFile
                              : kind_ == ExportKind::LiveEvents ? report::share::kLiveEventsFile
                                                                : report::share::kChannelsFile);
  size_t bytes = body.size();
  status_ = share_.start(std::move(body), opts);
  if (sharing()) {
    qr_ = report::qrEncode(status_.url);
  } else {
    qr_.reset();
    problem_ = bytes > report::share::kMaxBytes
                   ? "The file is too big to share (over 96 MB)."
                   : "Couldn't share on the home network. Check that the TV is connected to Wi-Fi or Ethernet.";
  }
  app_.invalidate();
}

std::string ReportShare::timeLeft() const {
  int m = status_.secondsLeft / 60, s = status_.secondsLeft % 60;
  char left[32];
  std::snprintf(left, sizeof left, "%d:%02d", m, s);
  return left;
}

std::string ReportShare::rowLine() const {
  if (building_) return "Building the report\xe2\x80\xa6";
  if (sharing()) return "Sharing now \xc2\xb7 " + timeLeft() + " left";
  if (!problem_.empty()) return "Couldn't share it";
  return "";
}

bool ReportShare::tick(uint32_t ms) {
  wake_.clear();
  bool changed = false;
  if (kind_ && !building_ && problem_.empty()) {
    platform::LanShare::Status st = share_.status();
    changed = st.state != status_.state || st.secondsLeft != status_.secondsLeft || st.downloads != status_.downloads;
    bool expired = status_.state == platform::LanShare::State::Ready && st.state != platform::LanShare::State::Ready;
    status_ = st;
    // The link's ten minutes are up: back to a plain row.
    if (expired) stop();
  }
  // Building the report: its spinner moves every 125 ms, asleep in between.
  if (building_ && spinner_.tick(ms, wake_)) changed = true;
  return changed;
}

void ReportShare::drawInfo(ui::Ctx& c, float x, float y, float w) const {
  if (!kind_) return;
  ui::label(c, str::upperAscii(reportName(*kind_)), x, y, Weight::Bold, 19, theme::white(0.5f), w);
  y += 40;
  if (building_) {
    ui::spinner(c, x + 14, y + 16, 14);
    ui::label(c, "Building the report\xe2\x80\xa6", x + 44, y + 2, Weight::Medium, 22, theme::white(0.85f), w - 44);
    return;
  }
  if (!problem_.empty()) {
    ui::paragraph(c, problem_, x, y, w, Weight::Medium, 21, theme::hex(theme::kWarning), 4, 1.4f);
    return;
  }
  if (!sharing() || !qr_) return;
  float qs = std::min(w, 332.0f);
  setup_ui::qr(c, {x, y, qs, qs}, *qr_);
  y += qs + 24;
  y += ui::paragraph(c, "On a phone on the same Wi-Fi, scan the code or open", x, y, w, Weight::Regular, 20,
                     theme::white(0.75f), 2, 1.4f) +
       8;
  for (const std::string& line : c.t.wrap(status_.url, Weight::Bold, 22, w, 3)) {
    ui::label(c, line, x, y, Weight::Bold, 22, theme::white(), w);
    y += 30;
  }
  y += 10;
  std::string left = "The link works for " + timeLeft() + ".";
  if (status_.downloads > 0)
    left += " Downloaded " + std::to_string(status_.downloads) + (status_.downloads == 1 ? " time." : " times.");
  y += ui::paragraph(c, left, x, y, w, Weight::Medium, 20, theme::white(0.75f), 2, 1.4f) + 6;
  ui::paragraph(c, "Only devices on your home network can open it.", x, y, w, Weight::Regular, 18, theme::white(0.55f),
                2, 1.35f);
}

}  // namespace marigold::app
