// Options › Help › Export Channel Report / Export Playback Log / Export Live
// Events Report (tvOS
// LiveTVExportView). The TV serves the file on the home network for 10
// minutes: a QR code and a link to open on a phone or computer on the same
// Wi-Fi. The link is shown on screen only, never logged.
#include <algorithm>
#include <cmath>
#include <optional>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/report/qr.hpp"
#include "core/report/share.hpp"
#include "core/util/strings.hpp"
#include "platform/lan_share.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;

class ExportScreen : public Screen {
 public:
  ExportScreen(App& app, ExportKind kind) : Screen(app), kind_(kind) {}
  ~ExportScreen() override { share_.stop(); }

  bool opaque() const override { return false; }

  void shown() override {
    if (started_) return;
    started_ = true;
    start();
  }

  bool tick(uint32_t) override {
    platform::LanShare::Status st = share_.status();
    bool changed = st.state != status_.state || st.secondsLeft != status_.secondsLeft || st.downloads != status_.downloads;
    if (st.state != status_.state && st.state != platform::LanShare::State::Ready) qr_.reset();
    status_ = st;
    return changed || building_;
  }

  bool key(const KeyEvent& e) override {
    int buttons = static_cast<int>(buttonTitles().size());
    switch (e.key) {
      case Key::Left:
        if (button_ > 0) --button_;
        return true;
      case Key::Right:
        if (button_ + 1 < buttons) ++button_;
        return true;
      case Key::Ok: press(); return true;
      case Key::Back: return false;
      default: return true;
    }
  }

  void hover(int id) override {
    if (id >= 1 && id <= static_cast<int>(buttonTitles().size())) button_ = id - 1;
  }

  void draw(ui::Ctx& c) override {
    ui::scrim(c, 0.7f);
    Rect card{(gfx::kLayoutWidth - 1240) * 0.5f, 150, 1240, 780};
    c.g.fill(card, theme::hex(0x1C1F25, 0.98f), 34);
    float x = card.x + 60, y = card.y + 50, w = card.w - 120;
    bool log = kind_ == ExportKind::PlaybackLog, events = kind_ == ExportKind::LiveEvents;
    ui::label(c, log ? "Export Playback Log" : events ? "Export Live Events Report" : "Export Channel Report", x, y,
              Weight::Bold, 40, theme::white(), w);
    y += 64;
    std::string about = log ? "What happened each time a channel was tuned: feeds tried, stalls, reconnects and errors "
                              "(no stream links). " + std::to_string(app_.playback().log().size()) + " events."
                        : events ? "A spreadsheet of every event in your provider's event channels: what it was read "
                                   "as, and the tile it's on or why it was left out. No links or server addresses."
                                 : "A spreadsheet of every channel: how it was sorted, its feeds, and where its listings "
                                   "come from. No passwords, links or server addresses.";
    y += ui::paragraph(c, about, x, y, w, Weight::Regular, 22, theme::white(0.7f), 3) + 24;

    using State = platform::LanShare::State;
    if (building_) {
      ui::spinner(c, x + 20, y + 40, 18);
      ui::label(c, "Building the report\xe2\x80\xa6", x + 56, y + 26, Weight::Medium, 24, theme::white(0.85f));
    } else if (status_.state == State::Ready) {
      float qs = 360;
      drawQr(c, {x, y, qs, qs});
      float tx = x + qs + 50, tw = w - qs - 50;
      ui::label(c, "On a phone or computer on the same Wi-Fi, scan the code or open:", tx, y + 6, Weight::Regular, 22,
                theme::white(0.75f), tw);
      y += 44;
      for (const std::string& line : c.t.wrap(status_.url, Weight::Bold, 26, tw, 3)) {
        ui::label(c, line, tx, y, Weight::Bold, 26, theme::white());
        y += 36;
      }
      y += 16;
      int m = status_.secondsLeft / 60, s = status_.secondsLeft % 60;
      char left[32];
      std::snprintf(left, sizeof left, "%d:%02d", m, s);
      std::string line = std::string("The link works for ") + left + ".";
      if (status_.downloads > 0)
        line += " Downloaded " + std::to_string(status_.downloads) + (status_.downloads == 1 ? " time." : " times.");
      ui::label(c, line, tx, y, Weight::Medium, 22, theme::white(0.75f), tw);
      y += 36;
      ui::label(c, "Only devices on your home network can open it.", tx, y, Weight::Regular, 20, theme::white(0.55f), tw);
    } else {
      std::string text = !problem_.empty() ? problem_
                         : status_.state == State::Stopped && sharedOnce_ ? "Sharing stopped."
                                                                           : "Not shared yet.";
      ui::paragraph(c, text, x, y + 10, w, Weight::Medium, 24, theme::white(0.85f), 3);
    }

    // Buttons.
    std::vector<std::string> titles = buttonTitles();
    float bx = x, by = card.y + card.h - 110;
    for (size_t i = 0; i < titles.size(); ++i) {
      float bw = ui::chipWidth(c, titles[i], 24) + 40;
      Rect pill{bx, by, bw, 66};
      c.hit(pill, 1 + static_cast<int>(i));
      bool f = static_cast<int>(i) == button_;
      c.g.fill(pill, f ? theme::white() : theme::white(0.12f), 33);
      ui::labelIn(c, titles[i], pill, Weight::Bold, 24, f ? theme::black() : theme::white(), Align::Center);
      bx += bw + 16;
    }
  }

 private:
  std::vector<std::string> buttonTitles() const {
    std::vector<std::string> out;
    if (status_.state == platform::LanShare::State::Ready) out.push_back("Stop Sharing");
    else if (!building_) out.push_back("Share Again");
    out.push_back("Done");
    if (kind_ == ExportKind::PlaybackLog && !building_) out.push_back("Clear Log");
    return out;
  }

  void press() {
    std::vector<std::string> titles = buttonTitles();
    if (button_ >= static_cast<int>(titles.size())) return;
    const std::string& t = titles[static_cast<size_t>(button_)];
    if (t == "Done") {
      app_.nav().pop();
    } else if (t == "Stop Sharing") {
      share_.stop();
      qr_.reset();
    } else if (t == "Share Again") {
      start();
    } else if (t == "Clear Log") {
      share_.stop();
      qr_.reset();
      app_.playback().clearLog();
      app_.toast("Playback log cleared");
    }
    button_ = std::min(button_, static_cast<int>(buttonTitles().size()) - 1);
  }

  void start() {
    problem_.clear();
    if (kind_ == ExportKind::PlaybackLog) {
      serve(app_.playback().log().csv(app_.zone()));
      return;
    }
    building_ = true;
    std::weak_ptr<bool> alive = alive_;
    auto built = [this, alive](std::shared_ptr<std::string> csv) {
      if (alive.expired()) return;
      building_ = false;
      if (!csv) {
        problem_ = "Couldn't build the report.";
        return;
      }
      serve(std::move(*csv));
    };
    if (kind_ == ExportKind::LiveEvents) app_.store().buildLiveEventsReport(built);
    else app_.store().buildChannelReport(built);
  }

  void serve(std::string body) {
    platform::LanShare::Options opts;
    opts.filename = std::string(kind_ == ExportKind::PlaybackLog  ? report::share::kPlaybackLogFile
                                : kind_ == ExportKind::LiveEvents ? report::share::kLiveEventsFile
                                                                  : report::share::kChannelsFile);
    size_t bytes = body.size();
    status_ = share_.start(std::move(body), opts);
    sharedOnce_ = true;
    if (status_.state == platform::LanShare::State::Ready) {
      qr_ = report::qrEncode(status_.url);
    } else {
      qr_.reset();
      problem_ = bytes > report::share::kMaxBytes
                     ? "The file is too big to share (over 96 MB)."
                     : "Couldn't share on the home network. Check that the TV is connected to Wi-Fi or Ethernet.";
    }
    app_.invalidate();
  }

  void drawQr(ui::Ctx& c, Rect r) {
    if (qr_) setup_ui::qr(c, r, *qr_);
    else c.g.fill(r, theme::white(), 16);
  }

  ExportKind kind_;
  platform::LanShare share_;
  platform::LanShare::Status status_;
  std::optional<report::QrMatrix> qr_;
  std::string problem_;
  bool started_ = false, building_ = false, sharedOnce_ = false;
  int button_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace

std::unique_ptr<Screen> makeExport(App& app, ExportKind kind) { return std::make_unique<ExportScreen>(app, kind); }

}  // namespace marigold::app
