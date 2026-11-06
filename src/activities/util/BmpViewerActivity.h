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
  // Sideways swipes step through the folder's images, from an edge too.
  bool claimsSwipe(const int sx, const int sy, const int ex, const int ey) override {
    return siblingImages.size() > 1 && std::abs(ex - sx) >= std::abs(ey - sy);
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
