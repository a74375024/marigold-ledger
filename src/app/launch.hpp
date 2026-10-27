// What every platform's entry point shares: the process set-up, the data
// folder and its log, and, once the window is open, the renderer, text,
// icons, video, the app with its first screen, the loop and the shutdown in
// the right order. webOS: main.cpp; Android: platform/android/main.cpp.
#pragma once

#include <string>

#include "app/app.hpp"

namespace marigold::app {

// First thing, before any thread starts: memory arenas, private data files,
// the C locale for numbers (JSON payloads are built with printf).
void prepareProcess();

// Opens the data folder under `appDir` and the log in it (the previous
// session's log survives one launch, for crashes).
void openStorage(platform::Storage& storage, const std::string& appDir);

// From an open window to the end of the app. `assetDir` holds fonts/ and
// icons/. Returns the process exit code.
int run(platform::Window& window, platform::Storage& storage, platform::Video& video, const std::string& assetDir,
        Build build);

}  // namespace marigold::app
