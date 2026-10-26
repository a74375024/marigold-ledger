// Big JSON documents written and read a piece at a time (the saved channel
// list and listings run to tens of MB on the TV; built as one json::Value
// they took five to ten times their size).
//
// Writer sends compact JSON to a sink in chunks. Written with keys in sorted
// order, the bytes are the same as json::write() of the whole document.
//
// DocumentReader reads one top-level object. Its small members are built
// whole; the members named as streamed (big arrays or objects, by dotted
// path: "lists", "catalog.traits") are handed over one element or member at
// a time and never held together. Objects on the way to a streamed member
// are not built either: their other members come as fields by path
// ("catalog.entryOf").
#pragma once

#include <cstddef>
#include <functional>
#include <iosfwd>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/json.hpp"
#include "core/util/json_stream.hpp"

namespace marigold::json {

class Writer {
 public:
  // Returns false when the bytes could not be written (then writing stops).
  using Sink = std::function<bool(std::string_view)>;
  explicit Writer(Sink sink, size_t bufferBytes = 64 * 1024);

  void beginObject();
  void endObject();
  void beginArray();
  void endArray();
  // In an object, before each member's value.
  void key(std::string_view k);
  // A whole value (small: one element of a big array).
  void value(const Value& v);
  // Sends what is left; false if the sink failed at any point.
  bool finish();

 private:
  void separator();
  void flushIfFull();

  Sink sink_;
  size_t bufferBytes_;
  std::string buffer_;
  std::vector<bool> first_;  // per open container: nothing written yet
  bool afterKey_ = false;
  bool ok_ = true;
};

// Writer into a string (tests, small files).
std::string written(const std::function<void(Writer&)>& write);

class DocumentReader {
 public:
  // A member that isn't streamed, built whole.
  using OnField = std::function<bool(const std::string& key, Value&& value)>;
  // One element of a streamed member: `member` is the object key, or empty
  // for an array element.
  using OnPart = std::function<bool(const std::string& key, const std::string& member, Value&& value)>;

  DocumentReader(std::set<std::string> streamed, OnField onField, OnPart onPart, StreamLimits limits = {});
  ~DocumentReader();
  DocumentReader(const DocumentReader&) = delete;
  DocumentReader& operator=(const DocumentReader&) = delete;

  bool feed(std::string_view chunk);
  // True when one whole object was read and no callback stopped it.
  bool finish();
  const std::string& error() const;

 private:
  class Handler;
  std::unique_ptr<Handler> handler_;
  StreamReader reader_;
};

// Feeds all of `in` to `reader`, 64 KiB at a time; false when the reader
// stopped or the stream failed.
bool feedAll(std::istream& in, DocumentReader& reader);

}  // namespace marigold::json
