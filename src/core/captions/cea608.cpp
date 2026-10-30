#include "core/captions/cea608.hpp"

#include <algorithm>
#include <utility>

namespace marigold::captions {
namespace {

// The basic set is ASCII except for a handful of cells.
char32_t basicChar(uint8_t c) {
  switch (c) {
    case 0x2A: return U'á';  // á
    case 0x5C: return U'é';  // é
    case 0x5E: return U'í';  // í
    case 0x5F: return U'ó';  // ó
    case 0x60: return U'ú';  // ú
    case 0x7B: return U'ç';  // ç
    case 0x7C: return U'÷';  // ÷
    case 0x7D: return U'Ñ';  // Ñ
    case 0x7E: return U'ñ';  // ñ
    case 0x7F: return U'█';  // solid block
    default: return c;
  }
}

// 0x11 0x30-0x3F (0x19 on data channel 2). 0x39 is the transparent space.
constexpr char32_t kSpecial[16] = {
    U'®', U'°', U'½', U'¿', U'™', U'¢', U'£', U'♪',
    U'à', U' ',      U'è', U'â', U'ê', U'î', U'ô', U'û'};

// 0x12 0x20-0x3F: Spanish, miscellaneous and French.
constexpr char32_t kExtended12[32] = {
    U'Á', U'É', U'Ó', U'Ú', U'Ü', U'ü', U'‘', U'¡',
    U'*',      U'’', U'—', U'©', U'℠', U'•', U'“', U'”',
    U'À', U'Â', U'Ç', U'È', U'Ê', U'Ë', U'ë', U'Î',
    U'Ï', U'ï', U'Ô', U'Ù', U'ù', U'Û', U'«', U'»'};

// 0x13 0x20-0x3F: Portuguese, German and Danish.
constexpr char32_t kExtended13[32] = {
    U'Ã', U'ã', U'Í', U'Ì', U'ì', U'Ò', U'ò', U'Õ',
    U'õ', U'{',      U'}',      U'\\',     U'^',      U'_',      U'|',      U'~',
    U'Ä', U'ä', U'Ö', U'ö', U'ß', U'¥', U'¤', U'│',
    U'Å', U'å', U'Ø', U'ø', U'┌', U'┐', U'└', U'┘'};

// Preamble rows (0-based) by the first byte (0x10-0x17) and bit 5 of the second.
constexpr int kPacRow[8][2] = {{10, 10}, {0, 1}, {2, 3}, {11, 12}, {13, 14}, {4, 5}, {6, 7}, {8, 9}};

void appendUtf8(std::string& out, char32_t c) {
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
}

bool blank(char32_t c) { return c == 0 || c == U' '; }

}  // namespace

bool Line::operator==(const Line& o) const {
  return row == o.row && indent == o.indent && italic == o.italic && text == o.text;
}

bool Screen::operator==(const Screen& o) const { return lines == o.lines; }

void Cea608::Memory::clear() {
  for (int r = 0; r < kRows; ++r) clearRow(r);
}

void Cea608::Memory::clearRow(int r) { std::fill(cell[r], cell[r] + kColumns, Cell{}); }

Cea608::Cea608(int channel)
    : channel_(std::clamp(channel, 1, 4)), field_((channel_ - 1) / 2), dataChannel_((channel_ - 1) % 2) {}

void Cea608::reset() {
  current_ = 0;
  xds_ = false;
  mode_ = Mode::None;
  beforeText_ = Mode::None;
  rollRows_ = 2;
  baseRow_ = kRows - 1;
  for (Memory& m : mem_) {
    m.clear();
    m.row = kRows - 1;
    m.col = 0;
    m.italic = false;
  }
  shown_ = 0;
  haveLast_ = false;
  haveTime_ = false;
  lastDataMs_ = 0;
  dirty_ = false;
  changed_ = !screen_.empty();
  screen_.lines.clear();
  sawText_ = false;
}

void Cea608::feed(int field, uint8_t b1, uint8_t b2, int64_t nowMs) {
  expire(nowMs);
  if (field != field_) return;
  // Parity is not checked: some encoders leave it zero.
  b1 &= 0x7F;
  b2 &= 0x7F;
  if (b1 == 0 && b2 == 0) return;  // padding
  if (b1 >= 0x10 && b1 <= 0x1F) {
    xds_ = false;  // a caption control code interrupts XDS
    if (b2 < 0x20) return;
    // Control codes are sent twice; the repeat right after is ignored.
    bool repeat = haveLast_ && b1 == last1_ && b2 == last2_;
    haveLast_ = !repeat;
    last1_ = b1;
    last2_ = b2;
    if (repeat) return;
    current_ = (b1 & 0x08) ? 1 : 0;
    if (current_ != dataChannel_) return;
    lastDataMs_ = nowMs;
    control(static_cast<uint8_t>(b1 & 0x17), b2);
    return;
  }
  haveLast_ = false;
  if (b1 >= 0x01 && b1 <= 0x0F) {
    // XDS (field 2): a packet runs from a start code to 0x0F.
    xds_ = b1 != 0x0F;
    return;
  }
  if (xds_ || current_ != dataChannel_) return;
  lastDataMs_ = nowMs;
  if (!writing()) return;
  if (b1 >= 0x20) put(basicChar(b1));
  if (b2 >= 0x20) put(basicChar(b2));
}

void Cea608::tick(int64_t nowMs) { expire(nowMs); }

void Cea608::expire(int64_t nowMs) {
  if (!haveTime_ || nowMs < lastDataMs_) {
    // First call, or the clock went back (a new stream): start counting here.
    haveTime_ = true;
    lastDataMs_ = nowMs;
    return;
  }
  if (nowMs - lastDataMs_ < kIdleEraseMs) return;
  lastDataMs_ = nowMs;
  displayed().clear();
  dirty_ = true;
}

Cea608::Memory& Cea608::target() {
  return mode_ == Mode::PaintOn || mode_ == Mode::RollUp ? displayed() : hidden();
}

void Cea608::touch(const Memory& m) {
  if (&m == &mem_[shown_]) dirty_ = true;
}

void Cea608::control(uint8_t b1, uint8_t b2) {
  bool misc = (b1 == 0x14 || b1 == 0x15) && b2 < 0x30;
  if (mode_ == Mode::Text) {
    // Only a caption mode command leaves text mode.
    if (!misc || !(b2 == 0x20 || b2 == 0x29 || (b2 >= 0x25 && b2 <= 0x27))) return;
  }
  if (b2 >= 0x40) {
    preamble(b1, b2);
    return;
  }
  if (misc) {
    command(b2);
    return;
  }
  switch (b1) {
    case 0x11:
      if (b2 >= 0x30) {
        put(kSpecial[b2 - 0x30]);
      } else {
        // Mid-row code: takes a cell as a space; italics on, or off with a colour.
        Memory& m = target();
        m.italic = b2 >= 0x2E;
        put(U' ');
      }
      break;
    case 0x12:
      if (b2 < 0x40) replacePrevious(kExtended12[b2 - 0x20]);
      break;
    case 0x13:
      if (b2 < 0x40) replacePrevious(kExtended13[b2 - 0x20]);
      break;
    case 0x17:
      if (b2 >= 0x21 && b2 <= 0x23 && writing()) {
        Memory& m = target();
        m.col = std::min(m.col + (b2 - 0x20), kColumns - 1);
      }
      break;
    default:
      break;  // background colours and other attributes
  }
}

void Cea608::command(uint8_t code) {
  switch (code) {
    case 0x20:  // RCL: resume caption loading (pop-on)
      mode_ = Mode::PopOn;
      break;
    case 0x21:  // BS
      backspace();
      break;
    case 0x24:  // DER
      deleteToEnd();
      break;
    case 0x25:
    case 0x26:
    case 0x27:  // RU2, RU3, RU4
      startRollUp(code - 0x23);
      break;
    case 0x29:  // RDC: resume direct captioning (paint-on)
      mode_ = Mode::PaintOn;
      break;
    case 0x2A:
    case 0x2B:  // TR, RTD
      if (mode_ != Mode::Text) beforeText_ = mode_;
      mode_ = Mode::Text;
      break;
    case 0x2C:  // EDM
      displayed().clear();
      dirty_ = true;
      break;
    case 0x2D:  // CR
      carriageReturn();
      break;
    case 0x2E:  // ENM
      hidden().clear();
      break;
    case 0x2F:  // EOC: swap the memories
      shown_ ^= 1;
      mode_ = Mode::PopOn;
      dirty_ = true;
      break;
    default:
      break;  // AOF, AON, FON
  }
}

void Cea608::startRollUp(int rows) {
  Memory& m = displayed();
  // Text mode has a screen of its own: the captions under it are kept.
  Mode was = mode_ == Mode::Text ? beforeText_ : mode_;
  if (was != Mode::RollUp) {
    // Coming from pop-on or paint-on: those captions go.
    m.clear();
    hidden().clear();
    baseRow_ = kRows - 1;
    m.row = baseRow_;
    m.col = 0;
    m.italic = false;
    dirty_ = true;
  } else if (rows < rollRows_) {
    for (int r = 0; r <= baseRow_ - rows; ++r) m.clearRow(r);
    dirty_ = true;
  }
  mode_ = Mode::RollUp;
  rollRows_ = rows;
  if (baseRow_ < rows - 1) baseRow_ = rows - 1;
}

void Cea608::carriageReturn() {
  if (mode_ != Mode::RollUp) return;
  Memory& m = displayed();
  int top = std::max(0, baseRow_ - rollRows_ + 1);
  for (int r = 0; r < top; ++r) m.clearRow(r);
  for (int r = top; r < baseRow_; ++r) std::copy(m.cell[r + 1], m.cell[r + 1] + kColumns, m.cell[r]);
  m.clearRow(baseRow_);
  m.row = baseRow_;
  m.col = 0;
  dirty_ = true;
}

void Cea608::preamble(uint8_t b1, uint8_t b2) {
  int row = kPacRow[b1 & 0x07][(b2 & 0x20) ? 1 : 0];
  int attr = b2 & 0x1F;
  Memory& m = target();
  if (mode_ == Mode::RollUp) {
    // The preamble's row is the new base row; the window moves with it.
    int base = std::max(row, rollRows_ - 1);
    if (base != baseRow_) {
      Memory& d = displayed();
      Cell window[4][kColumns];
      for (int i = 0; i < rollRows_; ++i) {
        int from = baseRow_ - rollRows_ + 1 + i;
        for (int c = 0; c < kColumns; ++c) window[i][c] = from >= 0 ? d.cell[from][c] : Cell{};
      }
      d.clear();
      for (int i = 0; i < rollRows_; ++i) {
        std::copy(window[i], window[i] + kColumns, d.cell[base - rollRows_ + 1 + i]);
      }
      baseRow_ = base;
      dirty_ = true;
    }
    m.row = baseRow_;
  } else {
    m.row = row;
  }
  m.col = attr >= 0x10 ? ((attr - 0x10) >> 1) * 4 : 0;
  m.italic = attr == 0x0E || attr == 0x0F;
}

void Cea608::put(char32_t ch) {
  if (!writing()) return;
  Memory& m = target();
  int c = std::min(m.col, kColumns - 1);  // past the last column, it overwrites the last
  m.cell[m.row][c] = Cell{ch, m.italic};
  m.col = c + 1;
  touch(m);
}

void Cea608::replacePrevious(char32_t ch) {
  // An extended character follows a basic one that stands in for decoders
  // without it, and takes its place.
  if (!writing()) return;
  Memory& m = target();
  if (m.col > 0) m.col = std::min(m.col, kColumns) - 1;
  put(ch);
}

void Cea608::backspace() {
  if (!writing()) return;
  Memory& m = target();
  if (m.col == 0) return;
  m.col = std::min(m.col, kColumns) - 1;
  m.cell[m.row][m.col] = Cell{};
  touch(m);
}

void Cea608::deleteToEnd() {
  if (!writing()) return;
  Memory& m = target();
  std::fill(m.cell[m.row] + std::min(m.col, kColumns), m.cell[m.row] + kColumns, Cell{});
  touch(m);
}

void Cea608::refresh() const {
  if (!dirty_) return;
  dirty_ = false;
  const Memory& m = mem_[shown_];
  size_t n = 0;
  for (int r = 0; r < kRows; ++r) {
    const Cell* row = m.cell[r];
    int first = 0;
    while (first < kColumns && blank(row[first].ch)) ++first;
    if (first == kColumns) continue;
    int last = kColumns - 1;
    while (blank(row[last].ch)) --last;
    if (n == scratch_.lines.size()) scratch_.lines.emplace_back();
    Line& line = scratch_.lines[n++];
    line.text.clear();
    line.row = r;
    line.indent = first;
    int letters = 0, italic = 0;
    for (int c = first; c <= last; ++c) {
      char32_t ch = row[c].ch;
      appendUtf8(line.text, ch ? ch : U' ');
      if (!blank(ch)) {
        ++letters;
        italic += row[c].italic ? 1 : 0;
      }
    }
    line.italic = italic * 2 > letters;
  }
  scratch_.lines.resize(n);
  if (scratch_ == screen_) return;
  std::swap(screen_, scratch_);
  changed_ = true;
  if (!screen_.empty()) sawText_ = true;
}

const Screen& Cea608::screen() const {
  refresh();
  return screen_;
}

bool Cea608::takeChanged() {
  refresh();
  return std::exchange(changed_, false);
}

bool Cea608::sawText() const {
  refresh();
  return sawText_;
}

}  // namespace marigold::captions
