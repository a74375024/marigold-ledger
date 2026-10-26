#include "core/util/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace marigold::json {
namespace {

constexpr int kMaxDepth = 64;

const Value& nullValue() {
  static const Value v;
  return v;
}
const Value::Array& emptyArray() {
  static const Value::Array a;
  return a;
}
const Value::Object& emptyObject() {
  static const Value::Object o;
  return o;
}

void appendUtf8(uint32_t cp, std::string& out) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view t) : t_(t) {}

  std::optional<Value> document() {
    std::optional<Value> v = value(0);
    skip();
    if (!v || i_ != t_.size()) return std::nullopt;
    return v;
  }

 private:
  void skip() {
    while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t'))
      ++i_;
  }
  bool eat(char c) {
    skip();
    if (i_ < t_.size() && t_[i_] == c) {
      ++i_;
      return true;
    }
    return false;
  }
  bool literal(std::string_view word) {
    if (t_.substr(i_, word.size()) != word) return false;
    i_ += word.size();
    return true;
  }

  std::optional<Value> value(int depth) {
    if (depth > kMaxDepth) return std::nullopt;
    skip();
    if (i_ >= t_.size()) return std::nullopt;
    char c = t_[i_];
    if (c == '{') return object(depth);
    if (c == '[') return array(depth);
    if (c == '"') {
      std::optional<std::string> s = string();
      if (!s) return std::nullopt;
      return Value(std::move(*s));
    }
    if (literal("true")) return Value(true);
    if (literal("false")) return Value(false);
    if (literal("null")) return Value();
    return number();
  }

  std::optional<Value> number() {
    size_t start = i_;
    if (i_ < t_.size() && (t_[i_] == '-' || t_[i_] == '+')) ++i_;
    while (i_ < t_.size() && ((t_[i_] >= '0' && t_[i_] <= '9') || t_[i_] == '.' || t_[i_] == 'e' ||
                              t_[i_] == 'E' || t_[i_] == '-' || t_[i_] == '+'))
      ++i_;
    if (i_ == start) return std::nullopt;
    std::string digits(t_.substr(start, i_ - start));
    char* end = nullptr;
    double v = std::strtod(digits.c_str(), &end);
    if (!end || *end != '\0') return std::nullopt;
    return Value(v);
  }

  std::optional<std::string> string() {
    ++i_;  // opening quote
    std::string out;
    while (i_ < t_.size()) {
      char c = t_[i_++];
      if (c == '"') return out;
      if (c != '\\') {
        out += c;
        continue;
      }
      if (i_ >= t_.size()) return std::nullopt;
      char e = t_[i_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          std::optional<uint32_t> cp = hex4();
          if (!cp) return std::nullopt;
          uint32_t code = *cp;
          if (code >= 0xD800 && code <= 0xDBFF && t_.substr(i_, 2) == "\\u") {
            i_ += 2;
            std::optional<uint32_t> low = hex4();
            if (!low) return std::nullopt;
            if (*low >= 0xDC00 && *low <= 0xDFFF) code = 0x10000 + ((code - 0xD800) << 10) + (*low - 0xDC00);
            else code = 0xFFFD;
          } else if (code >= 0xD800 && code <= 0xDFFF) {
            code = 0xFFFD;
          }
          appendUtf8(code, out);
          break;
        }
        default:
          return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<uint32_t> hex4() {
    if (i_ + 4 > t_.size()) return std::nullopt;
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      char c = t_[i_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else return std::nullopt;
    }
    return v;
  }

  std::optional<Value> array(int depth) {
    ++i_;
    Value::Array items;
    if (eat(']')) return Value(std::move(items));
    while (true) {
      std::optional<Value> v = value(depth + 1);
      if (!v) return std::nullopt;
      items.push_back(std::move(*v));
      if (eat(',')) continue;
      if (eat(']')) return Value(std::move(items));
      return std::nullopt;
    }
  }

  std::optional<Value> object(int depth) {
    ++i_;
    Value::Object fields;
    if (eat('}')) return Value(std::move(fields));
    while (true) {
      skip();
      if (i_ >= t_.size() || t_[i_] != '"') return std::nullopt;
      std::optional<std::string> key = string();
      if (!key || !eat(':')) return std::nullopt;
      std::optional<Value> v = value(depth + 1);
      if (!v) return std::nullopt;
      fields[std::move(*key)] = std::move(*v);
      if (eat(',')) continue;
      if (eat('}')) return Value(std::move(fields));
      return std::nullopt;
    }
  }

  std::string_view t_;
  size_t i_ = 0;
};

void writeNumber(double v, std::string& out) {
  if (!std::isfinite(v)) {
    out += "null";
    return;
  }
  if (v == std::floor(v) && std::fabs(v) < 9007199254740992.0) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(v));
    out += buf;
    return;
  }
  char buf[40];
  std::snprintf(buf, sizeof buf, "%.17g", v);
  out += buf;
}

void writeValue(const Value& v, bool pretty, int indent, std::string& out) {
  auto newline = [&](int level) {
    if (!pretty) return;
    out += '\n';
    out.append(static_cast<size_t>(level) * 2, ' ');
  };
  switch (v.type()) {
    case Value::Type::Null: out += "null"; break;
    case Value::Type::Bool: out += v.asBool() ? "true" : "false"; break;
    case Value::Type::Number: writeNumber(v.asNumber(), out); break;
    case Value::Type::String:
      out += '"';
      out += escape(v.asString());
      out += '"';
      break;
    case Value::Type::Array: {
      out += '[';
      bool first = true;
      for (const Value& item : v.items()) {
        if (!first) out += ',';
        first = false;
        newline(indent + 1);
        writeValue(item, pretty, indent + 1, out);
      }
      if (!v.items().empty()) newline(indent);
      out += ']';
      break;
    }
    case Value::Type::Object: {
      out += '{';
      bool first = true;
      for (const auto& [key, item] : v.fields()) {
        if (!first) out += ',';
        first = false;
        newline(indent + 1);
        out += '"';
        out += escape(key);
        out += pretty ? "\": " : "\":";
        writeValue(item, pretty, indent + 1, out);
      }
      if (!v.fields().empty()) newline(indent);
      out += '}';
      break;
    }
  }
}

}  // namespace

bool Value::asBool(bool fallback) const {
  if (type_ == Type::Bool) return bool_;
  if (type_ == Type::Number) return num_ != 0;
  if (type_ == Type::String) {
    if (str_ == "true" || str_ == "1") return true;
    if (str_ == "false" || str_ == "0") return false;
  }
  return fallback;
}

double Value::asNumber(double fallback) const {
  if (type_ == Type::Number) return num_;
  if (type_ == Type::String && !str_.empty()) {
    char* end = nullptr;
    double v = std::strtod(str_.c_str(), &end);
    if (end && *end == '\0') return v;
  }
  if (type_ == Type::Bool) return bool_ ? 1 : 0;
  return fallback;
}

int64_t Value::asInt(int64_t fallback) const {
  double d = asNumber(static_cast<double>(fallback));
  if (!std::isfinite(d)) return fallback;
  return static_cast<int64_t>(d);
}

std::string Value::asString(std::string fallback) const {
  if (type_ == Type::String) return str_;
  if (type_ == Type::Number) {
    std::string out;
    writeNumber(num_, out);
    return out;
  }
  if (type_ == Type::Bool) return bool_ ? "true" : "false";
  return fallback;
}

const Value::Array& Value::items() const { return arr_ ? *arr_ : emptyArray(); }
const Value::Object& Value::fields() const { return obj_ ? *obj_ : emptyObject(); }

size_t Value::size() const {
  if (type_ == Type::Array) return items().size();
  if (type_ == Type::Object) return fields().size();
  return 0;
}

const Value& Value::operator[](std::string_view key) const {
  if (!obj_) return nullValue();
  auto it = obj_->find(std::string(key));
  return it == obj_->end() ? nullValue() : it->second;
}

const Value& Value::operator[](size_t i) const {
  if (!arr_ || i >= arr_->size()) return nullValue();
  return (*arr_)[i];
}

bool Value::has(std::string_view key) const { return obj_ && obj_->count(std::string(key)); }

Value& Value::set(std::string key, Value v) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    obj_ = std::make_shared<Object>();
  } else if (obj_.use_count() > 1) {
    obj_ = std::make_shared<Object>(*obj_);
  }
  (*obj_)[std::move(key)] = std::move(v);
  return *this;
}

Value& Value::push(Value v) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    arr_ = std::make_shared<Array>();
  } else if (arr_.use_count() > 1) {
    arr_ = std::make_shared<Array>(*arr_);
  }
  arr_->push_back(std::move(v));
  return *this;
}

std::optional<Value> parse(std::string_view text) {
  // Tolerate a UTF-8 byte order mark.
  if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
  return Parser(text).document();
}

std::string write(const Value& v, bool pretty) {
  std::string out;
  writeValue(v, pretty, 0, out);
  return out;
}

std::string escape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 2);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

}  // namespace marigold::json
