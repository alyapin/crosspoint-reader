#include "ReadingPace.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t MAGIC = 0x45434150;  // "PACE"
constexpr uint8_t VERSION = 1;
constexpr size_t HEADER_SIZE = sizeof(uint32_t) + 3;
}  // namespace

bool ReadingPace::addPage(const uint32_t chars, const uint32_t dwellMs) {
  if (chars < MIN_PAGE_CHARS || dwellMs < MIN_DWELL_MS || dwellMs > MAX_DWELL_MS) return false;
  const uint64_t rate = static_cast<uint64_t>(chars) * 60000ULL / dwellMs;
  if (rate < MIN_RATE || rate > MAX_RATE) return false;
  if (count >= MIN_SAMPLES_FOR_OUTLIERS) {
    const uint32_t mid = median();
    if (rate * OUTLIER_FACTOR < mid || rate > static_cast<uint64_t>(mid) * OUTLIER_FACTOR) return false;
  }
  samples[next] = static_cast<uint16_t>(rate);
  next = static_cast<uint8_t>((next + 1) % WINDOW);
  if (count < WINDOW) count++;
  return true;
}

uint16_t ReadingPace::median() const {
  std::array<uint16_t, WINDOW> sorted = samples;
  std::sort(sorted.begin(), sorted.begin() + count);
  return sorted[count / 2];
}

uint16_t ReadingPace::centralMean() const {
  std::array<uint16_t, WINDOW> sorted = samples;
  std::sort(sorted.begin(), sorted.begin() + count);
  const size_t trim = count >= 4 ? count / 4 : 0;
  uint32_t sum = 0;
  for (size_t i = trim; i < count - trim; i++) sum += sorted[i];
  return static_cast<uint16_t>(sum / (count - 2 * trim));
}

uint16_t ReadingPace::charsPerMinute() const {
  if (count == 0) return DEFAULT_CHARS_PER_MINUTE;
  const uint32_t weight = std::min<uint32_t>(count, FULL_WEIGHT_SAMPLES);
  return static_cast<uint16_t>((centralMean() * weight + DEFAULT_CHARS_PER_MINUTE * (FULL_WEIGHT_SAMPLES - weight)) /
                               FULL_WEIGHT_SAMPLES);
}

void ReadingPace::encode(uint8_t (&out)[ENCODED_SIZE]) const {
  std::memcpy(out, &MAGIC, sizeof(MAGIC));
  out[4] = VERSION;
  out[5] = count;
  out[6] = next;
  std::memcpy(out + HEADER_SIZE, samples.data(), WINDOW * sizeof(uint16_t));
}

bool ReadingPace::decode(const uint8_t* data, const size_t size) {
  if (!data || size != ENCODED_SIZE) return false;
  uint32_t magic;
  std::memcpy(&magic, data, sizeof(magic));
  // A partial window fills slots in order, so its next slot is its count.
  if (magic != MAGIC || data[4] != VERSION || data[5] > WINDOW || data[6] >= WINDOW ||
      (data[5] < WINDOW && data[6] != data[5])) {
    return false;
  }
  std::array<uint16_t, WINDOW> loaded{};
  std::memcpy(loaded.data(), data + HEADER_SIZE, WINDOW * sizeof(uint16_t));
  for (size_t i = 0; i < data[5]; i++) {
    if (loaded[i] < MIN_RATE || loaded[i] > MAX_RATE) return false;
  }
  samples = loaded;
  count = data[5];
  next = data[6];
  return true;
}

uint32_t readingSecondsLeft(const uint32_t pages, const uint32_t charsPerPage, const uint16_t charsPerMinute) {
  if (charsPerMinute == 0) return 0;
  const uint64_t seconds = static_cast<uint64_t>(pages) * charsPerPage * 60ULL / charsPerMinute;
  return static_cast<uint32_t>(std::min<uint64_t>(seconds, UINT32_MAX));
}

void formatTimeLeft(const uint32_t seconds, const char* minutesFmt, const char* hoursFmt, const char* hoursMinutesFmt,
                    char* out, const size_t outSize) {
  if (!out || outSize == 0) return;
  const uint32_t minutes = std::max<uint32_t>(1, (seconds + 59) / 60);
  if (minutes < 60) {
    snprintf(out, outSize, minutesFmt, static_cast<unsigned>(minutes));
    return;
  }
  const uint32_t rounded = (minutes + 2) / 5 * 5;
  if (rounded >= 10 * 60) {
    snprintf(out, outSize, hoursFmt, static_cast<unsigned>((minutes + 30) / 60));
  } else if (rounded % 60 == 0) {
    snprintf(out, outSize, hoursFmt, static_cast<unsigned>(rounded / 60));
  } else {
    snprintf(out, outSize, hoursMinutesFmt, static_cast<unsigned>(rounded / 60), static_cast<unsigned>(rounded % 60));
  }
}
