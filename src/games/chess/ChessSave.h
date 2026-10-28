#pragma once

// SD persistence for the single unfinished chess game. The file exists only
// while a game is in progress, so its presence is the "game in progress" flag.

#include <cstdint>

namespace chess {
class Match;
}

namespace chesssave {

constexpr const char* SAVE_DIR = "/.crosspoint/games";
constexpr const char* SAVE_PATH = "/.crosspoint/games/chess.json";

bool hasGameInProgress();
// Writes an unfinished match (pause() it first so the clocks are charged);
// removes the save when the match is over.
bool save(const chess::Match& match);
// Restores the saved game, paused. False (and the unreadable file removed) on failure.
bool load(chess::Match& match, uint32_t seed);
void clear();

}  // namespace chesssave
