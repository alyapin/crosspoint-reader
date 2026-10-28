#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Personal reading speed in visible characters per minute, learned from
// forward page turns. Characters rather than pages keep the speed valid across
// font sizes and books. A turn counts only when it looks like reading: a full
// page, shown long enough to read and not so long that the reader was away,
// at a rate close to the ones already seen.
class ReadingPace {
 public:
  static constexpr uint16_t DEFAULT_CHARS_PER_MINUTE = 1100;  // ~170 words/min of Russian prose
  static constexpr size_t WINDOW = 16;
  static constexpr uint32_t MIN_PAGE_CHARS = 300;
  static constexpr uint32_t MIN_DWELL_MS = 3000;
  static constexpr uint32_t MAX_DWELL_MS = 10UL * 60UL * 1000UL;
  static constexpr uint32_t MIN_RATE = 100;
  static constexpr uint32_t MAX_RATE = 5000;
  // Rates this many times off the median are rejected once the window holds
  // MIN_SAMPLES_FOR_OUTLIERS samples.
  static constexpr uint32_t OUTLIER_FACTOR = 3;
  static constexpr uint8_t MIN_SAMPLES_FOR_OUTLIERS = 6;
  // The estimate blends from the default to the measured speed over this many samples.
  static constexpr uint8_t FULL_WEIGHT_SAMPLES = 8;

  // One page read in dwellMs. True when it was kept as a sample.
  bool addPage(uint32_t chars, uint32_t dwellMs);
  uint16_t charsPerMinute() const;
  uint8_t sampleCount() const { return count; }

  // Persisted form: magic, version, count, next slot, samples.
  static constexpr size_t ENCODED_SIZE = sizeof(uint32_t) + 3 + WINDOW * sizeof(uint16_t);
  void encode(uint8_t (&out)[ENCODED_SIZE]) const;
  bool decode(const uint8_t* data, size_t size);

 private:
  uint16_t median() const;
  // Mean of the middle half of the samples (all of them below four).
  uint16_t centralMean() const;

  std::array<uint16_t, WINDOW> samples{};
  uint8_t count = 0;
  uint8_t next = 0;
};

// Seconds to read `pages` pages of `charsPerPage` characters at `charsPerMinute`.
uint32_t readingSecondsLeft(uint32_t pages, uint32_t charsPerPage, uint16_t charsPerMinute);

// "ещё 9 мин" / "ещё 1 ч 20 мин" from translated printf templates taking
// unsigned values. Under an hour the minutes round up; from an hour on they
// round to 5 minutes, and from 10 hours on only hours are shown.
void formatTimeLeft(uint32_t seconds, const char* minutesFmt, const char* hoursFmt, const char* hoursMinutesFmt,
                    char* out, size_t outSize);
