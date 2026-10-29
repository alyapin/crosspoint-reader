#pragma once

#include <array>

#include "activities/SetkaListActivity.h"

// "Ещё" (X4 Pro home tab): the tools that are not reading. Each row opens the
// existing activity; Plugins shows only while the plugin system is enabled.
class MoreActivity final : public SetkaListActivity {
 public:
  MoreActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : SetkaListActivity("More", renderer, mappedInput) {}
  void onEnter() override;

 private:
  enum class Entry : uint8_t { Games, Cards, ReceiveBooks, FileTransfer, Catalogs, Plugins, SdFiles };

  int listCount() const override { return entryCount; }
  const char* headerTitle() const override;
  const char* rowLabel(int index) const override;
  const freeink::Icon* rowIcon(int index) const override;
  const char* rowStatus(int index) const override;
  void activateIndex(int index) override;
  void onChildClosed() override;

  std::array<Entry, 7> entries{};
  char cardsStatus[32] = {};  // read on entry and after a child closes, not per repaint
  int entryCount = 0;
};
