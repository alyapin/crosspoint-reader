add_executable(LightSleepTest LightSleepTest.cpp)

target_include_directories(LightSleepTest PRIVATE ../../src)

target_link_libraries(LightSleepTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LightSleepTest)
