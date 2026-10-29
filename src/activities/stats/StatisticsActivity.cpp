#include "StatisticsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>

#include "CrossPointSettings.h"
#include "StatsUi.h"

using namespace setka;

void StatisticsActivity::onEnter() {
  // The render task may run as soon as ActivityManager drops its lock; keep it
  // out until the state below is in place (same as LibraryListActivity).
  RenderLock lock(*this);
  Activity::onEnter();
  entryRefresh = true;
  summary = stats::StatsSummary{};
  std::fill(std::begin(levels), std::end(levels), 0);
  auto days = makeUniqueNoThrow<DayWindow>();
  if (!days) {
    LOG_ERR("STAT", "OOM: day window %u B", static_cast<unsigned>(sizeof(DayWindow)));
  } else if (READING_STATS.summarize(stats::today(), SETTINGS.readingGoalMinutes, summary, *days) &&
             summary.today != stats::NO_DAY) {
    const uint32_t goal = std::max<uint32_t>(1, SETTINGS.readingGoalMinutes) * 60;
    const uint16_t first = static_cast<uint16_t>(stats::weekStart(summary.today) - (kWeeks - 1) * 7);
    for (int i = 0; i < kWeeks * 7; ++i) {
      const uint16_t day = static_cast<uint16_t>(first + i);
      const uint32_t seconds = days->secondsOn(day);
      levels[i] = day > summary.today ? kFuture : seconds < 60 ? 0 : seconds * 2 < goal ? 1 : seconds < goal ? 2 : 3;
    }
  }
  requestUpdate();
}

void StatisticsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
}

int StatisticsActivity::drawStreaks(const int capTop) const {
  constexpr int kNumberGap = 10;  // between a number and its words
  constexpr int kLineGap = 12;    // between the parts when they take two lines
  const int right = renderer.getScreenWidth() - kSidePadding;
  const int space = renderer.getSpaceWidth(kText10Font);
  const int capHeight = heroCapHeight(renderer, kDigitsFont);
  const char* dot = "\xC2\xB7";
  const unsigned values[2] = {summary.currentStreak, summary.bestStreak};
  const StrId labels[2] = {StrId::STR_STATS_STREAK, StrId::STR_STATS_RECORD};
  char numbers[2][8];
  const char* units[2];
  int width = 2 * space + renderer.getTextWidth(kText10Font, dot);
  for (int i = 0; i < 2; ++i) {
    snprintf(numbers[i], sizeof(numbers[i]), "%u", values[i]);
    units[i] = I18N.get(statsui::plural(values[i], StrId::STR_STATS_DAYS_ONE, StrId::STR_STATS_DAYS_FEW,
                                        StrId::STR_STATS_DAYS_MANY));
    width += renderer.getTextWidth(kText10Font, I18N.get(labels[i])) + 2 * kNumberGap +
             renderer.getTextWidth(kDigitsFont, numbers[i]) + renderer.getTextWidth(kText10Font, units[i]);
  }
  // One line where both parts fit; otherwise the record goes under the streak.
  const bool oneLine = width <= right - kSidePadding;
  int x = kSidePadding;
  int top = capTop;
  for (int i = 0; i < 2; ++i) {
    if (i > 0 && !oneLine) {
      x = kSidePadding;
      top += capHeight + kLineGap;
    }
    const int textTop = top + capHeight - renderer.getFontAscenderSize(kText10Font);
    // Words a long translation pushes past the screen edge are cut.
    const auto text = [&](const char* s) {
      const int textWidth = renderer.getTextWidth(kText10Font, s);
      if (x + textWidth <= right) {
        renderer.drawText(kText10Font, x, textTop, s);
        x += textWidth;
        return;
      }
      const std::string fitted = renderer.truncatedText(kText10Font, s, std::max(0, right - x));
      renderer.drawText(kText10Font, x, textTop, fitted.c_str());
      x += renderer.getTextWidth(kText10Font, fitted.c_str());
    };
    if (i > 0 && oneLine) {
      x += space;
      text(dot);
      x += space;
    }
    text(I18N.get(labels[i]));
    // Numbers in the digits face, so they do not compete with the hero.
    x += kNumberGap + drawHeroNumber(renderer, kDigitsFont, x + kNumberGap, top, numbers[i]) + kNumberGap;
    text(units[i]);
  }
  return top + capHeight;
}

void StatisticsActivity::drawCalendar(const int top, const int width) const {
  constexpr int kGap = 4;
  constexpr int kCellHeight = 18;
  const int cellWidth = (width - 2 * kSidePadding - (kWeeks - 1) * kGap) / kWeeks;
  for (int week = 0; week < kWeeks; ++week) {
    for (int weekday = 0; weekday < 7; ++weekday) {
      const uint8_t level = levels[week * 7 + weekday];
      if (level == kFuture) continue;
      const int x = kSidePadding + week * (cellWidth + kGap);
      const int y = top + weekday * (kCellHeight + kGap);
      if (level == 3) {
        renderer.fillRect(x, y, cellWidth, kCellHeight, true);
        continue;
      }
      if (level > 0) {
        renderer.fillRectDither(x, y, cellWidth, kCellHeight, level == 1 ? Color::LightGray : Color::DarkGray);
      }
      renderer.drawRect(x, y, cellWidth, kCellHeight);
    }
  }
}

void StatisticsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto bands = statsui::drawHeader(renderer, tr(STR_STATS_TITLE), true);
  const int width = bands.width;
  const int line12 = renderer.getLineHeight(kText12Font);
  constexpr int kGap = 24;

  int y = bands.contentTop + kGap;
  renderer.drawText(kText12Font, kSidePadding, y, tr(STR_STATS_TODAY));
  y += line12 + 8;
  char text[48];
  snprintf(text, sizeof(text), "%u", static_cast<unsigned>(summary.todaySeconds / 60));
  const int heroWidth = drawHeroNumber(renderer, kHeroFont, kSidePadding, y, text);
  const int heroBaseline = y + heroCapHeight(renderer, kHeroFont);
  snprintf(text, sizeof(text), tr(STR_STATS_OF_GOAL), static_cast<unsigned>(summary.goalMinutes));
  renderer.drawText(kText12Font, kSidePadding + heroWidth + 12,
                    heroBaseline - renderer.getFontAscenderSize(kText12Font), text);
  y = heroBaseline + kGap;
  const int percent =
      summary.goalMinutes > 0 ? static_cast<int>(summary.todaySeconds * 100 / (summary.goalMinutes * 60u)) : 0;
  drawProgressBar(renderer, kSidePadding, y, width - 2 * kSidePadding, percent);
  y += kProgressHeight + kGap + 4;

  y = drawStreaks(y) + kGap;

  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 20;
  if (summary.today == stats::NO_DAY) {
    for (const auto& line : renderer.wrappedText(kText12Font, tr(STR_STATS_NO_CLOCK), width - 2 * kSidePadding, 3)) {
      renderer.drawText(kText12Font, kSidePadding, y, line.c_str());
      y += line12;
    }
  } else {
    renderer.drawText(kText12Font, kSidePadding, y, tr(STR_STATS_LAST_WEEKS));
    y += line12 + 12;
    drawCalendar(y, width);
    y += 7 * 22 - 4 + kGap;

    renderer.drawText(kText12Font, kSidePadding, y, tr(STR_STATS_THIS_YEAR));
    const unsigned books = summary.booksFinishedThisYear;
    snprintf(text, sizeof(text),
             I18N.get(statsui::plural(books, StrId::STR_STATS_BOOKS_ONE, StrId::STR_STATS_BOOKS_FEW,
                                      StrId::STR_STATS_BOOKS_MANY)),
             books);
    renderer.drawText(kText12Font, width / 2 + kSidePadding / 2, y, text, true, kSemiBold);
    snprintf(text, sizeof(text), tr(STR_STATS_HOURS), static_cast<unsigned>(summary.yearSeconds / 3600));
    renderer.drawText(kText12Font, width - kSidePadding - renderer.getTextWidth(kText12Font, text, kSemiBold), y, text,
                      true, kSemiBold);
  }
  statsui::drawHints(renderer, mappedInput, "", /*navigates=*/false);
  renderer.displayBuffer(entryRefresh ? cadence.entry() : cadence.update());
  entryRefresh = false;
}
