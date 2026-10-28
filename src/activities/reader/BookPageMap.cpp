#include "BookPageMap.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {
// Chapters smaller than this (title pages, a lone heading) are mostly page-break
// overhead and would skew the pages-per-byte ratio.
constexpr uint32_t MIN_RATIO_BYTES = 4096;
// Ratio used before any sizeable chapter has been laid out.
constexpr uint32_t DEFAULT_BYTES_PER_PAGE = 2500;
// Full pages needed before charsPerPage() answers.
constexpr uint64_t MIN_CHARS_PER_PAGE_SAMPLE = 10;

constexpr uint32_t MAGIC = 0x47504B42;  // "BKPG"
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = sizeof(uint32_t) + sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint16_t);
constexpr size_t ENTRY_SIZE = sizeof(uint16_t) + sizeof(uint32_t);

template <typename T>
void put(std::vector<uint8_t>& out, const T value) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
T get(const uint8_t* data) {
  T value;
  std::memcpy(&value, data, sizeof(T));
  return value;
}
}  // namespace

void BookPageMap::reset(std::vector<uint32_t> spineEnds, const uint32_t layoutKey) {
  ends = std::move(spineEnds);
  relayout(layoutKey);
}

void BookPageMap::relayout(const uint32_t layoutKey) {
  pages.assign(ends.size(), UNKNOWN);
  lastOffsets.assign(ends.size(), 0);
  key = layoutKey;
}

bool BookPageMap::setPages(const uint32_t layoutKey, const size_t spine, const uint16_t count,
                           const uint32_t lastPageOffset) {
  if (layoutKey != key || spine >= pages.size() || count == UNKNOWN) return false;
  if (pages[spine] == count && lastOffsets[spine] == lastPageOffset) return false;
  pages[spine] = count;
  lastOffsets[spine] = lastPageOffset;
  return true;
}

uint32_t BookPageMap::charsPerPage() const {
  // A chapter's last page is usually short, so only the full pages before it count.
  uint64_t chars = 0;
  uint64_t fullPages = 0;
  for (size_t i = 0; i < pages.size(); i++) {
    if (pages[i] == UNKNOWN || pages[i] < 2) continue;
    chars += lastOffsets[i];
    fullPages += pages[i] - 1;
  }
  return fullPages >= MIN_CHARS_PER_PAGE_SAMPLE ? static_cast<uint32_t>(chars / fullPages) : 0;
}

uint32_t BookPageMap::spineBytes(const size_t spine) const {
  const uint32_t start = spine == 0 ? 0 : ends[spine - 1];
  return ends[spine] > start ? ends[spine] - start : 0;
}

uint32_t BookPageMap::estimate(const size_t spine, const uint64_t ratioPages, const uint64_t ratioBytes) const {
  const uint64_t bytes = spineBytes(spine);
  if (bytes == 0) return 0;
  const uint64_t rounded = (bytes * ratioPages + ratioBytes / 2) / ratioBytes;
  return static_cast<uint32_t>(std::clamp<uint64_t>(rounded, 1, UNKNOWN - 1));
}

BookPageMap::Position BookPageMap::locate(const size_t spine, const uint32_t pageIndex, const uint32_t livePages,
                                          const bool liveExact) const {
  Position pos;
  if (spine >= pages.size()) return pos;

  uint32_t current;
  if (liveExact) {
    current = livePages;
  } else if (known(spine)) {
    current = pages[spine];
  } else {
    current = std::max(livePages, pageIndex + 1);
  }

  uint64_t ratioPages = 0;
  uint64_t ratioBytes = 0;
  for (size_t i = 0; i < pages.size(); i++) {
    const uint32_t count = i == spine ? current : pages[i];
    if ((i == spine || known(i)) && count > 0 && spineBytes(i) >= MIN_RATIO_BYTES) {
      ratioPages += count;
      ratioBytes += spineBytes(i);
    }
  }
  if (ratioBytes == 0) {
    ratioPages = 1;
    ratioBytes = DEFAULT_BYTES_PER_PAGE;
  }

  uint64_t before = 0;
  uint64_t after = 0;
  bool allKnown = liveExact || known(spine);
  for (size_t i = 0; i < pages.size(); i++) {
    if (i == spine) continue;
    uint32_t count = pages[i];
    if (count == UNKNOWN) {
      count = estimate(i, ratioPages, ratioBytes);
      allKnown = false;
    }
    (i < spine ? before : after) += count;
  }

  pos.page = static_cast<uint32_t>(before + pageIndex + 1);
  pos.total = static_cast<uint32_t>(std::max<uint64_t>(before + current + after, pos.page));
  pos.exact = allKnown;
  return pos;
}

int BookPageMap::nextUnknown(const size_t from) const {
  const size_t n = pages.size();
  for (size_t step = 1; step < n; step++) {
    const size_t i = (from + step) % n;
    if (pages[i] == UNKNOWN) return static_cast<int>(i);
  }
  return -1;
}

std::vector<uint8_t> BookPageMap::encode() const {
  std::vector<uint8_t> out;
  out.reserve(HEADER_SIZE + pages.size() * ENTRY_SIZE);
  put(out, MAGIC);
  put(out, VERSION);
  put(out, key);
  put(out, static_cast<uint16_t>(pages.size()));
  for (size_t i = 0; i < pages.size(); i++) {
    put(out, pages[i]);
    put(out, lastOffsets[i]);
  }
  return out;
}

bool BookPageMap::decode(const uint8_t* data, const size_t size) {
  if (!data || size < HEADER_SIZE) return false;
  if (get<uint32_t>(data) != MAGIC || data[4] != VERSION || get<uint32_t>(data + 5) != key) return false;
  const uint16_t count = get<uint16_t>(data + 9);
  if (count != pages.size() || size != HEADER_SIZE + count * ENTRY_SIZE) return false;
  for (size_t i = 0; i < count; i++) {
    const uint8_t* entry = data + HEADER_SIZE + i * ENTRY_SIZE;
    pages[i] = get<uint16_t>(entry);
    lastOffsets[i] = get<uint32_t>(entry + sizeof(uint16_t));
  }
  return true;
}
