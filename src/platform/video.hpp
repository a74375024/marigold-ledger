// The hardware video player (vendored Legacy video.c over LS2).
//
// One decoder, one picture: the guide preview and full screen are the same
// stream with a different plane rectangle. State is read once per frame into a
// plain snapshot so the UI never touches the LS2 thread's variables directly.
#pragma once

#include <string>

#include "platform/gfx.hpp"

namespace marigold::platform {

struct VideoState {
  bool registered = false;       // LS2 bus is up
  bool registrationDenied = false;
  bool active = false;           // a mediaId exists: the UI should cut the hole
  bool ready = false;            // loadCompleted
  // The player says when the stream's data has arrived, before any picture
  // (webOS loadCompleted, Android's buffered position). Not tvOS: VLC is
  // only "ready" with the picture.
  bool reportsLoading = false;
  bool playing = false;
  bool failed = false;
  bool ended = false;
  bool decoderSeen = false;      // videoInfo arrived
  bool audioUnsupported = false;
  unsigned bufferingMs = 0;
  double position = 0;
  double bufferEnd = 0;
  int width = 0, height = 0;
  std::string hdr;
  std::string error;             // already redacted
};

class Video {
 public:
  // Registers on the LS2 bus. Failing is not fatal: the app runs without video.
  bool start();
  // `headers`: "Name: value" lines (Referer, User-Agent, Cookie).
  // `loadMode`: 0 default-track payload, 2 lean live payload (Legacy #158).
  bool play(const std::string& url, const std::string& headers = "", int loadMode = 0);
  void stop();
  // Once per frame, on the UI thread.
  void pump();
  // Plane rectangle in 1920x1080 layout units.
  void setWindow(gfx::Rect r);
  // Applies `r` again even if it did not change.
  void reapplyWindow(gfx::Rect r);
  void shutdown();

  VideoState state() const;

 private:
  bool started_ = false;
};

}  // namespace marigold::platform
