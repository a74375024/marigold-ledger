// The engine's store as the webOS app sees it: the screens use its types
// (Store, GuideChannel, Channels, EventsBoard...) as their own. The store
// itself is shared code, in src/core/engine.
#pragma once

#include "core/engine/store.hpp"

namespace marigold::app {
using namespace marigold::engine;
}  // namespace marigold::app
