#include <Codeword.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "src/games/GameSave.h"

namespace {

using codeword::Game;
using codeword::Puzzle;

// К=1 О=2 Т=3 Д=4 М=5; О open.
constexpr char KOT[] =
    "CODEWORD 1\n"
    "size 3 3\n"
    "grid\n"
    "КОТ\n"
    "О.О\n"
    "ДОМ\n"
    "code КОТДМ\n"
    "given 2\n"
    "end\n";

constexpr uint8_t K = 11;
constexpr uint8_t O = 15;
constexpr uint8_t T = 19;
constexpr uint8_t D = 5;
constexpr uint8_t M = 13;
constexpr uint8_t E = 6;

uint32_t clockMs = 0;
uint32_t fakeMillis() { return clockMs; }

std::string replaced(std::string text, const std::string& from, const std::string& to) {
  return text.replace(text.find(from), from.size(), to);
}

Game started() {
  Game game(&fakeMillis);
  EXPECT_TRUE(game.start(7, KOT, sizeof(KOT) - 1));
  return game;
}

}  // namespace

TEST(CodewordPuzzle, ParsesNumbersAnswersAndGiven) {
  Puzzle p;
  ASSERT_TRUE(p.parse(KOT, sizeof(KOT) - 1));
  EXPECT_EQ(p.width(), 3);
  EXPECT_EQ(p.height(), 3);
  EXPECT_EQ(p.count(), 5);
  EXPECT_EQ(p.number(0), 1);
  EXPECT_EQ(p.number(4), 0);
  EXPECT_EQ(p.number(8), 5);
  EXPECT_EQ(p.answer(1), K);
  EXPECT_EQ(p.answer(4), D);
  EXPECT_TRUE(p.given(2));
  EXPECT_FALSE(p.given(1));
  EXPECT_EQ(p.givenCount(), 1);
  EXPECT_EQ(codeword::letterCodepoint(K), 0x41Au);
  EXPECT_EQ(codeword::letterCodepoint(0), 0u);
}

TEST(CodewordPuzzle, FoldsYoAndTakesCrlf) {
  Puzzle p;
  const std::string yo = replaced(replaced(KOT, "ДОМ", "ДОЁ"), "КОТДМ", "КОТДЕ");
  ASSERT_TRUE(p.parse(yo.data(), yo.size()));
  EXPECT_EQ(p.answer(5), E);
  std::string crlf;
  for (const char c : std::string(KOT)) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }
  Puzzle plain;
  Puzzle q;
  ASSERT_TRUE(plain.parse(KOT, sizeof(KOT) - 1));
  ASSERT_TRUE(q.parse(crlf.data(), crlf.size()));
  EXPECT_EQ(q.checksum(), plain.checksum());
}

TEST(CodewordPuzzle, RejectsBrokenText) {
  const std::string bad[] = {
      replaced(KOT, "CODEWORD 1", "CODEWORD 2"),
      replaced(KOT, "size 3 3", "size 12 3"),
      replaced(KOT, "О.О", "О.ОО"),
      replaced(KOT, "О.О", "О#О"),
      replaced(KOT, "КОТДМ", "КОТД"),    // М has no number
      replaced(KOT, "КОТДМ", "КОТДММ"),  // a letter twice
      replaced(KOT, "КОТДМ", "КОТДМЯ"),  // a number without a cell
      replaced(KOT, "given 2", "given 6"),
      replaced(KOT, "end\n", ""),
      replaced(KOT, "ДОМ", "дом"),
  };
  for (const std::string& text : bad) {
    Puzzle p;
    EXPECT_FALSE(p.parse(text.data(), text.size())) << text;
  }
}

TEST(CodewordPuzzle, ChecksumFollowsTheGrid) {
  Puzzle a;
  Puzzle b;
  const std::string other = replaced(KOT, "given 2", "given 1");
  ASSERT_TRUE(a.parse(KOT, sizeof(KOT) - 1));
  ASSERT_TRUE(b.parse(other.data(), other.size()));
  EXPECT_NE(a.checksum(), b.checksum());
}

TEST(CodewordIndex, ParsesIdsAndPicksTheNextUnsolved) {
  constexpr char INDEX[] = "CODEWORDS 1\n0001\n0002\n0003\n\n";
  uint16_t ids[4];
  ASSERT_EQ(codeword::parseIndex(INDEX, sizeof(INDEX) - 1, ids, 4), 3);
  EXPECT_EQ(ids[2], 3);
  EXPECT_EQ(codeword::parseIndex("SCANWORDS 1\n1\n", 14, ids, 4), -1);
  EXPECT_EQ(codeword::parseIndex("CODEWORDS 1\nx1\n", 15, ids, 4), -1);

  codeword::Solved solved;
  EXPECT_EQ(codeword::nextPuzzle(ids, 3, solved, 0), 1);
  EXPECT_EQ(codeword::nextPuzzle(ids, 3, solved, 1), 2);
  EXPECT_EQ(codeword::nextPuzzle(ids, 3, solved, 3), 1);
  solved.add(2);
  EXPECT_EQ(codeword::nextPuzzle(ids, 3, solved, 1), 3);
  solved.add(1);
  solved.add(3);
  EXPECT_EQ(codeword::nextPuzzle(ids, 3, solved, 1), 2);
  EXPECT_EQ(codeword::nextPuzzle(ids, 0, solved, 1), 0);
}

TEST(CodewordSolved, RoundTrips) {
  codeword::Solved solved;
  solved.add(1);
  solved.add(40);
  solved.add(codeword::MAX_ID);
  char text[codeword::SOLVED_MAX];
  const size_t len = solved.serialize(text, sizeof(text));
  ASSERT_GT(len, 0u);
  codeword::Solved back;
  ASSERT_TRUE(back.deserialize(text, len));
  EXPECT_TRUE(back.has(1));
  EXPECT_TRUE(back.has(40));
  EXPECT_TRUE(back.has(codeword::MAX_ID));
  EXPECT_FALSE(back.has(2));
  EXPECT_FALSE(back.deserialize("{\"v\":1,\"ids\":\"0g000000\"}", 25));
}

TEST(CodewordGame, StartsWithTheGivenLetter) {
  Game game = started();
  EXPECT_EQ(game.id(), 7);
  EXPECT_EQ(game.letter(2), O);
  EXPECT_TRUE(game.locked(2));
  EXPECT_EQ(game.opened(), 1);
  EXPECT_FALSE(game.inProgress());
  EXPECT_FALSE(game.set(2, K));
  EXPECT_FALSE(game.canHint(2));
}

TEST(CodewordGame, OneLetterPerNumber) {
  Game game = started();
  EXPECT_FALSE(game.set(1, O));  // the given О
  ASSERT_TRUE(game.set(1, T));
  EXPECT_EQ(game.holder(T), 1);
  EXPECT_FALSE(game.set(3, T));
  EXPECT_FALSE(game.set(1, T));
  ASSERT_TRUE(game.set(1, 0));
  EXPECT_EQ(game.holder(T), 0);
  EXPECT_TRUE(game.inProgress());
  EXPECT_FALSE(game.set(6, K));
  EXPECT_FALSE(game.set(1, codeword::LETTER_COUNT + 1));
}

TEST(CodewordGame, UndoStepsBack) {
  Game game = started();
  ASSERT_TRUE(game.set(1, T));
  ASSERT_TRUE(game.set(1, M));
  ASSERT_TRUE(game.set(3, T));
  ASSERT_TRUE(game.undo());
  EXPECT_EQ(game.letter(3), 0);
  ASSERT_TRUE(game.undo());
  EXPECT_EQ(game.letter(1), T);
  ASSERT_TRUE(game.undo());
  EXPECT_EQ(game.letter(1), 0);
  EXPECT_FALSE(game.canUndo());
  EXPECT_FALSE(game.undo());
}

TEST(CodewordGame, HintTakesTheLetterAndUndoSkipsIt) {
  Game game = started();
  ASSERT_TRUE(game.set(3, K));  // К belongs to 1
  ASSERT_TRUE(game.hint(1));
  EXPECT_EQ(game.letter(1), K);
  EXPECT_EQ(game.letter(3), 0);
  EXPECT_TRUE(game.locked(1));
  EXPECT_EQ(game.hints(), 1);
  EXPECT_FALSE(game.canUndo());
  EXPECT_FALSE(game.undo());  // putting К back on 3 would take it from the hint
  EXPECT_EQ(game.letter(3), 0);
}

TEST(CodewordGame, CheckMarksWrongUntilChanged) {
  Game game = started();
  ASSERT_TRUE(game.set(1, K));
  ASSERT_TRUE(game.set(3, D));
  ASSERT_TRUE(game.set(4, M));
  EXPECT_EQ(game.check(), 2);
  EXPECT_FALSE(game.wrong(1));
  EXPECT_TRUE(game.wrong(3));
  EXPECT_TRUE(game.wrong(4));
  ASSERT_TRUE(game.set(3, 0));
  EXPECT_FALSE(game.wrong(3));
  EXPECT_TRUE(game.wrong(4));
}

TEST(CodewordGame, SolvesAndStopsTheClock) {
  clockMs = 1000;
  Game game = started();
  game.resume();
  ASSERT_TRUE(game.set(1, K));
  ASSERT_TRUE(game.set(3, T));
  ASSERT_TRUE(game.set(4, D));
  clockMs = 61000;
  ASSERT_TRUE(game.set(5, M));
  EXPECT_TRUE(game.solved());
  EXPECT_FALSE(game.inProgress());
  clockMs = 900000;
  EXPECT_EQ(game.elapsedMs(), 60000u);
  EXPECT_FALSE(game.set(5, 0));
  EXPECT_FALSE(game.canUndo());
}

TEST(CodewordGame, SaveRoundTripsAndAttachesOnlyItsPuzzle) {
  clockMs = 0;
  Game game = started();
  game.resume();
  ASSERT_TRUE(game.set(1, T));
  ASSERT_TRUE(game.set(4, M));
  ASSERT_TRUE(game.hint(3));
  game.check();
  clockMs = 5000;
  game.pause();
  char text[codeword::SAVE_MAX];
  const size_t len = game.serialize(text, sizeof(text));
  ASSERT_GT(len, 0u);

  Game back(&fakeMillis);
  ASSERT_TRUE(back.deserialize(text, len));
  EXPECT_FALSE(back.attached());
  const std::string other = replaced(KOT, "given 2", "given 1");
  EXPECT_FALSE(back.attach(other.data(), other.size()));
  ASSERT_TRUE(back.attach(KOT, sizeof(KOT) - 1));
  EXPECT_EQ(back.id(), 7);
  EXPECT_EQ(back.letter(1), 0);  // the hint took Т
  EXPECT_EQ(back.letter(3), T);
  EXPECT_TRUE(back.hinted(3));
  EXPECT_TRUE(back.wrong(4));
  EXPECT_EQ(back.elapsedMs(), 5000u);
  EXPECT_TRUE(back.inProgress());
  ASSERT_TRUE(back.undo());
  EXPECT_EQ(back.letter(4), 0);

  EXPECT_FALSE(back.deserialize("{\"v\":1}", 7));
}

TEST(CodewordGame, GameSaveKeepsOnlyAGameInProgress) {
  fake::reset();
  Game game = started();
  ASSERT_TRUE(gamesave::save(game));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Codeword));
  ASSERT_TRUE(game.set(1, K));
  ASSERT_TRUE(gamesave::save(game));
  EXPECT_TRUE(gamesave::hasGameInProgress(gamesave::Kind::Codeword));
  Game loaded(&fakeMillis);
  ASSERT_TRUE(gamesave::load(loaded));
  ASSERT_TRUE(loaded.attach(KOT, sizeof(KOT) - 1));
  EXPECT_EQ(loaded.letter(1), K);

  codeword::Solved solved;
  solved.add(7);
  ASSERT_TRUE(gamesave::save(solved));
  codeword::Solved read;
  ASSERT_TRUE(gamesave::load(read));
  EXPECT_TRUE(read.has(7));
}
