add_executable(Fb2ToEpubTest
  Fb2ToEpubTest.cpp
  ${REPO_ROOT}/lib/Fb2/Fb2ToEpub.cpp
  ${REPO_ROOT}/lib/Fb2/StoredZipWriter.cpp
  ${REPO_ROOT}/lib/miniz/src/miniz_impl.c
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
)

target_include_directories(Fb2ToEpubTest PRIVATE
  ${REPO_ROOT}/lib/Fb2
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/miniz/src
  ${REPO_ROOT}/lib/expat
)

# Match the firmware's Expat configuration (platformio.ini).
target_compile_definitions(Fb2ToEpubTest PRIVATE
  XML_CONTEXT_BYTES=0
  XML_GE=0
)

target_link_libraries(Fb2ToEpubTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(Fb2ToEpubTest)
