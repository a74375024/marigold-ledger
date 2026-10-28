# marigold-preview: the Live Events board from a channel report, as a web
# page on a computer (tools/preview/README.md). Requests go through the
# system's curl command, so it needs nothing beyond the engine.

# The sport icons the TV draws, built into the page.
file(GLOB _preview_icons CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/app/icons/src/*.svg)
set(_icons_js "")
foreach(_svg ${_preview_icons})
  get_filename_component(_name ${_svg} NAME_WE)
  if(_name MATCHES "^(key|ui)-")
    continue()
  endif()
  file(READ ${_svg} _text)
  string(REPLACE "\\" "\\\\" _text "${_text}")
  string(REPLACE "\"" "\\\"" _text "${_text}")
  string(REPLACE "\n" " " _text "${_text}")
  string(APPEND _icons_js "\"${_name}\":\"${_text}\",")
endforeach()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/preview_icons.hpp.in
  "// Generated from app/icons/src by tools/preview/CMakeLists.txt.\n#pragma once\nnamespace preview {\nconstexpr const char* kIcons = R\"ICONS({${_icons_js}})ICONS\";\n}\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/preview_icons.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/preview_icons.hpp COPYONLY)

add_executable(marigold-preview preview.cpp)
target_include_directories(marigold-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
marigold_warnings(marigold-preview)
target_link_libraries(marigold-preview PRIVATE marigold_core)
