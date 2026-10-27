#include "platform/icons.hpp"

#include <fstream>
#include <sstream>

#include "core/util/log.hpp"
#include "platform/gfx.hpp"
#include "platform/image.hpp"

namespace marigold::icons {

IconSet::~IconSet() {
  for (auto& [name, id] : textures_) gfx::releaseTexture(id);
}

unsigned IconSet::get(const std::string& name) {
  if (name.empty() || dir_.empty()) return 0;
  auto it = textures_.find(name);
  if (it != textures_.end()) return it->second;
  unsigned& id = textures_[name];
  std::ifstream in(dir_ + "/" + name + ".png", std::ios::binary);
  std::ostringstream ss;
  if (in) ss << in.rdbuf();
  std::string bytes = ss.str();
  auto im = bytes.empty() ? std::nullopt : image::decode(bytes, 128, 128);
  if (!im) {
    MLOG_W("icons", "no icon %s", name.c_str());
    return 0;
  }
  id = gfx::uploadTexture(*im);
  return id;
}

}  // namespace marigold::icons
