#pragma once

#include <cstdint>

#include "MappedInputManager.h"
#include "util/ButtonNavigator.h"

namespace games::ui {

// Board keys of the button boards: the arrows (a held one repeats) and Confirm. The loop reads them
// every pass but applies them only when the render lock is free, so keys pressed during a panel
// refresh queue up and apply in order, and the next render shows where they ended.
class BoardKeys {
 public:
  struct Key {
    int8_t dx;
    int8_t dy;
    bool confirm;
  };

  // `sideStep`: Up/Down step like Left/Right (chess on edge-button boards).
  void read(const MappedInputManager& input, const bool sideStep = false) {
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
    const bool confirm = input.wasReleased(Button::Confirm);
    if ((dx != 0 || dy != 0 || confirm) && count < kCapacity) {
      keys[(head + count++) % kCapacity] = {static_cast<int8_t>(dx), static_cast<int8_t>(dy), confirm};
    }
  }

  bool pending() const { return count > 0; }

  bool pop(Key& key) {
    if (count == 0) return false;
    key = keys[head];
    head = static_cast<uint8_t>((head + 1) % kCapacity);
    --count;
    return true;
  }

  // Back from another screen: drop what was queued, and a key still held does not repeat until let go.
  void reset() {
    count = 0;
    holdOff = true;
  }

 private:
  static constexpr uint16_t kRepeatStartMs = 400;
  static constexpr uint16_t kRepeatMs = 250;
  static constexpr uint8_t kCapacity = 16;

  ButtonNavigator repeat{kRepeatMs, kRepeatStartMs};
  Key keys[kCapacity] = {};
  uint8_t head = 0;
  uint8_t count = 0;
  bool holdOff = false;
};

}  // namespace games::ui
