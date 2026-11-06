add_executable(EdgeBackSwipeTest EdgeBackSwipeTest.cpp)

target_include_directories(EdgeBackSwipeTest PRIVATE
  ../../src
  ../../freeink-sdk/libs/ui/FreeInkUI/include
)

target_link_libraries(EdgeBackSwipeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EdgeBackSwipeTest)
