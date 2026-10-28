#include "ChessBoard.h"

#include <cstring>
#include <initializer_list>

namespace chess {

const char* const START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

namespace {

struct ZobristTable {
  uint64_t piece[16][64];
  uint64_t castling[16];
  uint64_t epFile[8];
  uint64_t side;
};

constexpr uint64_t splitMix(uint64_t& state) {
  state += 0x9E3779B97F4A7C15ull;
  uint64_t z = state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

constexpr ZobristTable makeZobrist() {
  ZobristTable z{};
  uint64_t state = 0x2545F4914F6CDD1Dull;
  for (auto& row : z.piece) {
    for (auto& key : row) key = splitMix(state);
  }
  for (auto& key : z.castling) key = splitMix(state);
  for (auto& key : z.epFile) key = splitMix(state);
  z.side = splitMix(state);
  return z;
}

constexpr ZobristTable ZOBRIST = makeZobrist();

// Rights that survive a move touching each 0x88 square (king or rook home squares).
constexpr uint8_t castleMask(const int sq) {
  switch (sq) {
    case 0x00:
      return static_cast<uint8_t>(~WHITE_OOO & 15);
    case 0x07:
      return static_cast<uint8_t>(~WHITE_OO & 15);
    case 0x04:
      return static_cast<uint8_t>(~(WHITE_OO | WHITE_OOO) & 15);
    case 0x70:
      return static_cast<uint8_t>(~BLACK_OOO & 15);
    case 0x77:
      return static_cast<uint8_t>(~BLACK_OO & 15);
    case 0x74:
      return static_cast<uint8_t>(~(BLACK_OO | BLACK_OOO) & 15);
    default:
      return 15;
  }
}

constexpr int KNIGHT_OFFSETS[8] = {33, 31, 18, 14, -33, -31, -18, -14};
constexpr int KING_OFFSETS[8] = {1, -1, 16, -16, 15, 17, -15, -17};
constexpr int BISHOP_DIRS[4] = {15, 17, -15, -17};
constexpr int ROOK_DIRS[4] = {1, -1, 16, -16};

constexpr bool offBoard(const int sq) { return (sq & 0x88) != 0; }

inline uint64_t pieceKey(const Piece p, const int sq88) { return ZOBRIST.piece[p][from88(sq88)]; }

char pieceChar(const PieceType t) { return " pnbrqk"[t]; }

PieceType pieceFromLower(const char c) {
  switch (c) {
    case 'p':
      return PAWN;
    case 'n':
      return KNIGHT;
    case 'b':
      return BISHOP;
    case 'r':
      return ROOK;
    case 'q':
      return QUEEN;
    case 'k':
      return KING;
    default:
      return NO_PIECE;
  }
}

const char* skipSpaces(const char* p) {
  while (*p == ' ') ++p;
  return p;
}

// Parses an unsigned decimal; returns false when no digit is present.
bool parseNumber(const char*& p, uint32_t& out) {
  if (*p < '0' || *p > '9') return false;
  uint32_t value = 0;
  while (*p >= '0' && *p <= '9') {
    if (value < 100000) value = value * 10 + static_cast<uint32_t>(*p - '0');
    ++p;
  }
  out = value;
  return true;
}

// An en passant target is only kept when a pawn of the side to move can capture onto it.
bool epCapturePossible(const Position& pos, const int epSq) {
  const Color us = pos.side;
  const int pawnSq = epSq + (us == WHITE ? -16 : 16);  // the pawn that just double-pushed
  const Piece ourPawn = makePiece(us, PAWN);
  const int left = pawnSq - 1;
  const int right = pawnSq + 1;
  return (!offBoard(left) && pos.board[left] == ourPawn) || (!offBoard(right) && pos.board[right] == ourPawn);
}

}  // namespace

uint64_t computeKey(const Position& pos) {
  uint64_t key = 0;
  for (int sq = 0; sq < 128; ++sq) {
    if (offBoard(sq)) {
      sq += 7;
      continue;
    }
    if (pos.board[sq] != 0) key ^= pieceKey(pos.board[sq], sq);
  }
  key ^= ZOBRIST.castling[pos.castling];
  if (pos.ep != NO_EP) key ^= ZOBRIST.epFile[pos.ep & 7];
  if (pos.side == BLACK) key ^= ZOBRIST.side;
  return key;
}

void setStartPosition(Position& pos) { parseFen(START_FEN, pos); }

bool parseFen(const char* fen, Position& out) {
  if (fen == nullptr) return false;
  Position pos;
  memset(&pos, 0, sizeof(pos));
  pos.ep = NO_EP;
  int kings[2] = {0, 0};
  int pawns[2] = {0, 0};
  int pieces[2] = {0, 0};

  const char* p = skipSpaces(fen);
  int rank = 7;
  int file = 0;
  while (*p != '\0' && *p != ' ') {
    const char c = *p++;
    if (c == '/') {
      if (file != 8 || rank == 0) return false;
      --rank;
      file = 0;
      continue;
    }
    if (c >= '1' && c <= '8') {
      file += c - '0';
      if (file > 8) return false;
      continue;
    }
    const bool white = c >= 'A' && c <= 'Z';
    const PieceType t = pieceFromLower(white ? static_cast<char>(c - 'A' + 'a') : c);
    if (t == NO_PIECE || file >= 8) return false;
    if (t == PAWN && (rank == 0 || rank == 7)) return false;
    const Color color = white ? WHITE : BLACK;
    if ((t == PAWN && ++pawns[color] > 8) || ++pieces[color] > 16) return false;
    const int sq = rank * 16 + file;
    pos.board[sq] = makePiece(color, t);
    if (t == KING) {
      ++kings[color];
      pos.king[color] = static_cast<uint8_t>(sq);
    }
    ++file;
  }
  if (rank != 0 || file != 8 || kings[WHITE] != 1 || kings[BLACK] != 1) return false;

  p = skipSpaces(p);
  if (*p == 'w') {
    pos.side = WHITE;
  } else if (*p == 'b') {
    pos.side = BLACK;
  } else {
    return false;
  }
  ++p;

  p = skipSpaces(p);
  if (*p == '-') {
    ++p;
  } else {
    while (*p != '\0' && *p != ' ') {
      switch (*p++) {
        case 'K':
          pos.castling |= WHITE_OO;
          break;
        case 'Q':
          pos.castling |= WHITE_OOO;
          break;
        case 'k':
          pos.castling |= BLACK_OO;
          break;
        case 'q':
          pos.castling |= BLACK_OOO;
          break;
        default:
          return false;
      }
    }
  }
  // Drop rights the pieces no longer support, so move generation can trust them.
  const Piece wk = makePiece(WHITE, KING), wr = makePiece(WHITE, ROOK);
  const Piece bk = makePiece(BLACK, KING), br = makePiece(BLACK, ROOK);
  if (pos.board[0x04] != wk) pos.castling &= ~(WHITE_OO | WHITE_OOO);
  if (pos.board[0x07] != wr) pos.castling &= ~WHITE_OO;
  if (pos.board[0x00] != wr) pos.castling &= ~WHITE_OOO;
  if (pos.board[0x74] != bk) pos.castling &= ~(BLACK_OO | BLACK_OOO);
  if (pos.board[0x77] != br) pos.castling &= ~BLACK_OO;
  if (pos.board[0x70] != br) pos.castling &= ~BLACK_OOO;

  p = skipSpaces(p);
  if (*p == '-') {
    ++p;
  } else if (*p >= 'a' && *p <= 'h' && (p[1] == '3' || p[1] == '6')) {
    const int epFile = p[0] - 'a';
    const int epRank = p[1] - '1';
    p += 2;
    const int epSq = epRank * 16 + epFile;
    const int expectedRank = pos.side == WHITE ? 5 : 2;
    const int pusherSq = epSq + (pos.side == WHITE ? -16 : 16);
    const int originSq = epSq + (pos.side == WHITE ? 16 : -16);
    if (epRank == expectedRank && pos.board[epSq] == 0 && pos.board[originSq] == 0 &&
        pos.board[pusherSq] == makePiece(opposite(pos.side), PAWN) && epCapturePossible(pos, epSq)) {
      pos.ep = static_cast<uint8_t>(epSq);
    }
  } else if (*p != '\0') {
    return false;
  }

  uint32_t number = 0;
  p = skipSpaces(p);
  if (parseNumber(p, number)) {
    pos.halfmove = static_cast<uint8_t>(number > 255 ? 255 : number);
    p = skipSpaces(p);
    pos.fullmove = parseNumber(p, number) && number > 0 ? static_cast<uint16_t>(number > 9999 ? 9999 : number) : 1;
  } else {
    pos.fullmove = 1;
  }

  // The side that just moved cannot have left its king in check.
  if (isAttacked(pos, pos.king[opposite(pos.side)], pos.side)) return false;

  pos.key = computeKey(pos);
  out = pos;
  return true;
}

void writeFen(const Position& pos, char* out) {
  char* o = out;
  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const Piece piece = pos.board[rank * 16 + file];
      if (piece == 0) {
        ++empty;
        continue;
      }
      if (empty > 0) *o++ = static_cast<char>('0' + empty);
      empty = 0;
      const char c = pieceChar(typeOf(piece));
      *o++ = colorOf(piece) == WHITE ? static_cast<char>(c - 'a' + 'A') : c;
    }
    if (empty > 0) *o++ = static_cast<char>('0' + empty);
    if (rank > 0) *o++ = '/';
  }
  *o++ = ' ';
  *o++ = pos.side == WHITE ? 'w' : 'b';
  *o++ = ' ';
  if (pos.castling == 0) *o++ = '-';
  if (pos.castling & WHITE_OO) *o++ = 'K';
  if (pos.castling & WHITE_OOO) *o++ = 'Q';
  if (pos.castling & BLACK_OO) *o++ = 'k';
  if (pos.castling & BLACK_OOO) *o++ = 'q';
  *o++ = ' ';
  if (pos.ep == NO_EP) {
    *o++ = '-';
  } else {
    *o++ = static_cast<char>('a' + (pos.ep & 7));
    *o++ = static_cast<char>('1' + (pos.ep >> 4));
  }
  // Bounded fields: halfmove <= 255, fullmove <= 9999.
  char digits[8];
  for (const unsigned value : {static_cast<unsigned>(pos.halfmove), static_cast<unsigned>(pos.fullmove)}) {
    *o++ = ' ';
    int n = 0;
    unsigned v = value;
    do {
      digits[n++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v != 0);
    while (n > 0) *o++ = digits[--n];
  }
  *o = '\0';
}

bool isAttacked(const Position& pos, const int sq, const Color by) {
  const Piece* b = pos.board;
  if (by == WHITE) {
    const Piece pawn = makePiece(WHITE, PAWN);
    if (!offBoard(sq - 15) && b[sq - 15] == pawn) return true;
    if (!offBoard(sq - 17) && b[sq - 17] == pawn) return true;
  } else {
    const Piece pawn = makePiece(BLACK, PAWN);
    if (!offBoard(sq + 15) && b[sq + 15] == pawn) return true;
    if (!offBoard(sq + 17) && b[sq + 17] == pawn) return true;
  }
  const Piece knight = makePiece(by, KNIGHT);
  for (const int off : KNIGHT_OFFSETS) {
    const int s = sq + off;
    if (!offBoard(s) && b[s] == knight) return true;
  }
  const Piece king = makePiece(by, KING);
  for (const int off : KING_OFFSETS) {
    const int s = sq + off;
    if (!offBoard(s) && b[s] == king) return true;
  }
  const Piece queen = makePiece(by, QUEEN);
  const Piece bishop = makePiece(by, BISHOP);
  for (const int dir : BISHOP_DIRS) {
    for (int s = sq + dir; !offBoard(s); s += dir) {
      const Piece p = b[s];
      if (p == 0) continue;
      if (p == bishop || p == queen) return true;
      break;
    }
  }
  const Piece rook = makePiece(by, ROOK);
  for (const int dir : ROOK_DIRS) {
    for (int s = sq + dir; !offBoard(s); s += dir) {
      const Piece p = b[s];
      if (p == 0) continue;
      if (p == rook || p == queen) return true;
      break;
    }
  }
  return false;
}

int generatePseudo(const Position& pos, Move* out, const bool noisyOnly) {
  int n = 0;
  const Color us = pos.side;
  const Color them = opposite(us);
  const Piece* b = pos.board;
  const auto add = [&](const int from, const int to, const uint8_t flags, const PieceType promo) {
    out[n++] = Move{from88(from), from88(to), promo, flags};
  };
  const auto addPromotions = [&](const int from, const int to, const uint8_t flags) {
    add(from, to, flags | MOVE_PROMOTION, QUEEN);
    if (noisyOnly) return;
    add(from, to, flags | MOVE_PROMOTION, ROOK);
    add(from, to, flags | MOVE_PROMOTION, BISHOP);
    add(from, to, flags | MOVE_PROMOTION, KNIGHT);
  };
  const auto isEnemy = [&](const Piece p) { return p != 0 && colorOf(p) == them; };

  for (int sq = 0; sq < 128; ++sq) {
    if (offBoard(sq)) {
      sq += 7;
      continue;
    }
    const Piece piece = b[sq];
    if (piece == 0 || colorOf(piece) != us) continue;
    const PieceType type = typeOf(piece);

    if (type == PAWN) {
      const int fwd = us == WHITE ? 16 : -16;
      const int rank = sq >> 4;
      const bool promoting = rank == (us == WHITE ? 6 : 1);
      const int to = sq + fwd;
      if (b[to] == 0) {
        if (promoting) {
          addPromotions(sq, to, MOVE_QUIET);
        } else if (!noisyOnly) {
          add(sq, to, MOVE_QUIET, NO_PIECE);
          if (rank == (us == WHITE ? 1 : 6) && b[to + fwd] == 0) add(sq, to + fwd, MOVE_DOUBLE_PUSH, NO_PIECE);
        }
      }
      for (const int t : {to - 1, to + 1}) {
        if (offBoard(t)) continue;
        if (isEnemy(b[t])) {
          if (promoting) {
            addPromotions(sq, t, MOVE_CAPTURE);
          } else {
            add(sq, t, MOVE_CAPTURE, NO_PIECE);
          }
        } else if (t == pos.ep) {
          add(sq, t, MOVE_CAPTURE | MOVE_EN_PASSANT, NO_PIECE);
        }
      }
      continue;
    }

    if (type == KNIGHT || type == KING) {
      const int* offsets = type == KNIGHT ? KNIGHT_OFFSETS : KING_OFFSETS;
      for (int i = 0; i < 8; ++i) {
        const int t = sq + offsets[i];
        if (offBoard(t)) continue;
        const Piece target = b[t];
        if (target == 0) {
          if (!noisyOnly) add(sq, t, MOVE_QUIET, NO_PIECE);
        } else if (colorOf(target) == them) {
          add(sq, t, MOVE_CAPTURE, NO_PIECE);
        }
      }
      continue;
    }

    const bool diagonal = type == BISHOP || type == QUEEN;
    const bool straight = type == ROOK || type == QUEEN;
    for (int d = 0; d < 8; ++d) {
      if (d < 4 ? !diagonal : !straight) continue;
      const int dir = d < 4 ? BISHOP_DIRS[d] : ROOK_DIRS[d - 4];
      for (int t = sq + dir; !offBoard(t); t += dir) {
        const Piece target = b[t];
        if (target == 0) {
          if (!noisyOnly) add(sq, t, MOVE_QUIET, NO_PIECE);
          continue;
        }
        if (colorOf(target) == them) add(sq, t, MOVE_CAPTURE, NO_PIECE);
        break;
      }
    }
  }

  // Castling rights imply king and rook on their home squares (kept by parseFen and makeMove).
  // The destination square is checked by makeMove's legality test.
  if (!noisyOnly && pos.castling != 0) {
    const int base = us == WHITE ? 0x00 : 0x70;
    const uint8_t oo = us == WHITE ? WHITE_OO : BLACK_OO;
    const uint8_t ooo = us == WHITE ? WHITE_OOO : BLACK_OOO;
    if ((pos.castling & oo) && b[base + 5] == 0 && b[base + 6] == 0 && !isAttacked(pos, base + 4, them) &&
        !isAttacked(pos, base + 5, them)) {
      add(base + 4, base + 6, MOVE_CASTLE, NO_PIECE);
    }
    if ((pos.castling & ooo) && b[base + 3] == 0 && b[base + 2] == 0 && b[base + 1] == 0 &&
        !isAttacked(pos, base + 4, them) && !isAttacked(pos, base + 3, them)) {
      add(base + 4, base + 2, MOVE_CASTLE, NO_PIECE);
    }
  }
  return n;
}

bool makeMove(const Position& src, const Move& m, Position& dst) {
  dst = src;
  const int from = to88(m.from);
  const int to = to88(m.to);
  const Color us = src.side;
  const Color them = opposite(us);
  const Piece moving = dst.board[from];
  uint64_t key = dst.key;

  if (dst.ep != NO_EP) key ^= ZOBRIST.epFile[dst.ep & 7];
  dst.ep = NO_EP;

  Piece captured = dst.board[to];
  if (m.flags & MOVE_EN_PASSANT) {
    const int capSq = to + (us == WHITE ? -16 : 16);
    captured = dst.board[capSq];
    dst.board[capSq] = 0;
    key ^= pieceKey(captured, capSq);
  } else if (captured != 0) {
    key ^= pieceKey(captured, to);
  }

  const Piece placed = (m.flags & MOVE_PROMOTION) ? makePiece(us, m.promo) : moving;
  dst.board[from] = 0;
  dst.board[to] = placed;
  key ^= pieceKey(moving, from) ^ pieceKey(placed, to);

  if (typeOf(moving) == KING) {
    dst.king[us] = static_cast<uint8_t>(to);
    if (m.flags & MOVE_CASTLE) {
      const int rookFrom = to > from ? to + 1 : to - 2;
      const int rookTo = to > from ? to - 1 : to + 1;
      const Piece rook = dst.board[rookFrom];
      dst.board[rookFrom] = 0;
      dst.board[rookTo] = rook;
      key ^= pieceKey(rook, rookFrom) ^ pieceKey(rook, rookTo);
    }
  }

  if (dst.castling != 0) {
    key ^= ZOBRIST.castling[dst.castling];
    dst.castling &= castleMask(from) & castleMask(to);
    key ^= ZOBRIST.castling[dst.castling];
  }

  if (m.flags & MOVE_DOUBLE_PUSH) {
    const Piece enemyPawn = makePiece(them, PAWN);
    const int left = to - 1;
    const int right = to + 1;
    if ((!offBoard(left) && dst.board[left] == enemyPawn) || (!offBoard(right) && dst.board[right] == enemyPawn)) {
      dst.ep = static_cast<uint8_t>((from + to) / 2);
      key ^= ZOBRIST.epFile[dst.ep & 7];
    }
  }

  if (typeOf(moving) == PAWN || captured != 0) {
    dst.halfmove = 0;
  } else if (dst.halfmove < 255) {
    ++dst.halfmove;
  }
  if (us == BLACK) ++dst.fullmove;
  dst.side = them;
  key ^= ZOBRIST.side;
  dst.key = key;
  return !isAttacked(dst, dst.king[us], them);
}

void makeNullMove(const Position& src, Position& dst) {
  dst = src;
  if (dst.ep != NO_EP) dst.key ^= ZOBRIST.epFile[dst.ep & 7];
  dst.ep = NO_EP;
  dst.side = opposite(src.side);
  dst.key ^= ZOBRIST.side;
  // Positions before a null move are not reachable by real play; reset the repetition window.
  dst.halfmove = 0;
}

int generateLegal(const Position& pos, MoveList& list) {
  const int n = generatePseudo(pos, list.moves, false);
  int legal = 0;
  Position next;
  for (int i = 0; i < n; ++i) {
    if (makeMove(pos, list.moves[i], next)) list.moves[legal++] = list.moves[i];
  }
  list.count = legal;
  return legal;
}

bool hasLegalMove(const Position& pos, MoveList& scratch) {
  const int n = generatePseudo(pos, scratch.moves, false);
  Position next;
  for (int i = 0; i < n; ++i) {
    if (makeMove(pos, scratch.moves[i], next)) return true;
  }
  return false;
}

bool insufficientMaterial(const Position& pos) {
  int knights = 0;
  int bishopColors = 0;  // bit 0: a bishop on a dark square, bit 1: on a light square
  for (int sq = 0; sq < 128; ++sq) {
    if (offBoard(sq)) {
      sq += 7;
      continue;
    }
    switch (typeOf(pos.board[sq])) {
      case PAWN:
      case ROOK:
      case QUEEN:
        return false;
      case KNIGHT:
        ++knights;
        break;
      case BISHOP:
        bishopColors |= 1 << (((sq >> 4) + (sq & 7)) & 1);
        break;
      default:
        break;
    }
  }
  if (knights == 0) return bishopColors != 3;
  return knights == 1 && bishopColors == 0;
}

bool hasNonPawnMaterial(const Position& pos, const Color side) {
  for (int sq = 0; sq < 128; ++sq) {
    if (offBoard(sq)) {
      sq += 7;
      continue;
    }
    const Piece p = pos.board[sq];
    if (p != 0 && colorOf(p) == side && typeOf(p) != PAWN && typeOf(p) != KING) return true;
  }
  return false;
}

bool hasOnlyKing(const Position& pos, const Color side) {
  for (int sq = 0; sq < 128; ++sq) {
    if (offBoard(sq)) {
      sq += 7;
      continue;
    }
    const Piece p = pos.board[sq];
    if (p != 0 && colorOf(p) == side && typeOf(p) != KING) return false;
  }
  return true;
}

void moveToUci(const Move& m, char* out) {
  out[0] = static_cast<char>('a' + fileOf(m.from));
  out[1] = static_cast<char>('1' + rankOf(m.from));
  out[2] = static_cast<char>('a' + fileOf(m.to));
  out[3] = static_cast<char>('1' + rankOf(m.to));
  int n = 4;
  if (m.promo != NO_PIECE) out[n++] = pieceChar(m.promo);
  out[n] = '\0';
}

bool parseUci(const Position& pos, const char* uci, Move& out, MoveList& scratch) {
  if (uci == nullptr) return false;
  const auto square = [](const char* s, Square& sq) {
    if (s[0] < 'a' || s[0] > 'h' || s[1] < '1' || s[1] > '8') return false;
    sq = makeSquare(s[0] - 'a', s[1] - '1');
    return true;
  };
  Square from = 0;
  Square to = 0;
  if (!square(uci, from) || !square(uci + 2, to)) return false;
  PieceType promo = NO_PIECE;
  if (uci[4] != '\0') {
    promo = pieceFromLower(uci[4]);
    if (promo == NO_PIECE || promo == PAWN || promo == KING || uci[5] != '\0') return false;
  }
  const int n = generateLegal(pos, scratch);
  for (int i = 0; i < n; ++i) {
    const Move& m = scratch.moves[i];
    if (m.from == from && m.to == to && m.promo == promo) {
      out = m;
      return true;
    }
  }
  return false;
}

void moveToSan(const Position& pos, const Move& m, char* out, MoveList& scratch) {
  char* o = out;
  const PieceType type = typeOf(pos.at(m.from));
  if (m.flags & MOVE_CASTLE) {
    const char* text = fileOf(m.to) == 6 ? "O-O" : "O-O-O";
    while (*text != '\0') *o++ = *text++;
  } else if (type == PAWN) {
    if (m.isCapture()) {
      *o++ = static_cast<char>('a' + fileOf(m.from));
      *o++ = 'x';
    }
    *o++ = static_cast<char>('a' + fileOf(m.to));
    *o++ = static_cast<char>('1' + rankOf(m.to));
    if (m.isPromotion()) {
      *o++ = '=';
      *o++ = static_cast<char>(pieceChar(m.promo) - 'a' + 'A');
    }
  } else {
    *o++ = static_cast<char>(pieceChar(type) - 'a' + 'A');
    bool ambiguous = false;
    bool sameFile = false;
    bool sameRank = false;
    const int n = generateLegal(pos, scratch);
    for (int i = 0; i < n; ++i) {
      const Move& other = scratch.moves[i];
      if (other.to != m.to || other.from == m.from || typeOf(pos.at(other.from)) != type) continue;
      ambiguous = true;
      if (fileOf(other.from) == fileOf(m.from)) sameFile = true;
      if (rankOf(other.from) == rankOf(m.from)) sameRank = true;
    }
    if (ambiguous) {
      if (!sameFile) {
        *o++ = static_cast<char>('a' + fileOf(m.from));
      } else if (!sameRank) {
        *o++ = static_cast<char>('1' + rankOf(m.from));
      } else {
        *o++ = static_cast<char>('a' + fileOf(m.from));
        *o++ = static_cast<char>('1' + rankOf(m.from));
      }
    }
    if (m.isCapture()) *o++ = 'x';
    *o++ = static_cast<char>('a' + fileOf(m.to));
    *o++ = static_cast<char>('1' + rankOf(m.to));
  }

  Position next;
  if (makeMove(pos, m, next) && inCheck(next)) *o++ = hasLegalMove(next, scratch) ? '+' : '#';
  *o = '\0';
}

}  // namespace chess
