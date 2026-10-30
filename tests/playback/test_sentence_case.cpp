// Sentence case for ALL CAPS live captions.
#include <doctest.h>

#include <string>

#include "core/captions/sentence_case.hpp"

using marigold::captions::sentenceCase;

TEST_CASE("sentence case: sentences start with a capital") {
  CHECK(sentenceCase("HELLO THERE. HOW ARE YOU?", true) == "Hello there. How are you?");
  CHECK(sentenceCase("AND THEN WE LEFT.", false) == "and then we left.");
  CHECK(sentenceCase("WHAT?! NO... YES", true) == "What?! No... Yes");
  CHECK(sentenceCase("- WHERE ARE YOU GOING?", true) == "- Where are you going?");
  CHECK(sentenceCase("\"STOP!\" SHE SAID.", true) == "\"Stop!\" She said.");  // no way to tell
}

TEST_CASE("sentence case: a speaker marker starts a sentence") {
  CHECK(sentenceCase(">> GOOD EVENING. >> THANKS, JIM.", false) == ">> Good evening. >> Thanks, jim.");
  CHECK(sentenceCase("WELL >>> RIGHT", false) == "well >>> Right");
}

TEST_CASE("sentence case: I and its contractions") {
  CHECK(sentenceCase("YES I THINK I'LL GO, I'VE SAID I'D AND I'M SURE.", true) ==
        "Yes I think I'll go, I've said I'd and I'm sure.");
  CHECK(sentenceCase("SO I’M HERE", true) == "So I’m here");
  CHECK(sentenceCase("IT'S IN", true) == "It's in");
}

TEST_CASE("sentence case: words with digits stay as they are") {
  CHECK(sentenceCase("THE 49ERS LEAD 21-7 IN THE 3RD QUARTER", true) == "The 49ERS lead 21-7 in the 3RD quarter");
  CHECK(sentenceCase("IT'S 3.5 MILLION", true) == "It's 3.5 million");
  CHECK(sentenceCase("49ERS WIN", true) == "49ERS win");
}

TEST_CASE("sentence case: acronyms stay upper") {
  CHECK(sentenceCase("WATCH ESPN AND CNN ON TV, OK?", true) == "Watch ESPN and CNN on TV, OK?");
  CHECK(sentenceCase("THE NFL'S BEST QB SCORED A TD", true) == "The NFL's best QB scored a TD");
  CHECK(sentenceCase("NASCAR, F1 AND UFC ON FOX", true) == "NASCAR, F1 and UFC on FOX");
  CHECK(sentenceCase("NBA MVP FROM THE UK", true) == "NBA MVP from the UK");
}

TEST_CASE("sentence case: AM, PM and US only where they mean that") {
  CHECK(sentenceCase("AT 7 PM. I AM HERE", true) == "At 7 PM. I am here");
  CHECK(sentenceCase("TONIGHT AT 11:30 AM", true) == "Tonight at 11:30 AM");
  CHECK(sentenceCase("JOIN US FOR NEWS FROM THE US", true) == "Join us for news from the US");
  CHECK(sentenceCase("IN THE USA", true) == "In the USA");
  CHECK(sentenceCase("THE U.S. TEAM WON.", true) == "The U.S. team won.");
}

TEST_CASE("sentence case: mixed case is left alone") {
  CHECK(sentenceCase("Hello There, NFL fans", true) == "Hello There, NFL fans");
  CHECK(sentenceCase("already fine.", true) == "already fine.");
  CHECK(sentenceCase("", true).empty());
  CHECK(sentenceCase("123 -- 456", true) == "123 -- 456");
  // Mostly upper with a stray lower-case letter is still converted.
  CHECK(sentenceCase("McDONALD'S IS OPEN", true) == "Mcdonald's is open");
}

TEST_CASE("sentence case: accented capitals and music notes") {
  CHECK(sentenceCase("ÉL ESTÁ AQUÍ. ÑOÑO", true) == "Él está aquí. Ñoño");
  CHECK(sentenceCase("♪ LA LA LA ♪", true) == "♪ La la la ♪");
  CHECK(sentenceCase("STRAßE", true) == "Straße");
}
