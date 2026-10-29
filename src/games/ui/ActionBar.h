#pragma once

#include <cstdint>

#include "components/UiAppHost.h"

namespace freeink {
struct Icon;
}

namespace games::ui {

// The action bar at the bottom of every game screen (ui-style Setka): a 2 px
// rule, then equal slots split by 1 px lines, each a 24 px icon over a Light
// caption. A mode that is on gets the 4 px inset frame of a selected cell; key
// focus gets the 10 px bar left of the icon, as on the Home tabs. A slot
// without a label stays empty and keeps its dividers; a bar with one action
// (Меню) has none.
struct ActionSlot {
  const freeink::Icon* icon = nullptr;
  const char* label = nullptr;
  bool active = false;
  freeink::ui::ActionId action = 0;
  int16_t value = 0;
};

constexpr int kActionBarHeight = 70;  // the rule included

// Draws the bar from `top` (its rule) down to the button hints (the screen bottom on touch boards)
// and adds a hit per shown slot.
void drawActionBar(const GfxRenderer& renderer, UiAppHost::UiScreen& screen, int top, const ActionSlot* slots,
                   int count, int focused, uint16_t input);

// One part of a game's status at the right of the top line: a word label in Regular, then its number
// (a count, "1 из 6", "<1 мин") in SemiBold; either may be nullptr. Parts are joined by " · ".
struct StatusItem {
  const char* label = nullptr;
  const char* value = nullptr;
};
int statusWidth(const GfxRenderer& renderer, const StatusItem* items, int count);
// Draws the status ending at `right` on the top line at `y`.
void drawStatus(const GfxRenderer& renderer, int right, int y, const StatusItem* items, int count);

// A finished game's result under the board at the left margin: the result in
// UI 11 SemiBold, then an optional detail and, on button boards, the Confirm
// cue in UI 9. Returns the y below the last line.
int drawResult(const GfxRenderer& renderer, int top, const char* result, const char* detail, const char* cue);
int resultHeight(const GfxRenderer& renderer, bool detail, bool cue);

}  // namespace games::ui
