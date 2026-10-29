#pragma once

// A «5 букв» session, Wordle in Russian: guess a five-letter noun in six tries.
//   newGame(seed)            picks the answer
//   type(letter) / erase()   edit the current row
//   submit()                 scores a full row that is a known word
//   letter(), mark(), keyMark(), status()  for drawing
//   serialize() / deserialize()
// Letters are 0..31, а … я with ё folded into е. Scoring follows Wordle: a
// letter in its place is Placed; any other is Present while the answer still
// holds an unmatched copy of it (left to right), else Absent. No heap.

#include <cstddef>
#include <cstdint>

namespace fiveletters {

constexpr int LETTERS = 5;
constexpr int ATTEMPTS = 6;
constexpr int ALPHABET = 32;
constexpr uint8_t NO_LETTER = 0xFF;

enum class Mark : uint8_t { None, Absent, Present, Placed };
enum class Status : uint8_t { Playing, Won, Lost };
enum class Submit : uint8_t { Scored, Incomplete, Unknown, Over };

// The letter of a Cyrillic code point in either case, Ё as Е; NO_LETTER otherwise.
uint8_t letterOf(uint32_t codepoint);
// Upper-case code point of a letter.
inline uint32_t codepointOf(const uint8_t letter) { return 0x410 + letter; }

uint32_t pack(const uint8_t (&letters)[LETTERS]);
void unpack(uint32_t word, uint8_t (&letters)[LETTERS]);
// In the answers or the other accepted guesses.
bool isWord(uint32_t word);
int answerCount();
uint32_t answerAt(int index);

void score(const uint8_t (&guess)[LETTERS], const uint8_t (&answer)[LETTERS], Mark (&out)[LETTERS]);

struct Stats {
  uint32_t played = 0;
  uint32_t wins = 0;
  uint32_t streak = 0;
  uint32_t bestStreak = 0;

  void record(bool won);
  size_t serialize(char* out, size_t cap) const;
  // False (stats zeroed) on bad input.
  bool deserialize(const char* text, size_t len);
};

// Upper bounds of serialize() output, terminator included.
constexpr size_t SAVE_MAX = 96;
constexpr size_t STATS_MAX = 96;

class Game {
 public:
  Game() { start(answerAt(0)); }

  void newGame(uint32_t seed);
  // Any five letters will do as the answer.
  void start(uint32_t answer);

  bool type(uint8_t letter);
  bool erase();
  Submit submit();

  Status status() const { return state; }
  bool isOver() const { return state != Status::Playing; }
  // A guess is scored and the game goes on: worth a save.
  bool inProgress() const { return state == Status::Playing && rows > 0; }
  int guesses() const { return rows; }
  int typed() const { return count; }
  uint32_t answer() const { return target; }
  // NO_LETTER where nothing is typed.
  uint8_t letter(int row, int column) const;
  // None outside the scored rows.
  Mark mark(int row, int column) const;
  // Best mark of the letter over the scored rows.
  Mark keyMark(uint8_t letter) const;
  // The answer in upper case, UTF-8.
  size_t answerText(char* out, size_t cap) const;

  size_t serialize(char* out, size_t cap) const;
  // False (game unchanged) on bad input.
  bool deserialize(const char* text, size_t len);

 private:
  void scoreRow();

  uint32_t target = 0;
  uint8_t board[ATTEMPTS][LETTERS] = {};
  Mark marks[ATTEMPTS][LETTERS] = {};
  uint8_t rows = 0;
  uint8_t count = 0;
  Status state = Status::Playing;
};

}  // namespace fiveletters
