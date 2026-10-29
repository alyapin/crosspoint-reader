#pragma once

#include <Bitmap.h>
#include <FreeInkUICore.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Icon.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"

class MappedInputManager;

// Shared pieces of the statistics, library-status and shelf screens (Setka
// style, .claude/skills/ui-style): stock header, layout bands, plural forms,
// cover thumbs and the menu box.
namespace statsui {

using setka::kRowTextX;
using setka::kSelectionBarX;
constexpr int kIconSize = 24;
constexpr int kIconGap = 16;
constexpr int kMenuRowHeight = 56;
constexpr unsigned long kLongPressMs = 1000;

struct Bands {
  int width = 0;
  int contentTop = 0;     // below the header
  int contentBottom = 0;  // above the button hints
};

// Draws the stock header (back arrow on touch boards when `back`, title, rule)
// and returns the free band under it.
Bands drawHeader(const GfxRenderer& renderer, const char* title, bool back);
// Back/Select/Up/Down hints on button boards (Up/Down only when `navigates`); nothing on touch boards.
void drawHints(GfxRenderer& renderer, const MappedInputManager& input, const char* confirmLabel, bool navigates = true);

// Russian-style plural choice for the active language (one/few/many); other
// languages use one for 1 and many otherwise.
StrId plural(unsigned count, StrId one, StrId few, StrId many);
// "ещё 9 мин" / "ещё 1 ч 20 мин", as the reader footer writes it.
void timeLeftText(uint32_t seconds, char* out, size_t size);

// Cover thumb file for a book at a drawing height; empty for unsupported files.
std::string thumbPath(const std::string& bookPath, int height);
// Generates that thumb from the book's cover; false when the book has none.
bool generateThumb(const std::string& bookPath, int height);

// Draws a book cover thumb, or a dithered card with the title on a white label.
class CoverPainter {
 public:
  void draw(const GfxRenderer& renderer, const std::string& thumb, const char* title, int x, int y, int width,
            int height);

 private:
  HalFile file;
  Bitmap bitmap{file};
};

// A list row in the Setka shape: 24 px icon, label (SemiBold when focused),
// optional right-aligned status, and the left selection bar when focused.
void drawIconRow(const GfxRenderer& renderer, freeink::ui::DrawTarget& target, const freeink::Icon* icon,
                 const char* label, const char* status, int x, int y, int width, int height, bool focused);

// Long-press menu: a framed box above `bottom` with a title (up to two lines)
// and icon rows, on a cleared band. A tap outside fires dismissAction; a row
// fires itemAction with its index. rowPitch: the height of the list rows that
// start at areaTop, so the band starts on a row boundary with the box right
// under it (0: no rows).
// anchor: the box sits right under the long-pressed item's row instead, or
// right over it when there is no room below.
using MenuEntryFn = void (*)(int index, const freeink::Icon*& icon, const char*& label, void* ctx);
struct MenuAnchor {
  int row = 0;         // the item's row, 0 = the one at gridTop
  int gridTop = 0;     // the first row's top
  int pitch = 0;       // row pitch; its last `gap` px are blank
  int gap = 0;
  int contentEnd = 0;  // nothing is drawn below this
};
void drawMenuBox(const GfxRenderer& renderer, UiAppHost::UiScreen& screen, const char* title, int count, int focus,
                 bool showFocus, MenuEntryFn entry, void* ctx, int areaTop, int bottom, int width,
                 freeink::ui::ActionId itemAction, freeink::ui::ActionId dismissAction, int rowPitch = 0,
                 const MenuAnchor* anchor = nullptr);

// Dashed outline, for the add-a-book tile.
void drawDashedRect(const GfxRenderer& renderer, int x, int y, int width, int height);

}  // namespace statsui
