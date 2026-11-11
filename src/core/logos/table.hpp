// Curated channel logos from the tv-logo/tv-logos library (made for dark
// backgrounds), pinned to one commit and served by jsDelivr
// (ChannelLogoTable in tvOS). Generated from the library's file list; data,
// not logic.
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace marigold::logos {

// The library's root at the pinned commit, and the US folder under it:
// the prefix for every file below unless it says otherwise.
extern const char* const kLibraryRoot;
extern const char* const kLogoBase;
// Core network (catalog core channel name) -> file.
const std::unordered_map<std::string, std::string>& coreLogos();
// Broadcast network word (catalog::networkWord) -> light logo.
const std::unordered_map<std::string, std::string>& networkLogos();
// Station call sign (lower-case) -> files keyed by network word ("" when the
// file name has none).
const std::unordered_map<std::string, std::map<std::string, std::string>>& stationLogos();
// Every national logo in the library, by file name without "-us.png"
// ("mtv-2", "fox-weather"), for channels the tables above don't cover.
const std::vector<std::string_view>& logoSetNames();
// Free streaming channels' logos in the library's misc/vod folder (under
// kLibraryRoot), by file name without ".png" ("afv-tv-vod",
// "70s-cinema-vod-us"); files named by a bare genre ("movies-vod") left out.
const std::vector<std::string_view>& vodNames();
// National logos whose horizontal file ("syfy-hz-us.png") fills the wide
// guide cell better than the stacked one, by name as in logoSetNames().
const std::vector<std::string_view>& horizontalNames();

}  // namespace marigold::logos
