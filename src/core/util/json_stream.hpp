// Streaming JSON reader for bodies too large to hold as one tree (an Xtream
// get_live_streams list can be 60 MB, an Emby channel list several MB).
//
// StreamReader is a push tokenizer: feed it byte chunks of any size, cut
// anywhere (inside strings, escapes, \u surrogate pairs, numbers or
// literals), and it calls a StreamHandler per token. It keeps only the
// current token and the container stack, so memory is bounded by the
// longest string plus the nesting depth.
//
// ArrayReader sits on top: it finds one array (the top-level value, or the
// value at a key path such as {"Items"}) and materialises each element as a
// small json::Value, handing it to a callback and then dropping it. Nothing
// outside that array is built.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/json.hpp"

namespace marigold::json {

struct StreamLimits {
  int maxDepth = 64;                   // same as json::parse
  size_t maxTokenBytes = 1u << 20;     // longest string, key or number
};

// Receives tokens in document order. Each method returns false to stop.
// String views are valid only during the call.
class StreamHandler {
 public:
  virtual ~StreamHandler() = default;
  virtual bool onNull() = 0;
  virtual bool onBool(bool value) = 0;
  virtual bool onNumber(double value) = 0;
  virtual bool onString(std::string_view value) = 0;
  virtual bool onKey(std::string_view key) = 0;
  virtual bool onStartObject() = 0;
  virtual bool onEndObject() = 0;
  virtual bool onStartArray() = 0;
  virtual bool onEndArray() = 0;
};

class StreamReader {
 public:
  enum class Status { Reading, Done, Stopped, Failed };

  explicit StreamReader(StreamHandler& handler, StreamLimits limits = {});

  // Reads the next bytes. False once the input failed or the handler stopped.
  bool feed(std::string_view chunk);
  // End of input. True when exactly one complete value was read (a UTF-8
  // byte order mark and surrounding whitespace are allowed).
  bool finish();

  Status status() const { return status_; }
  // Why reading failed, with a byte offset. Never quotes the input, so it
  // cannot leak a link or a login.
  const std::string& error() const { return error_; }
  uint64_t offset() const { return offset_; }
  // Bytes reserved for the token being read (the only input-sized buffer).
  size_t tokenCapacity() const { return token_.capacity(); }
  int depth() const { return static_cast<int>(stack_.size()); }

 private:
  enum class Lex : uint8_t { None, String, Escape, Unicode, SurrogateSlash, SurrogateU, Number, Literal };
  enum class Expect : uint8_t { Value, ValueOrEnd, Key, KeyOrEnd, Colon, CommaOrEnd, End };

  bool step(char c);          // false: stop (status set)
  bool structural(char c);
  bool literalStart(char c);
  bool afterValue();
  bool endString();
  bool endNumber();
  bool codeUnit(uint32_t unit);
  bool append(char c);
  bool appendCodePoint(uint32_t cp);
  bool fail(const char* what);

  StreamHandler& handler_;
  StreamLimits limits_;
  Status status_ = Status::Reading;
  std::string error_;
  uint64_t offset_ = 0;
  std::vector<char> stack_;   // '{' or '['
  Expect expect_ = Expect::Value;
  Lex lex_ = Lex::None;
  bool stringIsKey_ = false;
  bool started_ = false;      // saw a byte other than the BOM
  uint8_t bom_ = 0;           // BOM bytes matched so far
  std::string token_;
  uint32_t unit_ = 0;         // \u digits so far
  int unitDigits_ = 0;
  uint32_t high_ = 0;         // pending high surrogate
  std::string_view literal_;
  size_t literalPos_ = 0;
};

struct ArrayLimits {
  StreamLimits stream;
  // Largest element, as estimated by ArrayReader (strings plus a fixed
  // amount per node). An Xtream channel object is well under 1 KiB.
  size_t maxElementBytes = 1u << 20;
};

class ArrayReader {
 public:
  // Return false to stop reading.
  using OnElement = std::function<bool(Value&& element)>;

  // `path` names the object keys leading to the array, outermost first;
  // empty means the document itself is the array.
  explicit ArrayReader(OnElement onElement, std::vector<std::string> path = {}, ArrayLimits limits = {});
  ~ArrayReader();
  ArrayReader(const ArrayReader&) = delete;
  ArrayReader& operator=(const ArrayReader&) = delete;

  bool feed(std::string_view chunk);
  bool finish();

  StreamReader::Status status() const;
  const std::string& error() const;
  // The array was found (a null or another type at the path is "not found").
  bool foundArray() const;
  // Elements handed to the callback.
  size_t count() const;
  // Largest element estimate and token buffer seen so far: what the reader
  // retained at most, besides its own fixed-size state.
  size_t peakElementBytes() const;
  size_t peakTokenBytes() const { return peakToken_; }

 private:
  class Builder;
  std::unique_ptr<Builder> builder_;
  StreamReader reader_;
  size_t peakToken_ = 0;
};

}  // namespace marigold::json
