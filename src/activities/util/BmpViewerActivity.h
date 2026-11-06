#pragma once

#include <cstdlib>
#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "activities/Activity.h"

class BmpViewerActivity final : public Activity {
 public:
  BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  // A sideways swipe that opens a sibling image (loop()) is not edge Back.
  bool claimsSwipe(const int sx, const int sy, const int ex, const int ey) override {
    if (currentImageIndex < 0 || std::abs(ex - sx) < std::abs(ey - sy)) return false;
    const int target = currentImageIndex + (ex < sx ? 1 : -1);
    return target >= 0 && target < static_cast<int>(siblingImages.size());
  }

 private:
  void loadSiblingImages();
  void doSetSleepCover();
  bool canSetSleepCover() const;
  bool renderImage();
  bool saveJpegSleepCover();

  std::string filePath;
  std::vector<std::string> siblingImages;
  int currentImageIndex = -1;
};
