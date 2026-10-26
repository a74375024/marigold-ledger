// QR codes are checked structurally (size, finder and timing patterns,
// format and version information) and, for versions 1-6, decoded back: a
// small reader walks the data modules, removes the mask, de-interleaves the
// blocks and reads the byte / alphanumeric segment. No image libraries.
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/report/qr.hpp"
#include "doctest.h"

using marigold::report::QrMatrix;
using marigold::report::qrEncode;

namespace {

// Format information around the top-left finder, unmasked: (ecc bits << 3)
// | mask, or -1 when its BCH check fails or the two copies differ.
int formatData(const QrMatrix& m) {
  auto bit = [&](int x, int y) { return m.at(x, y) ? 1 : 0; };
  int a = 0, b = 0;
  for (int i = 0; i <= 5; ++i) a |= bit(8, i) << i;
  a |= bit(8, 7) << 6;
  a |= bit(8, 8) << 7;
  a |= bit(7, 8) << 8;
  for (int i = 9; i < 15; ++i) a |= bit(14 - i, 8) << i;
  for (int i = 0; i < 8; ++i) b |= bit(m.size - 1 - i, 8) << i;
  for (int i = 8; i < 15; ++i) b |= bit(8, m.size - 15 + i) << i;
  if (a != b) return -1;
  int bits = a ^ 0x5412;
  int data = bits >> 10;
  int rem = data;
  for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  if (((data << 10) | (rem & 0x3FF)) != bits) return -1;
  return data;
}

// Version information (versions 7+), both copies; -1 when they differ or
// the BCH check fails.
int versionInfo(const QrMatrix& m) {
  long a = 0, b = 0;
  for (int i = 0; i < 18; ++i) {
    int x = m.size - 11 + i % 3, y = i / 3;
    a |= static_cast<long>(m.at(x, y)) << i;
    b |= static_cast<long>(m.at(y, x)) << i;
  }
  if (a != b) return -1;
  int version = static_cast<int>(a >> 12);
  int rem = version;
  for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
  if (((static_cast<long>(version) << 12) | (rem & 0xFFF)) != a) return -1;
  return version;
}

bool finderAt(const QrMatrix& m, int x0, int y0) {
  for (int dy = -1; dy <= 7; ++dy)
    for (int dx = -1; dx <= 7; ++dx) {
      int d = std::max(std::abs(dx - 3), std::abs(dy - 3));
      bool want = d != 2 && d != 4;  // dark core (0-1), dark ring (3), light ring and separator
      if (m.at(x0 + dx, y0 + dy) != want) return false;
    }
  return true;
}

bool maskBit(int mask, int x, int y) {
  switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}

// Level M block layout for versions 1-6: (blocks, data codewords per block).
// Every block of these versions has the same length.
struct Layout {
  int blocks, data, total;
};
const Layout kLayoutM[7] = {{0, 0, 0},   {1, 16, 26},  {1, 28, 44}, {1, 44, 70},
                            {2, 32, 100}, {2, 43, 134}, {4, 27, 172}};

// Decodes a version 1-6 symbol back to its text ("" on any problem).
std::string decode(const QrMatrix& m) {
  const int version = m.version();
  if (version < 1 || version > 6) return "";
  const int size = m.size;
  const int mask = formatData(m) & 7;
  auto isFunction = [&](int x, int y) {
    if (x == 6 || y == 6) return true;                       // timing
    if (x <= 8 && y <= 8) return true;                       // top-left finder + format
    if (x >= size - 8 && y <= 8) return true;                // top-right
    if (x <= 8 && y >= size - 8) return true;                // bottom-left + dark module
    if (version >= 2 && std::abs(x - (size - 7)) <= 2 && std::abs(y - (size - 7)) <= 2)
      return true;                                           // the one alignment pattern
    return false;
  };
  std::vector<int> bits;
  for (int right = size - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < size; ++vert)
      for (int j = 0; j < 2; ++j) {
        int x = right - j;
        bool upward = ((right + 1) & 2) == 0;
        int y = upward ? size - 1 - vert : vert;
        if (!isFunction(x, y)) bits.push_back((m.at(x, y) != maskBit(mask, x, y)) ? 1 : 0);
      }
  }
  const Layout l = kLayoutM[version];
  std::vector<int> codewords(static_cast<size_t>(l.total), 0);
  for (int i = 0; i < l.total; ++i)
    for (int k = 0; k < 8; ++k) codewords[static_cast<size_t>(i)] =
        (codewords[static_cast<size_t>(i)] << 1) | bits[static_cast<size_t>(i * 8 + k)];
  // Data codewords are interleaved block by block.
  std::vector<int> data;
  for (int b = 0; b < l.blocks; ++b)
    for (int i = 0; i < l.data; ++i) data.push_back(codewords[static_cast<size_t>(i * l.blocks + b)]);
  std::vector<int> stream;
  for (int c : data)
    for (int k = 7; k >= 0; --k) stream.push_back((c >> k) & 1);
  size_t pos = 0;
  auto read = [&](int n) {
    int v = 0;
    for (int i = 0; i < n; ++i) v = (v << 1) | (pos < stream.size() ? stream[pos++] : 0);
    return v;
  };
  std::string out;
  int mode = read(4);
  if (mode == 4) {  // byte
    int count = read(8);
    for (int i = 0; i < count; ++i) out += static_cast<char>(read(8));
  } else if (mode == 2) {  // alphanumeric
    static const char kAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    int count = read(9);
    for (int i = 0; i + 1 < count; i += 2) {
      int v = read(11);
      out += kAlnum[v / 45];
      out += kAlnum[v % 45];
    }
    if (count % 2) out += kAlnum[read(6)];
  } else {
    return "";
  }
  if (read(4) != 0 && pos < stream.size()) return "";  // terminator
  return out;
}

void checkStructure(const QrMatrix& m) {
  REQUIRE(m.size >= 21);
  REQUIRE((m.size - 17) % 4 == 0);
  REQUIRE(m.dark.size() == static_cast<size_t>(m.size * m.size));
  CHECK(finderAt(m, 0, 0));
  CHECK(finderAt(m, m.size - 7, 0));
  CHECK(finderAt(m, 0, m.size - 7));
  CHECK_FALSE(finderAt(m, m.size - 7, m.size - 7));  // three finders only
  for (int i = 8; i < m.size - 8; ++i) {
    CHECK(m.at(i, 6) == (i % 2 == 0));
    CHECK(m.at(6, i) == (i % 2 == 0));
  }
  CHECK(m.at(8, m.size - 8));  // the dark module
  int format = formatData(m);
  REQUIRE(format >= 0);
  CHECK((format >> 3) == 0);  // error correction level M
  if (m.version() >= 7) CHECK(versionInfo(m) == m.version());
  CHECK_FALSE(m.at(-1, 0));
  CHECK_FALSE(m.at(0, m.size));
}

}  // namespace

TEST_CASE("qr: byte mode text round-trips and has the right structure") {
  for (std::string text : {std::string("hello"), std::string("a"),
                           std::string("http://192.168.1.20:40123/0123456789abcdef0123456789abcdef/"
                                       "marigold-channels.csv"),
                           std::string("http://10.0.0.7:5/x")}) {
    auto m = qrEncode(text);
    REQUIRE(m);
    checkStructure(*m);
    CHECK(decode(*m) == text);
  }
}

TEST_CASE("qr: alphanumeric text uses the compact mode") {
  auto m = qrEncode("HELLO WORLD");
  REQUIRE(m);
  checkStructure(*m);
  CHECK(m->version() == 1);
  CHECK(decode(*m) == "HELLO WORLD");
}

TEST_CASE("qr: the smallest version that holds the text at level M") {
  // Byte-mode capacities at level M: v1 14, v2 26, v4 62, v6 106, v10 213.
  struct Case {
    size_t bytes;
    int version;
  };
  for (Case c : {Case{14, 1}, Case{15, 2}, Case{26, 2}, Case{27, 3}, Case{62, 4}, Case{63, 5},
                 Case{106, 6}, Case{107, 7}, Case{213, 10}, Case{214, 11}, Case{2331, 40}}) {
    std::string text(c.bytes, 'q');
    auto m = qrEncode(text);
    REQUIRE(m);
    CHECK(m->version() == c.version);
    CHECK(m->size == 17 + 4 * c.version);
    checkStructure(*m);
    if (c.version <= 6) CHECK(decode(*m) == text);
  }
}

TEST_CASE("qr: too long or unusual input") {
  CHECK_FALSE(qrEncode(std::string(2332, 'q')));
  auto withNul = qrEncode(std::string("a\0b", 3));
  REQUIRE(withNul);
  CHECK(decode(*withNul) == std::string("a\0b", 3));
  auto empty = qrEncode("");
  REQUIRE(empty);
  CHECK(empty->version() == 1);
  checkStructure(*empty);
  auto utf8 = qrEncode("Caf\xC3\xA9 TV");
  REQUIRE(utf8);
  CHECK(decode(*utf8) == "Caf\xC3\xA9 TV");
}
