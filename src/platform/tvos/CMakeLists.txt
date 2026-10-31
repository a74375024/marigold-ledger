# The Apple TV app (Marigold.app): the same app/ and shared platform/ sources
# as webOS and Android, with tvOS backends from this folder. Configure with
# the Xcode generator, once for the device and once for the Simulator:
#
#   cmake -S . -B build-tvos -G Xcode -DCMAKE_SYSTEM_NAME=tvOS \
#     -DCMAKE_OSX_SYSROOT=appletvos -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
#     -DMARIGOLD_VLCKIT=<TVVLCKit.xcframework>/tvos-arm64/TVVLCKit.framework
#   cmake --build build-tvos --config Release -- CODE_SIGNING_ALLOWED=NO
#
# (appletvsimulator and the tvos-arm64_x86_64-simulator slice for the
# Simulator.) tools/build-tvos.sh does this and packs the .ipa.
set(MARIGOLD_BUILD_NUMBER "$ENV{MARIGOLD_BUILD_NUMBER}")
if(NOT MARIGOLD_BUILD_NUMBER)
  set(MARIGOLD_BUILD_NUMBER "dev")
endif()
set(MARIGOLD_VLCKIT "" CACHE PATH "TVVLCKit.framework (the slice for the SDK being built)")
if(NOT EXISTS "${MARIGOLD_VLCKIT}/Headers/TVVLCKit.h")
  message(FATAL_ERROR "MARIGOLD_VLCKIT must name a TVVLCKit.framework (tools/build-tvos.sh fetches it)")
endif()

enable_language(OBJC OBJCXX)

set(src ${CMAKE_SOURCE_DIR}/src)
file(GLOB MARIGOLD_APP_SOURCES CONFIGURE_DEPENDS
  ${src}/app/*.cpp
  ${src}/app/ui/*.cpp
  ${src}/app/store/*.cpp
  ${src}/app/screens/*.cpp)
# webOS only: the TV's entry point and its SDL key/video test screen.
list(REMOVE_ITEM MARIGOLD_APP_SOURCES
  ${src}/app/main.cpp
  ${src}/app/test_screen.cpp
  ${src}/app/screens/diagnostics_screen.cpp)

set(MARIGOLD_TVOS_SOURCES
  gfx_metal.mm
  http.mm
  image.mm
  main.mm
  storage.mm
  text.mm
  video.mm
  window.mm)

add_executable(Marigold MACOSX_BUNDLE
  ${MARIGOLD_APP_SOURCES}
  # Shared platform code.
  ${src}/platform/http.cpp
  ${src}/platform/icons.cpp
  ${src}/platform/image.cpp
  ${src}/platform/lan_server.cpp
  ${src}/platform/lan_share.cpp
  ${src}/platform/logo_cache.cpp
  ${src}/platform/text.cpp
  # Shared with Android.
  ${src}/platform/diagnostics_basic.cpp
  ${src}/platform/files_posix.cpp
  ${src}/platform/http_groups.cpp
  ${src}/platform/ts_proxy_none.cpp
  # tvOS backends.
  ${MARIGOLD_TVOS_SOURCES}
  Assets.xcassets
)
target_include_directories(Marigold PRIVATE ${src})
target_compile_definitions(Marigold PRIVATE
  MARIGOLD_VERSION="${PROJECT_VERSION}"
  MARIGOLD_BUILD="${MARIGOLD_BUILD_NUMBER}")
marigold_warnings(Marigold)
# Objective-C++ casts its object pointers the C way.
set_source_files_properties(${MARIGOLD_TVOS_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-fobjc-arc;-Wno-old-style-cast")

get_filename_component(vlc_dir "${MARIGOLD_VLCKIT}" DIRECTORY)
target_link_libraries(Marigold PRIVATE marigold_core "${MARIGOLD_VLCKIT}"
  "-framework UIKit" "-framework Metal" "-framework QuartzCore" "-framework CoreText"
  "-framework CoreGraphics" "-framework ImageIO" "-framework Security" "-framework AVFoundation"
  "-framework VideoToolbox")

# The version shown in Settings is the release; the build is "Build NN".
if(MARIGOLD_BUILD_NUMBER STREQUAL "dev")
  set(bundle_version 1)
else()
  set(bundle_version ${MARIGOLD_BUILD_NUMBER})
endif()
set(MARIGOLD_BUNDLE_VERSION ${bundle_version})
configure_file(Info.plist.in ${CMAKE_CURRENT_BINARY_DIR}/Info.plist @ONLY)

set_target_properties(Marigold PROPERTIES
  OUTPUT_NAME Marigold
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
  RESOURCE Assets.xcassets
  XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.marigold.app
  XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY 3
  XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET}
  XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "App Icon & Top Shelf Image"
  XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
  XCODE_ATTRIBUTE_FRAMEWORK_SEARCH_PATHS "${vlc_dir}"
  XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
  XCODE_ATTRIBUTE_ENABLE_BITCODE NO
  XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT dwarf
  # Signed by whoever installs it (an unsigned .ipa).
  XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
  XCODE_EMBED_FRAMEWORKS "${MARIGOLD_VLCKIT}"
  XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY OFF
  XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON)
# The fonts, icons and licences go in when tools/build-tvos.sh packs the app.
