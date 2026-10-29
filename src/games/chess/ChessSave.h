#pragma once

// SD persistence for the single unfinished chess game. The file exists only
// while a game is in progress, so its presence is the "game in progress" flag.

#include <cstddef>
#include <cstdint>
#include <string>

namespace chess {
class Match;
}

namespace chesssave {

constexpr const char* SAVE_DIR = "/.crosspoint/games";
constexpr const char* SAVE_PATH = "/.crosspoint/games/chess.json";

enum class Read : uint8_t { Ok, Missing, Failed };

// Small files under SAVE_DIR, the prefs too: written to `tmpPath` and renamed
// over `path` once complete. A read first recovers a tmp left by a power cut
// inside the rename.
bool writeText(const char* path, const char* tmpPath, const char* text, size_t len);
Read readText(const char* path, const char* tmpPath, size_t cap, std::string& text);

bool hasGameInProgress();
// Writes an unfinished match (pause() it first so the clocks are charged);
// removes the save when the match is over.
bool save(const chess::Match& match);
// Restores the saved game, paused. False on failure; a file that reads but does
// not parse is removed. One that cannot be read is kept: until a load reads it,
// it is not saved over or removed, so a new game in its place plays unsaved.
bool load(chess::Match& match, uint32_t seed);
// False when a file stays.
bool clear();

}  // namespace chesssave
