#pragma once

#include <cstdint>

#include "activities/Activity.h"
#include "components/setka/SetkaStyle.h"
#include "stats/ReadingStats.h"

// «Статистика»: today's minutes against the daily goal, the streak and its
// record, a 12-week reading calendar and this year's totals. Read-only; Back
// (button, edge swipe or header) leaves.
class StatisticsActivity final : public Activity {
 public:
  StatisticsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Statistics", renderer, mappedInput) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int kWeeks = 12;
  static constexpr uint8_t kFuture = 0xFF;

  void drawCalendar(int top, int width) const;
  // "серия: 3 дня · рекорд: 12 дней" on one line; returns its baseline.
  int drawStreaks(int capTop) const;

  stats::StatsSummary summary;
  // Shade of each calendar day, Monday-first weeks, oldest week first: 0
  // unread .. 3 goal met, kFuture after today.
  uint8_t levels[kWeeks * 7] = {};
  bool entryRefresh = true;
  setka::RefreshCadence cadence;
};
