#include "core/util/json_doc.hpp"

#include <istream>
#include <memory>
#include <utility>

namespace marigold::json {

// MARK: Writer

Writer::Writer(Sink sink, size_t bufferBytes) : sink_(std::move(sink)), bufferBytes_(bufferBytes) {
  buffer_.reserve(bufferBytes_ + 1024);
}

void Writer::separator() {
  if (afterKey_) {
    afterKey_ = false;
    return;
  }
  if (first_.empty()) return;
  if (!first_.back()) buffer_ += ',';
  first_.back() = false;
}

void Writer::flushIfFull() {
  if (buffer_.size() < bufferBytes_) return;
  if (ok_) ok_ = sink_(buffer_);
  buffer_.clear();
}

void Writer::beginObject() {
  separator();
  buffer_ += '{';
  first_.push_back(true);
}

void Writer::endObject() {
  first_.pop_back();
  buffer_ += '}';
  flushIfFull();
}

void Writer::beginArray() {
  separator();
  buffer_ += '[';
  first_.push_back(true);
}

void Writer::endArray() {
  first_.pop_back();
  buffer_ += ']';
  flushIfFull();
}

void Writer::key(std::string_view k) {
  separator();
  buffer_ += '"';
  buffer_ += escape(k);
  buffer_ += "\":";
  afterKey_ = true;
}

void Writer::value(const Value& v) {
  separator();
  buffer_ += write(v);
  flushIfFull();
}

bool Writer::finish() {
  if (ok_ && !buffer_.empty()) ok_ = sink_(buffer_);
  buffer_.clear();
  return ok_;
}

std::string written(const std::function<void(Writer&)>& write) {
  std::string out;
  Writer w([&out](std::string_view chunk) {
    out.append(chunk.data(), chunk.size());
    return true;
  });
  write(w);
  w.finish();
  return out;
}

// MARK: DocumentReader

class DocumentReader::Handler final : public StreamHandler {
 public:
  Handler(std::set<std::string> streamed, OnField onField, OnPart onPart)
      : streamed_(std::move(streamed)), onField_(std::move(onField)), onPart_(std::move(onPart)) {}

  bool onNull() override { return scalar(Value()); }
  bool onBool(bool v) override { return scalar(Value(v)); }
  bool onNumber(double v) override { return scalar(Value(v)); }
  bool onString(std::string_view v) override { return scalar(Value(v)); }
  bool onKey(std::string_view k) override {
    if (!nodes_.empty()) nodes_.back().key.assign(k.data(), k.size());
    else if (!frames_.empty()) frames_.back().key.assign(k.data(), k.size());
    return true;
  }
  bool onStartObject() override { return start(true); }
  bool onStartArray() override { return start(false); }
  bool onEndObject() override { return end(); }
  bool onEndArray() override { return end(); }

  bool complete() const { return complete_; }
  const std::string& error() const { return error_; }

 private:
  // A container read without building it: an object on the way to a
  // streamed member (Pass), or a streamed member itself (Stream).
  struct Frame {
    bool stream;
    std::string path;  // "catalog", "catalog.traits"
    std::string key;   // the member being read (objects)
  };
  // A value being built.
  struct Node {
    Value value;
    std::string key;
    bool isObject;
  };

  bool fail(const char* what) {
    error_ = what;
    return false;
  }

  bool leadsToStreamed(const std::string& path) const {
    std::string prefix = path + ".";
    for (const std::string& s : streamed_)
      if (s.compare(0, prefix.size(), prefix) == 0) return true;
    return false;
  }

  std::string childPath() const {
    const Frame& f = frames_.back();
    return f.path.empty() ? f.key : f.path + "." + f.key;
  }

  // Where the value starting now goes, once built.
  void target() {
    const Frame& f = frames_.back();
    toPart_ = f.stream;
    targetPath_ = f.stream ? f.path : childPath();
    targetMember_ = f.stream ? f.key : std::string();
    frames_.back().key.clear();
  }

  bool deliver(Value&& v) {
    if (toPart_) return onPart_(targetPath_, targetMember_, std::move(v));
    return onField_(targetPath_, std::move(v));
  }

  void attach(Value&& v) {
    Node& n = nodes_.back();
    if (n.isObject) n.value.set(std::move(n.key), std::move(v));
    else n.value.push(std::move(v));
  }

  bool scalar(Value v) {
    if (frames_.empty()) return fail("JSON: the document is not an object");
    if (!nodes_.empty()) {
      attach(std::move(v));
      return true;
    }
    target();
    return deliver(std::move(v));
  }

  bool start(bool isObject) {
    if (frames_.empty()) {
      if (!isObject || started_) return fail("JSON: the document is not an object");
      started_ = true;
      frames_.push_back(Frame{false, std::string(), std::string()});
      return true;
    }
    if (nodes_.empty() && !frames_.back().stream) {
      std::string path = childPath();
      if (streamed_.count(path)) {
        frames_.back().key.clear();
        frames_.push_back(Frame{true, path, std::string()});
        return true;
      }
      if (isObject && leadsToStreamed(path)) {
        frames_.back().key.clear();
        frames_.push_back(Frame{false, path, std::string()});
        return true;
      }
    }
    if (nodes_.empty()) target();
    nodes_.push_back(Node{isObject ? Value::object() : Value::array(), std::string(), isObject});
    return true;
  }

  bool end() {
    if (!nodes_.empty()) {
      Value v = std::move(nodes_.back().value);
      nodes_.pop_back();
      if (nodes_.empty()) return deliver(std::move(v));
      attach(std::move(v));
      return true;
    }
    frames_.pop_back();
    if (frames_.empty()) complete_ = true;
    return true;
  }

  std::set<std::string> streamed_;
  OnField onField_;
  OnPart onPart_;
  std::vector<Frame> frames_;
  std::vector<Node> nodes_;
  bool toPart_ = false;
  std::string targetPath_, targetMember_;
  bool started_ = false;
  bool complete_ = false;
  std::string error_;
};

DocumentReader::DocumentReader(std::set<std::string> streamed, OnField onField, OnPart onPart,
                               StreamLimits limits)
    : handler_(std::make_unique<Handler>(std::move(streamed), std::move(onField), std::move(onPart))),
      reader_(*handler_, limits) {}

DocumentReader::~DocumentReader() = default;

bool DocumentReader::feed(std::string_view chunk) { return reader_.feed(chunk); }

bool DocumentReader::finish() { return reader_.finish() && handler_->complete(); }

const std::string& DocumentReader::error() const {
  return handler_->error().empty() ? reader_.error() : handler_->error();
}

bool feedAll(std::istream& in, DocumentReader& reader) {
  std::vector<char> buf(64 * 1024);
  while (in) {
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    std::streamsize got = in.gcount();
    if (got <= 0) break;
    if (!reader.feed(std::string_view(buf.data(), static_cast<size_t>(got)))) return false;
  }
  return !in.bad();
}

}  // namespace marigold::json
