// webOS and the Apple TV: the app can't install itself there (Developer
// Mode, a signing service), so it never looks for an update.
#include "platform/update.hpp"

namespace marigold::platform::update {

std::string asset() { return {}; }
std::string file() { return {}; }
engine::UpdateInstall install(const std::string&) { return {}; }

}  // namespace marigold::platform::update
