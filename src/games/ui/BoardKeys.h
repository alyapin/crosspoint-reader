#pragma once

#include <Arduino.h>
#include <KeyQueue.h>

#include "MappedInputManager.h"
#include "util/ButtonNavigator.h"

namespace games::ui {

// Reads the board keys of the button boards into a KeyQueue every loop pass: the arrows (a held one
// repeats), Confirm, a held Confirm (timed from its own press, not from an arrow held before it) and
// Back. The screen applies them in order once the render lock is free, so keys pressed during a panel
// refresh are neither lost nor reordered, and the next render shows where they ended.
class BoardKeys {
 public:
  // `sideStep`: Up/Down step like Left/Right (chess on edge-button boards). `holdable`: a held Confirm
  // is its own key; otherwise Confirm acts on release only.
  void read(const MappedInputManager& input, const bool sideStep, const bool holdable) {
    using Button = MappedInputManager::Button;
    int dx = 0;
    int dy = 0;
    int& vertical = sideStep ? dx : dy;
    if (holdOff && !input.isPressed(Button::Left) && !input.isPressed(Button::Right) && !input.isPressed(Button::Up) &&
        !input.isPressed(Button::Down)) {
      holdOff = false;
    }
    if (holdOff) {
      ButtonNavigator::onPress({Button::Left}, [&dx] { dx = -1; });
      ButtonNavigator::onPress({Button::Right}, [&dx] { dx = 1; });
      ButtonNavigator::onPress({Button::Up}, [&vertical] { vertical = -1; });
      ButtonNavigator::onPress({Button::Down}, [&vertical] { vertical = 1; });
    } else {
      repeat.onPressAndContinuous({Button::Left}, [&dx] { dx = -1; });
      repeat.onPressAndContinuous({Button::Right}, [&dx] { dx = 1; });
      repeat.onPressAndContinuous({Button::Up}, [&vertical] { vertical = -1; });
      repeat.onPressAndContinuous({Button::Down}, [&vertical] { vertical = 1; });
    }
    if (dx != 0 || dy != 0) queue.push({static_cast<int8_t>(dx), static_cast<int8_t>(dy), BoardKey::Step});

    if (input.wasPressed(Button::Confirm)) {
      confirmDownMs = millis();
      confirmHeld = false;
    }
    if (holdable && !confirmHeld && input.isPressed(Button::Confirm) && millis() - confirmDownMs >= kHoldMs) {
      confirmHeld = true;
      queue.push({0, 0, BoardKey::Hold});
    }
    if (input.wasReleased(Button::Confirm)) {
      if (!confirmHeld) queue.push({0, 0, BoardKey::Confirm});
      confirmHeld = false;
    }
    if (input.wasReleased(Button::Back)) queue.push({0, 0, BoardKey::Back});
  }

  bool pending() const { return queue.pending(); }
  template <typename Apply>
  bool drain(Apply&& apply) {
    return queue.drain(apply);
  }

  // Back from another screen: drop what was queued, and a key still held does not repeat until let go.
  void reset() {
    queue.clear();
    holdOff = true;
  }

 private:
  using BoardKey = games::BoardKey;
  static constexpr uint16_t kRepeatStartMs = 400;
  static constexpr uint16_t kRepeatMs = 250;
  static constexpr unsigned long kHoldMs = 450;

  ButtonNavigator repeat{kRepeatMs, kRepeatStartMs};
  KeyQueue queue;
  unsigned long confirmDownMs = 0;
  bool confirmHeld = false;  // the held Confirm already queued; its release is not a Confirm
  bool holdOff = false;
};

}  // namespace games::ui
