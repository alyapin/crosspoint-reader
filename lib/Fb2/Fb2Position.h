#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

// Carries a reading position across an FB2 reconversion. A newer converter lays the
// book out in other files, so a saved spine index and page no longer point at the
// same text; the fraction of the book still does, to within a page or so.
namespace Fb2Position {

// A position as the reader's progress.bin holds it.
struct Saved {
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t pageCount = 0;  // 0: unknown
};

// progress.bin: spine and page, then optionally the chapter's page count and a
// text offset, little-endian; 4, 6 or 10 bytes.
bool parseProgress(const uint8_t* data, size_t size, Saved& out);

// The fraction of the book at `pos`, as Epub::calculateProgress computes it.
// `cumulative(i)` is the size of spine items 0..i.
template <typename Cumulative>
float bookFraction(const Saved& pos, const int spineCount, Cumulative cumulative) {
  if (spineCount <= 0) return 0.0f;
  const auto total = static_cast<float>(cumulative(spineCount - 1));
  if (total <= 0.0f) return 0.0f;
  if (pos.spine >= spineCount) return 1.0f;
  // UINT16_MAX is the reader's "last page" sentinel, which it ignores too.
  const float chapter = pos.pageCount > 0 && pos.page != UINT16_MAX
                            ? std::min(1.0f, static_cast<float>(pos.page) / static_cast<float>(pos.pageCount))
                            : 0.0f;
  const auto before = static_cast<float>(pos.spine > 0 ? cumulative(pos.spine - 1) : 0);
  const auto size = static_cast<float>(cumulative(pos.spine)) - before;
  return std::clamp((before + chapter * size) / total, 0.0f, 1.0f);
}

// fb2.pos: a book fraction to reopen at and the converter version whose files the
// saved position (and bookmarks) refer to.
size_t formatPending(char* out, size_t size, float fraction, uint8_t version);
bool parsePending(const char* text, size_t len, float& fraction, uint8_t& version);

// With a pending fraction, the exact saved position wins only when it refers to the current conversion.
bool restoreByFraction(uint8_t pendingVersion, bool progressLoaded);
// Bookmarks keep spine-relative fields that a new conversion invalidates.
bool bookmarksMoved(uint8_t pendingVersion);

}  // namespace Fb2Position
