#pragma once

// SD side of scanwords. Puzzles are read-only files from tools/scanword:
//   /scanwords/index.txt   puzzle list, see ScanwordPuzzle.h
//   /scanwords/<id>.scw    one puzzle
// Progress per puzzle and the totals live under /.crosspoint/games/scanword/.

#include <ScanwordPuzzle.h>

namespace scanword {
class Game;
struct Stats;
}  // namespace scanword

namespace scanwordstore {

constexpr const char* PUZZLE_DIR = "/scanwords";
constexpr const char* INDEX_PATH = "/scanwords/index.txt";
constexpr const char* SAVE_DIR = "/.crosspoint/games/scanword";

// Calls `onEntry` per puzzle until it returns false. False if the index is
// missing or malformed; entries already delivered stay valid.
bool readIndex(scanword::IndexParser::EntryFn onEntry, void* ctx);
// Streams /scanwords/<id>.scw into `puzzle`.
bool loadPuzzle(const char* id, scanword::Puzzle& puzzle);

bool hasProgress(const char* id);
// Crash-safe: writes a temp file and renames it over the save.
bool saveProgress(const char* id, const scanword::Game& game);
// False when there is no save; a save that reads but does not parse is removed.
bool loadProgress(const char* id, scanword::Game& game);
void clearProgress(const char* id);

bool loadStats(scanword::Stats& stats);
bool saveStats(const scanword::Stats& stats);

}  // namespace scanwordstore
