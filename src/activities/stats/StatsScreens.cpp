#include "StatsScreens.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "LibraryShelvesActivity.h"
#include "StatisticsActivity.h"

void openStatistics(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  auto screen = makeUniqueNoThrow<StatisticsActivity>(renderer, mappedInput);
  if (!screen) {
    LOG_ERR("STAT", "OOM: statistics screen");
    return;
  }
  activityManager.replaceActivity(std::move(screen));
}

void openLibraryTabs(GfxRenderer& renderer, MappedInputManager& mappedInput, const int libraryTab) {
  const int tab = std::clamp(libraryTab, 0, static_cast<int>(LibraryShelvesActivity::TabCount) - 1);
  auto screen = makeUniqueNoThrow<LibraryShelvesActivity>(renderer, mappedInput, tab);
  if (!screen) {
    LOG_ERR("STAT", "OOM: library screen");
    return;
  }
  activityManager.replaceActivity(std::move(screen));
}
