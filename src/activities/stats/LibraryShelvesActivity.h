#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "StatsUi.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "stats/ReadingStats.h"

// «Библиотека» by reading status: tabs «Читаю / Прочитано / Отложено /
// Полки». Book rows show the cover, title and progress; a long press (or a
// held Confirm) opens the menu to change the status or put the book on a
// shelf. The Полки tab lists the shelves and opens one. Buttons move one
// focus ring (tab strip, then rows); Confirm on the strip steps the tab.
class LibraryShelvesActivity final : public Activity, private UiAppHost {
 public:
  enum Tab : int { TabReading, TabFinished, TabPaused, TabShelves, TabCount };

  LibraryShelvesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int tab = TabReading)
      : Activity("LibraryShelves", renderer, mappedInput), UiAppHost(renderer), tab(tab) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct BookRow {
    int id = 0;
    stats::BookStatus status = stats::BookStatus::None;
    uint32_t shelves = 0;
    uint32_t order = 0;  // newest first
    char detail[48] = "";
    std::string path;
    std::string title;
    std::string author;
    std::string thumb;  // resolved when first drawn
    bool thumbResolved = false;
  };
  struct ShelfRow {
    uint8_t slot = 0;
    uint16_t books = 0;
    std::string name;
  };
  enum class Menu : uint8_t { None, Book, PickShelf, Shelf };
  enum class Item : uint8_t { Finished, Paused, Reading, ToShelf, Rename, Delete, Shelf, NewShelf };
  struct MenuEntry {
    Item item;
    uint8_t shelf;
  };
  static constexpr int kMaxMenu = stats::MAX_SHELVES + 1;

  static void screenFn(UiScreen& screen, void* user);
  static void onTab(const freeink::ui::ActionEvent& event, void* user);
  static void onRow(const freeink::ui::ActionEvent& event, void* user);
  static void onMenu(const freeink::ui::ActionEvent& event, void* user);
  static void onDismiss(const freeink::ui::ActionEvent& event, void* user);

  void reload();
  void draw(UiScreen& screen);
  int drawTabs(UiScreen& screen, int stripTop, int width);
  void drawBookRow(UiScreen& screen, int index, int top, int width);
  static void describeMenu(int index, const freeink::Icon*& icon, const char*& label, void* ctx);
  void drawMenu(UiScreen& screen, int areaTop, int bottom, int width);
  bool generateMissingThumbs();

  int rowCount() const;
  void selectTab(int next);
  void moveFocus(int delta);
  void activateRow(int index);
  void openMenu(int index);
  void openShelfPicker();
  void chooseMenu(int index);
  void closeMenu();
  void askShelfName(int renameSlot);

  int tab;
  std::vector<BookRow> books;
  std::vector<ShelfRow> shelfRows;
  // Loop-task state, read by the render task under the render lock.
  int focus = -1;  // -1: the tab strip
  int top = 0;     // first visible row
  bool buttonsUsed = false;
  Menu menu = Menu::None;
  int menuRow = -1;
  MenuEntry menuEntries[kMaxMenu] = {};
  int menuCount = 0;
  int menuFocus = 0;
  // Measured by the render task.
  std::atomic<int> visibleRows{1};

  bool entryRefresh = true;
  bool thumbsChecked = false;
  setka::RefreshCadence cadence;
  statsui::CoverPainter covers;
};
