#include "CardLayout.h"

#include <algorithm>
#include <cstring>

namespace cards {

namespace {
size_t nextChar(const char* text, size_t i, const size_t n) {
  i++;
  while (i < n && (static_cast<uint8_t>(text[i]) & 0xC0) == 0x80) i++;
  return i;
}
}  // namespace

void wrapText(const char* text, const int width, int maxLines, const Measure measure, void* ctx, Lines& out) {
  out.count = 0;
  out.overflow = false;
  maxLines = std::min(maxLines, MAX_LINES);
  const size_t n = strlen(text);
  size_t pos = 0;
  while (pos < n) {
    if (out.count >= maxLines) {
      out.overflow = true;
      return;
    }
    const size_t start = pos;
    size_t breakEnd = 0;
    size_t breakNext = 0;
    size_t i = start;
    size_t end = n;
    size_t next = n;
    while (i < n && text[i] != '\n') {
      const size_t j = nextChar(text, i, n);
      if (measure(ctx, text + start, j - start) > width) {
        if (breakEnd > start) {
          end = breakEnd;
          next = breakNext;
        } else {
          end = i > start ? i : j;
          next = end;
        }
        break;
      }
      if (text[i] == ' ') {
        breakEnd = i;
        breakNext = j;
      } else if ((text[i] == '-' || text[i] == '/') && i > start) {
        breakEnd = j;
        breakNext = j;
      }
      i = j;
    }
    if (i >= n || text[i] == '\n') {
      end = i;
      next = i < n ? i + 1 : n;
    }
    while (end > start && text[end - 1] == ' ') end--;
    while (next < n && text[next] == ' ') next++;
    out.line[out.count++] = {static_cast<uint16_t>(start), static_cast<uint16_t>(end - start)};
    pos = next;
  }
}

}  // namespace cards
