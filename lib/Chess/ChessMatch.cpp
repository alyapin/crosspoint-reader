#include "ChessMatch.h"

#include <cstring>

namespace chess {
namespace {

constexpr uint32_t SAVE_VERSION = 1;

MatchSettings sanitize(MatchSettings s) {
  if (s.mode != Mode::HotSeat) s.mode = Mode::VsComputer;
  if (s.humanSide != SideChoice::Black && s.humanSide != SideChoice::Random) s.humanSide = SideChoice::White;
  if (s.level < MIN_LEVEL) s.level = MIN_LEVEL;
  if (s.level > MAX_LEVEL) s.level = MAX_LEVEL;
  if (s.time.baseMs > MAX_BASE_MS) s.time.baseMs = MAX_BASE_MS;
  if (s.time.incrementMs > MAX_INCREMENT_MS) s.time.incrementMs = MAX_INCREMENT_MS;
  if (s.time.unlimited()) s.time.incrementMs = 0;
  return s;
}

struct Writer {
  char* out;
  size_t cap;
  size_t len = 0;
  bool ok = true;

  void put(const char c) {
    if (len + 1 < cap) {
      out[len++] = c;
    } else {
      ok = false;
    }
  }
  void text(const char* s) {
    while (*s != '\0') put(*s++);
  }
  void number(uint32_t v) {
    char digits[10];
    int n = 0;
    do {
      digits[n++] = static_cast<char>('0' + v % 10);
      v /= 10;
    } while (v != 0);
    while (n > 0) put(digits[--n]);
  }
  void field(const char* key, const uint32_t v) {
    put('"');
    text(key);
    text("\":");
    number(v);
    put(',');
  }
};

// Reader for the flat object serialize() writes. Values never contain quotes.
struct Reader {
  const char* text;
  size_t len;

  const char* value(const char* key) const {
    const size_t keyLen = strlen(key);
    for (size_t i = 0; i + keyLen + 2 <= len; ++i) {
      if (text[i] != '"' || strncmp(text + i + 1, key, keyLen) != 0 || text[i + 1 + keyLen] != '"') continue;
      size_t j = i + keyLen + 2;
      while (j < len && text[j] == ' ') ++j;
      if (j >= len || text[j] != ':') continue;
      ++j;
      while (j < len && text[j] == ' ') ++j;
      return j < len ? text + j : nullptr;
    }
    return nullptr;
  }

  bool number(const char* key, uint32_t& out) const {
    const char* p = value(key);
    if (p == nullptr || *p < '0' || *p > '9') return false;
    uint64_t v = 0;
    while (p < text + len && *p >= '0' && *p <= '9') {
      v = v * 10 + static_cast<uint64_t>(*p - '0');
      if (v > UINT32_MAX) return false;
      ++p;
    }
    out = static_cast<uint32_t>(v);
    return true;
  }

  bool string(const char* key, const char*& begin, size_t& n) const {
    const char* p = value(key);
    if (p == nullptr || *p != '"') return false;
    begin = ++p;
    while (p < text + len && *p != '"') {
      if (*p == '\\') return false;
      ++p;
    }
    if (p >= text + len) return false;
    n = static_cast<size_t>(p - begin);
    return true;
  }
};

}  // namespace

bool Match::newGame(const MatchSettings& settings, const uint32_t seed, const char* fen) {
  if (fen != nullptr) {
    if (!record.reset(fen)) return false;
  } else {
    record.reset();
  }
  config = sanitize(settings);
  if (config.humanSide == SideChoice::Random) {
    human = ((seed * 2654435761u) >> 31) != 0 ? BLACK : WHITE;
  } else {
    human = config.humanSide == SideChoice::Black ? BLACK : WHITE;
  }
  engine.cancel();
  engine.newGame();
  engine.seed(seed);
  isPaused = false;
  const uint32_t t = now();
  clock.reset(config.time, record.sideToMove(), t);
  if (record.isOver()) clock.stop(t);
  return true;
}

bool Match::isHumanTurn() const {
  if (record.isOver()) return false;
  return config.mode == Mode::HotSeat || record.sideToMove() == human;
}

uint64_t Match::legalTargets(const Square from) const { return isHumanTurn() ? record.legalTargets(from) : 0; }

bool Match::play(const Square from, const Square to, const PieceType promo) {
  if (isPaused || !isHumanTurn()) return false;
  const uint32_t t = now();
  if (checkFlag(t)) return false;
  Move m;
  if (!record.findMove(from, to, promo, m)) return false;
  const Color mover = record.sideToMove();
  record.play(m);
  afterMove(mover, t);
  return true;
}

bool Match::update(const uint32_t sliceMs) {
  if (isPaused || record.isOver()) return false;
  if (checkFlag(now())) return true;
  if (config.mode != Mode::VsComputer || record.sideToMove() == human) return false;
  if (!engine.thinking() && !engine.ready()) startEngine(now());
  if (!engine.step(now, sliceMs)) return false;

  const Move m = engine.bestMove();
  engine.cancel();
  const uint32_t t = now();
  if (checkFlag(t)) return true;
  const Color mover = record.sideToMove();
  if (!record.play(m)) return false;
  afterMove(mover, t);
  return true;
}

void Match::startEngine(const uint32_t t) {
  const EngineLevel& level = engineLevel(config.level);
  SearchLimits limits;
  limits.maxDepth = level.maxDepth;
  limits.maxNodes = level.maxNodes;
  limits.marginCp = level.marginCp;
  limits.blunderPercent = level.blunderPercent;
  limits.blunderMarginCp = level.blunderMarginCp;
  uint32_t budget = level.moveTimeMs;
  if (timed()) {
    // About 1/20 of the remaining time plus most of the increment, never more than a third.
    const uint32_t left = clock.remainingMs(record.sideToMove(), t);
    const uint32_t share = left / 20 + config.time.incrementMs / 4 * 3;
    if (share < budget) budget = share;
    if (left / 3 < budget) budget = left / 3;
    if (budget < 10) budget = 10;
  }
  limits.hardMs = budget;
  limits.softMs = budget / 10 * 6;
  engine.start(record.position(), record.keys(), record.keyCount(), limits, t);
}

bool Match::checkFlag(const uint32_t t) {
  if (!timed() || record.isOver()) return false;
  const Color side = record.sideToMove();
  if (!clock.flagged(side, t)) return false;
  // A flag only loses when the opponent still has something to mate with.
  if (hasOnlyKing(record.position(), opposite(side))) {
    endGame(Outcome::Draw, Termination::TimeoutVsInsufficientMaterial);
  } else {
    endGame(side == WHITE ? Outcome::BlackWins : Outcome::WhiteWins, Termination::Timeout);
  }
  return true;
}

void Match::afterMove(const Color mover, const uint32_t t) {
  if (record.isOver()) {
    clock.stop(t);
  } else {
    clock.moveMade(mover, t);
  }
}

void Match::endGame(const Outcome outcome, const Termination termination) {
  record.finish(outcome, termination);
  clock.stop(now());
  engine.cancel();
}

int Match::undoPlies() const {
  const int plies = record.plyCount();
  if (plies == 0) return 0;
  switch (record.termination()) {
    case Termination::Timeout:
    case Termination::TimeoutVsInsufficientMaterial:
    case Termination::Resignation:
    case Termination::Agreement:
      return 0;
    default:
      break;
  }
  if (config.mode == Mode::HotSeat || record.colorOfPly(plies - 1) == human) return 1;
  // The engine moved last: take back its reply and the human move before it.
  return plies >= 2 ? 2 : 0;
}

bool Match::undo() {
  const int plies = undoPlies();
  if (plies == 0) return false;
  engine.cancel();
  for (int i = 0; i < plies; ++i) record.undo();
  clock.setTurn(record.sideToMove(), now());
  return true;
}

void Match::resign() {
  if (record.isOver()) return;
  const Color loser = config.mode == Mode::VsComputer ? human : record.sideToMove();
  endGame(loser == WHITE ? Outcome::BlackWins : Outcome::WhiteWins, Termination::Resignation);
}

void Match::agreeDraw() {
  if (!record.isOver()) endGame(Outcome::Draw, Termination::Agreement);
}

void Match::pause() {
  if (isPaused) return;
  clock.pause(now());
  engine.cancel();
  isPaused = true;
}

void Match::resume() {
  if (!isPaused) return;
  clock.resume(now());
  isPaused = false;
}

size_t Match::serialize(char* out, const size_t cap) const {
  if (out == nullptr || cap == 0) return 0;
  Writer w{out, cap};
  const uint32_t t = now();
  w.put('{');
  w.field("v", SAVE_VERSION);
  w.field("mode", config.mode == Mode::HotSeat ? 1 : 0);
  w.field("human", human == BLACK ? 1 : 0);
  w.field("level", config.level);
  w.field("base", config.time.baseMs);
  w.field("inc", config.time.incrementMs);
  w.field("white", clock.remainingMs(WHITE, t));
  w.field("black", clock.remainingMs(BLACK, t));
  char fen[FEN_MAX];
  writeFen(record.startPosition(), fen);
  w.text("\"fen\":\"");
  w.text(fen);
  w.text("\",\"moves\":\"");
  char uci[UCI_MAX];
  for (int i = 0; i < record.plyCount(); ++i) {
    if (i > 0) w.put(' ');
    moveToUci(record.ply(i).move, uci);
    w.text(uci);
  }
  w.text("\"}");
  if (!w.ok) return 0;
  out[w.len] = '\0';
  return w.len;
}

bool Match::deserialize(const char* text, const size_t len, const uint32_t seed) {
  const Reader r{text, text != nullptr ? len : 0};
  uint32_t version = 0;
  uint32_t mode = 0;
  uint32_t side = 0;
  uint32_t level = 0;
  uint32_t white = 0;
  uint32_t black = 0;
  MatchSettings s;
  const char* fen = nullptr;
  size_t fenLen = 0;
  const char* moves = nullptr;
  size_t movesLen = 0;
  const bool fieldsOk = r.number("v", version) && version == SAVE_VERSION && r.number("mode", mode) && mode <= 1 &&
                        r.number("human", side) && side <= 1 && r.number("level", level) && level >= MIN_LEVEL &&
                        level <= MAX_LEVEL && r.number("base", s.time.baseMs) && r.number("inc", s.time.incrementMs) &&
                        r.number("white", white) && r.number("black", black) && r.string("fen", fen, fenLen) &&
                        fenLen < FEN_MAX && r.string("moves", moves, movesLen);
  char fenText[FEN_MAX];
  if (fieldsOk) {
    memcpy(fenText, fen, fenLen);
    fenText[fenLen] = '\0';
  }
  s.mode = mode == 1 ? Mode::HotSeat : Mode::VsComputer;
  s.humanSide = side == 1 ? SideChoice::Black : SideChoice::White;
  s.level = static_cast<uint8_t>(level);
  if (!fieldsOk || !newGame(s, seed, fenText)) {
    newGame(MatchSettings{}, seed);
    return false;
  }

  char uci[UCI_MAX];
  size_t i = 0;
  while (i < movesLen) {
    if (moves[i] == ' ') {
      ++i;
      continue;
    }
    size_t n = 0;
    while (i < movesLen && moves[i] != ' ') {
      if (n + 1 >= UCI_MAX) {
        newGame(MatchSettings{}, seed);
        return false;
      }
      uci[n++] = moves[i++];
    }
    uci[n] = '\0';
    if (!record.playUci(uci)) {
      newGame(MatchSettings{}, seed);
      return false;
    }
  }

  clock.restore(config.time, record.sideToMove(), white, black);
  if (record.isOver()) clock.stop(now());
  isPaused = true;
  return true;
}

}  // namespace chess
