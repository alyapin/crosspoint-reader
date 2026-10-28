add_executable(ReadingStatsTest
  StatsMathTest.cpp
  ReadingStatsStoreTest.cpp
  ${REPO_ROOT}/src/stats/StatsCalendar.cpp
  ${REPO_ROOT}/src/stats/DayWindow.cpp
  ${REPO_ROOT}/src/stats/ReadingTracker.cpp
  ${REPO_ROOT}/src/stats/ReadingStats.cpp
)

target_include_directories(ReadingStatsTest PRIVATE
  ../library_builder/stubs
  ${REPO_ROOT}/src/stats
  ${REPO_ROOT}/lib/TrustedTime
)

target_link_libraries(ReadingStatsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingStatsTest)
