add_executable(QuietWaitTest QuietWaitTest.cpp)

target_include_directories(QuietWaitTest PRIVATE ../../src)

target_link_libraries(QuietWaitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(QuietWaitTest)
