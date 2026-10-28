#pragma once

// Chess choices that outlive a game: the last "Новая партия" setup and the
// piece set. A missing or unreadable file gives the defaults.

#include <ChessMatch.h>

#include "PieceSets.h"

namespace chess::ui {

struct Prefs {
  MatchSettings match;
  bool rotateBoard = false;  // hot-seat: turn the board to the side to move
  PieceSet pieceSet = PieceSet::Tabler;
};

Prefs loadPrefs();
void savePrefs(const Prefs& prefs);

}  // namespace chess::ui
