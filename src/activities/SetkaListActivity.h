#pragma once

#include <atomic>
#include <memory>

#include "activities/UiListActivity.h"
#include "components/setka/SetkaStyle.h"

// A short list screen in the Setka style: "‹ Назад" and the title at the top,
// a rule, then text rows; the focused row carries the left selection bar. Rows
// are drawn directly and registered as ACTION_ROW hits, so the UiListActivity
// loop (touch, buttons, back gesture) drives the screen unchanged. Lists are
// expected to fit one screen.
class SetkaListActivity : public UiListActivity {
 public:
  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  SetkaListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  virtual const char* rowLabel(int index) const = 0;
  // Shown under the rule when the list is empty.
  virtual const char* emptyText() const { return nullptr; }
  // Opens a screen on top of this one; coming back repaints with the entry refresh.
  void openChild(std::unique_ptr<Activity> child);

 private:
  static constexpr freeink::ui::ActionId ACTION_BACK = ACTION_USER;

  static void onBack(const freeink::ui::ActionEvent& event, void* user);
  bool handleCustomInput() override;
  void buildScreen(UiScreen& screen) override;
  void drawChrome() override {}
  void drawFooter() override {}

  setka::RefreshCadence cadence;  // render task only
  std::atomic<bool> entryRefresh{true};
};
