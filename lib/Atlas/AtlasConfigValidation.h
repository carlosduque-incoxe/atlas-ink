#pragma once

#include <cstddef>
#include <string_view>

namespace atlas_config_validation {

static constexpr size_t MAX_URL_BYTES = 512;
static constexpr size_t MAX_TOKEN_BYTES = 512;

bool isValidFeedUrl(std::string_view url);
bool isValidTokenLength(size_t len);
bool isValidBearerToken(std::string_view token);

}  // namespace atlas_config_validation
