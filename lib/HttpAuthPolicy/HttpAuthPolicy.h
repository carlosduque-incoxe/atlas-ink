#pragma once

#include <string_view>

namespace http_auth_policy {

bool hasBasicAuth(std::string_view username);
bool sameOrigin(std::string_view left, std::string_view right);

}  // namespace http_auth_policy
