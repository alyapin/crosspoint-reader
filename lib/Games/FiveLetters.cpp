#include "FiveLetters.h"

#include <cstring>

#include "FiveLettersWords.h"
#include "GameCommon.h"
#include "GameText.h"

namespace fiveletters {
namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr uint8_t LETTER_E = 5;
constexpr uint32_t WORD_LIMIT = 1u << (5 * LETTERS);
constexpr char DIGITS[] = "0123456789abcdefghijklmnopqrstuv";

bool find(const uint32_t* table, const size_t n, const uint32_t word) {
  size_t lo = 0;
  size_t hi = n;
  while (lo < hi) {
    const size_t mid = (lo + hi) / 2;
    if (table[mid] < word) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo < n && table[lo] == word;
}

int digitValue(const char c) {
  const char* p = c != '\0' ? strchr(DIGITS, c) : nullptr;
  return p != nullptr ? static_cast<int>(p - DIGITS) : -1;
}

}  // namespace

uint8_t letterOf(const uint32_t cp) {
  if (cp >= 0x410 && cp <= 0x42F) return static_cast<uint8_t>(cp - 0x410);
  if (cp >= 0x430 && cp <= 0x44F) return static_cast<uint8_t>(cp - 0x430);
  if (cp == 0x401 || cp == 0x451) return LETTER_E;
  return NO_LETTER;
}

uint32_t pack(const uint8_t (&letters)[LETTERS]) {
  uint32_t word = 0;
  for (const uint8_t l : letters) word = word << 5 | (l & 31);
  return word;
}

void unpack(uint32_t word, uint8_t (&letters)[LETTERS]) {
  for (int i = LETTERS - 1; i >= 0; --i) {
    letters[i] = static_cast<uint8_t>(word & 31);
    word >>= 5;
  }
}

bool isWord(const uint32_t word) {
  return find(ANSWERS, sizeof(ANSWERS) / sizeof(ANSWERS[0]), word) ||
         find(EXTRA_GUESSES, sizeof(EXTRA_GUESSES) / sizeof(EXTRA_GUESSES[0]), word);
}

int answerCount() { return static_cast<int>(sizeof(ANSWERS) / sizeof(ANSWERS[0])); }

uint32_t answerAt(const int index) { return ANSWERS[index]; }

void score(const uint8_t (&guess)[LETTERS], const uint8_t (&answer)[LETTERS], Mark (&out)[LETTERS]) {
  uint8_t unmatched[ALPHABET] = {};
  for (int i = 0; i < LETTERS; ++i) {
    if (guess[i] == answer[i]) {
      out[i] = Mark::Placed;
    } else {
      out[i] = Mark::Absent;
      ++unmatched[answer[i] & 31];
    }
  }
  for (int i = 0; i < LETTERS; ++i) {
    if (out[i] == Mark::Absent && unmatched[guess[i] & 31] > 0) {
      out[i] = Mark::Present;
      --unmatched[guess[i] & 31];
    }
  }
}

// --- stats -------------------------------------------------------------------

void Stats::record(const bool won) {
  ++played;
  if (won) {
    ++wins;
    ++streak;
    if (streak > bestStreak) bestStreak = streak;
  } else {
    streak = 0;
  }
}

size_t Stats::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("played", played);
  w.number("wins", wins);
  w.number("streak", streak);
  w.number("best", bestStreak);
  return w.finish();
}

bool Stats::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  Stats s;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("played", s.played) ||
      !r.number("wins", s.wins) || !r.number("streak", s.streak) || !r.number("best", s.bestStreak) ||
      s.wins > s.played || s.streak > s.bestStreak || s.bestStreak > s.wins) {
    *this = Stats{};
    return false;
  }
  *this = s;
  return true;
}

// --- game --------------------------------------------------------------------

void Game::newGame(const uint32_t seed) {
  games::Rng rng(seed);
  start(answerAt(static_cast<int>(rng.below(static_cast<uint32_t>(answerCount())))));
}

void Game::start(const uint32_t answer) {
  target = answer % WORD_LIMIT;
  memset(board, 0, sizeof(board));
  memset(marks, 0, sizeof(marks));
  rows = 0;
  count = 0;
  state = Status::Playing;
}

bool Game::type(const uint8_t letter) {
  if (isOver() || count >= LETTERS || letter >= ALPHABET) return false;
  board[rows][count++] = letter;
  return true;
}

bool Game::erase() {
  if (isOver() || count == 0) return false;
  --count;
  return true;
}

Submit Game::submit() {
  if (isOver()) return Submit::Over;
  if (count < LETTERS) return Submit::Incomplete;
  if (!isWord(pack(board[rows]))) return Submit::Unknown;
  scoreRow();
  return Submit::Scored;
}

void Game::scoreRow() {
  uint8_t answer[LETTERS];
  unpack(target, answer);
  score(board[rows], answer, marks[rows]);
  const bool won = pack(board[rows]) == target;
  ++rows;
  count = 0;
  if (won) {
    state = Status::Won;
  } else if (rows == ATTEMPTS) {
    state = Status::Lost;
  }
}

uint8_t Game::letter(const int row, const int column) const {
  if (row < 0 || column < 0 || column >= LETTERS) return NO_LETTER;
  if (row < rows || (row == rows && column < count)) return board[row][column];
  return NO_LETTER;
}

Mark Game::mark(const int row, const int column) const {
  return row >= 0 && row < rows && column >= 0 && column < LETTERS ? marks[row][column] : Mark::None;
}

Mark Game::keyMark(const uint8_t letter) const {
  Mark best = Mark::None;
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < LETTERS; ++c) {
      if (board[r][c] == letter && marks[r][c] > best) best = marks[r][c];
    }
  }
  return best;
}

size_t Game::answerText(char* out, const size_t cap) const {
  if (out == nullptr || cap < 2 * LETTERS + 1) return 0;
  uint8_t letters[LETTERS];
  unpack(target, letters);
  for (int i = 0; i < LETTERS; ++i) {
    const uint32_t cp = codepointOf(letters[i]);
    out[2 * i] = static_cast<char>(0xC0 | (cp >> 6));
    out[2 * i + 1] = static_cast<char>(0x80 | (cp & 0x3F));
  }
  out[2 * LETTERS] = '\0';
  return 2 * LETTERS;
}

size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("answer", target);
  w.number("rows", rows);
  w.beginString("board");
  for (int r = 0; r <= rows && r < ATTEMPTS; ++r) {
    const int n = r < rows ? LETTERS : count;
    for (int c = 0; c < n; ++c) w.put(DIGITS[board[r][c]]);
  }
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t answer = 0;
  uint32_t scored = 0;
  const char* letters = nullptr;
  size_t n = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("answer", answer) || answer >= WORD_LIMIT ||
      !r.number("rows", scored) || scored > ATTEMPTS || !r.string("board", letters, n) || n < scored * LETTERS ||
      n > scored * LETTERS + (scored < ATTEMPTS ? LETTERS : 0)) {
    return false;
  }
  for (size_t i = 0; i < n; ++i) {
    if (digitValue(letters[i]) < 0) return false;
  }
  Game g;
  g.start(answer);
  for (size_t i = 0; i < n; ++i) {
    if (g.isOver()) return false;
    g.board[g.rows][g.count++] = static_cast<uint8_t>(digitValue(letters[i]));
    if (g.count == LETTERS && g.rows < scored) g.scoreRow();
  }
  *this = g;
  return true;
}

}  // namespace fiveletters
