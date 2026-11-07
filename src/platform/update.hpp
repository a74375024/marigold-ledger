// Build 148: how this platform installs an update of the app by itself
// (core/engine/updates.hpp does the check, the download and the digest).
// Android and Fire TV: the system's installer (platform/android/update.cpp);
// the Mac: the new app swapped in when this one quits (platform/macos/
// update.mm). webOS and the Apple TV can't (update_none.cpp): asset() is
// empty there, and the app makes no check at all.
#pragma once

#include <string>

#include "core/engine/host.hpp"

namespace marigold::platform::update {

// The release file this platform installs ("marigold-android.apk"); empty:
// none.
std::string asset();
// Where its download goes, in the app's cache (its folder made); empty:
// nowhere.
std::string file();
// On a worker: installs the checked download at `path`.
engine::UpdateInstall install(const std::string& path);

}  // namespace marigold::platform::update
