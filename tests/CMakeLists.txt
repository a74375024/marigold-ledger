add_executable(marigold_tests
  main.cpp
  test_keys.cpp
  test_livestall.cpp
  test_log.cpp
  test_redact.cpp
)
target_include_directories(marigold_tests PRIVATE third_party)
target_link_libraries(marigold_tests PRIVATE marigold_core legacy_pure)
marigold_warnings(marigold_tests)
add_test(NAME marigold_tests COMMAND marigold_tests)
