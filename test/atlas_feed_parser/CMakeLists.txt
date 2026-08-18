add_executable(AtlasFeedJsonParserTest
  AtlasFeedJsonParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/AtlasFeedJsonParser.cpp
)

target_include_directories(AtlasFeedJsonParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(AtlasFeedJsonParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AtlasFeedJsonParserTest)
