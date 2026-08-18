#include <gtest/gtest.h>

#include <HttpAuthPolicy.h>

TEST(HttpAuthPolicy, BasicAuthAllowsEmptyPasswordWhenUsernameExists) {
  EXPECT_TRUE(http_auth_policy::hasBasicAuth("reader"));
  EXPECT_FALSE(http_auth_policy::hasBasicAuth(""));
}

TEST(HttpAuthPolicy, SameOriginNormalizesCaseAndDefaultPorts) {
  EXPECT_TRUE(http_auth_policy::sameOrigin("HTTP://Example.COM:80/a", "http://example.com/b"));
  EXPECT_TRUE(http_auth_policy::sameOrigin("https://example.com/a", "HTTPS://EXAMPLE.COM:443/b"));
  EXPECT_TRUE(http_auth_policy::sameOrigin("http://user@example.com/a", "http://example.com/b"));
}

TEST(HttpAuthPolicy, RejectsCrossOriginRedirects) {
  EXPECT_FALSE(http_auth_policy::sameOrigin("http://10.10.1.111/a", "http://10.10.1.112/b"));
  EXPECT_FALSE(http_auth_policy::sameOrigin("http://10.10.1.111/a", "https://10.10.1.111/b"));
  EXPECT_FALSE(http_auth_policy::sameOrigin("http://10.10.1.111:3456/a", "http://10.10.1.111:3457/b"));
  EXPECT_FALSE(http_auth_policy::sameOrigin("not-a-url", "http://10.10.1.111/b"));
}
