#include <gtest/gtest.h>

#include <string>

#include "Atlas/AtlasConfigValidation.h"

using atlas_config_validation::isValidBearerToken;
using atlas_config_validation::isValidFeedUrl;
using atlas_config_validation::isValidTokenLength;

TEST(AtlasConfigValidation, AcceptsTrustedPrivateLanHttp) {
  EXPECT_TRUE(isValidFeedUrl("http://10.10.1.111:3456/api/v2/atlas-ink/feed"));
  EXPECT_TRUE(isValidFeedUrl("http://172.16.0.1/api/v2/atlas-ink/feed"));
  EXPECT_TRUE(isValidFeedUrl("http://172.31.255.254:65535/api/v2/atlas-ink/feed"));
  EXPECT_TRUE(isValidFeedUrl("http://192.168.1.20/api/v2/atlas-ink/feed"));
}

TEST(AtlasConfigValidation, RejectsUntrustedOrUnverifiedOrigins) {
  EXPECT_FALSE(isValidFeedUrl("https://10.10.1.111/api/v2/atlas-ink/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://8.8.8.8/api/v2/atlas-ink/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://atlas.local/api/v2/atlas-ink/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://127.0.0.1/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://user@10.10.1.111/feed"));
}

TEST(AtlasConfigValidation, RejectsMalformedPrivateUrls) {
  EXPECT_FALSE(isValidFeedUrl("10.10.1.111/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111.2/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.999/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111:0/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111:65536/feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111:notaport/feed"));
  EXPECT_FALSE(isValidFeedUrl("http:///feed"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111/white space"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111/api/v2/tasks"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111/api/v2/atlas-ink/feed?other=1"));
  EXPECT_FALSE(isValidFeedUrl("http://10.10.1.111/api/v2/atlas-ink/feed#fragment"));
}

TEST(AtlasConfigValidation, RejectsEmbeddedNullAndOversizedUrl) {
  std::string embeddedNull("http://10.10.1.111/a\0b", 22);
  EXPECT_FALSE(isValidFeedUrl(embeddedNull));
  EXPECT_FALSE(isValidFeedUrl(std::string(atlas_config_validation::MAX_URL_BYTES + 1U, 'x')));
}

TEST(AtlasConfigValidation, EnforcesTokenLimit) {
  EXPECT_TRUE(isValidTokenLength(0));
  EXPECT_TRUE(isValidTokenLength(atlas_config_validation::MAX_TOKEN_BYTES));
  EXPECT_FALSE(isValidTokenLength(atlas_config_validation::MAX_TOKEN_BYTES + 1));
}

TEST(AtlasConfigValidation, RejectsBearerHeaderInjectionAndControls) {
  EXPECT_TRUE(isValidBearerToken("tk_Atlas-123._~+/="));
  EXPECT_FALSE(isValidBearerToken(""));
  EXPECT_FALSE(isValidBearerToken("token with space"));
  EXPECT_FALSE(isValidBearerToken("token\rInjected: yes"));
  EXPECT_FALSE(isValidBearerToken("token\nInjected: yes"));
  EXPECT_FALSE(isValidBearerToken(std::string("token\x7f", 6)));
  EXPECT_FALSE(isValidBearerToken(std::string(atlas_config_validation::MAX_TOKEN_BYTES + 1U, 'a')));
}
