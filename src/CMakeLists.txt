# The webOS app. Links against the TV's own SDL2, SDL2_ttf, SDL2_image, zlib
# and GLES2, like Legacy; libcurl, luna-service2 and libwebp are opened at run
# time with dlopen and must never be linked (the SDK has libcurl.so.4, the TVs
# only .so.5).
set(MARIGOLD_BUILD_NUMBER "$ENV{MARIGOLD_BUILD_NUMBER}")
if(NOT MARIGOLD_BUILD_NUMBER)
  set(MARIGOLD_BUILD_NUMBER "dev")
endif()

file(GLOB MARIGOLD_APP_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/app/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/app/ui/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/app/store/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/app/screens/*.cpp)
add_executable(marigold
  ${MARIGOLD_APP_SOURCES}
  platform/gfx.cpp
  platform/http.cpp
  platform/image.cpp
  platform/lan_share.cpp
  platform/logo_cache.cpp
  platform/stdout_capture.cpp
  platform/storage.cpp
  platform/text.cpp
  platform/ts_proxy.cpp
  platform/video.cpp
  platform/window.cpp
)
target_include_directories(marigold PRIVATE ${CMAKE_SYSROOT}/usr/include/SDL2)
target_compile_definitions(marigold PRIVATE
  MARIGOLD_VERSION="${PROJECT_VERSION}"
  MARIGOLD_BUILD="${MARIGOLD_BUILD_NUMBER}")
marigold_warnings(marigold)
target_link_libraries(marigold PRIVATE marigold_core legacy
  SDL2 SDL2_ttf SDL2_image GLESv2 ${CMAKE_DL_LIBS})
# The TVs' libstdc++ is far older than the SDK's gcc 14: carry our own.
target_link_options(marigold PRIVATE -static-libstdc++ -static-libgcc)
