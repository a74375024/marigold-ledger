#include "core/guide/xml.hpp"

#include <algorithm>

namespace marigold::guide::xml {
namespace {

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

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

// windows-1252 0x80-0x9F (0 = unassigned, read as Latin-1).
const uint16_t kCp1252[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                              0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                              0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                              0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

std::string transcodeLatin1(std::string_view in, bool cp1252) {
  std::string out;
  out.reserve(in.size() + in.size() / 8);
  for (char ch : in) {
    unsigned char c = static_cast<unsigned char>(ch);
    if (c < 0x80) {
      out += ch;
      continue;
    }
    uint32_t cp = c;
    if (cp1252 && c >= 0x80 && c <= 0x9F && kCp1252[c - 0x80]) cp = kCp1252[c - 0x80];
    appendUtf8(cp, out);
  }
  return out;
}

std::string lowerAscii(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

std::string_view trimView(std::string_view s) {
  size_t a = 0, b = s.size();
  while (a < b && space(s[a])) ++a;
  while (b > a && space(s[b - 1])) --b;
  return s.substr(a, b - a);
}

}  // namespace

const std::string* Attributes::find(std::string_view name) const {
  for (const Attribute& a : list_) {
    if (a.name == name) return &a.value;
  }
  return nullptr;
}

void decodeEntities(std::string_view in, std::string& out) {
  size_t i = 0;
  while (i < in.size()) {
    size_t amp = in.find('&', i);
    if (amp == std::string_view::npos) {
      out.append(in.substr(i));
      return;
    }
    out.append(in.substr(i, amp - i));
    size_t semi = in.find(';', amp + 1);
    if (semi == std::string_view::npos || semi - amp > 12) {
      out += '&';
      i = amp + 1;
      continue;
    }
    std::string_view name = in.substr(amp + 1, semi - amp - 1);
    bool done = true;
    if (name == "lt")
      out += '<';
    else if (name == "gt")
      out += '>';
    else if (name == "amp")
      out += '&';
    else if (name == "quot")
      out += '"';
    else if (name == "apos")
      out += '\'';
    else if (name.size() >= 2 && name[0] == '#') {
      bool hex = name[1] == 'x' || name[1] == 'X';
      std::string_view digits = name.substr(hex ? 2 : 1);
      uint32_t cp = 0;
      bool ok = !digits.empty();
      for (char c : digits) {
        uint32_t v;
        if (c >= '0' && c <= '9')
          v = static_cast<uint32_t>(c - '0');
        else if (hex && c >= 'a' && c <= 'f')
          v = static_cast<uint32_t>(c - 'a' + 10);
        else if (hex && c >= 'A' && c <= 'F')
          v = static_cast<uint32_t>(c - 'A' + 10);
        else {
          ok = false;
          break;
        }
        cp = cp * (hex ? 16 : 10) + v;
        if (cp > 0x10FFFF) {
          ok = false;
          break;
        }
      }
      // XML Char: no NUL, no surrogates.
      if (ok && cp != 0 && !(cp >= 0xD800 && cp <= 0xDFFF))
        appendUtf8(cp, out);
      else
        done = false;
    } else {
      done = false;
    }
    if (done) {
      i = semi + 1;
    } else {
      out += '&';
      i = amp + 1;
    }
  }
}

void Tokenizer::appendInput(std::string_view chunk) {
  if (latin1_)
    buf_ += transcodeLatin1(chunk, windows1252_);
  else
    buf_.append(chunk.data(), chunk.size());
}

void Tokenizer::feed(std::string_view chunk, Handler& handler) {
  bytes_ += chunk.size();
  appendInput(chunk);
  process(handler, false);
  compact();
}

void Tokenizer::finish(Handler& handler) {
  process(handler, true);
  if (mode_ != Mode::Text || pos_ < buf_.size()) ++errors_;  // unterminated markup
  buf_.clear();
  pos_ = scan_ = 0;
  mode_ = Mode::Text;
}

void Tokenizer::compact() {
  if (pos_ == 0) return;
  if (pos_ < buf_.size() && pos_ < 64 * 1024) return;
  buf_.erase(0, pos_);
  scan_ = scan_ > pos_ ? scan_ - pos_ : 0;
  pos_ = 0;
}

void Tokenizer::emitText(size_t from, size_t to, Handler& handler) {
  if (to <= from || skipDepth_ > 0) return;
  std::string_view raw(buf_.data() + from, to - from);
  if (raw.find('&') == std::string_view::npos) {
    handler.characters(raw);
    return;
  }
  text_.clear();
  decodeEntities(raw, text_);
  handler.characters(text_);
}

void Tokenizer::process(Handler& handler, bool final) {
  if (!started_) {
    // A UTF-8 byte order mark is not text.
    if (buf_.size() - pos_ < 3 && !final) return;
    if (buf_.compare(pos_, 3, "\xEF\xBB\xBF") == 0) pos_ += 3;
    started_ = true;
  }
  while (pos_ < buf_.size()) {
    switch (mode_) {
      case Mode::Text: {
        size_t lt = buf_.find('<', pos_);
        if (lt == std::string::npos) {
          size_t end = buf_.size();
          if (!final) {
            // Keep back an entity cut by the chunk boundary ("&am").
            size_t amp = buf_.rfind('&');
            if (amp != std::string::npos && amp >= pos_ && end - amp < 12 &&
                buf_.find(';', amp) == std::string::npos) {
              end = amp;
            }
          }
          emitText(pos_, end, handler);
          pos_ = end;
          return;
        }
        emitText(pos_, lt, handler);
        pos_ = lt;
        size_t rem = buf_.size() - pos_;
        if (rem < 2) {
          if (final) pos_ = buf_.size();
          return;
        }
        char next = buf_[pos_ + 1];
        if (next == '!') {
          static const std::string_view kComment = "<!--", kCData = "<![CDATA[";
          std::string_view head(buf_.data() + pos_, std::min(rem, kCData.size()));
          auto prefixOf = [&](std::string_view word) {
            size_t k = std::min(head.size(), word.size());
            return head.substr(0, k) == word.substr(0, k);
          };
          if (head.size() >= kComment.size() && head.substr(0, kComment.size()) == kComment) {
            mode_ = Mode::Comment;
            pos_ += kComment.size();
          } else if (head == kCData) {
            mode_ = Mode::CData;
            pos_ += kCData.size();
          } else if ((head.size() < kComment.size() && prefixOf(kComment)) ||
                     (head.size() < kCData.size() && prefixOf(kCData))) {
            if (final) pos_ = buf_.size();
            return;  // maybe a comment or CDATA: need more bytes
          } else {
            mode_ = Mode::Declaration;
            scan_ = pos_ + 2;
            declDepth_ = 0;
            quote_ = 0;
          }
        } else if (next == '?') {
          mode_ = Mode::Instruction;
          scan_ = pos_ + 2;
          discard_ = false;
        } else {
          mode_ = Mode::Tag;
          scan_ = pos_ + 1;
          quote_ = 0;
        }
        break;
      }
      case Mode::Comment: {
        size_t end = buf_.find("-->", pos_);
        if (end == std::string::npos) {
          if (buf_.size() - pos_ > 2) pos_ = buf_.size() - 2;  // keep a possible "--"
          if (final) pos_ = buf_.size();
          return;
        }
        pos_ = end + 3;
        mode_ = Mode::Text;
        break;
      }
      case Mode::CData: {
        size_t end = buf_.find("]]>", pos_);
        if (end == std::string::npos) {
          size_t keep = final ? buf_.size() : (buf_.size() - pos_ > 2 ? buf_.size() - 2 : pos_);
          if (keep > pos_ && skipDepth_ == 0) {
            handler.cdata(std::string_view(buf_.data() + pos_, keep - pos_));
          }
          pos_ = keep;
          return;
        }
        if (end > pos_ && skipDepth_ == 0) {
          handler.cdata(std::string_view(buf_.data() + pos_, end - pos_));
        }
        pos_ = end + 3;
        mode_ = Mode::Text;
        break;
      }
      case Mode::Instruction: {
        size_t from = discard_ ? pos_ : std::max(pos_ + 2, scan_ > 0 ? scan_ - 1 : 0);
        size_t end = buf_.find("?>", from);
        if (end == std::string::npos) {
          if (discard_ || buf_.size() - pos_ > maxMarkup_) {
            discard_ = true;  // too long to be a declaration we need: stream past it
            pos_ = buf_.size() - 1;
          }
          scan_ = buf_.size();
          if (final) pos_ = buf_.size();
          return;
        }
        if (!discard_ && end - pos_ > maxMarkup_) discard_ = true;
        std::string body = discard_ ? std::string() : buf_.substr(pos_ + 2, end - pos_ - 2);
        pos_ = end + 2;
        mode_ = Mode::Text;
        if (!discard_) handleInstruction(body);
        discard_ = false;
        break;
      }
      case Mode::Declaration: {
        size_t i = std::max(scan_, pos_);
        bool found = false;
        for (; i < buf_.size(); ++i) {
          char c = buf_[i];
          if (quote_) {
            if (c == quote_) quote_ = 0;
          } else if (c == '"' || c == '\'') {
            quote_ = c;
          } else if (c == '[') {
            ++declDepth_;
          } else if (c == ']') {
            --declDepth_;
          } else if (c == '>' && declDepth_ <= 0) {
            found = true;
            break;
          }
        }
        if (!found) {
          // Nothing in a declaration is used: drop what was scanned.
          pos_ = scan_ = buf_.size();
          return;
        }
        pos_ = i + 1;
        mode_ = Mode::Text;
        break;
      }
      case Mode::Tag: {
        size_t i = std::max(scan_, pos_ + 1);
        bool found = false;
        for (; i < buf_.size(); ++i) {
          char c = buf_[i];
          if (quote_) {
            if (c == quote_) quote_ = 0;
          } else if (c == '"' || c == '\'') {
            quote_ = c;
          } else if (c == '>') {
            found = true;
            break;
          }
        }
        if (!found) {
          scan_ = buf_.size();
          if (buf_.size() - pos_ > maxMarkup_) {
            // Too long to hold: skip this element (and what it contains).
            skipEnd_ = buf_[pos_ + 1] == '/';
            lastSignificant_ = 0;
            for (size_t k = buf_.size(); k > pos_; --k) {
              if (!space(buf_[k - 1])) {
                lastSignificant_ = buf_[k - 1];
                break;
              }
            }
            mode_ = Mode::SkipTag;
            pos_ = scan_ = buf_.size();
          }
          if (final) pos_ = buf_.size();
          return;
        }
        std::string_view body(buf_.data() + pos_ + 1, i - pos_ - 1);
        pos_ = i + 1;
        mode_ = Mode::Text;
        if (body.size() > maxMarkup_) {
          // Same as when it arrives in pieces: the element is skipped.
          std::string_view t = trimView(body);
          if (!t.empty() && t[0] == '/') {
            if (skipDepth_ > 0) --skipDepth_;
            ++errors_;
          } else {
            ++skipped_;
            if (t.empty() || t.back() != '/') ++skipDepth_;
          }
          break;
        }
        handleTag(body, handler);
        break;
      }
      case Mode::SkipTag: {
        size_t i = pos_;
        bool found = false;
        for (; i < buf_.size(); ++i) {
          char c = buf_[i];
          if (quote_) {
            if (c == quote_) quote_ = 0;
          } else if (c == '"' || c == '\'') {
            quote_ = c;
          } else if (c == '>') {
            found = true;
            break;
          }
          if (!space(c)) lastSignificant_ = c;
        }
        if (!found) {
          pos_ = scan_ = buf_.size();
          return;
        }
        pos_ = i + 1;
        mode_ = Mode::Text;
        if (skipEnd_) {
          if (skipDepth_ > 0) --skipDepth_;
          ++errors_;
        } else {
          ++skipped_;
          if (lastSignificant_ != '/') ++skipDepth_;
        }
        break;
      }
    }
  }
}

void Tokenizer::handleInstruction(std::string_view body) {
  // <?xml version="1.0" encoding="ISO-8859-1"?>
  if (body.size() < 4 || body.substr(0, 3) != "xml" || !space(body[3])) return;
  size_t at = body.find("encoding");
  if (at == std::string_view::npos) return;
  size_t q = body.find_first_of("\"'", at);
  if (q == std::string_view::npos) return;
  size_t close = body.find(body[q], q + 1);
  if (close == std::string_view::npos) return;
  std::string encoding = lowerAscii(body.substr(q + 1, close - q - 1));
  bool cp1252 = encoding == "windows-1252" || encoding == "cp1252";
  bool latin = cp1252 || encoding == "iso-8859-1" || encoding == "iso8859-1" ||
               encoding == "latin1" || encoding == "latin-1" || encoding == "iso-8859-15";
  if (!latin || latin1_) return;
  latin1_ = true;
  windows1252_ = cp1252;
  // Bytes already buffered after the declaration were read as UTF-8.
  std::string tail = transcodeLatin1(std::string_view(buf_).substr(pos_), cp1252);
  buf_.resize(pos_);
  buf_ += tail;
}

void Tokenizer::handleTag(std::string_view body, Handler& handler) {
  if (!body.empty() && body[0] == '/') {
    endTag(trimView(body.substr(1)), handler);
  } else {
    startTag(body, handler);
  }
}

void Tokenizer::startTag(std::string_view body, Handler& handler) {
  std::string_view trimmed = trimView(body);
  bool selfClosing = !trimmed.empty() && trimmed.back() == '/';
  if (selfClosing) trimmed = trimView(trimmed.substr(0, trimmed.size() - 1));
  size_t n = 0;
  while (n < trimmed.size() && !space(trimmed[n]) && trimmed[n] != '/') ++n;
  std::string_view name = trimmed.substr(0, n);
  if (name.empty()) {
    ++errors_;
    return;
  }
  if (skipDepth_ > 0) {
    if (!selfClosing) ++skipDepth_;
    return;
  }
  attrs_.list_.clear();
  size_t i = n;
  while (i < trimmed.size()) {
    while (i < trimmed.size() && (space(trimmed[i]) || trimmed[i] == '/')) ++i;
    size_t a = i;
    while (i < trimmed.size() && !space(trimmed[i]) && trimmed[i] != '=' && trimmed[i] != '/') ++i;
    if (i == a) {
      ++i;
      continue;
    }
    Attribute attr;
    attr.name = std::string(trimmed.substr(a, i - a));
    while (i < trimmed.size() && space(trimmed[i])) ++i;
    if (i < trimmed.size() && trimmed[i] == '=') {
      ++i;
      while (i < trimmed.size() && space(trimmed[i])) ++i;
      std::string_view raw;
      if (i < trimmed.size() && (trimmed[i] == '"' || trimmed[i] == '\'')) {
        char q = trimmed[i];
        size_t close = trimmed.find(q, i + 1);
        if (close == std::string_view::npos) close = trimmed.size();
        raw = trimmed.substr(i + 1, close - i - 1);
        i = close + 1;
      } else {
        size_t v = i;
        while (i < trimmed.size() && !space(trimmed[i])) ++i;
        raw = trimmed.substr(v, i - v);
      }
      // Attribute-value normalization: literal tabs and line breaks are spaces.
      std::string normalized(raw);
      for (char& c : normalized) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
      }
      decodeEntities(normalized, attr.value);
    }
    attrs_.list_.push_back(std::move(attr));
  }
  handler.startElement(name, attrs_);
  if (selfClosing) handler.endElement(name);
}

void Tokenizer::endTag(std::string_view name, Handler& handler) {
  if (skipDepth_ > 0) {
    --skipDepth_;
    return;
  }
  if (name.empty()) {
    ++errors_;
    return;
  }
  handler.endElement(name);
}

}  // namespace marigold::guide::xml
