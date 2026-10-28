add_executable(BookFooterTest
  BookPageMapTest.cpp
  ReadingPaceTest.cpp
  ${REPO_ROOT}/src/activities/reader/BookPageMap.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingPace.cpp
)

target_include_directories(BookFooterTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(BookFooterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookFooterTest)
