#pragma once

#include <atomic>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"

// One sleep picture full screen, exactly as the Custom sleep screen shows it
// (real grays), under a bottom bar with "‹ Back" and "Set". Finishes with
// isCancelled = false when the user picks Set; the caller stores the choice.
class SleepImagePreviewActivity final : public Activity, private UiAppHost {
 public:
  SleepImagePreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static void screenFn(UiScreen& screen, void* user);
  static void onAction(const freeink::ui::ActionEvent& event, void* user);
  static void paintBar(GfxRenderer& renderer, bool grayPlane, void* user);
  void close(bool set);

  const std::string path;
  // "Set" acts only once the picture is on screen; it is hidden when it failed to load.
  std::atomic<bool> ready{false};
  bool failed = false;  // render task only
};
