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
if(MARIGOLD_WEBOS)
  set(_tests_default OFF)
else()
  set(_tests_default ON)
endif()
option(MARIGOLD_BUILD_TESTS "Build host unit tests" ${_tests_default})
option(MARIGOLD_BUILD_APP "Build the webOS app" ${MARIGOLD_WEBOS})

include(cmake/warnings.cmake)
include(cmake/sanitizers.cmake)

# Pure logic: no platform or vendor headers. Built for the TV and for tests.
add_library(marigold_core STATIC
  src/core/input/keys.cpp
  src/core/util/log.cpp
  src/core/util/redact.cpp
)
target_include_directories(marigold_core PUBLIC src)
marigold_warnings(marigold_core)
find_package(Threads REQUIRED)
target_link_libraries(marigold_core PUBLIC Threads::Threads)

# Legacy headers that are pure C and safe to test on the host.
add_library(legacy_pure INTERFACE)
target_include_directories(legacy_pure SYSTEM INTERFACE vendor/legacy/src)

if(MARIGOLD_BUILD_APP)
  add_subdirectory(vendor/legacy)
  add_subdirectory(src)
endif()

if(MARIGOLD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
