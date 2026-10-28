#include "MoreActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "activities/games/GamesActivity.h"

void MoreActivity::onEnter() {
  SetkaListActivity::onEnter();
  entryCount = 0;
  entries[entryCount++] = Entry::Games;
  entries[entryCount++] = Entry::FileTransfer;
  entries[entryCount++] = Entry::Catalogs;
  if (SETTINGS.pluginsEnabled) entries[entryCount++] = Entry::Plugins;
  entries[entryCount++] = Entry::SdFiles;
}

const char* MoreActivity::headerTitle() const { return tr(STR_MORE); }

const char* MoreActivity::rowLabel(const int index) const {
  switch (entries[index]) {
    case Entry::Games:
      return tr(STR_GAMES);
    case Entry::FileTransfer:
      return tr(STR_FILE_TRANSFER);
    case Entry::Catalogs:
      return tr(STR_BOOK_CATALOGS);
    case Entry::Plugins:
      return tr(STR_PLUGINS);
    case Entry::SdFiles:
      return tr(STR_SD_FILES);
  }
  return "";
}

void MoreActivity::activateIndex(const int index) {
  app.clearTapFlash();
  switch (entries[index]) {
    case Entry::Games:
      openChild(std::make_unique<GamesActivity>(renderer, mappedInput));
      break;
    case Entry::FileTransfer:
      activityManager.goToFileTransfer();
      break;
    case Entry::Catalogs:
      activityManager.goToBrowser();
      break;
    case Entry::Plugins:
      // Catalogs have their own row here, so the plugin list skips its OPDS entry.
      activityManager.goToPlugins(false);
      break;
    case Entry::SdFiles:
      activityManager.goToFileBrowser();
      break;
  }
}
