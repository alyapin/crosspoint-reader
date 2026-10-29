#include "ChessPrefs.h"

#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "games/chess/ChessSave.h"

namespace chess::ui {
namespace {
constexpr const char* PREFS_PATH = "/.crosspoint/games/chess-prefs.json";
constexpr const char* PREFS_TMP = "/.crosspoint/games/chess-prefs.json.tmp";
constexpr size_t PREFS_MAX = 160;
// Written and read with the same format; anything else falls back to defaults.
constexpr const char* PREFS_FORMAT =
    "{\"mode\":%u,\"side\":%u,\"level\":%u,\"base\":%u,\"inc\":%u,\"rotate\":%u,\"pieces\":%u}";
}  // namespace

Prefs loadPrefs() {
  Prefs prefs;
  std::string text;
  if (chesssave::readText(PREFS_PATH, PREFS_TMP, PREFS_MAX, text) != chesssave::Read::Ok) return prefs;
  unsigned mode = 0, side = 0, level = 0, base = 0, inc = 0, rotate = 0, pieces = 0;
  if (sscanf(text.c_str(), PREFS_FORMAT, &mode, &side, &level, &base, &inc, &rotate, &pieces) != 7) {
    LOG_ERR("CHESS", "Ignoring unreadable prefs");
    return prefs;
  }
  prefs.match.mode = mode == 1 ? Mode::HotSeat : Mode::VsComputer;
  prefs.match.humanSide = side == 1 ? SideChoice::Black : side == 2 ? SideChoice::Random : SideChoice::White;
  prefs.match.level = static_cast<uint8_t>(std::clamp<unsigned>(level, MIN_LEVEL, MAX_LEVEL));
  prefs.match.time.baseMs = std::min<uint32_t>(base, MAX_BASE_MS);
  prefs.match.time.incrementMs = prefs.match.time.unlimited() ? 0 : std::min<uint32_t>(inc, MAX_INCREMENT_MS);
  prefs.rotateBoard = rotate != 0;
  if (pieces < PIECE_SET_COUNT) prefs.pieceSet = static_cast<PieceSet>(pieces);
  return prefs;
}

void savePrefs(const Prefs& prefs) {
  const auto& m = prefs.match;
  const unsigned side = m.humanSide == SideChoice::Black ? 1 : m.humanSide == SideChoice::Random ? 2 : 0;
  char text[PREFS_MAX];
  const int len = snprintf(text, sizeof(text), PREFS_FORMAT, m.mode == Mode::HotSeat ? 1u : 0u, side,
                           static_cast<unsigned>(m.level), static_cast<unsigned>(m.time.baseMs),
                           static_cast<unsigned>(m.time.incrementMs), prefs.rotateBoard ? 1u : 0u,
                           static_cast<unsigned>(prefs.pieceSet));
  if (len <= 0 || len >= static_cast<int>(sizeof(text)) ||
      !chesssave::writeText(PREFS_PATH, PREFS_TMP, text, static_cast<size_t>(len))) {
    LOG_ERR("CHESS", "Cannot save prefs");
  }
}

}  // namespace chess::ui
