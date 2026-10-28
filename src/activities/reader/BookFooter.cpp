#include "BookFooter.h"

#include <Arduino.h>
#include <Epub.h>
#include <Epub/Section.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <optional>

#include "activities/ActivityManager.h"

namespace {
constexpr char PACE_PATH[] = "/.crosspoint/reading_pace.bin";
// Samples gathered before the pace is written again (it is also written on close).
constexpr uint8_t PACE_SAVE_EVERY = 8;
// Longest supported spine list; anything bigger keeps every chapter estimated.
constexpr size_t MAX_MAP_BYTES = 16 * 1024;
}  // namespace

uint32_t BookFooter::layoutKey(const ReaderRenderSpec& spec) {
  uint32_t key = readerRenderSpecSignature(spec);
  const auto mix = [&key](const uint32_t value) {
    key ^= value;
    key *= 16777619U;
  };
  mix(spec.paragraphIndentSpaces);
  mix(Section::fileVersion());
  return key;
}

void BookFooter::open(const Epub& epub) {
  const int count = epub.getSpineItemsCount();
  std::vector<uint32_t> ends;
  ends.reserve(count);
  for (int i = 0; i < count; i++) ends.push_back(static_cast<uint32_t>(epub.getCumulativeSpineItemSize(i)));
  map.reset(std::move(ends), 0);
  skipped.assign(count, false);
  mapPath = epub.getCachePath() + "/book_pages.bin";

  HalFile f;
  if (Storage.openFileForRead("BFT", PACE_PATH, f)) {
    uint8_t data[ReadingPace::ENCODED_SIZE];
    const int read = f.read(data, sizeof(data));
    if (read > 0 && !pace.decode(data, static_cast<size_t>(read))) LOG_ERR("BFT", "Ignoring damaged reading pace");
  }
}

void BookFooter::loadMap() {
  HalFile f;
  if (!Storage.openFileForRead("BFT", mapPath, f)) return;
  const size_t size = f.size();
  if (size == 0 || size > MAX_MAP_BYTES) return;
  std::vector<uint8_t> data(size);
  if (f.read(data.data(), size) != static_cast<int>(size) || !map.decode(data.data(), size)) {
    LOG_DBG("BFT", "Book page cache is for another layout");
  }
}

void BookFooter::onPage(const uint32_t key, const int spine, const Section& section, const uint32_t pageOffset,
                        const bool manualTurn) {
  pageText_[0] = '\0';
  timeText_[0] = '\0';
  if (spine < 0 || static_cast<size_t>(spine) >= map.spineCount()) return;

  if (!tracksLayout(key)) {
    map.relayout(key);
    skipped.assign(map.spineCount(), false);
    loadMap();
    layoutSet = true;
    mapDirty = false;
  }
  const bool chapterExact = !section.isBuilding() && !section.isPartial();
  if (chapterExact && map.pagesOf(spine) != section.pageCount) recordChapter(key, spine, section);

  const uint32_t now = millis();
  const uint32_t visit = activityManager.visit();
  const int page = section.currentPage;
  if (spine != shownSpine || page != shownPage || visit != shownVisit) {
    // A forward turn within the chapter, with no other screen in between, times one page of reading.
    if (manualTurn && visit == shownVisit && spine == shownSpine && page == shownPage + 1 && pageOffset > shownOffset &&
        pace.addPage(pageOffset - shownOffset, now - shownMs)) {
      paceUnsaved++;
    }
    shownSpine = spine;
    shownPage = page;
    shownOffset = pageOffset;
    shownMs = now;
    shownVisit = visit;
  }

  uint32_t charsPerPage = map.charsPerPage();
  if (charsPerPage == 0) {
    if (spine != cppSpine || section.pageCount != cppPages) {
      cppSpine = spine;
      cppPages = section.pageCount;
      const auto last =
          section.pageCount >= 2 ? section.getVisibleTextOffsetForPage(section.pageCount - 1) : std::nullopt;
      if (last && *last >= section.pageCount - 1u) chapterCharsPerPage = *last / (section.pageCount - 1u);
    }
    charsPerPage = chapterCharsPerPage;
  }

  const auto pos = map.locate(spine, page < 0 ? 0 : page, section.estimatedTotalPages(), chapterExact);
  shownExact = pos.exact;
  char pages[sizeof(pageText_)];
  snprintf(pages, sizeof(pages), tr(STR_BOOK_PAGE_OF), static_cast<unsigned>(pos.page),
           static_cast<unsigned>(pos.total));
  snprintf(pageText_, sizeof(pageText_), "%s%s", pos.exact ? "" : "\xE2\x89\x88 ", pages);

  const uint32_t pagesLeft = pos.total - pos.page + 1;
  formatTimeLeft(readingSecondsLeft(pagesLeft, charsPerPage, pace.charsPerMinute()), tr(STR_TIME_LEFT_MIN),
                 tr(STR_TIME_LEFT_HOURS), tr(STR_TIME_LEFT_HOURS_MIN), timeText_, sizeof(timeText_));
}

void BookFooter::noteChapter(const uint32_t layoutKey, const int spine, const Section& section) {
  if (spine >= 0 && map.pagesOf(static_cast<size_t>(spine)) != section.pageCount) {
    recordChapter(layoutKey, spine, section);
  }
}

void BookFooter::recordChapter(const uint32_t layoutKey, const int spine, const Section& section) {
  const auto last = section.pageCount > 0 ? section.getVisibleTextOffsetForPage(section.pageCount - 1) : std::nullopt;
  if (map.setPages(layoutKey, static_cast<size_t>(spine), section.pageCount, last.value_or(0))) mapDirty = true;
}

bool BookFooter::estimateOutdated() const {
  return layoutSet && !shownExact && shownSpine >= 0 && map.known(static_cast<size_t>(shownSpine)) &&
         map.nextUnknown(static_cast<size_t>(shownSpine)) < 0;
}

void BookFooter::skipSpine(const int spine) {
  if (spine >= 0 && static_cast<size_t>(spine) < skipped.size()) skipped[spine] = true;
}

int BookFooter::nextSpineToPaginate(const int currentSpine) const {
  if (currentSpine < 0) return -1;
  // nextUnknown wraps around once, so it visits every other chapter before repeating.
  int from = currentSpine;
  for (size_t tries = 0; tries < map.spineCount(); tries++) {
    const int spine = map.nextUnknown(static_cast<size_t>(from));
    if (spine < 0 || spine == currentSpine) return -1;
    if (!skipped[spine]) return spine;
    from = spine;
  }
  return -1;
}

void BookFooter::persist() {
  if (mapDirty) {
    const std::vector<uint8_t> data = map.encode();
    HalFile f;
    if (Storage.openFileForWrite("BFT", mapPath, f) && f.write(data.data(), data.size()) == data.size()) {
      mapDirty = false;
    } else {
      LOG_ERR("BFT", "Failed to save book page counts");
    }
  }
  if (paceUnsaved >= PACE_SAVE_EVERY) savePace();
}

void BookFooter::close() {
  persist();
  if (paceUnsaved > 0) savePace();
}

void BookFooter::savePace() {
  uint8_t data[ReadingPace::ENCODED_SIZE];
  pace.encode(data);
  HalFile f;
  if (Storage.openFileForWrite("BFT", PACE_PATH, f) && f.write(data, sizeof(data)) == sizeof(data)) {
    paceUnsaved = 0;
  } else {
    LOG_ERR("BFT", "Failed to save reading pace");
  }
}
