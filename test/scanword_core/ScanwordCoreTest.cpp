#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ScanwordClueLayout.h"
#include "ScanwordGame.h"
#include "ScanwordPuzzle.h"
#include "ScanwordShelf.h"

using namespace scanword;

namespace {

// 5x5 grid, letters need not be real words:
//   # А . # Л      (0,0) RD  АБВО down from (0,1)
//   # Б Ё Е З      (0,3) RD  ЛЗМК down from (0,4), top half
//   # В # Е М      (0,3) D   ЕЕИЙ down from (1,3), bottom half
//   Н О # И К      (1,0) R   БЁЕЗ, (2,0) DR НО, (2,2) R ЕМ, (3,2) R ИК
//   . . . Й .
const char* const FIXTURE =
    "SCANWORD 1\n"
    "size 5 5\n"
    "level 2\n"
    "grid\n"
    "#А.#Л\n"
    "#БЁЕЗ\n"
    "#В#ЕМ\n"
    "НО#ИК\n"
    "...Й.\n"
    "clues\n"
    "0 0 RD Первое\n"
    "0 3 RD Вниз справа\n"
    "0 3 D Вниз\n"
    "1 0 R Вправо\n"
    "2 0 DR Внизу вправо\n"
    "2 2 R Короткое\n"
    "3 2 R Ещё короткое\n"
    "end\n";

enum : uint8_t { W_ABVO, W_LZMK, W_EEIY, W_BYOEZ, W_NO, W_EM, W_IK };

constexpr uint8_t A = 1, B = 2, V = 3, E = 6, YO = 7, Z = 9, I = 10, IY = 11, K = 12, L = 13, M = 14, N = 15, O = 16;

uint16_t at(const int row, const int col) { return static_cast<uint16_t>(row * 5 + col); }

bool parseChunked(const std::string& text, Puzzle& out, const size_t chunk) {
  PuzzleParser parser(out);
  for (size_t i = 0; i < text.size(); i += chunk) {
    if (!parser.feed(text.data() + i, std::min(chunk, text.size() - i))) return false;
  }
  return parser.finish();
}

bool parse(const std::string& text, Puzzle& out) { return parseChunked(text, out, text.size() + 1); }

std::string replaced(std::string text, const std::string& from, const std::string& to) {
  const size_t pos = text.find(from);
  EXPECT_NE(pos, std::string::npos) << from;
  return text.replace(pos, from.size(), to);
}

class Fixture : public ::testing::Test {
 protected:
  void SetUp() override {
    puzzle = std::make_unique<Puzzle>();
    ASSERT_TRUE(parse(FIXTURE, *puzzle));
    game = std::make_unique<Game>(*puzzle);
  }

  void type(std::initializer_list<uint8_t> letters) {
    for (const uint8_t l : letters) game->enter(l);
  }

  std::string save() const {
    char buf[SAVE_MAX];
    const size_t len = game->serialize(buf, sizeof(buf));
    EXPECT_GT(len, 0u);
    return std::string(buf, len);
  }

  std::unique_ptr<Puzzle> puzzle;
  std::unique_ptr<Game> game;
};

}  // namespace

TEST(ScanwordLetters, MapsRussianAlphabet) {
  EXPECT_EQ(letterFromCodepoint(0x410), 1);  // А
  EXPECT_EQ(letterFromCodepoint(0x430), 1);  // а
  EXPECT_EQ(letterFromCodepoint(0x415), LETTER_E);
  EXPECT_EQ(letterFromCodepoint(0x401), LETTER_YO);
  EXPECT_EQ(letterFromCodepoint(0x451), LETTER_YO);
  EXPECT_EQ(letterFromCodepoint(0x416), 8);   // Ж
  EXPECT_EQ(letterFromCodepoint(0x42F), 33);  // Я
  EXPECT_EQ(letterFromCodepoint('A'), NO_LETTER);
  EXPECT_EQ(letterFromCodepoint(0x404), NO_LETTER);  // Ukrainian Є
  for (uint8_t l = 1; l <= LETTER_COUNT; ++l) EXPECT_EQ(letterFromCodepoint(letterCodepoint(l)), l);
  EXPECT_EQ(letterCodepoint(0), 0u);
  EXPECT_EQ(letterCodepoint(34), 0u);
  EXPECT_TRUE(sameLetter(LETTER_E, LETTER_YO));
  EXPECT_TRUE(sameLetter(LETTER_YO, LETTER_E));
  EXPECT_FALSE(sameLetter(NO_LETTER, NO_LETTER));
  EXPECT_FALSE(sameLetter(LETTER_E, 8));
}

TEST_F(Fixture, ParsesGridAndClues) {
  EXPECT_EQ(puzzle->width(), 5);
  EXPECT_EQ(puzzle->height(), 5);
  EXPECT_EQ(puzzle->level(), 2);
  EXPECT_EQ(puzzle->wordCount(), 7);
  EXPECT_EQ(puzzle->letterCount(), 14);
  EXPECT_EQ(puzzle->kind(at(0, 0)), CellKind::Clue);
  EXPECT_EQ(puzzle->kind(at(0, 2)), CellKind::Blank);
  EXPECT_EQ(puzzle->kind(at(1, 2)), CellKind::Letter);
  EXPECT_EQ(puzzle->solution(at(1, 2)), LETTER_YO);
  EXPECT_EQ(puzzle->solution(at(1, 3)), LETTER_E);

  const Word& abvo = puzzle->wordAt(W_ABVO);
  EXPECT_EQ(abvo.arrow, Arrow::RightDown);
  EXPECT_EQ(abvo.dir, Dir::Down);
  EXPECT_EQ(abvo.start, at(0, 1));
  EXPECT_EQ(abvo.length, 4);
  const Word& no = puzzle->wordAt(W_NO);
  EXPECT_EQ(no.arrow, Arrow::DownRight);
  EXPECT_EQ(no.dir, Dir::Across);
  EXPECT_EQ(no.start, at(3, 0));
  EXPECT_EQ(no.length, 2);
  EXPECT_EQ(puzzle->clue(W_IK), "Ещё короткое");

  uint8_t ids[2];
  ASSERT_EQ(puzzle->cluesAt(at(0, 3), ids), 2);
  EXPECT_EQ(ids[0], W_LZMK);
  EXPECT_EQ(ids[1], W_EEIY);
  EXPECT_EQ(puzzle->wordAt(W_EEIY).half, 1);
  EXPECT_EQ(puzzle->cluesAt(at(4, 0), ids), 0);

  EXPECT_EQ(puzzle->wordAt(at(1, 3), Dir::Across), W_BYOEZ);
  EXPECT_EQ(puzzle->wordAt(at(1, 3), Dir::Down), W_EEIY);
  EXPECT_EQ(puzzle->wordAt(at(1, 2), Dir::Down), NO_WORD);
  EXPECT_EQ(puzzle->cellOf(puzzle->wordAt(W_LZMK), 3), at(3, 4));
}

TEST_F(Fixture, ChunkingAndLineEndingsDoNotMatter) {
  const uint32_t sum = puzzle->checksum();
  for (const size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{64}}) {
    Puzzle other;
    ASSERT_TRUE(parseChunked(FIXTURE, other, chunk)) << chunk;
    EXPECT_EQ(other.checksum(), sum);
    EXPECT_EQ(other.clue(W_LZMK), "Вниз справа");
  }
  std::string crlf;
  for (const char* c = FIXTURE; *c != '\0'; ++c) {
    if (*c == '\n') crlf += '\r';
    crlf += *c;
  }
  Puzzle other;
  ASSERT_TRUE(parse(crlf, other));
  EXPECT_EQ(other.checksum(), sum);
  // No newline after "end" is fine too.
  std::string noTrailing = FIXTURE;
  noTrailing.pop_back();
  ASSERT_TRUE(parse(noTrailing, other));
}

TEST(ScanwordParser, RejectsCorruptFiles) {
  const std::string good = FIXTURE;
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"", "truncated"},
      {replaced(good, "SCANWORD 1", "SCANWORD 2"), "not a scanword v1 file"},
      {good.substr(0, good.size() - 4), "truncated"},
      {replaced(good, "size 5 5", "size 5 50"), "size out of range"},
      {replaced(good, "size 5 5", "size 5"), "bad size"},
      {replaced(good, "level 2", "level 0"), "bad level"},
      {replaced(good, "#А.#Л", "#A.#Л"), "bad grid cell"},
      {replaced(good, "#А.#Л", "#Ѣ.#Л"), "bad grid letter"},
      {replaced(good, "#А.#Л", "#А.#"), "grid row too short"},
      {replaced(good, "#А.#Л", "#А.#ЛЛ"), "grid row too long"},
      {replaced(replaced(good, "2 2 R Короткое\n", ""), "#В#ЕМ", "#В.ЕМ"), "letter run without a clue"},
      {replaced(replaced(good, "0 0 RD Первое\n", ""), "#А.#Л", ".А.#Л"), "letter in no answer"},
      {replaced(good, "1 0 R Вправо", "1 1 R Вправо"), "clue on a non-clue cell"},
      {replaced(good, "1 0 R Вправо", "1 0 X Вправо"), "bad arrow"},
      {replaced(good, "1 0 R Вправо", "1 0 R"), "empty clue"},
      {replaced(good, "1 0 R Вправо", "9 0 R Вправо"), "clue outside grid"},
      {replaced(good, "2 2 R Короткое", "2 2 R Короткое\n2 2 R Дубль"), "two clues for one answer"},
      {replaced(good, "0 3 D Вниз", "0 3 D Вниз\n0 3 R Третья"), "more than two clues in a cell"},
      {replaced(good, "2 0 DR Внизу вправо", "2 0 D Внизу вправо"), "answer too short"},
      {replaced(good, "0 0 RD Первое", "4 3 R Первое"), "clue on a non-clue cell"},
      {replaced(good, "0 0 RD Первое", "2 2 D Первое"), "answer starts on a non-letter"},
      {replaced(good, "3 2 R Ещё короткое", "3 2 R " + std::string(130, 'x')), "line too long"},
      {replaced(good, "3 2 R Ещё короткое", "3 2 R " + std::string(90, 'x')), "clue too long"},
      {replaced(good, "3 2 R Ещё короткое", "3 2 R a\tb"), "control character in clue"},
      {good + "extra\n", "data after end"},
      {replaced(good, "#А.#Л", "#А##Л"), "clue cell without a clue"},
  };
  for (const auto& [text, why] : cases) {
    Puzzle p;
    PuzzleParser parser(p);
    const bool ok = parser.feed(text.data(), text.size()) && parser.finish();
    EXPECT_FALSE(ok) << why;
    EXPECT_STREQ(parser.error(), why.c_str()) << why;
  }
}

TEST(ScanwordParser, RejectsAnswerInsideARun) {
  // (1,0) R would start at (1,1), but (1,1)'s left neighbour is a letter here.
  const std::string text = "SCANWORD 1\nsize 3 3\nlevel 1\ngrid\n#..\nАБВ\n...\nclues\n0 0 DR x\n0 0 D y\nend\n";
  Puzzle p;
  EXPECT_FALSE(parse(text, p));
}

TEST_F(Fixture, StartsOnFirstAnswer) {
  EXPECT_EQ(game->selectedWord(), W_ABVO);
  EXPECT_EQ(game->cursor(), at(0, 1));
  EXPECT_EQ(game->filledCount(), 0);
  EXPECT_FALSE(game->solved());
}

TEST_F(Fixture, TapSelectsAndTogglesAtCrossings) {
  ASSERT_TRUE(game->tapCell(at(1, 3)));  // crossing of БЁЕЗ and ЕЕИЙ; keeps current direction (down)
  EXPECT_EQ(game->selectedWord(), W_EEIY);
  EXPECT_EQ(game->cursor(), at(1, 3));
  ASSERT_TRUE(game->tapCell(at(1, 3)));  // same cell again toggles
  EXPECT_EQ(game->selectedWord(), W_BYOEZ);
  ASSERT_TRUE(game->tapCell(at(1, 2)));  // inside the selection: cursor moves, word stays
  EXPECT_EQ(game->selectedWord(), W_BYOEZ);
  EXPECT_EQ(game->cursor(), at(1, 2));
  ASSERT_TRUE(game->tapCell(at(1, 2)));  // no down answer here: nothing to toggle
  EXPECT_EQ(game->selectedWord(), W_BYOEZ);
  ASSERT_TRUE(game->tapCell(at(0, 4)));  // only a down answer
  EXPECT_EQ(game->selectedWord(), W_LZMK);
  EXPECT_FALSE(game->tapCell(at(0, 2)));  // blank
  EXPECT_FALSE(game->tapCell(at(9, 9)));  // outside
  EXPECT_FALSE(game->toggleDirection());
  // Without toggling a tap on the cursor keeps the answer.
  ASSERT_TRUE(game->tapCell(at(1, 3)));
  ASSERT_TRUE(game->tapCell(at(1, 3), false));
  EXPECT_EQ(game->selectedWord(), W_EEIY);
}

TEST_F(Fixture, TapOnTwoClueCellCycles) {
  ASSERT_TRUE(game->tapCell(at(0, 3)));
  EXPECT_EQ(game->selectedWord(), W_LZMK);
  EXPECT_EQ(game->cursor(), at(0, 4));
  ASSERT_TRUE(game->tapCell(at(0, 3)));
  EXPECT_EQ(game->selectedWord(), W_EEIY);
  EXPECT_EQ(game->cursor(), at(1, 3));
  ASSERT_TRUE(game->tapCell(at(0, 3)));
  EXPECT_EQ(game->selectedWord(), W_LZMK);
  ASSERT_TRUE(game->tapCell(at(2, 0)));  // single bent clue
  EXPECT_EQ(game->selectedWord(), W_NO);
  EXPECT_EQ(game->cursor(), at(3, 0));
}

TEST_F(Fixture, EntryAdvancesAndCrossingsShareLetters) {
  game->selectWord(W_BYOEZ);
  type({B, E, YO, Z});  // Е for Ё and Ё for Е both match
  EXPECT_EQ(game->cursor(), at(1, 4));
  EXPECT_EQ(game->entry(at(1, 2)), LETTER_E);  // shown as typed
  EXPECT_EQ(game->entry(at(1, 3)), LETTER_YO);
  EXPECT_EQ(game->checkWord(), 0);
  EXPECT_TRUE(game->wordFilled(W_BYOEZ));
  EXPECT_FALSE(game->wordFilled(W_ABVO));
  // The crossing answer sees the shared letters.
  game->selectWord(W_ABVO);
  EXPECT_EQ(game->cursor(), at(0, 1));  // first empty cell
  type({A});
  EXPECT_EQ(game->cursor(), at(1, 1));  // advances onto the shared, filled cell
  game->selectWord(W_ABVO);
  EXPECT_EQ(game->cursor(), at(2, 1));
  EXPECT_EQ(game->filledCount(), 5);
}

TEST_F(Fixture, RejectsInvalidLetters) {
  EXPECT_EQ(game->enter(0), EntryResult::Ignored);
  EXPECT_EQ(game->enter(34), EntryResult::Ignored);
  EXPECT_EQ(game->filledCount(), 0);
}

TEST_F(Fixture, EraseAndUndo) {
  game->selectWord(W_BYOEZ);
  type({B, V});
  EXPECT_EQ(game->cursor(), at(1, 3));
  ASSERT_TRUE(game->erase());  // cursor cell empty: clears the previous one
  EXPECT_EQ(game->cursor(), at(1, 2));
  EXPECT_EQ(game->entry(at(1, 2)), NO_LETTER);
  ASSERT_TRUE(game->undo());  // brings В back
  EXPECT_EQ(game->entry(at(1, 2)), V);
  EXPECT_EQ(game->cursor(), at(1, 3));
  ASSERT_TRUE(game->undo());  // removes В
  EXPECT_EQ(game->entry(at(1, 2)), NO_LETTER);
  EXPECT_EQ(game->cursor(), at(1, 2));
  ASSERT_TRUE(game->undo());  // removes Б
  EXPECT_EQ(game->entry(at(1, 1)), NO_LETTER);
  EXPECT_FALSE(game->undo());
  EXPECT_FALSE(game->canUndo());
}

TEST_F(Fixture, UndoSelectsTheAnswerOfTheRestoredCell) {
  game->selectWord(W_NO);
  type({N});
  game->selectWord(W_BYOEZ);
  ASSERT_TRUE(game->undo());
  EXPECT_EQ(game->cursor(), at(3, 0));
  EXPECT_EQ(game->selectedWord(), W_NO);
}

TEST_F(Fixture, UndoHistoryIsBounded) {
  for (int i = 0; i < Game::UNDO_MAX + 10; ++i) {
    game->selectWord(W_NO);
    game->enter(static_cast<uint8_t>(1 + i % 30));
  }
  int undone = 0;
  while (game->undo()) ++undone;
  EXPECT_EQ(undone, Game::UNDO_MAX);
}

TEST_F(Fixture, CheckMarksWrongLettersUntilChanged) {
  game->selectWord(W_BYOEZ);
  type({B, A, A});
  EXPECT_EQ(game->checkWord(), 2);
  EXPECT_EQ(game->flags(at(1, 2)) & FLAG_WRONG, FLAG_WRONG);
  EXPECT_EQ(game->flags(at(1, 1)) & FLAG_WRONG, 0);
  game->tapCell(at(1, 2));
  game->enter(YO);
  EXPECT_EQ(game->flags(at(1, 2)) & FLAG_WRONG, 0);
  EXPECT_EQ(game->checkAll(), 1);
  EXPECT_EQ(game->progress().checks, 2);
}

TEST_F(Fixture, RevealLocksCellsAndCountsHints) {
  game->selectWord(W_BYOEZ);
  type({A});  // wrong first letter
  game->tapCell(at(1, 1));
  ASSERT_EQ(game->cursor(), at(1, 1));
  EXPECT_EQ(game->revealLetter(), EntryResult::Entered);
  EXPECT_EQ(game->entry(at(1, 1)), B);
  EXPECT_EQ(game->flags(at(1, 1)), FLAG_REVEALED);
  EXPECT_EQ(game->progress().hints, 1);
  EXPECT_EQ(game->cursor(), at(1, 2));
  // Typing skips the locked cell.
  game->tapCell(at(1, 1));
  game->enter(YO);
  EXPECT_EQ(game->entry(at(1, 1)), B);
  EXPECT_EQ(game->entry(at(1, 2)), YO);
  // Erase never clears a revealed cell, and undo skips it.
  game->tapCell(at(1, 2));
  EXPECT_TRUE(game->erase());
  EXPECT_FALSE(game->erase());
  EXPECT_EQ(game->entry(at(1, 1)), B);
  while (game->undo()) {
  }
  EXPECT_EQ(game->entry(at(1, 1)), B);
  game->tapCell(at(1, 3));
  game->enter(A);  // wrong: Е
  EXPECT_TRUE(game->canUndo());
  EXPECT_EQ(game->revealWord(), EntryResult::Entered);
  EXPECT_FALSE(game->canUndo());  // only records of now-revealed cells are left
  EXPECT_EQ(game->progress().hints, 4);
  EXPECT_EQ(game->revealWord(), EntryResult::Ignored);
  EXPECT_EQ(game->entry(at(1, 2)), YO);
}

TEST_F(Fixture, RevealLetterPicksFirstWrongCellWhenCursorIsCorrect) {
  game->selectWord(W_EM);
  type({E});
  game->tapCell(at(2, 3));
  EXPECT_EQ(game->revealLetter(), EntryResult::Entered);
  EXPECT_EQ(game->entry(at(2, 4)), M);
  EXPECT_EQ(game->flags(at(2, 3)), 0);
  EXPECT_EQ(game->cursor(), at(2, 3));  // the word has no empty cell left: back to its start
}

TEST_F(Fixture, SolvingEndsTheGame) {
  const struct {
    uint8_t word;
    std::vector<uint8_t> letters;
  } answers[] = {
      {W_ABVO, {A, B, V, O}},
      {W_LZMK, {L, Z, M, K}},
      {W_EEIY, {E, E, I, IY}},
      {W_BYOEZ, {B, YO, E, Z}},
  };
  for (const auto& a : answers) {
    game->selectWord(a.word);
    game->tapCell(game->puzzle().wordAt(a.word).start);
    if (game->selectedWord() != a.word) game->toggleDirection();
    ASSERT_EQ(game->selectedWord(), a.word);
    for (const uint8_t l : a.letters) EXPECT_EQ(game->enter(l), EntryResult::Entered);
  }
  game->addSeconds(30);
  game->selectWord(W_NO);  // only Н is missing; О came with АБВО
  EXPECT_EQ(game->cursor(), at(3, 0));
  EXPECT_EQ(game->enter(N), EntryResult::Solved);
  EXPECT_TRUE(game->solved());
  EXPECT_TRUE(game->progress().solved);
  EXPECT_EQ(game->enter(A), EntryResult::Ignored);
  EXPECT_FALSE(game->erase());
  EXPECT_FALSE(game->undo());
  EXPECT_EQ(game->revealLetter(), EntryResult::Ignored);
  EXPECT_EQ(game->checkAll(), 0);
  game->addSeconds(30);
  EXPECT_EQ(game->progress().seconds, 30u);
}

TEST_F(Fixture, MoveCursorKeepsDirection) {
  game->selectWord(W_BYOEZ);            // cursor (1,1), across
  ASSERT_TRUE(game->moveCursor(1, 0));  // (2,1): only the down answer АБВО
  EXPECT_EQ(game->cursor(), at(2, 1));
  EXPECT_EQ(game->selectedWord(), W_ABVO);
  ASSERT_TRUE(game->moveCursor(0, 1));  // skips the clue cell to (2,3); keeps down
  EXPECT_EQ(game->cursor(), at(2, 3));
  EXPECT_EQ(game->selectedWord(), W_EEIY);
  ASSERT_TRUE(game->toggleDirection());
  EXPECT_EQ(game->selectedWord(), W_EM);
  ASSERT_TRUE(game->moveCursor(0, 1));
  EXPECT_EQ(game->cursor(), at(2, 4));
  EXPECT_FALSE(game->moveCursor(0, 1));  // border
  EXPECT_FALSE(game->moveCursor(0, 0));
}

TEST_F(Fixture, NextWordWraps) {
  game->nextWord(-1);
  EXPECT_EQ(game->selectedWord(), W_IK);
  game->nextWord(1);
  EXPECT_EQ(game->selectedWord(), W_ABVO);
  game->nextWord(3);
  EXPECT_EQ(game->selectedWord(), W_BYOEZ);
}

TEST_F(Fixture, SaveRoundTrip) {
  game->selectWord(W_BYOEZ);
  type({B, A});
  game->checkWord();
  game->selectWord(W_LZMK);
  game->revealLetter();
  game->addSeconds(75);
  const std::string text = save();

  Game restored(*puzzle);
  ASSERT_TRUE(restored.deserialize(text.data(), text.size()));
  for (uint16_t cell = 0; cell < puzzle->cellCount(); ++cell) {
    EXPECT_EQ(restored.entry(cell), game->entry(cell)) << cell;
    EXPECT_EQ(restored.flags(cell), game->flags(cell)) << cell;
  }
  EXPECT_EQ(restored.selectedWord(), game->selectedWord());
  EXPECT_EQ(restored.cursor(), game->cursor());
  EXPECT_EQ(restored.progress().seconds, 75u);
  EXPECT_EQ(restored.progress().hints, 1);
  EXPECT_EQ(restored.progress().checks, 1);
  EXPECT_FALSE(restored.solved());
  EXPECT_FALSE(restored.canUndo());

  char tiny[16];
  EXPECT_EQ(game->serialize(tiny, sizeof(tiny)), 0u);
}

TEST_F(Fixture, SaveOfSolvedPuzzleRoundTrips) {
  for (uint8_t w = 0; w < puzzle->wordCount(); ++w) {
    game->selectWord(w);
    game->revealWord();
  }
  ASSERT_TRUE(game->solved());
  const std::string text = save();
  Game restored(*puzzle);
  ASSERT_TRUE(restored.deserialize(text.data(), text.size()));
  EXPECT_TRUE(restored.solved());
  EXPECT_EQ(restored.progress().hints, puzzle->letterCount());
}

TEST_F(Fixture, RejectsCorruptSaves) {
  game->selectWord(W_BYOEZ);
  type({B, A});
  game->selectWord(W_NO);
  game->revealLetter();
  const std::string good = save();
  const std::string cells = good.substr(good.find("\"cells\":\"") + 9, puzzle->cellCount());
  const std::string flags = good.substr(good.find("\"flags\":\"") + 9, puzzle->cellCount());
  ASSERT_EQ(cells[at(1, 1)], 'B');
  ASSERT_EQ(flags[at(3, 0)], '1');

  Puzzle other;
  ASSERT_TRUE(parse(replaced(FIXTURE, "...Й.", "...Л."), other));
  const std::string otherSum = "\"sum\":" + std::to_string(other.checksum());
  const std::string sum = "\"sum\":" + std::to_string(puzzle->checksum());

  std::string wrongReveal = flags;
  wrongReveal[at(1, 2)] = '1';  // revealed, but holds А instead of Ё
  std::string clueEntry = cells;
  clueEntry[at(0, 0)] = 'A';
  std::string badChar = cells;
  badChar[at(1, 1)] = '?';
  std::string badFlag = flags;
  badFlag[at(1, 1)] = '7';
  std::string wrongOnEmpty = flags;
  wrongOnEmpty[at(4, 3)] = '2';
  std::string wrongOnRight = flags;
  wrongOnRight[at(1, 1)] = '2';  // Б is correct

  const std::vector<std::pair<std::string, std::string>> cases = {
      {"", "empty"},
      {good.substr(0, good.size() / 2), "truncated"},
      {replaced(good, "\"v\":1", "\"v\":2"), "version"},
      {replaced(good, sum, otherSum), "other puzzle"},
      {replaced(good, cells, cells.substr(1)), "short cells"},
      {replaced(good, flags, wrongReveal), "revealed wrong letter"},
      {replaced(good, cells, clueEntry), "letter in a clue cell"},
      {replaced(good, cells, badChar), "bad letter char"},
      {replaced(good, flags, badFlag), "bad flag"},
      {replaced(good, flags, wrongOnEmpty), "wrong mark on an empty cell"},
      {replaced(good, flags, wrongOnRight), "wrong mark on a correct letter"},
      {replaced(good, "\"solved\":0", "\"solved\":1"), "solved mismatch"},
      {replaced(good, "\"sel\":4", "\"sel\":99"), "selection range"},
      {replaced(good, "\"cur\":16", "\"cur\":1"), "cursor outside selection"},
      {replaced(good, "\"time\":0", "\"time\":99999999999"), "time overflow"},
  };
  for (const auto& [text, name] : cases) {
    Game restored(*puzzle);
    restored.selectWord(W_IK);
    restored.enter(I);
    EXPECT_FALSE(restored.deserialize(text.data(), text.size())) << name;
    EXPECT_EQ(restored.filledCount(), 0) << name;  // reset on failure
    EXPECT_EQ(restored.selectedWord(), W_ABVO) << name;
  }
  EXPECT_FALSE(game->deserialize(nullptr, 10));
}

TEST(ScanwordStats, RecordsSolvedPuzzlesAndRoundTrips) {
  Stats s;
  Progress p;
  p.seconds = 300;
  s.record(p);  // unsolved: ignored
  EXPECT_EQ(s.solved, 0u);
  p.solved = true;
  s.record(p);
  p.seconds = 200;
  p.hints = 3;
  s.record(p);  // hints: not a best time
  p.seconds = 250;
  p.hints = 0;
  s.record(p);
  EXPECT_EQ(s.solved, 3u);
  EXPECT_EQ(s.seconds, 750u);
  EXPECT_EQ(s.hints, 3u);
  EXPECT_EQ(s.bestSeconds, 250u);

  char buf[STATS_MAX];
  const size_t len = s.serialize(buf, sizeof(buf));
  ASSERT_GT(len, 0u);
  Stats back;
  ASSERT_TRUE(back.deserialize(buf, len));
  EXPECT_EQ(back.solved, 3u);
  EXPECT_EQ(back.seconds, 750u);
  EXPECT_EQ(back.bestSeconds, 250u);
  EXPECT_EQ(back.hints, 3u);
  Stats max;
  max.solved = max.seconds = max.bestSeconds = max.hints = UINT32_MAX;
  EXPECT_GT(max.serialize(buf, sizeof(buf)), 0u);

  const char* partial = "{\"v\":1,\"solved\":2}";
  EXPECT_FALSE(back.deserialize(partial, strlen(partial)));
  EXPECT_EQ(back.solved, 0u);
}

namespace {

struct Collected {
  std::vector<std::pair<std::string, int>> entries;
  size_t limit = 100;
};

bool collect(void* ctx, const IndexEntry& e) {
  auto* c = static_cast<Collected*>(ctx);
  c->entries.emplace_back(e.id, e.level);
  return c->entries.size() < c->limit;
}

bool parseIndex(const std::string& text, Collected& c, const size_t chunk = 5) {
  IndexParser parser(collect, &c);
  for (size_t i = 0; i < text.size(); i += chunk) {
    if (!parser.feed(text.data() + i, std::min(chunk, text.size() - i))) break;
  }
  return parser.finish();
}

}  // namespace

TEST(ScanwordIndex, ParsesAndStops) {
  Collected c;
  ASSERT_TRUE(parseIndex("SCANWORDS 1\r\n0001 1\n0002 3\n\nabc_-Z 2", c));
  ASSERT_EQ(c.entries.size(), 3u);
  EXPECT_EQ(c.entries[0], std::make_pair(std::string("0001"), 1));
  EXPECT_EQ(c.entries[2], std::make_pair(std::string("abc_-Z"), 2));

  Collected first;
  first.limit = 1;
  EXPECT_TRUE(parseIndex("SCANWORDS 1\n0001 1\n0002 3\n", first));
  EXPECT_EQ(first.entries.size(), 1u);

  for (const char* bad : {"", "0001 1\n", "SCANWORDS 1\n../x 1\n", "SCANWORDS 1\n0001\n", "SCANWORDS 1\n0001 0\n",
                          "SCANWORDS 1\n0001 1 extra\n", "SCANWORDS 1\n12345678901234567 1\n"}) {
    Collected b;
    EXPECT_FALSE(parseIndex(bad, b)) << bad;
  }
  EXPECT_TRUE(validId("0001"));
  EXPECT_FALSE(validId(""));
  EXPECT_FALSE(validId(nullptr));
  EXPECT_FALSE(validId("a/b"));
  EXPECT_FALSE(validId("12345678901234567"));
}

// Every sample pack puzzle loads and can be solved by typing its own answers.
TEST(ScanwordSamples, LoadAndSolve) {
  const std::filesystem::path dir = SCANWORD_SAMPLES_DIR;
  std::ifstream index(dir / "index.txt");
  ASSERT_TRUE(index.good());
  std::stringstream indexText;
  indexText << index.rdbuf();
  Collected ids;
  ASSERT_TRUE(parseIndex(indexText.str(), ids));
  ASSERT_GE(ids.entries.size(), 3u);
  for (const auto& [id, level] : ids.entries) {
    std::ifstream file(dir / (id + ".scw"), std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    auto p = std::make_unique<Puzzle>();
    ASSERT_TRUE(parseChunked(text.str(), *p, 61)) << id;
    EXPECT_EQ(p->level(), level);
    auto g = std::make_unique<Game>(*p);
    EntryResult last = EntryResult::Ignored;
    for (uint8_t w = 0; w < p->wordCount() && last != EntryResult::Solved; ++w) {
      const Word& word = p->wordAt(w);
      ASSERT_GE(word.length, 3) << id;
      ASSERT_TRUE(g->selectWord(w));
      // The cursor sits on the first empty cell; earlier ones came from crossings.
      uint8_t from = 0;
      while (p->cellOf(word, from) != g->cursor()) ++from;
      for (uint8_t i = from; i < word.length && last != EntryResult::Solved; ++i) {
        last = g->enter(foldLetter(p->solution(p->cellOf(word, i))));
      }
    }
    EXPECT_EQ(last, EntryResult::Solved) << id;
    EXPECT_EQ(g->filledCount(), p->letterCount()) << id;
  }
}

namespace {

// Every character is 6 px wide.
int fixedWidth(void*, const char* text) {
  int n = 0;
  for (const char* p = text; *p != '\0'; ++p) n += (static_cast<uint8_t>(*p) & 0xC0) != 0x80 ? 6 : 0;
  return n;
}

std::vector<std::string> layout(const char* text, const int chars, const uint8_t lines = 3) {
  ClueLines out;
  layoutClue(text, chars * 6, lines, fixedWidth, nullptr, out);
  return std::vector<std::string>(out.line, out.line + out.count);
}

}  // namespace

TEST(ScanwordClueLayout, WrapsWords) {
  EXPECT_EQ(layout("Часть суток", 8), (std::vector<std::string>{"Часть", "суток"}));
  EXPECT_EQ(layout("Кот", 8), (std::vector<std::string>{"Кот"}));
  EXPECT_EQ(layout("  Два   пробела ", 20), (std::vector<std::string>{"Два пробела"}));
  EXPECT_TRUE(layout("", 8).empty());
}

TEST(ScanwordClueLayout, HyphenatesLongWords) {
  // Гиппопотам: the longest prefix that fits with "-" and keeps a vowel on both sides.
  EXPECT_EQ(layout("Гиппопотам", 8), (std::vector<std::string>{"Гиппопо-", "там"}));
  // A word that fits a line moves down whole; a longer one starts on the current line.
  EXPECT_EQ(layout("Самец домашней кошки", 9), (std::vector<std::string>{"Самец", "домашней", "кошки"}));
  EXPECT_EQ(layout("Кот гиппопотам", 8), (std::vector<std::string>{"Кот гип-", "попотам"}));
  // Ь never starts a part: "Мель-ник", not "Мел-ьник".
  EXPECT_EQ(layout("Мельник", 5), (std::vector<std::string>{"Мель-", "ник"}));
}

TEST(ScanwordClueLayout, EllipsisWhenTooLong) {
  const auto lines = layout("Очень длинное определение слова", 7);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[2].substr(lines[2].size() - 3), "\xE2\x80\xA6");
  EXPECT_LE(fixedWidth(nullptr, lines[2].c_str()), 7 * 6);
  // Without vowels on both sides a word is cut by width.
  const auto forced = layout("Бвгджзклмн", 4, 3);
  EXPECT_EQ(forced[0], "Бвг-");
}

TEST(ScanwordShelf, SetFindAndRoundTrip) {
  auto shelf = std::make_unique<Shelf>();
  EXPECT_TRUE(shelf->set("0001", 40, false, 300));
  EXPECT_TRUE(shelf->set("0002", 100, true, 1080));
  EXPECT_TRUE(shelf->set("0001", 55, false, 360));
  EXPECT_FALSE(shelf->set("../x", 1, false, 1));
  EXPECT_EQ(shelf->count(), 2);
  EXPECT_EQ(shelf->find("0001")->percent, 55);
  EXPECT_EQ(shelf->find("0003"), nullptr);
  EXPECT_TRUE(shelf->anyInProgress());

  std::string text = std::string(Shelf::header()) + "\n";
  char line[Shelf::LINE_CAP];
  for (uint16_t i = 0; i < shelf->count(); ++i) {
    const size_t n = shelf->formatEntry(i, line, sizeof(line));
    ASSERT_GT(n, 0u);
    text += std::string(line, n) + "\n";
  }
  auto back = std::make_unique<Shelf>();
  back->beginParse();
  for (size_t i = 0; i < text.size(); i += 3) back->feed(text.data() + i, std::min<size_t>(3, text.size() - i));
  ASSERT_TRUE(back->finish());
  ASSERT_EQ(back->count(), 2);
  EXPECT_EQ(back->find("0002")->seconds, 1080u);
  EXPECT_TRUE(back->find("0002")->solved);
  EXPECT_TRUE(back->anyInProgress());
  EXPECT_FALSE(back->find("0001")->solved);
  back->remove("0001");
  EXPECT_EQ(back->find("0001"), nullptr);
  EXPECT_FALSE(back->anyInProgress());
}

TEST(ScanwordShelf, RejectsCorruptText) {
  for (const char* bad :
       {"", "0001 1 0 5\n", "SCANWORD-SHELF 1\n0001 101 0 5\n", "SCANWORD-SHELF 1\n0001 1 2 5\n",
        "SCANWORD-SHELF 1\n0001 1 0\n", "SCANWORD-SHELF 1\na/b 1 0 5\n", "SCANWORD-SHELF 1\n0001 1 0 5 x\n"}) {
    auto shelf = std::make_unique<Shelf>();
    shelf->beginParse();
    shelf->feed(bad, strlen(bad));
    EXPECT_FALSE(shelf->finish()) << bad;
    EXPECT_EQ(shelf->count(), 0) << bad;
  }
}
