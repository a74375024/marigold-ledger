// QR codes for the export screen (QRCode.image in tvOS, which used Core
// Image's generator at correction level M). The encoder is Project Nayuki's
// QR Code generator (vendor/qrcodegen); this returns the bare module grid and
// the UI draws it: each module as a square block, dark on white, with a
// quiet zone of kQuietZone light modules around it (Core Image added that
// margin itself).
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace marigold::report {

// Light modules the reader needs around the code (ISO/IEC 18004).
constexpr int kQuietZone = 4;

struct QrMatrix {
  int size = 0;                // modules per side: 21 (version 1) to 177
  std::vector<uint8_t> dark;   // size * size, row by row; 1 = dark

  // Out-of-range coordinates read as light (the quiet zone).
  bool at(int x, int y) const {
    if (x < 0 || y < 0 || x >= size || y >= size) return false;
    return dark[static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)] != 0;
  }
  int version() const { return (size - 17) / 4; }
};

// `text` (UTF-8, e.g. the share URL) at correction level M, in the smallest
// version that holds it; the encoder picks numeric, alphanumeric or byte
// mode. Empty when the text is too long for a QR code (over 2331 bytes at M).
std::optional<QrMatrix> qrEncode(std::string_view text);

}  // namespace marigold::report
