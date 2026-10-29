set(CHESS_SOURCES
  ${REPO_ROOT}/lib/Chess/ChessBoard.cpp
  ${REPO_ROOT}/lib/Chess/ChessClock.cpp
  ${REPO_ROOT}/lib/Chess/ChessEngine.cpp
  ${REPO_ROOT}/lib/Chess/ChessGame.cpp
  ${REPO_ROOT}/lib/Chess/ChessMatch.cpp
)

add_executable(ChessCoreTest ChessCoreTest.cpp ${CHESS_SOURCES})
target_include_directories(ChessCoreTest PRIVATE ${REPO_ROOT}/lib/Chess)
target_link_libraries(ChessCoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ChessCoreTest DISCOVERY_TIMEOUT 60)

# Strength/speed measurement, run by hand: build/test/chess_core/ChessBench
add_executable(ChessBench ChessBench.cpp ${CHESS_SOURCES})
target_include_directories(ChessBench PRIVATE ${REPO_ROOT}/lib/Chess)
target_link_libraries(ChessBench PRIVATE crosspoint_test_common)

add_executable(ChessSaveTest ChessSaveTest.cpp ${REPO_ROOT}/src/games/chess/ChessSave.cpp
               ${REPO_ROOT}/src/games/chess/ui/ChessPrefs.cpp ${CHESS_SOURCES})
target_include_directories(ChessSaveTest PRIVATE ${REPO_ROOT}/lib/Chess ${REPO_ROOT}/src ../library_builder/stubs)
target_link_libraries(ChessSaveTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ChessSaveTest DISCOVERY_TIMEOUT 60)
