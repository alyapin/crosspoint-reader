"""Run with python3 test/test_edge_back_input.py (requires a host C++ compiler)."""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def between(source, start, end):
    return start + source.split(start, 1)[1].split(end, 1)[0]


# Production swipe routing: the raw swipe, its filtering out of wasSwipe(), and Back.
input_source = (ROOT / "src/MappedInputManager.cpp").read_text()
swipe_methods = between(input_source, "bool MappedInputManager::decodeSwipe(",
                        "bool MappedInputManager::wasTopEdgeDownSwipe(")
# The book reader in front, by type rather than by isReaderActivity().
manager_source = (ROOT / "src/activities/ActivityManager.cpp").read_text()
foreground_reader = between(manager_source, "ReaderActivity* ActivityManager::foregroundReader() const {",
                            "\n}\n") + "\n}\n"

stub = r"""
#pragma once
#include <cstdint>
struct HalGPIO {
  bool swiped = false;
  float sx = 0, sy = 0, ex = 0, ey = 0;
  bool wasSwipe(float& a, float& b, float& c, float& d) const {
    a = sx; b = sy; c = ex; d = ey;
    return swiped;
  }
  bool wasTouchTap(float&, float&) const { return false; }
  unsigned long lastTouchHeldMs() const { return 200; }
};
"""

harness = r"""
#include <cassert>
#include <memory>
#include <FreeInkUICore.h>
#include "MappedInputManager.h"
#include "components/HeaderBackTapTarget.h"
#include "util/EdgeBackSwipe.h"
namespace fui = freeink::ui;
unsigned long millis() { return 1000; }
class GfxRenderer {
 public:
  int getScreenWidth() const { return 480; }
  int getScreenHeight() const { return 800; }
  void tapToLogical(float nx, float ny, int& x, int& y) const {
    x = static_cast<int>(nx * 480);
    y = static_cast<int>(ny * 800);
  }
};
struct {
  uint8_t edgeBackSwipe = edge_back_swipe::EXCEPT_READING;
} SETTINGS;
void MappedInputManager::rememberTouchHeldTime() const {}
""" + swipe_methods + r"""

struct ReaderActivity;
struct Activity {
  virtual ~Activity() = default;
  virtual bool isReaderActivity() const { return false; }
  virtual ReaderActivity* asReader() { return nullptr; }
};
struct ReaderActivity final : Activity {
  bool isReaderActivity() const override { return true; }
  ReaderActivity* asReader() override { return this; }
};
// A reader child screen (clip selection, lookup) that reports isReaderActivity().
struct ReaderChild final : Activity {
  bool isReaderActivity() const override { return true; }
};
struct ActivityManager {
  enum class PendingAction { None, Push };
  PendingAction pendingAction = PendingAction::None;
  std::unique_ptr<Activity> currentActivity;
  ReaderActivity* foregroundReader() const;
};
""" + foreground_reader + r"""

using Dir = MappedInputManager::SwipeDir;

void swipe(HalGPIO& gpio, int sx, int sy, int ex, int ey) {
  gpio.swiped = true;
  gpio.sx = sx / 480.0f;
  gpio.sy = sy / 800.0f;
  gpio.ex = ex / 480.0f;
  gpio.ey = ey / 800.0f;
}

int main() {
  HalGPIO gpio;
  GfxRenderer renderer;
  MappedInputManager input(gpio, renderer);

  // An inward flick from the right edge: Back, and gone from wasSwipe(); the raw
  // swipe still reports it.
  swipe(gpio, 476, 400, 280, 410);
  assert(input.wasBackGesture());
  assert(input.wasSwipe() == Dir::None);
  int sx = 0, sy = 0, ex = 0, ey = 0;
  assert(input.decodeSwipe(sx, sy, ex, ey) && sx == 476 && ex == 280);

  // Content that claims it (a 2048 board, a deletable row) keeps it a swipe.
  input.setSwipeClaimed(true);
  assert(!input.wasBackGesture());
  assert(input.wasSwipe() == Dir::Left);
  input.setSwipeClaimed(false);

  // Except Reading on a book page where Back would close the book: a page turn.
  input.setBackLeavesBook(true);
  assert(!input.wasBackGesture() && input.wasSwipe() == Dir::Left);
  SETTINGS.edgeBackSwipe = edge_back_swipe::EVERYWHERE;
  assert(input.wasBackGesture() && input.wasSwipe() == Dir::None);
  SETTINGS.edgeBackSwipe = edge_back_swipe::OFF;
  input.setBackLeavesBook(false);
  assert(!input.wasBackGesture() && input.wasSwipe() == Dir::Left);
  SETTINGS.edgeBackSwipe = edge_back_swipe::EXCEPT_READING;

  // Away from the edges a swipe is never Back.
  swipe(gpio, 300, 400, 100, 400);
  assert(!input.wasBackGesture() && input.wasSwipe() == Dir::Left);
  gpio.swiped = false;
  assert(!input.decodeSwipe(sx, sy, ex, ey) && input.wasSwipe() == Dir::None);

  ActivityManager manager;
  assert(manager.foregroundReader() == nullptr);
  manager.currentActivity = std::make_unique<ReaderChild>();
  assert(manager.foregroundReader() == nullptr);
  manager.currentActivity = std::make_unique<ReaderActivity>();
  assert(manager.foregroundReader() == manager.currentActivity.get());
  manager.pendingAction = ActivityManager::PendingAction::Push;
  assert(manager.foregroundReader() == nullptr);
}
"""

with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "HalGPIO.h").write_text(stub)
    cpp = directory / "edge_back_input.cpp"
    executable = directory / "edge_back_input"
    cpp.write_text(harness)
    subprocess.run([
        "c++", "-std=c++20", "-DFREEINK_CAP_TOUCH=0", "-I", str(directory), "-I", str(ROOT / "src"),
        "-I", str(ROOT / "freeink-sdk/libs/ui/FreeInkUI/include"), str(cpp), "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("Edge Back input checks passed")
