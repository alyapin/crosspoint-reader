#include <HalStorage.h>
#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "ScanwordGame.h"
#include "ScanwordShelf.h"
#include "src/games/scanword/ScanwordStore.h"

using namespace scanword;

namespace {

constexpr char PROGRESS[] = "/.crosspoint/games/scanword/0001.json";
constexpr char PROGRESS_TMP[] = "/.crosspoint/games/scanword/0001.json.tmp";
constexpr char STATS[] = "/.crosspoint/games/scanword/stats.json";
constexpr char STATS_TMP[] = "/.crosspoint/games/scanword/stats.json.tmp";
constexpr char SHELF[] = "/.crosspoint/games/scanword/shelf.txt";
constexpr char SHELF_TMP[] = "/.crosspoint/games/scanword/shelf.txt.tmp";
constexpr char MARKER[] = "/.crosspoint/games/scanword/in-progress";

std::string contents(const char* path) {
  const auto& bytes = fake::files.at(path)->bytes;
  return {bytes.begin(), bytes.end()};
}

std::unique_ptr<Puzzle> samplePuzzle() {
  std::ifstream in(std::string(SCANWORD_SAMPLES_DIR) + "/0001.scw", std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  fake::add("/scanwords/0001.scw", text.str());
  auto puzzle = std::make_unique<Puzzle>();
  EXPECT_TRUE(scanwordstore::loadPuzzle("0001", *puzzle));
  return puzzle;
}

Stats solvedStats(const uint32_t seconds) {
  Stats stats;
  Progress p;
  p.solved = true;
  p.seconds = seconds;
  stats.record(p);
  return stats;
}

uint32_t loadedSeconds() {
  Stats stats;
  EXPECT_TRUE(scanwordstore::loadStats(stats));
  return stats.seconds;
}

}  // namespace

TEST(ScanwordStore, ProgressRecoversFromTheTmp) {
  fake::reset();
  const auto puzzle = samplePuzzle();
  auto game = std::make_unique<Game>(*puzzle);
  game->addSeconds(42);
  ASSERT_TRUE(scanwordstore::saveProgress("0001", *game));
  ASSERT_TRUE(Storage.rename(PROGRESS, PROGRESS_TMP));
  EXPECT_TRUE(scanwordstore::hasProgress("0001"));
  auto loaded = std::make_unique<Game>(*puzzle);
  ASSERT_TRUE(scanwordstore::loadProgress("0001", *loaded));
  EXPECT_EQ(loaded->progress().seconds, 42u);
  EXPECT_FALSE(Storage.exists(PROGRESS_TMP));
}

TEST(ScanwordStore, EmptyTmpIsNoProgress) {
  fake::reset();
  fake::add(PROGRESS_TMP, "");
  EXPECT_FALSE(scanwordstore::hasProgress("0001"));
  EXPECT_FALSE(Storage.exists(PROGRESS_TMP));
}

TEST(ScanwordStore, ClearDropsTheTmp) {
  fake::reset();
  fake::add(PROGRESS, "x");
  fake::add(PROGRESS_TMP, "y");
  scanwordstore::clearProgress("0001");
  EXPECT_FALSE(scanwordstore::hasProgress("0001"));
}

TEST(ScanwordStore, FailedCloseKeepsThePreviousFile) {
  fake::reset();
  ASSERT_TRUE(scanwordstore::saveStats(solvedStats(80)));
  const std::string before = contents(STATS);
  fake::failClosePath = STATS_TMP;
  EXPECT_FALSE(scanwordstore::saveStats(solvedStats(40)));
  EXPECT_EQ(contents(STATS), before);
  EXPECT_FALSE(Storage.exists(STATS_TMP));
}

TEST(ScanwordStore, FailedRenameKeepsTheOnlyCopy) {
  fake::reset();
  ASSERT_TRUE(scanwordstore::saveStats(solvedStats(80)));
  fake::failRename = 0;
  EXPECT_FALSE(scanwordstore::saveStats(solvedStats(40)));
  EXPECT_FALSE(Storage.exists(STATS));
  EXPECT_EQ(loadedSeconds(), 40u);
}

TEST(ScanwordStore, ShelfRecoversFromTheTmp) {
  fake::reset();
  Shelf shelf;
  ASSERT_TRUE(shelf.set("0001", 50, false, 7));
  ASSERT_TRUE(scanwordstore::saveShelf(shelf));
  EXPECT_TRUE(scanwordstore::hasPuzzleInProgress());
  ASSERT_TRUE(Storage.rename(SHELF, SHELF_TMP));
  Shelf loaded;
  ASSERT_TRUE(scanwordstore::loadShelf(loaded));
  ASSERT_NE(loaded.find("0001"), nullptr);
  EXPECT_EQ(loaded.find("0001")->percent, 50);
}

TEST(ScanwordStore, ShelfFailedCloseKeepsThePreviousFile) {
  fake::reset();
  Shelf shelf;
  ASSERT_TRUE(shelf.set("0001", 50, false, 7));
  ASSERT_TRUE(scanwordstore::saveShelf(shelf));
  const std::string before = contents(SHELF);
  ASSERT_TRUE(shelf.set("0001", 90, false, 9));
  fake::failClosePath = SHELF_TMP;
  EXPECT_FALSE(scanwordstore::saveShelf(shelf));
  EXPECT_EQ(contents(SHELF), before);
}

TEST(ScanwordStore, ShelfReportsAFailedMarker) {
  fake::reset();
  Shelf shelf;
  ASSERT_TRUE(shelf.set("0001", 50, false, 7));
  fake::failClosePath = MARKER;
  EXPECT_FALSE(scanwordstore::saveShelf(shelf));
  EXPECT_TRUE(scanwordstore::saveShelf(shelf));
  EXPECT_TRUE(scanwordstore::hasPuzzleInProgress());

  ASSERT_TRUE(shelf.set("0001", 100, true, 9));
  fake::failRemovePath = MARKER;
  EXPECT_FALSE(scanwordstore::saveShelf(shelf));
  EXPECT_TRUE(scanwordstore::saveShelf(shelf));
  EXPECT_FALSE(scanwordstore::hasPuzzleInProgress());
}
