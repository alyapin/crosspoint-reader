#pragma once

#include "activities/SetkaListActivity.h"

// The new-game picker of the game screens: a Setka list of options. Choosing a
// row finishes with MenuResult{action = row, pageTurnOption = toggle state};
// Back finishes cancelled. An optional toggle row flips a yes/no option in
// place (shown as the row status) instead of finishing; other rows may carry a fixed status.
class GamePickerActivity final : public SetkaListActivity {
 public:
  struct Row {
    const char* label;
    const freeink::Icon* icon;
    const char* status = nullptr;
  };
  static constexpr int MAX_ROWS = 8;

  GamePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* title, const Row* rows,
                     int count, int selected, int toggleRow = -1, bool toggleOn = false);

  void onEnter() override;

 private:
  int listCount() const override { return count; }
  const char* headerTitle() const override { return title; }
  const char* rowLabel(int index) const override { return rows[index].label; }
  const freeink::Icon* rowIcon(int index) const override { return rows[index].icon; }
  const char* rowStatus(int index) const override;
  void activateIndex(int index) override;
  void onBackButton() override;

  const char* title;
  Row rows[MAX_ROWS] = {};
  int count;
  int initial;
  int toggleRow;
  bool toggleOn;
};
