add_executable(KeyboardLayoutsTest KeyboardLayoutsTest.cpp ../../src/activities/util/KeyboardLayoutSet.cpp)
target_include_directories(KeyboardLayoutsTest PRIVATE stubs ../../src)
target_link_libraries(KeyboardLayoutsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KeyboardLayoutsTest)
