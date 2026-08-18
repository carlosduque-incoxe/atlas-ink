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
- requires an ECDSA P-256 signature from the pinned Atlas Ink release key;
- binds the signature to version, byte size and GitHub SHA-256 digest;
- calculates SHA-256 while streaming into the inactive OTA partition and
  refuses to select the slot unless size, digest, ESP image and chip guard pass;
- records the previous and target A/B slots in one checksummed NVS blob; a new
  slot gets one boot attempt and is armed only after storage, display, settings
  and activity routing initialize; it must then survive 60 seconds and at least
  100 complete main-loop iterations, otherwise the next reset retries rollback
  to the previous slot;
- never erases stock flash, NVS, SD data, or Wi-Fi credentials.

GitHub TLS protects transport, while the pinned key protects release authority.
The private signing key is stored outside GitHub and the repository. CI only
builds artifacts; Atlas verifies them, signs locally, and then creates a release.

## Release discipline

1. Build and test from a clean committed revision.
2. Inspect the ESP32-C3 image and partition fit.
3. Tag a semantic version; CI builds an immutable artifact from that tag.
4. Atlas verifies the build, signs a canonical manifest locally, then publishes
   `firmware.bin`, `firmware.bin.manifest`, and `firmware.bin.sig`.
5. Verify signature, release digest, image chip and partition fit independently.
6. Never publish an autonomous update until its hardware-risk gates pass.

The original full-device stock backup remains private and is not part of this
repository.