#pragma once

// Pieces shared by the FreeCell and Spider cores: Klondike's card byte, the
// move history in the save text and the fewest-moves record. No heap.

#include <cstddef>
#include <cstdint>

#include "GameText.h"

namespace cards {

constexpr int RANKS = 13;

// Card byte: rank 0 (ace) .. 12 (king) in bits 0-3, suit in bits 4-5, face-up in bit 7.
enum Suit : uint8_t { SPADES, HEARTS, DIAMONDS, CLUBS };
constexpr uint8_t FACE_UP = 0x80;
constexpr uint8_t NO_CARD = 0xFF;
constexpr uint8_t makeCard(const int suit, const int rank) { return static_cast<uint8_t>((suit << 4) | rank); }
constexpr int rankOf(const uint8_t card) { return card & 0x0F; }
constexpr int suitOf(const uint8_t card) { return (card >> 4) & 0x03; }
constexpr bool isRed(const uint8_t card) { return suitOf(card) == HEARTS || suitOf(card) == DIAMONDS; }
constexpr bool isFaceUp(const uint8_t card) { return (card & FACE_UP) != 0; }

// One move of the history.
struct Step {
  uint8_t from = 0;
  uint8_t to = 0;
  uint8_t count = 0;
};

// The fewest moves of a won game per mode; 0 until one is won.
struct Best {
  static constexpr int MODES = 3;
  uint16_t moves[MODES] = {};

  // True when `moveCount` beats the kept one and replaces it.
  bool record(int mode, int moveCount);
  size_t serialize(char* out, size_t cap) const;
  bool deserialize(const char* text, size_t len);
};
constexpr size_t BEST_MAX = 64;

// A history as a string of three hex digits per step.
void putSteps(games::TextWriter& w, const char* key, const Step* steps, int count);
// One step from three hex digits; false on a bad digit.
bool readStep(const char* text, Step& out);

}  // namespace cards
