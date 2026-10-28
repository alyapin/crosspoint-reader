#include "ReadingTracker.h"

#include <algorithm>

#include "StatsCalendar.h"

void ReadingTracker::start() {
  if (openHasData) return;
  open = ReadingSegment{};
  open.startEpoch = lastEpoch;
  open.day = lastDay;
  open.continues = visitStarted;
  openHasData = true;
}

void ReadingTracker::addMs(const uint32_t ms) {
  if (ms == 0) return;
  start();
  msCarry += ms;
  open.seconds = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, open.seconds + msCarry / 1000));
  msCarry %= 1000;
}

void ReadingTracker::cut() {
  if (!openHasData) return;
  if (!readyValid) {
    ready = open;
    readyValid = true;
  } else {
    // The owner has not written the previous segment yet; keep one.
    ready.seconds = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, ready.seconds + open.seconds));
    ready.pages = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, ready.pages + open.pages));
    ready.progressBp = open.progressBp;
  }
  openHasData = false;
  visitStarted = true;
}

void ReadingTracker::onPageShown(const uint32_t nowMs, const int64_t epoch, const int progressBp, const bool atEnd) {
  const uint16_t localDay = stats::localDay(epoch);
  const bool turnedForward = anchored && pendingForward;
  pendingForward = false;
  if (anchored) {
    const uint32_t gap = nowMs - lastMs;
    if (gap <= IDLE_MS && localDay != lastDay && localDay != stats::NO_DAY && lastDay != stats::NO_DAY) {
      // Read across midnight: the time before it belongs to the old day.
      const uint32_t afterMidnight = std::min<uint32_t>(gap, stats::localSecondsOfDay(epoch) * 1000u);
      addMs(gap - afterMidnight);
      cut();
      lastEpoch = epoch - afterMidnight / 1000;
      lastDay = localDay;
      addMs(afterMidnight);
    } else if (gap <= IDLE_MS) {
      addMs(gap);
    }
    if (turnedForward) {
      start();
      open.pages++;
    }
  }
  if (openHasData) {
    open.progressBp = static_cast<uint16_t>(std::clamp(progressBp, 0, 10000));
    if (localDay != open.day || open.seconds >= SEGMENT_SECONDS) cut();
  }
  if (atEnd || (turnedForward && progressBp >= FINISHED_BP)) finished = true;
  lastMs = nowMs;
  lastEpoch = epoch;
  lastDay = localDay;
  anchored = true;
}

void ReadingTracker::close(const uint32_t nowMs) {
  if (anchored && nowMs - lastMs <= IDLE_MS) addMs(nowMs - lastMs);
  anchored = false;
  pendingForward = false;
  msCarry = 0;
  if (openHasData && !open.continues && open.seconds < MIN_VISIT_SECONDS && open.pages == 0) openHasData = false;
  cut();
  visitStarted = false;
}

ReadingSegment ReadingTracker::takeReady() {
  readyValid = false;
  return ready;
}

bool ReadingTracker::takeFinished() {
  const bool was = finished;
  finished = false;
  return was;
}
