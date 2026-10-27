#include "platform/image.hpp"

namespace marigold::image {

std::optional<px::Image> decode(std::string_view bytes, int maxW, int maxH) {
  if (bytes.size() < 8 || bytes.size() > (16u << 20)) return std::nullopt;
  std::optional<px::Image> im = decodeFull(bytes);
  if (!im || static_cast<long>(im->w) * im->h > kMaxPixels) return std::nullopt;
  px::Image out = px::shrinkToFit(px::trimTransparent(std::move(*im)), maxW, maxH);
  if (out.empty()) return std::nullopt;
  return out;
}

}  // namespace marigold::image
