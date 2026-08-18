add_executable(AtlasConfigValidationTest
  AtlasConfigValidationTest.cpp
  ${REPO_ROOT}/lib/Atlas/AtlasConfigValidation.cpp
)

target_include_directories(AtlasConfigValidationTest PRIVATE
  ${REPO_ROOT}/lib/Atlas
)

target_link_libraries(AtlasConfigValidationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AtlasConfigValidationTest)
