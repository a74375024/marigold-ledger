#include "core/util/sha256.hpp"

#include <string>

#include "doctest.h"

using marigold::sha256::Hasher;
using marigold::sha256::hex;

TEST_CASE("sha256: the standard test vectors") {
  // FIPS 180-4 examples and the NIST short/long messages.
  CHECK(hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(hex("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrst"
            "nopqrstu") == "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
  CHECK(hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  CHECK(hex("The quick brown fox jumps over the lazy dog") ==
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST_CASE("sha256: fed in pieces of any size, the same digest") {
  std::string data;
  for (int i = 0; i < 5000; ++i) data += static_cast<char>((i * 131 + 7) & 0xFF);
  std::string whole = hex(data);
  for (size_t piece : {1u, 3u, 55u, 56u, 63u, 64u, 65u, 127u, 4096u}) {
    Hasher h;
    for (size_t at = 0; at < data.size(); at += piece) h.update(std::string_view(data).substr(at, piece));
    CHECK(h.hex() == whole);
  }
  // The lengths around the padding's edge (55, 56, 64 bytes) differ.
  CHECK(hex(std::string(55, 'x')) != hex(std::string(56, 'x')));
  CHECK(hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
  // One byte changed: another digest.
  std::string other = data;
  other[2500] = static_cast<char>(other[2500] ^ 1);
  CHECK(hex(other) != whole);
}
