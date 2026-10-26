# Cross-compiles for LG webOS with the openlgtv buildroot-nc4 SDK.
#
#   cmake -B build-webos -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-webos.cmake
#
# MARIGOLD_SDK (environment or cache) points at the extracted SDK folder
# (arm-webos-linux-gnueabi_sdk-buildroot). The Docker image in tools/ sets it.
if(NOT MARIGOLD_SDK)
  if(DEFINED ENV{MARIGOLD_SDK})
    set(MARIGOLD_SDK "$ENV{MARIGOLD_SDK}")
  else()
    set(MARIGOLD_SDK "/opt/arm-webos-linux-gnueabi_sdk-buildroot")
  endif()
endif()
set(MARIGOLD_SDK "${MARIGOLD_SDK}" CACHE PATH "openlgtv buildroot-nc4 SDK folder")
# Toolchain files run again for try_compile projects; pass the path along.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MARIGOLD_SDK)

# Same flags as Legacy (tools/arm.sh): plain -O2, no large-file defines, so
# struct layouts match what the TV's libraries were built with.
set(CMAKE_C_FLAGS "" CACHE STRING "")
set(CMAKE_CXX_FLAGS "" CACHE STRING "")
include("${MARIGOLD_SDK}/share/buildroot/toolchainfile.cmake")

set(MARIGOLD_WEBOS ON CACHE BOOL "Building for webOS" FORCE)
