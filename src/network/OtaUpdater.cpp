#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
// clang-format on

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "AtlasBootHealth.h"
#include "AtlasInkReleasePublicKey.h"
#include "FirmwareFlasher.h"

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/carlosduque-incoxe/atlas-ink/releases/latest";

bool parseSemver(const char* text, int& major, int& minor, int& patch) {
  if (!text || *text == '\0') return false;
  int consumed = 0;
  if (sscanf(text, "%d.%d.%d%n", &major, &minor, &patch, &consumed) != 3) return false;
  return text[consumed] == '\0' && major >= 0 && minor >= 0 && patch >= 0;
}

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseGithubDigest(const char* digest, std::array<uint8_t, 32>& out) {
  constexpr char prefix[] = "sha256:";
  if (!digest || std::strncmp(digest, prefix, sizeof(prefix) - 1) != 0) return false;
  const char* hex = digest + sizeof(prefix) - 1;
  if (std::strlen(hex) != out.size() * 2) return false;
  for (size_t i = 0; i < out.size(); ++i) {
    const int high = hexNibble(hex[i * 2]);
    const int low = hexNibble(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    out[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool constantTimeEqual(const uint8_t* left, const uint8_t* right, size_t len) {
  uint8_t diff = 0;
  for (size_t i = 0; i < len; ++i) diff |= left[i] ^ right[i];
  return diff == 0;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  signatureVerified = false;
  latestVersion.clear();
  otaUrl.clear();
  otaDigest.clear();
  otaSignatureUrl.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;
  otaSha256.fill(0);

  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  auto releaseParser = std::unique_ptr<ReleaseJsonParser>(new (std::nothrow) ReleaseJsonParser());
  if (!releaseParser) return OOM_ERROR;
  const bool ok = HttpDownloader::fetchUrl(latestReleaseUrl, [&releaseParser](const uint8_t* data, size_t len) {
    releaseParser->feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  if (releaseParser->hasError() || !releaseParser->isComplete()) {
    LOG_ERR("OTA", "Release JSON is malformed, ambiguous, truncated, or oversized");
    return JSON_PARSE_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s signature=%s", releaseParser->foundTag() ? "yes" : "no",
          releaseParser->foundFirmware() ? "yes" : "no", releaseParser->foundSignature() ? "yes" : "no");

  if (!releaseParser->foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser->foundFirmware()) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  if (!releaseParser->foundSignature()) {
    LOG_ERR("OTA", "No firmware.bin.sig asset found");
    return SIGNATURE_ERROR;
  }

  latestVersion = releaseParser->getTagName();
  otaUrl = releaseParser->getFirmwareUrl();
  otaDigest = releaseParser->getFirmwareDigest();
  otaSignatureUrl = releaseParser->getSignatureUrl();
  otaSize = releaseParser->getFirmwareSize();
  totalSize = otaSize;

  if (otaSize == 0 || !parseGithubDigest(otaDigest.c_str(), otaSha256)) {
    LOG_ERR("OTA", "Missing or invalid GitHub SHA256 digest");
    return DIGEST_ERROR;
  }

  signatureVerified = verifyReleaseSignature();
  if (!signatureVerified) return SIGNATURE_ERROR;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::verifyReleaseSignature() {
  std::array<uint8_t, 96> signature{};
  size_t signatureSize = 0;
  const bool fetched = HttpDownloader::fetchUrl(otaSignatureUrl, [&](const uint8_t* data, size_t len) {
    if (len > signature.size() - signatureSize) return false;
    std::memcpy(signature.data() + signatureSize, data, len);
    signatureSize += len;
    return true;
  });
  if (!fetched || signatureSize == 0) {
    LOG_ERR("OTA", "Release signature fetch failed");
    return false;
  }

  char manifest[256];
  const int manifestLen =
      std::snprintf(manifest, sizeof(manifest), "ATLAS-INK-RELEASE-V1\nversion=%s\nsize=%zu\ndigest=%s\n",
                    latestVersion.c_str(), otaSize, otaDigest.c_str());
  if (manifestLen <= 0 || static_cast<size_t>(manifestLen) >= sizeof(manifest)) return false;

  uint8_t manifestSha[32];
  if (mbedtls_sha256(reinterpret_cast<const uint8_t*>(manifest), static_cast<size_t>(manifestLen), manifestSha, 0) != 0)
    return false;

  mbedtls_pk_context publicKey;
  mbedtls_pk_init(&publicKey);
  const int parseResult = mbedtls_pk_parse_public_key(
      &publicKey, reinterpret_cast<const uint8_t*>(atlas_release_key::PEM), sizeof(atlas_release_key::PEM));
  if (parseResult != 0) {
    LOG_ERR("OTA", "Pinned release public key parse failed: -0x%04X", -parseResult);
    mbedtls_pk_free(&publicKey);
    return false;
  }

  const int verifyResult = mbedtls_pk_verify(&publicKey, MBEDTLS_MD_SHA256, manifestSha, sizeof(manifestSha),
                                             signature.data(), signatureSize);
  mbedtls_pk_free(&publicKey);
  if (verifyResult != 0) {
    LOG_ERR("OTA", "Release signature rejected: -0x%04X", -verifyResult);
    return false;
  }
  LOG_INF("OTA", "Release signature verified");
  return true;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  int currentMajor = 0, currentMinor = 0, currentPatch = 0;
  int latestMajor = 0, latestMinor = 0, latestPatch = 0;

  const auto currentVersion = CROSSPOINT_VERSION;

  // Atlas Ink releases use one canonical bare MAJOR.MINOR.PATCH grammar.
  // Refuse anything else instead of guessing an ordering.
  if (!parseSemver(latestVersion.c_str(), latestMajor, latestMinor, latestPatch) ||
      !parseSemver(currentVersion, currentMajor, currentMinor, currentPatch)) {
    LOG_ERR("OTA", "Invalid semver current=%s latest=%s", currentVersion, latestVersion.c_str());
    return false;
  }

  /*
   * Compare major versions.
   * If they differ, return true if latest major version greater than current major version
   * otherwise return false.
   */
  if (latestMajor != currentMajor) return latestMajor > currentMajor;

  /*
   * Compare minor versions.
   * If they differ, return true if latest minor version greater than current minor version
   * otherwise return false.
   */
  if (latestMinor != currentMinor) return latestMinor > currentMinor;

  /*
   * Check patch versions.
   */
  if (latestPatch != currentPatch) return latestPatch > currentPatch;

  // If we reach here, it means all segments are equal.
  // One final check, if we're on an RC build (contains "-rc"), we should consider the latest version as newer even if
  // the segments are equal, since RC builds are pre-release versions.
  if (strstr(currentVersion, "-rc") != nullptr) {
    return true;
  }

  return false;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!signatureVerified || !isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }
  if (otaSize > updatePartition->size) {
    LOG_ERR("OTA", "Signed firmware is too large for OTA slot: %zu > %zu", otaSize, updatePartition->size);
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, otaSize, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  /* For better timing and connectivity, we disable power saving for WiFi */
  esp_wifi_set_ps(WIFI_PS_NONE);

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  mbedtls_sha256_context downloadSha;
  mbedtls_sha256_init(&downloadSha);
  bool hashOk = mbedtls_sha256_starts(&downloadSha, 0) == 0;
  if (!hashOk) {
    mbedtls_sha256_free(&downloadSha);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    esp_ota_abort(otaHandle);
    return DIGEST_ERROR;
  }
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    if (mbedtls_sha256_update(&downloadSha, data, len) != 0) {
      hashOk = false;
      return false;
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });

  /* Return back to default power saving for WiFi in case of failing */
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  uint8_t downloadedSha[32]{};
  if (hashOk && mbedtls_sha256_finish(&downloadSha, downloadedSha) != 0) hashOk = false;
  mbedtls_sha256_free(&downloadSha);

  if (wrongChip) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk || !hashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    if (!hashOk) return DIGEST_ERROR;
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  if (processedSize != otaSize || !constantTimeEqual(downloadedSha, otaSha256.data(), otaSha256.size())) {
    LOG_ERR("OTA", "Firmware digest mismatch (bytes=%zu expected=%zu)", processedSize, otaSize);
    esp_ota_abort(otaHandle);
    return DIGEST_ERROR;
  }
  LOG_INF("OTA", "Firmware SHA256 verified");

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  const esp_partition_t* runningPartition = esp_ota_get_running_partition();
  if (!runningPartition || !atlas_boot_health::recordPending(runningPartition->address, updatePartition->address)) {
    LOG_ERR("OTA", "Could not persist A/B rollback marker");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    atlas_boot_health::cancelPending();
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
