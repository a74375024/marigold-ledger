// Curated channel logos from the tv-logo/tv-logos library (made for dark
// backgrounds), pinned to one commit and served by jsDelivr
// (ChannelLogoTable in tvOS). Generated from the library's file list; data,
// not logic.
#pragma once

#include <map>
#include <string>
#include <unordered_map>

namespace marigold::logos {

// Prefix for every file below.
extern const char* const kLogoBase;
// Core network (catalog core channel name) -> file.
const std::unordered_map<std::string, std::string>& coreLogos();
// Broadcast network word (catalog::networkWord) -> light logo.
const std::unordered_map<std::string, std::string>& networkLogos();
// Station call sign (lower-case) -> files keyed by network word ("" when the
// file name has none).
const std::unordered_map<std::string, std::map<std::string, std::string>>& stationLogos();

}  // namespace marigold::logos
