// Host measurement of engine speed and level strength. Not a test.
//   ChessBench [slowdown=40] [games=20]
//   ChessBench pair <low> <high> <games> [slowdown=40]
//   ChessBench anchor <level> <stockfishElo> <games> [slowdown=40] [sfMoveMs=1000]
// `slowdown` is how many times slower the target CPU is than this host; level
// time budgets are divided by it so depths and results approximate the device.
// `anchor` plays a level against Stockfish (on PATH) with UCI_LimitStrength.

#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "ChessBoard.h"
#include "ChessEngine.h"
#include "ChessGame.h"

using namespace chess;

namespace {

uint32_t realMillis() {
  using namespace std::chrono;
  return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// CHESS_LEVELS="depth,margin,blunder%,blunderMargin;..." overrides levels 1.. for tuning runs.
EngineLevel levelFor(const int level) {
  EngineLevel lv = engineLevel(level);
  const char* spec = getenv("CHESS_LEVELS");
  for (int i = 1; spec != nullptr && *spec != '\0'; ++i) {
    int depth = 0;
    int margin = 0;
    int blunder = 0;
    int blunderMargin = 0;
    if (std::sscanf(spec, "%d,%d,%d,%d", &depth, &margin, &blunder, &blunderMargin) == 4 && i == level) {
      lv.maxDepth = static_cast<uint8_t>(depth);
      lv.marginCp = static_cast<int16_t>(margin);
      lv.blunderPercent = static_cast<uint8_t>(blunder);
      lv.blunderMarginCp = static_cast<int16_t>(blunderMargin);
    }
    spec = strchr(spec, ';');
    if (spec != nullptr) ++spec;
  }
  return lv;
}

SearchLimits limitsFor(const int level, const uint32_t slowdown) {
  const EngineLevel lv = levelFor(level);
  SearchLimits l;
  l.maxDepth = lv.maxDepth;
  l.maxNodes = lv.maxNodes;
  l.marginCp = lv.marginCp;
  l.blunderPercent = lv.blunderPercent;
  l.blunderMarginCp = lv.blunderMarginCp;
  l.hardMs = lv.moveTimeMs / slowdown;
  if (l.hardMs == 0) l.hardMs = 1;
  l.softMs = l.hardMs / 10 * 6;
  return l;
}

Move think(Engine& engine, const Game& game, const SearchLimits& limits, SearchInfo* info) {
  engine.start(game.position(), game.keys(), game.keyCount(), limits, realMillis());
  while (!engine.step(realMillis, 50)) {
  }
  if (info != nullptr) *info = engine.info();
  return engine.bestMove();
}

// +1 white wins, -1 black wins, 0 draw.
int playGame(Engine& white, Engine& black, const int whiteLevel, const int blackLevel, const uint32_t slowdown,
             uint32_t seed) {
  auto game = std::make_unique<Game>();
  MoveList list;
  // Two random plies so deterministic levels do not replay one game.
  for (int i = 0; i < 2; ++i) {
    game->legalMoves(list);
    seed = seed * 1103515245u + 12345u;
    game->play(list.moves[(seed >> 16) % list.count]);
  }
  white.newGame();
  black.newGame();
  while (!game->isOver()) {
    const bool whiteToMove = game->sideToMove() == WHITE;
    Engine& e = whiteToMove ? white : black;
    const Move m = think(e, *game, limitsFor(whiteToMove ? whiteLevel : blackLevel, slowdown), nullptr);
    game->play(m);
  }
  switch (game->outcome()) {
    case Outcome::WhiteWins:
      return 1;
    case Outcome::BlackWins:
      return -1;
    default:
      return 0;
  }
}

void ladder(Engine& a, Engine& b, const int low, const int high, const int games, const uint32_t slowdown) {
  int wins = 0;
  int draws = 0;
  int losses = 0;
  for (int g = 0; g < games; ++g) {
    a.seed(1000 + g);
    b.seed(2000 + g);
    const bool highWhite = (g & 1) == 0;
    const int r =
        highWhite ? playGame(a, b, high, low, slowdown, 77 + g / 2) : playGame(a, b, low, high, slowdown, 77 + g / 2);
    const int forHigh = highWhite ? r : -r;
    if (forHigh > 0) ++wins;
    if (forHigh == 0) ++draws;
    if (forHigh < 0) ++losses;
  }
  std::printf("  L%d vs L%d: +%d =%d -%d (%.0f%%)\n", high, low, wins, draws, losses,
              100.0 * (wins + 0.5 * draws) / games);
}

struct Stockfish {
  FILE* in = nullptr;
  FILE* out = nullptr;

  bool open(const int elo) {
    int toChild[2];
    int fromChild[2];
    if (pipe(toChild) != 0 || pipe(fromChild) != 0) return false;
    if (fork() == 0) {
      dup2(toChild[0], 0);
      dup2(fromChild[1], 1);
      execlp("stockfish", "stockfish", static_cast<char*>(nullptr));
      _exit(1);
    }
    close(toChild[0]);
    close(fromChild[1]);
    in = fdopen(toChild[1], "w");
    out = fdopen(fromChild[0], "r");
    send("uci");
    waitFor("uciok");
    send("setoption name UCI_LimitStrength value true");
    send(("setoption name UCI_Elo value " + std::to_string(elo)).c_str());
    send("isready");
    waitFor("readyok");
    return true;
  }
  void send(const char* line) {
    std::fprintf(in, "%s\n", line);
    std::fflush(in);
  }
  std::string waitFor(const char* prefix) {
    char line[4096];
    while (std::fgets(line, sizeof(line), out) != nullptr) {
      if (strncmp(line, prefix, strlen(prefix)) == 0) return line;
    }
    return "";
  }
  Move think(const Game& game, const uint32_t moveMs) {
    std::string cmd = "position fen ";
    char fen[FEN_MAX];
    writeFen(game.startPosition(), fen);
    cmd += fen;
    if (game.plyCount() > 0) cmd += " moves";
    char uci[UCI_MAX];
    for (int i = 0; i < game.plyCount(); ++i) {
      moveToUci(game.ply(i).move, uci);
      cmd += ' ';
      cmd += uci;
    }
    send(cmd.c_str());
    send(("go movetime " + std::to_string(moveMs)).c_str());
    const std::string reply = waitFor("bestmove ");
    char text[8] = {};
    std::sscanf(reply.c_str(), "bestmove %7s", text);
    Move m;
    MoveList scratch;
    parseUci(game.position(), text, m, scratch);
    return m;
  }
};

int anchor(const int level, const int elo, const int games, const uint32_t slowdown, const uint32_t sfMoveMs) {
  Stockfish sf;
  if (!sf.open(elo)) return 1;
  auto engine = std::make_unique<Engine>();
  int wins = 0;
  int draws = 0;
  int losses = 0;
  for (int g = 0; g < games; ++g) {
    engine->newGame();
    engine->seed(500 + g);
    sf.send("ucinewgame");
    auto game = std::make_unique<Game>();
    const bool oursWhite = (g & 1) == 0;
    while (!game->isOver()) {
      const bool ours = (game->sideToMove() == WHITE) == oursWhite;
      const Move m = ours ? think(*engine, *game, limitsFor(level, slowdown), nullptr) : sf.think(*game, sfMoveMs);
      if (!game->play(m)) break;
    }
    const Outcome o = game->outcome();
    const bool won = (o == Outcome::WhiteWins && oursWhite) || (o == Outcome::BlackWins && !oursWhite);
    const bool lost = (o == Outcome::WhiteWins && !oursWhite) || (o == Outcome::BlackWins && oursWhite);
    wins += won ? 1 : 0;
    losses += lost ? 1 : 0;
    draws += !won && !lost ? 1 : 0;
    std::fprintf(stderr, "game %d: %s\n", g, won ? "win" : (lost ? "loss" : "draw"));
  }
  std::printf("L%d vs Stockfish UCI_Elo %d (%u ms/move): +%d =%d -%d (%.0f%%)\n", level, elo, sfMoveMs, wins, draws,
              losses, 100.0 * (wins + 0.5 * draws) / games);
  sf.send("quit");
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 5 && strcmp(argv[1], "pair") == 0) {
    auto a = std::make_unique<Engine>();
    auto b = std::make_unique<Engine>();
    ladder(*a, *b, atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), argc > 5 ? static_cast<uint32_t>(atoi(argv[5])) : 40);
    return 0;
  }
  if (argc >= 5 && strcmp(argv[1], "anchor") == 0) {
    return anchor(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), argc > 5 ? static_cast<uint32_t>(atoi(argv[5])) : 40,
                  argc > 6 ? static_cast<uint32_t>(atoi(argv[6])) : 1000);
  }
  const uint32_t slowdown = argc > 1 ? static_cast<uint32_t>(atoi(argv[1])) : 40;
  const int games = argc > 2 ? atoi(argv[2]) : 20;
  auto a = std::make_unique<Engine>();
  auto b = std::make_unique<Engine>();

  const char* fens[] = {START_FEN, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
                        "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 3 9",
                        "8/5pk1/6p1/8/3R4/6P1/5PK1/1r6 w - - 0 1"};
  std::printf("Speed, level %d settings (no randomness), %u ms host = %u ms at slowdown %u:\n", MAX_LEVEL,
              engineLevel(MAX_LEVEL).moveTimeMs / slowdown, engineLevel(MAX_LEVEL).moveTimeMs, slowdown);
  for (const char* fen : fens) {
    auto game = std::make_unique<Game>();
    game->reset(fen);
    SearchInfo deviceInfo;
    think(*a, *game, limitsFor(MAX_LEVEL, slowdown), &deviceInfo);
    a->newGame();
    SearchLimits longer = limitsFor(MAX_LEVEL, 1);
    longer.hardMs = longer.softMs = 2000;
    SearchInfo hostInfo;
    think(*a, *game, longer, &hostInfo);
    a->newGame();
    std::printf("  depth %2u in device-equivalent time; host 2 s: depth %2u, %8u nodes, %6.0f knps  %s\n",
                deviceInfo.depth, hostInfo.depth, hostInfo.nodes,
                hostInfo.elapsedMs ? hostInfo.nodes / static_cast<double>(hostInfo.elapsedMs) : 0.0, fen);
  }

  std::printf("Level ladder, %d games per pair, colours alternate (score of the higher level):\n", games);
  for (int low = MIN_LEVEL; low < MAX_LEVEL; ++low) ladder(*a, *b, low, low + 1, games, slowdown);
  return 0;
}