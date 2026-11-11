file(GLOB_RECURSE MARIGOLD_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
# The two home-network servers are plain POSIX sockets: tested on the host too.
add_executable(marigold_tests main.cpp ${MARIGOLD_TEST_SOURCES}
  ${CMAKE_SOURCE_DIR}/src/platform/lan_server.cpp ${CMAKE_SOURCE_DIR}/src/platform/lan_share.cpp)
# doctest is a system header to the warnings: its own float comparisons
# (Approx) trip -Wdouble-promotion under clang.
target_include_directories(marigold_tests SYSTEM PRIVATE third_party)
target_include_directories(marigold_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(marigold_tests PRIVATE marigold_core legacy_pure)
target_compile_definitions(marigold_tests PRIVATE
  MARIGOLD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  MARIGOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
marigold_warnings(marigold_tests)
add_test(NAME marigold_tests COMMAND marigold_tests)
