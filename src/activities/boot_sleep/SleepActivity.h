#pragma once
#include <string>

#include "activities/Activity.h"

class Bitmap;
class HalFile;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput), fromTimeout(fromTimeout) {}
  void onEnter() override;

  // Draws `chrome` over a sleep picture preview: in the B/W frame, and in each
  // gray plane (grayPlane), where it must leave the plane neutral or redraw
  // its B/W pixels (GfxRenderer::grayPlanesAreAbsolute()).
  using PreviewChrome = void (*)(GfxRenderer& renderer, bool grayPlane, void* user);
  // Shows the BMP at `path` full screen as the Custom sleep screen would
  // (placement, filter, real grays) with `chrome` on top, and leaves the B/W
  // frame in the framebuffer for later updates. False when it could not be
  // shown in full; the caller paints its own error screen then.
  static bool renderPreview(GfxRenderer& renderer, const std::string& path, PreviewChrome chrome, void* user);

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  bool renderSleepBmp(const std::string& path) const;
  void renderCoverSleepScreen() const;
  // False when the pixels could not be read or the gray base or a gray plane
  // failed (a partial frame may already be on the panel); the caller can try
  // another picture.
  bool renderBitmapSleepScreen(const Bitmap& bitmap, bool preserveBackground = false) const;
  bool renderSleepOverlayFile(HalFile& file, const char* pathForLog) const;
  bool renderTransparentOverlayPng(const std::string& path) const;
  bool renderSleepOverlayPath(const std::string& path) const;
  void renderLastScreenSleepScreen() const;
  void renderTransparentCustomSleepScreen() const;
  void renderBlankSleepScreen() const;

  bool fromTimeout = false;
  // Cover + Transparent: the cover under the overlay, and the same cover while its gray planes are redrawn.
  mutable const Bitmap* overlayCover = nullptr;
  mutable const Bitmap* overlayGrayCover = nullptr;
};
