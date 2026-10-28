add_executable(BootRescuePolicyTest BootRescuePolicyTest.cpp)

target_include_directories(BootRescuePolicyTest PRIVATE stubs ../../src)

target_link_libraries(BootRescuePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BootRescuePolicyTest)
