#pragma once

// A hangman round over the built-in word list (HangmanWords.h):
//   newGame(seed)          a word not among the last RECENT_COUNT
//   guess(letter)          a miss costs a mistake; MAX_MISTAKES lose
//   hint()                 opens the first hidden letter for a mistake
//   shown(i), key(letter), mistakes(), status()  for drawing
//   serialize() / deserialize()
// Letters are codes 1..33 in alphabet order (А=1, Ё=7, Я=33). Ё is a letter of
// its own: the list spells it, and the keyboard has its key.

#include <cstddef>
#include <cstdint>

namespace hangman {

constexpr uint8_t LETTER_COUNT = 33;
constexpr uint8_t LETTER_E = 6;
constexpr uint8_t LETTER_YO = 7;
constexpr int MAX_LENGTH = 10;
constexpr int MAX_MISTAKES = 6;
constexpr int RECENT_COUNT = 32;
constexpr size_t MAX_CLUE_BYTES = 64;
constexpr size_t SAVE_MAX = 320;

// One byte per letter in the word list and the save, А..Я.
constexpr char ALPHABET[] = "abvgdeEjziyklmnoprstufhcCwWXYxqUQ";

// Upper case; 0 for an invalid code.
uint32_t letterCodepoint(uint8_t letter);
// Appends the UTF-8 bytes of a codepoint below U+0800; returns the count.
int putUtf8(uint32_t cp, char* out);

// "word:definition\n" entries in ALPHABET, the definition's capital implied.
class WordList {
 public:
  WordList(const char* data, uint16_t count) : data(data), count(count) {}

  uint16_t size() const { return count; }
  // Letter codes of the word; its length, or 0 for a bad index or entry.
  int word(uint16_t index, uint8_t* out, int cap) const;
  // The definition as UTF-8, capitalised; its length in bytes.
  size_t clue(uint16_t index, char* out, size_t cap) const;

 private:
  const char* entry(uint16_t index) const;

  const char* data;
  uint16_t count;
};

enum class Status : uint8_t { Playing, Won, Lost };
enum class Key : uint8_t { Untried, Right, Wrong };
enum class Guess : uint8_t { Hit, Miss, Repeat, Ignored };

class Game {
 public:
  explicit Game(const WordList& words);

  void newGame(uint32_t seed);
  // The word at `index`; false (game unchanged) when it does not load.
  bool start(uint16_t index);

  uint16_t wordIndex() const { return index; }
  int length() const { return len; }
  uint8_t letter(const int i) const { return word[i]; }
  bool shown(int i) const;
  Key key(uint8_t letter) const;
  int mistakes() const { return misses + hints; }
  Status status() const { return state; }
  bool isOver() const { return state != Status::Playing; }
  // A round with a guess or a hint in it.
  bool inProgress() const { return state == Status::Playing && tried != 0; }

  Guess guess(uint8_t letter);
  // Not for the last mistake, so a hint never loses the round.
  bool canHint() const { return state == Status::Playing && mistakes() < MAX_MISTAKES - 1; }
  bool hint();

  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d round of the same word list. False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  bool wasTried(const uint8_t l) const { return (tried >> l) & 1; }
  bool inWord(uint8_t l) const;
  void settle();
  void remember(uint16_t word);
  bool recent(uint16_t word) const;

  const WordList* words;
  uint16_t index = 0;
  uint8_t word[MAX_LENGTH] = {};
  uint8_t len = 0;
  uint64_t tried = 0;  // bit per letter code
  uint8_t misses = 0;
  uint8_t hints = 0;
  Status state = Status::Playing;
  uint16_t recentWords[RECENT_COUNT] = {};
  uint8_t recentCount = 0;
};

}  // namespace hangman
