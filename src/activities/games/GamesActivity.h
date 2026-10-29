#pragma once

#include "activities/SetkaListActivity.h"

// The keyboard games (Сканворды, Виселица, 5 букв, Кейворды) take too many key
// presses on the button-only X4 and X3, so their builds leave them out.
#define SOVA_KEYBOARD_GAMES (!(FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3))

// "Игры" under "Ещё": one row per entry of the game registry in
// GamesActivity.cpp; an empty registry shows the empty-state line.
class GamesActivity final : public SetkaListActivity {
 public:
  GamesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : SetkaListActivity("Games", renderer, mappedInput) {}

 private:
  int listCount() const override;
  const char* headerTitle() const override;
  const char* rowLabel(int index) const override;
  const freeink::Icon* rowIcon(int index) const override;
  const char* rowStatus(int index) const override;
  const char* emptyText() const override;
  void activateIndex(int index) override;
};
