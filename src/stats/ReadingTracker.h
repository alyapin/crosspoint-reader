#pragma once

#include <cstdint>

// A stretch of reading in one book on one local day, as written to the log.
struct ReadingSegment {
  int64_t startEpoch = 0;  // UTC, 0 when the clock was unknown
  uint16_t day = 0;        // stats::NO_DAY when the clock was unknown
  uint16_t seconds = 0;
  uint16_t pages = 0;
  uint16_t progressBp = 0;  // whole-book progress at the end, 0..10000
  bool continues = false;   // the same visit as the previous segment
};

// Active reading time of one reader visit, fed from the reader's page-turn and
// page-drawn hooks. Time between two page draws counts unless the gap exceeds
// IDLE_MS (the reader was away); a gap across local midnight is split there.
// A segment closes at local midnight and once it holds SEGMENT_SECONDS, so a
// crash loses at most one segment (under SEGMENT_SECONDS + IDLE_MS). No
// allocation or I/O; the owner writes ready segments outside the render path.
// Session/idle model adapted from guloff/folio-firmware (MIT),
// src/inklink/ReadingStats.cpp.
class ReadingTracker {
 public:
  static constexpr uint32_t IDLE_MS = 5UL * 60UL * 1000UL;
  static constexpr uint16_t SEGMENT_SECONDS = 5 * 60;
  // A visit shorter than this with no page turned was opened by mistake.
  static constexpr uint16_t MIN_VISIT_SECONDS = 10;
  static constexpr uint16_t FINISHED_BP = 9800;

  void noteTurn(bool forward, bool succeeded) { pendingForward = forward && succeeded; }
  // After a page (or the end-of-book screen) is drawn. epoch is
  // trustedtime::trustedNow(), 0 when unknown.
  void onPageShown(uint32_t nowMs, int64_t epoch, int progressBp, bool atEnd);
  // The reader leaves or the device sleeps: time since the last page is idle
  // unless short. The next page drawn starts a new visit.
  void close(uint32_t nowMs);

  bool hasReady() const { return readyValid; }
  ReadingSegment takeReady();
  // A forward turn reached FINISHED_BP, or the end-of-book screen was shown.
  bool takeFinished();

 private:
  // Opens a segment at the last page drawn, unless one is open.
  void start();
  void addMs(uint32_t ms);
  void cut();

  ReadingSegment open;
  ReadingSegment ready;
  // The last page drawn.
  uint32_t lastMs = 0;
  int64_t lastEpoch = 0;
  uint16_t lastDay = 0;
  uint32_t msCarry = 0;
  bool anchored = false;
  bool openHasData = false;
  bool readyValid = false;
  bool visitStarted = false;
  bool pendingForward = false;
  bool finished = false;
};
