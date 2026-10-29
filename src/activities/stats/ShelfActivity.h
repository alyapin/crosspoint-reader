#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "StatsUi.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"

// One shelf as a 3-column cover grid with a «+» tile that lists the other
// catalog books to add. Tap or Confirm opens a book; a long press (or held
// Confirm) offers to take it off the shelf. Back leaves the picker, then the
// shelf.
class ShelfActivity final : public Activity, private UiAppHost {
 public:
  ShelfActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, uint8_t shelf)
      : Activity("Shelf", renderer, mappedInput), UiAppHost(renderer), shelf(shelf) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Book {
    int id = 0;
    uint32_t order = 0;  // last read
    std::string path;
    std::string title;
    std::string thumb;  // resolved when first drawn
    bool thumbResolved = false;
  };

  static void screenFn(UiScreen& screen, void* user);
  static void onCell(const freeink::ui::ActionEvent& event, void* user);
  static void onPick(const freeink::ui::ActionEvent& event, void* user);
  static void onMenu(const freeink::ui::ActionEvent& event, void* user);
  static void onDismiss(const freeink::ui::ActionEvent& event, void* user);
  static void describeMenu(int index, const freeink::Icon*& icon, const char*& label, void* ctx);

  void reload();
  // reload() for a caller that holds the render lock.
  void reloadLocked();
  void draw(UiScreen& screen);
  // Returns where its content ends.
  int drawGrid(UiScreen& screen, int areaTop, int bottom, int width);
  void drawPicker(UiScreen& screen, int areaTop, int bottom, int width);
  bool generateMissingThumbs();

  int itemCount() const;
  void moveFocus(int delta);
  void activate(int index);
  void openMenu(int index);
  void closeMenu();
  void setPicking(bool on);

  uint8_t shelf;
  std::string name;
  std::vector<Book> books;       // on the shelf
  std::vector<Book> candidates;  // the picker: catalog books not on it
  unsigned shelfTotal = 0;       // books on the shelf, beyond the listed ones too
  unsigned shelfFinished = 0;
  // Loop-task state, read by the render task.
  bool picking = false;
  int focus = 0;
  int top = 0;  // first visible grid row or picker row
  bool buttonsUsed = false;
  bool menuOpen = false;
  int menuCell = -1;
  // Measured by the render task: grid or picker rows that fit, and grid columns.
  std::atomic<int> visibleRows{1};

  bool entryRefresh = true;
  bool thumbsChecked = false;
  setka::RefreshCadence cadence;
  statsui::CoverPainter covers;
};
