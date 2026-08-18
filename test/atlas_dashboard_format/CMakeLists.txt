add_executable(AtlasDashboardFormatTest
  AtlasDashboardFormatTest.cpp
  ${REPO_ROOT}/lib/Atlas/AtlasDashboardFormat.cpp
)

target_include_directories(AtlasDashboardFormatTest PRIVATE
  ${REPO_ROOT}/lib/Atlas
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(AtlasDashboardFormatTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AtlasDashboardFormatTest)
