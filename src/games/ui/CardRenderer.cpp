#include "CardRenderer.h"

#include <algorithm>

#include "GameScreen.h"
#include "components/setka/SetkaStyle.h"

namespace games::ui {
namespace {

enum Suit { SPADES, HEARTS, DIAMONDS, CLUBS };
constexpr int kCorner = 4;
constexpr int kOutline = 2;
constexpr const char* kRanks[] = {"A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

void fillCircle(const GfxRenderer& renderer, const int cx, const int cy, const int r, const bool black) {
  if (r > 0) renderer.fillRoundedRect(cx - r, cy - r, 2 * r, 2 * r, r, black ? Color::Black : Color::White);
}

// The suit shape shrunk by `in` px, in black or white: an outline is the shape with its white inside.
void fillSuit(const GfxRenderer& renderer, const int x, const int y, const int size, const int suit, const int in,
              const bool black) {
  const int cx = x + size / 2;
  const int r = size / 4;
  const int k = in + in / 2;  // a slanted edge moves in further along the axis
  switch (suit) {
    case DIAMONDS:
      GameScreen::fillTriangle(renderer, cx, y + k, x + size - 2 - k, y + size / 2, x + 2 + k, y + size / 2, black);
      GameScreen::fillTriangle(renderer, cx, y + size - k, x + size - 2 - k, y + size / 2, x + 2 + k, y + size / 2,
                               black);
      break;
    case HEARTS:
      fillCircle(renderer, cx - r, y + r + 1, r + 1 - in, black);
      fillCircle(renderer, cx + r, y + r + 1, r + 1 - in, black);
      GameScreen::fillTriangle(renderer, x + k, y + r + 2, x + size - k, y + r + 2, cx, y + size - k, black);
      break;
    case SPADES:
      GameScreen::fillTriangle(renderer, cx, y, x + size, y + size * 3 / 5, x, y + size * 3 / 5);
      fillCircle(renderer, cx - r, y + size * 3 / 5, r + 1, true);
      fillCircle(renderer, cx + r, y + size * 3 / 5, r + 1, true);
      GameScreen::fillTriangle(renderer, cx, y + size / 2, cx + r, y + size, cx - r, y + size);
      break;
    case CLUBS:
      fillCircle(renderer, cx, y + r + 1, r + 1, true);
      fillCircle(renderer, cx - r - 1, y + size / 2 + 1, r + 1, true);
      fillCircle(renderer, cx + r + 1, y + size / 2 + 1, r + 1, true);
      GameScreen::fillTriangle(renderer, cx, y + size / 3, cx + r, y + size, cx - r, y + size);
      break;
    default:
      break;
  }
}

}  // namespace

void drawSuit(const GfxRenderer& renderer, const int x, const int y, const int size, const int suit) {
  fillSuit(renderer, x, y, size, suit, 0, true);
  if (suit == HEARTS || suit == DIAMONDS) fillSuit(renderer, x, y, size, suit, kOutline, false);
}

void drawCard(const GfxRenderer& renderer, const CardStyle& style, const int x, const int y, const int rank,
              const int suit, const bool faceUp) {
  renderer.fillRect(x + 1, y + 1, style.width - 2, style.height - 2, false);
  if (!faceUp) {
    renderer.fillRectDither(x + 3, y + 3, style.width - 6, style.height - 6, Color::LightGray);
  } else {
    const char* label = kRanks[rank];
    renderer.drawText(style.font, x + style.inset, y + 2, label, true, setka::kSemiBold);
    const int rankWidth = renderer.getTextWidth(style.font, label, setka::kSemiBold);
    const int lineHeight = renderer.getLineHeight(style.font);
    drawSuit(renderer, x + style.inset + 2 + rankWidth, y + 2 + (lineHeight - style.suitSize) / 2, style.suitSize,
             suit);
  }
  renderer.drawRoundedRect(x, y, style.width, style.height, 1, kCorner, true);
}

void drawDashedRect(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  constexpr int kDash = 6;
  constexpr int kGap = 4;
  constexpr int kStroke = 2;
  for (int d = 0; d < width; d += kDash + kGap) {
    const int n = std::min(kDash, width - d);
    renderer.fillRect(x + d, y, n, kStroke, true);
    renderer.fillRect(x + d, y + height - kStroke, n, kStroke, true);
  }
  for (int d = 0; d < height; d += kDash + kGap) {
    const int n = std::min(kDash, height - d);
    renderer.fillRect(x, y + d, kStroke, n, true);
    renderer.fillRect(x + width - kStroke, y + d, kStroke, n, true);
  }
}

}  // namespace games::ui
