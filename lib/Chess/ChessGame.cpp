#include "ChessGame.h"

#include <cstring>

namespace chess {

const Notation NOTATION_EN = {{"", "", "N", "B", "R", "Q", "K"}, "O-O", "O-O-O"};
const Notation NOTATION_RU = {{"", "", "К", "С", "Л", "Ф", "Кр"}, "0-0", "0-0-0"};

size_t formatSan(const char* san, const Notation& notation, char* out, const size_t cap) {
  if (cap == 0) return 0;
  size_t n = 0;
  bool full = false;
  // Whole tokens only, so a multi-byte letter is never cut in half.
  const auto put = [&](const char* text, const size_t len) {
    if (full || n + len + 1 > cap) {
      full = true;
      return;
    }
    memcpy(out + n, text, len);
    n += len;
  };
  const char* p = san;
  if (strncmp(p, "O-O-O", 5) == 0) {
    put(notation.castleLong, strlen(notation.castleLong));
    p += 5;
  } else if (strncmp(p, "O-O", 3) == 0) {
    put(notation.castleShort, strlen(notation.castleShort));
    p += 3;
  }
  for (; *p != '\0'; ++p) {
    PieceType type = NO_PIECE;
    switch (*p) {
      case 'N':
        type = KNIGHT;
        break;
      case 'B':
        type = BISHOP;
        break;
      case 'R':
        type = ROOK;
        break;
      case 'Q':
        type = QUEEN;
        break;
      case 'K':
        type = KING;
        break;
      default:
        break;
    }
    if (type != NO_PIECE) {
      put(notation.pieces[type], strlen(notation.pieces[type]));
    } else {
      put(p, 1);
    }
  }
  out[n] = '\0';
  return n;
}

void Game::reset() {
  Position initial;
  setStartPosition(initial);
  restart(initial);
}

bool Game::reset(const char* fen) {
  Position initial;
  if (!parseFen(fen, initial)) return false;
  restart(initial);
  return true;
}

void Game::restart(const Position& from) {
  start = from;
  pos = from;
  count = 0;
  positionKeys[0] = pos.key;
  result = Outcome::Ongoing;
  reason = Termination::None;
  updateOutcome();
}

int Game::moveNumberOfPly(const int i) const {
  const int offset = start.side == BLACK ? 1 : 0;
  return start.fullmove + (i + offset) / 2;
}

Color Game::colorOfPly(const int i) const { return (i & 1) == 0 ? start.side : opposite(start.side); }

bool Game::lastMove(Move& out) const {
  if (count == 0) return false;
  out = plies[count - 1].move;
  return true;
}

bool Game::findMove(const Square from, const Square to, const PieceType promo, Move& out) const {
  const int n = generateLegal(pos, scratch);
  for (int i = 0; i < n; ++i) {
    const Move& m = scratch.moves[i];
    if (m.from != from || m.to != to) continue;
    if (m.isPromotion() && m.promo != promo) continue;
    out = m;
    return true;
  }
  return false;
}

bool Game::isPromotion(const Square from, const Square to) const {
  Move m;
  return findMove(from, to, QUEEN, m) && m.isPromotion();
}

uint64_t Game::legalTargets(const Square from) const {
  uint64_t mask = 0;
  const int n = generateLegal(pos, scratch);
  for (int i = 0; i < n; ++i) {
    if (scratch.moves[i].from == from) mask |= 1ull << scratch.moves[i].to;
  }
  return mask;
}

bool Game::play(const Move& m) {
  if (isOver()) return false;
  Move legal;
  if (!findMove(m.from, m.to, m.promo, legal)) return false;
  PlyRecord& record = plies[count];
  record.move = legal;
  moveToSan(pos, legal, record.san, scratch);
  Position next;
  makeMove(pos, legal, next);
  pos = next;
  ++count;
  positionKeys[count] = pos.key;
  updateOutcome();
  return true;
}

bool Game::playUci(const char* uci) {
  Move m;
  return parseUci(pos, uci, m, scratch) && play(m);
}

bool Game::undo() {
  if (count == 0) return false;
  const int target = count - 1;
  pos = start;
  Position next;
  for (int i = 0; i < target; ++i) {
    makeMove(pos, plies[i].move, next);
    pos = next;
  }
  count = target;
  result = Outcome::Ongoing;
  reason = Termination::None;
  updateOutcome();
  return true;
}

void Game::finish(const Outcome outcome, const Termination termination) {
  if (isOver() || outcome == Outcome::Ongoing) return;
  result = outcome;
  reason = termination;
}

int Game::repetitions() const {
  int seen = 1;
  const int oldest = count - pos.halfmove;
  for (int i = count - 2; i >= 0 && i >= oldest; i -= 2) {
    if (positionKeys[i] == positionKeys[count]) ++seen;
  }
  return seen;
}

void Game::updateOutcome() {
  if (!hasLegalMove(pos, scratch)) {
    if (chess::inCheck(pos)) {
      result = pos.side == WHITE ? Outcome::BlackWins : Outcome::WhiteWins;
      reason = Termination::Checkmate;
    } else {
      result = Outcome::Draw;
      reason = Termination::Stalemate;
    }
    return;
  }
  Termination draw = Termination::None;
  if (insufficientMaterial(pos)) {
    draw = Termination::InsufficientMaterial;
  } else if (pos.halfmove >= 100) {
    draw = Termination::FiftyMoves;
  } else if (repetitions() >= 3) {
    draw = Termination::Threefold;
  } else if (count >= MAX_PLIES) {
    draw = Termination::MoveLimit;
  }
  if (draw != Termination::None) {
    result = Outcome::Draw;
    reason = draw;
  }
}

}  // namespace chess
