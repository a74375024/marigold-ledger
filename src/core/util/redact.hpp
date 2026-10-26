// Removes links and credentials from text before it is logged or shown.
//
// Stream links carry the provider login (Xtream puts user and password in the
// path, playlists put them in the query), so no URL may reach a log file, an
// export or the screen. Everything that leaves the app goes through redact().
#pragma once

#include <string>
#include <string_view>

namespace marigold {

// Every scheme://... run becomes "[link]". Bare credential parameters
// (user=, username=, pass=, password=, token=, key=, auth=) keep their name
// but lose their value: "password=***".
std::string redact(std::string_view text);

// True when redact() would change the text.
bool containsSecret(std::string_view text);

}  // namespace marigold
