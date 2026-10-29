// First-run setup, the screens that aren't My Lineup's or My Sports': the
// welcome (the provider from the phone or with the remote), "Scan with your
// phone" (the TV serves the provider form to a phone on the home network;
// core/net/phone_setup) and "You're all set". The order is Provider, Area,
// Languages, Channels, Sports, Teams, Done (setup_ui::Step); Back goes to
// the step before.
#include <algorithm>
#include <memory>

#include "app/app.hpp"
#include "app/screens/guide_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/screens/setup_ui.hpp"
#include "app/store/store.hpp"
#include "core/input/hints.hpp"
#include "core/net/phone_setup.hpp"
#include "core/report/qr.hpp"
#include "core/sports/my_sports.hpp"
#include "platform/lan_server.hpp"

namespace marigold::app {
namespace {

using ui::Align;
using ui::Color;
using ui::Rect;
using ui::Weight;
namespace theme = ui::theme;
using setup_ui::Step;
using setup_ui::kLeft;

constexpr int kHitCard = 10, kHitButton = 20;

// A phone and a remote, drawn from rectangles.
void iconPhone(ui::Ctx& c, float cx, float cy, Color ink) {
  c.g.stroke({cx - 11, cy - 20, 22, 40}, ink, 5, 2.6f);
  c.g.fill({cx - 4, cy + 13, 8, 2.6f}, ink, 1.3f);
}
void iconRemote(ui::Ctx& c, float cx, float cy, Color ink) {
  c.g.stroke({cx - 9, cy - 21, 18, 42}, ink, 8, 2.6f);
  c.g.stroke({cx - 4.5f, cy - 13, 9, 9}, ink, 4.5f, 2.2f);
  c.g.fill({cx - 4, cy + 3, 8, 2.4f}, ink, 1.2f);
  c.g.fill({cx - 4, cy + 10, 8, 2.4f}, ink, 1.2f);
}

// ------------------------------------------------------------------ welcome

class Welcome : public Screen {
 public:
  explicit Welcome(App& app) : Screen(app) {}

  bool key(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Up: focus_ = 0; return true;
      case Key::Down: focus_ = 1; return true;
      case Key::Ok:
        if (focus_ == 0) app_.nav().push(makePhoneSignIn(app_, true));
        else app_.nav().push(makeAccounts(app_, true));
        return true;
      default: return e.key != Key::Back;  // Back here leaves the app
    }
  }

  void hover(int id) override {
    if (id == kHitCard || id == kHitCard + 1) focus_ = id - kHitCard;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    float y = setup_ui::heading(c, "MARIGOLD", "Welcome",
                                "Live TV and every game, in one guide. First, add the provider your channels come from.",
                                780, 110);
    ui::paragraph(c, "Xtream, Emby / Jellyfin or an M3U playlist. You can add more later in Options.", kLeft, y + 10, 780,
                  Weight::Regular, 22, theme::white(0.55f), 2);
    struct Card {
      const char* title;
      const char* detail;
      bool tag;
    };
    const Card cards[] = {
        {"Add it from your phone", "Scan a code and type the server and password on your phone. The rest of setup stays on the TV.", true},
        {"Use the remote", "Type each field with the TV keyboard.", false},
    };
    float cy = 230;
    for (int i = 0; i < 2; ++i) {
      Rect r{960, cy, 880, 190};
      c.hit(r, kHitCard + i);
      bool f = focus_ == i;
      c.g.fill(r, f ? theme::white() : theme::hex(0x1C1F25), 24);
      Color ink = f ? theme::black() : theme::white();
      Rect icon{r.x + 34, r.y + 53, 84, 84};
      c.g.fill(icon, f ? theme::black(0.08f) : theme::white(0.1f), 22);
      if (i == 0) iconPhone(c, icon.x + 42, icon.y + 42, ink);
      else iconRemote(c, icon.x + 42, icon.y + 42, ink);
      float tx = r.x + 150;
      float tw = ui::label(c, cards[i].title, tx, r.y + 44, Weight::Bold, 32, ink, 560);
      if (cards[i].tag) ui::chip(c, "EASIEST", tx + tw + 14, r.y + 48, 16, theme::hex(theme::kToggleOn), theme::white());
      ui::paragraph(c, cards[i].detail, tx, r.y + 94, r.w - 190, Weight::Regular, 22, f ? theme::black(0.65f) : theme::white(0.65f), 2);
      cy += 214;
    }
    setup_ui::stepBar(c, Step::Provider);
    setup_ui::keys(c, app_.remoteKeys(), false);
  }

 private:
  int focus_ = 0;
};

// -------------------------------------------------------- phone sign-in

class PhoneSignIn : public Screen {
 public:
  PhoneSignIn(App& app, bool firstRun) : Screen(app), firstRun_(firstRun) {}
  ~PhoneSignIn() override { server_.stop(); }

  void shown() override {
    if (!started_) start();
  }

  bool tick(uint32_t ms) override {
    bool changed = false;
    platform::LanServer::Status st = server_.status();
    if (st.state != serverStatus_.state || st.secondsLeft != serverStatus_.secondsLeft) changed = true;
    serverStatus_ = st;
    if (session_) {
      phone::Status ps = session_->status();
      if (ps.phase != phase_.phase || ps.code != phase_.code) changed = true;
      phase_ = ps;
      if (std::optional<phone::ProviderForm> form = session_->take()) {
        signIn(*form);
        changed = true;
      }
    }
    if (signedIn_) {
      // Channels come in on the store's workers; carry on once they're
      // downloaded (setup goes on while they're sorted).
      const StoreStatus& status = app_.store().status();
      size_t received = status.received ? status.received : app_.store().channels()->rawChannels;
      if (received != channels_) {
        channels_ = received;
        changed = true;
      }
      bool loaded = channels_ > 0 && (status.received > 0 || status.phase == StoreStatus::Phase::Ready);
      if (loaded && !loaded_) {
        loaded_ = true;
        continueAt_ = ms + 1500;
        changed = true;
      }
      if (loaded_ && continueAt_ && ms >= continueAt_) {
        continueAt_ = 0;
        next();
        return true;
      }
    }
    return changed || busy_ || (signedIn_ && !loaded_) || continueAt_ != 0;
  }

  bool key(const KeyEvent& e) override {
    std::vector<std::string> titles = buttonTitles();
    int n = static_cast<int>(titles.size());
    switch (e.key) {
      case Key::Left: button_ = std::max(0, button_ - 1); return true;
      case Key::Right: button_ = std::min(n - 1, button_ + 1); return true;
      case Key::Ok:
        if (button_ < n) press(titles[static_cast<size_t>(button_)]);
        return true;
      case Key::Back:
        if (busy_) return true;
        return false;  // back to the welcome (or Providers)
      default: return true;
    }
  }

  void hover(int id) override {
    if (id >= kHitButton && id < kHitButton + 4) button_ = id - kHitButton;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    std::string kick = firstRun_ ? setup_ui::kicker(Step::Provider) : "OPTIONS \xc2\xb7 PROVIDERS";
    if (busy_ || signedIn_) drawProgress(c, kick);
    else drawScan(c, kick);
    if (firstRun_) setup_ui::stepBar(c, Step::Provider);
    setup_ui::keys(c, app_.remoteKeys(), false);
  }

 private:
  using State = platform::LanServer::State;

  void start() {
    started_ = true;
    server_.stop();
    problem_.clear();
    std::string token = platform::randomHex(phone::kTokenBytes);
    if (token.empty()) {
      setUnavailable();
      return;
    }
    session_ = std::make_shared<phone::Session>(token, [](size_t n) { return platform::randomHex(n); }, "your TV");
    std::shared_ptr<phone::Session> s = session_;
    platform::LanServer::Options o;
    o.lifetimeSeconds = phone::kLifetimeSeconds;
    serverStatus_ = server_.start([s](const lan::Request& r) { return s->handle(r); }, o);
    phase_ = session_->status();
    if (serverStatus_.state != State::Ready) {
      setUnavailable();
      return;
    }
    // The link is on screen and in the QR code only, never in the log.
    qr_ = report::qrEncode(serverStatus_.base() + session_->path());
    button_ = 0;
  }

  void setUnavailable() {
    qr_.reset();
    problem_ = "Couldn't start on the home network. Check that the TV is connected to Wi-Fi or Ethernet, or use the remote.";
  }

  void signIn(const phone::ProviderForm& f) {
    AccountForm a;
    a.kind = f.kind == phone::ProviderForm::Kind::Emby  ? AccountForm::Kind::Emby
             : f.kind == phone::ProviderForm::Kind::M3U ? AccountForm::Kind::M3U
                                                        : AccountForm::Kind::Xtream;
    a.name = f.name;
    a.server = f.server;
    a.user = f.user;
    a.password = f.password;
    a.guideUrl = f.guide;
    busy_ = true;
    problem_.clear();
    providerName_ = !f.name.empty() ? f.name : "your provider";
    std::weak_ptr<bool> alive = alive_;
    std::shared_ptr<phone::Session> s = session_;
    App& app = app_;
    app_.store().addAccount(a, [this, alive, s, &app](std::string problem) {
      if (alive.expired()) return;
      busy_ = false;
      if (!problem.empty()) {
        problem_ = problem;
        if (s) s->failed(problem, phone::fieldsForProblem(problem));
        return;
      }
      std::vector<AccountSummary> accounts = app.store().accounts();
      if (!accounts.empty()) providerName_ = accounts.back().name;
      signedIn_ = true;
      if (s) s->signedIn(providerName_, 0);
      app.invalidate();
    });
  }

  void next() {
    server_.stop();
    if (firstRun_) {
      app_.nav().replaceAll(makeLineupSetup(app_, true));
    } else {
      app_.toast("Provider added");
      app_.nav().pop();
    }
  }

  std::vector<std::string> buttonTitles() const {
    if (signedIn_) return {"Continue"};
    if (busy_) return {};
    std::vector<std::string> out = {"Use the remote instead"};
    if (serverStatus_.state == State::Ready) out.push_back("New code");
    else out.push_back("Try again");
    return out;
  }

  void press(const std::string& t) {
    if (t == "Continue") {
      next();
    } else if (t == "Use the remote instead") {
      server_.stop();
      if (firstRun_) {
        app_.nav().pop();
        app_.nav().push(makeAccounts(app_, true));
      } else {
        app_.nav().pop();
      }
    } else if (t == "New code" || t == "Try again") {
      start();
    }
  }

  void drawScan(ui::Ctx& c, const std::string& kick) {
    setup_ui::heading(c, kick, "Scan with your phone", "", 1400);
    bool ready = serverStatus_.state == State::Ready && qr_;
    Rect box{kLeft, 290, 460, 460};
    if (ready) {
      setup_ui::qr(c, box, *qr_);
      std::string address = serverStatus_.host + ":" + std::to_string(serverStatus_.port);
      ui::label(c, "No camera? Open " + address, kLeft, box.y + box.h + 26, Weight::Medium, 22, theme::white(0.7f), 560);
      float w = ui::label(c, "and enter ", kLeft, box.y + box.h + 60, Weight::Medium, 22, theme::white(0.7f));
      ui::label(c, phone::spacedCode(phase_.code), kLeft + w, box.y + box.h + 54, Weight::Bold, 30, theme::white());
    } else {
      c.g.fill(box, theme::white(0.06f), 24);
      ui::paragraph(c, problem_.empty() ? "The code expired." : problem_, box.x + 30, box.y + 170, box.w - 60,
                    Weight::Medium, 24, theme::white(0.75f), 5);
    }

    float x = 640, y = 300, w = 1180;
    struct Line {
      const char* title;
      const char* detail;
    };
    const Line lines[] = {
        {"Point your phone's camera at the code", "Your phone must be on the same Wi-Fi as this TV."},
        {"Fill in your provider on the page that opens",
         "Paste the server address and password straight from your messages or password manager."},
        {"Tap Send to TV", firstRun_ ? "The TV signs in by itself. Everything after that happens here, on the TV."
                                     : "The TV signs in by itself."},
    };
    for (int i = 0; i < 3; ++i) {
      Rect num{x, y, 46, 46};
      c.g.fill(num, theme::white(0.12f), 23);
      ui::labelIn(c, std::to_string(i + 1), num, Weight::Bold, 22, theme::white(), Align::Center);
      ui::label(c, lines[i].title, x + 66, y + 2, Weight::Medium, 26, theme::white(), w - 66);
      ui::label(c, lines[i].detail, x + 66, y + 40, Weight::Regular, 21, theme::white(0.55f), w - 66);
      y += 104;
    }
    y += 10;
    if (phase_.phase == phone::Phase::Failed && !problem_.empty()) {
      Rect bad{x, y, w - 60, 100};
      c.g.fill(bad, theme::hex(theme::kWatching, 0.18f), 18);
      ui::label(c, "Couldn't sign in", bad.x + 24, bad.y + 16, Weight::Bold, 24, theme::white());
      ui::label(c, problem_ + " Fix it on your phone: the page is still open.", bad.x + 24, bad.y + 56, Weight::Regular, 21,
                theme::white(0.8f), bad.w - 48);
      y += 124;
    } else if (ready) {
      std::string pill = phase_.phase == phone::Phase::Connected ? "Phone connected: fill in your provider"
                                                                 : "Waiting for your phone\xe2\x80\xa6";
      float pw = ui::chipWidth(c, pill, 22, Weight::Medium) + 70;
      Rect r{x, y, pw, 52};
      c.g.fill(r, theme::white(0.1f), 26);
      c.g.fill({r.x + 22, r.y + 19, 14, 14},
               phase_.phase == phone::Phase::Connected ? theme::hex(theme::kToggleOn) : theme::white(0.5f), 7);
      ui::labelIn(c, pill, {r.x + 48, r.y, r.w - 56, r.h}, Weight::Medium, 22, theme::white());
      int m = serverStatus_.secondsLeft / 60, s = serverStatus_.secondsLeft % 60;
      char left[48];
      std::snprintf(left, sizeof left, "This code works once, for %d:%02d more.", m, s);
      ui::label(c, left, r.x + r.w + 20, r.y + 14, Weight::Regular, 20, theme::white(0.55f));
      y += 80;
    } else {
      y += 10;
    }
    setup_ui::buttons(c, x, y, buttonTitles(), button_, kHitButton);
  }

  void drawProgress(ui::Ctx& c, const std::string& kick) {
    setup_ui::heading(c, kick, "Got it from your phone", "", 1400);
    float x = kLeft, y = 300, w = 900;
    auto row = [&](int state, const std::string& title, const std::string& detail) {
      // state: 0 working, 1 done
      Rect r{x, y, w, detail.empty() ? 72.0f : 84.0f};
      c.g.fill(r, theme::white(0.07f), 16);
      Rect dot{r.x + 24, r.y + r.h * 0.5f - 20, 40, 40};
      if (state == 1) {
        c.g.fill(dot, theme::hex(theme::kToggleOn), 20);
        ui::labelIn(c, "\xe2\x9c\x93", dot, Weight::Bold, 22, theme::white(), Align::Center);
      } else {
        ui::spinner(c, dot.x + 20, dot.y + 20, 15);
      }
      ui::label(c, title, r.x + 86, r.y + (detail.empty() ? 21 : 14), Weight::Bold, 25, theme::white(), w - 110);
      if (!detail.empty()) ui::label(c, detail, r.x + 86, r.y + 48, Weight::Regular, 19, theme::white(0.6f), w - 110);
      y += r.h + 20;
    };
    row(1, "Phone connected", "");
    row(signedIn_ ? 1 : 0, signedIn_ ? "Signed in to " + providerName_ : "Signing in to " + providerName_ + "\xe2\x80\xa6", "");
    if (signedIn_) {
      row(loaded_ ? 1 : 0, loaded_ ? setup_ui::grouped(channels_) + " channels received" : "Getting your channel list",
          loaded_ ? "" : "From " + providerName_);
    }
    float rx = 1080, ry = 300;
    ui::label(c, "If it doesn't work", rx, ry, Weight::Bold, 24, theme::white());
    ry += 44;
    ry += ui::paragraph(c,
                        "The TV says why, and the phone shows the same message next to the field to fix. The phone "
                        "page stays open until it works or the 10 minutes are up.",
                        rx, ry, 760, Weight::Regular, 21, theme::white(0.6f), 4) +
          30;
    std::vector<std::string> titles = buttonTitles();
    if (!titles.empty()) {
      setup_ui::buttons(c, rx, ry, titles, button_, kHitButton);
      ui::label(c, firstRun_ ? "Moves on to Area by itself when the channel list is in." : "Closes by itself when the channel list is in.",
                rx, ry + 90, Weight::Regular, 20, theme::white(0.55f), 760);
    }
  }

  bool firstRun_;
  bool started_ = false, busy_ = false, signedIn_ = false, loaded_ = false;
  uint32_t continueAt_ = 0;
  size_t channels_ = 0;
  int button_ = 0;
  std::string problem_, providerName_;
  platform::LanServer server_;
  platform::LanServer::Status serverStatus_;
  std::shared_ptr<phone::Session> session_;
  phone::Status phase_;
  std::optional<report::QrMatrix> qr_;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// --------------------------------------------------------------- done

class SetupDone : public Screen {
 public:
  explicit SetupDone(App& app) : Screen(app) {}

  bool tick(uint32_t) override {
    auto ch = app_.store().channels();
    bool ready = ch->lineupConfigured;
    bool changed = ready != ready_ || ch->lineup.size() != lineup_;
    ready_ = ready;
    lineup_ = ch->lineup.size();
    return changed || !ready_;
  }

  bool key(const KeyEvent& e) override {
    switch (e.key) {
      case Key::Left: button_ = 0; return true;
      case Key::Right: button_ = 1; return true;
      case Key::Ok: finish(button_ == 1); return true;
      default: return e.key != Key::Back;  // Back: the teams
    }
  }

  void hover(int id) override {
    if (id == kHitButton || id == kHitButton + 1) button_ = id - kHitButton;
  }

  void draw(ui::Ctx& c) override {
    c.g.fill({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight}, theme::hex(theme::kBackground));
    float y = setup_ui::heading(c, setup_ui::kicker(Step::Done), "You're all set", "", 1400) + 20;
    // Today's games on your channels, and your teams' next one.
    std::shared_ptr<const EventsBoard> board = app_.store().events();
    time::Seconds now = time::now();
    time::Civil local = app_.zone().toLocal(now);
    time::Seconds endOfDay = now + (24 * 3600 - (local.hour * 3600 + local.minute * 60 + local.second));
    int games = 0;
    const EventTile* nextTeam = nullptr;
    for (const EventRow& row : board->rows) {
      if (row.id.rfind("sport|", 0) != 0 && row.id != "more") continue;
      for (const EventGroup& g : row.groups)
        for (const EventTile& t : g.tiles) {
          bool today = !t.finished && (!t.upcoming || (t.start > 0 && t.start < endOfDay));
          if (t.watchable && today) ++games;
          if (t.favorite && t.upcoming && t.start > now && (!nextTeam || t.start < nextTeam->start)) nextTeam = &t;
        }
    }
    const sports::MySportsPrefs& my = app_.store().mySports();
    struct Stat {
      std::string value, label;
    };
    std::vector<Stat> stats;
    stats.push_back({ready_ ? std::to_string(lineup_) : "\xe2\x80\xa6", ready_ ? "channels in My Lineup" : "building My Lineup"});
    if (games > 0) stats.push_back({std::to_string(games), "games on your channels today"});
    if (!my.teams.empty()) {
      std::string l = my.teams.size() == 1 ? "favorite team" : "favorite teams";
      if (nextTeam) l += " \xc2\xb7 next: " + (nextTeam->heroTitle.empty() ? nextTeam->title : nextTeam->heroTitle) + ", " +
                         ui::clock(c, nextTeam->start);
      stats.push_back({std::to_string(my.teams.size()), l});
    }
    float sw = (1760 - 24 * static_cast<float>(stats.size() - 1)) / static_cast<float>(stats.size());
    float sx = kLeft;
    for (const Stat& s : stats) {
      Rect r{sx, y, sw, 150};
      c.g.fill(r, theme::hex(0x1C1F25), 22);
      ui::label(c, s.value, r.x + 30, r.y + 22, Weight::Bold, 54, theme::white());
      ui::label(c, s.label, r.x + 30, r.y + 98, Weight::Regular, 22, theme::white(0.6f), r.w - 60);
      sx += sw + 24;
    }
    y += 210;
    ui::label(c, "YOUR REMOTE", kLeft, y, Weight::Bold, 18, theme::white(0.5f));
    y += 40;
    ui::paragraph(c,
                  "The keys at the bottom work in the guide. In the player, " + std::string("\xe2\x96\xb2 \xe2\x96\xbc") +
                      " change channel. Everything you picked here can be changed in Options.",
                  kLeft, y, 1400, Weight::Regular, 24, theme::white(0.8f), 2);
    y += 110;
    setup_ui::buttons(c, kLeft, y, {"Start watching", "Go to Live Events"}, button_, kHitButton);
    setup_ui::stepBar(c, Step::Done);
    ui::keyBar(c, hints::guide(app_.remoteKeys(), true));
  }

 private:
  void finish(bool events) {
    auto guide = std::make_unique<GuideScreen>(app_);
    GuideScreen* g = guide.get();
    app_.nav().replaceAll(std::move(guide));
    if (events) g->openEvents();
  }

  bool ready_ = false;
  size_t lineup_ = 0;
  int button_ = 0;
};

}  // namespace

std::unique_ptr<Screen> makeWelcome(App& app) { return std::make_unique<Welcome>(app); }
std::unique_ptr<Screen> makePhoneSignIn(App& app, bool firstRun) { return std::make_unique<PhoneSignIn>(app, firstRun); }
std::unique_ptr<Screen> makeSetupDone(App& app) { return std::make_unique<SetupDone>(app); }

}  // namespace marigold::app
