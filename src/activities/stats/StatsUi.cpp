#include "StatsUi.h"

#include <Epub.h>
#include <FreeInkUIIcon.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Xtc.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "activities/reader/ReadingPace.h"
#include "components/UITheme.h"

namespace statsui {

Bands drawHeader(const GfxRenderer& renderer, const char* title, const bool back) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  Bands bands;
  bands.width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, bands.width, metrics.headerHeight}, title, nullptr, back);
  int insetTop = 0;
  int insetRight = 0;
  int insetBottom = 0;
  int insetLeft = 0;
  renderer.getOrientedViewableTRBL(&insetTop, &insetRight, &insetBottom, &insetLeft);
  bands.contentTop = metrics.topPadding + metrics.headerHeight;
  bands.contentBottom = renderer.getScreenHeight() - insetBottom - metrics.buttonHintsHeight;
  return bands;
}

void drawHints(GfxRenderer& renderer, const MappedInputManager& input, const char* confirmLabel, const bool navigates) {
  const auto labels =
      input.mapLabels(tr(STR_BACK), confirmLabel, navigates ? tr(STR_DIR_UP) : "", navigates ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

StrId plural(const unsigned count, const StrId one, const StrId few, const StrId many) {
  const Language language = I18N.getLanguage();
  if (language != Language::RU && language != Language::UK && language != Language::BE) {
    return count == 1 ? one : many;
  }
  const unsigned mod10 = count % 10;
  const unsigned mod100 = count % 100;
  if (mod10 == 1 && mod100 != 11) return one;
  if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return few;
  return many;
}

void timeLeftText(const uint32_t seconds, char* out, const size_t size) {
  formatTimeLeft(seconds, tr(STR_TIME_LEFT_MIN), tr(STR_TIME_LEFT_HOURS), tr(STR_TIME_LEFT_HOURS_MIN), out, size);
}

std::string thumbPath(const std::string& bookPath, const int height) {
  // Constructors only derive cache paths; the objects exceed the stack budget.
  std::string base;
  if (FsHelpers::hasReflowableBookExtension(bookPath)) {
    if (auto epub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint")) base = epub->getThumbBmpPath();
  } else if (FsHelpers::hasXtcExtension(bookPath)) {
    if (auto xtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint")) base = xtc->getThumbBmpPath();
  }
  return base.empty() ? base : UITheme::getCoverThumbPath(base, height);
}

bool generateThumb(const std::string& bookPath, const int height) {
  if (FsHelpers::hasReflowableBookExtension(bookPath)) {
    auto epub = makeUniqueNoThrow<Epub>(bookPath, "/.crosspoint");
    if (!epub) {
      LOG_ERR("STAT", "OOM: cover EPUB");
      return false;
    }
    return epub->generateThumbBmpFromSource(height);
  }
  if (FsHelpers::hasXtcExtension(bookPath)) {
    auto xtc = makeUniqueNoThrow<Xtc>(bookPath, "/.crosspoint");
    if (!xtc) {
      LOG_ERR("STAT", "OOM: cover XTC");
      return false;
    }
    return xtc->load() && xtc->generateThumbBmp(height);
  }
  return false;
}

void CoverPainter::draw(const GfxRenderer& renderer, const std::string& thumb, const char* title, const int x,
                        const int y, const int width, const int height) {
  bool drawn = false;
  if (!thumb.empty() && Storage.exists(thumb.c_str()) && Storage.openFileForRead("STAT", thumb, file)) {
    if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
      drawn = GUI.drawCoverThumbFill(renderer, bitmap, Rect{x, y, width, height}, 0);
    }
    file.close();
  }
  if (!drawn) {
    // Text never sits on a dither: the title goes on a white label.
    constexpr int kPad = 4;
    const int lineHeight = renderer.getLineHeight(setka::kCaptionFont);
    renderer.fillRectDither(x, y, width, height, Color::LightGray);
    // List-size covers stay plain: the title is printed beside them.
    constexpr int kMinLabelWidth = 100;
    const int maxLines = std::max(0, (height - 4 * kPad) / lineHeight);
    if (title && *title && maxLines > 0 && width >= kMinLabelWidth) {
      scanword::ClueLines lines;
      setka::captionLines(renderer, title, width - 4 * kPad, static_cast<uint8_t>(std::min(maxLines, 4)), lines);
      if (lines.count > 0) {
        renderer.fillRect(x + kPad, y + kPad, width - 2 * kPad, lines.count * lineHeight + 2 * kPad, false);
        int textTop = y + 2 * kPad;
        for (int i = 0; i < lines.count; ++i) {
          renderer.drawText(setka::kCaptionFont, x + 2 * kPad, textTop, lines.line[i]);
          textTop += lineHeight;
        }
      }
    }
  }
  renderer.drawRect(x, y, width, height);
}

void drawIconRow(const GfxRenderer& renderer, freeink::ui::DrawTarget& target, const freeink::Icon* icon,
                 const char* label, const char* status, const int x, const int y, const int width, const int height,
                 const bool focused) {
  namespace fui = freeink::ui;
  using namespace setka;
  if (focused) drawSelectionBar(renderer, x - (kRowTextX - kSelectionBarX), y, height);
  int textX = x;
  if (icon) {
    target.bitmap(fui::makeRect(x, y + (height - kIconSize) / 2, kIconSize, kIconSize), fui::bitmapFromIcon(*icon),
                  fui::BitmapMode::Center);
    textX += kIconSize + kIconGap;
  }
  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int textTop = y + (height - lineHeight) / 2;
  int labelWidth = x + width - textX;
  if (status && *status) {
    const int statusWidth = renderer.getTextWidth(kText10Font, status);
    // Shares the label's baseline.
    const int statusTop =
        textTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font);
    renderer.drawText(kText10Font, x + width - statusWidth, statusTop, status);
    labelWidth -= statusWidth + kIconGap;
  }
  const auto style = focused ? kSemiBold : EpdFontFamily::REGULAR;
  renderer.drawText(kText12Font, textX, textTop, renderer.truncatedText(kText12Font, label, labelWidth, style).c_str(),
                    true, style);
}

void drawMenuBox(const GfxRenderer& renderer, UiAppHost::UiScreen& screen, const char* title, const int count,
                 const int focus, const bool showFocus, const MenuEntryFn entry, void* ctx, const int areaTop,
                 const int bottom, const int width, const freeink::ui::ActionId itemAction,
                 const freeink::ui::ActionId dismissAction, const int rowPitch, const MenuAnchor* anchor) {
  namespace fui = freeink::ui;
  using namespace setka;
  constexpr int kPad = 16;
  constexpr int kBandMargin = 20;  // cleared above the box, as around OptionPopup
  const int line12 = renderer.getLineHeight(kText12Font);
  const int boxX = kSidePadding;
  const int boxWidth = width - 2 * kSidePadding;
  const auto titleLines = renderer.wrappedText(kText12Font, title ? title : "", boxWidth - 2 * kPad, 2, kSemiBold);
  const int headHeight = kPad + static_cast<int>(titleLines.size()) * line12 + kPad / 2;
  const int fit = std::max(1, (bottom - areaTop - 2 * kPad - headHeight) / kMenuRowHeight);
  const int shown = std::min(count, fit);
  const int first = std::clamp(focus - shown + 1, 0, std::max(0, count - shown));
  const int boxHeight = headHeight + shown * kMenuRowHeight + kPad / 2;
  int boxY = bottom - kPad - boxHeight;

  // Registered first: the rows registered after it win inside the box.
  screen.frame().hit(fui::makeRect(0, 0, width, renderer.getScreenHeight()), dismissAction, 0,
                     fui::InputTouch | fui::InputLongPress);
  // The screen under the box is cleared full width down to `bottom`, from a
  // row boundary when the caller lists rows, so no row shows cut by the frame.
  int bandTop = std::max(areaTop, boxY - kBandMargin);
  int bandBottom = bottom;
  bool anchored = false;
  if (anchor && anchor->pitch > 0) {
    // Band edges sit mid-gap between rows (or past the content), the box centred between them.
    const int need = boxHeight + 2 * kBandMargin;
    const int firstEdge = anchor->gridTop - anchor->gap / 2;  // edges: firstEdge + k * pitch
    const int under = firstEdge + (anchor->row + 1) * anchor->pitch;
    int end = under + need;
    if (end < anchor->contentEnd) {
      const int nextEdge = firstEdge + (end - firstEdge + anchor->pitch - 1) / anchor->pitch * anchor->pitch;
      end = std::min(nextEdge, anchor->contentEnd);
    }
    const int over = firstEdge + anchor->row * anchor->pitch;
    const int start = over - need;
    if (end <= bottom) {
      bandTop = under;
      bandBottom = end;
      anchored = true;
    } else if (start >= areaTop) {
      bandTop = start < firstEdge ? areaTop : firstEdge + (start - firstEdge) / anchor->pitch * anchor->pitch;
      bandBottom = over;
      anchored = true;
    }
    if (anchored) boxY = bandTop + (bandBottom - bandTop - boxHeight) / 2;
  }
  if (!anchored && rowPitch > 0) {
    bandTop = areaTop + (bandTop - areaTop) / rowPitch * rowPitch;
    // The box moves up under the band's edge, so no blank row sits above it.
    boxY = std::min(boxY, bandTop + kBandMargin);
  }
  renderer.fillRect(0, bandTop, width, bandBottom - bandTop, false);
  if (bandTop > areaTop) drawRule(renderer, 0, bandTop - kRuleWidth, width);
  if (bandBottom < bottom) drawRule(renderer, 0, bandBottom, width);
  renderer.drawRect(boxX, boxY, boxWidth, boxHeight, kRuleWidth, true);
  int titleTop = boxY + kPad;
  for (const auto& line : titleLines) {
    renderer.drawText(kText12Font, boxX + kPad, titleTop, line.c_str(), true, kSemiBold);
    titleTop += line12;
  }
  const int rowX = boxX + kPad + (kRowTextX - kSelectionBarX);
  const int rowWidth = boxX + boxWidth - kPad - rowX;
  for (int i = first; i < first + shown; ++i) {
    const freeink::Icon* icon = nullptr;
    const char* label = "";
    entry(i, icon, label, ctx);
    const int rowTop = boxY + headHeight + (i - first) * kMenuRowHeight;
    drawIconRow(renderer, screen.target(), icon, label, nullptr, rowX, rowTop, rowWidth, kMenuRowHeight,
                showFocus && i == focus);
    screen.frame().hit(fui::makeRect(boxX, rowTop, boxWidth, kMenuRowHeight), itemAction, static_cast<int16_t>(i),
                       fui::InputTouch | fui::InputLongPress);
  }
}

void drawDashedRect(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  constexpr int kDash = 8;
  for (int i = 0; i < width; i += 2 * kDash) {
    const int w = std::min(kDash, width - i);
    renderer.fillRect(x + i, y, w, 2, true);
    renderer.fillRect(x + i, y + height - 2, w, 2, true);
  }
  for (int i = 0; i < height; i += 2 * kDash) {
    const int h = std::min(kDash, height - i);
    renderer.fillRect(x, y + i, 2, h, true);
    renderer.fillRect(x + width - 2, y + i, 2, h, true);
  }
}

}  // namespace statsui
