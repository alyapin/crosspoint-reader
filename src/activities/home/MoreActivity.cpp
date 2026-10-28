#include "MoreActivity.h"

#include <I18n.h>

#include "CrossPointSettings.h"
#include "activities/games/GamesActivity.h"
#include "components/icons/listIcons.h"
#include "components/setka/setkaIcons.h"

void MoreActivity::onEnter() {
  entryCount = 0;
  entries[entryCount++] = Entry::Games;
  entries[entryCount++] = Entry::ReceiveBooks;
  entries[entryCount++] = Entry::FileTransfer;
  entries[entryCount++] = Entry::Catalogs;
  if (SETTINGS.pluginsEnabled) entries[entryCount++] = Entry::Plugins;
  entries[entryCount++] = Entry::SdFiles;
  SetkaListActivity::onEnter();
}

const char* MoreActivity::headerTitle() const { return tr(STR_MORE); }

const char* MoreActivity::rowLabel(const int index) const {
  switch (entries[index]) {
    case Entry::Games:
      return tr(STR_GAMES);
    case Entry::ReceiveBooks:
      return tr(STR_LOCALSEND_TITLE);
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

const freeink::Icon* MoreActivity::rowIcon(const int index) const {
  switch (entries[index]) {
    case Entry::Games:
      return &icon_gamepad_24;
    case Entry::ReceiveBooks:
      return &icon_download_24;
    case Entry::FileTransfer:
      return &icon_send_24;
    case Entry::Catalogs:
      return &icon_globe_24;
    case Entry::Plugins:
      return &icon_blocks_24;
    case Entry::SdFiles:
      return &icon_folder_24;
  }
  return nullptr;
}

void MoreActivity::activateIndex(const int index) {
  app.clearTapFlash();
  switch (entries[index]) {
    case Entry::Games:
      openChild(std::make_unique<GamesActivity>(renderer, mappedInput));
      break;
    case Entry::ReceiveBooks:
      activityManager.goToLocalSend();
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
