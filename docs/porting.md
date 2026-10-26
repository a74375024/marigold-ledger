# Porting the tvOS logic to C++

Rules for code in `src/core/`. They apply to every module ported from the
tvOS app (`tvosApp/NuvioTV/Sources/Core/LiveTV` on the `multiview` branch).

## Faithful first

- Port function by function. Keep the Swift names in English camelCase
  (`ChannelCatalog.traitsFor` becomes `catalog::traitsFor`), the same
  constants, thresholds and order of checks.
- Keep the intent of the Swift doc comments, rewritten as short English
  comments. Don't invent behaviour; when Swift relies on a Foundation
  detail, say in a comment what the C++ does instead.
- Regular expressions: use `core/util/rx.hpp` with the same pattern
  strings. It is PCRE2 in UTF+UCP mode, which reads ICU patterns the same
  way (look-behinds, `\b`, Unicode classes, `$1` templates).
- Dates: `core/util/time.hpp` (`Seconds` since the epoch, `Zone::named`
  for the IANA zones Swift uses, `Zone::device()` for `TimeZone.current`).
- Strings: `core/util/strings.hpp` (`foldDiacritics` is Swift's
  `.diacriticInsensitive`).
- Shared types: `core/model/channel.hpp` (`Channel`, `Program`).
- JSON: `core/util/json.hpp` for settings, caches and small bodies (whole
  tree). Large arrays need a streaming reader (the sources module adds one).

## Shape

- Namespace `marigold::<module>`, files under `src/core/<module>/`, tests
  under `tests/<module>/test_*.cpp`. Sources and tests are globbed by
  CMake; don't edit CMake files.
- `src/core/` never includes SDL, GL, curl, LS2 or vendored Legacy code. No
  file or network I/O: functions take strings or byte chunks and return
  values. Where Swift downloads or reads a file, take the bytes as input or
  take a small callback.
- No globals with mutable state, no singletons. Caches are members of an
  object the caller owns. Large results are plain structs the store
  publishes as immutable snapshots, so they must be copyable or movable
  and never hold pointers into temporaries.
- Bounded memory: streaming parsers take chunks and keep only what the
  caller asked for. Caps (channels, programmes, string lengths) are
  constants named after the Swift ones.

## Safety

- C++17, `-Wall -Wextra -Werror` plus the flags in `cmake/warnings.cmake`
  (no old-style casts, no shadowing). Must pass under ASan+UBSan and TSan.
- No fixed-size char buffers; use `std::string`, `std::string_view` and
  `std::vector`. Check every index.
- Never log or format a URL, user name, password or token. Use
  `marigold::redact()` if a message could contain one.

## Tests

- doctest (`#include "doctest.h"`). Synthetic data only: invented channel
  names that exercise the rules (e.g. "US: ESPN FHD", "UK| Sky Sports
  Main", "NCAAF 16: Team A vs Team B 7:30 PM ET"). URLs only on the
  reserved `example.invalid` domain. Never commit real channel lists,
  guide files or logs.
- Cover each rule the Swift comments describe, and the edge cases.
- Build and run before handing back:
  ```
  cmake -S . -B build-asan -DMARIGOLD_SANITIZE=address,undefined -DCMAKE_BUILD_TYPE=Debug
  cmake --build build-asan && ./build-asan/tests/marigold_tests
  cmake -S . -B build-tsan -DMARIGOLD_SANITIZE=thread -DCMAKE_BUILD_TYPE=Debug
  cmake --build build-tsan && ./build-tsan/tests/marigold_tests
  ```
  (Locally the sanitizers need gcc: `CC=gcc CXX=g++`.)
