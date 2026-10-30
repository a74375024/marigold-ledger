#include "core/util/gunzip.hpp"

#include <zlib.h>

#include <vector>

namespace marigold::gz {

struct Gunzip::State {
  z_stream s{};
  bool ready = false;
  std::vector<unsigned char> buf = std::vector<unsigned char>(64 * 1024);
  ~State() {
    if (ready) inflateEnd(&s);
  }
};

Gunzip::Gunzip(Sink sink, size_t maxOutput)
    : z_(std::make_unique<State>()), sink_(std::move(sink)), max_(maxOutput) {}

Gunzip::~Gunzip() = default;

bool Gunzip::feed(std::string_view chunk) {
  if (failed_) return false;
  if (chunk.empty()) return true;
  if (mode_ == 0) {
    // Decide from the first two bytes, which may arrive in separate chunks.
    while (nHead_ < 2 && !chunk.empty()) {
      head_[nHead_++] = static_cast<unsigned char>(chunk.front());
      chunk.remove_prefix(1);
    }
    if (nHead_ < 2) return true;
    mode_ = head_[0] == 0x1f && head_[1] == 0x8b ? 1 : 2;
    std::string_view head(reinterpret_cast<const char*>(head_), 2);
    if (mode_ == 2) {
      out_ += 2;
      if (out_ > max_ || !sink_(head)) return !(failed_ = true);
    } else {
      if (inflateInit2(&z_->s, 16 + 15) != Z_OK) return !(failed_ = true);
      z_->ready = true;
      if (!inflateSome(head)) return false;
    }
  }
  if (chunk.empty()) return true;
  if (mode_ == 2) {
    out_ += chunk.size();
    if (out_ > max_ || !sink_(chunk)) return !(failed_ = true);
    return true;
  }
  return inflateSome(chunk);
}

bool Gunzip::inflateSome(std::string_view in) {
  z_stream& s = z_->s;
  s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
  s.avail_in = static_cast<uInt>(in.size());
  while (s.avail_in > 0) {
    if (ended_) {
      // Zero padding after the last member is common on some servers.
      while (s.avail_in > 0 && *s.next_in == 0) {
        ++s.next_in;
        --s.avail_in;
      }
      if (s.avail_in == 0) break;
      // Another member follows: start over.
      if (inflateReset(&s) != Z_OK) return !(failed_ = true);
      ended_ = false;
    }
    s.next_out = z_->buf.data();
    s.avail_out = static_cast<uInt>(z_->buf.size());
    int r = inflate(&s, Z_NO_FLUSH);
    if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) return !(failed_ = true);
    size_t n = z_->buf.size() - s.avail_out;
    if (n) {
      out_ += n;
      if (out_ > max_) return !(failed_ = true);
      if (!sink_(std::string_view(reinterpret_cast<const char*>(z_->buf.data()), n)))
        return !(failed_ = true);
    }
    if (r == Z_STREAM_END) {
      ended_ = true;
    } else if (r == Z_BUF_ERROR && n == 0) {
      break;  // needs more input
    }
  }
  return true;
}

bool Gunzip::finish() {
  if (failed_) return false;
  if (mode_ == 0) {
    // Fewer than two bytes in total: plain data.
    if (nHead_ && !sink_(std::string_view(reinterpret_cast<const char*>(head_), nHead_)))
      return !(failed_ = true);
    out_ += nHead_;
    mode_ = 2;
    return true;
  }
  return mode_ == 2 || ended_;
}

struct Deflate::State {
  z_stream s{};
  bool ready = false;
  std::vector<unsigned char> buf = std::vector<unsigned char>(64 * 1024);
  ~State() {
    if (ready) deflateEnd(&s);
  }
};

Deflate::Deflate(Sink sink, int level) : z_(std::make_unique<State>()), sink_(std::move(sink)) {
  // 16 + 15: a gzip wrapper, the largest window.
  if (deflateInit2(&z_->s, level, Z_DEFLATED, 16 + 15, 8, Z_DEFAULT_STRATEGY) == Z_OK) z_->ready = true;
  else failed_ = true;
}

Deflate::~Deflate() = default;

bool Deflate::run(std::string_view in, int flush) {
  if (failed_) return false;
  z_->s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
  z_->s.avail_in = static_cast<uInt>(in.size());
  int rc;
  do {
    z_->s.next_out = z_->buf.data();
    z_->s.avail_out = static_cast<uInt>(z_->buf.size());
    rc = deflate(&z_->s, flush);
    if (rc == Z_STREAM_ERROR) return !(failed_ = true);
    size_t have = z_->buf.size() - z_->s.avail_out;
    if (have && !sink_(std::string_view(reinterpret_cast<const char*>(z_->buf.data()), have)))
      return !(failed_ = true);
  } while (z_->s.avail_out == 0 || (flush == Z_FINISH && rc != Z_STREAM_END));
  return true;
}

bool Deflate::feed(std::string_view chunk) {
  // zlib takes at most 4 GB a call; saved files are far smaller, but keep
  // the calls bounded anyway.
  while (chunk.size() > (1u << 20)) {
    if (!run(chunk.substr(0, 1u << 20), Z_NO_FLUSH)) return false;
    chunk.remove_prefix(1u << 20);
  }
  return chunk.empty() || run(chunk, Z_NO_FLUSH);
}

bool Deflate::finish() { return run({}, Z_FINISH); }

}  // namespace marigold::gz
