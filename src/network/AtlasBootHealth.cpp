#include "AtlasBootHealth.h"

#include <Arduino.h>
#include <Logging.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace atlas_boot_health {
namespace {

constexpr char NAMESPACE[] = "atlas-boot";
constexpr char RECORD_KEY[] = "record";
constexpr uint32_t RECORD_MAGIC = 0x41544C53UL;
constexpr unsigned long HEALTH_WINDOW_MS = 60000UL;
constexpr uint32_t MIN_HEALTHY_LOOPS = 100;

struct BootRecord {
  uint32_t magic;
  uint32_t previousAddress;
  uint32_t targetAddress;
  uint8_t attempts;
  uint8_t reserved[3];
  uint32_t checksum;
};

static_assert(sizeof(BootRecord) == 20, "BootRecord layout changed");

bool pendingBoot = false;
bool runtimeReady = false;
unsigned long runtimeReadyAt = 0;
uint32_t healthyLoopCount = 0;

uint32_t checksumRecord(const BootRecord& record) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(BootRecord, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

const esp_partition_t* findOtaPartition(uint32_t address) {
  esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  const esp_partition_t* match = nullptr;
  while (iterator) {
    const esp_partition_t* candidate = esp_partition_get(iterator);
    const bool isOta = candidate && candidate->subtype >= ESP_PARTITION_SUBTYPE_APP_OTA_0 &&
                       candidate->subtype <= ESP_PARTITION_SUBTYPE_APP_OTA_15;
    if (isOta && candidate->address == address) {
      match = candidate;
      break;
    }
    iterator = esp_partition_next(iterator);
  }
  if (iterator) esp_partition_iterator_release(iterator);
  return match;
}

bool loadRecord(Preferences& prefs, BootRecord& record) {
  if (prefs.getBytesLength(RECORD_KEY) != sizeof(record)) return false;
  if (prefs.getBytes(RECORD_KEY, &record, sizeof(record)) != sizeof(record)) return false;
  return record.magic == RECORD_MAGIC && record.checksum == checksumRecord(record);
}

bool saveRecord(Preferences& prefs, BootRecord& record) {
  record.checksum = checksumRecord(record);
  return prefs.putBytes(RECORD_KEY, &record, sizeof(record)) == sizeof(record);
}

void clearRecord(Preferences& prefs) { prefs.clear(); }

}  // namespace

bool recordPending(uint32_t previousAddress, uint32_t targetAddress) {
  if (previousAddress == targetAddress || !findOtaPartition(previousAddress) || !findOtaPartition(targetAddress)) {
    LOG_ERR("ATLAS_BOOT", "Invalid A/B rollback partition addresses");
    return false;
  }

  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  clearRecord(prefs);

  BootRecord record{};
  record.magic = RECORD_MAGIC;
  record.previousAddress = previousAddress;
  record.targetAddress = targetAddress;
  record.attempts = 0;
  const bool ok = saveRecord(prefs, record);
  if (!ok) clearRecord(prefs);
  prefs.end();
  return ok;
}

void cancelPending() {
  Preferences prefs;
  if (prefs.begin(NAMESPACE, false)) {
    clearRecord(prefs);
    prefs.end();
  }
  pendingBoot = false;
  runtimeReady = false;
  healthyLoopCount = 0;
}

void begin() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return;

  BootRecord record{};
  const size_t storedLength = prefs.getBytesLength(RECORD_KEY);
  if (storedLength == 0) {
    prefs.end();
    return;
  }
  if (!loadRecord(prefs, record)) {
    LOG_ERR("ATLAS_BOOT", "Invalid OTA health record");
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t stateResult = running ? esp_ota_get_state_partition(running, &state) : ESP_ERR_NOT_FOUND;
    if (stateResult == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
      const esp_err_t rollbackResult = esp_ota_mark_app_invalid_rollback_and_reboot();
      LOG_ERR("ATLAS_BOOT", "Native rollback of invalid record failed: %s", esp_err_to_name(rollbackResult));
      prefs.end();
      return;
    }
    if (stateResult == ESP_OK && (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED)) {
      clearRecord(prefs);
    }
    prefs.end();
    return;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running || running->address != record.targetAddress) {
    LOG_INF("ATLAS_BOOT", "Clearing stale OTA health record");
    clearRecord(prefs);
    prefs.end();
    return;
  }

  if (record.attempts >= 1) {
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
      LOG_ERR("ATLAS_BOOT", "Native OTA health check failed; requesting rollback");
      const esp_err_t nativeResult = esp_ota_mark_app_invalid_rollback_and_reboot();
      LOG_ERR("ATLAS_BOOT", "Native rollback failed: %s", esp_err_to_name(nativeResult));
    }

    const esp_partition_t* previous = findOtaPartition(record.previousAddress);
    if (previous && esp_ota_set_boot_partition(previous) == ESP_OK) {
      LOG_ERR("ATLAS_BOOT", "Health check failed; rolling back to %s", previous->label);
      clearRecord(prefs);
      prefs.end();
      delay(100);
      ESP.restart();
      return;
    }

    // Keep the rollback intent. This slot must never become permanently
    // accepted merely because selecting the fallback failed once.
    LOG_ERR("ATLAS_BOOT", "Rollback unavailable; marker retained for next reset");
    prefs.end();
    return;
  }

  record.attempts = 1;
  if (!saveRecord(prefs, record)) {
    LOG_ERR("ATLAS_BOOT", "Could not persist boot attempt; trying immediate rollback");
    const esp_partition_t* previous = findOtaPartition(record.previousAddress);
    if (previous && esp_ota_set_boot_partition(previous) == ESP_OK) {
      clearRecord(prefs);
      prefs.end();
      delay(100);
      ESP.restart();
      return;
    }
    // Preserve the original attempts=0 blob if NVS did not accept the update,
    // and do not arm confirmation for this boot.
    prefs.end();
    return;
  }

  prefs.end();
  pendingBoot = true;
  LOG_INF("ATLAS_BOOT", "New OTA slot is pending runtime readiness");
}

void markRuntimeReady() {
  if (!pendingBoot) return;
  runtimeReady = true;
  runtimeReadyAt = millis();
  healthyLoopCount = 0;
  LOG_INF("ATLAS_BOOT", "Runtime initialization complete; health window started");
}

void loop() {
  if (!pendingBoot || !runtimeReady) return;
  if (healthyLoopCount < UINT32_MAX) ++healthyLoopCount;
  if (healthyLoopCount < MIN_HEALTHY_LOOPS || millis() - runtimeReadyAt < HEALTH_WINDOW_MS) return;

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return;

  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateResult = esp_ota_get_state_partition(running, &state);
  if (stateResult == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t confirmResult = esp_ota_mark_app_valid_cancel_rollback();
    if (confirmResult != ESP_OK) {
      LOG_ERR("ATLAS_BOOT", "Native boot confirmation failed: %s", esp_err_to_name(confirmResult));
      return;
    }
  } else if (stateResult == ESP_OK && state != ESP_OTA_IMG_VALID && state != ESP_OTA_IMG_UNDEFINED) {
    LOG_ERR("ATLAS_BOOT", "Unexpected OTA image state %d; confirmation withheld", static_cast<int>(state));
    return;
  } else if (stateResult != ESP_OK && stateResult != ESP_ERR_NOT_SUPPORTED) {
    LOG_ERR("ATLAS_BOOT", "Could not read OTA image state: %s", esp_err_to_name(stateResult));
    return;
  }

  cancelPending();
  LOG_INF("ATLAS_BOOT", "OTA slot health confirmed after sustained runtime");
}

}  // namespace atlas_boot_health
