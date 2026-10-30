// CEA-608 closed captions: one caption channel decoded into a screen of lines.
//
// Line 21 captions as carried in ATSC cc_data (see ts_captions.hpp): two
// bytes at a time, odd parity in bit 7. Field 1 carries CC1 / CC2, field 2
// carries CC3 / CC4 (and XDS). Control codes say which of a field's two data
// channels the following characters belong to, and are sent twice so a
// lost pair doesn't lose the command: the repeat is ignored.
//
// The three caption styles are all handled: pop-on (built off screen, then
// swapped on with EOC), roll-up (2, 3 or 4 rows scrolling up from a base
// row) and paint-on (each character appears as it arrives). Text mode and
// XDS are recognised only so their bytes stay out of the captions. Colours,
// underline and flash are dropped; italics are kept per line.
//
// No timers: every input carries the presentation time in ms, and tick()
// lets time pass without data (captions left up 16 s with nothing new are
// erased, as TV decoders do). The object is used from one thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace marigold::captions {

inline constexpr int kRows = 15;
inline constexpr int kColumns = 32;
// Captions left up with no data on the channel this long are erased.
inline constexpr int64_t kIdleEraseMs = 16000;

struct Line {
  std::string text;      // UTF-8, trailing spaces trimmed; 608 special/extended chars mapped to Unicode
  int row = 0;           // 0..14, the 608 row it sits on
  int indent = 0;        // columns from the left (PAC indent + tab offsets + leading spaces)
  bool italic = false;   // most of the line's characters are italic
  bool operator==(const Line& o) const;
  bool operator!=(const Line& o) const { return !(*this == o); }
};

struct Screen {
  std::vector<Line> lines;  // top to bottom, only non-empty rows
  bool empty() const { return lines.empty(); }
  bool operator==(const Screen& o) const;
  bool operator!=(const Screen& o) const { return !(*this == o); }
};

// Decodes one caption channel (1..4: CC1/CC2 on field 1, CC3/CC4 on field 2).
class Cea608 {
 public:
  explicit Cea608(int channel = 1);

  // One cc pair as carried (with odd parity bits). `field` 0 = field 1, 1 = field 2.
  // `nowMs` is presentation time in ms (monotonic within a stream).
  void feed(int field, uint8_t b1, uint8_t b2, int64_t nowMs);
  // Advances time without data (clears a stale screen: captions with no data for
  // 16 s are erased, as decoders do).
  void tick(int64_t nowMs);

  const Screen& screen() const;  // what is displayed now
  bool takeChanged();            // screen changed since the last call
  bool sawText() const;          // this channel has ever shown text
  void reset();                  // channel change

  int channel() const { return channel_; }

 private:
  enum class Mode { None, PopOn, PaintOn, RollUp, Text };
  struct Cell {
    char32_t ch = 0;  // 0 = nothing here
    bool italic = false;
  };
  struct Memory {
    Cell cell[kRows][kColumns];
    int row = kRows - 1;
    int col = 0;
    bool italic = false;
    void clear();
    void clearRow(int r);
  };

  void control(uint8_t b1, uint8_t b2);
  void command(uint8_t code);
  void preamble(uint8_t b1, uint8_t b2);
  void startRollUp(int rows);
  void carriageReturn();
  void put(char32_t ch);
  void replacePrevious(char32_t ch);
  void backspace();
  void deleteToEnd();
  void expire(int64_t nowMs);
  Memory& displayed() { return mem_[shown_]; }
  Memory& hidden() { return mem_[shown_ ^ 1]; }
  Memory& target();
  bool writing() const { return mode_ != Mode::None && mode_ != Mode::Text; }
  void touch(const Memory& m);
  void refresh() const;

  int channel_;
  int field_;        // 0 or 1
  int dataChannel_;  // 0 or 1 within the field
  int current_ = 0;  // data channel of the last control code on our field
  bool xds_ = false;
  Mode mode_ = Mode::None;
  Mode beforeText_ = Mode::None;  // the caption mode text mode interrupted
  int rollRows_ = 2;
  int baseRow_ = kRows - 1;
  Memory mem_[2];
  int shown_ = 0;
  bool haveLast_ = false;
  uint8_t last1_ = 0, last2_ = 0;
  bool haveTime_ = false;
  int64_t lastDataMs_ = 0;

  mutable Screen screen_;
  mutable Screen scratch_;
  mutable bool dirty_ = false;
  mutable bool changed_ = false;
  mutable bool sawText_ = false;
};

}  // namespace marigold::captions
