# Atlas Ink

Atlas Ink is Carlos's autonomous e-ink companion firmware for Hermes/Atlas on
the base XTEINK X4 (ESP32-C3). It starts from CrossPoint Reader v1.5.0 so the
known-good display, buttons, SD, Wi-Fi, power management, recovery and dual OTA
partition support remain intact while the Atlas interface is developed.

## Bootstrap 1.5.1

The first bootstrap deliberately keeps the CrossPoint user interface. Its only
new runtime responsibility is to establish the managed update channel:

- checks `carlosduque-incoxe/atlas-ink` releases automatically;
- checks only while USB power is connected;
- reuses the last Wi-Fi network already saved on the SD card;
- waits 30 seconds after boot, retries bounded failures, and checks every six
  hours after a successful no-update response;
- streams the image into the inactive OTA partition and relies on
  `esp_ota_end()` plus the ESP32 chip guard before switching slots;
- never erases stock flash, NVS, SD data, or Wi-Fi credentials.

This bootstrap trusts GitHub TLS and repository write control. Independent
firmware signing, boot health confirmation and hardened rollback are mandatory
before Atlas publishes feature-bearing autonomous updates.

## Release discipline

1. Build and test from a clean committed revision.
2. Inspect the ESP32-C3 image and partition fit.
3. Tag a semantic version; GitHub Actions publishes only `firmware.bin`.
4. Record the release asset size and SHA-256.
5. Never publish an autonomous update until its hardware-risk gates pass.

The original full-device stock backup remains private and is not part of this
repository.