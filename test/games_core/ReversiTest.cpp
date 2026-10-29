#include <HalStorage.h>
#include <gtest/gtest.h>

#include <string>

#include "Reversi.h"
#include "src/games/GameSave.h"

using namespace reversi;

namespace {

constexpr char SAVE_PATH[] = "/.crosspoint/games/reversi.json";
constexpr char DONE_PATH[] = "/.crosspoint/games/reversi-done.json";

uint32_t frozenClock() { return 0; }

uint32_t ticks = 0;
uint32_t tickingClock() { return ticks += 5; }

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

uint64_t bit(const int col, const int row) { return 1ull << (row * SIZE + col); }

int nthMove(uint64_t moves, uint32_t n) {
  n %= static_cast<uint32_t>(popcount(moves));
  for (int s = 0; s < CELLS; ++s) {
    if ((moves >> s & 1) && n-- == 0) return s;
  }
  return -1;
}

int think(const uint64_t me, const uint64_t opp, const Limits& limits, const uint32_t seed) {
  Engine engine;
  engine.start(me, opp, limits, seed, 0);
  while (!engine.step(&frozenClock, 20)) {
  }
  return engine.move();
}

void playRandom(Game& g, games::Rng& rng, const int count) {
  for (int i = 0; i < count && !g.isOver(); ++i) ASSERT_TRUE(g.play(nthMove(g.moves(), rng.next())));
}

int32_t exactValue(const uint64_t me, const uint64_t opp, const bool passed = false) {
  const uint64_t moves = legalMoves(me, opp);
  if (moves == 0) {
    if (passed) return popcount(me) - popcount(opp);
    return -exactValue(opp, me, true);
  }
  int32_t best = -CELLS - 1;
  for (int s = 0; s < CELLS; ++s) {
    if (!(moves >> s & 1)) continue;
    const uint64_t turned = flips(me, opp, s);
    best = std::max(best, -exactValue(opp ^ turned, me | turned | (1ull << s)));
  }
  return best;
}

}  // namespace

TEST(Reversi, NewGameHasFourDiscsAndFourMoves) {
  Game g;
  g.newGame(Mode::Medium, 1);
  EXPECT_EQ(g.at(27), Disc::White);
  EXPECT_EQ(g.at(28), Disc::Black);
  EXPECT_EQ(g.at(35), Disc::Black);
  EXPECT_EQ(g.at(36), Disc::White);
  EXPECT_EQ(g.toMove(), Disc::Black);
  EXPECT_EQ(g.moves(), (1ull << 19) | (1ull << 26) | (1ull << 37) | (1ull << 44));
  EXPECT_FALSE(g.inProgress());
  EXPECT_FALSE(g.canUndo());
  EXPECT_EQ(g.lastMove(), -1);
}

TEST(Reversi, FlipsInAllEightDirections) {
  uint64_t me = 0;
  uint64_t opp = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0) continue;
      opp |= bit(3 + dx, 3 + dy) | bit(3 + 2 * dx, 3 + 2 * dy);
      me |= bit(3 + 3 * dx, 3 + 3 * dy);
    }
  }
  EXPECT_EQ(flips(me, opp, 3 * SIZE + 3), opp);
  EXPECT_TRUE(legalMoves(me, opp) >> (3 * SIZE + 3) & 1);

  // An open run turns nothing.
  const uint64_t open = me & ~bit(6, 6);
  EXPECT_EQ(flips(open, opp, 3 * SIZE + 3), opp & ~(bit(4, 4) | bit(5, 5)));
}

TEST(Reversi, RunsDoNotWrapAroundTheEdge) {
  EXPECT_EQ(flips(bit(6, 1), bit(7, 1), 2 * SIZE), 0u);
  EXPECT_EQ(flips(bit(1, 4), bit(0, 4), 3 * SIZE + 7), 0u);
  EXPECT_EQ(legalMoves(bit(6, 1), bit(7, 1)), 0u);
}

TEST(Reversi, LegalMovesMatchTheFlips) {
  games::Rng rng(7);
  for (int game = 0; game < 40; ++game) {
    Game g;
    g.newGame(Mode::TwoPlayers, game);
    while (!g.isOver()) {
      const bool black = g.toMove() == Disc::Black;
      const uint64_t me = black ? g.blackDiscs() : g.whiteDiscs();
      const uint64_t opp = black ? g.whiteDiscs() : g.blackDiscs();
      uint64_t expected = 0;
      for (int s = 0; s < CELLS; ++s) {
        if (flips(me, opp, s) != 0) expected |= 1ull << s;
      }
      ASSERT_EQ(g.moves(), expected);
      ASSERT_NE(expected, 0u);
      ASSERT_TRUE(g.play(nthMove(expected, rng.next())));
    }
  }
}

TEST(Reversi, IllegalMovesAreRefused) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  EXPECT_FALSE(g.play(0));
  EXPECT_FALSE(g.play(27));
  EXPECT_FALSE(g.play(-1));
  EXPECT_FALSE(g.play(64));
  ASSERT_TRUE(g.play(19));
  EXPECT_EQ(g.at(27), Disc::Black);
  EXPECT_EQ(g.toMove(), Disc::White);
  EXPECT_EQ(g.lastMove(), 19);
  EXPECT_EQ(g.lastMover(), Disc::Black);
}

TEST(Reversi, PassIsAutomaticAndTheGameEndsWhenNobodyCanMove) {
  games::Rng rng(11);
  int passes = 0;
  int early = 0;
  for (int game = 0; game < 200; ++game) {
    Game g;
    g.newGame(Mode::TwoPlayers, game);
    while (!g.isOver()) {
      const Disc mover = g.toMove();
      ASSERT_TRUE(g.play(nthMove(g.moves(), rng.next())));
      if (g.isOver()) break;
      if (g.toMove() == mover) {
        ++passes;
        EXPECT_EQ(g.passed(), mover == Disc::Black ? Disc::White : Disc::Black);
        const uint64_t other = mover == Disc::Black ? legalMoves(g.whiteDiscs(), g.blackDiscs())
                                                    : legalMoves(g.blackDiscs(), g.whiteDiscs());
        EXPECT_EQ(other, 0u);
      }
    }
    EXPECT_EQ(legalMoves(g.blackDiscs(), g.whiteDiscs()), 0u);
    EXPECT_EQ(legalMoves(g.whiteDiscs(), g.blackDiscs()), 0u);
    EXPECT_EQ(g.moves(), 0u);
    const int b = g.count(Disc::Black);
    const int w = g.count(Disc::White);
    EXPECT_EQ(g.status(), b > w ? Status::BlackWon : b < w ? Status::WhiteWon : Status::Draw);
    early += popcount(g.blackDiscs() | g.whiteDiscs()) < CELLS;
  }
  EXPECT_GT(passes, 0);
  EXPECT_GT(early, 0);
}

TEST(Reversi, UndoAgainstTheComputerTakesBackTheReplyToo) {
  Game g;
  g.newGame(Mode::Easy, 3);
  ASSERT_TRUE(g.play(19));
  EXPECT_TRUE(g.computerToMove());
  ASSERT_TRUE(g.play(think(g.whiteDiscs(), g.blackDiscs(), limitsFor(Mode::Easy), 1)));
  EXPECT_EQ(g.lastMover(), Disc::White);
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(g.movesMade(), 0);
  EXPECT_EQ(g.toMove(), Disc::Black);
  // While the computer thinks, only the player's move goes.
  ASSERT_TRUE(g.play(26));
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(g.movesMade(), 0);
  EXPECT_FALSE(g.undo());
}

TEST(Reversi, TwoPlayersUndoOneMove) {
  Game g;
  g.newGame(Mode::TwoPlayers, 3);
  ASSERT_TRUE(g.play(19));
  ASSERT_TRUE(g.play(18));
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(g.movesMade(), 1);
  EXPECT_EQ(g.toMove(), Disc::White);
  EXPECT_EQ(g.at(18), Disc::None);
}

TEST(Reversi, FinishedGameCountsAndUndoTakesItBack) {
  Game g;
  g.newGame(Mode::TwoPlayers, 5);
  games::Rng rng(5);
  playRandom(g, rng, MAX_MOVES);
  ASSERT_TRUE(g.isOver());
  const Score s = g.score();
  EXPECT_EQ(s.won + s.lost + s.draws, 1);
  EXPECT_FALSE(g.play(0));
  ASSERT_TRUE(g.undo());
  EXPECT_FALSE(g.isOver());
  EXPECT_EQ(g.score().won + g.score().lost + g.score().draws, 0);
}

TEST(Reversi, ComputerPicksALegalMoveOnEveryLevel) {
  for (const Mode mode : {Mode::Easy, Mode::Medium, Mode::Hard}) {
    for (int game = 0; game < 6; ++game) {
      Game g;
      g.newGame(mode, game);
      games::Rng rng(game + 100);
      while (!g.isOver()) {
        int square;
        if (g.computerToMove()) {
          Limits limits = limitsFor(mode);
          limits.maxNodes = std::min<uint32_t>(limits.maxNodes, 20000);
          square = think(g.whiteDiscs(), g.blackDiscs(), limits, g.movesMade());
        } else {
          square = nthMove(g.moves(), rng.next());
        }
        ASSERT_TRUE(g.moves() >> square & 1) << square;
        ASSERT_TRUE(g.play(square));
      }
    }
  }
}

TEST(Reversi, SearchRunsInSlices) {
  Game g;
  g.newGame(Mode::Hard, 2);
  games::Rng rng(2);
  playRandom(g, rng, 20);
  ASSERT_FALSE(g.isOver());
  const bool black = g.toMove() == Disc::Black;
  const uint64_t me = black ? g.blackDiscs() : g.whiteDiscs();
  const uint64_t opp = black ? g.whiteDiscs() : g.blackDiscs();
  Engine engine;
  ticks = 0;
  engine.start(me, opp, limitsFor(Mode::Hard), 1, tickingClock());
  int slices = 0;
  while (!engine.step(&tickingClock, 20)) ++slices;
  EXPECT_GT(slices, 0);
  EXPECT_TRUE(legalMoves(me, opp) >> engine.move() & 1);
  EXPECT_FALSE(engine.thinking());
}

TEST(Reversi, HardSolvesTheEndgameExactly) {
  for (int game = 0; game < 4; ++game) {
    Game g;
    g.newGame(Mode::TwoPlayers, game);
    games::Rng rng(game + 40);
    playRandom(g, rng, MAX_MOVES - 9);
    if (g.isOver()) continue;
    const bool black = g.toMove() == Disc::Black;
    const uint64_t me = black ? g.blackDiscs() : g.whiteDiscs();
    const uint64_t opp = black ? g.whiteDiscs() : g.blackDiscs();
    const int square = think(me, opp, limitsFor(Mode::Hard), 1);
    const uint64_t turned = flips(me, opp, square);
    ASSERT_NE(turned, 0u);
    EXPECT_EQ(-exactValue(opp ^ turned, me | turned | (1ull << square)), exactValue(me, opp)) << game;
  }
}

TEST(Reversi, SaveRoundTrip) {
  Game g;
  g.newGame(Mode::Hard, 99);
  games::Rng rng(9);
  playRandom(g, rng, 17);
  const std::string text = save(g);
  Game loaded;
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_EQ(loaded.blackDiscs(), g.blackDiscs());
  EXPECT_EQ(loaded.whiteDiscs(), g.whiteDiscs());
  EXPECT_EQ(loaded.toMove(), g.toMove());
  EXPECT_EQ(loaded.mode(), Mode::Hard);
  EXPECT_EQ(loaded.seed(), 99u);
  EXPECT_EQ(loaded.movesMade(), 17);
  EXPECT_EQ(loaded.passed(), g.passed());
  EXPECT_EQ(save(loaded), text);
}

TEST(Reversi, TamperedSaveIsRejected) {
  Game g;
  g.newGame(Mode::Medium, 4);
  ASSERT_TRUE(g.play(19));
  ASSERT_TRUE(g.play(18));
  const std::string good = save(g);
  Game before;
  before.newGame(Mode::Easy, 1);
  const std::string untouched = save(before);
  const auto rejects = [&](std::string text) {
    Game target = before;
    EXPECT_FALSE(target.deserialize(text.data(), text.size())) << text;
    EXPECT_EQ(save(target), untouched);
  };
  std::string illegal = good;
  illegal.replace(illegal.find("d3"), 2, "a1");
  rejects(illegal);
  std::string odd = good;
  odd.replace(odd.find("d3"), 2, "d");
  rejects(odd);
  std::string version = good;
  version.replace(version.find("\"v\":1"), 5, "\"v\":7");
  rejects(version);
  std::string mode = good;
  mode.replace(mode.find("\"mode\":1"), 8, "\"mode\":9");
  rejects(mode);
  std::string score = good;
  score.replace(score.find("\"w0\":0"), 6, "\"w0\":70000");
  rejects(score);
  rejects("");
}

TEST(Reversi, SaveFileRoundTripAndTamperedFileIsDropped) {
  fake::reset();
  Game g;
  g.newGame(Mode::Medium, 4);
  ASSERT_TRUE(g.play(19));
  ASSERT_TRUE(gamesave::save(g));
  EXPECT_TRUE(gamesave::hasGameInProgress(gamesave::Kind::Reversi));
  Game loaded;
  ASSERT_TRUE(gamesave::load(loaded));
  EXPECT_EQ(save(loaded), save(g));

  auto& bytes = fake::files.at(SAVE_PATH)->bytes;
  std::string text(bytes.begin(), bytes.end());
  text.replace(text.find("d3"), 2, "h8");
  bytes.assign(text.begin(), text.end());
  Game tampered;
  EXPECT_FALSE(gamesave::load(tampered));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Reversi));
}

TEST(Reversi, FinishedGameKeepsItsScoreInTheSecondFile) {
  fake::reset();
  Game g;
  g.newGame(Mode::TwoPlayers, 5);
  games::Rng rng(5);
  playRandom(g, rng, MAX_MOVES);
  ASSERT_TRUE(g.isOver());
  ASSERT_TRUE(gamesave::save(g));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Reversi));
  EXPECT_TRUE(Storage.exists(DONE_PATH));
  Game loaded;
  ASSERT_TRUE(gamesave::load(loaded));
  EXPECT_TRUE(loaded.isOver());
  EXPECT_EQ(loaded.score().won + loaded.score().lost + loaded.score().draws, 1);
}
