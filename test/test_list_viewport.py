"""Run with python3 test/test_list_viewport.py (requires a host C++ compiler)."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
# Compile the production method without the activity's hardware dependencies.
source = (ROOT / "src/activities/UiListActivity.cpp").read_text()
method = source.split("void UiListActivity::syncListViewport(", 1)[1]
method = "void UiListActivity::syncListViewport(" + method.split(
    "\nvoid UiListActivity::drawChrome", 1
)[0]
harness = r"""
#include <components/lists/list.h>
#include <cassert>
#include <cstring>
namespace fui = freeink::ui;
struct UiScreen {
  int16_t height = 80;
  int16_t rowHeight = 20;
  fui::DeviceContext device() const { return {}; }
  void syncListViewport(fui::ListNav& nav, fui::ListProps& props, int count, int offset) {
    nav.syncToProps({0, 0, 200, height}, rowHeight, 0, count, props, offset);
  }
};
// 8 px a byte, 12 in SemiBold; clips like the firmware target, so a preview
// row would show up as text past the band.
struct BoldTarget : fui::DrawTarget {
  fui::Rect clip{0, 0, 32767, 32767};
  int lowest = 0;
  fui::Size measureText(fui::FontId, const char* text, fui::TextStyle style) const override {
    return {static_cast<int16_t>((style.bold ? 12 : 8) * std::strlen(text)), 20};
  }
  int16_t lineHeight(fui::FontId) const override { return 20; }
  fui::Rect clipRect() const override { return clip; }
  bool setClipRect(fui::Rect rect) override { clip = rect; return true; }
  void fill(fui::Rect, fui::Paint, uint8_t, uint8_t) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect rect, const char*, fui::TextStyle) override {
    if (rect.bottom() > lowest) lowest = rect.bottom();
  }
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint, fui::Rotation) override {}
};
struct UiListActivity {
  fui::ListNav nav;
  int count = 16;
  fui::ListNav& activeNav() { return nav; }
  int listCount() const { return count; }
  void syncListViewport(UiScreen&, fui::ListProps&, int = 0);
};
""" + method + r"""
int main() {
  for (int offset : {0, 1}) {
    UiListActivity activity;
    UiScreen screen;
    fui::ListProps props;
    auto& nav = activity.nav;
    nav.reset(offset);
    activity.syncListViewport(screen, props, offset);
    nav.onListRendered(0, 4, true);
    nav.requestScroll(4);
    activity.syncListViewport(screen, props, offset);
    assert(nav.top == 4 && props.topIndex == 4 && nav.selected == offset);
    nav.onListRendered(4, 4, false);
    nav.selected = 5 + offset;
    nav.requestScroll(-4);
    activity.syncListViewport(screen, props, offset);
    assert(nav.top == 0 && props.topIndex == 0 && nav.selected == 5 + offset);

    // Wrapped settings rows: button follow must preserve the measured viewport
    // even when the fixed-height estimate says the entire list fits.
    activity.count = 6;
    screen.height = 160;
    nav.onListRendered(2, 3, true);
    nav.drawnCount = 6;
    nav.requestSelection(3 + offset);
    activity.syncListViewport(screen, props, offset);
    assert(nav.top == 2 && props.topIndex == 2);
  }

  // Button boards draw whole rows only, and a selected label that wraps only
  // in SemiBold still lands among them after the render's follow passes.
  {
    UiListActivity activity;
    activity.count = 12;
    UiScreen screen;
    screen.height = 200;
    screen.rowHeight = 30;
    fui::ListItem items[12];
    for (auto& item : items) item.label = "Eighteen chars.";  // 120 px, 180 in SemiBold
    BoldTarget target;
    fui::DeviceContext device;
    device.width = 200;
    device.height = 200;
    fui::InputSnapshot input;
    fui::InteractionBuffer<32> interactions;
    for (int selection : {0, 5, 6, 7, 11, 3, 0}) {
      activity.nav.requestSelection(selection);
      for (int pass = 0; pass < 8; ++pass) {
        fui::ListProps props;
        props.items = items;
        props.count = 12;
        props.rowHeight = 30;
        props.sidePadding = 0;
        props.labelText.maxLines = 2;
        props.boldSelected = true;
        activity.syncListViewport(screen, props);
        assert(!props.partialTrailingRow);
        interactions.clear();
        target.lowest = 0;
        fui::Frame<32> frame(target, device, input, interactions);
        fui::list(frame, {0, 0, 160, 200}, props);
        if (!activity.nav.consumeRebuildNeeded()) break;
      }
      assert(target.lowest <= 200);
      assert(activity.nav.top <= selection && selection < activity.nav.top + activity.nav.drawnRows);
    }
  }

  // A selected last whole row whose new text wraps grows in place; the next
  // render follows it without the selection moving.
  {
    UiListActivity activity;
    activity.count = 12;
    UiScreen screen;
    screen.height = 190;
    screen.rowHeight = 30;
    fui::ListItem items[12];
    for (auto& item : items) item.label = "Short";
    BoldTarget target;
    fui::DeviceContext device;
    device.width = 200;
    device.height = 200;
    fui::InputSnapshot input;
    fui::InteractionBuffer<32> interactions;
    const auto render = [&] {
      for (int pass = 0; pass < 8; ++pass) {
        fui::ListProps props;
        props.items = items;
        props.count = 12;
        props.rowHeight = 30;
        props.sidePadding = 0;
        props.labelText.maxLines = 2;
        props.boldSelected = true;
        activity.syncListViewport(screen, props);
        interactions.clear();
        target.lowest = 0;
        fui::Frame<32> frame(target, device, input, interactions);
        fui::list(frame, {0, 0, 160, 190}, props);
        if (!activity.nav.consumeRebuildNeeded()) break;
      }
    };
    activity.nav.requestSelection(5);
    render();
    assert(activity.nav.top == 0 && activity.nav.drawnRows == 6);
    items[5].label = "Eighteen chars.";  // two lines in SemiBold: 50 px
    render();
    assert(target.lowest <= 190);
    assert(activity.nav.top <= 5 && 5 < activity.nav.top + activity.nav.drawnRows);
  }
}
"""
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "viewport.cpp"
    executable = Path(directory) / "viewport"
    cpp.write_text("#include <initializer_list>\n" + harness)
    subprocess.run([
        "c++", "-std=c++17", "-I",
        str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/include"),
        str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp"),
        str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("List viewport regression checks passed")
