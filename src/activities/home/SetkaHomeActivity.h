#pragma once

#include <Bitmap.h>
#include <HalStorage.h>

#include <array>
#include <atomic>
#include <string>

#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"
#include "util/BookProgress.h"

// X4 Pro home in the Setka style (.claude/skills/ui-style, layout "3в"): up to
// three books in progress as covers with their percent, the current one as a
// row, and tabs to the library, "Ещё" and settings. Other boards keep the
// theme-driven HomeActivity.
class SetkaHomeActivity final : public Activity, private UiAppHost {
 public:
  SetkaHomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Home", renderer, mappedInput), UiAppHost(renderer) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isHomeActivity() const override { return true; }

 private:
  static constexpr int kMaxCovers = 3;
  enum Tab : int16_t { TabLibrary, TabMore, TabSettings, TabCount };

  struct Slot {
    RecentBook book;
    BookPosition position;
    char percentText[8]{};
    // The thumb is generated at the height it is drawn: rescaling a dithered
    // 1-bit image aliases badly. Both are set by the layout pass.
    int thumbHeight = 0;
    std::string thumbPath;
  };

  static void screenFn(UiScreen& screen, void* user);
  static void onBook(const freeink::ui::ActionEvent& event, void* user);
  static void onTab(const freeink::ui::ActionEvent& event, void* user);
  static void onOpenLibrary(const freeink::ui::ActionEvent& event, void* user);

  void loadBooks();
  void draw(UiScreen& screen);
  void drawStatusLine(int top, int width) const;
  void drawBooks(UiScreen& screen, int top, int bottom, int width);
  void drawEmpty(UiScreen& screen, int top, int width);
  void drawTabs(UiScreen& screen, int top, int bottom, int width);
  void drawCover(Slot& slot, int x, int y, int width, int height);
  bool generateMissingThumbs();

  int bookFocusCount() const { return slotCount > 0 ? slotCount : 1; }
  void moveFocus(int delta);
  void activateFocus();
  void openBook(int index);
  void openTab(int tab);

  std::array<Slot, kMaxCovers> slots;
  int slotCount = 0;
  int readingCount = 0;  // recent books still on the card
  std::string currentSubtitle;
  // Button focus (the books, or the empty-state row, then the tabs); written
  // by the loop task, read by the render task.
  std::atomic<int> focus{0};
  // Reset by onEnter, then render task only.
  bool firstPaint = true;
  bool thumbsChecked = false;
  setka::RefreshCadence cadence;
  HalFile coverFile;
  Bitmap coverBitmap{coverFile};
};
