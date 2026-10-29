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
// without a label stays empty and keeps its dividers.
struct ActionSlot {
  const freeink::Icon* icon = nullptr;
  const char* label = nullptr;
  bool active = false;
  freeink::ui::ActionId action = 0;
  int16_t value = 0;
};

constexpr int kActionBarHeight = 70;  // the rule included

// Draws the bar from `top` (its rule) to the bottom of the screen and adds a hit per shown slot.
void drawActionBar(const GfxRenderer& renderer, UiAppHost::UiScreen& screen, int top, const ActionSlot* slots,
                   int count, int focused, uint16_t input);

}  // namespace games::ui
