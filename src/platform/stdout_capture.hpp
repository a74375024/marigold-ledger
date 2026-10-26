// Routes stdout and stderr into the app log.
//
// The vendored Legacy C logs with printf. webOS sends a native app's stdout to
// /dev/null, and Legacy itself redirected it to a file; here a pipe feeds every
// line to log::write, so those lines are redacted like ours before they reach
// the file.
#pragma once

namespace marigold::platform {

bool captureStdout();
// Flushes and stops the reader thread. Call before exit.
void releaseStdout();

}  // namespace marigold::platform
