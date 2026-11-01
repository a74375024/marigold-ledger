#include "core/captions/caption_style.hpp"

#include <algorithm>
#include <cmath>

namespace marigold::captions {
namespace {

const char* const kSizes[] = {"Small", "Medium", "Large", "Extra large"};
const char* const kLooks[] = {"Box", "Outline", "Shadow"};

}  // namespace

int letterPx(int size) {
  static const int kPx[] = {36, 44, 56, 72};
  return kPx[std::clamp(size, 0, 3)];
}

int vlcRelativeFontSize(int size) { return static_cast<int>(std::lround(1080.0 / letterPx(size))); }

bool vlcCaptionBox(int look) { return std::clamp(look, 0, 2) == 0; }

std::string vlcCaptionNote(int size, int look, int delayMs) {
  return std::string("size ") + kSizes[std::clamp(size, 0, 3)] + " (1/" + std::to_string(vlcRelativeFontSize(size)) +
         " of the picture), look " + kLooks[std::clamp(look, 0, 2)] + (vlcCaptionBox(look) ? " (boxed)" : " (edged)") +
         ", timing +" + std::to_string(std::max(delayMs, 0)) + " ms";
}

}  // namespace marigold::captions
