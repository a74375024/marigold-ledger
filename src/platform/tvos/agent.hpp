// tvOS: the user agent the box's own media player sends
// ("AppleCoreMedia/1.0.0.22K557 (Apple TV; U; CPU OS 18_2 like Mac OS X;
// en_us)"), so requests look like any app on an Apple TV, without our name.
// The Mac sends its own media player's ("AppleCoreMedia/1.0.0.24C101
// (Macintosh; U; Intel Mac OS X 15_2; en_us)": Apple Silicon Macs say
// Intel too).
#pragma once

#import <Foundation/Foundation.h>
#include <TargetConditionals.h>
#include <sys/sysctl.h>

#include <cstdio>

#include <string>

namespace marigold::tvos {

inline const std::string& neutralUserAgent() {
  static const std::string agent = [] {
    char build[32] = {0};
    size_t size = sizeof build - 1;
    if (sysctlbyname("kern.osversion", build, &size, nullptr, 0) != 0 || !build[0]) {
      std::snprintf(build, sizeof build, "%s", "22K557");
    }
    NSOperatingSystemVersion v = NSProcessInfo.processInfo.operatingSystemVersion;
    std::string version = std::to_string(v.majorVersion) + "_" + std::to_string(v.minorVersion);
    if (v.patchVersion) version += "_" + std::to_string(v.patchVersion);
#if TARGET_OS_OSX
    return "AppleCoreMedia/1.0.0." + std::string(build) + " (Macintosh; U; Intel Mac OS X " + version + "; en_us)";
#else
    return "AppleCoreMedia/1.0.0." + std::string(build) + " (Apple TV; U; CPU OS " + version + " like Mac OS X; en_us)";
#endif
  }();
  return agent;
}

}  // namespace marigold::tvos
