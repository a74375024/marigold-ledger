// CEA-608 decoding: the three caption styles, characters, channels, timing.
#include <doctest.h>

#include <string>
#include <vector>

#include "core/captions/cea608.hpp"

using marigold::captions::Cea608;
using marigold::captions::kIdleEraseMs;
using marigold::captions::Line;
using marigold::captions::Screen;

namespace {

// Sets bit 7 so the byte has odd parity, as broadcast.
uint8_t odd(uint8_t b) {
  b &= 0x7F;
  int ones = 0;
  for (int i = 0; i < 7; ++i) ones += (b >> i) & 1;
  return static_cast<uint8_t>(ones % 2 ? b : b | 0x80);
}

struct Feeder {
  explicit Feeder(int channel = 1) : d(channel), field(channel >= 3 ? 1 : 0) {}
  Cea608 d;
  int field;
  int64_t t = 1000;
  bool parity = true;

  void pair(uint8_t a, uint8_t b) {
    d.feed(field, parity ? odd(a) : a, parity ? odd(b) : b, t);
    t += 33;
  }
  // Control codes go out twice, as encoders send them.
  void code(uint8_t a, uint8_t b) {
    pair(a, b);
    pair(a, b);
  }
  void text(const std::string& s) {
    for (size_t i = 0; i < s.size(); i += 2) {
      pair(static_cast<uint8_t>(s[i]), i + 1 < s.size() ? static_cast<uint8_t>(s[i + 1]) : 0);
    }
  }
  std::vector<std::string> lines() const {
    std::vector<std::string> out;
    for (const Line& l : d.screen().lines) out.push_back(l.text);
    return out;
  }
};

using Lines = std::vector<std::string>;

// Data channel 1 control codes (field 1: CC1; field 2: CC3).
constexpr uint8_t kMisc = 0x14;
constexpr uint8_t RCL = 0x20, BS = 0x21, DER = 0x24, RU2 = 0x25, RU3 = 0x26, RDC = 0x29, TR = 0x2A, EDM = 0x2C,
                  CR = 0x2D, ENM = 0x2E, EOC = 0x2F;

}  // namespace

TEST_CASE("captions 608: roll-up 2 scrolls on CR and keeps two rows") {
  Feeder f;
  f.code(kMisc, RU2);
  f.code(kMisc, CR);
  f.code(0x14, 0x60);  // PAC row 15
  f.text("HELLO");
  CHECK(f.d.takeChanged());
  CHECK(f.lines() == Lines{"HELLO"});
  CHECK(f.d.screen().lines[0].row == 14);
  f.code(kMisc, CR);
  f.text("WORLD");
  CHECK(f.lines() == Lines{"HELLO", "WORLD"});
  CHECK(f.d.screen().lines[0].row == 13);
  f.code(kMisc, CR);
  f.text("AGAIN");
  CHECK(f.lines() == Lines{"WORLD", "AGAIN"});
  CHECK(f.d.sawText());
}

TEST_CASE("captions 608: a roll-up preamble moves the base row") {
  Feeder f;
  f.code(kMisc, RU3);
  f.code(0x13, 0x40);  // PAC row 12
  f.text("ONE");
  f.code(kMisc, CR);
  f.text("TWO");
  REQUIRE(f.d.screen().lines.size() == 2);
  CHECK(f.d.screen().lines[0].row == 10);
  CHECK(f.d.screen().lines[1].row == 11);
  // A preamble on another row moves the window there.
  f.code(0x14, 0x60);
  REQUIRE(f.d.screen().lines.size() == 2);
  CHECK(f.d.screen().lines[0].row == 13);
  CHECK(f.d.screen().lines[1].row == 14);
  CHECK(f.lines() == Lines{"ONE", "TWO"});
  f.code(kMisc, CR);
  f.text("THREE");
  CHECK(f.lines() == Lines{"ONE", "TWO", "THREE"});  // three rows
  CHECK(f.d.screen().lines[2].row == 14);
  f.code(kMisc, CR);
  CHECK(f.lines() == Lines{"TWO", "THREE"});
  CHECK(f.d.screen().lines[0].row == 12);
}

TEST_CASE("captions 608: pop-on builds off screen and shows on EOC") {
  Feeder f;
  f.code(kMisc, RCL);
  f.code(kMisc, ENM);
  f.code(0x13, 0x50);  // PAC row 12, indent 0
  f.text("WHERE ARE");
  f.code(0x13, 0x74);  // PAC row 13, indent 8
  f.text("YOU GOING?");
  CHECK(f.d.screen().empty());
  CHECK_FALSE(f.d.takeChanged());
  CHECK_FALSE(f.d.sawText());
  f.code(kMisc, EOC);
  CHECK(f.d.takeChanged());
  REQUIRE(f.d.screen().lines.size() == 2);
  CHECK(f.d.screen().lines[0] == Line{"WHERE ARE", 11, 0, false});
  CHECK(f.d.screen().lines[1] == Line{"YOU GOING?", 12, 8, false});
  CHECK(f.d.sawText());
  // The next caption replaces it on the next EOC.
  f.code(kMisc, ENM);
  f.code(0x14, 0x40);
  f.text("HOME.");
  CHECK(f.lines() == Lines{"WHERE ARE", "YOU GOING?"});
  f.code(kMisc, EOC);
  CHECK(f.lines() == Lines{"HOME."});
  CHECK(f.d.screen().lines[0].row == 13);
}

TEST_CASE("captions 608: paint-on shows each character as it comes") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x11, 0x40);  // PAC row 1
  f.text("AB");
  CHECK(f.lines() == Lines{"AB"});
  CHECK(f.d.screen().lines[0].row == 0);
  f.text("CD");
  CHECK(f.lines() == Lines{"ABCD"});
}

TEST_CASE("captions 608: EDM clears the screen, roll-up from pop-on erases it") {
  Feeder f;
  f.code(kMisc, RCL);
  f.code(0x14, 0x40);
  f.text("POP");
  f.code(kMisc, EOC);
  CHECK(f.lines() == Lines{"POP"});
  f.d.takeChanged();
  f.code(kMisc, EDM);
  CHECK(f.d.screen().empty());
  CHECK(f.d.takeChanged());

  f.code(kMisc, RCL);
  f.code(0x14, 0x40);
  f.text("AGAIN");
  f.code(kMisc, EOC);
  CHECK(f.lines() == Lines{"AGAIN"});
  f.code(kMisc, RU2);
  CHECK(f.d.screen().empty());
}

TEST_CASE("captions 608: special, extended and basic-set characters") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x14, 0x40);
  f.text("CAF\x5C ");  // 0x5C is é in the basic set
  f.text("NI\x7EO");   // 0x7E is ñ
  f.code(0x11, 0x37);  // ♪
  f.text(" ");
  f.text("U");
  f.code(0x12, 0x24);  // Ü replaces the U before it
  f.text("BER");
  f.code(0x11, 0x31);  // °
  f.text(" ");
  f.text("O");
  f.code(0x13, 0x33);  // ö replaces O
  CHECK(f.lines() == Lines{"CAFé NIñO♪ ÜBER° ö"});
  // A special character does not replace the one before it.
  f.code(kMisc, EDM);
  f.code(0x14, 0x40);
  f.text("A");
  f.code(0x11, 0x30);  // ®
  f.text("\x2A");      // á
  CHECK(f.lines() == Lines{"A®á"});
}

TEST_CASE("captions 608: backspace and delete to end of row") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x14, 0x40);
  f.text("ABCD");
  f.code(kMisc, BS);
  CHECK(f.lines() == Lines{"ABC"});
  f.text("XY");
  CHECK(f.lines() == Lines{"ABCXY"});
  f.code(0x14, 0x40);  // back to column 0
  f.text("Z");
  f.code(kMisc, DER);
  CHECK(f.lines() == Lines{"Z"});
}

TEST_CASE("captions 608: a control code repeated right away counts once") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x14, 0x40);
  f.text("ABCDEF");
  f.pair(kMisc, BS);
  f.pair(kMisc, BS);  // the redundant copy
  CHECK(f.lines() == Lines{"ABCDE"});
  // A third in a row is a new command (sent, then its copy, then a new one).
  f.pair(kMisc, BS);
  f.pair(kMisc, BS);
  f.pair(kMisc, BS);
  CHECK(f.lines() == Lines{"ABC"});
  // Anything in between makes the same code count again.
  f.text("Z");
  f.pair(kMisc, BS);
  f.text("Y");
  f.pair(kMisc, BS);
  CHECK(f.lines() == Lines{"ABC"});
}

TEST_CASE("captions 608: tab offsets and preamble indents give the line's indent") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x14, 0x52);  // PAC row 14, indent 4
  f.code(0x17, 0x22);  // TO2
  f.text("  HI   ");
  REQUIRE(f.d.screen().lines.size() == 1);
  CHECK(f.d.screen().lines[0].indent == 8);
  CHECK(f.d.screen().lines[0].text == "HI");
}

TEST_CASE("captions 608: italics from a preamble or a mid-row code") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x14, 0x4E);  // PAC row 14, white italics
  f.text("SINGING");
  f.code(0x14, 0x60);  // row 15, plain
  f.text("SAID");
  f.code(0x11, 0x2E);  // mid-row italics
  f.text("LOUDLY");
  REQUIRE(f.d.screen().lines.size() == 2);
  CHECK(f.d.screen().lines[0].italic);
  CHECK(f.d.screen().lines[1].text == "SAID LOUDLY");
  CHECK(f.d.screen().lines[1].italic);  // 6 of 10 letters
  f.code(0x11, 0x20);  // mid-row white: italics off
  f.text("AND THEN SOME");
  CHECK_FALSE(f.d.screen().lines[1].italic);
}

TEST_CASE("captions 608: channel 2 data doesn't leak into channel 1") {
  Feeder f1(1);
  Feeder f2(2);
  auto both = [&](uint8_t a, uint8_t b, bool twice) {
    for (Feeder* f : {&f1, &f2}) {
      f->pair(a, b);
      if (twice) f->pair(a, b);
    }
  };
  auto text = [&](const std::string& s) {
    f1.text(s);
    f2.text(s);
  };
  both(0x14, RU2, true);
  both(0x14, 0x60, true);
  text("ONE");
  both(0x1C, RU2, true);  // CC2 from here on
  both(0x1C, 0x60, true);
  text("TWO");
  CHECK(f1.lines() == Lines{"ONE"});
  CHECK(f2.lines() == Lines{"TWO"});
  both(0x14, CR, true);  // back to CC1
  text("THREE");
  CHECK(f1.lines() == Lines{"ONE", "THREE"});
  CHECK(f2.lines() == Lines{"TWO"});
}

TEST_CASE("captions 608: field 2 carries CC3, and field 1 doesn't reach it") {
  Feeder f(3);
  CHECK(f.field == 1);
  f.code(0x15, RU2);  // CEA-608-E sends field 2 commands with 0x15
  f.code(0x14, 0x60);
  f.text("FIELD TWO");
  CHECK(f.lines() == Lines{"FIELD TWO"});
  f.code(0x14, CR);  // 0x14 works there too
  f.text("MORE");
  CHECK(f.lines() == Lines{"FIELD TWO", "MORE"});
  // The same bytes on field 1 are CC1's.
  f.field = 0;
  f.code(0x14, CR);
  f.text("NOT HERE");
  CHECK(f.lines() == Lines{"FIELD TWO", "MORE"});
  Feeder cc1(1);
  cc1.field = 1;
  cc1.code(0x14, RU2);
  cc1.text("NOT CC1");
  CHECK(cc1.d.screen().empty());
}

TEST_CASE("captions 608: XDS and text mode stay out of the captions") {
  Feeder f(3);
  f.code(0x14, RU2);
  f.code(0x14, 0x60);
  f.text("CAP");
  f.pair(0x01, 0x03);  // XDS start: program name
  f.text("SECRET");
  f.pair(0x0F, 0x1D);  // end, checksum
  f.text("S");
  CHECK(f.lines() == Lines{"CAPS"});
  f.code(0x14, TR);
  f.text("TEXT SERVICE");
  f.code(0x14, CR);  // ignored in text mode
  CHECK(f.lines() == Lines{"CAPS"});
  f.code(0x14, RU2);
  f.code(0x14, CR);
  f.text("BACK");
  CHECK(f.lines() == Lines{"CAPS", "BACK"});
}

TEST_CASE("captions 608: captions with no data for 16 s are erased") {
  Feeder f;
  f.code(kMisc, RU2);
  f.code(0x14, 0x60);
  f.text("STALE");
  f.d.takeChanged();
  int64_t last = f.t;
  f.d.tick(last + kIdleEraseMs - 500);
  CHECK(f.lines() == Lines{"STALE"});
  CHECK_FALSE(f.d.takeChanged());
  f.d.tick(last + kIdleEraseMs + 100);
  CHECK(f.d.screen().empty());
  CHECK(f.d.takeChanged());
  CHECK(f.d.sawText());
  // Padding is not data.
  f.t = last + 2 * kIdleEraseMs;
  f.code(kMisc, RU2);
  f.text("FRESH");
  for (int i = 0; i < 600; ++i) {
    f.pair(0x00, 0x00);
    f.t += 33;
  }
  CHECK(f.d.screen().empty());
}

TEST_CASE("captions 608: parity bits are stripped, and missing parity still decodes") {
  for (bool parity : {true, false}) {
    Feeder f;
    f.parity = parity;
    f.code(kMisc, RU2);
    f.code(0x14, 0x60);
    f.text("PARITY");
    CHECK(f.lines() == Lines{"PARITY"});
  }
  // Every byte flagged wrong (bit 7 flipped from odd parity) decodes the same.
  Cea608 d;
  auto even = [](uint8_t b) { return static_cast<uint8_t>(odd(b) ^ 0x80); };
  int64_t t = 0;
  for (int i = 0; i < 2; ++i) d.feed(0, even(0x14), even(RU2), t += 33);
  d.feed(0, even('O'), even('K'), t += 33);
  REQUIRE(d.screen().lines.size() == 1);
  CHECK(d.screen().lines[0].text == "OK");
}

TEST_CASE("captions 608: text before any mode command and after reset is ignored") {
  Feeder f;
  f.text("JOINED MID CAPTION");
  CHECK(f.d.screen().empty());
  f.code(kMisc, RU2);
  f.text("OK");
  CHECK(f.lines() == Lines{"OK"});
  f.d.takeChanged();
  f.d.reset();
  CHECK(f.d.screen().empty());
  CHECK(f.d.takeChanged());
  CHECK_FALSE(f.d.sawText());
  f.text("AFTER");
  CHECK(f.d.screen().empty());
}

TEST_CASE("captions 608: a full row overwrites its last column") {
  Feeder f;
  f.code(kMisc, RDC);
  f.code(0x14, 0x40);
  f.text(std::string(32, 'A'));
  f.text("BC");
  REQUIRE(f.d.screen().lines.size() == 1);
  CHECK(f.d.screen().lines[0].text == std::string(31, 'A') + "C");
}
