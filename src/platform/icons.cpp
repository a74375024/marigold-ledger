#include "platform/icons.hpp"

#include <GLES2/gl2.h>

#include <fstream>
#include <sstream>

#include "core/util/log.hpp"
#include "platform/image.hpp"

namespace marigold::icons {

IconSet::~IconSet() {
  for (auto& [name, id] : textures_) {
    if (id) glDeleteTextures(1, &id);
  }
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
  glGenTextures(1, &id);
  glBindTexture(GL_TEXTURE_2D, id);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im->w, im->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, im->rgba.data());
  return id;
}

}  // namespace marigold::icons
