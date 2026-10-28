add_executable(LocalSendTest
  LocalSendTest.cpp
  ${REPO_ROOT}/lib/LocalSend/LocalSendHttp.cpp
  ${REPO_ROOT}/lib/LocalSend/LocalSendSession.cpp
  ${REPO_ROOT}/lib/LocalSend/LocalSendConnection.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
)

target_include_directories(LocalSendTest PRIVATE
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/LocalSend
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
)

target_link_libraries(LocalSendTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LocalSendTest)
