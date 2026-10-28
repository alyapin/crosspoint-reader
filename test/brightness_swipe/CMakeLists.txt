add_executable(BrightnessSwipeTest BrightnessSwipeTest.cpp)

target_include_directories(BrightnessSwipeTest PRIVATE
  ../../src
  ../../freeink-sdk/libs/ui/FreeInkUI/include
)

target_link_libraries(BrightnessSwipeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BrightnessSwipeTest)
