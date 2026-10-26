// App data folder (vendored Legacy dados.c picks a writable one at startup).
#pragma once

#include <optional>
#include <string>

namespace marigold::platform {

class Storage {
 public:
  // `appDir` is the installed app folder (a last-resort data location).
  void init(const std::string& appDir);
  const std::string& dir() const { return dir_; }
  std::string path(const std::string& name) const;

  std::optional<std::string> read(const std::string& name) const;
  // Atomic replace. `secret` files are made readable by the app user only.
  bool write(const std::string& name, const std::string& content, bool secret = false);
  bool remove(const std::string& name);

 private:
  std::string dir_;
};

}  // namespace marigold::platform
