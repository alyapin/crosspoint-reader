#include "GameSave.h"

#include <Fifteen.h>
#include <FiveLetters.h>
#include <Game2048.h>
#include <HalStorage.h>
#include <Hangman.h>
#include <Klondike.h>
#include <Logging.h>
#include <Memory.h>
#include <Minesweeper.h>
#include <Reversi.h>
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
    case Kind::Hangman:
      return {"/.crosspoint/games/hangman.json", "/.crosspoint/games/hangman.json.tmp"};
    case Kind::Reversi:
      return {"/.crosspoint/games/reversi.json", "/.crosspoint/games/reversi.json.tmp"};
    case Kind::Klondike:
      break;
  }
  return {"/.crosspoint/games/klondike.json", "/.crosspoint/games/klondike.json.tmp"};
}

// A save or record that is there but cannot be read is not written over until it reads.
struct Record {
  Paths paths;
  bool unread = false;
};

uint16_t unreadGames = 0;  // a bit per Kind

uint16_t bitOf(const Kind kind) { return static_cast<uint16_t>(1u << static_cast<unsigned>(kind)); }

Record tictactoeDone{{"/.crosspoint/games/tictactoe-done.json", "/.crosspoint/games/tictactoe-done.json.tmp"}};
Record reversiDone{{"/.crosspoint/games/reversi-done.json", "/.crosspoint/games/reversi-done.json.tmp"}};
Record fifteenBest{{"/.crosspoint/games/fifteen-best.json", "/.crosspoint/games/fifteen-best.json.tmp"}};
Record fiveLettersStats{{"/.crosspoint/games/fiveletters-stats.json", "/.crosspoint/games/fiveletters-stats.json.tmp"}};
Record best2048{{"/.crosspoint/games/2048-best.txt", "/.crosspoint/games/2048-best.txt.tmp"}};

enum class Read : uint8_t { Ok, Missing, Failed };

// Crash-safe: tmp file + rename.
bool writeText(const Paths& p, const char* text, const size_t len) {
  if (len == 0 || !Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("GAMES", "Cannot prepare save %s", p.path);
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("GAMES", p.tmp, file)) return false;
  const size_t written = file.write(text, len);
  if (!file.close() || written != len) {
    LOG_ERR("GAMES", "Save failed (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
    Storage.remove(p.tmp);
    return false;
  }
  if (!Storage.replaceFile(p.tmp, p.path)) {
    LOG_ERR("GAMES", "Cannot replace %s", p.path);
    // replaceFile removes the old file first: if it is gone, the tmp is the only copy.
    if (Storage.exists(p.path)) Storage.remove(p.tmp);
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
  if (!game.inProgress()) return clear(kind);
  return !(unreadGames & bitOf(kind)) && writeTo(pathsOf(kind), game, cap);
}

// A power cut inside replaceFile, after the old file is removed, leaves only
// the tmp; one cut short in its own write is empty.
bool present(const Paths& p) {
  if (Storage.exists(p.path)) return true;
  if (!Storage.exists(p.tmp)) return false;
  HalFile file;
  const bool empty = Storage.openFileForRead("GAMES", p.tmp, file) && file.fileSize() == 0;
  file.close();
  if (empty) {
    Storage.remove(p.tmp);
    return false;
  }
  LOG_INF("GAMES", "Recovering %s", p.path);
  if (!Storage.rename(p.tmp, p.path)) LOG_ERR("GAMES", "Cannot recover %s", p.path);
  return true;
}

// A read error may be transient: the file is kept.
Read readText(const Paths& p, const size_t cap, std::string& text) {
  if (!present(p)) return Read::Missing;
  if (!Storage.readFileToString("GAMES", p.path, cap, text)) {
    LOG_ERR("GAMES", "Cannot read %s", p.path);
    return Read::Failed;
  }
  return Read::Ok;
}

// Only content that fails to parse is dropped.
template <typename T>
Read readFrom(const Paths& p, T& value, const size_t cap) {
  std::string text;
  const Read result = readText(p, cap, text);
  if (result != Read::Ok) return result;
  if (!value.deserialize(text.data(), text.size())) {
    LOG_ERR("GAMES", "Discarding corrupt %s", p.path);
    Storage.remove(p.path);
    return Read::Missing;
  }
  return Read::Ok;
}

template <typename GameT>
bool read(const Kind kind, GameT& game, const size_t cap) {
  const Read result = readFrom(pathsOf(kind), game, cap);
  unreadGames = result == Read::Failed ? unreadGames | bitOf(kind) : unreadGames & ~bitOf(kind);
  return result == Read::Ok;
}

template <typename T>
Read readRecord(Record& record, T& value, const size_t cap) {
  const Read result = readFrom(record.paths, value, cap);
  record.unread = result == Read::Failed;
  return result;
}

template <typename T>
bool loadRecord(Record& record, T& value, const size_t cap) {
  value = T{};
  return readRecord(record, value, cap) != Read::Failed;
}

bool saveRecord(const Record& record, const char* text, const size_t len) {
  return !record.unread && writeText(record.paths, text, len);
}

}  // namespace

bool hasGameInProgress(const Kind kind) { return present(pathsOf(kind)); }

bool clear(const Kind kind) {
  if (unreadGames & bitOf(kind)) return false;
  const Paths p = pathsOf(kind);
  bool ok = true;
  for (const char* path : {p.path, p.tmp}) ok = (!Storage.exists(path) || Storage.remove(path)) && ok;
  return ok;
}

bool save(const sudoku::Game& game) { return write(Kind::Sudoku, game, sudoku::SAVE_MAX); }
bool save(const minesweeper::Game& game) { return write(Kind::Minesweeper, game, minesweeper::SAVE_MAX); }
bool save(const klondike::Game& game) { return write(Kind::Klondike, game, klondike::SAVE_MAX); }
bool save(const tictactoe::Game& game) {
  if (game.inProgress()) return write(Kind::TicTacToe, game, tictactoe::SAVE_MAX);
  return !tictactoeDone.unread && writeTo(tictactoeDone.paths, game, tictactoe::SAVE_MAX) && clear(Kind::TicTacToe);
}
bool save(const fifteen::Game& game) { return write(Kind::Fifteen, game, fifteen::SAVE_MAX); }
bool save(const fiveletters::Game& game) { return write(Kind::FiveLetters, game, fiveletters::SAVE_MAX); }
bool save(const g2048::Game& game) { return write(Kind::Game2048, game, g2048::SAVE_MAX); }
bool save(const hangman::Game& game) { return write(Kind::Hangman, game, hangman::SAVE_MAX); }
bool save(const reversi::Game& game) {
  if (game.inProgress()) return write(Kind::Reversi, game, reversi::SAVE_MAX);
  return !reversiDone.unread && writeTo(reversiDone.paths, game, reversi::SAVE_MAX) && clear(Kind::Reversi);
}

bool load(sudoku::Game& game) { return read(Kind::Sudoku, game, sudoku::SAVE_MAX); }
bool load(minesweeper::Game& game) { return read(Kind::Minesweeper, game, minesweeper::SAVE_MAX); }
bool load(klondike::Game& game) { return read(Kind::Klondike, game, klondike::SAVE_MAX); }
bool load(tictactoe::Game& game) {
  return read(Kind::TicTacToe, game, tictactoe::SAVE_MAX) ||
         readRecord(tictactoeDone, game, tictactoe::SAVE_MAX) == Read::Ok;
}
bool load(fifteen::Game& game) { return read(Kind::Fifteen, game, fifteen::SAVE_MAX); }
bool load(fiveletters::Game& game) { return read(Kind::FiveLetters, game, fiveletters::SAVE_MAX); }
bool load(g2048::Game& game) { return read(Kind::Game2048, game, g2048::SAVE_MAX); }
bool load(hangman::Game& game) { return read(Kind::Hangman, game, hangman::SAVE_MAX); }
bool load(reversi::Game& game) {
  return read(Kind::Reversi, game, reversi::SAVE_MAX) || readRecord(reversiDone, game, reversi::SAVE_MAX) == Read::Ok;
}

bool save(const fifteen::Best& best) {
  char text[48];
  return saveRecord(fifteenBest, text, best.serialize(text, sizeof(text)));
}
bool load(fifteen::Best& best) { return loadRecord(fifteenBest, best, 48); }

bool saveStats(const fiveletters::Stats& stats) {
  char text[fiveletters::STATS_MAX];
  return saveRecord(fiveLettersStats, text, stats.serialize(text, sizeof(text)));
}

bool loadStats(fiveletters::Stats& stats) { return loadRecord(fiveLettersStats, stats, fiveletters::STATS_MAX); }

uint32_t loadBest2048() {
  std::string text;
  unsigned long best = 0;
  const Read result = readText(best2048.paths, 16, text);
  best2048.unread = result == Read::Failed;
  if (result != Read::Ok || sscanf(text.c_str(), "%lu", &best) != 1) return 0;
  return static_cast<uint32_t>(best);
}

void saveBest2048(const uint32_t score) {
  char text[16];
  saveRecord(best2048, text, snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(score)));
}

}  // namespace gamesave
