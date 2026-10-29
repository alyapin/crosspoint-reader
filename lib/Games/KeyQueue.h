#pragma once

#include <cstdint>

namespace games {

// One key of a button board: an arrow step, Confirm, a held Confirm or Back.
struct BoardKey {
  enum Action : uint8_t { Step, Confirm, Hold, Back };
  int8_t dx = 0;
  int8_t dy = 0;
  Action action = Step;
};

// The board keys in press order, read every loop pass and applied once the render lock is free.
class KeyQueue {
 public:
  static constexpr uint8_t kCapacity = 16;

  // False when full: the key is dropped.
  bool push(const BoardKey& key) {
    if (count == kCapacity) return false;
    keys[(head + count++) % kCapacity] = key;
    return true;
  }
  bool pending() const { return count > 0; }
  void clear() { count = 0; }

  // Applies the keys in order and stops after the first one that is not a step, since it may open
  // another screen; true then, with the rest still queued.
  template <typename Apply>
  bool drain(Apply&& apply) {
    while (count > 0) {
      const BoardKey key = keys[head];
      head = static_cast<uint8_t>((head + 1) % kCapacity);
      --count;
      apply(key);
      if (key.action != BoardKey::Step) return true;
    }
    return false;
  }

 private:
  BoardKey keys[kCapacity] = {};
  uint8_t head = 0;
  uint8_t count = 0;
};

}  // namespace games
