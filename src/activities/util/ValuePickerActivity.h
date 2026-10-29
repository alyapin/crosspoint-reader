#pragma once

#include <I18n.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "components/setka/SetkaStyle.h"

// Full-screen choice of one value for an enumerated setting, in the Setka
// style: header "← {setting}", 56 px rows of a circle / circle-dot icon and
// the value; the bar and SemiBold follow the focus, which starts on the
// current value. A tap (or Confirm) picks the row and returns. Lists longer
// than a screen page by swipes and by the buttons.
class ValuePickerActivity final : public UiListActivity {
 public:
  ValuePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                      std::vector<std::string> labels, int current);

  // Opens the picker over host; onSelect runs in the host after a value is picked.
  static void open(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                   std::vector<std::string> labels, int current, std::function<void(int)> onSelect);
  // As above, for a translated title and option list.
  static void open(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput, StrId title,
                   const StrId* options, int count, int current, std::function<void(int)> onSelect);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  int listCount() const override { return static_cast<int>(labels.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void navigateButtons() override;
  const char* headerTitle() const override { return title.c_str(); }

  const std::string title;
  const std::vector<std::string> labels;
  const int current;

  int pageTop = 0;                // first row shown; render task only
  std::atomic<int> pageShift{0};  // swiped pages, applied by the next build
  std::atomic<int> pageRows{1};   // rows per page, measured by the last build
  setka::RefreshCadence cadence;  // render task only
  std::atomic<bool> entryRefresh{true};
};
