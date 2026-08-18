#include "HttpAuthPolicy.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace http_auth_policy {
namespace {

std::string urlOrigin(const std::string_view url) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string_view::npos) return {};
  std::string scheme(url.substr(0, schemeEnd));
  std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  const size_t authorityStart = schemeEnd + 3U;
  const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  std::string authority(url.substr(authorityStart, authorityEnd - authorityStart));
  const size_t userInfo = authority.rfind('@');
  if (userInfo != std::string::npos) authority.erase(0, userInfo + 1U);
  std::transform(authority.begin(), authority.end(), authority.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (authority.empty()) return {};

  const char* defaultPort = scheme == "http" ? ":80" : (scheme == "https" ? ":443" : nullptr);
  if (defaultPort) {
    const size_t suffixLen = std::strlen(defaultPort);
    if (authority.size() >= suffixLen && authority.compare(authority.size() - suffixLen, suffixLen, defaultPort) == 0) {
      authority.resize(authority.size() - suffixLen);
    }
  }
  return scheme + "://" + authority;
}

}  // namespace

bool hasBasicAuth(const std::string_view username) { return !username.empty(); }

bool sameOrigin(const std::string_view left, const std::string_view right) {
  const std::string leftOrigin = urlOrigin(left);
  return !leftOrigin.empty() && leftOrigin == urlOrigin(right);
}

}  // namespace http_auth_policy
