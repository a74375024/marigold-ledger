// Image decoding for logos and artwork, on worker threads. PNG, JPEG and GIF
// go through the TV's SDL2_image; WebP through the TV's libwebp, opened with
// dlopen like Legacy does (the TV's SDL2_image is built without WebP).
#pragma once

#include <optional>
#include <string_view>

#include "core/util/pixels.hpp"

namespace marigold::image {

// Decodes `bytes` and shrinks the result to fit maxW x maxH. Thread safe.
std::optional<px::Image> decode(std::string_view bytes, int maxW, int maxH);

}  // namespace marigold::image
