# Atlas Ink — signed firmware channel

This repository is the public **binary update channel** for Atlas Ink devices.

It intentionally contains no application source code, infrastructure details, account data, credentials, private endpoints, or device configuration. Releases contain only:

- `firmware.bin`
- `firmware.bin.manifest`
- `firmware.bin.sig`

Devices verify the canonical manifest, SHA-256 digest, ESP32-C3 chip identity, and pinned ECDSA signature before activating an A/B OTA slot. GitHub hosting alone is not trusted as firmware authority.
