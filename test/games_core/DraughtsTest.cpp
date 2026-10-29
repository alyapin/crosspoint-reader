#include <HalStorage.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "Draughts.h"
#include "DraughtsEngine.h"
#include "GameCommon.h"
#include "src/games/GameSave.h"

using namespace draughts;

namespace {

constexpr char SAVE[] = "/.crosspoint/games/draughts.json";

int sq(const char* name) { return squareAt('8' - name[1], name[0] - 'a'); }

// 64 cells from the top row: '.' empty, w/W white man/king, b/B black.
Position board(const char* cells, const Side side = White) {
  Position p;
  p.side = side;
  for (int i = 0; i < 64; ++i) {
    const char c = cells[i];
    if (c == '.') continue;
    const int s = squareAt(i / 8, i % 8);
    EXPECT_GE(s, 0) << "piece on a light cell " << i;
    if (s < 0) continue;
    (c == 'w' || c == 'W' ? p.white : p.black) |= 1u << s;
    if (c == 'W' || c == 'B') p.kings |= 1u << s;
  }
  return p;
}

std::vector<Move> movesOf(const Position& p) {
  std::vector<Move> list(MAX_MOVES);
  list.resize(legalMoves(p, list.data(), MAX_MOVES));
  return list;
}

std::string text(const Move& m) {
  std::string s;
  auto name = [&](const int square) {
    s += static_cast<char>('a' + colOf(square));
    s += static_cast<char>('8' - rowOf(square));
  };
  name(m.from);
  for (int k = 0; k < m.stops(); ++k) {
    s += m.jumps > 0 ? ':' : '-';
    name(m.stopAt(k));
  }
  return s;
}

std::vector<std::string> texts(const Position& p) {
  std::vector<std::string> out;
  for (const Move& m : movesOf(p)) out.push_back(text(m));
  std::sort(out.begin(), out.end());
  return out;
}

const Move* find(const std::vector<Move>& list, const std::string& name) {
  for (const Move& m : list) {
    if (text(m) == name) return &m;
  }
  return nullptr;
}

void play(Game& g, const std::string& name) {
  Move legal[MAX_MOVES];
  const int n = g.moves(legal, MAX_MOVES);
  for (int i = 0; i < n; ++i) {
    if (text(legal[i]) == name) {
      ASSERT_TRUE(g.play(legal[i])) << name;
      return;
    }
  }
  FAIL() << "not legal: " << name;
}

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

uint32_t ticks = 0;
uint32_t fakeClock() { return ticks += 5; }

Move think(Engine& engine, const Game& g, const uint32_t seed) {
  engine.start(g.position(), g.mode(), seed, &fakeClock);
  while (!engine.step(20)) {
  }
  return engine.bestMove();
}

}  // namespace

TEST(Draughts, OpeningHasSevenMoves) {
  EXPECT_EQ(texts(Position::initial()),
            (std::vector<std::string>{"a3-b4", "c3-b4", "c3-d4", "e3-d4", "e3-f4", "g3-f4", "g3-h4"}));
}

TEST(Draughts, CaptureIsMandatory) {
  const Position p = board(
      "........"
      "........"
      "........"
      "........"
      "...b...."
      "..w...w."
      "........"
      "........");
  EXPECT_EQ(texts(p), std::vector<std::string>{"c3:e5"});
}

TEST(Draughts, AnyCaptureMayBeChosen) {
  const Position p = board(
      "........"
      "........"
      "...b...."
      "........"
      ".b...b.."
      "..w...w."
      "........"
      "........");
  EXPECT_EQ(texts(p), (std::vector<std::string>{"c3:a5", "g3:e5:c7"}));
}

TEST(Draughts, MenCaptureBackwardButMoveOnlyForward) {
  const Position p = board(
      "........"
      "........"
      "........"
      "........"
      "...w...."
      "....b..."
      "........"
      "........");
  EXPECT_EQ(texts(p), std::vector<std::string>{"d4:f2"});
  Position quiet = p;
  quiet.black = 0;
  quiet.black |= 1u << sq("h8");
  EXPECT_EQ(texts(quiet), (std::vector<std::string>{"d4-c5", "d4-e5"}));
}

TEST(Draughts, MultiJumpTakesEveryPieceOnTheWay) {
  const Position p = board(
      "........"
      "........"
      "...b.b.."
      "........"
      ".....b.."
      "......w."
      "........"
      "........");
  const auto list = movesOf(p);
  const Move* m = find(list, "g3:e5:g7");
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(m->jumps, 2);
  EXPECT_EQ(m->captured, (1u << sq("f4")) | (1u << sq("f6")));
  EXPECT_NE(find(list, "g3:e5:c7"), nullptr);
  EXPECT_EQ(list.size(), 2u);
}

TEST(Draughts, ManCrownedMidCaptureGoesOnAsAKing) {
  const Position p = board(
      "........"
      "..b....."
      ".w...b.."
      "........"
      "........"
      "........"
      "........"
      "........");
  EXPECT_EQ(texts(p), (std::vector<std::string>{"b6:d8:g5", "b6:d8:h4"}));
  const auto list = movesOf(p);
  const Move* m = find(list, "b6:d8:h4");
  ASSERT_NE(m, nullptr);
  EXPECT_TRUE(m->promotes);
  Position after = p;
  apply(after, m->step());
  EXPECT_EQ(after.white, 1u << sq("h4"));
  EXPECT_EQ(after.kings, 1u << sq("h4"));
  EXPECT_EQ(after.black, 0u);
}

TEST(Draughts, ManReachingTheLastRowBecomesAKing) {
  Position p = board(
      "........"
      "..w....."
      "........"
      "........"
      "........"
      "........"
      "........"
      "B.......");
  const auto list = movesOf(p);
  const Move* m = find(list, "c7-d8");
  ASSERT_NE(m, nullptr);
  apply(p, m->step());
  EXPECT_NE(p.kings & 1u << sq("d8"), 0u);
}

TEST(Draughts, FlyingKingLandsAnywhereBeyond) {
  const Position p = board(
      "........"
      "........"
      "........"
      "........"
      "...b...."
      "........"
      "........"
      "W.......");
  EXPECT_EQ(texts(p), (std::vector<std::string>{"a1:e5", "a1:f6", "a1:g7", "a1:h8"}));
}

TEST(Draughts, FlyingKingMustLandWhereItGoesOn) {
  const Position p = board(
      "........"
      "........"
      "........"
      "........"
      "...b.b.."
      "........"
      "........"
      "W.......");
  EXPECT_EQ(texts(p), (std::vector<std::string>{"a1:e5:g3", "a1:e5:h2"}));
}

// Were captured pieces lifted at once, the king could also take c3 through e5.
TEST(Draughts, TurkishStrikeKeepsCapturedPiecesUntilTheEnd) {
  const Position p = board(
      "........"
      "........"
      "...b...."
      "....b..."
      ".....b.."
      "..b.b..."
      "........"
      "......W.");
  const auto list = movesOf(p);
  ASSERT_FALSE(list.empty());
  for (const Move& m : list) {
    EXPECT_EQ(m.jumps, 2) << text(m);
    EXPECT_EQ(__builtin_popcount(m.captured), m.jumps) << text(m);
  }
  EXPECT_NE(find(list, "g1:d4:f6"), nullptr);
  EXPECT_NE(find(list, "g1:d4:b2"), nullptr);
}

// Every capture over random positions: each jump takes one new piece and passes only empty cells.
TEST(Draughts, CapturesNeverJumpAPieceTwice) {
  games::Rng rng(99);
  int checked = 0;
  for (int iter = 0; iter < 20000; ++iter) {
    Position p;
    for (int s = 0; s < SQUARES; ++s) {
      const uint32_t roll = rng.below(10);
      if (roll == 0) p.white |= 1u << s;
      if (roll == 1 || roll == 2) p.black |= 1u << s;
      if (roll == 0 && rng.below(2) == 0) p.kings |= 1u << s;
    }
    p.white &= ~0xFu;
    p.black &= ~0xF0000000u;
    p.white |= p.kings & 0xFu;
    for (const Move& m : movesOf(p)) {
      if (m.jumps == 0) continue;
      ++checked;
      EXPECT_EQ(__builtin_popcount(m.captured), m.jumps);
      int at = m.from;
      for (int k = 0; k < m.jumps; ++k) {
        const int to = m.path[k];
        const int dr = rowOf(to) > rowOf(at) ? 1 : -1;
        const int dc = colOf(to) > colOf(at) ? 1 : -1;
        ASSERT_EQ(std::abs(rowOf(to) - rowOf(at)), std::abs(colOf(to) - colOf(at)));
        int pieces = 0;
        for (int r = rowOf(at) + dr, c = colOf(at) + dc; r != rowOf(to); r += dr, c += dc) {
          const int s = squareAt(r, c);
          if (s == m.from) continue;
          if (((p.white | p.black) >> s & 1u) != 0) {
            ++pieces;
            EXPECT_NE(m.captured >> s & 1u, 0u);
          }
        }
        EXPECT_EQ(pieces, 1);
        at = to;
      }
    }
  }
  EXPECT_GT(checked, 1000);
}

TEST(Draughts, ThreefoldRepetitionDraws) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1,
            board(".B......"
                  "........"
                  "........"
                  "........"
                  "........"
                  "........"
                  "........"
                  "......W."));
  for (int round = 0; round < 2; ++round) {
    play(g, "g1-h2");
    play(g, "b8-a7");
    play(g, "h2-g1");
    EXPECT_FALSE(g.isOver());
    play(g, "a7-b8");
  }
  EXPECT_TRUE(g.isOver());
  EXPECT_EQ(g.status(), Status::Draw);
  EXPECT_EQ(g.end(), End::Repetition);
}

TEST(Draughts, FifteenKingOnlyMovesEachDraw) {
  Position start = board(
      ".B......"
      "........"
      "........"
      "........"
      "........"
      "........"
      "...w...."
      "......W.");
  start.quiet = KING_DRAW_PLIES - 2;
  Game g;
  g.newGame(Mode::TwoPlayers, 1, start);
  play(g, "g1-h2");
  EXPECT_FALSE(g.isOver());
  play(g, "b8-a7");
  EXPECT_TRUE(g.isOver());
  EXPECT_EQ(g.end(), End::KingMoves);

  g.newGame(Mode::TwoPlayers, 1, start);
  play(g, "d2-c3");
  play(g, "b8-a7");
  EXPECT_FALSE(g.isOver());
  EXPECT_EQ(g.position().quiet, 1);
}

TEST(Draughts, NoMovesLoses) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1,
            board("........"
                  "........"
                  "........"
                  "........"
                  "...b...."
                  "..w....."
                  "........"
                  "........"));
  play(g, "c3:e5");
  EXPECT_EQ(g.status(), Status::WhiteWon);
  EXPECT_EQ(g.end(), End::NoMoves);
}

TEST(Draughts, UndoAgainstTheComputerTakesBackBothMoves) {
  Game g;
  g.newGame(Mode::Medium, 3);
  play(g, "c3-d4");
  EXPECT_TRUE(g.computerToMove());
  play(g, "f6-g5");
  ASSERT_TRUE(g.canUndo());
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(g.plyCount(), 0);
  EXPECT_TRUE(g.position().samePlacement(Position::initial()));
  EXPECT_FALSE(g.canUndo());
}

TEST(Draughts, DrawOffers) {
  Game two;
  two.newGame(Mode::TwoPlayers, 1);
  play(two, "c3-d4");
  EXPECT_TRUE(two.offerDraw());
  EXPECT_EQ(two.end(), End::Agreed);

  Game vs;
  vs.newGame(Mode::Easy, 1,
             board("........"
                   "........"
                   "...b.b.."
                   "........"
                   "........"
                   "........"
                   ".......w"
                   "........"));
  EXPECT_FALSE(vs.offerDraw());
  EXPECT_FALSE(vs.isOver());
  vs.resign();
  EXPECT_EQ(vs.status(), Status::BlackWon);
  EXPECT_EQ(vs.end(), End::Resigned);
}

TEST(DraughtsEngine, AlwaysPlaysALegalMove) {
  Engine engine;
  for (const Mode mode : {Mode::Easy, Mode::Medium, Mode::Hard}) {
    for (uint32_t seed = 1; seed <= 3; ++seed) {
      Game g;
      g.newGame(mode, seed);
      games::Rng rng(seed * 7919);
      for (int ply = 0; ply < 160 && !g.isOver(); ++ply) {
        if (g.computerToMove()) {
          const Move m = think(engine, g, seed + ply);
          ASSERT_TRUE(g.play(m)) << "mode " << static_cast<int>(mode) << " seed " << seed << " ply " << ply;
        } else {
          Move legal[MAX_MOVES];
          const int n = g.moves(legal, MAX_MOVES);
          ASSERT_GT(n, 0);
          ASSERT_TRUE(g.play(legal[rng.below(static_cast<uint32_t>(n))]));
        }
      }
    }
  }
}

TEST(DraughtsEngine, RunsInBoundedSlices) {
  Game g;
  g.newGame(Mode::Hard, 5);
  play(g, "c3-d4");
  Engine engine;
  ticks = 0;
  engine.start(g.position(), Mode::Hard, 5, &fakeClock);
  const uint32_t begin = ticks;
  int slices = 0;
  while (!engine.step(0)) ++slices;
  EXPECT_GT(slices, 5);
  EXPECT_LE(ticks - begin, Engine::HARD_MS + 100);
  EXPECT_GE(engine.depth(), 2);
}

TEST(DraughtsEngine, TakesTheBetterCapture) {
  const Position p = board(
      "........"
      "........"
      "...b...."
      "........"
      ".b...b.."
      "..w...w."
      "........"
      "........");
  Game g;
  g.newGame(Mode::Medium, 1, p);
  Engine engine;
  const Move m = think(engine, g, 1);
  EXPECT_EQ(text(m), "g3:e5:c7");
}

TEST(Draughts, SaveRoundTrip) {
  Game g;
  g.newGame(Mode::Hard, 42);
  for (const char* m : {"c3-d4", "f6-e5", "d4:f6", "g7:e5", "g3-f4", "e5:g3", "h2:f4"}) play(g, m);
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());
  EXPECT_NE(text.find("\"moves\":\"c3-d4 f6-e5 d4:f6 g7:e5 g3-f4 e5:g3 h2:f4\""), std::string::npos) << text;
  Game loaded;
  ASSERT_TRUE(loaded.deserialize(text.data(), text.size()));
  EXPECT_EQ(loaded.mode(), Mode::Hard);
  EXPECT_EQ(loaded.plyCount(), g.plyCount());
  EXPECT_TRUE(loaded.position().samePlacement(g.position()));
  EXPECT_EQ(save(loaded), text);
}

TEST(Draughts, TamperedSaveIsRejected) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  play(g, "c3-d4");
  play(g, "f6-g5");
  const std::string good = save(g);
  Game loaded;
  loaded.newGame(Mode::Easy, 9);
  for (const auto& [from, to] : std::vector<std::pair<std::string, std::string>>{
           {"c3-d4 f6-g5", "c3-d5 f6-g5"},
           {"c3-d4 f6-g5", "c3-e5 f6-g5"},
           {"c3-d4 f6-g5", "c3:d4 f6-g5"},
           {"c3-d4 f6-g5", "c3-d4f6-g5"},
           {"\"mode\":3", "\"mode\":7"},
           {"bbbbbbbbbbbb", "wbbbbbbbbbbb"},
           {"bbbbbbbbbbbb", "bbbbbbbbbbbx"},
       }) {
    std::string bad = good;
    const size_t at = bad.find(from);
    ASSERT_NE(at, std::string::npos) << from;
    bad.replace(at, from.size(), to);
    EXPECT_FALSE(loaded.deserialize(bad.data(), bad.size())) << bad;
    EXPECT_EQ(loaded.mode(), Mode::Easy);
    EXPECT_EQ(loaded.plyCount(), 0);
  }
}

TEST(Draughts, SaveFileRoundTripAndTamperedFile) {
  fake::reset();
  Game g;
  g.newGame(Mode::Medium, 8);
  EXPECT_TRUE(gamesave::save(g));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Draughts));
  play(g, "c3-d4");
  play(g, "b6-a5");
  ASSERT_TRUE(gamesave::save(g));
  ASSERT_TRUE(gamesave::hasGameInProgress(gamesave::Kind::Draughts));
  Game loaded;
  ASSERT_TRUE(gamesave::load(loaded));
  EXPECT_TRUE(loaded.position().samePlacement(g.position()));
  EXPECT_EQ(loaded.mode(), Mode::Medium);

  auto& bytes = fake::files.at(SAVE)->bytes;
  std::string text(bytes.begin(), bytes.end());
  const size_t at = text.find("b6-a5");
  ASSERT_NE(at, std::string::npos);
  text.replace(at, 5, "b6-c7");
  bytes.assign(text.begin(), text.end());
  EXPECT_FALSE(gamesave::load(loaded));
  EXPECT_FALSE(gamesave::hasGameInProgress(gamesave::Kind::Draughts));
}
