// Image decoding for logos and artwork, on worker threads. The platform
// decodes (webOS: image_sdl.cpp, the TV's SDL2_image and libwebp; Android:
// android/image.cpp, the system's decoder); trimming and shrinking are shared.
#pragma once

#include <optional>
#include <string_view>

#include "core/util/pixels.hpp"

namespace marigold::image {

// Larger sources are refused (a logo never needs this).
constexpr long kMaxPixels = 4096L * 4096L;

// Decodes `bytes` and shrinks the result to fit maxW x maxH. Thread safe.
std::optional<px::Image> decode(std::string_view bytes, int maxW, int maxH);

// The platform's decoder: PNG, JPEG, GIF (first frame) and WebP, full size,
// RGBA with straight alpha. It may refuse sources over kMaxPixels. Thread safe.
std::optional<px::Image> decodeFull(std::string_view bytes);

}  // namespace marigold::image
