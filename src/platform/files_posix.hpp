// Whole-file reads and atomic writes on a POSIX file system, for the
// platforms whose Storage keeps plain files (Android, tvOS).
#pragma once

#include <optional>
#include <string>

namespace marigold::platform::files {

std::optional<std::string> read(const std::string& path);
// A temporary file, fsync, then a rename over the old one. `secret` files
// are made readable by the app user only.
bool writeAtomic(const std::string& path, const std::string& content, bool secret);

}  // namespace marigold::platform::files
