#pragma once

#include <cstddef>
#include <cstdint>

// Line breaking for card text: at spaces and after '-' and '/', at the field's
// own line breaks, and inside a word only when the word alone is wider than
// the line.
namespace cards {

inline constexpr int MAX_LINES = 32;

struct Lines {
  struct Span {
    uint16_t start;
    uint16_t len;
  };
  Span line[MAX_LINES];
  uint8_t count = 0;
  bool overflow = false;  // text left past maxLines
};

// Width in pixels of text[0, len).
using Measure = int (*)(void* ctx, const char* text, size_t len);

void wrapText(const char* text, int width, int maxLines, Measure measure, void* ctx, Lines& out);

}  // namespace cards
