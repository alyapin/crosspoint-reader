#include "DraughtsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iterator>

#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"
#include "levelIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using draughts::Move;
using draughts::Status;

namespace {
constexpr int kSquare = 60;
constexpr int kBoardTop = GameScreen::BOARD_TOP;
constexpr int kBoardSize = 8 * kSquare;
constexpr int kPiece = 46;
constexpr int kOutline = 3;
constexpr int kInner = 28;
constexpr int kInnerRing = 2;
constexpr int kDotSize = 16;
constexpr int kCross = 7;  // half the × inside the inner ring
constexpr int kCrossStroke = 3;
constexpr int kCountDot = 14;
constexpr int kStatusGap = 20;  // the rule under the board to the heading
constexpr int kLineGap = 6;
constexpr int kFlipSlot = 1;
constexpr int kNewGameSlot = 2;
constexpr uint32_t kSliceMs = 40;
constexpr int kCenter = 17;  // d4: the key cursor starts on the piece nearest to it

bool has(const uint32_t set, const int square) { return (set >> square & 1u) != 0; }

// A crown 18 px wide: three points over a band, the middle one tallest.
void drawCrown(const GfxRenderer& renderer, const int cx, const int cy, const bool black) {
  constexpr int kKnots[][2] = {{0, 2}, {4, 7}, {9, 0}, {13, 7}, {17, 2}};
  constexpr int kBottom = 13;
  const int left = cx - 9;
  const int top = cy - 7;
  for (int k = 0; k + 1 < static_cast<int>(std::size(kKnots)); ++k) {
    const int x0 = kKnots[k][0];
    const int x1 = kKnots[k + 1][0];
    for (int x = x0; x < x1 || (x == x1 && k + 2 == static_cast<int>(std::size(kKnots))); ++x) {
      const int y = kKnots[k][1] + (kKnots[k + 1][1] - kKnots[k][1]) * (x - x0) / (x1 - x0);
      renderer.fillRect(left + x, top + y, 1, kBottom - y + 1, black);
    }
  }
}

const char* resultText(const draughts::Game& game) {
  const bool vs = game.vsComputer();
  switch (game.status()) {
    case Status::WhiteWon:
      return vs ? tr(STR_CHESS_YOU_WON) : tr(STR_CHESS_WHITE_WON);
    case Status::BlackWon:
      return vs ? tr(STR_CHESS_YOU_LOST) : tr(STR_CHESS_BLACK_WON);
    default:
      return tr(STR_CHESS_DRAW);
  }
}

const char* endText(const draughts::End end) {
  switch (end) {
    case draughts::End::NoMoves:
      return tr(STR_DRAUGHTS_NO_MOVES);
    case draughts::End::Resigned:
      return tr(STR_CHESS_RESIGNATION);
    case draughts::End::Repetition:
      return tr(STR_CHESS_THREEFOLD);
    case draughts::End::KingMoves:
      return tr(STR_DRAUGHTS_KING_MOVES);
    case draughts::End::Agreed:
      return tr(STR_CHESS_BY_AGREEMENT);
    default:
      return nullptr;
  }
}
}  // namespace

const char* draughtsListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Draughts) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openDraughts(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<DraughtsActivity>(renderer, mappedInput);
}

DraughtsActivity::DraughtsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Draughts", renderer, mappedInput) {}

bool DraughtsActivity::restore() {
  if (!gamesave::load(game) || !game.inProgress()) return false;
  reset();
  return true;
}

void DraughtsActivity::saveGame() { gamesave::save(game); }

void DraughtsActivity::clearSave() { gamesave::clear(gamesave::Kind::Draughts); }

void DraughtsActivity::resumeGame() {
  if (game.computerToMove() && !engine.thinking() && !engine.ready()) {
    engine.start(game.position(), game.mode(), esp_random(), &GameScreen::nowMs);
  }
}

void DraughtsActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_SUDOKU_EASY), &icon_level_1_24},
      {tr(STR_SUDOKU_MEDIUM), &icon_level_2_24},
      {tr(STR_SUDOKU_HARD), &icon_level_3_24},
      {tr(STR_TTT_TWO_PLAYERS), &icon_friend_24},
  };
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), static_cast<int>(game.mode()));
}

void DraughtsActivity::pickerChosen(const int row, bool) {
  game.newGame(static_cast<draughts::Mode>(std::clamp(row, 0, draughts::MODE_COUNT - 1)), esp_random());
  reset();
}

int DraughtsActivity::menuRows(GamePickerActivity::Row* out) const {
  out[0] = {tr(STR_CHESS_OFFER_DRAW), &icon_draw_24};
  out[1] = {tr(STR_CHESS_RESIGN), &icon_surrender_24};
  return 2;
}

void DraughtsActivity::menuChosen(const int row) {
  drawDeclined = row == 0 && !game.offerDraw();
  if (row == 1) game.resign();
  afterChange();
}

// --- loop task ---------------------------------------------------------------

void DraughtsActivity::loop() {
  if (engine.thinking()) engine.step(kSliceMs);
  if (engine.ready()) {
    RenderLock lock(RenderLock::Mode::Try);
    if (lock.ownsLock()) {
      const Move move = engine.bestMove();
      engine.cancel();
      if (game.computerToMove()) play(move);
    }
  }
  GameScreen::loop();
}

void DraughtsActivity::reset() {
  flipped = false;
  drawDeclined = false;
  barFocus = -1;
  cursor = mappedInput.hasTouch() ? -1 : kCenter;
  refresh();
}

// After any change of the game: its moves, a fresh pick, the computer's turn.
void DraughtsActivity::refresh() {
  moveCount = game.computerToMove() ? 0 : game.moves(moves, draughts::MAX_MOVES);
  selected = soleMover() ? moves[0].from : -1;
  chainLen = 0;
  if (cursor >= 0) placeCursor();
  engine.cancel();
  resumeGame();
}

void DraughtsActivity::afterChange() {
  refresh();
  if (game.isOver() && !mappedInput.hasTouch()) barFocus = kNewGameSlot;
  markChanged();
}

void DraughtsActivity::play(const Move& move) {
  if (!game.play(move)) return;
  drawDeclined = false;
  afterChange();
}

bool DraughtsActivity::onPath(const Move& move, const int length) const {
  if (move.from != selected || move.stops() < length) return false;
  for (int k = 0; k < length; ++k) {
    if (move.stopAt(k) != chain[k]) return false;
  }
  return true;
}

bool DraughtsActivity::soleMover() const {
  return moveCount > 0 && std::all_of(moves, moves + moveCount, [&](const Move& m) { return m.from == moves[0].from; });
}

bool DraughtsActivity::isMover(const int square) const {
  return std::any_of(moves, moves + moveCount, [&](const Move& m) { return m.from == square; });
}

void DraughtsActivity::select(const int square) {
  selected = square;
  chainLen = 0;
  if (cursor >= 0) placeCursor();
}

// A tap on a dot: the path grows to it and a finished path is played. A dot
// reached two ways is taken but waits for the dot before it.
bool DraughtsActivity::stepTo(const int square) {
  const auto indexIn = [&](const Move& m) {
    if (!onPath(m, chainLen)) return -1;
    for (int k = chainLen; k < m.stops(); ++k) {
      if (m.stopAt(k) == square) return k;
    }
    return -1;
  };
  const Move* first = nullptr;
  int at = draughts::MAX_JUMPS;
  for (int i = 0; i < moveCount; ++i) {
    const int k = indexIn(moves[i]);
    if (k >= 0 && k < at) {
      at = k;
      first = &moves[i];
    }
  }
  if (first == nullptr) return false;
  for (int i = 0; i < moveCount; ++i) {
    if (indexIn(moves[i]) != at) continue;
    for (int j = chainLen; j < at; ++j) {
      if (moves[i].stopAt(j) != first->stopAt(j)) return true;
    }
  }
  for (int j = chainLen; j <= at; ++j) chain[j] = static_cast<uint8_t>(first->stopAt(j));
  chainLen = at + 1;
  for (int i = 0; i < moveCount; ++i) {
    if (moves[i].stops() == chainLen && onPath(moves[i], chainLen)) {
      play(moves[i]);
      return true;
    }
  }
  if (cursor >= 0) placeCursor();
  requestRepaint(RepaintUpdate);
  return true;
}

// Key stops in screen order: the pieces that can move, or the next landings of the one taken.
int DraughtsActivity::boardItems(int* out) const {
  uint32_t set = 0;
  for (int i = 0; i < moveCount; ++i) {
    const Move& m = moves[i];
    if (selected < 0) {
      set |= 1u << m.from;
    } else if (onPath(m, chainLen) && m.stops() > chainLen) {
      set |= 1u << m.stopAt(chainLen);
    }
  }
  int count = 0;
  for (; set != 0; set &= set - 1) out[count++] = __builtin_ctz(set);
  std::sort(out, out + count, [&](const int a, const int b) { return visualIndex(a) < visualIndex(b); });
  return count;
}

// The key cursor on the item nearest to where it was.
void DraughtsActivity::placeCursor() {
  int items[draughts::SQUARES];
  const int count = boardItems(items);
  if (count == 0) return;
  const int from = cursor >= 0 ? visualIndex(cursor) : 0;
  int best = items[0];
  for (int i = 1; i < count; ++i) {
    const int v = visualIndex(items[i]);
    const int b = visualIndex(best);
    if (std::abs(v / 8 - from / 8) + std::abs(v % 8 - from % 8) <
        std::abs(b / 8 - from / 8) + std::abs(b % 8 - from % 8)) {
      best = items[i];
    }
  }
  cursor = best;
}

void DraughtsActivity::runBar(const int slot) {
  if (slot == 0) {
    if (game.undo()) afterChange();
  } else if (slot == kFlipSlot) {
    flipped = !flipped;
    requestRepaint(RepaintUpdate);
  } else if (slot == kNewGameSlot) {
    if (game.isOver()) {
      openNewGame();
    } else {
      openGameMenu();
    }
  }
}

bool DraughtsActivity::barShown(const int slot) const {
  if (slot == 0) return game.canUndo();
  if (slot == kFlipSlot) return !game.vsComputer();
  return slot == kNewGameSlot;
}

int DraughtsActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void DraughtsActivity::onTouch(const fui::ActionEvent& event, const int x, const int y) {
  if (event.action == ACTION_BAR) {
    if (barShown(event.value)) runBar(event.value);
    return;
  }
  if (event.action != ACTION_BOARD || moveCount == 0) return;
  const int square = squareAtPoint(x, y);
  if (square < 0) return;
  if (selected >= 0 && stepTo(square)) return;
  if (isMover(square) && square != selected) {
    select(square);
  } else if (soleMover()) {
    chainLen = 0;
  } else {
    selected = -1;
    chainLen = 0;
  }
  requestRepaint(RepaintUpdate);
}

void DraughtsActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before[] = {cursor, barFocus, selected, chainLen, flipped};
  int items[draughts::SQUARES];
  const int count = boardItems(items);
  const int at = static_cast<int>(std::find(items, items + count, cursor) - items);
  const int firstSlot = nextBarSlot(BAR_SLOTS - 1, 1);
  const int lastSlot = nextBarSlot(0, -1);
  if (barFocus >= 0) {
    if (confirm) {
      runBar(barFocus);
      if (barFocus >= 0 && !barShown(barFocus)) barFocus = nextBarSlot(barFocus, 1);
    } else if (dx != 0 && count > 0 && barFocus == (dx > 0 ? lastSlot : firstSlot)) {
      barFocus = -1;
      cursor = items[dx > 0 ? 0 : count - 1];
    } else if (dx != 0) {
      barFocus = nextBarSlot(barFocus, dx);
    } else if (dy < 0 && count > 0) {
      barFocus = -1;
      if (at == count) cursor = items[0];
    }
  } else if (count == 0) {
    barFocus = firstSlot;
  } else if (at == count) {
    cursor = items[0];
  } else if (confirm) {
    if (selected < 0) {
      select(cursor);
    } else {
      stepTo(cursor);
    }
  } else if (dx != 0) {
    const int next = at + dx;
    if (next < 0 || next >= count) {
      barFocus = dx > 0 ? firstSlot : lastSlot;
    } else {
      cursor = items[next];
    }
  } else if (dy > 0) {
    barFocus = firstSlot;
  }
  const int after[] = {cursor, barFocus, selected, chainLen, flipped};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

// Back drops the bar focus, then the piece taken (or the path entered so far).
bool DraughtsActivity::onBack() {
  if (barFocus >= 0) {
    barFocus = -1;
    if (cursor < 0) cursor = kCenter;
    placeCursor();
  } else if (selected >= 0 && (chainLen > 0 || !soleMover())) {
    const int piece = selected;
    chainLen = 0;
    if (!soleMover()) selected = -1;
    if (cursor >= 0) cursor = piece;
    if (cursor >= 0) placeCursor();
  } else {
    return false;
  }
  requestRepaint(RepaintUpdate);
  return true;
}

// --- geometry ----------------------------------------------------------------

int DraughtsActivity::visualIndex(const int square) const {
  const int index = draughts::rowOf(square) * 8 + draughts::colOf(square);
  return flipped ? 63 - index : index;
}

int DraughtsActivity::squareAtPoint(const int x, const int y) const {
  const int left = (renderer.getScreenWidth() - kBoardSize) / 2;
  if (x < left || x >= left + kBoardSize || y < kBoardTop || y >= kBoardTop + kBoardSize) return -1;
  int row = (y - kBoardTop) / kSquare;
  int col = (x - left) / kSquare;
  if (flipped) {
    row = 7 - row;
    col = 7 - col;
  }
  return draughts::squareAt(row, col);
}

void DraughtsActivity::cellOrigin(const int square, int& x, int& y) const {
  const int index = visualIndex(square);
  x = (renderer.getScreenWidth() - kBoardSize) / 2 + index % 8 * kSquare;
  y = kBoardTop + index / 8 * kSquare;
}

// --- render task -------------------------------------------------------------

void DraughtsActivity::build(UiScreen& screen) {
  char mode[32];
  const char* value = tr(STR_TTT_TWO_PLAYERS);
  if (game.vsComputer()) {
    snprintf(mode, sizeof(mode), tr(STR_CHESS_COMPUTER_LEVEL), static_cast<int>(game.mode()) + 1);
    value = mode;
  }
  drawTopLine(screen, nullptr, value, false);
  drawBoard();
  screen.frame().hit(fui::makeRect((renderer.getScreenWidth() - kBoardSize) / 2, kBoardTop, kBoardSize, kBoardSize),
                     ACTION_BOARD, 0, shellInput());
  drawStatus();

  const bool over = game.isOver();
  BarItem items[BAR_SLOTS];
  if (barShown(0)) items[0] = {&icon_bar_undo_24, tr(STR_GAMES_UNDO)};
  if (barShown(kFlipSlot)) items[kFlipSlot] = {&icon_flip_24, tr(STR_CHESS_FLIP_SHORT)};
  items[kNewGameSlot] = {over ? &icon_bar_new_game_24 : &icon_menu_24,
                         over ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU)};
  drawBar(screen, items, barFocus);
}

void DraughtsActivity::drawBoard() const {
  const int left = (renderer.getScreenWidth() - kBoardSize) / 2;
  for (int i = 0; i < 64; ++i) {
    if ((i / 8 + i % 8) % 2 == 1) {
      renderer.fillRectDither(left + i % 8 * kSquare, kBoardTop + i / 8 * kSquare, kSquare, kSquare, Color::LightGray);
    }
  }

  uint32_t dots = 0;
  uint32_t marked = 0;
  if (selected >= 0) {
    marked = ~0u;
    for (int i = 0; i < moveCount; ++i) {
      const Move& m = moves[i];
      if (!onPath(m, chainLen) || m.stops() <= chainLen) continue;
      for (int k = chainLen; k < m.stops(); ++k) dots |= 1u << m.stopAt(k);
      marked &= m.captured;
    }
    if (dots == 0) marked = 0;
  }

  // Along a capture being entered the piece stands on its last dot, crowned once it passed the far row.
  const draughts::Position& pos = game.position();
  const int at = chainLen > 0 ? chain[chainLen - 1] : selected;
  bool crowned = selected >= 0 && has(pos.kings, selected);
  for (int k = 0; k < chainLen; ++k) {
    crowned = crowned || draughts::rowOf(chain[k]) == (pos.side == draughts::White ? 0 : 7);
  }
  int x = 0;
  int y = 0;
  for (int s = 0; s < draughts::SQUARES; ++s) {
    const bool white = has(pos.white, s);
    if ((!white && !has(pos.black, s)) || (s == selected && at != selected)) continue;
    cellOrigin(s, x, y);
    drawPiece(x, y, white, has(pos.kings, s), has(marked, s));
  }
  if (at != selected) {
    cellOrigin(at, x, y);
    drawPiece(x, y, pos.side == draughts::White, crowned, false);
  }
  draughts::Step last;
  if (game.vsComputer() && pos.side == draughts::White && game.lastMove(last)) {
    for (const int s : {static_cast<int>(last.from), static_cast<int>(last.to)}) {
      cellOrigin(s, x, y);
      drawDashedFrame(renderer, x, y, kSquare);
    }
  }
  if (selected >= 0) {
    cellOrigin(at, x, y);
    drawCellFrame(renderer, x, y, kSquare, kSquare);
  }
  for (int s = 0; s < draughts::SQUARES; ++s) {
    if (!has(dots, s)) continue;
    cellOrigin(s, x, y);
    renderer.fillRoundedRect(x + (kSquare - kDotSize) / 2, y + (kSquare - kDotSize) / 2, kDotSize, kDotSize,
                             kDotSize / 2, Color::Black);
  }
  if (cursor >= 0 && barFocus < 0 && moveCount > 0) {
    cellOrigin(cursor, x, y);
    drawCursor(x, y, kSquare, kSquare);
  }
}

// White: black outline, black inner ring. Black: solid, white inner ring.
// A king carries a crown, a piece about to be taken a ×.
void DraughtsActivity::drawPiece(const int x, const int y, const bool white, const bool king, const bool marked) const {
  const int px = x + (kSquare - kPiece) / 2;
  const int py = y + (kSquare - kPiece) / 2;
  renderer.fillRoundedRect(px, py, kPiece, kPiece, kPiece / 2, Color::Black);
  if (white) {
    renderer.fillRoundedRect(px + kOutline, py + kOutline, kPiece - 2 * kOutline, kPiece - 2 * kOutline,
                             kPiece / 2 - kOutline, Color::White);
  }
  const int cx = x + kSquare / 2;
  const int cy = y + kSquare / 2;
  renderer.drawRoundedRect(cx - kInner / 2, cy - kInner / 2, kInner, kInner, kInnerRing, kInner / 2, white);
  if (marked) {
    renderer.drawLine(cx - kCross, cy - kCross, cx + kCross, cy + kCross, kCrossStroke, white);
    renderer.drawLine(cx - kCross, cy + kCross, cx + kCross, cy - kCross, kCrossStroke, white);
  } else if (king) {
    drawCrown(renderer, cx, cy, white);
  }
}

void DraughtsActivity::drawStatus() const {
  const int width = renderer.getScreenWidth();
  int y = kBoardTop + kBoardSize;
  drawRule(renderer, 0, y, width);
  y += kStatusGap;

  const char* heading = nullptr;
  const char* lines[3] = {};
  int n = 0;
  if (drawDeclined) lines[n++] = tr(STR_CHESS_DRAW_DECLINED);
  if (game.isOver()) {
    heading = resultText(game);
    lines[n++] = endText(game.end());
  } else if (game.computerToMove()) {
    heading = tr(STR_CHESS_THINKING);
  } else {
    const bool capture = moveCount > 0 && moves[0].captured != 0;
    const bool chain = selected >= 0 && std::any_of(moves, moves + moveCount,
                                                    [&](const Move& m) { return onPath(m, chainLen) && m.jumps >= 2; });
    if (game.vsComputer()) {
      heading = capture ? tr(STR_DRAUGHTS_MUST_CAPTURE) : tr(STR_CHESS_YOUR_MOVE);
      if (chain) lines[n++] = tr(STR_DRAUGHTS_CHAIN);
      if (capture) lines[n++] = tr(STR_DRAUGHTS_ONLY_CAPTURES);
    } else {
      heading = game.position().side == draughts::White ? tr(STR_CHESS_WHITE_TO_MOVE) : tr(STR_CHESS_BLACK_TO_MOVE);
      lines[n++] = capture ? tr(STR_DRAUGHTS_MUST_CAPTURE_LINE) : tr(STR_DRAUGHTS_NO_AUTO_FLIP);
      if (capture) lines[n++] = chain ? tr(STR_DRAUGHTS_CHAIN) : tr(STR_DRAUGHTS_ONLY_CAPTURES);
    }
  }
  renderer.drawText(kText12Font, kSidePadding, y, heading, true, kSemiBold);

  // ● black  ○ white, right-aligned on the heading line.
  const draughts::Position& pos = game.position();
  char counts[2][4];
  snprintf(counts[0], sizeof(counts[0]), "%d", __builtin_popcount(pos.black));
  snprintf(counts[1], sizeof(counts[1]), "%d", __builtin_popcount(pos.white));
  constexpr int kDotGap = 8;
  constexpr int kPairGap = 20;
  const int lineHeight = renderer.getLineHeight(kText12Font);
  int x = width - kSidePadding - 2 * (kCountDot + kDotGap) - kPairGap;
  for (const char* count : counts) x -= renderer.getTextWidth(kText12Font, count, kSemiBold);
  const int dotTop = y + (lineHeight - kCountDot) / 2;
  for (int i = 0; i < 2; ++i) {
    renderer.fillRoundedRect(x, dotTop, kCountDot, kCountDot, kCountDot / 2, Color::Black);
    if (i == 1) {
      renderer.fillRoundedRect(x + 2, dotTop + 2, kCountDot - 4, kCountDot - 4, kCountDot / 2 - 2, Color::White);
    }
    x += kCountDot + kDotGap;
    renderer.drawText(kText12Font, x, y, counts[i], true, kSemiBold);
    x += renderer.getTextWidth(kText12Font, counts[i], kSemiBold) + kPairGap;
  }

  y += lineHeight + kLineGap;
  const int maxY = barTop() - renderer.getLineHeight(kText10Font);
  for (int i = 0; i < n && y <= maxY; ++i) {
    if (lines[i] == nullptr) continue;
    renderer.drawText(kText10Font, kSidePadding, y, lines[i]);
    y += renderer.getLineHeight(kText10Font) + 2;
  }
}
