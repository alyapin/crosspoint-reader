#pragma once

// Chess rules primitives: position, move generation, copy-make, FEN, SAN, UCI.
// Freestanding C++ (no Arduino, no heap) so it is host-testable.

#include <cstddef>
#include <cstdint>

namespace chess {

enum Color : uint8_t { WHITE = 0, BLACK = 1 };
constexpr Color opposite(const Color c) { return static_cast<Color>(c ^ 1); }

enum PieceType : uint8_t { NO_PIECE = 0, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING };

// Board byte: type in bits 0-2, colour in bit 3. 0 is an empty square.
using Piece = uint8_t;
constexpr Piece makePiece(const Color c, const PieceType t) { return static_cast<Piece>(t | (c << 3)); }
constexpr PieceType typeOf(const Piece p) { return static_cast<PieceType>(p & 7); }
constexpr Color colorOf(const Piece p) { return static_cast<Color>((p >> 3) & 1); }

// Public squares are 0..63: a1 = 0, h1 = 7, a8 = 56, h8 = 63.
using Square = uint8_t;
constexpr uint8_t fileOf(const Square s) { return s & 7; }
constexpr uint8_t rankOf(const Square s) { return s >> 3; }
constexpr Square makeSquare(const int file, const int rank) { return static_cast<Square>(rank * 8 + file); }

// The board is stored 0x88 (index = rank * 16 + file) so off-board tests are one AND.
constexpr int to88(const Square s) { return s + (s & ~7); }
constexpr Square from88(const int s) { return static_cast<Square>((s + (s & 7)) >> 1); }
constexpr uint8_t NO_EP = 0x80;

enum CastlingRight : uint8_t { WHITE_OO = 1, WHITE_OOO = 2, BLACK_OO = 4, BLACK_OOO = 8 };

enum MoveFlag : uint8_t {
  MOVE_QUIET = 0,
  MOVE_CAPTURE = 1,
  MOVE_DOUBLE_PUSH = 2,
  MOVE_EN_PASSANT = 4,
  MOVE_CASTLE = 8,
  MOVE_PROMOTION = 16,
};

struct Move {
  Square from = 0;
  Square to = 0;
  PieceType promo = NO_PIECE;
  uint8_t flags = MOVE_QUIET;

  bool isNull() const { return from == to; }
  bool sameAs(const Move& o) const { return from == o.from && to == o.to && promo == o.promo; }
  bool isCapture() const { return (flags & MOVE_CAPTURE) != 0; }
  bool isPromotion() const { return (flags & MOVE_PROMOTION) != 0; }
};

// 218 is the most legal moves any reachable position has.
constexpr int MAX_MOVES = 256;

struct MoveList {
  Move moves[MAX_MOVES];
  int count = 0;
};

struct Position {
  Piece board[128];
  uint8_t king[2];  // 0x88 squares
  Color side;
  uint8_t castling;
  // 0x88 en passant target, set only when an enemy pawn stands next to the pushed
  // pawn. Keeps the hash equal for positions that differ only by a dead ep square.
  uint8_t ep;
  uint8_t halfmove;  // saturates at 255
  uint16_t fullmove;
  uint64_t key;  // Zobrist hash, maintained by makeMove

  Piece at(const Square s) const { return board[to88(s)]; }
};

constexpr size_t FEN_MAX = 92;  // longest FEN plus terminator
constexpr size_t SAN_MAX = 8;   // "Qa1xb2+" / "exd8=Q#" plus terminator
constexpr size_t UCI_MAX = 6;   // "e7e8q" plus terminator

extern const char* const START_FEN;

void setStartPosition(Position& pos);
// Validates the record (one king each, side not to move not in check, no pawns on
// the back ranks). Leaves `pos` untouched on failure.
bool parseFen(const char* fen, Position& pos);
void writeFen(const Position& pos, char* out);  // out holds FEN_MAX

uint64_t computeKey(const Position& pos);
bool isAttacked(const Position& pos, int sq88, Color by);
inline bool inCheck(const Position& pos) { return isAttacked(pos, pos.king[pos.side], opposite(pos.side)); }

// Pseudo-legal moves into `out` (MAX_MOVES slots); returns the count. `noisyOnly`
// keeps captures and queen promotions, for quiescence search.
int generatePseudo(const Position& pos, Move* out, bool noisyOnly);
int generateLegal(const Position& pos, MoveList& list);
bool hasLegalMove(const Position& pos, MoveList& scratch);

// Copy-make: writes the successor into `dst` and returns false when the move
// leaves the mover's king attacked. `m` must come from generatePseudo(src).
bool makeMove(const Position& src, const Move& m, Position& dst);
void makeNullMove(const Position& src, Position& dst);

// Neither side can mate by any sequence: K v K, K+minor v K, bishops all on one colour.
bool insufficientMaterial(const Position& pos);
// The side has at least one piece other than king and pawns.
bool hasNonPawnMaterial(const Position& pos, Color side);
bool hasOnlyKing(const Position& pos, Color side);

void moveToUci(const Move& m, char* out);  // out holds UCI_MAX
// Matches a UCI string against the legal moves; false if it is not legal here.
bool parseUci(const Position& pos, const char* uci, Move& out, MoveList& scratch);
// English SAN for `m` played from `pos`; `scratch` is used for disambiguation and the check suffix.
void moveToSan(const Position& pos, const Move& m, char* out, MoveList& scratch);

}  // namespace chess
