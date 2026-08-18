add_executable(http_auth_policy_tests
  HttpAuthPolicyTest.cpp
  ${REPO_ROOT}/lib/HttpAuthPolicy/HttpAuthPolicy.cpp
)

target_link_libraries(http_auth_policy_tests
  PRIVATE
    crosspoint_test_common
    GTest::gtest_main
)

target_include_directories(http_auth_policy_tests PRIVATE ${REPO_ROOT}/lib/HttpAuthPolicy)

gtest_discover_tests(http_auth_policy_tests)
