#include "core/report/crash_note.hpp"

#include "core/util/redact.hpp"

namespace marigold::report {
namespace {

std::string_view trimLine(std::string_view s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.remove_suffix(1);
  return s;
}

bool take(std::string_view line, std::string_view key, std::string& out) {
  if (line.substr(0, key.size()) != key) return false;
  out = std::string(line.substr(key.size()));
  return true;
}

// "/a/b/marigold(+0x12)[0x34]" -> "marigold(+0x12)": the folder and the
// absolute address (the offset is what maps to the source) left out.
std::string shortFrame(std::string_view frame) {
  size_t paren = frame.find('(');
  size_t slash = frame.rfind('/', paren == std::string_view::npos ? frame.size() : paren);
  if (slash != std::string_view::npos) frame.remove_prefix(slash + 1);
  size_t close = frame.find(')');
  if (close != std::string_view::npos && frame.find('(') != std::string_view::npos) frame = frame.substr(0, close + 1);
  return std::string(frame);
}

}  // namespace

std::optional<CrashNote> parseCrashNote(std::string_view text) {
  CrashNote n;
  bool first = true, frames = false;
  while (!text.empty()) {
    size_t nl = text.find('\n');
    std::string_view line = trimLine(text.substr(0, nl));
    text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
    if (first) {
      if (line.substr(0, kCrashNoteHead.size()) != kCrashNoteHead) return std::nullopt;
      line.remove_prefix(kCrashNoteHead.size());
      size_t space = line.find(' ');
      n.signal = std::string(line.substr(0, space));
      if (space != std::string_view::npos) {
        std::string_view rest = line.substr(space + 1);
        if (rest.size() >= 2 && rest.front() == '(' && rest.back() == ')') rest = rest.substr(1, rest.size() - 2);
        n.detail = std::string(rest);
      }
      first = false;
      continue;
    }
    if (line.empty()) continue;
    if (frames) {
      n.frames.push_back(std::string(line));
      continue;
    }
    if (line == "backtrace:") frames = true;
    else if (take(line, "build: ", n.build) || take(line, "uptime: ", n.uptime) || take(line, "reason: ", n.reason) ||
             take(line, "pc: ", n.registers))
      continue;
  }
  if (first || n.signal.empty()) return std::nullopt;
  // The handler never writes a link; a reason quoting one is scrubbed again.
  n.reason = redact(n.reason);
  return n;
}

std::string crashSummary(const CrashNote& note, size_t maxBytes) {
  std::string out = note.signal;
  if (!note.uptime.empty()) out += " after " + note.uptime;
  if (!note.build.empty()) out += " (" + note.build + ")";
  if (!note.reason.empty()) out += "; " + note.reason;
  bool firstFrame = true;
  for (const std::string& f : note.frames) {
    std::string piece = (firstFrame ? "; " : " < ") + shortFrame(f);
    if (out.size() + piece.size() > maxBytes) break;
    out += piece;
    firstFrame = false;
  }
  if (out.size() > maxBytes) {
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
    out.resize(cut);
  }
  return out;
}

}  // namespace marigold::report
