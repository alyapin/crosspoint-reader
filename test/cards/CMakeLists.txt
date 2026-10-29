add_executable(CardsTest
  CardDeckTest.cpp
  CardScheduleTest.cpp
  CardStoreTest.cpp
  ${REPO_ROOT}/lib/Cards/CardDeck.cpp
  ${REPO_ROOT}/lib/Cards/CardSchedule.cpp
  ${REPO_ROOT}/lib/Cards/CardLayout.cpp
  ${REPO_ROOT}/lib/Cards/CardStore.cpp
  ${REPO_ROOT}/src/stats/StatsCalendar.cpp
)
target_include_directories(CardsTest PRIVATE ../library_builder/stubs ${REPO_ROOT}/lib/Cards ${REPO_ROOT}/src/stats)
target_link_libraries(CardsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(CardsTest)
