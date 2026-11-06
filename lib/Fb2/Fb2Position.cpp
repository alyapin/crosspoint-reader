#include "Fb2Position.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "Fb2ToEpub.h"

namespace Fb2Position {

bool parseProgress(const uint8_t* data, const size_t size, Saved& out) {
  if (size != 4 && size != 6 && size != 10) return false;
  out.spine = static_cast<uint16_t>(data[0] | data[1] << 8);
  out.page = static_cast<uint16_t>(data[2] | data[3] << 8);
  out.pageCount = size >= 6 ? static_cast<uint16_t>(data[4] | data[5] << 8) : 0;
  return true;
}

uint8_t parseStamp(const char* text, const size_t len) {
  const std::string s(text, len);
  char* end = nullptr;
  const unsigned long v = strtoul(s.c_str(), &end, 10);
  return end != s.c_str() && v > 0 && v <= UINT8_MAX ? static_cast<uint8_t>(v) : 0;
}

size_t formatStamp(char* out, const size_t size) {
  const int n = snprintf(out, size, "%u\n", static_cast<unsigned>(Fb2ToEpub::FORMAT_VERSION));
  return n > 0 && static_cast<size_t>(n) < size ? static_cast<size_t>(n) : 0;
}

bool reconvert(const uint8_t stamp) { return stamp != Fb2ToEpub::FORMAT_VERSION; }

size_t formatPending(char* out, const size_t size, const float fraction, const uint8_t version) {
  const int n = snprintf(out, size, "%.6f %u\n", static_cast<double>(std::clamp(fraction, 0.0f, 1.0f)),
                         static_cast<unsigned>(version));
  return n > 0 && static_cast<size_t>(n) < size ? static_cast<size_t>(n) : 0;
}

bool parsePending(const char* text, const size_t len, float& fraction, uint8_t& version) {
  const std::string s(text, len);
  char* end = nullptr;
  const float f = strtof(s.c_str(), &end);
  if (end == s.c_str() || !std::isfinite(f) || f < 0.0f || f > 1.0f) return false;
  const char* rest = end;
  const unsigned long v = strtoul(rest, &end, 10);
  if (end == rest || v > UINT8_MAX) return false;
  fraction = f;
  version = static_cast<uint8_t>(v);
  return true;
}

bool restoreByFraction(const uint8_t pendingVersion, const bool progressLoaded) {
  return !progressLoaded || pendingVersion != Fb2ToEpub::FORMAT_VERSION;
}

bool bookmarksMoved(const uint8_t pendingVersion) { return pendingVersion != Fb2ToEpub::FORMAT_VERSION; }

}  // namespace Fb2Position
