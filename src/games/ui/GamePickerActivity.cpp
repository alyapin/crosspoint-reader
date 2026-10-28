#include "GamePickerActivity.h"

#include <I18n.h>

#include <algorithm>

GamePickerActivity::GamePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const char* title,
                                       const Row* source, const int count, const int selected, const int toggleRow,
                                       const bool toggleOn)
    : SetkaListActivity("GamePicker", renderer, mappedInput),
      title(title),
      count(std::clamp(count, 0, MAX_ROWS)),
      initial(selected),
      toggleRow(toggleRow),
      toggleOn(toggleOn) {
  std::copy(source, source + this->count, rows);
}

void GamePickerActivity::onEnter() {
  SetkaListActivity::onEnter();
  if (initial >= 0 && initial < count) moveSelectionTo(initial);
}

const char* GamePickerActivity::rowStatus(const int index) const {
  if (index != toggleRow) return nullptr;
  return toggleOn ? tr(STR_YES) : tr(STR_NO);
}

void GamePickerActivity::activateIndex(const int index) {
  if (index == toggleRow) {
    toggleOn = !toggleOn;
    requestUpdate();
    return;
  }
  app.clearTapFlash();
  MenuResult choice;
  choice.action = index;
  choice.pageTurnOption = toggleOn ? 1 : 0;
  setResult(ActivityResult{choice});
  finish();
}

void GamePickerActivity::onBackButton() {
  app.clearTapFlash();
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}
