// tvOS: the user agent the box's own media player sends
// ("AppleCoreMedia/1.0.0.22K557 (Apple TV; U; CPU OS 18_2 like Mac OS X;
// en_us)"), so requests look like any app on an Apple TV, without our name.
#pragma once

#import <Foundation/Foundation.h>
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
    return "AppleCoreMedia/1.0.0." + std::string(build) + " (Apple TV; U; CPU OS " + version + " like Mac OS X; en_us)";
  }();
  return agent;
}

}  // namespace marigold::tvos
