// Drawing shared by the setup screens (first run, My Lineup, My Sports): the
// seven steps along the bottom, the heading at the top left, rows of pill
// buttons, the remote's key hints, QR codes and jumping a letter in A-to-Z
// lists.
#pragma once

#include <string>
#include <vector>

#include "app/ui/ui.hpp"
#include "core/engine/model.hpp"
#include "core/input/keys.hpp"
#include "core/report/qr.hpp"

namespace marigold::app::setup_ui {

// First-run setup, in order.
enum class Step { Provider, Area, Languages, Channels, Sports, Teams, Done };
constexpr int kSteps = 7;

constexpr float kLeft = 80;
constexpr float kStepsY = 944;   // the step bar's top
constexpr float kContentBottom = 910;  // first run: nothing below this but the bars

const char* title(Step s);
// "STEP 3 OF 7 · LANGUAGES"
std::string kicker(Step s);
// The steps along the bottom, `at` highlighted, the ones before ticked.
void stepBar(ui::Ctx& c, Step at);

// Kicker, title and lead at the top left, `width` wide. Returns the y below.
float heading(ui::Ctx& c, const std::string& kicker, const std::string& title, const std::string& lead,
              float width = 760, float y = 96);

// Pill buttons in a row from (x, y); `focused` -1 for none. Hit ids are
// hitBase + index. Returns the row's width.
float buttons(ui::Ctx& c, float x, float y, const std::vector<std::string>& titles, int focused, int hitBase);

// The remote's keys for these screens (hints::setup), along the bottom.
void keys(ui::Ctx& c, const RemoteKeys& remote, bool letters);

// A QR code (dark modules on white, the quiet zone included) in `r`.
void qr(ui::Ctx& c, ui::Rect r, const report::QrMatrix& m);

// My Sports' pages (Build 127) ------------------------------------------

// A pick's tile: its name, a gray line under it, a green line saying why it
// was ticked for you, and a check circle (or a chevron for a tile that
// opens a page). Ticked: a green ring. Focused: white, dark text.
struct PickTile {
  std::string title, detail, why;
  bool on = false;
  bool chevron = false;  // "More sports ›": opens a page, no check
  bool strong = false;   // "All of the Big Ten": a slightly lighter tile
};
constexpr float kPickPad = 22;
void pickTile(ui::Ctx& c, ui::Rect r, const PickTile& t, bool focused);

// A list down the left of a rail-and-grid page (Soccer's groups, the
// college sports, a sport's conferences): `selected` is the one the grid
// shows, focused when `focused`. Rows scroll to keep the selected one in
// view (`scroll` keeps the offset). Hit ids are hitBase + index.
constexpr float kRailRowH = 56, kRailRowStep = 64;
void railList(ui::Ctx& c, float x, float y, float w, float bottom, const std::vector<std::string>& rows,
              int selected, bool focused, float& scroll, int hitBase);

// An options row (the college page's): a title, a gray line, and at the
// right a switch (`toggle`, on or off) or a chevron.
void optionRow(ui::Ctx& c, ui::Rect r, const std::string& title, const std::string& detail, bool toggle, bool on,
               bool focused);

// A grid's vertical scroll that keeps row `focusRow` (of `rowH` + `gap`)
// inside `visible`.
float keepRowInView(float scroll, int focusRow, float rowH, float gap, float visible);

// Bottom bars for these pages, built from plain hints.
hints::Hint keyHint(const std::string& cap, const std::string& label);

// The keys that jump a letter in an A-to-Z list: +1 for the next letter,
// -1 for the previous one, 0 for any other key.
int letterStep(Key k);
// From `from`, the first name (searching in `step` direction) that starts
// with a different letter; `from` itself when there is none. -1 entries
// (headers) are skipped.
int nextLetter(const std::vector<std::string>& names, int from, int step);

// "Building your lineup": an amber petal ring, what's being done, a bar that
// follows the sort's real steps (eased inside a step, never backwards, never
// full before it's done) and the four steps, centred in `area`. Keep one
// state per screen; redraw every frame while it shows.
struct Building {
  float shown = 0;       // the bar, 0..1
  uint32_t lastMs = 0;   // last frame
  uint32_t downloadFrom = 0;
};
void building(ui::Ctx& c, ui::Rect area, const engine::LoadProgress& p, Building& state, bool large = true);

}  // namespace marigold::app::setup_ui
