#include "platform/storage.hpp"

#include <sys/stat.h>

#include <cstdlib>

#include "dados.h"

namespace marigold::platform {

void Storage::init(const std::string& appDir) {
  dados_iniciar(appDir.c_str());
  dir_ = dados_dir();
}

std::string Storage::path(const std::string& name) const {
  return dir_.empty() ? std::string() : dir_ + "/" + name;
}

std::optional<std::string> Storage::read(const std::string& name) const {
  char* raw = dados_ler(name.c_str());
  if (!raw) return std::nullopt;
  std::string out(raw);
  std::free(raw);
  return out;
}

bool Storage::write(const std::string& name, const std::string& content, bool secret) {
  if (dir_.empty()) return false;
  if (!dados_gravar(name.c_str(), content.c_str())) return false;
  if (secret) chmod(path(name).c_str(), S_IRUSR | S_IWUSR);
  return true;
}

bool Storage::remove(const std::string& name) { return dados_apagar(name.c_str()) != 0; }

}  // namespace marigold::platform
