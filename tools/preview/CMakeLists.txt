# marigold-preview: the Live Events board from a channel report, as a web
# page on a computer (tools/preview/README.md). Requests go through the
# system's curl command, so it needs nothing beyond the engine.
add_executable(marigold-preview preview.cpp)
target_include_directories(marigold-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
marigold_warnings(marigold-preview)
target_link_libraries(marigold-preview PRIVATE marigold_core)
