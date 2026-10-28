#pragma once

// Escape hatches for a build that cannot reach Settings -> SD firmware update:
// Left + Right held for 3 s at boot, or three crash resets in a row, restart
// into the other OTA slot when it holds a usable app. X4 Pro only; elsewhere
// and in the simulator these are no-ops.
namespace bootrescue {

// Call first in setup(), right after the power rails are latched, before any
// other peripheral init. Does not return when it switches slots.
void runAtBoot();

// Call from loop(): ends the crash streak once this boot has run HEALTHY_UPTIME_MS.
void clearIfHealthy();

// Call before deep sleep or an intentional reboot.
void clearCrashCount();

}  // namespace bootrescue
