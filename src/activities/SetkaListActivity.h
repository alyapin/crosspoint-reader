#pragma once

#include <Icon.h>

#include <atomic>
#include <memory>

#include "activities/UiListActivity.h"
#include "components/setka/SetkaStyle.h"

// A short menu in the Setka style (skill variant Б): the stock header Settings
// draws (back arrow + title + rule), then rows of a 24 px Lucide icon and a
// label with an optional status on the right; the focused row carries the
// left selection bar and a SemiBold label. Rows are drawn directly and
// registered as ACTION_ROW hits, so the UiListActivity loop (touch, buttons,
// back gesture, header back tap) drives the screen unchanged. Lists are
// expected to fit one screen.
class SetkaListActivity : public UiListActivity {
 public:
  void onEnter() override;
  void render(RenderLock&&) override;

 protected:
  SetkaListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);

  virtual const char* rowLabel(int index) const = 0;
  virtual const freeink::Icon* rowIcon(int index) const { return nullptr; }
  // Right-aligned status text, e.g. an unfinished game; nullptr or empty for none.
  virtual const char* rowStatus(int index) const { return nullptr; }
  // Shown under the header when the list is empty.
  virtual const char* emptyText() const { return nullptr; }
  // Opens a screen on top of this one; coming back repaints with the entry refresh.
  void openChild(std::unique_ptr<Activity> child);
  // A screen opened with openChild() closed.
  virtual void onChildClosed() {}

 private:
  bool handleCustomInput() override;
  void buildScreen(UiScreen& screen) override;

  setka::RefreshCadence cadence;  // render task only
  std::atomic<bool> entryRefresh{true};
};
