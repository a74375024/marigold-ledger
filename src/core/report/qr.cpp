#include "core/report/qr.hpp"

#include <exception>
#include <string>

#include "qrcodegen.hpp"

namespace marigold::report {

std::optional<QrMatrix> qrEncode(std::string_view text) {
  using qrcodegen::QrCode;
  using qrcodegen::QrSegment;
  try {
    std::vector<QrSegment> segments;
    if (text.find('\0') == std::string_view::npos) {
      // Picks the most compact mode for the whole text (makeSegments reads a
      // C string, hence the NUL check).
      std::string copy(text);
      segments = QrSegment::makeSegments(copy.c_str());
    } else {
      segments.push_back(QrSegment::makeBytes(std::vector<uint8_t>(text.begin(), text.end())));
    }
    // Level M exactly, like Core Image: no boost to a higher level.
    QrCode code = QrCode::encodeSegments(segments, QrCode::Ecc::MEDIUM, QrCode::MIN_VERSION,
                                         QrCode::MAX_VERSION, -1, false);
    QrMatrix m;
    m.size = code.getSize();
    m.dark.assign(static_cast<size_t>(m.size) * static_cast<size_t>(m.size), 0);
    for (int y = 0; y < m.size; ++y)
      for (int x = 0; x < m.size; ++x)
        m.dark[static_cast<size_t>(y) * static_cast<size_t>(m.size) + static_cast<size_t>(x)] =
            code.getModule(x, y) ? 1 : 0;
    return m;
  } catch (const std::exception&) {
    // data_too_long (or a length the segment can't carry).
    return std::nullopt;
  }
}

}  // namespace marigold::report
