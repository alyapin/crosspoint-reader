#include "ScanwordShelf.h"

#include <KeyboardGames.h>
#if SOVA_KEYBOARD_GAMES
#include <cstdio>
#include <cstring>

namespace scanword {
namespace {

// Reads a decimal number up to `max` at text[pos], followed by a space or the end.
bool number(const char* text, const size_t len, size_t& pos, const uint32_t max, uint32_t& out) {
  if (pos >= len || text[pos] < '0' || text[pos] > '9') return false;
  uint64_t v = 0;
  while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
    v = v * 10 + static_cast<uint64_t>(text[pos] - '0');
    if (v > max) return false;
    ++pos;
  }
  if (pos < len && text[pos] != ' ') return false;
  if (pos < len) ++pos;
  out = static_cast<uint32_t>(v);
  return true;
}

}  // namespace

const ShelfEntry* Shelf::find(const char* id) const {
  for (uint16_t i = 0; i < size; ++i) {
    if (strcmp(entries[i].id, id) == 0) return &entries[i];
  }
  return nullptr;
}

bool Shelf::set(const char* id, const uint8_t percent, const bool solved, const uint32_t seconds) {
  if (!validId(id)) return false;
  auto* e = const_cast<ShelfEntry*>(find(id));
  if (e == nullptr) {
    if (size == CAPACITY) return false;
    e = &entries[size++];
    strcpy(e->id, id);
  }
  e->percent = percent > 100 ? 100 : percent;
  e->solved = solved;
  e->seconds = seconds;
  return true;
}

void Shelf::remove(const char* id) {
  for (uint16_t i = 0; i < size; ++i) {
    if (strcmp(entries[i].id, id) != 0) continue;
    entries[i] = entries[--size];
    return;
  }
}

bool Shelf::anyInProgress() const {
  for (uint16_t i = 0; i < size; ++i) {
    if (!entries[i].solved) return true;
  }
  return false;
}

size_t Shelf::formatEntry(const uint16_t index, char* out, const size_t cap) const {
  const ShelfEntry& e = entries[index];
  const int n = snprintf(out, cap, "%s %u %u %lu", e.id, static_cast<unsigned>(e.percent), e.solved ? 1u : 0u,
                         static_cast<unsigned long>(e.seconds));
  return n > 0 && static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : 0;
}

void Shelf::beginParse() {
  size = 0;
  sawHeader = false;
  failed = false;
  used = 0;
}

bool Shelf::feed(const char* data, const size_t len) {
  for (size_t i = 0; i < len && !failed; ++i) {
    if (data[i] == '\n') {
      if (used > 0 && buf[used - 1] == '\r') --used;
      failed = !parseLine(buf, used);
      used = 0;
    } else if (used + 1 >= LINE_CAP) {
      failed = true;
    } else {
      buf[used++] = data[i];
    }
  }
  return !failed;
}

bool Shelf::finish() {
  if (!failed && used > 0) {
    if (buf[used - 1] == '\r') --used;
    failed = !parseLine(buf, used);
    used = 0;
  }
  if (failed || !sawHeader) {
    size = 0;
    return false;
  }
  return true;
}

bool Shelf::parseLine(const char* text, const size_t len) {
  if (len == 0) return true;
  if (!sawHeader) {
    sawHeader = len == strlen(header()) && memcmp(text, header(), len) == 0;
    return sawHeader;
  }
  size_t space = 0;
  while (space < len && text[space] != ' ') ++space;
  if (space == 0 || space > ID_MAX || space == len) return false;
  char id[ID_MAX + 1];
  memcpy(id, text, space);
  id[space] = '\0';
  size_t pos = space + 1;
  uint32_t percent = 0;
  uint32_t solved = 0;
  uint32_t seconds = 0;
  if (!number(text, len, pos, 100, percent) || !number(text, len, pos, 1, solved) ||
      !number(text, len, pos, UINT32_MAX, seconds) || pos != len) {
    return false;
  }
  return set(id, static_cast<uint8_t>(percent), solved == 1, seconds);
}

}  // namespace scanword

#endif  // SOVA_KEYBOARD_GAMES
