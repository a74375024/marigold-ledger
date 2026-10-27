# The Android app's native library (libmarigold.so), loaded by the activity.
# The same app/ and shared platform/ sources as the webOS app, with Android
# backends from this folder in place of the TV's SDL, LS2 and libcurl code.
set(MARIGOLD_BUILD_NUMBER "$ENV{MARIGOLD_BUILD_NUMBER}")
if(NOT MARIGOLD_BUILD_NUMBER)
  set(MARIGOLD_BUILD_NUMBER "dev")
endif()

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

add_library(marigold SHARED
  ${MARIGOLD_APP_SOURCES}
  # Shared platform code.
  ${src}/platform/gfx.cpp
  ${src}/platform/http.cpp
  ${src}/platform/icons.cpp
  ${src}/platform/image.cpp
  ${src}/platform/lan_share.cpp
  ${src}/platform/logo_cache.cpp
  ${src}/platform/text.cpp
  # Android backends.
  diagnostics_screen.cpp
  http.cpp
  image.cpp
  jni.cpp
  main.cpp
  storage.cpp
  text.cpp
  ts_proxy.cpp
  video.cpp
  window.cpp
)
target_include_directories(marigold PRIVATE ${src})
# gfx.cpp exports the small C drawing API declared in the Legacy shim.
target_include_directories(marigold SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor/legacy/shim)
target_compile_definitions(marigold PRIVATE
  MARIGOLD_VERSION="${PROJECT_VERSION}"
  MARIGOLD_BUILD="${MARIGOLD_BUILD_NUMBER}")
marigold_warnings(marigold)
target_link_libraries(marigold PRIVATE marigold_core android EGL GLESv2 jnigraphics log)
# Only the JNI entry points need to be visible.
target_link_options(marigold PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL)
