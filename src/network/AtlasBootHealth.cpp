#include "AtlasBootHealth.h"

#include <Arduino.h>
#include <Logging.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

namespace atlas_boot_health {
namespace {

constexpr char NAMESPACE[] = "atlas-boot";
constexpr unsigned long HEALTH_WINDOW_MS = 60000UL;
bool pendingBoot = false;
unsigned long bootStartedAt = 0;

const esp_partition_t* findAppPartition(uint32_t address) {
  esp_partition_iterator_t iterator = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  const esp_partition_t* match = nullptr;
  while (iterator) {
    const esp_partition_t* candidate = esp_partition_get(iterator);
    if (candidate && candidate->address == address) {
      match = candidate;
      break;
    }
    iterator = esp_partition_next(iterator);
  }
  if (iterator) esp_partition_iterator_release(iterator);
  return match;
}

}  // namespace

bool recordPending(uint32_t previousAddress, uint32_t targetAddress) {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return false;
  const bool ok = prefs.putUInt("prev", previousAddress) == sizeof(uint32_t) &&
                  prefs.putUInt("target", targetAddress) == sizeof(uint32_t) &&
                  prefs.putUChar("attempts", 0) == sizeof(uint8_t) && prefs.putBool("pending", true) == sizeof(bool);
  prefs.end();
  return ok;
}

void cancelPending() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return;
  prefs.clear();
  prefs.end();
  pendingBoot = false;
}

void begin() {
  Preferences prefs;
  if (!prefs.begin(NAMESPACE, false)) return;
  if (!prefs.getBool("pending", false)) {
    prefs.end();
    return;
  }

  const uint32_t previousAddress = prefs.getUInt("prev", 0);
  const uint32_t targetAddress = prefs.getUInt("target", 0);
  const uint8_t attempts = prefs.getUChar("attempts", 0);
  const esp_partition_t* running = esp_ota_get_running_partition();

  if (!running || running->address != targetAddress) {
    LOG_INF("ATLAS_BOOT", "Clearing stale OTA health marker");
    prefs.clear();
    prefs.end();
    return;
  }

  if (attempts >= 1) {
    const esp_partition_t* previous = findAppPartition(previousAddress);
    if (previous && esp_ota_set_boot_partition(previous) == ESP_OK) {
      LOG_ERR("ATLAS_BOOT", "Health check failed; rolling back to %s", previous->label);
      prefs.clear();
      prefs.end();
      delay(100);
      ESP.restart();
      return;
    }
    LOG_ERR("ATLAS_BOOT", "Rollback target unavailable; keeping current slot");
    prefs.clear();
    prefs.end();
    return;
  }

  if (prefs.putUChar("attempts", 1) != sizeof(uint8_t)) {
    const esp_partition_t* previous = findAppPartition(previousAddress);
    if (previous) esp_ota_set_boot_partition(previous);
    prefs.clear();
    prefs.end();
    LOG_ERR("ATLAS_BOOT", "Could not persist boot attempt; reverting");
    delay(100);
    ESP.restart();
    return;
  }
  prefs.end();
  pendingBoot = true;
  bootStartedAt = millis();
  LOG_INF("ATLAS_BOOT", "New OTA slot is pending health confirmation");
}

void loop() {
  if (!pendingBoot || millis() - bootStartedAt < HEALTH_WINDOW_MS) return;
  const esp_err_t result = esp_ota_mark_app_valid_cancel_rollback();
  if (result != ESP_OK && result != ESP_ERR_NOT_SUPPORTED && result != ESP_ERR_INVALID_STATE) {
    LOG_ERR("ATLAS_BOOT", "Bootloader confirmation failed: %s", esp_err_to_name(result));
    return;
  }
  cancelPending();
  LOG_INF("ATLAS_BOOT", "OTA slot health confirmed");
}

}  // namespace atlas_boot_health