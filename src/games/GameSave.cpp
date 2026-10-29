#include "GameSave.h"

#include <Fifteen.h>
#include <FiveLetters.h>
#include <Game2048.h>
#include <HalStorage.h>
#include <Klondike.h>
#include <Logging.h>
#include <Memory.h>
#include <Minesweeper.h>
#include <Sudoku.h>
#include <TicTacToe.h>

#include <cstdio>
#include <string>

namespace gamesave {
namespace {

constexpr const char* SAVE_DIR = "/.crosspoint/games";

struct Paths {
  const char* path;
  const char* tmp;
};

Paths pathsOf(const Kind kind) {
  switch (kind) {
    case Kind::Sudoku:
      return {"/.crosspoint/games/sudoku.json", "/.crosspoint/games/sudoku.json.tmp"};
    case Kind::Minesweeper:
      return {"/.crosspoint/games/minesweeper.json", "/.crosspoint/games/minesweeper.json.tmp"};
    case Kind::TicTacToe:
      return {"/.crosspoint/games/tictactoe.json", "/.crosspoint/games/tictactoe.json.tmp"};
    case Kind::Fifteen:
      return {"/.crosspoint/games/fifteen.json", "/.crosspoint/games/fifteen.json.tmp"};
    case Kind::FiveLetters:
      return {"/.crosspoint/games/fiveletters.json", "/.crosspoint/games/fiveletters.json.tmp"};
    case Kind::Game2048:
      return {"/.crosspoint/games/2048.json", "/.crosspoint/games/2048.json.tmp"};
    case Kind::Klondike:
      break;
  }
  return {"/.crosspoint/games/klondike.json", "/.crosspoint/games/klondike.json.tmp"};
}

constexpr Paths TICTACTOE_DONE{"/.crosspoint/games/tictactoe-done.json", "/.crosspoint/games/tictactoe-done.json.tmp"};
constexpr Paths FIFTEEN_BEST{"/.crosspoint/games/fifteen-best.json", "/.crosspoint/games/fifteen-best.json.tmp"};
constexpr Paths FIVE_LETTERS_STATS{"/.crosspoint/games/fiveletters-stats.json",
                                   "/.crosspoint/games/fiveletters-stats.json.tmp"};
constexpr Paths BEST_2048{"/.crosspoint/games/2048-best.txt", "/.crosspoint/games/2048-best.txt.tmp"};

// Crash-safe: tmp file + rename.
bool writeText(const Paths& p, const char* text, const size_t len) {
  if (len == 0 || !Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("GAMES", "Cannot prepare save %s", p.path);
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("GAMES", p.tmp, file)) return false;
  const size_t written = file.write(text, len);
  file.close();
  if (written != len || !Storage.replaceFile(p.tmp, p.path)) {
    LOG_ERR("GAMES", "Save failed (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
    Storage.remove(p.tmp);
    return false;
  }
  return true;
}

// Cold path (exit, sleep, after a move): one bounded buffer, freed on return.
template <typename T>
bool writeTo(const Paths& p, const T& value, const size_t cap) {
  auto text = makeUniqueNoThrow<char[]>(cap);
  if (!text) {
    LOG_ERR("GAMES", "OOM: %u bytes for save", static_cast<unsigned>(cap));
    return false;
  }
  return writeText(p, text.get(), value.serialize(text.get(), cap));
}

template <typename GameT>
bool write(const Kind kind, const GameT& game, const size_t cap) {
  if (!game.inProgress()) {
    clear(kind);
    return true;
  }
  return writeTo(pathsOf(kind), game, cap);
}

// A read error may be transient: the file is kept.
bool readText(const Paths& p, const size_t cap, std::string& text) {
  if (!Storage.exists(p.path)) return false;
  if (!Storage.readFileToString("GAMES", p.path, cap, text)) {
    LOG_ERR("GAMES", "Cannot read %s", p.path);
    return false;
  }
  return true;
}

// Only content that fails to parse is dropped.
template <typename T>
bool readFrom(const Paths& p, T& value, const size_t cap) {
  std::string text;
  if (!readText(p, cap, text)) return false;
  if (!value.deserialize(text.data(), text.size())) {
    LOG_ERR("GAMES", "Discarding corrupt %s", p.path);
    Storage.remove(p.path);
    return false;
  }
  return true;
}

template <typename GameT>
bool read(const Kind kind, GameT& game, const size_t cap) {
  return readFrom(pathsOf(kind), game, cap);
}

}  // namespace

bool hasGameInProgress(const Kind kind) { return Storage.exists(pathsOf(kind).path); }

void clear(const Kind kind) {
  const char* path = pathsOf(kind).path;
  if (Storage.exists(path)) Storage.remove(path);
}

bool save(const sudoku::Game& game) { return write(Kind::Sudoku, game, sudoku::SAVE_MAX); }
bool save(const minesweeper::Game& game) { return write(Kind::Minesweeper, game, minesweeper::SAVE_MAX); }
bool save(const klondike::Game& game) { return write(Kind::Klondike, game, klondike::SAVE_MAX); }
bool save(const tictactoe::Game& game) {
  if (game.inProgress()) return writeTo(pathsOf(Kind::TicTacToe), game, tictactoe::SAVE_MAX);
  const bool ok = writeTo(TICTACTOE_DONE, game, tictactoe::SAVE_MAX);
  if (ok) clear(Kind::TicTacToe);
  return ok;
}
bool save(const fifteen::Game& game) { return write(Kind::Fifteen, game, fifteen::SAVE_MAX); }
bool save(const fiveletters::Game& game) { return write(Kind::FiveLetters, game, fiveletters::SAVE_MAX); }
bool save(const g2048::Game& game) { return write(Kind::Game2048, game, g2048::SAVE_MAX); }

bool load(sudoku::Game& game) { return read(Kind::Sudoku, game, sudoku::SAVE_MAX); }
bool load(minesweeper::Game& game) { return read(Kind::Minesweeper, game, minesweeper::SAVE_MAX); }
bool load(klondike::Game& game) { return read(Kind::Klondike, game, klondike::SAVE_MAX); }
bool load(tictactoe::Game& game) {
  return read(Kind::TicTacToe, game, tictactoe::SAVE_MAX) || readFrom(TICTACTOE_DONE, game, tictactoe::SAVE_MAX);
}
bool load(fifteen::Game& game) { return read(Kind::Fifteen, game, fifteen::SAVE_MAX); }
bool load(fiveletters::Game& game) { return read(Kind::FiveLetters, game, fiveletters::SAVE_MAX); }
bool load(g2048::Game& game) { return read(Kind::Game2048, game, g2048::SAVE_MAX); }

bool save(const fifteen::Best& best) {
  char text[48];
  return writeText(FIFTEEN_BEST, text, best.serialize(text, sizeof(text)));
}
bool load(fifteen::Best& best) { return readFrom(FIFTEEN_BEST, best, 48); }

bool saveStats(const fiveletters::Stats& stats) {
  char text[fiveletters::STATS_MAX];
  return writeText(FIVE_LETTERS_STATS, text, stats.serialize(text, sizeof(text)));
}

bool loadStats(fiveletters::Stats& stats) {
  stats = fiveletters::Stats{};
  return !Storage.exists(FIVE_LETTERS_STATS.path) || readFrom(FIVE_LETTERS_STATS, stats, fiveletters::STATS_MAX);
}

uint32_t loadBest2048() {
  std::string text;
  unsigned long best = 0;
  if (!readText(BEST_2048, 16, text) || sscanf(text.c_str(), "%lu", &best) != 1) return 0;
  return static_cast<uint32_t>(best);
}

void saveBest2048(const uint32_t score) {
  char text[16];
  writeText(BEST_2048, text, snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(score)));
}

}  // namespace gamesave
