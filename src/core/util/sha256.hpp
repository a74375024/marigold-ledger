// SHA-256 (FIPS 180-4), for checking a downloaded update against the
// digest its release gives (Build 148). Fed in pieces as a download
// arrives, so a 50 MB file is never held in memory.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace marigold::sha256 {

class Hasher {
 public:
  void update(std::string_view bytes);
  // The digest as 64 lowercase hex digits. Ends the hasher: update() after
  // it starts nothing new (make another).
  std::string hex();

 private:
  void block(const uint8_t* p);
  std::array<uint32_t, 8> h_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  std::array<uint8_t, 64> buf_{};
  size_t used_ = 0;
  uint64_t total_ = 0;  // bytes
};

// The digest of `bytes`, as 64 lowercase hex digits.
std::string hex(std::string_view bytes);

}  // namespace marigold::sha256
