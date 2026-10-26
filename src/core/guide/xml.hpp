// A small streaming XML tokenizer for guide files (XMLTV). Fed byte chunks
// of any size, it reports elements, attributes and text to a handler. It
// handles the XML declaration (UTF-8, or ISO-8859-1 / windows-1252 when the
// declaration says so), comments, CDATA, DOCTYPE, the five standard entities
// and numeric character references. No DTDs, no namespaces (names are given
// as written, like XMLParser without namespace processing).
//
// Memory is bounded: text and CDATA are passed on as they arrive, comments
// and declarations are skipped as they stream past, and an element whose
// start tag is longer than `maxMarkup` bytes is skipped whole, with
// everything inside it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace marigold::guide::xml {

struct Attribute {
  std::string name;
  std::string value;  // entities decoded, whitespace normalized
};

class Attributes {
 public:
  // The value of `name`, or nullptr.
  const std::string* find(std::string_view name) const;
  const std::vector<Attribute>& all() const { return list_; }

  std::vector<Attribute> list_;
};

class Handler {
 public:
  virtual ~Handler() = default;
  virtual void startElement(std::string_view name, const Attributes& attributes) = 0;
  virtual void endElement(std::string_view name) = 0;
  // Character data (entities decoded). One text run may arrive in pieces.
  virtual void characters(std::string_view text) = 0;
  // CDATA content; by default the same as text.
  virtual void cdata(std::string_view text) { characters(text); }
};

class Tokenizer {
 public:
  static constexpr size_t kMaxMarkup = 64 * 1024;

  explicit Tokenizer(size_t maxMarkup = kMaxMarkup) : maxMarkup_(maxMarkup) {}

  void feed(std::string_view chunk, Handler& handler);
  // End of input: flushes pending text. Unfinished markup is dropped.
  void finish(Handler& handler);

  uint64_t bytesFed() const { return bytes_; }
  // Elements skipped because their start tag was too long.
  size_t skippedElements() const { return skipped_; }
  // Markup that could not be read (unterminated at the end, bad names).
  size_t errors() const { return errors_; }

 private:
  enum class Mode {
    Text,
    Comment,
    CData,
    Declaration,  // <!DOCTYPE ...> and other <! ... >
    Instruction,  // <? ... ?>
    Tag,          // <name ...> or </name>
    SkipTag,      // an over-long start tag, streamed past
  };

  void process(Handler& handler, bool final);
  void emitText(size_t from, size_t to, Handler& handler);
  void handleTag(std::string_view body, Handler& handler);
  void handleInstruction(std::string_view body);
  void startTag(std::string_view body, Handler& handler);
  void endTag(std::string_view name, Handler& handler);
  void appendInput(std::string_view chunk);
  void compact();

  size_t maxMarkup_;
  std::string buf_;
  size_t pos_ = 0;   // first unconsumed byte in buf_
  size_t scan_ = 0;  // Tag / SkipTag / Declaration: how far the scan has got
  Mode mode_ = Mode::Text;
  char quote_ = 0;            // open quote while scanning a tag
  int declDepth_ = 0;         // [ ] nesting inside <!DOCTYPE
  char lastSignificant_ = 0;  // SkipTag: last non-space byte, for "/>"
  bool skipEnd_ = false;      // SkipTag: the over-long tag is an end tag
  bool discard_ = false;      // Instruction: too long, streamed past
  size_t skipDepth_ = 0;      // > 0 while inside a skipped element
  bool started_ = false;      // past a possible BOM
  bool latin1_ = false;
  bool windows1252_ = false;
  uint64_t bytes_ = 0;
  size_t skipped_ = 0;
  size_t errors_ = 0;
  Attributes attrs_;  // reused
  std::string text_;  // reused decode buffer
};

// Decodes the standard entities and numeric references in `in`, appending to
// `out`. Unknown or malformed references are kept as written.
void decodeEntities(std::string_view in, std::string& out);

}  // namespace marigold::guide::xml
