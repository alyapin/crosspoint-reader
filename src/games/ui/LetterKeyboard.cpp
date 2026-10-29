#include "LetterKeyboard.h"

#include <KeyboardGames.h>
#if SOVA_KEYBOARD_GAMES
#include <GfxRenderer.h>

#include "components/setka/SetkaStyle.h"

using namespace setka;

namespace games::ui {

namespace {
constexpr int kLetterCap = 16;
constexpr int kCursorInset = kCellFrameWidth + 2;  // inside a Present key's frame
constexpr int kPlacedCursorInset = 3;

constexpr uint8_t kLetters[kKeyboardRows][kKeyboardColumns] = {
    {11, 24, 21, 12, 6, 15, 4, 26, 27, 9, 23},
    {22, 29, 3, 1, 17, 18, 16, 13, 5, 8, 31},
    {33, 25, 19, 14, 10, 20, 30, 2, 32, 7, 28},
};

// The text face's glyph scaled to a cap height, centred in the key.
void drawLetter(const GfxRenderer& renderer, const uint32_t cp, const int x, const int y, const int width,
                const int height, const bool black) {
  int32_t advance = 0;
  int top = 0;
  int32_t refAdvance = 0;
  int refTop = 0;
  if (!renderer.getCodepointMetrics(kText12Font, cp, kSemiBold, advance, top) ||
      !renderer.getCodepointMetrics(kText12Font, 0x41D, kSemiBold, refAdvance, refTop) || refTop <= 0) {
    return;
  }
  const int scale = kLetterCap * 256 / refTop;
  const int glyphWidth = static_cast<int>(advance * scale / (256 * 16));
  renderer.drawScaledCodepoint(kText12Font, cp, kSemiBold, x + (width - glyphWidth) / 2, y + (height + kLetterCap) / 2,
                               scale, black);
}
}  // namespace

uint8_t keyboardLetter(const int key) {
  if (key < 0 || key >= kKeyboardRows * kKeyboardColumns) return 0;
  return kLetters[key / kKeyboardColumns][key % kKeyboardColumns];
}

uint32_t keyboardCodepoint(const int key) {
  const uint8_t letter = keyboardLetter(key);
  if (letter == 0) return 0;
  if (letter == 7) return 0x401;  // Ё
  return 0x410 + letter - (letter > 7 ? 2 : 1);
}

void drawKeyboard(const GfxRenderer& renderer, const int top, const KeyLookFn look, const void* ctx, const int cursor) {
  const int width = renderer.getScreenWidth();
  drawRule(renderer, 0, top - kRuleWidth, width);
  for (int r = 0; r < kKeyboardRows; ++r) {
    const int y = top + r * kKeyboardKeyHeight;
    // The last row ends on the bar's rule.
    const int height = r < kKeyboardRows - 1 ? kKeyboardKeyHeight - 1 : kKeyboardKeyHeight;
    for (int k = 0; k < kKeyboardColumns; ++k) {
      const int x0 = k * width / kKeyboardColumns;
      const int x1 = (k + 1) * width / kKeyboardColumns;
      const int left = k > 0 ? x0 + 1 : x0;
      if (k > 0) renderer.drawLine(x0, y, x0, y + kKeyboardKeyHeight - 1);
      const int key = r * kKeyboardColumns + k;
      const uint32_t letter = keyboardCodepoint(key);
      const KeyLook shown = look != nullptr ? look(ctx, key) : KeyLook::Plain;
      switch (shown) {
        case KeyLook::Placed:
          renderer.fillRect(left, y, x1 - left, height, true);
          drawLetter(renderer, letter, left, y, x1 - left, height, false);
          break;
        case KeyLook::Present:
          drawCellFrame(renderer, left, y, x1 - left, height);
          drawLetter(renderer, letter, left, y, x1 - left, height, true);
          break;
        case KeyLook::Spent:
          renderer.fillRectDither(left, y, x1 - left, height, Color::LightGray);
          break;
        case KeyLook::Plain:
          drawLetter(renderer, letter, left, y, x1 - left, height, true);
          break;
      }
      if (key == cursor) {
        // White inside a black key, so a black rim stays round it.
        const bool placed = shown == KeyLook::Placed;
        const int inset = shown == KeyLook::Present ? kCursorInset : placed ? kPlacedCursorInset : 0;
        renderer.drawRect(left + inset, y + inset, x1 - left - 2 * inset, height - 2 * inset, kCellFrameWidth, !placed);
      }
    }
    if (r < kKeyboardRows - 1) renderer.drawLine(0, y + kKeyboardKeyHeight - 1, width - 1, y + kKeyboardKeyHeight - 1);
  }
}

int keyboardKeyAt(const GfxRenderer& renderer, const int top, const int x, const int y) {
  const int width = renderer.getScreenWidth();
  if (y < top || y >= top + kKeyboardHeight || x < 0 || x >= width) return -1;
  return (y - top) / kKeyboardKeyHeight * kKeyboardColumns + x * kKeyboardColumns / width;
}

}  // namespace games::ui

#endif  // SOVA_KEYBOARD_GAMES
