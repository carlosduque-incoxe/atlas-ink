#pragma once

#include <cstdint>

namespace atlas_boot_health {

bool recordPending(uint32_t previousAddress, uint32_t targetAddress);
void cancelPending();
void begin();
void markRuntimeReady();
void loop();

}  // namespace atlas_boot_health