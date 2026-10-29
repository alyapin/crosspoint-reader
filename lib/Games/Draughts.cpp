#include "Draughts.h"

#include <algorithm>

#include "GameText.h"

namespace draughts {

namespace {

constexpr uint32_t SAVE_VERSION = 1;
constexpr int MOVE_TEXT_MAX = 2 + 3 * MAX_JUMPS;
// Directions: up-left, up-right, down-left, down-right. White men go up.
constexpr int DR[4] = {-1, -1, 1, 1};
constexpr int DC[4] = {-1, 1, -1, 1};

struct Neighbors {
  int8_t next[SQUARES][4];
};

constexpr Neighbors makeNeighbors() {
  Neighbors n{};
  for (int s = 0; s < SQUARES; ++s) {
    const int row = s / 4;
    const int col = 2 * (s % 4) + (row % 2 == 0 ? 1 : 0);
    for (int d = 0; d < 4; ++d) {
      const int r = row + DR[d];
      const int c = col + DC[d];
      n.next[s][d] = static_cast<int8_t>(r < 0 || r > 7 || c < 0 || c > 7 ? -1 : r * 4 + c / 2);
    }
  }
  return n;
}

constexpr Neighbors NB = makeNeighbors();

constexpr uint32_t bit(const int square) { return 1u << square; }
constexpr bool has(const uint32_t set, const int square) { return (set >> square & 1u) != 0; }

bool lastRow(const Side side, const int square) { return side == White ? square < 4 : square >= SQUARES - 4; }

// Fills the moves of one position; captures first, plain moves only when there are none.
class Generator {
 public:
  Generator(const Position& pos, Move* out, const int cap)
      : pos(pos), out(out), cap(cap), foe(pos.side == White ? pos.black : pos.white) {}

  int run() {
    const uint32_t own = pos.side == White ? pos.white : pos.black;
    const uint32_t occupied = pos.white | pos.black;
    for (int s = 0; s < SQUARES; ++s) {
      if (!has(own, s)) continue;
      empty = ~occupied | bit(s);
      cur.from = static_cast<uint8_t>(s);
      jumps(s, has(pos.kings, s), false, 0, 0);
    }
    if (count > 0 || found) return count;
    empty = ~occupied;
    for (int s = 0; s < SQUARES; ++s) {
      if (has(own, s)) plain(s);
    }
    return count;
  }

 private:
  bool open(const int square) const { return square >= 0 && has(empty, square); }

  // The first piece along `d` a jump could take, or -1.
  int target(const int from, const int d, const bool king, const uint32_t taken) const {
    int s = NB.next[from][d];
    if (king) {
      while (open(s)) s = NB.next[s][d];
    }
    if (s < 0 || !has(foe, s) || has(taken, s) || !open(NB.next[s][d])) return -1;
    return s;
  }

  bool canJump(const int from, const bool king, const uint32_t taken) const {
    for (int d = 0; d < 4; ++d) {
      if (target(from, d, king, taken) >= 0) return true;
    }
    return false;
  }

  void jumps(const int from, const bool king, const bool promoted, const uint32_t taken, const int n) {
    for (int d = 0; d < 4; ++d) {
      const int victim = target(from, d, king, taken);
      if (victim < 0) continue;
      const uint32_t nowTaken = taken | bit(victim);
      // A king must land where it can go on, when any landing allows it.
      bool goOn = false;
      for (int s = NB.next[victim][d]; king && !goOn && open(s); s = NB.next[s][d]) goOn = canJump(s, true, nowTaken);
      for (int s = NB.next[victim][d]; open(s); s = king ? NB.next[s][d] : -1) {
        const bool crowned = king || lastRow(pos.side, s);
        const bool more = n + 1 < MAX_JUMPS && canJump(s, crowned, nowTaken);
        if (king && goOn && !more) continue;
        cur.path[n] = static_cast<uint8_t>(s);
        if (more) {
          jumps(s, crowned, promoted || crowned != king, nowTaken, n + 1);
        } else {
          found = true;
          cur.to = static_cast<uint8_t>(s);
          cur.captured = nowTaken;
          cur.jumps = static_cast<uint8_t>(n + 1);
          cur.promotes = promoted || crowned != king;
          emit();
        }
      }
    }
  }

  void plain(const int from) {
    const bool king = has(pos.kings, from);
    cur.from = static_cast<uint8_t>(from);
    cur.captured = 0;
    cur.jumps = 0;
    for (int d = 0; d < 4; ++d) {
      if (!king && (d < 2) != (pos.side == White)) continue;
      for (int s = NB.next[from][d]; open(s); s = king ? NB.next[s][d] : -1) {
        cur.to = static_cast<uint8_t>(s);
        cur.promotes = !king && lastRow(pos.side, s);
        emit();
      }
    }
  }

  void emit() {
    if (count < cap) out[count++] = cur;
  }

  const Position& pos;
  Move* out;
  int cap;
  uint32_t foe;
  uint32_t empty = 0;
  Move cur;
  int count = 0;
  bool found = false;
};

int pieceValue(const Position& p, const int s, const bool white) {
  const int row = rowOf(s);
  const int col = colOf(s);
  if (has(p.kings, s)) return 300 + (row + col == 7 ? 15 : 0);
  const int advance = white ? 7 - row : row;
  int value = 100 + 4 * advance;
  if (advance == 0) value += 8;
  if (row >= 3 && row <= 4 && col >= 2 && col <= 5) value += 6;
  return value;
}

void squareText(const int square, char* out) {
  out[0] = static_cast<char>('a' + colOf(square));
  out[1] = static_cast<char>('8' - rowOf(square));
}

int parseSquare(const char* text) {
  const int col = text[0] - 'a';
  const int row = '8' - text[1];
  return col >= 0 && col < 8 && row >= 0 && row < 8 ? squareAt(row, col) : -1;
}

constexpr char PIECES[] = ".wWbB";

}  // namespace

int rowOf(const int square) { return square / 4; }
int colOf(const int square) { return 2 * (square % 4) + (rowOf(square) % 2 == 0 ? 1 : 0); }
int squareAt(const int row, const int col) {
  return row >= 0 && row < 8 && col >= 0 && col < 8 && (row + col) % 2 == 1 ? row * 4 + col / 2 : -1;
}

Position Position::initial() {
  Position p;
  p.black = 0x00000FFFu;
  p.white = 0xFFF00000u;
  return p;
}

int legalMoves(const Position& pos, Move* out, const int cap) { return Generator(pos, out, cap).run(); }

void apply(Position& pos, const Step& step) {
  uint32_t& own = pos.side == White ? pos.white : pos.black;
  uint32_t& foe = pos.side == White ? pos.black : pos.white;
  const bool king = has(pos.kings, step.from);
  own = (own & ~bit(step.from)) | bit(step.to);
  foe &= ~step.captured;
  pos.kings &= ~(bit(step.from) | step.captured);
  if (king || step.promotes) pos.kings |= bit(step.to);
  pos.quiet = king && step.captured == 0 ? static_cast<uint8_t>(std::min(pos.quiet + 1, 255)) : 0;
  pos.side = pos.side == White ? Black : White;
}

int evaluate(const Position& pos) {
  int score = 0;
  for (uint32_t w = pos.white; w != 0; w &= w - 1) score += pieceValue(pos, __builtin_ctz(w), true);
  for (uint32_t b = pos.black; b != 0; b &= b - 1) score -= pieceValue(pos, __builtin_ctz(b), false);
  return pos.side == White ? score : -score;
}

// --- game ---------------------------------------------------------------------

void Game::newGame(const Mode mode, const uint32_t seed, const Position& start) {
  gameMode = mode;
  seedValue = seed;
  base = start;
  replay(0);
}

bool Game::lastMove(Step& out) const {
  if (plies == 0) return false;
  out = history[plies - 1];
  return true;
}

void Game::replay(const int count) {
  pos = base;
  for (int i = 0; i < count; ++i) apply(pos, history[i]);
  plies = count;
  judge();
}

void Game::judge() {
  state = Status::Playing;
  reason = End::None;
  Move any;
  if (legalMoves(pos, &any, 1) == 0) {
    state = pos.side == White ? Status::BlackWon : Status::WhiteWon;
    reason = End::NoMoves;
    return;
  }
  if (pos.quiet >= KING_DRAW_PLIES) {
    state = Status::Draw;
    reason = End::KingMoves;
    return;
  }
  // Only king moves repeat a position: earlier ones cannot match.
  int seen = 0;
  Position p = base;
  for (int k = 0; k < plies; ++k) {
    if (k >= plies - pos.quiet && p.samePlacement(pos)) ++seen;
    apply(p, history[k]);
  }
  if (seen >= 2) {
    state = Status::Draw;
    reason = End::Repetition;
  }
}

bool Game::play(const Move& move) {
  if (isOver()) return false;
  Move legal[MAX_MOVES];
  const int n = legalMoves(pos, legal, MAX_MOVES);
  const Step step = move.step();
  const bool ok = std::any_of(legal, legal + n, [&](const Move& m) {
    return m.from == step.from && m.to == step.to && m.captured == step.captured && m.promotes == step.promotes;
  });
  if (!ok) return false;
  if (plies == MAX_PLIES) {
    constexpr int kFold = MAX_PLIES / 2;
    for (int i = 0; i < kFold; ++i) apply(base, history[i]);
    std::copy(history + kFold, history + plies, history);
    plies -= kFold;
  }
  history[plies++] = step;
  apply(pos, step);
  judge();
  return true;
}

bool Game::undo() {
  if (!canUndo()) return false;
  replay(plies - undoPlies());
  return true;
}

void Game::resign() {
  if (isOver()) return;
  state = vsComputer() || pos.side == White ? Status::BlackWon : Status::WhiteWon;
  reason = End::Resigned;
}

bool Game::offerDraw() {
  if (isOver()) return false;
  if (vsComputer()) {
    const int computer = pos.side == Black ? evaluate(pos) : -evaluate(pos);
    if (computer > 0) return false;
  }
  state = Status::Draw;
  reason = End::Agreed;
  return true;
}

// --- save ---------------------------------------------------------------------

// {"v":1,"mode":0,"seed":7,"start":"bbbb…wwww","side":0,"quiet":0,"moves":"c3-d4 f6-g5 d4:f6"}
size_t Game::serialize(char* out, const size_t cap) const {
  games::TextWriter w(out, cap);
  w.number("v", SAVE_VERSION);
  w.number("mode", static_cast<uint32_t>(gameMode));
  w.number("seed", seedValue);
  w.beginString("start");
  for (int s = 0; s < SQUARES; ++s) {
    const int kind = has(base.white, s) ? 1 : has(base.black, s) ? 3 : 0;
    w.put(PIECES[kind + (kind != 0 && has(base.kings, s) ? 1 : 0)]);
  }
  w.endString();
  w.number("side", base.side);
  w.number("quiet", base.quiet);
  w.beginString("moves");
  Position p = base;
  Move legal[MAX_MOVES];
  for (int i = 0; i < plies; ++i) {
    const Step& step = history[i];
    const int n = legalMoves(p, legal, MAX_MOVES);
    const Move* m = std::find_if(legal, legal + n, [&](const Move& c) {
      return c.from == step.from && c.to == step.to && c.captured == step.captured;
    });
    if (m == legal + n) return 0;
    char text[MOVE_TEXT_MAX];
    squareText(m->from, text);
    int len = 2;
    for (int k = 0; k < m->stops(); ++k) {
      text[len++] = m->jumps > 0 ? ':' : '-';
      squareText(m->stopAt(k), text + len);
      len += 2;
    }
    if (i > 0) w.put(' ');
    for (int k = 0; k < len; ++k) w.put(text[k]);
    apply(p, step);
  }
  w.endString();
  return w.finish();
}

bool Game::deserialize(const char* text, const size_t len) {
  const games::TextReader r(text, len);
  uint32_t version = 0;
  uint32_t mode = 0;
  uint32_t seed = 0;
  uint32_t side = 0;
  uint32_t quiet = 0;
  const char* start = nullptr;
  size_t startLen = 0;
  const char* moves = nullptr;
  size_t movesLen = 0;
  if (!r.number("v", version) || version != SAVE_VERSION || !r.number("mode", mode) || mode >= MODE_COUNT ||
      !r.number("seed", seed) || !r.string("start", start, startLen) || startLen != SQUARES ||
      !r.number("side", side) || side > Black || !r.number("quiet", quiet) || quiet >= KING_DRAW_PLIES ||
      !r.string("moves", moves, movesLen)) {
    return false;
  }
  Position p;
  for (int s = 0; s < SQUARES; ++s) {
    const char* kind = std::find(PIECES, PIECES + 5, start[s]);
    const int k = static_cast<int>(kind - PIECES);
    if (k >= 5) return false;
    if (k == 1 || k == 2) p.white |= bit(s);
    if (k == 3 || k == 4) p.black |= bit(s);
    if (k == 2 || k == 4) p.kings |= bit(s);
    if ((k == 1 && lastRow(White, s)) || (k == 3 && lastRow(Black, s))) return false;
  }
  p.side = static_cast<Side>(side);
  p.quiet = static_cast<uint8_t>(quiet);

  Game g;
  g.newGame(static_cast<Mode>(mode), seed, p);
  Move legal[MAX_MOVES];
  size_t i = 0;
  while (i < movesLen) {
    if (g.isOver() || i + 2 > movesLen) return false;
    uint8_t stops[MAX_JUMPS];
    int count = 0;
    const int from = parseSquare(moves + i);
    const char sep = i + 2 < movesLen ? moves[i + 2] : ' ';
    i += 2;
    while (i + 3 <= movesLen && moves[i] == sep && count < MAX_JUMPS) {
      const int s = parseSquare(moves + i + 1);
      if (s < 0) return false;
      stops[count++] = static_cast<uint8_t>(s);
      i += 3;
    }
    if (from < 0 || count == 0 || (sep != '-' && sep != ':') || (sep == '-' && count != 1)) return false;
    if (i < movesLen && moves[i++] != ' ') return false;
    const int n = legalMoves(g.pos, legal, MAX_MOVES);
    const Move* m = std::find_if(legal, legal + n, [&](const Move& c) {
      if (c.from != from || c.stops() != count || (c.jumps > 0) != (sep == ':')) return false;
      for (int k = 0; k < count; ++k) {
        if (c.stopAt(k) != stops[k]) return false;
      }
      return true;
    });
    if (m == legal + n || !g.play(*m)) return false;
  }
  if (g.isOver()) return false;
  *this = g;
  return true;
}

}  // namespace draughts
