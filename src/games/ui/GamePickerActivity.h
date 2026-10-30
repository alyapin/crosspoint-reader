#pragma once

#include "activities/SetkaListActivity.h"

// The new-game picker of the game screens: a Setka list of options. Choosing a
// row finishes with MenuResult{action = row, pageTurnOption = toggle state};
// Back finishes cancelled. An optional toggle row flips an option in place
// (shown as a checkbox, like the settings toggles) instead of finishing; other rows may carry a fixed status or
// show a checkbox they do not flip.
class GamePickerActivity final : public SetkaListActivity {
 public:
  struct Row {
    const char* label;
    const freeink::Icon* icon;
    const char* status = nullptr;  // right-aligned, e.g. the difficulty
    int8_t check = -1;             // a mode's checkbox (SetkaListActivity::rowCheck)
  };
  static constexpr int MAX_ROWS = 10;

  GamePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* title, const Row* rows,
                     int count, int selected, int toggleRow = -1, bool toggleOn = false);

  void onEnter() override;

 private:
  int listCount() const override { return count; }
  const char* headerTitle() const override { return title; }
  const char* rowLabel(int index) const override { return rows[index].label; }
  const freeink::Icon* rowIcon(int index) const override { return rows[index].icon; }
  const char* rowStatus(int index) const override;
  int rowCheck(int index) const override { return index == toggleRow ? static_cast<int>(toggleOn) : rows[index].check; }
  void activateIndex(int index) override;
  void onBackButton() override;

  const char* title;
  Row rows[MAX_ROWS] = {};
  int count;
  int initial;
  int toggleRow;
  bool toggleOn;
};
