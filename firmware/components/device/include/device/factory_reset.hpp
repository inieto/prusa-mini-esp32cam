// Factory reset: forget the web user, WiFi, PrusaConnect token and camera settings.
//
// Hardware gesture (same as v1.1.2, so existing users know it): jumper GPIO12 to GND, power on,
// keep it there ~10 s while the red LED blinks, until the LED stays on; then remove the jumper
// and the camera reboots clean. Releasing early cancels. The per-device setup AP password and the
// reset history survive: they are facts about the board, not user configuration.
#pragma once

#include "core/ports.hpp"

namespace device {

// Erases user configuration from NVS (does not reboot).
void erase_user_config();

// Call once at boot, after NVS init and before reading any setting. Returns only if no reset
// was requested (or it was cancelled); otherwise it erases and reboots.
void check_factory_reset_pin(core::SystemControl& system);

}  // namespace device
