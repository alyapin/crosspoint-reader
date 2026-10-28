#pragma once

// What a chess clock shows, kept coarse so the e-ink panel repaints rarely:
// whole minutes rounded up while more than a minute is left ("10" for
// 9:01-10:00, drawn with "мин"), then M:SS rounded up to 10 s ("1:00", "0:50" ...
// "0:10"). `shownSeconds` changes exactly when the text does: a clock needs a
// repaint only when it differs from the value last painted.

#include <cstdint>
#include <cstdio>

namespace chess::ui {

struct ClockText {
  uint32_t shownSeconds = 0;
  bool minutes = false;  // text is a minute count
  char text[8] = {};
};

inline ClockText clockText(const uint32_t remainingMs) {
  ClockText clock;
  const uint32_t seconds = remainingMs / 1000 + (remainingMs % 1000 != 0 ? 1 : 0);
  if (seconds > 60) {
    const uint32_t minutes = (seconds + 59) / 60;
    clock.shownSeconds = minutes * 60;
    clock.minutes = true;
    snprintf(clock.text, sizeof(clock.text), "%u", static_cast<unsigned>(minutes));
  } else {
    clock.shownSeconds = (seconds + 9) / 10 * 10;
    snprintf(clock.text, sizeof(clock.text), "%u:%02u", static_cast<unsigned>(clock.shownSeconds / 60),
             static_cast<unsigned>(clock.shownSeconds % 60));
  }
  return clock;
}

// Milliseconds until a running clock shows its next text; the last step is the flag at 0.
inline uint32_t msUntilClockTextChange(const uint32_t remainingMs) {
  if (remainingMs == 0) return 0;
  const ClockText clock = clockText(remainingMs);
  const uint32_t nextShownMs = (clock.shownSeconds - (clock.minutes ? 60 : 10)) * 1000;
  return remainingMs - nextShownMs;
}

}  // namespace chess::ui
