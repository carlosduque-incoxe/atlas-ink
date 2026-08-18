#include "AtlasConfigStore.h"

#include <AtlasConfigValidation.h>
#include <CredentialIntegrity.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cstring>

void AtlasConfigStore::toJson(JsonDocument& doc) const {
  std::lock_guard<std::mutex> lock(configMutex);
  doc["schema"] = 1;
  doc["url"] = url.empty() ? DEFAULT_FEED_URL : url;
  doc["token_obf"] = obfuscation::obfuscateToBase64(token);
  doc["token_len"] = token.size();
  doc["token_crc32"] = credential_integrity::crc32(token);
}

bool AtlasConfigStore::fromJson(JsonVariantConst doc) {
  std::lock_guard<std::mutex> lock(configMutex);

  const char* configuredUrl = doc["url"] | DEFAULT_FEED_URL;
  if (!atlas_config_validation::isValidFeedUrl(configuredUrl)) {
    status = AtlasConfigStatus::InvalidUrl;
    url.clear();
    token.clear();
    return false;
  }

  std::string loadedToken;
  bool needsResave = false;
  const char* plaintextToken = doc["token"] | "";
  if (plaintextToken[0] != '\0') {
    const size_t plaintextLen = strlen(plaintextToken);
    if (!atlas_config_validation::isValidTokenLength(plaintextLen)) {
      status = AtlasConfigStatus::InvalidToken;
      url.clear();
      token.clear();
      return false;
    }
    loadedToken.assign(plaintextToken, plaintextLen);
    needsResave = true;
  } else {
    bool ok = false;
    bool tooLong = false;
    loadedToken = obfuscation::deobfuscateFromBase64(doc["token_obf"] | "", atlas_config_validation::MAX_TOKEN_BYTES,
                                                     &ok, &tooLong);
    if (!ok || tooLong) {
      status = AtlasConfigStatus::InvalidToken;
      url.clear();
      token.clear();
      return false;
    }
  }

  if (!atlas_config_validation::isValidBearerToken(loadedToken)) {
    status = AtlasConfigStatus::InvalidToken;
    url.clear();
    token.clear();
    return false;
  }

  const JsonVariantConst tokenLength = doc["token_len"];
  if (!plaintextToken[0]) {
    if (!tokenLength.is<size_t>() || tokenLength.as<size_t>() != loadedToken.size()) {
      status = AtlasConfigStatus::InvalidToken;
      url.clear();
      token.clear();
      return false;
    }

    const JsonVariantConst checksum = doc["token_crc32"];
    if (!checksum.is<uint32_t>() ||
        !credential_integrity::validate(loadedToken, tokenLength.as<size_t>(), checksum.as<uint32_t>())) {
      status = AtlasConfigStatus::InvalidToken;
      url.clear();
      token.clear();
      return false;
    }
  } else if (tokenLength.isNull() || doc["token_crc32"].isNull()) {
    needsResave = true;
  }

  url = configuredUrl;
  token = loadedToken;
  status = AtlasConfigStatus::Ok;

  if (needsResave) requestResave();
  return true;
}

bool AtlasConfigStore::load() {
  {
    std::lock_guard<std::mutex> lock(configMutex);
    url.clear();
    token.clear();
    status = AtlasConfigStatus::NotLoaded;
  }

  if (!Storage.exists(getFilePath())) {
    std::lock_guard<std::mutex> lock(configMutex);
    status = AtlasConfigStatus::Missing;
    return false;
  }

  if (!loadFromFile()) {
    std::lock_guard<std::mutex> lock(configMutex);
    if (status == AtlasConfigStatus::NotLoaded || status == AtlasConfigStatus::Ok) {
      status = AtlasConfigStatus::InvalidJson;
    }
    return false;
  }
  return true;
}

std::optional<AtlasConfig> AtlasConfigStore::getConfig() const {
  std::lock_guard<std::mutex> lock(configMutex);
  if (status != AtlasConfigStatus::Ok) return std::nullopt;
  return AtlasConfig{url, token};
}

AtlasConfigStatus AtlasConfigStore::getStatus() const {
  std::lock_guard<std::mutex> lock(configMutex);
  return status;
}
