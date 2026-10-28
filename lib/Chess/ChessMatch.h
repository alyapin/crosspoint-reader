#pragma once

// A playable session: game + clocks + computer opponent + save format.
// This is the surface a chess screen drives:
//   newGame() / deserialize()  set up (a restored game comes back paused)
//   play(), legalTargets(), needsPromotion()  human input
//   update() every loop pass  clocks, flag fall, engine time slices
//   undo(), resign(), agreeDraw(), pause()/resume()
//   serialize()  save text (JSON) for the SD layer
// The engine searches its own copy of the position, so only update() and the
// input calls mutate shared state; the screen serializes them with rendering.
// legalTargets()/needsPromotion() share a scratch move list: call them from the
// input side, not the render task.
//
// ~150 KB (~65 KB on boards without PSRAM, see Engine::TT_BITS), so allocate it
// once (it lands in PSRAM on boards that have it).

#include <cstddef>
#include <cstdint>

#include "ChessClock.h"
#include "ChessEngine.h"
#include "ChessGame.h"

namespace chess {

enum class Mode : uint8_t { VsComputer, HotSeat };
enum class SideChoice : uint8_t { White, Black, Random };

struct MatchSettings {
  Mode mode = Mode::VsComputer;
  SideChoice humanSide = SideChoice::White;
  uint8_t level = 3;  // 1..5
  TimeControl time = TIME_UNLIMITED;
};

// Upper bound of serialize() output, terminator included.
constexpr size_t SAVE_MAX = 256 + MAX_PLIES * 6;

class Match {
 public:
  using MillisFn = Engine::MillisFn;
  static constexpr uint32_t DEFAULT_SLICE_MS = 20;

  explicit Match(MillisFn millis) : now(millis) { newGame(MatchSettings{}, 1); }

  // `fen` = nullptr for the standard start; false (match unchanged) on a bad FEN.
  bool newGame(const MatchSettings& settings, uint32_t seed, const char* fen = nullptr);

  const Game& game() const { return record; }
  const MatchSettings& settings() const { return config; }
  Color humanColor() const { return human; }  // vs-computer only
  bool isHumanTurn() const;
  bool engineThinking() const { return engine.thinking(); }
  const SearchInfo& engineInfo() const { return engine.info(); }

  bool needsPromotion(Square from, Square to) const { return record.isPromotion(from, to); }
  uint64_t legalTargets(Square from) const;
  // Plays a human move; `promo` is needed only for promotions.
  bool play(Square from, Square to, PieceType promo = NO_PIECE);

  // Runs clocks and up to `sliceMs` of engine search. True when the position or result changed.
  bool update(uint32_t sliceMs = DEFAULT_SLICE_MS);

  // Vs computer this takes back to the human's previous turn (usually two plies).
  bool canUndo() const { return undoPlies() > 0; }
  bool undo();
  // Vs computer the human resigns; hot-seat the side to move does.
  void resign();
  void agreeDraw();

  void pause();
  void resume();
  bool paused() const { return isPaused; }
  // Display time; 0 for an unlimited game.
  uint32_t remainingMs(Color side) const { return clock.remainingMs(side, now()); }
  bool timed() const { return !config.time.unlimited(); }

  // Writes the save text into `out`; returns its length, or 0 if `cap` is too small.
  size_t serialize(char* out, size_t cap) const;
  // Restores a serialize()d match, paused. On failure starts a default game and returns false.
  bool deserialize(const char* text, size_t len, uint32_t seed);

 private:
  int undoPlies() const;
  bool checkFlag(uint32_t t);
  void afterMove(Color mover, uint32_t t);
  void startEngine(uint32_t t);
  void endGame(Outcome outcome, Termination termination);

  MillisFn now;
  MatchSettings config;
  Color human = WHITE;
  bool isPaused = false;
  Game record;
  Clock clock;
  Engine engine;
};

}  // namespace chess
