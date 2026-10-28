#pragma once

#include "activities/SetkaListActivity.h"

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
  const char* emptyText() const override;
  void activateIndex(int index) override;
};
