#pragma once

#include <HalStorage.h>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

class UsbDriveActivity final : public Activity, private UiAppHost {
 public:
  // toMore: opened under Ещё, so the exit reboot lands there.
  UsbDriveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const bool toMore = false)
      : Activity("UsbDrive", renderer, mappedInput), UiAppHost(renderer), toMore(toMore) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::Connected || (!startFailed && state == State::IoError); }
  bool requiresExclusiveStorageLoop() const override { return true; }

 private:
  using State = UsbDriveState;

  static constexpr unsigned long HOST_WAIT_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  static constexpr unsigned long START_FAILURE_TIMEOUT_MS = 30UL * 1000UL;
  static constexpr unsigned long FORCED_DISCONNECT_TIMEOUT_MS = 1000UL;
  static constexpr unsigned long HOST_SUSPEND_TIMEOUT_MS = 2000UL;

  static void driveScreen(UiScreen& screen, void* user);
  void buildDriveScreen(UiScreen& screen) const;
  // Lands on Ещё when opened from there, unless `home` (Power or the Home gesture).
  void restartToHome(bool home = false);

  const bool toMore;
  State state = State::Unsupported;
  bool preparing = true;
  bool startFailed = false;
  bool restartRequested = false;
  bool forcedDisconnectRequested = false;
  bool hostSuspendPending = false;
  unsigned long hostWaitStartedAt = 0;
  unsigned long startFailureStartedAt = 0;
  unsigned long forcedDisconnectRequestedAt = 0;
  unsigned long hostSuspendStartedAt = 0;
};
