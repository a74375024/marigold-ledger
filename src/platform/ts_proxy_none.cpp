// Android and tvOS: no TS proxy. ExoPlayer and VLC follow HLS playlists
// themselves, so feeds play directly (TsProxy::wanted() is false and the rest
// does nothing).
#include "platform/ts_proxy.hpp"

#include "core/util/strings.hpp"

namespace marigold::platform {

bool TsProxy::wanted() { return false; }
bool TsProxy::start() { return false; }
void TsProxy::stop() {}
std::string TsProxy::localUrl(const std::string&, const std::string&, bool) { return {}; }
unsigned TsProxy::session() const { return 0; }
void TsProxy::setTap(Tap*) {}
bool TsProxy::redirected() const { return false; }
TsProxy::Source TsProxy::source() const { return {}; }
int TsProxy::connections(int from, std::vector<Connection>& out) const {
  out.clear();
  return from;
}
void TsProxy::setPreferredHeight(int) {}

bool TsProxy::candidate(const std::string& source) {
  return str::startsWith(source, "http://") || str::startsWith(source, "https://");
}

bool TsProxy::isLocalUrl(const std::string&) { return false; }

}  // namespace marigold::platform
