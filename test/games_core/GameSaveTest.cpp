#include <Fifteen.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "src/games/GameSave.h"

namespace {

constexpr char BEST[] = "/.crosspoint/games/fifteen-best.json";
constexpr char BEST_TMP[] = "/.crosspoint/games/fifteen-best.json.tmp";
constexpr char SAVE[] = "/.crosspoint/games/fifteen.json";
constexpr char SAVE_TMP[] = "/.crosspoint/games/fifteen.json.tmp";
constexpr char BEST_2048[] = "/.crosspoint/games/2048-best.txt";

uint32_t fakeMillis() { return 0; }

fifteen::Best best(const int moves) {
  fifteen::Best b;
  b.record(moves, 1000);
  return b;
}

std::string contents(const char* path) {
  const auto& bytes = fake::files.at(path)->bytes;
  return {bytes.begin(), bytes.end()};
}

int loadedMoves() {
  fifteen::Best b;
  EXPECT_TRUE(gamesave::load(b));
  return b.moves;
}

}  // namespace

// replaceFile removed the old file, the power went before the rename.
TEST(GameSave, RecordRecoversFromTheTmp) {
  fake::reset();
  ASSERT_TRUE(gamesave::save(best(80)));
  ASSERT_TRUE(Storage.rename(BEST, BEST_TMP));
  EXPECT_EQ(loadedMoves(), 80);
  EXPECT_TRUE(Storage.exists(BEST));
  EXPECT_FALSE(Storage.exists(BEST_TMP));
}

TEST(GameSave, GameInProgressRecoversFromTheTmp) {
  fake::reset();
  fifteen::Game game(&fakeMillis);
  game.newGame(7);
  ASSERT_TRUE(gamesave::save(game));
  ASSERT_TRUE(Storage.rename(SAVE, SAVE_TMP));
  EXPECT_TRUE(gamesave::hasGameInProgress(gamesave::Kind::Fifteen));
  fifteen::Game loaded(&fakeMillis);
  ASSERT_TRUE(gamesave::load(loaded));
  EXPECT_EQ(loaded.gap(), game.gap());
}

TEST(GameSave, EmptyTmpIsNoGame) {
  fake::reset();
  fake::add(SAVE_TMP, "");
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Fifteen));
  EXPECT_FALSE(Storage.exists(SAVE_TMP));
}

TEST(GameSave, ClearDropsTheTmp) {
  fake::reset();
  fake::add(SAVE, "x");
  fake::add(SAVE_TMP, "y");
  gamesave::clear(gamesave::Kind::Fifteen);
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Fifteen));
}

TEST(GameSave, FailedCloseKeepsThePreviousFile) {
  fake::reset();
  ASSERT_TRUE(gamesave::save(best(80)));
  const std::string before = contents(BEST);
  fake::failClosePath = BEST_TMP;
  EXPECT_FALSE(gamesave::save(best(40)));
  EXPECT_EQ(contents(BEST), before);
  EXPECT_FALSE(Storage.exists(BEST_TMP));
}

TEST(GameSave, FailedRenameKeepsTheOnlyCopy) {
  fake::reset();
  ASSERT_TRUE(gamesave::save(best(80)));
  fake::failRename = 0;
  EXPECT_FALSE(gamesave::save(best(40)));
  EXPECT_FALSE(Storage.exists(BEST));
  EXPECT_EQ(loadedMoves(), 40);
}

TEST(GameSave, MissingRecordLoadsEmpty) {
  fake::reset();
  EXPECT_EQ(loadedMoves(), 0);
  EXPECT_TRUE(gamesave::save(best(40)));
  EXPECT_EQ(gamesave::loadBest2048(), 0u);
  gamesave::saveBest2048(16);
  EXPECT_EQ(gamesave::loadBest2048(), 16u);
}

TEST(GameSave, UnreadableRecordIsNotWritten) {
  fake::reset();
  ASSERT_TRUE(gamesave::save(best(80)));
  const std::string before = contents(BEST);
  fake::failRead = 0;
  fifteen::Best b;
  EXPECT_FALSE(gamesave::load(b));
  EXPECT_FALSE(gamesave::save(best(90)));
  EXPECT_EQ(contents(BEST), before);
  EXPECT_EQ(loadedMoves(), 80);
  EXPECT_TRUE(gamesave::save(best(40)));
  EXPECT_EQ(loadedMoves(), 40);
}

TEST(GameSave, Unreadable2048RecordIsNotWritten) {
  fake::reset();
  gamesave::saveBest2048(2048);
  fake::failRead = 0;
  EXPECT_EQ(gamesave::loadBest2048(), 0u);
  gamesave::saveBest2048(16);
  EXPECT_EQ(contents(BEST_2048), "2048");
  EXPECT_EQ(gamesave::loadBest2048(), 2048u);
}
