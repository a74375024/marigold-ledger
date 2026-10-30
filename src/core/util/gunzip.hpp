// Streaming gzip reader for compressed guide files (.xml.gz). Feed it the
// bytes as they arrive; it passes inflated chunks on and never holds the
// whole file. Concatenated gzip members are read one after another. Input
// that does not start with the gzip magic passes through unchanged, so the
// caller does not need to know in advance.
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string_view>

namespace marigold::gz {

class Gunzip {
 public:
  using Sink = std::function<bool(std::string_view)>;  // false stops

  // `maxOutput` caps the inflated total (a guard against gzip bombs).
  explicit Gunzip(Sink sink, size_t maxOutput = size_t(1) << 30);
  ~Gunzip();
  Gunzip(const Gunzip&) = delete;
  Gunzip& operator=(const Gunzip&) = delete;

  // False when the data is corrupt, the cap is hit or the sink said stop.
  bool feed(std::string_view chunk);
  // True when the stream ended cleanly (or was plain data).
  bool finish();

  size_t output() const { return out_; }
  bool failed() const { return failed_; }

 private:
  bool inflateSome(std::string_view in);

  struct State;
  std::unique_ptr<State> z_;
  Sink sink_;
  size_t max_;
  size_t out_ = 0;
  int mode_ = 0;  // 0 = undecided, 1 = gzip, 2 = plain
  unsigned char head_[2] = {0, 0};
  size_t nHead_ = 0;
  bool failed_ = false;
  bool ended_ = false;  // last member finished
};

// Streaming gzip writer for the app's own saved files: deflates what it is
// fed and passes the compressed bytes to `sink`. Gunzip reads them back
// (and still reads files saved plain).
class Deflate {
 public:
  using Sink = std::function<bool(std::string_view)>;  // false stops

  // Level 1: fast, and most of the size win on JSON.
  explicit Deflate(Sink sink, int level = 1);
  ~Deflate();
  Deflate(const Deflate&) = delete;
  Deflate& operator=(const Deflate&) = delete;

  bool feed(std::string_view chunk);
  // Flushes the rest and the gzip trailer.
  bool finish();

 private:
  bool run(std::string_view in, int flush);

  struct State;
  std::unique_ptr<State> z_;
  Sink sink_;
  bool failed_ = false;
};

}  // namespace marigold::gz
