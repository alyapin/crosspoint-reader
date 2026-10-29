#include <HalStorage.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ChessMatch.h"
#include "src/games/chess/ChessSave.h"
#include "src/games/chess/ui/ChessPrefs.h"

using namespace chess;
using chess::ui::PieceSet;

namespace {

constexpr char SAVE[] = "/.crosspoint/games/chess.json";
constexpr char SAVE_TMP[] = "/.crosspoint/games/chess.json.tmp";
constexpr char PREFS[] = "/.crosspoint/games/chess-prefs.json";
constexpr char PREFS_TMP[] = "/.crosspoint/games/chess-prefs.json.tmp";

uint32_t fakeMillis() { return 1000; }

Square sq(const char* name) { return makeSquare(name[0] - 'a', name[1] - '1'); }

std::string contents(const char* path) {
  const auto& bytes = fake::files.at(path)->bytes;
  return {bytes.begin(), bytes.end()};
}

// A hot-seat match after e2e4, and after e7e5 when `second`.
std::unique_ptr<Match> match(const bool second) {
  auto m = std::make_unique<Match>(fakeMillis);
  MatchSettings s;
  s.mode = Mode::HotSeat;
  m->newGame(s, 1);
  EXPECT_TRUE(m->play(sq("e2"), sq("e4")));
  if (second) EXPECT_TRUE(m->play(sq("e7"), sq("e5")));
  m->pause();
  return m;
}

int loadedPlies() {
  auto m = std::make_unique<Match>(fakeMillis);
  EXPECT_TRUE(chesssave::load(*m, 1));
  return m->game().plyCount();
}

ui::Prefs prefs(const PieceSet set) {
  ui::Prefs p;
  p.match.mode = Mode::HotSeat;
  p.pieceSet = set;
  return p;
}

}  // namespace

TEST(ChessSaveFile, RecoversFromTheTmp) {
  fake::reset();
  ASSERT_TRUE(chesssave::save(*match(false)));
  ASSERT_TRUE(Storage.rename(SAVE, SAVE_TMP));
  EXPECT_TRUE(chesssave::hasGameInProgress());
  EXPECT_EQ(loadedPlies(), 1);
  EXPECT_FALSE(Storage.exists(SAVE_TMP));
}

TEST(ChessSaveFile, EmptyTmpIsNoGame) {
  fake::reset();
  fake::add(SAVE_TMP, "");
  EXPECT_FALSE(chesssave::hasGameInProgress());
  EXPECT_FALSE(Storage.exists(SAVE_TMP));
}

TEST(ChessSaveFile, FailedCloseKeepsThePreviousFile) {
  fake::reset();
  ASSERT_TRUE(chesssave::save(*match(false)));
  const std::string before = contents(SAVE);
  fake::failClosePath = SAVE_TMP;
  EXPECT_FALSE(chesssave::save(*match(true)));
  EXPECT_EQ(contents(SAVE), before);
  EXPECT_FALSE(Storage.exists(SAVE_TMP));
}

TEST(ChessSaveFile, FailedRenameKeepsTheOnlyCopy) {
  fake::reset();
  ASSERT_TRUE(chesssave::save(*match(false)));
  fake::failRename = 0;
  EXPECT_FALSE(chesssave::save(*match(true)));
  EXPECT_FALSE(Storage.exists(SAVE));
  EXPECT_EQ(loadedPlies(), 2);
}

TEST(ChessSaveFile, UnreadableSaveIsKept) {
  fake::reset();
  ASSERT_TRUE(chesssave::save(*match(false)));
  const std::string before = contents(SAVE);
  fake::failRead = 0;
  auto m = std::make_unique<Match>(fakeMillis);
  EXPECT_FALSE(chesssave::load(*m, 1));
  EXPECT_FALSE(chesssave::save(*match(true)));
  EXPECT_FALSE(chesssave::clear());
  EXPECT_EQ(contents(SAVE), before);
  EXPECT_EQ(loadedPlies(), 1);
  EXPECT_TRUE(chesssave::save(*match(true)));
  EXPECT_EQ(loadedPlies(), 2);
}

TEST(ChessSaveFile, PrefsRecoverAndKeepTheOnlyCopy) {
  fake::reset();
  ui::savePrefs(prefs(PieceSet::Lucide));
  ASSERT_TRUE(Storage.rename(PREFS, PREFS_TMP));
  EXPECT_EQ(ui::loadPrefs().pieceSet, PieceSet::Lucide);
  EXPECT_EQ(ui::loadPrefs().match.mode, Mode::HotSeat);

  fake::failClosePath = PREFS_TMP;
  ui::savePrefs(prefs(PieceSet::Material));
  EXPECT_EQ(ui::loadPrefs().pieceSet, PieceSet::Lucide);

  fake::failRename = 0;
  ui::savePrefs(prefs(PieceSet::FontAwesome));
  EXPECT_FALSE(Storage.exists(PREFS));
  EXPECT_EQ(ui::loadPrefs().pieceSet, PieceSet::FontAwesome);
}
