// Streams through one XMLTV file, keeping only programmes for channels that
// match the user's lineup (XMLTVReader in tvOS). Outside sources match by
// call sign or name; a provider's own guide matches by its exact channel ids.
//
// Feed it the plain (inflated) XML in chunks of any size, then finish().
// Channels come first in XMLTV: their display names are kept only until the
// first programme, when the keep list is settled and the names dropped.
// Only programmes of kept channels inside the window are stored, as compact
// RawPrograms.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/guide/outside.hpp"
#include "core/guide/xml.hpp"

namespace marigold::guide {

class XmltvReader : private xml::Handler {
 public:
  // Longest text kept for a title, sub-title, description or category; the
  // rest of a longer one is dropped while it streams past.
  static constexpr size_t kMaxText = 16 * 1024;
  // Descriptions over this many characters are cut to 199 plus "…".
  static constexpr size_t kMaxDescription = 200;

  // An outside source: matches by call sign / name through `targets`.
  XmltvReader(std::shared_ptr<const Targets> targets, bool locals, Window window,
              bool stationNames = false);
  static XmltvReader forSource(const Source& source, std::shared_ptr<const Targets> targets,
                               Window window);
  // A provider's own guide: keeps programmes whose channel id is one of `ids`
  // (lower-cased guide id -> user channel ids).
  static XmltvReader exact(std::unordered_map<std::string, std::vector<std::string>> ids,
                           Window window);

  // Called every 4000 programmes, so the caller can wait while video plays.
  void setPauseHook(std::function<void()> hook) { pause_ = std::move(hook); }

  void feed(std::string_view chunk);
  void finish();

  // User channel id -> (programmes, how it matched). A user channel matching
  // several guide channels takes the one with the most listings; low-power
  // repeaters ("KGO-LD") only win when the main station isn't listed, and a
  // feed of its own beats one derived from another (West from East).
  std::map<std::string, SourceResult::Entry> result();
  // result() plus counts and the horizon median (seconds is left 0 for the
  // caller to fill in).
  SourceResult sourceResult();

  int guideChannelCount() const { return guideChannels_; }
  int programmeCount() const { return programmes_; }
  uint64_t bytesFed() const { return tokenizer_.bytesFed(); }

  // "20261021200000 +0000" -> minutes since 1970 (no date formatter).
  static std::optional<int> minutes(std::string_view raw);

 private:
  XmltvReader(std::shared_ptr<const Targets> targets, bool locals, Window window,
              std::optional<std::unordered_map<std::string, std::vector<std::string>>> exact,
              bool stationNames);

  void startElement(std::string_view name, const xml::Attributes& attributes) override;
  void endElement(std::string_view name) override;
  void characters(std::string_view text) override;
  void settleKeepList();

  std::shared_ptr<const Targets> targets_;
  bool locals_;
  Window window_;
  std::optional<std::unordered_map<std::string, std::vector<std::string>>> exact_;
  bool stationNames_;
  std::function<void()> pause_;
  xml::Tokenizer tokenizer_;
  bool finished_ = false;

  int guideChannels_ = 0;
  int programmes_ = 0;
  std::unordered_map<std::string, std::vector<std::string>> channelNames_;
  std::vector<std::string> channelOrder_;  // first-seen order of channelNames_
  std::optional<std::string> currentChannel_;
  std::optional<std::map<std::string, std::vector<Match>>> keep_;  // xmltv id -> user channels
  std::unordered_map<std::string, std::string> keepNames_;
  std::unordered_map<std::string, std::vector<RawProgram>> found_;  // xmltv id -> programmes

  std::optional<std::string> programmeChannel_;
  int programmeStart_ = 0;
  int programmeEnd_ = 0;
  std::optional<std::string> title_;
  std::optional<std::string> subtitle_;
  std::optional<std::string> desc_;
  std::vector<std::string> categories_;
  int flags_ = 0;
  std::string text_;
  bool collecting_ = false;
  int counter_ = 0;
};

}  // namespace marigold::guide
