#pragma once

// OTA rollback guard (adapted from guloff/folio-firmware, MIT). Rollback-enabled
// bootloaders (X4 Pro) boot a freshly flashed image as pending-verify, but the
// Arduino core confirms it before setup() runs, so a build that crashes during
// boot would never roll back. The confirmation is deferred until the image has
// proven itself.
namespace boothealth {

constexpr unsigned long HEALTHY_AFTER_MS = 10000;

// Call from loop(); confirms the running image after HEALTHY_AFTER_MS of runtime.
void confirmIfHealthy();

// Call before deep sleep or an intentional reboot: both go through a reset, which
// would otherwise roll back an image that booted fine but reset within the window.
// Firmware installers must also call it and abort on false: flashing over the
// other slot while this image is still pending would destroy the only rollback
// target. Returns true once the running image no longer needs confirming.
bool confirmNow(const char* reason);

}  // namespace boothealth
