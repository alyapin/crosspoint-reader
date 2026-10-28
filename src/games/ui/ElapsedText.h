#pragma once

// Play time as the game screens show it: whole minutes rounded down ("4 мин"),
// and one fixed text under a minute ("<1 мин"). The timer needs a repaint only
// when elapsedMinutes() differs from the value last painted, so a running game
// repaints it at most once a minute.

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace games::ui {

inline uint32_t elapsedMinutes(const uint32_t ms) { return ms / 60000; }

// The minute a timer shows, or -1 for a screen without one (a finished game).
inline int32_t shownMinute(const bool timerShown, const uint32_t ms) {
  return timerShown ? static_cast<int32_t>(elapsedMinutes(ms)) : -1;
}

// The timer needs a repaint only when the minute it shows is not the one painted.
inline bool timerStale(const int32_t painted, const int32_t shown) { return painted != shown; }

// `minutesFormat` takes the minute count as %u; `underMinute` is shown before the first minute.
inline void formatElapsed(char* out, const size_t cap, const uint32_t ms, const char* minutesFormat,
                          const char* underMinute) {
  const uint32_t minutes = elapsedMinutes(ms);
  if (minutes == 0) {
    snprintf(out, cap, "%s", underMinute);
  } else {
    snprintf(out, cap, minutesFormat, static_cast<unsigned>(minutes));
  }
}

}  // namespace games::ui
