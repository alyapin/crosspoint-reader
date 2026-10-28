#pragma once

#include <Epub/ReaderRenderSpec.h>

#include <cstdint>
#include <string>
#include <vector>

#include "BookPageMap.h"
#include "ReadingPace.h"

class Epub;
class Section;

// Whole-book page and reading time left for the EPUB reader's status bar.
// Chapter page counts are cached per book and layout in the book's cache
// folder; the reading speed is personal and kept in /.crosspoint.
class BookFooter {
 public:
  // Identifies everything that changes how a chapter paginates.
  static uint32_t layoutKey(const ReaderRenderSpec& spec);

  void open(const Epub& epub);
  // Before drawing the page section->currentPage of `spine`. manualTurn is
  // false for automatic page turns, which say nothing about reading speed.
  void onPage(uint32_t layoutKey, int spine, const Section& section, uint32_t pageOffset, bool manualTurn);
  // A chapter laid out off screen under layoutKey.
  void noteChapter(uint32_t layoutKey, int spine, const Section& section);
  // The footer on screen shows an estimate, but every count is known now.
  bool estimateOutdated() const;
  // A chapter whose off-screen build failed stays estimated until the book is reopened.
  void skipSpine(int spine);
  // Next chapter to lay out off screen, or -1 when every count is known.
  int nextSpineToPaginate(int currentSpine) const;
  bool tracksLayout(const uint32_t key) const { return layoutSet && map.layoutKey() == key; }

  // Writes whatever changed; cheap when nothing did.
  void persist();
  void close();

  const char* pageText() const { return pageText_; }
  const char* timeText() const { return timeText_; }

 private:
  void loadMap();
  void savePace();
  // Records a finished section's page count and last page offset.
  void recordChapter(uint32_t layoutKey, int spine, const Section& section);

  BookPageMap map;
  ReadingPace pace;
  std::vector<bool> skipped;
  std::string mapPath;
  bool layoutSet = false;
  bool mapDirty = false;
  uint8_t paceUnsaved = 0;

  // The page on screen, for timing the next forward turn.
  int shownSpine = -1;
  int shownPage = -1;
  uint32_t shownOffset = 0;
  uint32_t shownMs = 0;
  uint32_t shownVisit = 0;
  bool shownExact = false;

  // Characters per page of the current chapter, used until the map has enough laid-out pages.
  int cppSpine = -1;
  uint16_t cppPages = 0;
  uint32_t chapterCharsPerPage = 1500;

  char pageText_[40] = "";
  char timeText_[40] = "";
};
