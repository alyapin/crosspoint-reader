add_executable(SleepImagePickTest
  SleepImagePickTest.cpp
  ${REPO_ROOT}/src/util/SleepImagePick.cpp
)
target_include_directories(SleepImagePickTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(SleepImagePickTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SleepImagePickTest)
