#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Whole-book page numbers for a reflowed EPUB. Chapters paginate lazily, so a
// chapter's count is exact once its section has been laid out and an estimate
// (its HTML bytes x the book's pages per byte) until then. Counts are valid
// for one layout only: the owner keys the map by a layout signature and drops
// it when the signature changes. Each laid-out chapter also records where its
// last page starts in visible characters, which gives the layout's average
// characters per page.
class BookPageMap {
 public:
  static constexpr uint16_t UNKNOWN = 0xFFFF;

  struct Position {
    uint32_t page = 0;   // 1-based page in the book
    uint32_t total = 0;  // pages in the book
    bool exact = false;  // every chapter's count is known
  };

  // spineEnds[i] is the cumulative HTML size through spine item i.
  void reset(std::vector<uint32_t> spineEnds, uint32_t layoutKey);
  // Same book, another layout: every count is unknown again.
  void relayout(uint32_t layoutKey);
  uint32_t layoutKey() const { return key; }
  size_t spineCount() const { return pages.size(); }

  // Records a chapter laid out under layoutKey: its page count and the
  // visible-character offset its last page starts at. A count from another
  // layout is ignored. True when the map changed.
  bool setPages(uint32_t layoutKey, size_t spine, uint16_t count, uint32_t lastPageOffset);
  bool known(size_t spine) const { return spine < pages.size() && pages[spine] != UNKNOWN; }
  uint16_t pagesOf(size_t spine) const { return spine < pages.size() ? pages[spine] : UNKNOWN; }

  // pageIndex is zero-based within `spine`; livePages is that chapter's best
  // known count (Section::estimatedTotalPages), exact when liveExact.
  Position locate(size_t spine, uint32_t pageIndex, uint32_t livePages, bool liveExact) const;

  // Visible characters per full page over the laid-out chapters; 0 until
  // enough pages are known for a stable figure.
  uint32_t charsPerPage() const;

  // First spine without a count, searching forward from `from` and wrapping,
  // never `from` itself; -1 when there is none.
  int nextUnknown(size_t from) const;

  // Persisted form: magic, version, layout key, spine count, then per spine
  // the count and the last page's offset.
  std::vector<uint8_t> encode() const;
  // Restores counts saved for the same layout and spine count; false (map
  // left as is) on any mismatch or a damaged file.
  bool decode(const uint8_t* data, size_t size);

 private:
  uint32_t spineBytes(size_t spine) const;
  uint32_t estimate(size_t spine, uint64_t ratioPages, uint64_t ratioBytes) const;

  std::vector<uint32_t> ends;
  std::vector<uint16_t> pages;
  std::vector<uint32_t> lastOffsets;
  uint32_t key = 0;
};
