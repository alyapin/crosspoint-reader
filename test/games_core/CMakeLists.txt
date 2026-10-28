set(GAMES_DIR ${REPO_ROOT}/lib/Games)
set(GAMES_SOURCES
  ${GAMES_DIR}/GameText.cpp
  ${GAMES_DIR}/SudokuSolver.cpp
  ${GAMES_DIR}/Sudoku.cpp
  ${GAMES_DIR}/Minesweeper.cpp
  ${GAMES_DIR}/Klondike.cpp
  KlondikeSolver.cpp
)

add_executable(GamesCoreTest SudokuTest.cpp MinesweeperTest.cpp KlondikeTest.cpp ${GAMES_SOURCES})
target_include_directories(GamesCoreTest PRIVATE ${GAMES_DIR})
target_link_libraries(GamesCoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GamesCoreTest DISCOVERY_TIMEOUT 60)

# Bank generators, run by hand; the output headers name the command.
add_executable(SudokuBankGen SudokuBankGen.cpp ${GAMES_SOURCES})
target_include_directories(SudokuBankGen PRIVATE ${GAMES_DIR})
target_link_libraries(SudokuBankGen PRIVATE crosspoint_test_common)

add_executable(KlondikeDealGen KlondikeDealGen.cpp ${GAMES_SOURCES})
target_include_directories(KlondikeDealGen PRIVATE ${GAMES_DIR})
target_link_libraries(KlondikeDealGen PRIVATE crosspoint_test_common)
