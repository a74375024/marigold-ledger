// The caption size and look the settings name, as each player shows them.
// The app draws captions itself on webOS and Android; on Apple TV VLC draws
// them over the picture, so its text renderer is given the same size.
#pragma once

#include <string>

namespace marigold::captions {

// The letters' size in px on the 1920x1080 layout for a caption size (0
// small, 1 medium, 2 large, 3 extra large; others clamped), as the app draws
// them.
int letterPx(int size);

// VLC's text renderer (Apple TV) sizes letters as the picture's height over
// a number (freetype-rel-fontsize; unset, TVVLCKit draws them a tenth of the
// height): the number that gives a caption size the same share of the
// screen as where the app draws them (Medium: 1080 / 25 = 43 px).
int vlcRelativeFontSize(int size);

// VLC's dark box behind 608/708 captions (its cc-opaque), for a look (0 box,
// 1 outline, 2 shadow): Box only. Without the box VLC edges the letters.
bool vlcCaptionBox(int look);

// For the log, values only: "size Medium (1/25 of the picture), look Box
// (boxed), timing +500 ms" (VLC holds captions back only, never earlier).
std::string vlcCaptionNote(int size, int look, int delayMs);

}  // namespace marigold::captions
