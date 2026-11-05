# The Mac app (Marigold.app): the same app/ and shared platform/ sources as
# the TV apps, with the Apple TV's backends (src/platform/tvos, which build
# for the Mac too) and this folder's entry point. tools/build-mac.sh
# configures it (Makefiles, both Apple Silicon and Intel), builds it, packs
# the app with VLCKit, the fonts and icons, signs it ad hoc and makes the
# .dmg:
#
#   cmake -S . -B build-mac -DMARIGOLD_MAC_APP=ON -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
#     -DMARIGOLD_VLCKIT=<VLCKit.xcframework>/macos-arm64_x86_64/VLCKit.framework
set(MARIGOLD_BUILD_NUMBER "$ENV{MARIGOLD_BUILD_NUMBER}")
if(NOT MARIGOLD_BUILD_NUMBER)
  set(MARIGOLD_BUILD_NUMBER "dev")
endif()
set(MARIGOLD_VLCKIT "" CACHE PATH "VLCKit.framework (the xcframework's macOS slice)")
if(NOT EXISTS "${MARIGOLD_VLCKIT}/Headers/VLCKit.h")
  message(FATAL_ERROR "MARIGOLD_VLCKIT must name a VLCKit.framework (tools/build-mac.sh fetches it)")
endif()

enable_language(OBJC OBJCXX)

set(src ${CMAKE_SOURCE_DIR}/src)
set(tvos ${src}/platform/tvos)
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

set(MARIGOLD_MAC_SOURCES
  ${tvos}/gfx_metal.mm
  ${tvos}/http.mm
  ${tvos}/image.mm
  ${tvos}/storage.mm
  ${tvos}/text.mm
  ${tvos}/video.mm
  ${tvos}/window.mm
  main.mm)

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
  # Shared with Android and the Apple TV.
  ${src}/platform/diagnostics_basic.cpp
  ${src}/platform/files_posix.cpp
  ${src}/platform/http_groups.cpp
  ${src}/platform/ts_proxy_none.cpp
  # The Apple backends and the Mac's entry point.
  ${MARIGOLD_MAC_SOURCES}
)
target_include_directories(Marigold PRIVATE ${src})
target_compile_definitions(Marigold PRIVATE
  MARIGOLD_VERSION="${PROJECT_VERSION}"
  MARIGOLD_BUILD="${MARIGOLD_BUILD_NUMBER}")
marigold_warnings(Marigold)
# Objective-C++ casts its object pointers the C way.
get_filename_component(vlc_dir "${MARIGOLD_VLCKIT}" DIRECTORY)
set_source_files_properties(${MARIGOLD_MAC_SOURCES} PROPERTIES
  COMPILE_OPTIONS "-fobjc-arc;-Wno-old-style-cast;-F${vlc_dir}")

# VLCKit's own install name is @loader_path/../Frameworks/VLCKit.framework,
# where tools/build-mac.sh copies it.
target_link_options(Marigold PRIVATE "-F${vlc_dir}")
target_link_libraries(Marigold PRIVATE marigold_core
  "-framework VLCKit" "-framework AppKit" "-framework Metal" "-framework QuartzCore" "-framework CoreText"
  "-framework CoreGraphics" "-framework ImageIO" "-framework Security" "-framework AVFoundation"
  "-framework VideoToolbox" "-framework IOKit" "-framework MediaAccessibility")

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
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_BINARY_DIR}/Info.plist)
# The fonts, icons, VLCKit and licences go in when tools/build-mac.sh packs the app.
