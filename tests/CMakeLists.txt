file(GLOB_RECURSE MARIGOLD_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp)
add_executable(marigold_tests main.cpp ${MARIGOLD_TEST_SOURCES})
target_include_directories(marigold_tests PRIVATE third_party ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(marigold_tests PRIVATE marigold_core legacy_pure)
target_compile_definitions(marigold_tests PRIVATE
  MARIGOLD_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  MARIGOLD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
marigold_warnings(marigold_tests)
add_test(NAME marigold_tests COMMAND marigold_tests)
