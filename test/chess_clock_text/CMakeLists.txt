add_executable(ChessClockTextTest ChessClockTextTest.cpp)

target_include_directories(ChessClockTextTest PRIVATE ../../src)

target_link_libraries(ChessClockTextTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ChessClockTextTest)
