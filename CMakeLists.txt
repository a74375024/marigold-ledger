cmake_minimum_required(VERSION 3.16)
project(marigold VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MARIGOLD_WEBOS "Building for webOS (set by the toolchain file)" OFF)
if(MARIGOLD_WEBOS OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  set(_tests_default OFF)
else()
  set(_tests_default ON)
endif()
option(MARIGOLD_BUILD_TESTS "Build host unit tests" ${_tests_default})
option(MARIGOLD_BUILD_APP "Build the webOS app" ${MARIGOLD_WEBOS})
option(MARIGOLD_BUILD_PREVIEW "Build marigold-preview (Live Events from a channel report)" ${_tests_default})

include(cmake/warnings.cmake)
include(cmake/sanitizers.cmake)

add_subdirectory(vendor/pcre2)
add_subdirectory(vendor/qrcodegen)

# Pure logic: no platform headers. Built for the TV and for tests. Sources are
# globbed so modules can be added without touching this file.
file(GLOB_RECURSE MARIGOLD_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/core/*.cpp)
add_library(marigold_core STATIC ${MARIGOLD_CORE_SOURCES})
target_include_directories(marigold_core PUBLIC src)
marigold_warnings(marigold_core)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(marigold_core PUBLIC Threads::Threads pcre2 ZLIB::ZLIB PRIVATE qrcodegen)

# Legacy headers that are pure C and safe to test on the host.
add_library(legacy_pure INTERFACE)
target_include_directories(legacy_pure SYSTEM INTERFACE vendor/legacy/src)

if(MARIGOLD_BUILD_APP)
  add_subdirectory(vendor/legacy)
  add_subdirectory(src)
endif()

# The Android app's native library (the Gradle project in android/ builds it).
if(ANDROID)
  add_subdirectory(src/platform/android)
endif()

# The Apple TV app (tools/build-tvos.sh builds it).
if(CMAKE_SYSTEM_NAME STREQUAL "tvOS")
  add_subdirectory(src/platform/tvos)
endif()

if(MARIGOLD_BUILD_PREVIEW)
  add_subdirectory(tools/preview)
endif()

if(MARIGOLD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
