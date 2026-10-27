// App data folder (vendored Legacy dados.c picks a writable one at startup).
#pragma once

#include <optional>
#include <string>

#include "core/engine/host.hpp"

namespace marigold::platform {

class Storage final : public engine::Files {
 public:
  // `appDir` is the installed app folder (a last-resort data location).
  void init(const std::string& appDir);
  const std::string& dir() const override { return dir_; }
  std::string path(const std::string& name) const override;

  std::optional<std::string> read(const std::string& name) const override;
  // Atomic replace. `secret` files are made readable by the app user only.
  bool write(const std::string& name, const std::string& content, bool secret = false) override;
  bool remove(const std::string& name);

 private:
  std::string dir_;
};

}  // namespace marigold::platform
