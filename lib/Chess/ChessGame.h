#pragma once

// One game's history and rules: legal play, SAN move sheet, undo, and every
// automatic ending (mate, stalemate, threefold, fifty moves, dead material).

#include "ChessBoard.h"

namespace chess {

enum class Outcome : uint8_t { Ongoing, WhiteWins, BlackWins, Draw };

enum class Termination : uint8_t {
  None,
  Checkmate,
  Stalemate,
  Threefold,
  FiftyMoves,
  InsufficientMaterial,
  MoveLimit,  // MAX_PLIES reached; scored as a draw
  Timeout,
  TimeoutVsInsufficientMaterial,  // flag fell but the opponent has a bare king: draw
  Resignation,
  Agreement,
};

// Piece tokens for move-list output; pieces[PAWN] stays empty. A screen may pass
// its own table (figurine glyphs, other locales).
struct Notation {
  const char* pieces[7];
  const char* castleShort;
  const char* castleLong;
};

extern const Notation NOTATION_EN;  // N B R Q K, O-O
extern const Notation NOTATION_RU;  // К С Л Ф Кр, 0-0

// Rewrites English SAN with `notation`'s tokens into `out` (always terminated).
// Returns the length written.
size_t formatSan(const char* san, const Notation& notation, char* out, size_t cap);

// Longest game kept; reaching it ends the game as a draw.
constexpr int MAX_PLIES = 1024;

struct PlyRecord {
  Move move;
  char san[SAN_MAX];  // English SAN, see formatSan
};

class Game {
 public:
  Game() { reset(); }

  void reset();
  // Starts from `fen`; false (game unchanged) when the FEN is invalid.
  bool reset(const char* fen);

  const Position& position() const { return pos; }
  const Position& startPosition() const { return start; }
  Color sideToMove() const { return pos.side; }
  bool inCheck() const { return chess::inCheck(pos); }

  int plyCount() const { return count; }
  const PlyRecord& ply(const int i) const { return plies[i]; }
  int moveNumberOfPly(int i) const;
  Color colorOfPly(int i) const;
  bool lastMove(Move& out) const;

  int legalMoves(MoveList& list) const { return generateLegal(pos, list); }
  // The legal move from -> to. `promo` is required for promotions and ignored otherwise.
  bool findMove(Square from, Square to, PieceType promo, Move& out) const;
  bool isPromotion(Square from, Square to) const;
  // Bit `sq` set for every legal destination of the piece on `from`.
  uint64_t legalTargets(Square from) const;

  // Plays a legal move of the current position. False when illegal or the game is over.
  bool play(const Move& m);
  bool playUci(const char* uci);
  // Takes back the last ply and reopens the game. False at the start position.
  bool undo();

  bool isOver() const { return result != Outcome::Ongoing; }
  Outcome outcome() const { return result; }
  Termination termination() const { return reason; }
  // Ends the game for a reason the board cannot see (clock, resignation, agreement).
  void finish(Outcome outcome, Termination termination);

  // Hashes of every position so far, oldest first; the last is the current one.
  const uint64_t* keys() const { return positionKeys; }
  int keyCount() const { return count + 1; }

 private:
  void restart(const Position& from);
  void updateOutcome();
  int repetitions() const;

  Position start;
  Position pos;
  PlyRecord plies[MAX_PLIES];
  uint64_t positionKeys[MAX_PLIES + 1];
  int count = 0;
  Outcome result = Outcome::Ongoing;
  Termination reason = Termination::None;
  mutable MoveList scratch;
};

}  // namespace chess
