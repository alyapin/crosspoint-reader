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
