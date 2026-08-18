#include "AtlasConfigValidation.h"

#include <cstdint>

namespace atlas_config_validation {
namespace {

bool parsePrivateIPv4(const std::string_view host) {
  uint16_t octets[4] = {};
  size_t octet = 0;
  size_t start = 0;
  bool reachedEnd = false;
  while (octet < 4) {
    const size_t dot = host.find('.', start);
    const size_t end = dot == std::string_view::npos ? host.size() : dot;
    if (end == start || end - start > 3) return false;
    if (end - start > 1U && host[start] == '0') return false;

    uint16_t value = 0;
    for (size_t i = start; i < end; ++i) {
      const char ch = host[i];
      if (ch < '0' || ch > '9') return false;
      value = static_cast<uint16_t>(value * 10U + static_cast<uint16_t>(ch - '0'));
      if (value > 255U) return false;
    }
    octets[octet++] = value;
    if (dot == std::string_view::npos) {
      reachedEnd = true;
      break;
    }
    start = dot + 1U;
  }
  if (octet != 4 || !reachedEnd) return false;

  return octets[0] == 10U || (octets[0] == 172U && octets[1] >= 16U && octets[1] <= 31U) ||
         (octets[0] == 192U && octets[1] == 168U);
}

bool validPort(const std::string_view port) {
  if (port.empty() || port.size() > 5) return false;
  uint32_t value = 0;
  for (const char ch : port) {
    if (ch < '0' || ch > '9') return false;
    value = value * 10U + static_cast<uint32_t>(ch - '0');
  }
  return value > 0U && value <= 65535U;
}

}  // namespace

bool isValidFeedUrl(const std::string_view url) {
  if (url.empty() || url.size() > MAX_URL_BYTES || url.find('\0') != std::string_view::npos) return false;

  constexpr std::string_view HTTP = "http://";
  if (url.rfind(HTTP, 0) != 0) return false;

  for (const char ch : url) {
    if (static_cast<unsigned char>(ch) <= 0x20U || ch == '"' || ch == '<' || ch == '>' || ch == '\\') return false;
  }

  const size_t authorityStart = HTTP.size();
  const size_t authorityEnd = url.find_first_of("/?#", authorityStart);
  if (authorityEnd == std::string_view::npos) return false;
  const size_t end = authorityEnd;
  if (end == authorityStart) return false;
  const std::string_view authority = url.substr(authorityStart, end - authorityStart);
  if (authority.find('@') != std::string_view::npos) return false;

  const size_t colon = authority.rfind(':');
  const std::string_view host = colon == std::string_view::npos ? authority : authority.substr(0, colon);
  if (!parsePrivateIPv4(host)) return false;
  if (colon != std::string_view::npos && !validPort(authority.substr(colon + 1U))) return false;

  return url.substr(authorityEnd) == "/api/v2/atlas-ink/feed";
}

bool isValidTokenLength(const size_t len) { return len <= MAX_TOKEN_BYTES; }

bool isValidBearerToken(const std::string_view token) {
  if (token.empty() || !isValidTokenLength(token.size())) return false;
  for (const unsigned char ch : token) {
    if (ch <= 0x20U || ch == 0x7FU) return false;
  }
  return true;
}

}  // namespace atlas_config_validation
