// Sentence case for live captions, which are sent ALL CAPS.
#pragma once

#include <string>

namespace marigold::captions {

// Live 608 captions are ALL CAPS. Returns sentence case: lower-case, then capitalise the
// first letter of each sentence (after . ! ? and after a ">>" speaker marker, and the
// line's first letter when `startsSentence`), "I" and "I'm"/"I'll"/"I've"/"I'd" kept,
// words with digits kept as is, and a short list of acronyms kept upper (NFL NBA MLB NHL
// NCAA NASCAR ESPN CNN NBC CBS ABC FOX BBC UK US USA TV OK AM PM MVP UFC WWE F1 TD QB).
// Mixed-case input (not mostly upper) is returned unchanged.
//
// Two of those acronyms are also everyday words, so they are kept upper only
// where they read as the acronym: "AM" / "PM" after a number ("7 PM", not
// "I AM"), and "US" after "the" ("the US", not "tell us"). Dotted
// abbreviations ("U.S.", "A.M.") stay upper and don't end the sentence.
// Accented capitals (Latin-1, as 608 sends them) are lowered too.
std::string sentenceCase(const std::string& line, bool startsSentence);

}  // namespace marigold::captions
