#include "AtlasAutoOta.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "OtaUpdater.h"
#include "WifiCredentialStore.h"

namespace atlas_auto_ota {
namespace {

constexpr unsigned long INITIAL_DELAY_MS = 30000UL;
constexpr unsigned long NO_CREDENTIAL_RETRY_MS = 120000UL;
constexpr unsigned long FAILURE_RETRY_MS = 900000UL;
constexpr unsigned long SUCCESS_RETRY_MS = 21600000UL;
constexpr unsigned long CONNECT_TIMEOUT_MS = 15000UL;

unsigned long nextAttemptAt = INITIAL_DELAY_MS;
bool running = false;

void scheduleAfter(unsigned long delayMs) { nextAttemptAt = millis() + delayMs; }

void shutDownWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool connectSavedNetwork() {
  WIFI_STORE.loadFromFile();
  const std::string ssid = WIFI_STORE.getLastConnectedSsid();
  if (ssid.empty()) {
    LOG_INF("ATLAS_OTA", "No saved WiFi network; retry deferred");
    scheduleAfter(NO_CREDENTIAL_RETRY_MS);
    return false;
  }

  const auto credential = WIFI_STORE.findCredential(ssid);
  if (!credential) {
    LOG_ERR("ATLAS_OTA", "Saved WiFi entry is unavailable");
    scheduleAfter(NO_CREDENTIAL_RETRY_MS);
    return false;
  }

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setAutoReconnect(false);
  WiFi.begin(credential->ssid.c_str(), credential->password.c_str());

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < CONNECT_TIMEOUT_MS) delay(100);

  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("ATLAS_OTA", "WiFi connection timed out");
    shutDownWifi();
    scheduleAfter(FAILURE_RETRY_MS);
    return false;
  }
  return true;
}

}  // namespace

void loop(bool usbConnected) {
  if (running || !usbConnected || static_cast<long>(millis() - nextAttemptAt) < 0) return;
  running = true;
  LOG_INF("ATLAS_OTA", "Autonomous update check starting");

  if (!connectSavedNetwork()) {
    running = false;
    return;
  }

  OtaUpdater updater;
  const auto check = updater.checkForUpdate();
  if (check == OtaUpdater::OK && updater.isUpdateNewer()) {
    LOG_INF("ATLAS_OTA", "Installing Atlas Ink %s", updater.getLatestVersion().c_str());
    const auto install = updater.installUpdate();
    if (install == OtaUpdater::OK) {
      LOG_INF("ATLAS_OTA", "Install verified; rebooting into new slot");
      delay(100);
      ESP.restart();
    }
    LOG_ERR("ATLAS_OTA", "Install failed: %d", static_cast<int>(install));
    scheduleAfter(FAILURE_RETRY_MS);
  } else if (check == OtaUpdater::OK || check == OtaUpdater::NO_UPDATE) {
    LOG_INF("ATLAS_OTA", "Firmware is current");
    scheduleAfter(SUCCESS_RETRY_MS);
  } else {
    LOG_ERR("ATLAS_OTA", "Update check failed: %d", static_cast<int>(check));
    scheduleAfter(FAILURE_RETRY_MS);
  }

  shutDownWifi();
  running = false;
}

}  // namespace atlas_auto_ota