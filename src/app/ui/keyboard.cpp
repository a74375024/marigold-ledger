#include "app/ui/keyboard.hpp"

namespace marigold::ui::keyboard {
namespace {

using search_keys::Grid;
using Area = Grid::Spot::Area;

// A magnifier from rectangles, like the top bar's.
void magnifier(Ctx& c, float cx, float cy, Color col) {
  c.g.stroke({cx - 10, cy - 10, 16, 16}, col, 8, 2.4f);
  for (int i = 0; i < 3; ++i) c.g.fill({cx + 3 + i * 1.8f, cy + 3 + i * 1.8f, 3.2f, 3.2f}, col, 1.4f);
}

// A microphone: a capsule on a stand.
void microphone(Ctx& c, float cx, float cy, Color col) {
  c.g.fill({cx - 5, cy - 13, 10, 17}, col, 5);
  c.g.stroke({cx - 9, cy - 4, 18, 14}, col, 7, 2);
  c.g.fill({cx - 1, cy + 9, 2, 5}, col);
  c.g.fill({cx - 5, cy + 13, 10, 2}, col, 1);
}

Rect keyRect(int row, int col, int span = 1) {
  return {kLeft + kStepX * static_cast<float>(col), kKeysTop + kStepY * static_cast<float>(row),
          kKeyW + kStepX * static_cast<float>(span - 1), kKeyH};
}

Rect padRect(int row, int col) {
  return {kPadX + kPadStepX * static_cast<float>(col), kPadY + kPadStepY * static_cast<float>(row), kPadW, kPadH};
}

Rect wideRect(int col) {
  return {kLeft + (kWideW + 8) * static_cast<float>(col), kKeysTop + 5 * kStepY, kWideW, kKeyH};
}

// `quiet`: the keypad's & and ' (the matcher reads them as "and" and as
// nothing), a darker key; `mode`: the mode key, outlined.
void key(Ctx& c, Rect r, const std::string& text, bool focused, float dim, int px, int hit, bool quiet = false,
         bool mode = false) {
  c.hit(r, hit);
  Rect shown = focused ? Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4} : r;
  c.g.fill(shown, focused ? theme::white() : theme::white((quiet ? 0.05f : 0.1f) * dim), 12);
  if (!focused && (quiet || mode)) c.g.stroke(shown, theme::white((quiet ? 0.08f : 0.35f) * dim), 12, 2);
  Color ink = focused ? theme::black() : theme::white((quiet ? 0.82f : 0.92f) * dim);
  labelIn(c, text, shown, mode ? Weight::Bold : Weight::Medium, px, ink, Align::Center);
}

}  // namespace

void draw(Ctx& c, const Grid& grid, const Look& look, int base) {
  float dim = look.focused ? 1.0f : 0.6f;
  const Grid::Spot& at = grid.at();
  // The field: what's typed, or what can be found.
  c.hit(kField, base + kHitField);
  c.g.fill(kField, theme::white(0.1f), kField.h * 0.5f);
  magnifier(c, kField.x + 34, kField.y + 34, theme::white(0.85f));
  Rect text{kField.x + 58, kField.y, kField.w - 80, kField.h};
  if (look.query.empty()) {
    labelIn(c, "Channel, show, movie or team", text, Weight::Medium, 26, theme::white(0.55f));
  } else {
    float w = labelIn(c, look.query, text, Weight::Medium, 28, theme::white());
    if (look.focused && look.caret && w < text.w)
      c.g.fill({text.x + w + 3, kField.y + 20, 2, kField.h - 40}, theme::white(0.9f));
  }
  if (grid.speak()) {
    bool f = look.focused && at.area == Area::Speak;
    c.hit(kSpeak, base + kHitSpeak);
    c.g.fill(kSpeak, f ? theme::white() : theme::white(0.1f), kSpeak.h * 0.5f);
    Color ink = f ? theme::black() : theme::white(0.92f);
    microphone(c, kSpeak.x + 30, kSpeak.y + 36, ink);
    labelIn(c, "Speak", {kSpeak.x + 46, kSpeak.y, kSpeak.w - 52, kSpeak.h}, Weight::Bold, 24, ink);
  }
  using search_keys::Page;
  Page page = grid.page();
  auto focusedKey = [&](int row, int col) {
    return look.focused && at.area == Area::Key && at.row == row && at.col == col;
  };
  if (page == Page::Letters) {
    // a-z, then Space in the cells after z.
    for (int row = 0; row < search_keys::kRows; ++row) {
      for (int col = 0; col < search_keys::kLetterCols; ++col) {
        char ch = Grid::keyAt(page, row, col);
        int id = base + kHitKeys + search_keys::targetOf(page, {Area::Key, row, col});
        if (ch == ' ') {
          key(c, keyRect(row, col, search_keys::kLetterCols - col), "Space", focusedKey(row, col), dim, 24, id);
          break;
        }
        key(c, keyRect(row, col), std::string(1, ch), focusedKey(row, col), dim, 32, id);
      }
    }
  } else {
    // The keypad on its panel, the margins even; Space across its row.
    c.g.fill(kPanel, theme::white(0.035f * dim), 22);
    c.g.stroke(kPanel, theme::white(0.06f * dim), 22, 1);
    for (int row = 0; row + 1 < search_keys::kRows; ++row) {
      for (int col = 0; col < search_keys::kPadCols; ++col) {
        char ch = Grid::keyAt(page, row, col);
        key(c, padRect(row, col), std::string(1, ch), focusedKey(row, col), dim, ch == '&' || ch == '\'' ? 30 : 32,
            base + kHitKeys + search_keys::targetOf(page, {Area::Key, row, col}), ch == '&' || ch == '\'');
      }
    }
    int row = search_keys::kRows - 1;
    key(c, keyRect(row, 0, search_keys::kLetterCols), "Space", focusedKey(row, 0), dim, 24,
        base + kHitKeys + search_keys::targetOf(page, {Area::Key, row, 0}));
  }
  // ⌫ Delete · 123 (abc) · Clear: Clear sits at the far end, away from Delete.
  const char* wide[] = {"Delete", page == Page::Letters ? "123" : "abc", "Clear"};
  for (int col = 0; col < 3; ++col) {
    bool f = look.focused && at.area == Area::Bottom && at.col == col;
    bool mode = col == static_cast<int>(search_keys::Wide::Mode);
    key(c, wideRect(col), wide[col], f, dim, 24, base + kHitKeys + search_keys::kTargetWide + col, false, mode);
    if (col == 0) {
      // ⌫: a small key shape with a cross, before the word.
      Rect r = wideRect(0);
      Color ink = f ? theme::black() : theme::white(0.92f * dim);
      float tw = textWidth(c, wide[0], Weight::Medium, 24);
      float x = r.x + (r.w - tw) * 0.5f - 34;
      float y = r.y + r.h * 0.5f;
      c.g.stroke({x + 6, y - 9, 22, 18}, ink, 3, 2);
      c.g.fill({x, y - 1, 8, 2}, ink, 1);
      c.g.fill({x + 13, y - 1, 9, 2}, ink, 1);
    }
  }
  if (look.tab) {
    // The way to the results.
    c.hit(kTab, base + kHitTab);
    c.g.fill(kTab, theme::white(look.focused ? 0.14f : 0.08f), 32);
    labelIn(c, "\xe2\x96\xb6", kTab, Weight::Bold, 30, theme::white(0.92f), Align::Center);
  }
}

bool spotFor(int id, int base, search_keys::Page page, Grid::Spot& spot) {
  int k = id - base;
  if (k == kHitSpeak) {
    spot = {Area::Speak, 0, 0};
    return true;
  }
  return k >= kHitKeys && search_keys::spotOfTarget(page, k - kHitKeys, spot);
}

}  // namespace marigold::ui::keyboard
