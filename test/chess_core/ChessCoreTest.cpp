#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <string>

#include "ChessBoard.h"
#include "ChessClock.h"
#include "ChessEngine.h"
#include "ChessGame.h"
#include "ChessMatch.h"

using namespace chess;

namespace {

uint32_t fakeNow = 1000;
uint32_t fakeMillis() { return fakeNow; }

uint32_t realMillis() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

uint64_t perft(const Position& pos, const int depth) {
  Move moves[MAX_MOVES];
  const int n = generatePseudo(pos, moves, false);
  uint64_t total = 0;
  Position next;
  for (int i = 0; i < n; ++i) {
    if (!makeMove(pos, moves[i], next)) continue;
    total += depth <= 1 ? 1 : perft(next, depth - 1);
  }
  return total;
}

uint64_t perftFen(const char* fen, const int depth) {
  Position pos;
  EXPECT_TRUE(parseFen(fen, pos)) << fen;
  return perft(pos, depth);
}

Square sq(const char* name) { return makeSquare(name[0] - 'a', name[1] - '1'); }

std::string fenOf(const Position& pos) {
  char fen[FEN_MAX];
  writeFen(pos, fen);
  return fen;
}

bool playAll(Game& game, const char* const* moves, const int count) {
  for (int i = 0; i < count; ++i) {
    if (!game.playUci(moves[i])) return false;
  }
  return true;
}

std::string moveList(const Game& game, const Notation& notation) {
  std::string out;
  char buf[32];
  for (int i = 0; i < game.plyCount(); ++i) {
    formatSan(game.ply(i).san, notation, buf, sizeof(buf));
    if (!out.empty()) out += ' ';
    out += buf;
  }
  return out;
}

constexpr const char* KIWIPETE = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";

}  // namespace

// --- Move generation ---------------------------------------------------------

TEST(ChessPerft, StartPosition) {
  EXPECT_EQ(perftFen(START_FEN, 1), 20u);
  EXPECT_EQ(perftFen(START_FEN, 2), 400u);
  EXPECT_EQ(perftFen(START_FEN, 3), 8902u);
  EXPECT_EQ(perftFen(START_FEN, 4), 197281u);
  EXPECT_EQ(perftFen(START_FEN, 5), 4865609u);
}

TEST(ChessPerft, Kiwipete) {
  EXPECT_EQ(perftFen(KIWIPETE, 1), 48u);
  EXPECT_EQ(perftFen(KIWIPETE, 2), 2039u);
  EXPECT_EQ(perftFen(KIWIPETE, 3), 97862u);
  EXPECT_EQ(perftFen(KIWIPETE, 4), 4085603u);
}

TEST(ChessPerft, ChessProgrammingWikiPositions) {
  EXPECT_EQ(perftFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5), 674624u);
  EXPECT_EQ(perftFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4), 422333u);
  EXPECT_EQ(perftFen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3), 62379u);
  EXPECT_EQ(perftFen("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 3), 89890u);
}

TEST(ChessFen, RoundTripAndValidation) {
  for (const char* fen : {START_FEN, KIWIPETE, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
                          "rnbqkbnr/pp1ppppp/8/2pP4/8/8/PPP1PPPP/RNBQKBNR w KQkq c6 0 3"}) {
    Position pos;
    ASSERT_TRUE(parseFen(fen, pos)) << fen;
    EXPECT_EQ(fenOf(pos), fen);
    EXPECT_EQ(pos.key, computeKey(pos));
  }
  Position pos;
  EXPECT_FALSE(parseFen("", pos));
  EXPECT_FALSE(parseFen("8/8/8/8/8/8/8/8 w - - 0 1", pos));                                 // no kings
  EXPECT_FALSE(parseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1", pos));  // bad side
  EXPECT_FALSE(parseFen("4k3/8/8/8/8/8/8/P3K3 w - - 0 1", pos));                            // pawn on rank 1
  EXPECT_FALSE(parseFen("4k3/8/8/8/8/8/4R3/4K3 w - - 0 1", pos));                           // side not to move in check
  EXPECT_FALSE(parseFen("4k3/8/8/8/8/8/PPPPPPPP/P3K3 w - - 0 1", pos));                     // too many pawns
  // A dead en passant square (no capturer) is dropped so it cannot split repetitions.
  ASSERT_TRUE(parseFen("rnbqkbnr/pppp1ppp/8/4p3/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 2", pos));
  EXPECT_EQ(pos.ep, NO_EP);
}

TEST(ChessFen, RequiresBothCountersAndNothingAfter) {
  Position pos;
  const char* bad[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 junk",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1x",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 256 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 10000",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -0 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR wKQkq - 0 1",
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e9 0 1",
  };
  for (const char* fen : bad) EXPECT_FALSE(parseFen(fen, pos)) << fen;
  ASSERT_TRUE(parseFen("  rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w  KQkq  -  12  34  ", pos));
  EXPECT_EQ(pos.halfmove, 12);
  EXPECT_EQ(pos.fullmove, 34);
}

TEST(ChessFen, IncrementalKeyMatchesRecomputed) {
  Position pos;
  ASSERT_TRUE(parseFen(KIWIPETE, pos));
  Move moves[MAX_MOVES];
  const int n = generatePseudo(pos, moves, false);
  Position next;
  for (int i = 0; i < n; ++i) {
    if (makeMove(pos, moves[i], next)) EXPECT_EQ(next.key, computeKey(next));
  }
}

// --- Special moves ------------------------------------------------------------

TEST(ChessRules, CastlingBothSidesAndThroughCheck) {
  Game game;
  ASSERT_TRUE(game.reset("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
  EXPECT_TRUE(game.legalTargets(sq("e1")) & (1ull << sq("g1")));
  EXPECT_TRUE(game.legalTargets(sq("e1")) & (1ull << sq("c1")));
  ASSERT_TRUE(game.playUci("e1g1"));
  EXPECT_EQ(game.position().at(sq("f1")), makePiece(WHITE, ROOK));
  EXPECT_EQ(game.position().at(sq("g1")), makePiece(WHITE, KING));
  ASSERT_TRUE(game.playUci("e8c8"));
  EXPECT_EQ(game.position().at(sq("d8")), makePiece(BLACK, ROOK));
  EXPECT_STREQ(game.ply(0).san, "O-O");
  EXPECT_STREQ(game.ply(1).san, "O-O-O");
  EXPECT_EQ(game.position().castling, 0);

  // Bishop on c4 covers f1: no short castling; queen side is fine.
  ASSERT_TRUE(game.reset("r3k2r/8/8/8/2b5/8/8/R3K2R w KQkq - 0 1"));
  EXPECT_FALSE(game.legalTargets(sq("e1")) & (1ull << sq("g1")));
  EXPECT_TRUE(game.legalTargets(sq("e1")) & (1ull << sq("c1")));
  // In check: no castling at all.
  ASSERT_TRUE(game.reset("r3k2r/8/8/8/4r3/8/8/R3K2R w KQkq - 0 1"));
  EXPECT_FALSE(game.legalTargets(sq("e1")) & ((1ull << sq("g1")) | (1ull << sq("c1"))));
}

TEST(ChessRules, CastlingRightsLostByRookMoveAndCapture) {
  Game game;
  ASSERT_TRUE(game.reset("r3k2r/8/8/8/8/8/6b1/R3K2R b KQkq - 0 1"));
  ASSERT_TRUE(game.playUci("g2h1"));  // bishop takes the h1 rook
  EXPECT_EQ(game.position().castling, WHITE_OOO | BLACK_OO | BLACK_OOO);
  ASSERT_TRUE(game.playUci("a1b1"));
  EXPECT_EQ(game.position().castling, BLACK_OO | BLACK_OOO);
  ASSERT_TRUE(game.playUci("e8e7"));
  EXPECT_EQ(game.position().castling, 0);
}

TEST(ChessRules, EnPassant) {
  Game game;
  const char* const opening[] = {"e2e4", "a7a6", "e4e5", "d7d5"};
  ASSERT_TRUE(playAll(game, opening, 4));
  EXPECT_TRUE(game.legalTargets(sq("e5")) & (1ull << sq("d6")));
  ASSERT_TRUE(game.playUci("e5d6"));
  EXPECT_STREQ(game.ply(4).san, "exd6");
  EXPECT_EQ(game.position().at(sq("d5")), 0);
  EXPECT_EQ(game.position().at(sq("d6")), makePiece(WHITE, PAWN));

  // Capturing en passant would expose the king along the rank: illegal.
  ASSERT_TRUE(game.reset("8/8/8/8/k2Pp2Q/8/8/3K4 b - d3 0 1"));
  EXPECT_FALSE(game.legalTargets(sq("e4")) & (1ull << sq("d3")));
  // The right expires after one move.
  ASSERT_TRUE(game.reset("4k3/8/8/8/3Pp3/8/8/4K3 b - d3 0 1"));
  EXPECT_TRUE(game.legalTargets(sq("e4")) & (1ull << sq("d3")));
  ASSERT_TRUE(game.playUci("e8d8"));
  ASSERT_TRUE(game.playUci("e1d1"));
  EXPECT_FALSE(game.legalTargets(sq("e4")) & (1ull << sq("d3")));
}

TEST(ChessRules, PromotionNeedsAChoice) {
  Game game;
  ASSERT_TRUE(game.reset("8/4P2k/8/8/8/8/8/K7 w - - 0 1"));
  EXPECT_TRUE(game.isPromotion(sq("e7"), sq("e8")));
  EXPECT_FALSE(game.isPromotion(sq("a1"), sq("a2")));
  MoveList list;
  game.legalMoves(list);
  int promotions = 0;
  for (int i = 0; i < list.count; ++i) promotions += list.moves[i].isPromotion() ? 1 : 0;
  EXPECT_EQ(promotions, 4);

  Move m;
  EXPECT_FALSE(game.findMove(sq("e7"), sq("e8"), NO_PIECE, m));
  ASSERT_TRUE(game.findMove(sq("e7"), sq("e8"), KNIGHT, m));
  ASSERT_TRUE(game.play(m));
  EXPECT_EQ(game.position().at(sq("e8")), makePiece(WHITE, KNIGHT));
  EXPECT_STREQ(game.ply(0).san, "e8=N");

  Match match(fakeMillis);
  MatchSettings hotSeat;
  hotSeat.mode = Mode::HotSeat;
  ASSERT_TRUE(match.newGame(hotSeat, 7, "8/4P2k/8/8/8/8/8/K7 w - - 0 1"));
  EXPECT_TRUE(match.needsPromotion(sq("e7"), sq("e8")));
  EXPECT_FALSE(match.play(sq("e7"), sq("e8")));
  ASSERT_TRUE(match.play(sq("e7"), sq("e8"), QUEEN));
  EXPECT_STREQ(match.game().ply(0).san, "e8=Q");
}

// --- Notation -----------------------------------------------------------------

TEST(ChessNotation, SanDisambiguationChecksAndLocales) {
  Game game;
  ASSERT_TRUE(game.reset("4k3/8/8/8/8/8/8/RN2K1NR w - - 0 1"));
  ASSERT_TRUE(game.playUci("g1e2"));  // only one knight reaches e2
  EXPECT_STREQ(game.ply(0).san, "Ne2");

  ASSERT_TRUE(game.reset("4k3/8/8/8/8/8/8/1N1NK3 w - - 0 1"));
  ASSERT_TRUE(game.playUci("b1c3"));
  EXPECT_STREQ(game.ply(0).san, "Nbc3");

  ASSERT_TRUE(game.reset("4k3/8/8/8/R7/8/8/R3K3 w - - 0 1"));
  ASSERT_TRUE(game.playUci("a1a2"));
  EXPECT_STREQ(game.ply(0).san, "R1a2");

  ASSERT_TRUE(game.reset("2k5/8/8/8/Q6Q/8/8/K6Q w - - 0 1"));
  ASSERT_TRUE(game.playUci("h4e1"));
  EXPECT_STREQ(game.ply(0).san, "Q4e1");
  ASSERT_TRUE(game.reset("2k5/8/8/8/1Q5Q/8/8/K6Q w - - 0 1"));
  ASSERT_TRUE(game.playUci("h4e1"));
  EXPECT_STREQ(game.ply(0).san, "Qh4e1");

  // Fool's mate: check suffix, mate suffix and the result.
  game.reset();
  const char* const fools[] = {"f2f3", "e7e5", "g2g4", "d8h4"};
  ASSERT_TRUE(playAll(game, fools, 4));
  EXPECT_STREQ(game.ply(3).san, "Qh4#");
  EXPECT_EQ(game.outcome(), Outcome::BlackWins);
  EXPECT_EQ(game.termination(), Termination::Checkmate);
  EXPECT_FALSE(game.playUci("a2a3"));

  game.reset();
  const char* const check[] = {"e2e4", "f7f6", "d1h5"};
  ASSERT_TRUE(playAll(game, check, 3));
  EXPECT_STREQ(game.ply(2).san, "Qh5+");

  game.reset();
  const char* const line[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "g8f6", "e1g1", "f6e4", "b5c6", "d7c6"};
  ASSERT_TRUE(playAll(game, line, 10));
  EXPECT_EQ(moveList(game, NOTATION_EN), "e4 e5 Nf3 Nc6 Bb5 Nf6 O-O Nxe4 Bxc6 dxc6");
  EXPECT_EQ(moveList(game, NOTATION_RU), "e4 e5 Кf3 Кc6 Сb5 Кf6 0-0 Кxe4 Сxc6 dxc6");

  char buf[32];
  formatSan("Kxe2+", NOTATION_RU, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Крxe2+");
  formatSan("exd8=Q#", NOTATION_RU, buf, sizeof(buf));
  EXPECT_STREQ(buf, "exd8=Ф#");
  formatSan("O-O-O+", NOTATION_RU, buf, sizeof(buf));
  EXPECT_STREQ(buf, "0-0-0+");
  formatSan("Rxa8", NOTATION_RU, buf, sizeof(buf));
  EXPECT_STREQ(buf, "Лxa8");
  formatSan("Qa1xb2+", NOTATION_RU, buf, 4);  // truncation stays terminated
  EXPECT_EQ(strlen(buf), 3u);

  EXPECT_EQ(game.moveNumberOfPly(0), 1);
  EXPECT_EQ(game.moveNumberOfPly(1), 1);
  EXPECT_EQ(game.moveNumberOfPly(2), 2);
  ASSERT_TRUE(game.reset("4k3/8/8/8/8/8/8/4K3 b - - 0 12"));
  EXPECT_EQ(game.moveNumberOfPly(0), 12);
  EXPECT_EQ(game.colorOfPly(0), BLACK);
  EXPECT_EQ(game.moveNumberOfPly(1), 13);
}

// --- Draw rules ---------------------------------------------------------------

TEST(ChessDraws, ThreefoldRepetition) {
  Game game;
  const char* const shuffle[] = {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1"};
  ASSERT_TRUE(playAll(game, shuffle, 7));
  EXPECT_FALSE(game.isOver());
  ASSERT_TRUE(game.playUci("f6g8"));  // start position for the third time
  EXPECT_EQ(game.outcome(), Outcome::Draw);
  EXPECT_EQ(game.termination(), Termination::Threefold);
  ASSERT_TRUE(game.undo());
  EXPECT_FALSE(game.isOver());
}

TEST(ChessDraws, RepetitionDoesNotCrossIrreversibleMoves) {
  Game game;
  const char* const moves[] = {"g1f3", "g8f6", "f3g1", "f6g8", "e2e4", "e7e5", "g1f3",
                               "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"};
  ASSERT_TRUE(playAll(game, moves, 13));
  EXPECT_FALSE(game.isOver());
  ASSERT_TRUE(game.playUci(moves[13]));
  EXPECT_EQ(game.termination(), Termination::Threefold);
}

TEST(ChessDraws, FiftyMoveRule) {
  Game game;
  ASSERT_TRUE(game.reset("4k3/8/8/8/8/8/8/R3K3 w - - 98 80"));
  ASSERT_TRUE(game.playUci("a1a2"));
  EXPECT_FALSE(game.isOver());
  ASSERT_TRUE(game.playUci("e8d8"));
  EXPECT_EQ(game.termination(), Termination::FiftyMoves);

  // Mate on the hundredth half-move is still mate.
  ASSERT_TRUE(game.reset("k7/8/1K6/8/8/8/8/7R w - - 99 80"));
  ASSERT_TRUE(game.playUci("h1h8"));
  EXPECT_EQ(game.termination(), Termination::Checkmate);
}

TEST(ChessDraws, InsufficientMaterialAndStalemate) {
  Game game;
  ASSERT_TRUE(game.reset("4k3/8/8/8/8/8/3q4/4K3 w - - 0 1"));
  ASSERT_TRUE(game.playUci("e1d2"));
  EXPECT_EQ(game.termination(), Termination::InsufficientMaterial);

  Position pos;
  ASSERT_TRUE(parseFen("4k3/8/8/8/8/8/8/2B1K3 w - - 0 1", pos));
  EXPECT_TRUE(insufficientMaterial(pos));
  ASSERT_TRUE(parseFen("4k3/8/8/8/8/8/8/1N2K3 w - - 0 1", pos));
  EXPECT_TRUE(insufficientMaterial(pos));
  ASSERT_TRUE(parseFen("4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1", pos));  // c1 and f8 are both dark
  EXPECT_TRUE(insufficientMaterial(pos));
  ASSERT_TRUE(parseFen("4k1b1/8/8/8/8/8/8/2B1K3 w - - 0 1", pos));  // opposite colours: mate possible
  EXPECT_FALSE(insufficientMaterial(pos));
  ASSERT_TRUE(parseFen("4k3/8/8/8/8/8/8/1NN1K3 w - - 0 1", pos));
  EXPECT_FALSE(insufficientMaterial(pos));
  ASSERT_TRUE(parseFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", pos));
  EXPECT_FALSE(insufficientMaterial(pos));

  ASSERT_TRUE(game.reset("k7/8/1Q6/8/8/8/8/K7 w - - 0 1"));
  ASSERT_TRUE(game.playUci("b6c7"));
  EXPECT_EQ(game.termination(), Termination::Stalemate);
  EXPECT_EQ(game.outcome(), Outcome::Draw);
}

// --- Clock --------------------------------------------------------------------

TEST(ChessClock, IncrementPauseAndFlag) {
  Clock clock;
  clock.reset(TIME_5_3, WHITE, 0);
  EXPECT_EQ(clock.remainingMs(WHITE, 10000), 290000u);
  clock.moveMade(WHITE, 10000);
  EXPECT_EQ(clock.remainingMs(WHITE, 20000), 293000u);
  EXPECT_EQ(clock.remainingMs(BLACK, 20000), 290000u);
  clock.pause(20000);
  EXPECT_EQ(clock.remainingMs(BLACK, 500000), 290000u);
  clock.resume(500000);
  EXPECT_EQ(clock.remainingMs(BLACK, 501000), 289000u);
  EXPECT_FALSE(clock.flagged(BLACK, 501000 + 288999));
  EXPECT_TRUE(clock.flagged(BLACK, 501000 + 289000));
  EXPECT_EQ(clock.remainingMs(BLACK, 501000 + 999999), 0u);

  // millis() wrap-around.
  clock.reset(TIME_10_5, WHITE, 0xFFFFF000u);
  EXPECT_EQ(clock.remainingMs(WHITE, 0x00001000u), 600000u - 0x2000u);

  clock.reset(TIME_UNLIMITED, WHITE, 0);
  EXPECT_FALSE(clock.flagged(WHITE, 0xFFFFFFF0u));

  // Banked increments stop at the ceiling, however many moves (or undo cycles) happen.
  const TimeControl fast{1000, 1000};
  clock.reset(fast, WHITE, 0);
  for (int i = 0; i < 2 * static_cast<int>(MAX_INCREMENTS); ++i) {
    clock.moveMade(WHITE, 0);
    clock.setTurn(WHITE, 0);
  }
  EXPECT_EQ(clock.remainingMs(WHITE, 0), maxRemainingMs(fast));
  EXPECT_EQ(maxRemainingMs(TimeControl{UINT32_MAX, UINT32_MAX}), UINT32_MAX);
}

TEST(ChessClock, MatchFlagFallAndInsufficientMaterialDraw) {
  fakeNow = 1000;
  Match match(fakeMillis);
  MatchSettings s;
  s.mode = Mode::HotSeat;
  s.time = TimeControl{60000, 2000};
  match.newGame(s, 1);
  fakeNow += 5000;
  ASSERT_TRUE(match.play(sq("e2"), sq("e4")));
  EXPECT_EQ(match.remainingMs(WHITE), 57000u);
  fakeNow += 59999;
  EXPECT_FALSE(match.update());
  fakeNow += 1;
  EXPECT_TRUE(match.update());
  EXPECT_EQ(match.game().outcome(), Outcome::WhiteWins);
  EXPECT_EQ(match.game().termination(), Termination::Timeout);
  EXPECT_FALSE(match.canUndo());
  EXPECT_FALSE(match.play(sq("e7"), sq("e5")));

  // Pausing (sleep/exit) stops the running clock.
  match.newGame(s, 1);
  match.pause();
  fakeNow += 1000000;
  EXPECT_FALSE(match.update());
  match.resume();
  EXPECT_EQ(match.remainingMs(WHITE), 60000u);

  // Flagging against a bare king is a draw.
  ASSERT_TRUE(match.newGame(s, 1, "4k3/8/8/8/8/8/8/R3K3 b - - 0 1"));
  fakeNow += 60000;
  EXPECT_TRUE(match.update());
  EXPECT_EQ(match.game().termination(), Termination::Timeout);
  EXPECT_EQ(match.game().outcome(), Outcome::WhiteWins);
  ASSERT_TRUE(match.newGame(s, 1, "4k3/8/8/8/8/8/8/r3K3 w - - 0 1"));
  fakeNow += 60000;
  EXPECT_TRUE(match.update());
  EXPECT_EQ(match.game().termination(), Termination::Timeout);
  EXPECT_EQ(match.game().outcome(), Outcome::BlackWins);
  ASSERT_TRUE(match.newGame(s, 1, "4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
  fakeNow += 60000;
  EXPECT_TRUE(match.update());
  EXPECT_EQ(match.game().termination(), Termination::TimeoutVsInsufficientMaterial);
  EXPECT_EQ(match.game().outcome(), Outcome::Draw);
}

// --- Undo ---------------------------------------------------------------------

TEST(ChessUndo, HotSeatAndVsComputerPairs) {
  fakeNow = 1000;
  Game game;
  const char* const fools[] = {"f2f3", "e7e5", "g2g4", "d8h4"};
  ASSERT_TRUE(playAll(game, fools, 4));
  const std::string before = fenOf(game.position());
  ASSERT_TRUE(game.undo());
  EXPECT_FALSE(game.isOver());
  ASSERT_TRUE(game.playUci("d8h4"));
  EXPECT_EQ(fenOf(game.position()), before);

  auto match = std::make_unique<Match>(fakeMillis);
  MatchSettings s;
  s.mode = Mode::VsComputer;
  s.humanSide = SideChoice::White;
  s.level = 1;
  match->newGame(s, 42);
  EXPECT_FALSE(match->canUndo());
  ASSERT_TRUE(match->play(sq("e2"), sq("e4")));
  EXPECT_FALSE(match->isHumanTurn());
  // Undo while the engine is to move takes back just the human move.
  ASSERT_TRUE(match->undo());
  EXPECT_EQ(match->game().plyCount(), 0);
  ASSERT_TRUE(match->play(sq("e2"), sq("e4")));
  while (!match->isHumanTurn()) match->update();
  EXPECT_EQ(match->game().plyCount(), 2);
  ASSERT_TRUE(match->undo());
  EXPECT_EQ(match->game().plyCount(), 0);
  EXPECT_TRUE(match->isHumanTurn());

  // Human as Black: the engine's opening move cannot be taken back alone.
  s.humanSide = SideChoice::Black;
  match->newGame(s, 42);
  while (!match->isHumanTurn()) match->update();
  EXPECT_EQ(match->game().plyCount(), 1);
  EXPECT_FALSE(match->canUndo());

  match->resign();
  EXPECT_EQ(match->game().outcome(), Outcome::WhiteWins);
  EXPECT_EQ(match->game().termination(), Termination::Resignation);
  EXPECT_FALSE(match->canUndo());
}

// --- Save / load --------------------------------------------------------------

TEST(ChessSave, RoundTrip) {
  fakeNow = 5000;
  auto a = std::make_unique<Match>(fakeMillis);
  auto b = std::make_unique<Match>(fakeMillis);
  MatchSettings s;
  s.mode = Mode::HotSeat;
  s.time = TIME_10_5;
  a->newGame(s, 3);
  const Square line[][2] = {{sq("e2"), sq("e4")}, {sq("d7"), sq("d5")}, {sq("e4"), sq("d5")}, {sq("g8"), sq("f6")},
                            {sq("f1"), sq("b5")}, {sq("c7"), sq("c6")}, {sq("d5"), sq("c6")}, {sq("d8"), sq("d2")}};
  for (const auto& m : line) {
    fakeNow += 1500;
    ASSERT_TRUE(a->play(m[0], m[1]));
  }
  fakeNow += 700;
  a->pause();

  std::unique_ptr<char[]> text(new char[SAVE_MAX]);
  const size_t len = a->serialize(text.get(), SAVE_MAX);
  ASSERT_GT(len, 0u);
  EXPECT_NE(std::string(text.get()).find("\"moves\":\"e2e4 d7d5 e4d5 g8f6 f1b5 c7c6 d5c6 d8d2\""), std::string::npos);

  fakeNow += 99999;
  ASSERT_TRUE(b->deserialize(text.get(), len, 9));
  EXPECT_TRUE(b->paused());
  EXPECT_EQ(fenOf(b->game().position()), fenOf(a->game().position()));
  EXPECT_EQ(b->game().plyCount(), a->game().plyCount());
  for (int i = 0; i < a->game().plyCount(); ++i) EXPECT_STREQ(b->game().ply(i).san, a->game().ply(i).san);
  EXPECT_EQ(b->game().keyCount(), a->game().keyCount());
  EXPECT_EQ(b->remainingMs(WHITE), a->remainingMs(WHITE));
  EXPECT_EQ(b->remainingMs(BLACK), a->remainingMs(BLACK));
  EXPECT_EQ(b->settings().mode, Mode::HotSeat);
  EXPECT_EQ(b->settings().time.baseMs, TIME_10_5.baseMs);
  EXPECT_EQ(b->settings().time.incrementMs, TIME_10_5.incrementMs);
  EXPECT_STREQ(b->game().ply(7).san, "Qxd2+");
  b->resume();
  fakeNow += 1000;
  EXPECT_EQ(b->remainingMs(WHITE), a->remainingMs(WHITE) - 1000);
  ASSERT_TRUE(b->play(sq("c1"), sq("d2")));

  // Vs computer settings and a custom start survive too.
  MatchSettings v;
  v.level = 4;
  v.humanSide = SideChoice::Black;
  ASSERT_TRUE(a->newGame(v, 5, "4k3/8/8/8/8/8/4P3/4K3 b - - 3 40"));
  const size_t len2 = a->serialize(text.get(), SAVE_MAX);
  ASSERT_TRUE(b->deserialize(text.get(), len2, 5));
  EXPECT_EQ(b->settings().level, 4);
  EXPECT_EQ(b->humanColor(), BLACK);
  EXPECT_EQ(fenOf(b->game().startPosition()), "4k3/8/8/8/8/8/4P3/4K3 b - - 3 40");
  EXPECT_FALSE(b->timed());
}

TEST(ChessSave, RejectsCorruptInput) {
  auto m = std::make_unique<Match>(fakeMillis);
  const char* bad[] = {
      "",
      "{}",
      "{\"v\":2,\"mode\":0,\"human\":0,\"level\":3,\"base\":0,\"inc\":0,\"white\":0,\"black\":0,\"fen\":\"rnbqkbnr/"
      "pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\",\"moves\":\"\"}",
      "{\"v\":1,\"mode\":0,\"human\":0,\"level\":3,\"base\":0,\"inc\":0,\"white\":0,\"black\":0,\"fen\":\"rnbqkbnr/"
      "pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\",\"moves\":\"e2e5\"}",
      "{\"v\":1,\"mode\":0,\"human\":0,\"level\":9,\"base\":0,\"inc\":0,\"white\":0,\"black\":0,\"fen\":\"rnbqkbnr/"
      "pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\",\"moves\":\"\"}",
      "{\"v\":1,\"mode\":0,\"human\":0,\"level\":3,\"base\":0,\"inc\":0,\"white\":0,\"black\":0,\"fen\":\"garbage\","
      "\"moves\":\"\"}",
      "{\"v\":1,\"mode\":0,\"human\":0,\"level\":3,\"base\":0,\"inc\":0,\"white\":0,\"black\":0,\"fen\":\"rnbqkbnr/"
      "pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\",\"moves\":\"e2e4 e7e5",
  };
  for (const char* text : bad) {
    EXPECT_FALSE(m->deserialize(text, strlen(text), 1)) << text;
    EXPECT_EQ(m->game().plyCount(), 0);
  }
  const char* spaced =
      "{ \"v\" : 1, \"mode\" : 1, \"human\" : 0, \"level\" : 2, \"base\" : 300000, \"inc\" : 3000, \"white\" : 1234, "
      "\"black\" : 5678, \"fen\" : \"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\", \"moves\" : "
      "\"e2e4\" }";
  ASSERT_TRUE(m->deserialize(spaced, strlen(spaced), 1));
  EXPECT_EQ(m->game().plyCount(), 1);
  EXPECT_EQ(m->remainingMs(BLACK), 5678u);

  char tiny[16];
  EXPECT_EQ(m->serialize(tiny, sizeof(tiny)), 0u);
}

TEST(ChessSave, RejectsClockTimesOutsideTheTimeControl) {
  auto m = std::make_unique<Match>(fakeMillis);
  const auto save = [](const char* base, const char* inc, const char* white, const char* black) {
    return std::string("{\"v\":1,\"mode\":1,\"human\":0,\"level\":3,\"base\":") + base + ",\"inc\":" + inc +
           ",\"white\":" + white + ",\"black\":" + black +
           ",\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\",\"moves\":\"e2e4\"}";
  };
  // 5+3: the ceiling is base + MAX_INCREMENTS increments.
  const uint32_t ceiling = maxRemainingMs(TIME_5_3);
  EXPECT_EQ(ceiling, 300000u + MAX_INCREMENTS * 3000u);
  const std::string atCeiling = std::to_string(ceiling);
  const std::string overCeiling = std::to_string(ceiling + 1);
  std::string text = save("300000", "3000", atCeiling.c_str(), "1000");
  EXPECT_TRUE(m->deserialize(text.data(), text.size(), 1));
  for (const auto& bad :
       {save("300000", "3000", overCeiling.c_str(), "1000"), save("300000", "3000", "1000", "4294967295"),
        save("0", "0", "5", "0"), save("0", "0", "0", "300000")}) {
    EXPECT_FALSE(m->deserialize(bad.data(), bad.size(), 1)) << bad;
    EXPECT_EQ(m->game().plyCount(), 0);
  }
  // A base above MAX_BASE_MS is clamped first, so its full value no longer fits.
  text = save("99999999", "0", "99999999", "99999999");
  EXPECT_FALSE(m->deserialize(text.data(), text.size(), 1));
  text = save("0", "0", "0", "0");
  EXPECT_TRUE(m->deserialize(text.data(), text.size(), 1));
  // A damaged start FEN fails the whole save.
  text =
      "{\"v\":1,\"mode\":1,\"human\":0,\"level\":3,\"base\":0,\"inc\":0,\"white\":0,\"black\":0,"
      "\"fen\":\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 junk\",\"moves\":\"\"}";
  EXPECT_FALSE(m->deserialize(text.data(), text.size(), 1));
}

// --- Engine -------------------------------------------------------------------

namespace {

struct EngineRun {
  Move move;
  uint32_t elapsedMs;
  uint32_t longestStepMs;
  SearchInfo info;
};

EngineRun runEngine(Engine& engine, const char* fen, const int level, uint32_t budgetMs = 0,
                    const uint32_t sliceMs = 10) {
  Position pos;
  EXPECT_TRUE(parseFen(fen, pos));
  const EngineLevel& lv = engineLevel(level);
  SearchLimits limits;
  limits.maxDepth = lv.maxDepth;
  limits.maxNodes = lv.maxNodes;
  limits.marginCp = lv.marginCp;
  limits.blunderPercent = lv.blunderPercent;
  limits.blunderMarginCp = lv.blunderMarginCp;
  if (budgetMs == 0) budgetMs = lv.moveTimeMs;
  limits.hardMs = budgetMs;
  limits.softMs = budgetMs / 10 * 6;
  EngineRun run{};
  const uint32_t start = realMillis();
  engine.start(pos, &pos.key, 1, limits, start);
  for (;;) {
    const uint32_t before = realMillis();
    const bool done = engine.step(realMillis, sliceMs);
    const uint32_t took = realMillis() - before;
    if (took > run.longestStepMs) run.longestStepMs = took;
    if (done) break;
  }
  run.elapsedMs = realMillis() - start;
  run.move = engine.bestMove();
  run.info = engine.info();
  return run;
}

bool isLegal(const char* fen, const Move& m) {
  Position pos;
  parseFen(fen, pos);
  MoveList list;
  generateLegal(pos, list);
  for (int i = 0; i < list.count; ++i) {
    if (list.moves[i].sameAs(m)) return true;
  }
  return false;
}

}  // namespace

TEST(ChessEngine, LegalMoveAtEveryLevelWithinBudget) {
  auto engine = std::make_unique<Engine>();
  const char* positions[] = {START_FEN, KIWIPETE, "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 3 9",
                             "8/8/4k3/8/2p5/8/B2K4/8 w - - 0 1", "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1"};
  // Short budgets keep the suite fast; one full level-5 think checks the real cap.
  // Wall-clock bounds are loose so a loaded host does not make them flaky.
  constexpr uint32_t BUDGET_MS = 300;
  constexpr uint32_t SLACK_MS = 100;
  for (int level = MIN_LEVEL; level <= MAX_LEVEL; ++level) {
    for (const char* fen : positions) {
      const EngineRun run = runEngine(*engine, fen, level, BUDGET_MS);
      EXPECT_TRUE(isLegal(fen, run.move)) << "level " << level << " " << fen;
      EXPECT_LE(run.elapsedMs, BUDGET_MS + SLACK_MS) << "level " << level << " " << fen;
      EXPECT_LE(run.longestStepMs, 10u + SLACK_MS / 2) << "level " << level;
      if (engineLevel(level).maxNodes != 0) EXPECT_LE(run.info.nodes, engineLevel(level).maxNodes);
    }
  }
  const EngineRun full = runEngine(*engine, positions[2], MAX_LEVEL);
  EXPECT_TRUE(isLegal(positions[2], full.move));
  EXPECT_LE(full.elapsedMs, engineLevel(MAX_LEVEL).moveTimeMs + SLACK_MS);
}

TEST(ChessEngine, FindsMateAndWinsMaterial) {
  auto engine = std::make_unique<Engine>();
  // Back-rank mate in one.
  EngineRun run = runEngine(*engine, "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", 5);
  EXPECT_EQ(run.move.from, sq("d1"));
  EXPECT_EQ(run.move.to, sq("d8"));
  EXPECT_GE(run.info.score, MATE_BOUND);
  // A hanging queen is taken even at level 3.
  run = runEngine(*engine, "rnb1kbnr/pppp1ppp/8/4p1q1/3P4/2N5/PPP1PPPP/R1BQKBNR w KQkq - 0 3", 3);
  EXPECT_EQ(run.move.to, sq("g5"));
  // Mate in two: 1.Nf6+ gxf6 2.Bxf7#.
  run = runEngine(*engine, "r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 1 1", 5);
  EXPECT_EQ(run.move.from, sq("d5"));
  EXPECT_EQ(run.move.to, sq("f6"));
  EXPECT_GE(run.info.score, MATE_BOUND);
  // Only one legal move: answered immediately.
  run = runEngine(*engine, "k7/8/8/8/8/8/1q6/K7 w - - 0 1", 5);
  EXPECT_EQ(run.move.to, sq("b2"));
  EXPECT_LE(run.info.depth, 1);
}

TEST(ChessEngine, WeakLevelsVaryStrongLevelIsStable) {
  auto engine = std::make_unique<Engine>();
  const char* fen = "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3";
  int distinct = 0;
  Move first{};
  for (uint32_t seed = 1; seed <= 12; ++seed) {
    engine->seed(seed);
    const Move m = runEngine(*engine, fen, 1).move;
    if (seed == 1) first = m;
    if (!m.sameAs(first)) ++distinct;
  }
  EXPECT_GT(distinct, 0);
}

TEST(ChessEngine, CooperativeStepsAndCancel) {
  auto engine = std::make_unique<Engine>();
  Position pos;
  ASSERT_TRUE(parseFen(KIWIPETE, pos));
  SearchLimits limits;
  limits.maxDepth = Engine::MAX_DEPTH;
  limits.hardMs = 400;
  limits.softMs = 300;
  engine->start(pos, &pos.key, 1, limits, realMillis());
  int steps = 0;
  while (!engine->step(realMillis, 2)) ++steps;
  EXPECT_GT(steps, 10);
  EXPECT_TRUE(engine->ready());
  EXPECT_GE(engine->info().depth, 3);

  engine->start(pos, &pos.key, 1, limits, realMillis());
  EXPECT_FALSE(engine->step(realMillis, 1));
  engine->cancel();
  EXPECT_FALSE(engine->thinking());
  EXPECT_FALSE(engine->ready());
}

TEST(ChessEngine, EvaluationIsColourSymmetric) {
  const char* fens[][2] = {
      {KIWIPETE, "r3k2r/pppbbppp/2n2q1P/1P2p3/3pn3/BN2PNP1/P1PPQPB1/R3K2R b KQkq - 0 1"},
      {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", "8/4p1p1/8/1r3P1K/kp5R/3P4/2P5/8 b - - 0 1"},
  };
  for (const auto& pair : fens) {
    Position a;
    Position b;
    ASSERT_TRUE(parseFen(pair[0], a));
    ASSERT_TRUE(parseFen(pair[1], b));
    EXPECT_EQ(evaluate(a), evaluate(b));
  }
}

TEST(ChessMatchFlow, VsComputerPlaysAFullGameAndEnds) {
  auto match = std::make_unique<Match>(realMillis);
  MatchSettings s;
  s.level = 2;
  s.humanSide = SideChoice::Random;
  match->newGame(s, 11);
  // Let a second level-2 engine stand in for the human until the game ends.
  auto human = std::make_unique<Engine>();
  human->seed(99);
  int guard = 0;
  while (!match->game().isOver() && ++guard < 4000) {
    if (match->isHumanTurn()) {
      const Game& g = match->game();
      SearchLimits limits;
      limits.maxDepth = 2;
      limits.marginCp = 30;
      human->start(g.position(), g.keys(), g.keyCount(), limits, realMillis());
      while (!human->step(realMillis, 50)) {
      }
      const Move m = human->bestMove();
      ASSERT_TRUE(match->play(m.from, m.to, m.promo));
    } else {
      match->update();
    }
  }
  EXPECT_TRUE(match->game().isOver());
  EXPECT_NE(match->game().termination(), Termination::None);
}
