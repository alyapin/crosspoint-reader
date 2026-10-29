#include "Cards.h"

namespace cards {
namespace {
constexpr const char* BEST_KEYS[Best::MODES] = {"best0", "best1", "best2"};
}  // namespace

bool Best::record(const int mode, const int moveCount) {
  if (mode < 0 || mode >= MODES || moveCount <= 0 || moveCount > UINT16_MAX) return false;
  if (moves[mode] != 0 && moveCount >= moves[mode]) return false;
  moves[mode] = static_cast<uint16_t>(moveCount);
  return true;
}

size_t Best::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  for (int i = 0; i < MODES; ++i) w.number(BEST_KEYS[i], moves[i]);
  return w.finish();
}

bool Best::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  Best read;
  for (int i = 0; i < MODES; ++i) {
    uint32_t value = 0;
    if (!r.number(BEST_KEYS[i], value) || value > UINT16_MAX) return false;
    read.moves[i] = static_cast<uint16_t>(value);
  }
  *this = read;
  return true;
}

void putSteps(games::TextWriter& w, const char* key, const Step* steps, const int count) {
  w.beginString(key);
  for (int i = 0; i < count; ++i) {
    w.put(games::hexDigit(steps[i].from));
    w.put(games::hexDigit(steps[i].to));
    w.put(games::hexDigit(steps[i].count));
  }
  w.endString();
}

bool readStep(const char* text, Step& out) {
  const int from = games::hexValue(text[0]);
  const int to = games::hexValue(text[1]);
  const int count = games::hexValue(text[2]);
  if (from < 0 || to < 0 || count < 0) return false;
  out = Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(count)};
  return true;
}

}  // namespace cards
