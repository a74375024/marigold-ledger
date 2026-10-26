#include "app/test_screen.hpp"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <utility>

#include "core/util/log.hpp"
#include "entrada_texto.h"

namespace marigold::app {
namespace {

using gfx::Color;
using gfx::Rect;
using text::Weight;

const Color kBackground = Color::hex(0x121418);
const Color kPanel = Color::hex(0xFFFFFF, 0.06f);
const Color kWhite = Color::hex(0xFFFFFF);
const Color kDim = Color::hex(0xFFFFFF, 0.62f);
const Color kFaint = Color::hex(0xFFFFFF, 0.45f);
const Color kBlack = Color::hex(0x000000);
const Color kSeen = Color::hex(0x2E9E55);
const Color kLive = Color::hex(0xC62828);
const Color kAmber = Color::hex(0xE0A100);

constexpr const char* kLinkFile = "test-link.txt";
constexpr size_t kMaxKeyLines = 9;
constexpr uint32_t kBackTwiceMs = 2000;

// The keys the chip grid checks off, in reading order.
constexpr Key kChecklist[] = {
    Key::Up, Key::Down, Key::Left, Key::Right, Key::Ok, Key::Back,
    Key::ChannelUp, Key::ChannelDown, Key::Red, Key::Green, Key::Yellow, Key::Blue,
    Key::PlayPause, Key::Play, Key::Pause, Key::Stop, Key::Rewind, Key::FastForward,
    Key::Digit1, Key::Digit2, Key::Digit3, Key::Digit4, Key::Digit5, Key::Digit6,
    Key::Digit7, Key::Digit8, Key::Digit9, Key::Digit0, Key::Info, Key::Menu,
};

const char* kButtonLabels[] = {"Enter stream link", "Play full screen", "Play in corner",
                               "Load mode", "Stop"};

std::string fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char* f, ...) {
  char buf[256];
  va_list args;
  va_start(args, f);
  std::vsnprintf(buf, sizeof buf, f, args);
  va_end(args);
  return buf;
}

std::string clockText() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_r(&now, &tm);
  char buf[16];
  std::strftime(buf, sizeof buf, "%H:%M", &tm);
  return buf;
}

// The decoded picture inside the corner preview.
constexpr Rect kCorner = {1000, 600, 560, 315};

}  // namespace

TestScreen::TestScreen(gfx::Renderer& g, text::TextRenderer& t, platform::Video& v,
                       platform::Storage& s, SystemInfo info)
    : g_(g), t_(t), video_(v), storage_(s), info_(std::move(info)) {
  if (auto saved = storage_.read(kLinkFile)) {
    link_ = *saved;
    while (!link_.empty() && (link_.back() == '\n' || link_.back() == ' ')) link_.pop_back();
  }
}

void TestScreen::toast(const std::string& text, uint32_t nowMs) {
  toast_ = text;
  toastUntil_ = nowMs + 2500;
}

std::string TestScreen::linkSummary() const {
  if (link_.empty()) return "No link entered";
  // Never show the link itself: Xtream links carry the login in the path.
  std::string ext;
  size_t q = link_.find('?');
  std::string path = link_.substr(0, q);
  size_t dot = path.rfind('.'), slash = path.rfind('/');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash) &&
      path.size() - dot <= 6) {
    ext = path.substr(dot);
  }
  return fmt("Link set: %zu characters%s%s", link_.size(), ext.empty() ? "" : ", ends in ",
             ext.c_str());
}

void TestScreen::suspend() {
  if (view_ != VideoView::None) {
    MLOG_I("test", "background: stopping video");
    stopPlayback();
  }
}

void TestScreen::handle(const SDL_Event& e, uint32_t nowMs) {
  if (e.type == texto_sistema_evento()) {
    if (e.user.code == TS_EV_FIM || e.user.code == TS_EV_ERRO) {
      keyboardOpen_ = false;
      // On LG, Enter closes the keyboard without a "confirmed" flag (Legacy
      // measured it), so whatever is in the field counts.
      std::string value = texto_sistema_valor();
      if (e.user.code == TS_EV_ERRO) {
        toast("The TV keyboard did not open", nowMs);
      } else if (!value.empty() && value != link_) {
        link_ = value;
        storage_.write(kLinkFile, link_, true);
        MLOG_I("test", "stream link entered (%zu characters)", link_.size());
      }
    }
    return;
  }
  if (keyboardOpen_ && texto_sistema_engole(&e)) return;

  switch (e.type) {
    case SDL_KEYDOWN:
      onKey(e.key, nowMs);
      break;
    case SDL_KEYUP:
      held_ = Key::Unknown;
      break;
    case SDL_MOUSEMOTION:
      pointerVisible_ = true;
      pointerX_ = e.motion.x;
      pointerY_ = e.motion.y;
      break;
    case SDL_MOUSEBUTTONDOWN:
      ++clicks_;
      pointerX_ = e.button.x;
      pointerY_ = e.button.y;
      MLOG_D("test", "click button %d at %d,%d", e.button.button, e.button.x, e.button.y);
      break;
    case SDL_MOUSEWHEEL:
      if (e.wheel.y > 0) ++wheelUp_;
      if (e.wheel.y < 0) ++wheelDown_;
      break;
    default:
      break;
  }
}

void TestScreen::onKey(const SDL_KeyboardEvent& k, uint32_t nowMs) {
  int scancode = static_cast<int>(k.keysym.scancode);
  Key key = keyFromSdl(scancode, k.keysym.sym);
  if (key == Key::CursorShown || key == Key::CursorHidden) {
    pointerVisible_ = key == Key::CursorShown;
    return;
  }
  held_ = key;
  if (static_cast<size_t>(key) < seen_.size()) seen_[static_cast<size_t>(key)] = true;
  lastKeys_.push_front({fmt("%-12s scancode %-4d keycode 0x%X%s", keyName(key), scancode,
                            static_cast<unsigned>(k.keysym.sym), k.repeat ? "  repeat" : ""),
                        nowMs});
  if (lastKeys_.size() > kMaxKeyLines) lastKeys_.pop_back();
  if (!k.repeat) MLOG_D("test", "key %s scancode %d", keyName(key), scancode);

  if (key == Key::Exit) {
    quit_ = true;
    return;
  }
  if (key == Key::Back) {
    if (view_ != VideoView::None) {
      stopPlayback();
    } else if (backPressedMs_ && nowMs - backPressedMs_ < kBackTwiceMs) {
      quit_ = true;
    } else {
      backPressedMs_ = nowMs;
      toast("Press Back again to exit", nowMs);
    }
    return;
  }
  if (view_ == VideoView::Full) return;  // every other key is only recorded
  int count = static_cast<int>(Button::Count);
  if (key == Key::Up) focus_ = (focus_ + count - 1) % count;
  if (key == Key::Down) focus_ = (focus_ + 1) % count;
  if (key == Key::Ok && !k.repeat) activate(static_cast<Button>(focus_), nowMs);
}

void TestScreen::activate(Button b, uint32_t nowMs) {
  switch (b) {
    case Button::EnterLink:
      if (!texto_sistema_disponivel()) {
        toast("This TV's keyboard is not available", nowMs);
        return;
      }
      keyboardOpen_ = true;
      texto_sistema_abrir(link_.c_str(), 0);
      break;
    case Button::PlayFull:
      startPlayback(VideoView::Full, nowMs);
      break;
    case Button::PlayCorner:
      startPlayback(VideoView::Corner, nowMs);
      break;
    case Button::LoadMode:
      loadMode_ = loadMode_ == 0 ? 2 : 0;
      toast(loadMode_ == 0 ? "Load mode 0: default payload" : "Load mode 2: lean live payload",
            nowMs);
      break;
    case Button::Stop:
      stopPlayback();
      break;
    case Button::Count:
      break;
  }
}

void TestScreen::startPlayback(VideoView view, uint32_t nowMs) {
  if (link_.empty()) {
    toast("Enter a stream link first", nowMs);
    return;
  }
  if (view == VideoView::Full) video_.setWindow({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight});
  else video_.setWindow(kCorner);
  if (view_ == VideoView::None || !video_.state().active) {
    if (!video_.play(link_, "", loadMode_)) {
      toast("The player refused the link", nowMs);
      return;
    }
    playStartedMs_ = nowMs;
    firstFrameMs_ = 0;
  }
  view_ = view;
}

void TestScreen::stopPlayback() {
  video_.stop();
  view_ = VideoView::None;
}

void TestScreen::frame(uint32_t nowMs) {
  platform::VideoState vs = video_.state();
  if (view_ != VideoView::None && !firstFrameMs_ && vs.position > 0.0) {
    firstFrameMs_ = nowMs;
    MLOG_I("test", "first frame after %u ms", nowMs - playStartedMs_);
  }

  g_.beginFrame(kBackground);
  if (view_ == VideoView::Full) {
    g_.hole({0, 0, gfx::kLayoutWidth, gfx::kLayoutHeight});
    drawFullScreenOverlay(vs, nowMs);
    return;
  }
  drawHeader();
  drawKeys(nowMs);
  drawPlayback(vs, nowMs);
  drawLog();
  if (!toast_.empty() && nowMs < toastUntil_) {
    float w = t_.measure(toast_, Weight::Medium, 26) + 64;
    Rect r = {(gfx::kLayoutWidth - w) / 2, 980, w, 60};
    g_.fill(r, Color::hex(0x2A2D33, 0.97f), 30);
    t_.draw(g_, toast_, r.x + 32, r.y + 14, Weight::Medium, 26, kWhite);
  }
}

void TestScreen::section(float x, float y, const char* title) {
  t_.draw(g_, title, x, y, Weight::Bold, 18, kFaint);
}

void TestScreen::drawHeader() {
  t_.draw(g_, "Marigold", 80, 48, Weight::Bold, 46, kWhite);
  t_.draw(g_, fmt("M0 test screen  \xc2\xb7  version %s  \xc2\xb7  build %s", info_.version.c_str(),
                  info_.build.c_str()),
          330, 64, Weight::Medium, 24, kDim);
  t_.draw(g_, clockText(), 1640, 56, Weight::Medium, 30, kDim, 200, text::Align::Right);
  std::string sys = fmt("webOS %s  \xc2\xb7  SDL %s  \xc2\xb7  %s  \xc2\xb7  %dx%d  \xc2\xb7  "
                        "see-through surface: %s",
                        info_.webosMajor ? std::to_string(info_.webosMajor).c_str() : "?",
                        info_.sdlRuntime.c_str(), info_.glRenderer.c_str(), info_.drawableW,
                        info_.drawableH, info_.transparentSurface ? "yes" : "NO");
  t_.draw(g_, sys, 80, 118, Weight::Regular, 22, kDim, 1760);
}

void TestScreen::drawKeys(uint32_t nowMs) {
  (void)nowMs;
  const float x0 = 80, y0 = 180, w = 860;
  g_.fill({x0, y0, w, 690}, kPanel, 24);
  section(x0 + 32, y0 + 26, "REMOTE KEYS  \xc2\xb7  PRESS EVERY KEY ONCE");
  const float chipW = 128, chipH = 48, gap = 10;
  int i = 0;
  for (Key k : kChecklist) {
    float cx = x0 + 32 + static_cast<float>(i % 6) * (chipW + gap);
    float cy = y0 + 64 + static_cast<float>(i / 6) * (chipH + gap);
    bool seen = seen_[static_cast<size_t>(k)];
    bool held = held_ == k;
    Color fill = held ? kWhite : seen ? kSeen : Color::hex(0xFFFFFF, 0.08f);
    g_.fill({cx, cy, chipW, chipH}, fill, 12);
    t_.draw(g_, keyName(k), cx, cy + 11, Weight::Medium, 22, held ? kBlack : kWhite, chipW,
            text::Align::Center);
    ++i;
  }
  float ly = y0 + 64 + 5 * (chipH + gap) + 18;
  section(x0 + 32, ly, "LAST KEYS");
  ly += 34;
  for (const KeyLine& l : lastKeys_) {
    t_.draw(g_, l.text, x0 + 32, ly, Weight::Regular, 21, kWhite, w - 64);
    ly += 30;
  }
  std::string pointer =
      pointerVisible_
          ? fmt("Pointer: x %d  y %d  \xc2\xb7  clicks %d  \xc2\xb7  wheel up %d, down %d",
                pointerX_, pointerY_, clicks_, wheelUp_, wheelDown_)
          : fmt("Pointer: hidden  \xc2\xb7  clicks %d  \xc2\xb7  wheel up %d, down %d", clicks_,
                wheelUp_, wheelDown_);
  t_.draw(g_, pointer, x0 + 32, y0 + 690 - 48, Weight::Medium, 22, kDim, w - 64);
}

void TestScreen::drawPlayback(const platform::VideoState& vs, uint32_t nowMs) {
  const float x0 = 1000, y0 = 180, w = 840;
  g_.fill({x0, y0, w, 690}, kPanel, 24);
  section(x0 + 32, y0 + 26, "PLAYBACK");
  t_.draw(g_, linkSummary(), x0 + 32, y0 + 60, Weight::Medium, 24, kWhite, w - 64);

  float by = y0 + 104;
  for (int i = 0; i < static_cast<int>(Button::Count); ++i) {
    bool focused = focus_ == i;
    Rect r = {x0 + 32, by, 360, 50};
    g_.fill(r, focused ? kWhite : Color::hex(0xFFFFFF, 0.10f), 25);
    std::string label = kButtonLabels[i];
    if (static_cast<Button>(i) == Button::LoadMode) label += fmt(": %d", loadMode_);
    t_.draw(g_, label, r.x + 24, r.y + 12, Weight::Medium, 23, focused ? kBlack : kWhite, 312);
    by += 60;
  }

  // Status to the right of the buttons.
  float sx = x0 + 420, sy = y0 + 104;
  auto row = [&](const char* name, const std::string& value, Color c) {
    t_.draw(g_, name, sx, sy, Weight::Regular, 20, kFaint);
    t_.draw(g_, value, sx + 130, sy, Weight::Medium, 20, c, 270);
    sy += 32;
  };
  row("Bus", vs.registrationDenied ? "denied (permission)" : vs.registered ? "registered" : "off",
      vs.registrationDenied ? kAmber : kWhite);
  std::string state = !vs.active  ? "idle"
                      : vs.failed ? "failed"
                      : vs.ended  ? "ended"
                      : vs.bufferingMs ? fmt("buffering %.1f s", vs.bufferingMs / 1000.0)
                      : vs.playing ? "playing"
                      : vs.ready   ? "ready"
                                   : "loading";
  row("State", state, vs.failed ? kLive : kWhite);
  row("Decoder", vs.decoderSeen ? fmt("%dx%d  %s", vs.width, vs.height, vs.hdr.c_str()) : "-",
      kWhite);
  row("Position", vs.active ? fmt("%.1f s  (buffer %.1f s)", vs.position, vs.bufferEnd) : "-",
      kWhite);
  row("First frame",
      firstFrameMs_ ? fmt("%u ms", firstFrameMs_ - playStartedMs_)
      : (view_ != VideoView::None) ? fmt("waiting %u s", (nowMs - playStartedMs_) / 1000)
                                   : "-",
      kWhite);
  if (vs.audioUnsupported) row("Audio", "codec not supported by the TV", kAmber);
  if (!vs.error.empty()) row("Error", vs.error, kLive);

  if (view_ == VideoView::Corner) {
    g_.stroke({kCorner.x - 3, kCorner.y - 3, kCorner.w + 6, kCorner.h + 6},
              Color::hex(0xFFFFFF, 0.3f), 6, 2);
    g_.hole(kCorner);
  } else {
    g_.fill(kCorner, Color::hex(0x000000, 0.35f), 4);
    t_.draw(g_, "Corner preview appears here", kCorner.x, kCorner.y + kCorner.h / 2 - 14,
            Weight::Regular, 22, kFaint, kCorner.w, text::Align::Center);
  }
}

void TestScreen::drawLog() {
  const float x0 = 80, y0 = 890;
  section(x0, y0, "LOG (LINKS AND LOGINS ARE REMOVED)");
  float y = y0 + 28;
  for (const std::string& line : log::recent(2)) {
    t_.draw(g_, line, x0, y, Weight::Regular, 19, kFaint, 1760);
    y += 26;
  }
}

void TestScreen::drawFullScreenOverlay(const platform::VideoState& vs, uint32_t nowMs) {
  // Small status pill so a black picture can still be told apart from a stall.
  bool early = nowMs - playStartedMs_ < 8000 || !vs.playing || vs.bufferingMs || vs.failed;
  if (!early) return;
  std::string status = vs.failed      ? "Failed: " + vs.error
                       : vs.bufferingMs ? fmt("Buffering %.1f s", vs.bufferingMs / 1000.0)
                       : vs.playing     ? fmt("Playing  \xc2\xb7  %dx%d  \xc2\xb7  mode %d",
                                              vs.width, vs.height, loadMode_)
                                        : "Loading\xe2\x80\xa6";
  status += "   \xc2\xb7   Back stops";
  float w = t_.measure(status, Weight::Medium, 24) + 56;
  g_.fill({60, 50, w, 56}, Color::hex(0x101216, 0.85f), 28);
  t_.draw(g_, status, 88, 63, Weight::Medium, 24, vs.failed ? Color::hex(0xFF8A80) : kWhite);
}

}  // namespace marigold::app
