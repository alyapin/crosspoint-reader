#pragma once
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <ScanwordClueLayout.h>

#include <algorithm>

#include "fontIds.h"

// Tokens of the "Setka" UI style (.claude/skills/ui-style/SKILL.md) for the
// screens this fork adds. Text uses the UI font roles (Unbounded on the X4 Pro,
// Ubuntu elsewhere); the hero digit and Golos faces are compiled into X4 Pro
// builds only, and other boards map them onto the Ubuntu UI faces so shared
// code still builds there.
namespace setka {

constexpr int kText10Font = UI_10_FONT_ID;
constexpr int kText12Font = UI_12_FONT_ID;
#if SOVA_UNBOUNDED_UI
constexpr int kDenseFont = GOLOS_8_FONT_ID;             // narrow face for dense game text (scanword clue cells)
constexpr int kHeroFont = UNBOUNDED_61_FONT_ID;         // ~96 px digits: the screen's one big number
constexpr int kHeroSmallFont = UNBOUNDED_31_FONT_ID;    // ~48 px digits: a secondary number
constexpr int kDigitsFont = UNBOUNDED_19_FONT_ID;       // ~30 px digits: numbers under covers
constexpr int kDigitsSmallFont = UNBOUNDED_14_FONT_ID;  // ~22 px digits
constexpr int kDigitsMiniFont = UNBOUNDED_11_FONT_ID;   // ~17 px digits: the level in the brightness overlay
#else
constexpr int kDenseFont = SMALL_FONT_ID;
constexpr int kHeroFont = UI_12_FONT_ID;
constexpr int kHeroSmallFont = UI_12_FONT_ID;
constexpr int kDigitsFont = UI_12_FONT_ID;
constexpr int kDigitsSmallFont = UI_10_FONT_ID;
constexpr int kDigitsMiniFont = UI_10_FONT_ID;
#endif
constexpr int kCaptionFont = SMALL_FONT_ID;
// The X4 Pro text faces carry SemiBold in the family's bold slot.
constexpr EpdFontFamily::Style kSemiBold = EpdFontFamily::BOLD;

constexpr int kSelectionBarWidth = 10;  // left bar on the selected list row or tab
constexpr int kCellFrameWidth = 4;      // inset frame on the selected grid cell
constexpr int kRuleWidth = 2;           // black rule between sections
constexpr int kSidePadding = 20;
// The selection bar sits in the side padding, clear of the X4 Pro bezel; list
// text starts past it, so a label does not shift when the selection moves.
constexpr int kSelectionBarX = kSidePadding - kSelectionBarWidth;
constexpr int kRowTextX = kSidePadding + 12;
constexpr int kProgressHeight = 12;
constexpr int kMinTouch = 44;
// Row pitch, no gap between rows: a navigation row (icon + label) and a
// label + value row (settings).
constexpr int kNavRowHeight = 64;
constexpr int kListRowHeight = 56;
// Game boards: "‹ Выйти" and the game status under the status line, then the board.
constexpr int kGameTopLineY = 40;
constexpr int kGameBoardTop = 84;

// Long-lived screens: HALF on entry, FAST for in-screen updates, and one HALF
// after every kFastUpdatesPerHalf FAST updates to clear ghosting.
constexpr HalDisplay::RefreshMode kEntryRefresh = HalDisplay::HALF_REFRESH;
constexpr HalDisplay::RefreshMode kUpdateRefresh = HalDisplay::FAST_REFRESH;
constexpr int kFastUpdatesPerHalf = 20;

class RefreshCadence {
 public:
  HalDisplay::RefreshMode entry() {
    fastSinceHalf_ = 0;
    return kEntryRefresh;
  }
  HalDisplay::RefreshMode update() {
    if (fastSinceHalf_ >= kFastUpdatesPerHalf) return entry();
    ++fastSinceHalf_;
    return kUpdateRefresh;
  }

 private:
  int fastSinceHalf_ = 0;
};

inline void drawSelectionBar(const GfxRenderer& renderer, const int x, const int y, const int height) {
  renderer.fillRect(x, y, kSelectionBarWidth, height, true);
}

inline void drawCellFrame(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  renderer.drawRect(x, y, width, height, kCellFrameWidth, true);
}

inline void drawRule(const GfxRenderer& renderer, const int x, const int y, const int width) {
  renderer.fillRect(x, y, width, kRuleWidth, true);
}

// The clock and battery line exactly as the Lyra header draws it (Light
// caption, battery glyph after the percent, same corners), for screens that
// draw their own top. Returns the y just below it.
int drawStatusLine(const GfxRenderer& renderer);

// A cover placeholder's title in caption lines: whole words, and a word wider
// than the line hyphenated by the scanword clue rules; an ellipsis past maxLines.
void captionLines(const GfxRenderer& renderer, const char* title, int width, uint8_t maxLines,
                  scanword::ClueLines& out);

// Black fill on a LightGray-dithered track inside a 1 px black outline.
inline void drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width, int percent) {
  percent = std::clamp(percent, 0, 100);
  const int inner = width - 2;
  const int filled = inner * percent / 100;
  renderer.drawRect(x, y, width, kProgressHeight, true);
  renderer.fillRect(x + 1, y + 1, filled, kProgressHeight - 2, true);
  renderer.fillRectDither(x + 1 + filled, y + 1, inner - filled, kProgressHeight - 2, Color::LightGray);
}

// The same bar standing upright: it fills from the bottom.
inline void drawProgressBarVertical(const GfxRenderer& renderer, const int x, const int y, const int width,
                                    const int height, int percent) {
  percent = std::clamp(percent, 0, 100);
  const int inner = height - 2;
  const int filled = inner * percent / 100;
  renderer.drawRect(x, y, width, height, true);
  renderer.fillRect(x + 1, y + 1 + inner - filled, width - 2, filled, true);
  renderer.fillRectDither(x + 1, y + 1, width - 2, inner - filled, Color::LightGray);
}

// Height of the hero digits above the baseline. Unbounded's line box carries
// a tall empty ascender, so lay hero numbers out by this, not getLineHeight().
inline int heroCapHeight(const GfxRenderer& renderer, const int fontId) {
  int32_t advance = 0;
  int top = 0;
  return renderer.getCodepointMetrics(fontId, '1', EpdFontFamily::REGULAR, advance, top)
             ? top
             : renderer.getFontAscenderSize(fontId);
}

// Draws a hero number with the top of its digits at capTopY; returns its width.
inline int drawHeroNumber(const GfxRenderer& renderer, const int fontId, const int x, const int capTopY,
                          const char* text) {
  renderer.drawText(fontId, x, capTopY + heroCapHeight(renderer, fontId) - renderer.getFontAscenderSize(fontId), text);
  return renderer.getTextWidth(fontId, text);
}

}  // namespace setka
