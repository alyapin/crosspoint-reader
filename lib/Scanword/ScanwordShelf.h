#pragma once

// Per-puzzle summary for the puzzle list: how far each started puzzle got, so
// the list needs no puzzle or save file per row. Stored as text:
//   SCANWORD-SHELF 1
//   <id> <percent> <solved 0|1> <seconds>

#include <cstddef>
#include <cstdint>

#include "ScanwordPuzzle.h"

namespace scanword {

struct ShelfEntry {
  char id[ID_MAX + 1];
  uint8_t percent;  // filled letters
  bool solved;
  uint32_t seconds;
};

class Shelf {
 public:
  static constexpr uint16_t CAPACITY = 256;
  static constexpr size_t LINE_CAP = 48;

  void clear() { size = 0; }
  uint16_t count() const { return size; }
  const ShelfEntry* find(const char* id) const;
  void remove(const char* id);
  // Adds or updates; false for a bad id or a full shelf.
  bool set(const char* id, uint8_t percent, bool solved, uint32_t seconds);
  bool anyInProgress() const;

  static const char* header() { return "SCANWORD-SHELF 1"; }
  // One line of the stored text, without the newline; returns its length.
  size_t formatEntry(uint16_t index, char* out, size_t cap) const;

  // Stored text in chunks of any size; malformed lines make finish() fail.
  void beginParse();
  bool feed(const char* data, size_t len);
  bool finish();

 private:
  bool parseLine(const char* text, size_t len);

  uint16_t size = 0;
  ShelfEntry entries[CAPACITY];
  bool sawHeader = false;
  bool failed = false;
  size_t used = 0;
  char buf[LINE_CAP];
};

}  // namespace scanword
