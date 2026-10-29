#include "BookProgress.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>

namespace {
uint32_t readLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

BookPosition loadBookPosition(const std::string& path) {
  BookPosition position;
  uint8_t data[10]{};
  if (FsHelpers::hasReflowableBookExtension(path)) {
    // Metadata objects exceed the stack budget; load one book at a time.
    auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
    if (!epub) {
      LOG_ERR("HOME", "OOM: progress metadata");
      position.unreadable = true;
      return position;
    }
    const std::string progressPath = epub->getCachePath() + "/progress.bin";
    position.unreadable = Storage.exists(progressPath.c_str());
    if (!epub->load(false, true)) return position;
    HalFile file;
    if (!Storage.openFileForRead("HOME", progressPath, file)) return position;
    const int size = file.read(data, sizeof(data));
    if (size != 4 && size != 6 && size != 10) return position;
    const int spine = data[0] | (data[1] << 8);
    const int page = data[2] | (data[3] << 8);
    const int total = size >= 6 ? data[4] | (data[5] << 8) : 0;
    const int spineCount = epub->getSpineItemsCount();
    if (spineCount <= 0 || epub->getBookSize() == 0 || spine > spineCount) return position;
    const int tocCount = epub->getTocItemsCount();
    const int tocIndex = epub->getTocIndexForSpineIndex(std::min(spine, spineCount - 1));
    if (tocCount > 0 && tocIndex >= 0) {
      position.chapter = tocIndex + 1;
      position.chapterCount = tocCount;
    }
    position.unreadable = false;
    if (spine == spineCount) {
      position.percent = 100;
      return position;
    }
    const float fraction = total > 0 && page != UINT16_MAX ? std::clamp(float(page) / total, 0.0f, 1.0f) : 0;
    position.percent = std::clamp(static_cast<int>(epub->calculateProgress(spine, fraction) * 100 + 0.5f), 0, 100);
    return position;
  }
  if (FsHelpers::hasXtcExtension(path)) {
    auto xtc = makeUniqueNoThrow<Xtc>(path, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("HOME", "OOM: XTC progress metadata");
      position.unreadable = true;
      return position;
    }
    const std::string progressPath = xtc->getCachePath() + "/progress.bin";
    position.unreadable = Storage.exists(progressPath.c_str());
    if (!xtc->load()) return position;
    HalFile file;
    if (!Storage.openFileForRead("HOME", progressPath, file) || file.read(data, 4) != 4) return position;
    const uint32_t page = readLe32(data);
    if (xtc->getPageCount() == 0) return position;
    position.unreadable = false;
    position.percent = page >= xtc->getPageCount() ? 100 : xtc->calculateProgress(page);
    return position;
  }
  return position;
}
