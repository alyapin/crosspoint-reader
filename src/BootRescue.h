#pragma once

// Escape hatches for a build that cannot reach Settings -> SD firmware update:
// a combo held for 3 s at boot, or three crash resets in a row during start-up, restart
// into the other OTA slot when it holds a usable app. X4 Pro: Left + Right, already held on
// a deep-sleep wake or pressed up to 1.5 s into any other boot. X4 and X3: Back + Down,
// held before Power or Reset. Elsewhere and in the simulator these are no-ops.
namespace bootrescue {

// Call first in setup(), right after the power rails are latched, before any
// other peripheral init. Does not return when it switches slots. silentRestart: this boot
// follows the firmware's own silentRestart(), so the combo is not waited for.
void runAtBoot(bool silentRestart);

// Call from loop(): ends the crash streak once this boot has run HEALTHY_UPTIME_MS.
void clearIfHealthy();

// Call before deep sleep or an intentional reboot.
void clearCrashCount();

}  // namespace bootrescue
