// Small JSON value with a parser and a writer, for settings, caches and
// small API bodies. Large arrays (Xtream channel lists, scoreboards) use a
// streaming reader instead; this one builds the whole tree.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marigold::json {

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Value>;
  using Object = std::map<std::string, Value>;  // sorted: stable output

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : type_(Type::Bool), bool_(b) {}
  Value(int v) : type_(Type::Number), num_(v) {}
  Value(int64_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
  Value(double v) : type_(Type::Number), num_(v) {}
  Value(const char* s) : type_(Type::String), str_(s ? s : "") {}
  Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
  Value(std::string_view s) : type_(Type::String), str_(s) {}
  Value(Array a) : type_(Type::Array), arr_(std::make_shared<Array>(std::move(a))) {}
  Value(Object o) : type_(Type::Object), obj_(std::make_shared<Object>(std::move(o))) {}

  static Value array() { return Value(Array{}); }
  static Value object() { return Value(Object{}); }

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  // Lenient readers: a wrong type gives the fallback. Numbers given as
  // strings ("123") are read as numbers, which Xtream panels often do.
  bool asBool(bool fallback = false) const;
  double asNumber(double fallback = 0) const;
  int64_t asInt(int64_t fallback = 0) const;
  std::string asString(std::string fallback = "") const;  // numbers become text

  const Array& items() const;   // empty unless an array
  const Object& fields() const;  // empty unless an object
  size_t size() const;

  // Object lookup; a missing key (or a non-object) gives a shared null.
  const Value& operator[](std::string_view key) const;
  // Array index; out of range gives a shared null.
  const Value& operator[](size_t i) const;
  bool has(std::string_view key) const;

  // Builders. A null becomes an object/array on first use.
  Value& set(std::string key, Value v);
  Value& push(Value v);

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  // Shared so copies are cheap; builders copy on write.
  std::shared_ptr<Array> arr_;
  std::shared_ptr<Object> obj_;
};

// Parses a whole document. Depth and size are bounded; nullopt on error.
std::optional<Value> parse(std::string_view text);
// Compact output. `pretty` indents with two spaces.
std::string write(const Value& v, bool pretty = false);
// JSON string escaping without the quotes.
std::string escape(std::string_view s);

}  // namespace marigold::json
