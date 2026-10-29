#include "GameSave.h"

#include <Game2048.h>
#include <HalStorage.h>
#include <Klondike.h>
#include <Logging.h>
#include <Memory.h>
#include <Minesweeper.h>
#include <Sudoku.h>

#include <cstdio>
#include <string>

namespace gamesave {
namespace {

constexpr const char* SAVE_DIR = "/.crosspoint/games";
constexpr const char* BEST_2048_PATH = "/.crosspoint/games/2048-best.txt";

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
    case Kind::Game2048:
      return {"/.crosspoint/games/2048.json", "/.crosspoint/games/2048.json.tmp"};
    case Kind::Klondike:
      break;
  }
  return {"/.crosspoint/games/klondike.json", "/.crosspoint/games/klondike.json.tmp"};
}

// Cold path (exit, sleep, after a move): one bounded buffer, freed on return.
template <typename GameT>
bool write(const Kind kind, const GameT& game, const size_t cap) {
  const Paths p = pathsOf(kind);
  if (!game.inProgress()) {
    clear(kind);
    return true;
  }
  auto text = makeUniqueNoThrow<char[]>(cap);
  if (!text) {
    LOG_ERR("GAMES", "OOM: %u bytes for save", static_cast<unsigned>(cap));
    return false;
  }
  const size_t len = game.serialize(text.get(), cap);
  if (len == 0 || !Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("GAMES", "Cannot prepare save %s", p.path);
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("GAMES", p.tmp, file)) return false;
  const size_t written = file.write(text.get(), len);
  file.close();
  if (written != len || !Storage.replaceFile(p.tmp, p.path)) {
    LOG_ERR("GAMES", "Save failed (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
    Storage.remove(p.tmp);
    return false;
  }
  return true;
}

template <typename GameT>
bool read(const Kind kind, GameT& game, const size_t cap) {
  const Paths p = pathsOf(kind);
  if (!Storage.exists(p.path)) return false;
  std::string text;
  // A read error may be transient: keep the file. Only content that fails to parse is dropped.
  if (!Storage.readFileToString("GAMES", p.path, cap, text)) {
    LOG_ERR("GAMES", "Cannot read %s", p.path);
    return false;
  }
  if (!game.deserialize(text.data(), text.size())) {
    LOG_ERR("GAMES", "Discarding corrupt %s", p.path);
    Storage.remove(p.path);
    return false;
  }
  return true;
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
bool save(const g2048::Game& game) { return write(Kind::Game2048, game, g2048::SAVE_MAX); }

bool load(sudoku::Game& game) { return read(Kind::Sudoku, game, sudoku::SAVE_MAX); }
bool load(minesweeper::Game& game) { return read(Kind::Minesweeper, game, minesweeper::SAVE_MAX); }
bool load(klondike::Game& game) { return read(Kind::Klondike, game, klondike::SAVE_MAX); }
bool load(g2048::Game& game) { return read(Kind::Game2048, game, g2048::SAVE_MAX); }

uint32_t loadBest2048() {
  char text[16] = {};
  unsigned long best = 0;
  if (!Storage.exists(BEST_2048_PATH) || Storage.readFileToBuffer(BEST_2048_PATH, text, sizeof(text)) == 0 ||
      sscanf(text, "%lu", &best) != 1) {
    return 0;
  }
  return static_cast<uint32_t>(best);
}

void saveBest2048(const uint32_t score) {
  char text[16];
  snprintf(text, sizeof(text), "%lu", static_cast<unsigned long>(score));
  if (!Storage.ensureDirectoryExists(SAVE_DIR) || !Storage.writeFile(BEST_2048_PATH, String(text))) {
    LOG_ERR("GAMES", "Cannot save the 2048 record");
  }
}

}  // namespace gamesave
