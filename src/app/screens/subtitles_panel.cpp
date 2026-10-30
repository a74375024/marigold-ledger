// Subtitles: the panel from the player's Subtitles pill (Off or a language
// the stream carries), Caption style, and the Options page. Picking a
// language turns subtitles on for every channel that has them until Off is
// picked.
#include <algorithm>

#include "app/app.hpp"
#include "app/playback.hpp"
#include "app/screens/panel_screen.hpp"
#include "app/screens/screens.hpp"
#include "app/store/store.hpp"

namespace marigold::app {
namespace {

constexpr float kWidth = 760.0f;

const char* const kSizes[] = {"Small", "Medium", "Large"};
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

std::string styleSummary(const Settings& s) {
  return std::string(kSizes[std::clamp(s.subtitleSize, 0, 2)]) + " \xc2\xb7 " + kLooks[std::clamp(s.subtitleLook, 0, 2)];
}

ui::PanelItem styleRow(App& app) {
  ui::PanelItem it{ui::PanelItem::Kind::Chevron, "Caption style"};
  it.value = styleSummary(app.store().settings());
  it.select = [&app] { app.nav().push(makeCaptionStyle(app)); };
  return it;
}

ui::PanelItem choice(const std::string& title, std::vector<std::string> choices, int on, std::function<void(int)> pick) {
  ui::PanelItem it{ui::PanelItem::Kind::Choice, title};
  it.choices = std::move(choices);
  it.choice = on;
  it.pick = std::move(pick);
  return it;
}

}  // namespace

std::unique_ptr<Screen> makeSubtitles(App& app) {
  auto panel = page(app, [&app](ui::ListPanel& p) {
    Playback& pb = app.playback();
    const engine::Subtitles& subs = pb.subtitles();
    const Settings& set = app.store().settings();
    p.subtitle = "SUBTITLES";
    const GuideChannel* g = app.store().channels()->find(pb.entry());
    p.title = g ? g->name : std::string("Subtitles");
    ui::PanelItem off{ui::PanelItem::Kind::Check, "Off"};
    off.on = !set.subtitles;
    off.select = [&app] { change(app, [](Settings& s) { s.subtitles = false; }); };
    p.items.push_back(off);
    std::vector<engine::CaptionTrack> tracks = subs.tracks();
    for (const engine::CaptionTrack& t : tracks) {
      ui::PanelItem it{ui::PanelItem::Kind::Check, t.label};
      it.detail = subs.fromStream() || t.id.rfind("cc", 0) == 0 ? "Closed captions" : "Subtitles";
      it.on = set.subtitles && subs.selected() == t.id;
      std::string label = t.label;
      it.select = [&app, label] {
        change(app, [&label](Settings& s) {
          s.subtitles = true;
          s.subtitleLanguage = label;
        });
      };
      p.items.push_back(it);
    }
    if (tracks.empty()) {
      // Subtitles on but none seen yet (they can take a few seconds, and
      // ads often have none); the choice is still there.
      ui::PanelItem on{ui::PanelItem::Kind::Check, "On"};
      on.detail = set.subtitles ? "None on this channel so far" : "When a channel has them";
      on.on = set.subtitles;
      on.select = [&app] { change(app, [](Settings& s) { s.subtitles = true; }); };
      p.items.push_back(on);
    }
    p.items.push_back(ui::PanelItem::header("STYLE"));
    p.items.push_back(styleRow(app));
    p.hint = "Your choice stays on as you change channels";
  });
  panel->liveRows = true;  // languages appear as the stream shows them
  return panel;
}

std::unique_ptr<Screen> makeCaptionStyle(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    const Settings& set = app.store().settings();
    p.subtitle = "SUBTITLES";
    p.title = "Caption style";
    p.items.push_back(choice("Size", {kSizes[0], kSizes[1], kSizes[2]}, set.subtitleSize,
                             [&app](int k) { change(app, [k](Settings& s) { s.subtitleSize = k; }); }));
    p.items.push_back(choice("Look", {kLooks[0], kLooks[1], kLooks[2]}, set.subtitleLook,
                             [&app](int k) { change(app, [k](Settings& s) { s.subtitleLook = k; }); }));
    if (Playback::playerDrawsSubtitles()) {
      // Apple TV's player draws them: size and look apply from the next
      // channel, and it keeps its own position and letters.
      p.items.push_back(ui::PanelItem::note("Changes show from the next channel you tune."));
    } else {
      p.items.push_back(choice("Letters", {"As sent", "Sentence case"}, set.subtitleSentenceCase ? 1 : 0,
                               [&app](int k) { change(app, [k](Settings& s) { s.subtitleSentenceCase = k == 1; }); }));
      p.items.push_back(choice("Position", {"Bottom", "Top"}, set.subtitleTop ? 1 : 0,
                               [&app](int k) { change(app, [k](Settings& s) { s.subtitleTop = k == 1; }); }));
      p.items.push_back(ui::PanelItem::note(
          "Sentence case turns live captions in capitals into normal sentences. It's easier to read but can get names wrong."));
    }
    p.hint = "Left and right change it";
  });
}

std::unique_ptr<Screen> makeSubtitleSettings(App& app) {
  return page(app, [&app](ui::ListPanel& p) {
    const Settings& set = app.store().settings();
    p.subtitle = "OPTIONS";
    p.title = "Subtitles";
    ui::PanelItem on{ui::PanelItem::Kind::Toggle, "Subtitles"};
    on.detail = "On for every channel that has them";
    on.on = set.subtitles;
    on.select = [&app] { change(app, [](Settings& s) { s.subtitles = !s.subtitles; }); };
    p.items.push_back(on);
    if (!set.subtitleLanguage.empty()) {
      ui::PanelItem lang{ui::PanelItem::Kind::Note, "Language: " + set.subtitleLanguage};
      lang.detail = "Pick another from Subtitles in the player";
      p.items.push_back(lang);
    }
    p.items.push_back(styleRow(app));
  });
}

std::string subtitleSummary(App& app) {
  const Settings& s = app.store().settings();
  if (!s.subtitles) return "Off";
  return s.subtitleLanguage.empty() ? std::string("On") : s.subtitleLanguage;
}

}  // namespace marigold::app
