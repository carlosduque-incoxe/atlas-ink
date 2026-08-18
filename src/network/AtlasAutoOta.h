#pragma once

namespace atlas_auto_ota {

// Call from the main loop. Work is attempted only while USB power is present.
void loop(bool usbConnected);

}  // namespace atlas_auto_ota