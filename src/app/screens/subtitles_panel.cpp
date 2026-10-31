// Subtitles: the panel from the player's Aa button (Off or a language the
// stream carries, the timing and the style). Settings › Watching has the same choices
// (settings_rows.cpp). Picking a language turns subtitles on for every channel of the same
// language (English-language or Spanish-language) until Off is picked.
// Caption timing changes every channel's, or this channel's own once it has one.
#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

constexpr float kWidth = 760.0f;

const char* const kSizes[] = {"Small", "Medium", "Large", "Extra large"};
const char* const kLooks[] = {"Box", "Outline", "Shadow"};

std::unique_ptr<PanelScreen> page(App& app, std::function<void(ui::ListPanel&)> build) {
  return std::make_unique<PanelScreen>(app, PanelScreen::Side::Trailing, kWidth, std::move(build));
}

void change(App& app, const std::function<void(Settings&)>& edit) {
  Settings s = app.store().settings();
  edit(s);
  app.store().setSettings(s);
  app.playback().subtitlesChanged();
}

ui::PanelItem choice(const std::string& title, std::vector<std::string> choices, int on, std::function<void(int)> pick) {
  ui::PanelItem it{ui::PanelItem::Kind::Choice, title};
  it.choices = std::move(choices);
  it.choice = on;
  it.pick = std::move(pick);
  return it;
}

// Size, look and (where Marigold draws them) letters and position.
std::vector<ui::PanelItem> styleItems(App& app) {
  std::vector<ui::PanelItem> out;
  const Settings& set = app.store().settings();
  out.push_back(choice("Size", {kSizes[0], kSizes[1], kSizes[2], kSizes[3]}, set.subtitleSize,
                       [&app](int k) { change(app, [k](Settings& s) { s.subtitleSize = k; }); }));
  out.push_back(choice("Look", {kLooks[0], kLooks[1], kLooks[2]}, set.subtitleLook,
                       [&app](int k) { change(app, [k](Settings& s) { s.subtitleLook = k; }); }));
  if (Playback::playerDrawsSubtitles()) {
    // Apple TV's player draws them: size and look apply from the next
    // channel, and it keeps its own position and letters.
    out.push_back(ui::PanelItem::note("Changes show from the next channel you tune."));
  } else {
    out.push_back(choice("Letters", {"As sent", "Sentence case"}, set.subtitleSentenceCase ? 1 : 0,
                         [&app](int k) { change(app, [k](Settings& s) { s.subtitleSentenceCase = k == 1; }); }));
    out.push_back(choice("Position", {"Bottom", "Top"}, set.subtitleTop ? 1 : 0,
                         [&app](int k) { change(app, [k](Settings& s) { s.subtitleTop = k == 1; }); }));
  }
  return out;
}

// Caption timing: later or earlier, for every channel or this one alone.
std::vector<ui::PanelItem> timingItems(App& app) {
  std::vector<ui::PanelItem> out;
  Playback& pb = app.playback();
  std::string entry = pb.entry();
  bool own = hasOwnCaptionTiming(app.store().settings(), entry);
  ui::PanelItem timing = captionTimingRow(pb.captionTiming(), [&app, entry, own](int ms) {
    change(app, [entry, own, ms](Settings& s) { setCaptionTiming(s, own ? entry : std::string(), ms); });
  });
  timing.detail = own ? "For this channel" : "For every channel";
  out.push_back(timing);
  ui::PanelItem only{ui::PanelItem::Kind::Toggle, "Only this channel"};
  only.detail = "Its own timing, kept for next time";
  only.on = own;
  only.enabled = !entry.empty();
  only.why = "Nothing playing";
  only.select = [&app, entry] {
    int now = app.playback().captionTiming();
    change(app, [entry, now](Settings& s) {
      if (hasOwnCaptionTiming(s, entry)) clearOwnCaptionTiming(s, entry);
      else setCaptionTiming(s, entry, now);
    });
  };
  out.push_back(only);
  return out;
}

}  // namespace

ui::PanelItem captionTimingRow(int ms, std::function<void(int)> pick) {
  ui::PanelItem it{ui::PanelItem::Kind::Choice, "Caption timing"};
  for (int v = -kCaptionTimingMaxMs; v <= kCaptionTimingMaxMs; v += kCaptionTimingStepMs)
    it.choices.push_back(engine::captionTimingLabel(v));
  it.choice = (clampCaptionTiming(ms) + kCaptionTimingMaxMs) / kCaptionTimingStepMs;
  it.stepper = true;
  it.pick = [pick = std::move(pick)](int k) { pick(k * kCaptionTimingStepMs - kCaptionTimingMaxMs); };
  return it;
}

std::unique_ptr<Screen> makeSubtitles(App& app) {
  auto panel = page(app, [&app](ui::ListPanel& p) {
    Playback& pb = app.playback();
    const engine::Subtitles& subs = pb.subtitles();
    SubtitleChoice choice = pb.subtitleChoice();
    p.subtitle = "SUBTITLES";
    const GuideChannel* g = app.store().channels()->find(pb.entry());
    p.title = g ? g->name : std::string("Subtitles");
    ui::PanelItem off{ui::PanelItem::Kind::Check, "Off"};
    off.on = !choice.on;
    off.select = [&app] {
      SubtitleChoice c = app.playback().subtitleChoice();
      c.on = false;
      app.playback().setSubtitleChoice(c);
    };
    p.items.push_back(off);
    std::vector<engine::CaptionTrack> tracks = subs.tracks();
    for (const engine::CaptionTrack& t : tracks) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, t.label};
      it.detail = subs.fromStream() || t.id.rfind("cc", 0) == 0 ? "Closed captions" : "Subtitles";
      it.on = choice.on && subs.selected() == t.id;
      std::string label = t.label;
      it.select = [&app, label] { app.playback().setSubtitleChoice({true, label}); };
      p.items.push_back(it);
    }
    if (tracks.empty()) {
      // Subtitles on but none seen yet (they can take a few seconds, and
      // ads often have none); the choice is still there.
      ui::PanelItem on{ui::PanelItem::Kind::Check, "On"};
      on.detail = choice.on ? "None on this channel so far" : "When a channel has them";
      on.on = choice.on;
      on.select = [&app] {
        SubtitleChoice c = app.playback().subtitleChoice();
        c.on = true;
        app.playback().setSubtitleChoice(c);
      };
      p.items.push_back(on);
    }
    p.items.push_back(ui::PanelItem::header("TIMING"));
    for (ui::PanelItem& it : timingItems(app)) p.items.push_back(std::move(it));
    p.items.push_back(ui::PanelItem::header("STYLE"));
    for (ui::PanelItem& it : styleItems(app)) p.items.push_back(std::move(it));
    p.hint = pb.spanishChannel() ? "Your choice stays on for every Spanish-language channel"
                                 : "Your choice stays on as you change channels";
  });
  panel->liveRows = true;  // languages appear as the stream shows them
  return panel;
}

}  // namespace marigold::app
