#include "core/util/json_stream.hpp"

#include <cstdlib>
#include <utility>

namespace marigold::json {
namespace {

constexpr char kBom[] = "\xEF\xBB\xBF";

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isNumberChar(char c) { return isDigit(c) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E'; }

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// RFC 8259 number grammar: -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
bool validNumber(std::string_view s) {
  size_t i = 0;
  auto digits = [&]() {
    size_t start = i;
    while (i < s.size() && isDigit(s[i])) ++i;
    return i > start;
  };
  if (i < s.size() && s[i] == '-') ++i;
  if (i < s.size() && s[i] == '0') {
    ++i;
  } else if (!digits()) {
    return false;
  }
  if (i < s.size() && s[i] == '.') {
    ++i;
    if (!digits()) return false;
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
    if (!digits()) return false;
  }
  return i == s.size();
}

}  // namespace

// ---------------------------------------------------------------------------
// StreamReader

StreamReader::StreamReader(StreamHandler& handler, StreamLimits limits) : handler_(handler), limits_(limits) {}

bool StreamReader::fail(const char* what) {
  status_ = Status::Failed;
  error_ = std::string("JSON: ") + what + " at byte " + std::to_string(offset_);
  token_.clear();
  return false;
}

bool StreamReader::feed(std::string_view chunk) {
  if (status_ != Status::Reading) return false;
  size_t i = 0;
  const size_t n = chunk.size();
  while (i < n) {
    if (lex_ == Lex::String) {
      // Fast path: copy a run of plain string bytes at once.
      size_t j = i;
      while (j < n && chunk[j] != '"' && chunk[j] != '\\') ++j;
      if (j > i) {
        if (token_.size() + (j - i) > limits_.maxTokenBytes) {
          offset_ += token_.size() < limits_.maxTokenBytes ? limits_.maxTokenBytes - token_.size() : 0;
          return fail("string too long");
        }
        token_.append(chunk.data() + i, j - i);
        offset_ += j - i;
        i = j;
        continue;
      }
    }
    if (!step(chunk[i])) return false;
    ++i;
    ++offset_;
  }
  return true;
}

bool StreamReader::finish() {
  if (status_ != Status::Reading) return status_ == Status::Done;
  if (lex_ == Lex::Number && !endNumber()) return false;
  if (lex_ != Lex::None || expect_ != Expect::End) return fail("unexpected end of input");
  status_ = Status::Done;
  return true;
}

bool StreamReader::append(char c) {
  if (token_.size() >= limits_.maxTokenBytes) return fail(lex_ == Lex::Number ? "number too long" : "string too long");
  token_ += c;
  return true;
}

bool StreamReader::appendCodePoint(uint32_t cp) {
  char buf[4];
  size_t len;
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    len = 1;
  } else if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    len = 2;
  } else if (cp < 0x10000) {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    len = 3;
  } else {
    buf[0] = static_cast<char>(0xF0 | (cp >> 18));
    buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
    len = 4;
  }
  if (token_.size() + len > limits_.maxTokenBytes) return fail("string too long");
  token_.append(buf, len);
  return true;
}

// One UTF-16 code unit from a \u escape. A high surrogate waits for its low
// half; a lone half becomes U+FFFD (as json::parse does).
bool StreamReader::codeUnit(uint32_t unit) {
  if (high_ != 0) {
    uint32_t high = high_;
    high_ = 0;
    if (unit >= 0xDC00 && unit <= 0xDFFF) return appendCodePoint(0x10000 + ((high - 0xD800) << 10) + (unit - 0xDC00));
    if (!appendCodePoint(0xFFFD)) return false;
  }
  if (unit >= 0xD800 && unit <= 0xDBFF) {
    high_ = unit;
    lex_ = Lex::SurrogateSlash;
    return true;
  }
  if (unit >= 0xDC00 && unit <= 0xDFFF) return appendCodePoint(0xFFFD);
  return appendCodePoint(unit);
}

bool StreamReader::step(char c) {
  if (!started_) {
    if (bom_ < 3 && c == kBom[bom_]) {
      ++bom_;
      return true;
    }
    if (bom_ != 0 && bom_ != 3) return fail("bad byte order mark");
    started_ = true;
  }
  switch (lex_) {
    case Lex::None:
      return structural(c);
    case Lex::String:
      if (c == '"') return endString();
      if (c == '\\') {
        lex_ = Lex::Escape;
        return true;
      }
      return append(c);
    case Lex::Escape:
      lex_ = Lex::String;
      switch (c) {
        case '"': return append('"');
        case '\\': return append('\\');
        case '/': return append('/');
        case 'b': return append('\b');
        case 'f': return append('\f');
        case 'n': return append('\n');
        case 'r': return append('\r');
        case 't': return append('\t');
        case 'u':
          lex_ = Lex::Unicode;
          unit_ = 0;
          unitDigits_ = 0;
          return true;
        default:
          return fail("bad escape");
      }
    case Lex::Unicode: {
      int d = hexDigit(c);
      if (d < 0) return fail("bad \\u escape");
      unit_ = (unit_ << 4) | static_cast<uint32_t>(d);
      if (++unitDigits_ < 4) return true;
      lex_ = Lex::String;
      return codeUnit(unit_);
    }
    case Lex::SurrogateSlash:
      if (c == '\\') {
        lex_ = Lex::SurrogateU;
        return true;
      }
      // High surrogate with no escape after it.
      high_ = 0;
      lex_ = Lex::String;
      if (!appendCodePoint(0xFFFD)) return false;
      return step(c);
    case Lex::SurrogateU:
      if (c == 'u') {
        lex_ = Lex::Unicode;
        unit_ = 0;
        unitDigits_ = 0;
        return true;
      }
      // High surrogate followed by another escape ("\n", "\"").
      high_ = 0;
      lex_ = Lex::Escape;
      if (!appendCodePoint(0xFFFD)) return false;
      return step(c);
    case Lex::Number:
      if (isNumberChar(c)) return append(c);
      if (!endNumber()) return false;
      return structural(c);
    case Lex::Literal:
      if (literalPos_ >= literal_.size() || c != literal_[literalPos_]) return fail("bad literal");
      if (++literalPos_ < literal_.size()) return true;
      lex_ = Lex::None;
      {
        bool ok = literal_[0] == 'n' ? handler_.onNull() : handler_.onBool(literal_[0] == 't');
        if (!ok) {
          status_ = Status::Stopped;
          return false;
        }
      }
      return afterValue();
  }
  return fail("internal state");
}

bool StreamReader::literalStart(char c) {
  if (c == 't') literal_ = "true";
  else if (c == 'f') literal_ = "false";
  else literal_ = "null";
  literalPos_ = 1;
  lex_ = Lex::Literal;
  return true;
}

bool StreamReader::structural(char c) {
  if (isSpace(c)) return true;
  auto openContainer = [&](char kind) {
    if (stack_.size() >= static_cast<size_t>(limits_.maxDepth)) return fail("nested too deeply");
    stack_.push_back(kind);
    bool ok = kind == '{' ? handler_.onStartObject() : handler_.onStartArray();
    if (!ok) {
      status_ = Status::Stopped;
      return false;
    }
    expect_ = kind == '{' ? Expect::KeyOrEnd : Expect::ValueOrEnd;
    return true;
  };
  auto closeContainer = [&]() {
    char kind = stack_.back();
    stack_.pop_back();
    bool ok = kind == '{' ? handler_.onEndObject() : handler_.onEndArray();
    if (!ok) {
      status_ = Status::Stopped;
      return false;
    }
    return afterValue();
  };
  switch (expect_) {
    case Expect::ValueOrEnd:
      if (c == ']') return closeContainer();
      [[fallthrough]];
    case Expect::Value:
      if (c == '{' || c == '[') return openContainer(c);
      if (c == '"') {
        lex_ = Lex::String;
        stringIsKey_ = false;
        return true;
      }
      if (c == '-' || isDigit(c)) {
        lex_ = Lex::Number;
        return append(c);
      }
      if (c == 't' || c == 'f' || c == 'n') return literalStart(c);
      return fail("unexpected character");
    case Expect::KeyOrEnd:
      if (c == '}') return closeContainer();
      [[fallthrough]];
    case Expect::Key:
      if (c == '"') {
        lex_ = Lex::String;
        stringIsKey_ = true;
        return true;
      }
      return fail("expected a key");
    case Expect::Colon:
      if (c == ':') {
        expect_ = Expect::Value;
        return true;
      }
      return fail("expected ':'");
    case Expect::CommaOrEnd:
      if (c == ',') {
        expect_ = stack_.back() == '{' ? Expect::Key : Expect::Value;
        return true;
      }
      if ((c == '}' && stack_.back() == '{') || (c == ']' && stack_.back() == '[')) return closeContainer();
      return fail("expected ',' or the end of a container");
    case Expect::End:
      return fail("data after the end of the document");
  }
  return fail("internal state");
}

bool StreamReader::afterValue() {
  expect_ = stack_.empty() ? Expect::End : Expect::CommaOrEnd;
  return true;
}

bool StreamReader::endString() {
  lex_ = Lex::None;
  bool ok;
  if (stringIsKey_) {
    ok = handler_.onKey(token_);
    expect_ = Expect::Colon;
  } else {
    ok = handler_.onString(token_);
    afterValue();
  }
  token_.clear();
  if (!ok) {
    status_ = Status::Stopped;
    return false;
  }
  return true;
}

bool StreamReader::endNumber() {
  lex_ = Lex::None;
  if (!validNumber(token_)) return fail("bad number");
  double v = std::strtod(token_.c_str(), nullptr);
  token_.clear();
  if (!handler_.onNumber(v)) {
    status_ = Status::Stopped;
    return false;
  }
  return afterValue();
}

// ---------------------------------------------------------------------------
// ArrayReader

namespace {
// Rough heap cost of a node, for the element size estimate.
constexpr size_t kNodeBytes = 48;
}  // namespace

class ArrayReader::Builder final : public StreamHandler {
 public:
  Builder(OnElement onElement, std::vector<std::string> path, size_t maxElementBytes)
      : onElement_(std::move(onElement)), path_(std::move(path)), maxElementBytes_(maxElementBytes) {}

  bool onNull() override { return scalar(Value()); }
  bool onBool(bool v) override { return scalar(Value(v)); }
  bool onNumber(double v) override { return scalar(Value(v)); }
  bool onString(std::string_view v) override {
    if (mode_ == Mode::InTarget && !charge(v.size())) return false;
    return scalar(Value(v));
  }
  bool onKey(std::string_view k) override {
    if (mode_ == Mode::InTarget) {
      if (!charge(k.size())) return false;
      nodes_.back().key.assign(k.data(), k.size());
      return true;
    }
    if (mode_ == Mode::Seek && !frames_.empty() && frames_.back()) {
      size_t level = frames_.size() - 1;
      pendingOnPath_ = level < path_.size() && k == path_[level];
    } else {
      pendingOnPath_ = false;
    }
    return true;
  }
  bool onStartObject() override { return start(true); }
  bool onStartArray() override { return start(false); }
  bool onEndObject() override { return end(); }
  bool onEndArray() override { return end(); }

  bool found() const { return found_; }
  size_t count() const { return count_; }
  size_t peakElementBytes() const { return peakElement_; }
  const std::string& error() const { return error_; }

 private:
  enum class Mode { Seek, InTarget, Done };
  struct Node {
    Value value;
    std::string key;  // object: key of the child being read
    bool isObject;
  };

  // Seek: is the value starting now the one the path names?
  bool valueOnPath(size_t& matched) {
    if (frames_.empty()) {
      matched = 0;
      return true;
    }
    bool on = pendingOnPath_;
    pendingOnPath_ = false;
    matched = frames_.size();
    return on;
  }

  bool charge(size_t bytes) {
    elementBytes_ += kNodeBytes + bytes;
    if (elementBytes_ > peakElement_) peakElement_ = elementBytes_;
    if (elementBytes_ > maxElementBytes_) {
      error_ = "JSON: array element too large (element " + std::to_string(count_ + 1) + ")";
      return false;
    }
    return true;
  }

  bool scalar(Value v) {
    if (mode_ == Mode::Seek) {
      size_t matched = 0;
      if (valueOnPath(matched) && matched == path_.size()) mode_ = Mode::Done;  // not an array
      return true;
    }
    if (mode_ == Mode::Done) return true;
    if (nodes_.empty()) {
      if (!charge(0)) return false;
      return emit(std::move(v));
    }
    if (!charge(0)) return false;
    attach(std::move(v));
    return true;
  }

  bool start(bool isObject) {
    if (mode_ == Mode::Seek) {
      size_t matched = 0;
      bool on = valueOnPath(matched);
      if (on && matched == path_.size()) {
        if (isObject) {
          mode_ = Mode::Done;  // the path names an object, not an array
        } else {
          mode_ = Mode::InTarget;
          found_ = true;
        }
        return true;
      }
      frames_.push_back(on && isObject);
      return true;
    }
    if (mode_ == Mode::Done) return true;
    if (!charge(0)) return false;
    nodes_.push_back(Node{isObject ? Value::object() : Value::array(), std::string(), isObject});
    return true;
  }

  bool end() {
    if (mode_ == Mode::Seek) {
      if (!frames_.empty()) frames_.pop_back();
      pendingOnPath_ = false;
      return true;
    }
    if (mode_ == Mode::Done) return true;
    if (nodes_.empty()) {  // the target array closed
      mode_ = Mode::Done;
      return true;
    }
    Value v = std::move(nodes_.back().value);
    nodes_.pop_back();
    if (nodes_.empty()) return emit(std::move(v));
    attach(std::move(v));
    return true;
  }

  void attach(Value v) {
    Node& parent = nodes_.back();
    if (parent.isObject) {
      parent.value.set(std::move(parent.key), std::move(v));
      parent.key.clear();
    } else {
      parent.value.push(std::move(v));
    }
  }

  bool emit(Value v) {
    ++count_;
    elementBytes_ = 0;
    return onElement_(std::move(v));
  }

  OnElement onElement_;
  std::vector<std::string> path_;
  size_t maxElementBytes_;
  Mode mode_ = Mode::Seek;
  std::vector<bool> frames_;  // Seek: containers above, true when on the path
  bool pendingOnPath_ = false;
  bool found_ = false;
  std::vector<Node> nodes_;   // InTarget: the element being built
  size_t elementBytes_ = 0;
  size_t peakElement_ = 0;
  size_t count_ = 0;
  std::string error_;
};

ArrayReader::ArrayReader(OnElement onElement, std::vector<std::string> path, ArrayLimits limits)
    : builder_(std::make_unique<Builder>(std::move(onElement), std::move(path), limits.maxElementBytes)),
      reader_(*builder_, limits.stream) {}

ArrayReader::~ArrayReader() = default;

bool ArrayReader::feed(std::string_view chunk) {
  bool ok = reader_.feed(chunk);
  if (reader_.tokenCapacity() > peakToken_) peakToken_ = reader_.tokenCapacity();
  return ok;
}

bool ArrayReader::finish() { return reader_.finish() && builder_->error().empty(); }

StreamReader::Status ArrayReader::status() const {
  return builder_->error().empty() ? reader_.status() : StreamReader::Status::Failed;
}

const std::string& ArrayReader::error() const {
  return builder_->error().empty() ? reader_.error() : builder_->error();
}

bool ArrayReader::foundArray() const { return builder_->found(); }
size_t ArrayReader::count() const { return builder_->count(); }
size_t ArrayReader::peakElementBytes() const { return builder_->peakElementBytes(); }

}  // namespace marigold::json
