#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"
#include "util/ButtonNavigator.h"

// Settings → Display → Sleep image: a Setka grid with a "Random" tile and a
// thumbnail per picture in /.sleep and /sleep. Tapping a picture opens it full
// screen (SleepImagePreviewActivity); "Set" there stores it in
// CrossPointSettings::sleepImagePath, which SleepActivity's Custom screen then
// always shows. Thumbnails are generated lazily, one per repaint, into
// sleep_thumbs' SD cache.
// Adapted from guloff/folio-firmware (MIT), SleepImageSelectActivity: the
// "Random" entry plus a sleep-folder listing, and switching a sleep screen that
// shows no folder pictures to Custom on a pick.
class SleepImagePickerActivity final : public Activity, private UiAppHost {
 public:
  SleepImagePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t kMaxImages = 200;
  enum class Thumb : uint8_t { Unknown, Ready, Missing, Failed };
  struct Entry {
    std::string path;
    uint32_t key = 0;
    Thumb thumb = Thumb::Unknown;  // render task only
  };
  struct Geometry {
    int gridTop = 0;
    int footerRuleY = 0;
    int footerTextTop = 0;
    int tileW = 0;
    int tileH = 0;
    int perPage = 0;
  };

  static void screenFn(UiScreen& screen, void* user);
  static void onTile(const freeink::ui::ActionEvent& event, void* user);
  void layout();
  void scan();
  int findChosen() const;
  void buildScreen(UiScreen& screen);
  void drawTile(int tile, int x, int y);
  bool generateNextThumb();
  void openTile(int tile);
  void choose(int tile);
  void moveFocus(int tile);
  void showPage(int newPage);
  int tileCount() const { return static_cast<int>(entries.size()) + 1; }
  int pageCount() const { return (tileCount() + geo.perPage - 1) / geo.perPage; }

  Geometry geo;
  std::vector<Entry> entries;  // tile i + 1; tile 0 is "Random"
  int chosen = 0;              // tile of CrossPointSettings::sleepImagePath
  int focus = 0;               // button boards: tile the frame and Confirm act on
  int page = 0;
  setka::RefreshCadence cadence;  // render task only
  std::atomic<bool> entryRefresh{true};
  ButtonNavigator buttonNavigator;
};
