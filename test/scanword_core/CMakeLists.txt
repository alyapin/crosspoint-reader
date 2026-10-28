add_executable(ScanwordCoreTest
  ScanwordCoreTest.cpp
  ${REPO_ROOT}/lib/Scanword/ScanwordPuzzle.cpp
  ${REPO_ROOT}/lib/Scanword/ScanwordGame.cpp
  ${REPO_ROOT}/lib/Scanword/ScanwordClueLayout.cpp
  ${REPO_ROOT}/lib/Scanword/ScanwordShelf.cpp
)
target_include_directories(ScanwordCoreTest PRIVATE ${REPO_ROOT}/lib/Scanword)
target_compile_definitions(ScanwordCoreTest PRIVATE SCANWORD_SAMPLES_DIR="${REPO_ROOT}/tools/scanword/samples")
target_link_libraries(ScanwordCoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ScanwordCoreTest DISCOVERY_TIMEOUT 60)
