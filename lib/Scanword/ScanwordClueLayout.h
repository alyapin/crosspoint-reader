#pragma once

// Fits a clue into a grid cell: word wrap to a pixel width, Russian hyphenation
// for words wider than a line, and an ellipsis when the text still does not fit.

#include <cstddef>
#include <cstdint>

namespace scanword {

// Pixel width of a NUL-terminated UTF-8 string.
using MeasureFn = int (*)(void* ctx, const char* text);

constexpr uint8_t CLUE_MAX_LINES = 3;
constexpr size_t CLUE_LINE_BYTES = 48;

struct ClueLines {
  uint8_t count = 0;
  char line[CLUE_MAX_LINES][CLUE_LINE_BYTES] = {};
};

// A word breaks only where both parts have two letters and a vowel, and the
// second part starts on a consonant other than Ь, Ъ or Й.
void layoutClue(const char* text, int maxWidth, uint8_t maxLines, MeasureFn measure, void* ctx, ClueLines& out);

}  // namespace scanword
