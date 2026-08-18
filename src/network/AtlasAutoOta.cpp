#include "AtlasAutoOta.h"

// Disabled in 1.5.6 after physical XTEINK X4 validation showed that the
// synchronous network check starved the main UI loop exactly 30 seconds after
// boot. Manual Settings -> Update remains available through OtaUpdateActivity.
// Automatic checks will return only after they are implemented as a bounded,
// non-blocking state machine and physically validated on the X4.
namespace atlas_auto_ota {

void loop(bool usbConnected) { (void)usbConnected; }

}  // namespace atlas_auto_ota
