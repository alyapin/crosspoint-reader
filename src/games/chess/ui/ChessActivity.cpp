#include "ChessActivity.h"

#include <ChessEngine.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <new>

#include "ClockText.h"
#include "PieceSets.h"
#include "activities/util/ConfirmationActivity.h"
#include "chessIcons.h"
#include "components/HeaderBackTapTarget.h"
#include "components/UITheme.h"
#include "games/chess/ChessSave.h"
#include "games/ui/ActionBar.h"
#include "games/ui/gameIcons.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
constexpr fui::ActionId ACTION_BACK = 1;
constexpr fui::ActionId ACTION_ROW = 2;
constexpr fui::ActionId ACTION_STEP = 3;
constexpr fui::ActionId ACTION_BOARD = 4;
constexpr fui::ActionId ACTION_UNDO = 5;
constexpr fui::ActionId ACTION_MENU = 6;
constexpr fui::ActionId ACTION_PROMOTE = 7;
constexpr fui::ActionId ACTION_NEW_GAME = 8;
constexpr fui::ActionId ACTION_FLIP = 9;

// List screens: the CrossPoint 2.0 header, then the rows of the other game menus (setka::kList*).
constexpr int kHeaderTop = kGameTopLineY;  // game view top line, under the status line
constexpr int kExitStatusGap = 40;         // the least room between "‹ Выйти" and the status
constexpr int kRowHeight = setka::kListPitch;
constexpr int kRowGap = 0;
constexpr int kIconSize = 24;
constexpr int kStepperSize = 56;
constexpr int kCustomDoneRow = 2;

// Game screen: top line, full-bleed 8x8 board, panel, actions.
constexpr int kSquare = 60;
constexpr int kBoardTop = kGameBoardTop;
constexpr int kBoardSize = 8 * kSquare;
constexpr int kPanelTop = kBoardTop + kBoardSize;
constexpr int kPieceInset = (kSquare - 56) / 2;
constexpr int kDotSize = 16;
constexpr int kRingWidth = 3;
constexpr int kTurnLineGap = 16;
constexpr int kUnitGap = 12;  // hero minutes and their "мин"
constexpr int kPickArm = 14;  // a picked piece: corner brackets on its square
constexpr int kPickInCursor = kCellFrameWidth + 2;
// Key focus ring in the game view: the 64 board cells (visual order), then the actions.
constexpr int kCells = 64;
constexpr int kStartCursor = 6 * 8 + 4;  // the near side's e-pawn
#if SOVA_UNBOUNDED_UI
constexpr int kHeroCapTarget = 0;  // the Unbounded hero face at its own size
#else
constexpr int kHeroCapTarget = 64;  // no hero face here: the UI digits are scaled up
#endif

constexpr uint8_t kBaseMinutes[] = {1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 90, 120, 180};
constexpr uint8_t kIncrementSeconds[] = {0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60};
constexpr uint32_t kDefaultCustomMinutes = 25;
constexpr uint32_t kDefaultCustomSeconds = 10;
constexpr chess::TimeControl kTimePresets[] = {chess::TIME_UNLIMITED, chess::TIME_5_3, chess::TIME_10_5,
                                               chess::TIME_15_10};
constexpr int kCustomTimeOption = static_cast<int>(std::size(kTimePresets));
constexpr const char* kTimePresetLabels[] = {nullptr, "5+3", "10+5", "15+10"};
constexpr const char* kLevelLabels[] = {"1", "2", "3", "4", "5"};
constexpr StrId kPieceSetNames[chess::ui::PIECE_SET_COUNT] = {
    StrId::STR_CHESS_PIECES_OUTLINE, StrId::STR_CHESS_PIECES_CLASSIC, StrId::STR_CHESS_PIECES_THIN,
    StrId::STR_CHESS_PIECES_BOLD};
constexpr chess::PieceType kPromotions[] = {chess::QUEEN, chess::ROOK, chess::BISHOP, chess::KNIGHT};
constexpr chess::ui::PieceKind kPreviewKinds[] = {chess::ui::PieceKind::King, chess::ui::PieceKind::Queen,
                                                  chess::ui::PieceKind::Knight};

// The computer accepts a draw when its static evaluation is not in its favour.
constexpr int kDrawAcceptCp = 0;

uint32_t nowMs() { return static_cast<uint32_t>(millis()); }

static_assert(static_cast<int>(chess::ui::PieceKind::Pawn) == chess::PAWN);
static_assert(static_cast<int>(chess::ui::PieceKind::Knight) == chess::KNIGHT);
static_assert(static_cast<int>(chess::ui::PieceKind::Bishop) == chess::BISHOP);
static_assert(static_cast<int>(chess::ui::PieceKind::Rook) == chess::ROOK);
static_assert(static_cast<int>(chess::ui::PieceKind::Queen) == chess::QUEEN);
static_assert(static_cast<int>(chess::ui::PieceKind::King) == chess::KING);

chess::ui::PieceKind pieceKind(const chess::PieceType type) { return static_cast<chess::ui::PieceKind>(type); }

chess::ui::PieceColor pieceColor(const chess::Color color) {
  return color == chess::WHITE ? chess::ui::PieceColor::White : chess::ui::PieceColor::Black;
}

void formatTime(const chess::TimeControl& time, char* out, const size_t cap) {
  snprintf(out, cap, "%u+%u", static_cast<unsigned>(time.baseMs / 60000),
           static_cast<unsigned>(time.incrementMs / 1000));
}

int timePreset(const chess::TimeControl& time) {
  for (int i = 0; i < kCustomTimeOption; ++i) {
    if (kTimePresets[i].baseMs == time.baseMs && kTimePresets[i].incrementMs == time.incrementMs) return i;
  }
  return kCustomTimeOption;
}

// Index of the first step >= value (the last one past the end).
template <size_t N>
uint8_t stepIndex(const uint8_t (&steps)[N], const uint32_t value) {
  for (size_t i = 0; i < N; ++i) {
    if (steps[i] >= value) return static_cast<uint8_t>(i);
  }
  return static_cast<uint8_t>(N - 1);
}

void drawRightAligned(const GfxRenderer& renderer, const int font, const int right, const int y, const char* text,
                      const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(font, right - renderer.getTextWidth(font, text, style), y, text, true, style);
}

void drawCentered(const GfxRenderer& renderer, const int font, const int x, const int width, const int y,
                  const char* text, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  renderer.drawText(font, x + (width - renderer.getTextWidth(font, text, style)) / 2, y, text, true, style);
}

// Truncates with an ellipsis only when the text does not fit, so the usual
// case allocates nothing.
void drawFitted(const GfxRenderer& renderer, const int font, const int x, const int y, const char* text,
                const int maxWidth, const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (maxWidth <= 0) return;
  if (renderer.getTextWidth(font, text, style) <= maxWidth) {
    renderer.drawText(font, x, y, text, true, style);
    return;
  }
  renderer.drawText(font, x, y, renderer.truncatedText(font, text, maxWidth, style).c_str(), true, style);
}

// Russian piece letters for the Russian UI, English SAN otherwise.
const chess::Notation& notation() {
  return I18N.getLanguage() == Language::RU ? chess::NOTATION_RU : chess::NOTATION_EN;
}

// The key cursor is the 4 px inset frame; a picked piece takes corner brackets, inside the frame under the cursor.
void drawPick(const GfxRenderer& renderer, const int x, const int y, const bool underCursor) {
  constexpr int kWidth = kCellFrameWidth;
  const int inset = underCursor ? kPickInCursor : 0;
  const int size = kSquare - 2 * inset;
  const int left = x + inset;
  const int top = y + inset;
  for (const int cx : {left, left + size - kPickArm}) {
    for (const int cy : {top, top + size - kWidth}) renderer.fillRect(cx, cy, kPickArm, kWidth, true);
  }
  for (const int cx : {left, left + size - kWidth}) {
    for (const int cy : {top, top + size - kPickArm}) renderer.fillRect(cx, cy, kWidth, kPickArm, true);
  }
}

const char* overTitle(const chess::Game& game) {
  switch (game.termination()) {
    case chess::Termination::Checkmate:
      return tr(STR_CHESS_CHECKMATE);
    case chess::Termination::Stalemate:
      return tr(STR_CHESS_STALEMATE);
    case chess::Termination::Timeout:
    case chess::Termination::TimeoutVsInsufficientMaterial:
      return tr(STR_CHESS_TIMEOUT);
    case chess::Termination::Resignation:
      return tr(STR_CHESS_RESIGNATION);
    default:
      return tr(STR_CHESS_DRAW);
  }
}

const char* overSubtitle(const chess::Match& match) {
  const chess::Game& game = match.game();
  if (game.outcome() == chess::Outcome::Draw) {
    switch (game.termination()) {
      case chess::Termination::Threefold:
        return tr(STR_CHESS_THREEFOLD);
      case chess::Termination::FiftyMoves:
        return tr(STR_CHESS_FIFTY_MOVES);
      case chess::Termination::InsufficientMaterial:
        return tr(STR_CHESS_NO_MATERIAL);
      case chess::Termination::MoveLimit:
        return tr(STR_CHESS_MOVE_LIMIT);
      case chess::Termination::Agreement:
        return tr(STR_CHESS_BY_AGREEMENT);
      default:
        return tr(STR_CHESS_DRAW);
    }
  }
  const chess::Color winner = game.outcome() == chess::Outcome::WhiteWins ? chess::WHITE : chess::BLACK;
  if (match.settings().mode == chess::Mode::VsComputer) {
    return winner == match.humanColor() ? tr(STR_CHESS_YOU_WON) : tr(STR_CHESS_YOU_LOST);
  }
  return winner == chess::WHITE ? tr(STR_CHESS_WHITE_WON) : tr(STR_CHESS_BLACK_WON);
}
}  // namespace

const char* chessListStatus() { return chesssave::hasGameInProgress() ? tr(STR_CHESS_IN_PROGRESS) : nullptr; }

std::unique_ptr<Activity> openChess(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return std::make_unique<ChessActivity>(renderer, mappedInput);
}

ChessActivity::ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Chess", renderer, mappedInput), UiAppHost(renderer) {}

void ChessActivity::onEnter() {
  // The render task may run as soon as ActivityManager drops its lock; keep it
  // out until the state below is in place (same as LibraryListActivity).
  RenderLock lock(*this);
  Activity::onEnter();
  resetUi();
  app.setScreen(&ChessActivity::screenFn, this);
  match.reset(new (std::nothrow) chess::Match(&nowMs));
  if (!match) {
    LOG_ERR("CHESS", "OOM: %u bytes for the match", static_cast<unsigned>(sizeof(chess::Match)));
    finish();
    return;
  }
  prefs = chess::ui::loadPrefs();
  if (chesssave::load(*match, esp_random())) {
    overGuard.reset(match->game().isOver());
    inGame = true;
    setView(View::Game);
  } else {
    setView(View::Setup);
  }
}

void ChessActivity::onExit() {
  saveGame();
  match.reset();
  Activity::onExit();
}

void ChessActivity::prepareForSleep() { saveGame(); }

bool ChessActivity::skipLoopDelay() { return view == View::Game && match && match->engineThinking(); }

// tickClocks() repaints a clock whose text changed and update() flags it at 0.
uint32_t ChessActivity::idleSleepBudgetMs() const {
  if (view != View::Game || !match || !match->timed() || match->paused() || match->game().isOver()) {
    return light_sleep::kNoDeadline;
  }
  return chess::ui::msUntilClockTextChange(match->remainingMs(match->game().sideToMove()));
}

void ChessActivity::saveGame() {
  if (!match || !inGame) return;
  match->pause();
  chesssave::save(*match);
}

void ChessActivity::requestRepaint(const Repaint kind) {
  uint8_t current = repaint.load();
  while (current < kind && !repaint.compare_exchange_weak(current, kind)) {
  }
  requestUpdate();
}

// --- loop task ---------------------------------------------------------------

void ChessActivity::loop() {
  if (!match) return;
  overGuard.track(match->game().isOver(), nowMs());
  if (view == View::Game) {
    {
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock() && match->update()) afterPly();
    }
    tickClocks();
  }

  if (view == View::Game) keys.read(mappedInput, gpio.hasEdgeSideButtons(), false);
  if (const auto touch = routeTouch(mappedInput)) {
    RenderLock lock(*this);
    // Keys pressed before the tap act first; one that may have opened another view drops the tap.
    if (!applyGameKeys()) handleAction(touch.event, touch.snap.touchX, touch.snap.touchY);
  } else if (view == View::Game) {
    if (keys.pending()) {
      // A panel refresh holds the lock: the keys wait in the queue, in order, and none is lost.
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock()) applyGameKeys();
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    RenderLock lock(*this);
    goBack();
  } else {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      RenderLock lock(*this);
      activateRow(focus);
    }
    const int count = rowCount();
    buttonNavigator.onNextPress([this, count] {
      RenderLock lock(*this);
      focus = ButtonNavigator::nextIndex(focus, count);
      requestRepaint(RepaintUpdate);
    });
    buttonNavigator.onPreviousPress([this, count] {
      RenderLock lock(*this);
      focus = focus < 0 ? count - 1 : ButtonNavigator::previousIndex(focus, count);
      requestRepaint(RepaintUpdate);
    });
  }

  if (savePending) {
    savePending = false;
    if (inGame) chesssave::save(*match);
  }
}

int ChessActivity::gameActions(GameAction* out) const {
  int n = 0;
  if (!hasBar()) return n;
  if (match->canUndo()) out[n++] = GameAction::Undo;
  out[n++] = match->game().isOver() ? GameAction::NewGame : GameAction::Flip;
  out[n++] = GameAction::Menu;
  return n;
}

ChessActivity::GameAction ChessActivity::focusedAction() const {
  if (cursor < kCells) return GameAction::None;
  GameAction actions[kMaxActions];
  const int count = gameActions(actions);
  return actions[std::min(cursor - kCells, count - 1)];
}

void ChessActivity::runAction(const GameAction action) {
  switch (action) {
    case GameAction::NewGame:
      // Not straight after the move that ended the game: a doubled press keeps the result on screen.
      if (!overGuard.holds(match->game().isOver(), nowMs())) setView(View::Setup);
      break;
    case GameAction::Undo:
      if (match->undo()) afterPly();
      break;
    case GameAction::Flip:
      manualFlip = !manualFlip;
      requestRepaint(RepaintUpdate);
      break;
    case GameAction::Menu:
      setView(View::Menu);
      break;
    case GameAction::None:
      break;
  }
}

// In order; true after a Confirm or Back, which may change the view (the board's queue is dropped on return).
bool ChessActivity::applyGameKeys() {
  return keys.drain([this](const games::BoardKey& key) {
    if (view != View::Game) return;
    if (key.action == games::BoardKey::Back) {
      goBack();
    } else {
      gameKey(key.dx, key.dy, key.action == games::BoardKey::Confirm);
    }
  });
}

// Keys only (X4 without touch, or by choice): a cursor walks the 64 squares and
// then the bar's actions, if shown; Confirm acts like a tap. Left/Right step along the ring,
// Up/Down change rank (on edge-button boards the side buttons step instead).
void ChessActivity::gameKey(const int dx, const int dy, const bool confirm) {
  if (promoFrom != kNoSquare) {
    if (confirm) {
      playMove(promoFrom, promoTo, kPromotions[promoFocus]);
      return;
    }
    const int count = static_cast<int>(std::size(kPromotions));
    promoFocus = (promoFocus + dx + dy + count) % count;
    requestRepaint(RepaintUpdate);
    return;
  }
  if (!hasBar() && match->game().isOver()) {
    if (confirm) runAction(GameAction::NewGame);
    return;
  }
  GameAction actions[kMaxActions];
  const int ring = kCells + gameActions(actions);
  if (cursor < 0) {
    // The first key shows the cursor; on a finished game it lands on Новая игра, before Меню.
    cursor = match->game().isOver() ? ring - 2 : kStartCursor;
    requestRepaint(RepaintUpdate);
    return;
  }
  cursor = std::min(cursor, ring - 1);
  if (confirm) {
    if (cursor < kCells) {
      tapSquare(squareAtCell(cursor));
    } else {
      runAction(actions[cursor - kCells]);
    }
    return;
  }
  if (dx != 0) cursor = (cursor + dx + ring) % ring;
  if (dy != 0 && cursor < kCells) {
    const int row = cursor / 8 + dy;
    if (row > 7) {
      if (ring > kCells) cursor = kCells;
    } else if (row >= 0) {
      cursor = row * 8 + cursor % 8;
    }
  } else if (dy < 0) {
    cursor = kStartCursor + 8;  // from the actions back to the near rank
  }
  requestRepaint(RepaintUpdate);
}

void ChessActivity::tickClocks() {
  // The computer's reply is a few seconds at most: its clock catches up when it moves.
  if (!match->timed() || match->paused() || match->game().isOver() || match->engineThinking()) return;
  // Clocks show whole minutes, then 10 s steps (ClockText): repaint only when that changes.
  bool changed = false;
  for (const chess::Color side : {chess::WHITE, chess::BLACK}) {
    const uint32_t shown = chess::ui::clockText(match->remainingMs(side)).shownSeconds;
    if (shown != shownSeconds[side]) {
      shownSeconds[side] = shown;
      changed = true;
    }
  }
  if (changed) requestRepaint(RepaintClock);
}

void ChessActivity::handleAction(const fui::ActionEvent& event, const int x, const int y) {
  switch (event.action) {
    case ACTION_BACK:
      goBack();
      break;
    case ACTION_ROW:
      focus = event.value;
      activateRow(event.value);
      break;
    case ACTION_STEP:
      stepCustom(event.value / 2, event.value % 2 != 0 ? 1 : -1);
      break;
    case ACTION_BOARD:
      if (const chess::Square square = squareAt(x, y); square != kNoSquare) tapSquare(square);
      break;
    case ACTION_UNDO:
      runAction(GameAction::Undo);
      break;
    case ACTION_FLIP:
      runAction(GameAction::Flip);
      break;
    case ACTION_MENU:
      runAction(GameAction::Menu);
      break;
    case ACTION_PROMOTE:
      if (promoFrom != kNoSquare && event.value >= 0 && event.value < static_cast<int>(std::size(kPromotions))) {
        playMove(promoFrom, promoTo, kPromotions[event.value]);
      }
      break;
    case ACTION_NEW_GAME:
      runAction(GameAction::NewGame);
      break;
    default:
      break;
  }
}

void ChessActivity::goBack() {
  switch (view) {
    case View::Setup:
      if (inGame) {
        setView(View::Game);
      } else {
        app.clearTapFlash();
        finish();
      }
      break;
    case View::Picker:
      closePicker();
      break;
    case View::CustomTime:
      setView(View::Picker);
      break;
    case View::Menu:
      setView(View::Game);
      break;
    case View::Game:
      if (promoFrom != kNoSquare || selected != kNoSquare) {
        promoFrom = promoTo = kNoSquare;
        clearSelection();
        requestRepaint(RepaintUpdate);
      } else if (!hasBar()) {
        setView(View::Menu);
      } else {
        app.clearTapFlash();
        finish();
      }
      break;
  }
}

void ChessActivity::setView(const View next) {
  app.clearTapFlash();
  if (view == View::Game && next != View::Game) match->pause();
  view = next;
  if (next == View::Game) {
    keys.reset();
    match->resume();
    syncShownClocks();
    if (!mappedInput.hasTouch() && cursor < 0) cursor = kStartCursor;
  }
  if (next == View::Setup) {
    focus = rowCount() - 1;  // "Начать партию"
  } else if (next == View::Picker) {
    focus = currentOption(pickerRow);
  } else if (next == View::CustomTime) {
    focus = kCustomDoneRow;
  } else if (next == View::Menu) {
    // As the other games: touch and a finished game start on Новая игра (no stray tap resigns); keys start on
    // Продолжить.
    Row rows[kMaxRows];
    const int count = listRows(rows);
    const Row start = mappedInput.hasTouch() || match->game().isOver() ? Row::NewGame : Row::Continue;
    const Row* found = std::find(rows, rows + count, start);
    if (found == rows + count) found = std::find(rows, rows + count, Row::NewGame);
    focus = found < rows + count ? static_cast<int>(found - rows) : 0;
  } else {
    focus = mappedInput.hasTouch() ? -1 : 0;
  }
  requestRepaint(RepaintEntry);
}

// Back to the list the picker came from, focused on the row that opened it.
void ChessActivity::closePicker() {
  setView(pickerReturn);
  Row rows[kMaxRows];
  const int count = listRows(rows);
  for (int i = 0; i < count; ++i) {
    if (rows[i] == pickerRow) focus = i;
  }
}

void ChessActivity::openPicker(const Row row) {
  pickerRow = row;
  pickerReturn = view;
  setView(View::Picker);
}

void ChessActivity::activateRow(const int index) {
  if (view == View::Picker) {
    applyOption(index);
    return;
  }
  if (view == View::CustomTime) {
    if (index == 0 || index == 1) {
      // Keys only: Confirm walks the value up and wraps to the smallest.
      uint8_t& step = index == 0 ? customBase : customIncrement;
      const int count =
          index == 0 ? static_cast<int>(std::size(kBaseMinutes)) : static_cast<int>(std::size(kIncrementSeconds));
      step = static_cast<uint8_t>((step + 1) % count);
      requestRepaint(RepaintUpdate);
      return;
    }
    if (index != kCustomDoneRow) return;
    prefs.match.time.baseMs = kBaseMinutes[customBase] * 60000u;
    prefs.match.time.incrementMs = kIncrementSeconds[customIncrement] * 1000u;
    chess::ui::savePrefs(prefs);
    closePicker();
    return;
  }
  Row rows[kMaxRows];
  if (index < 0 || index >= listRows(rows)) return;
  switch (rows[index]) {
    case Row::Start:
      startGame();
      break;
    case Row::Continue:
      setView(View::Game);
      break;
    case Row::Resign:
      confirmResign();
      break;
    case Row::Draw:
      offerDraw();
      setView(View::Game);
      break;
    case Row::NewGame:
      setView(View::Setup);
      break;
    case Row::Flip:
      manualFlip = !manualFlip;
      setView(View::Game);
      break;
    case Row::Undo:
      if (match->undo()) afterPly();
      setView(View::Game);
      break;
    case Row::Exit:
      app.clearTapFlash();
      finish();
      break;
    default:
      openPicker(rows[index]);
      break;
  }
}

void ChessActivity::confirmResign() {
  auto confirm =
      makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_CHESS_RESIGN_CONFIRM),
                                              tr(STR_CHESS_RESIGN_BODY), tr(STR_CANCEL), tr(STR_CHESS_RESIGN));
  if (!confirm) {
    LOG_ERR("CHESS", "OOM: resign confirmation");
    return;
  }
  app.clearTapFlash();
  startActivityForResult(std::move(confirm), [this](const ActivityResult& result) {
    RenderLock lock(*this);
    if (result.isCancelled || match->game().isOver()) {
      requestRepaint(RepaintEntry);
      return;
    }
    match->resign();
    afterPly();
    setView(View::Game);
  });
}

void ChessActivity::applyOption(const int option) {
  if (option < 0 || option >= optionCount(pickerRow)) return;
  auto& m = prefs.match;
  switch (pickerRow) {
    case Row::Opponent:
      m.mode = option == 1 ? chess::Mode::HotSeat : chess::Mode::VsComputer;
      break;
    case Row::Level:
      m.level = static_cast<uint8_t>(option + 1);
      break;
    case Row::Color:
      m.humanSide = option == 1   ? chess::SideChoice::Black
                    : option == 2 ? chess::SideChoice::Random
                                  : chess::SideChoice::White;
      break;
    case Row::Time:
      if (option == kCustomTimeOption) {
        const bool custom = timePreset(m.time) == kCustomTimeOption;
        customBase = stepIndex(kBaseMinutes, custom ? m.time.baseMs / 60000 : kDefaultCustomMinutes);
        customIncrement = stepIndex(kIncrementSeconds, custom ? m.time.incrementMs / 1000 : kDefaultCustomSeconds);
        setView(View::CustomTime);
        return;
      }
      m.time = kTimePresets[option];
      break;
    case Row::Rotate:
      prefs.rotateBoard = option == 0;
      break;
    case Row::Pieces:
      prefs.pieceSet = static_cast<chess::ui::PieceSet>(option);
      break;
    default:
      return;
  }
  chess::ui::savePrefs(prefs);
  closePicker();
}

void ChessActivity::stepCustom(const int field, const int delta) {
  if (view != View::CustomTime) return;
  uint8_t& index = field == 0 ? customBase : customIncrement;
  const int last =
      field == 0 ? static_cast<int>(std::size(kBaseMinutes)) - 1 : static_cast<int>(std::size(kIncrementSeconds)) - 1;
  index = static_cast<uint8_t>(std::clamp(index + delta, 0, last));
  focus = field;
  requestRepaint(RepaintUpdate);
}

void ChessActivity::startGame() {
  match->newGame(prefs.match, esp_random());
  inGame = true;
  manualFlip = false;
  cursor = -1;
  afterPly();
  setView(View::Game);
}

void ChessActivity::tapSquare(const chess::Square square) {
  if (!match->isHumanTurn() || match->paused()) return;
  if (promoFrom != kNoSquare) {
    promoFrom = promoTo = kNoSquare;
    clearSelection();
    requestRepaint(RepaintUpdate);
    return;
  }
  if (selected != kNoSquare && ((targets >> square) & 1) != 0) {
    if (match->needsPromotion(selected, square)) {
      promoFrom = selected;
      promoTo = square;
      promoFocus = 0;
      requestRepaint(RepaintUpdate);
      return;
    }
    playMove(selected, square, chess::NO_PIECE);
    return;
  }
  const chess::Piece piece = match->game().position().at(square);
  if (piece != 0 && chess::colorOf(piece) == match->game().sideToMove() && square != selected) {
    selected = square;
    targets = match->legalTargets(square);
  } else {
    clearSelection();
  }
  requestRepaint(RepaintUpdate);
}

void ChessActivity::playMove(const chess::Square from, const chess::Square to, const chess::PieceType promo) {
  if (match->play(from, to, promo)) {
    afterPly();
    return;
  }
  promoFrom = promoTo = kNoSquare;
  clearSelection();
  requestRepaint(RepaintUpdate);
}

// After anything that changes the position or the result.
void ChessActivity::afterPly() {
  clearSelection();
  promoFrom = promoTo = kNoSquare;
  drawDeclined = false;
  syncShownClocks();
  savePending = true;
  requestRepaint(RepaintUpdate);
}

void ChessActivity::syncShownClocks() {
  for (const chess::Color side : {chess::WHITE, chess::BLACK}) {
    shownSeconds[side] = chess::ui::clockText(match->remainingMs(side)).shownSeconds;
  }
}

void ChessActivity::clearSelection() {
  selected = kNoSquare;
  targets = 0;
}

void ChessActivity::offerDraw() {
  if (match->game().isOver()) return;
  if (match->settings().mode == chess::Mode::VsComputer) {
    const chess::Position& position = match->game().position();
    int score = chess::evaluate(position);
    if (position.side == match->humanColor()) score = -score;
    if (score > kDrawAcceptCp) {
      drawDeclined = true;
      return;
    }
  }
  match->agreeDraw();
  afterPly();
}

// --- rows --------------------------------------------------------------------

int ChessActivity::listRows(Row* out) const {
  int n = 0;
  if (view == View::Setup) {
    const bool vsComputer = prefs.match.mode == chess::Mode::VsComputer;
    out[n++] = Row::Opponent;
    if (vsComputer) {
      out[n++] = Row::Level;
      out[n++] = Row::Color;
    }
    out[n++] = Row::Time;
    if (!vsComputer) out[n++] = Row::Rotate;
    out[n++] = Row::Pieces;
    out[n++] = Row::Start;
  } else if (view == View::Menu) {
    // The games' one menu form (GameScreen::openGameMenu); a finished game leads with Новая игра.
    const bool keys = !hasBar();
    const bool over = match->game().isOver();
    if (keys && !over) out[n++] = Row::Continue;
    if (over) out[n++] = Row::NewGame;
    if (keys && match->canUndo()) out[n++] = Row::Undo;
    if (keys) out[n++] = Row::Flip;
    if (!over) out[n++] = Row::Draw;
    out[n++] = Row::Pieces;
    if (!over) out[n++] = Row::NewGame;
    if (!over) out[n++] = Row::Resign;
    if (keys) out[n++] = Row::Exit;
  }
  return n;
}

int ChessActivity::rowCount() const {
  if (view == View::Picker) return optionCount(pickerRow);
  if (view == View::CustomTime) return kCustomDoneRow + 1;
  Row rows[kMaxRows];
  return listRows(rows);
}

ChessActivity::RowText ChessActivity::rowText(const int index) const {
  RowText text;
  if (view == View::Picker) {
    text.label = optionLabel(pickerRow, index);
    text.icon = index == currentOption(pickerRow) ? &icon_option_on_24 : &icon_option_24;
    if (pickerRow == Row::Pieces) text.preview = static_cast<int8_t>(index);
    if (pickerRow == Row::Time && index == kCustomTimeOption && currentOption(Row::Time) == kCustomTimeOption) {
      formatTime(prefs.match.time, text.value, sizeof(text.value));
    }
    return text;
  }
  Row rows[kMaxRows];
  if (index < 0 || index >= listRows(rows)) return text;
  text.label = rowLabel(rows[index]);
  text.icon = rowIcon(rows[index]);
  rowValue(rows[index], text.value, sizeof(text.value));
  return text;
}

const freeink::Icon* ChessActivity::rowIcon(const Row row) const {
  switch (row) {
    case Row::Opponent:
      return prefs.match.mode == chess::Mode::HotSeat ? &icon_friend_24 : &icon_computer_24;
    case Row::Level:
      return &icon_level_24;
    case Row::Color:
      return &icon_color_24;
    case Row::Time:
      return &icon_time_24;
    case Row::Rotate:
      return &icon_rotate_24;
    case Row::Pieces:
      return &icon_pieces_24;
    case Row::Start:
    case Row::Continue:
      return &icon_start_24;
    case Row::Resign:
      return &icon_finish_24;
    case Row::Draw:
      return &icon_draw_24;
    case Row::NewGame:
      return &icon_bar_new_game_24;
    case Row::Flip:
      return &icon_flip_24;
    case Row::Undo:
      return &icon_undo_move_24;
    case Row::Exit:
      return &icon_exit_24;
  }
  return nullptr;
}

const char* ChessActivity::rowLabel(const Row row) const {
  switch (row) {
    case Row::Opponent:
      return tr(STR_CHESS_OPPONENT);
    case Row::Level:
      return tr(STR_CHESS_LEVEL);
    case Row::Color:
      return tr(STR_CHESS_MY_COLOR);
    case Row::Time:
      return tr(STR_CHESS_TIME);
    case Row::Rotate:
      return tr(STR_CHESS_ROTATE);
    case Row::Pieces:
      return tr(STR_CHESS_PIECES);
    case Row::Start:
      return tr(STR_CHESS_START);
    case Row::Resign:
      return tr(STR_CHESS_RESIGN);
    case Row::Draw:
      return match->settings().mode == chess::Mode::HotSeat ? tr(STR_CHESS_AGREE_DRAW) : tr(STR_CHESS_OFFER_DRAW);
    case Row::NewGame:
      return tr(STR_GAMES_NEW_GAME);
    case Row::Continue:
      return tr(STR_GAMES_CONTINUE);
    case Row::Flip:
      return tr(STR_CHESS_FLIP);
    case Row::Undo:
      return tr(STR_GAMES_UNDO);
    case Row::Exit:
      return tr(STR_GAMES_LEAVE);
  }
  return "";
}

void ChessActivity::rowValue(const Row row, char* out, const size_t cap) const {
  out[0] = '\0';
  switch (row) {
    case Row::Opponent:
    case Row::Level:
    case Row::Color:
    case Row::Rotate:
    case Row::Pieces:
      snprintf(out, cap, "%s", optionLabel(row, currentOption(row)));
      break;
    case Row::Time:
      if (currentOption(row) == kCustomTimeOption) {
        formatTime(prefs.match.time, out, cap);
      } else {
        snprintf(out, cap, "%s", optionLabel(row, currentOption(row)));
      }
      break;
    default:
      break;
  }
}

int ChessActivity::optionCount(const Row row) const {
  switch (row) {
    case Row::Opponent:
    case Row::Rotate:
      return 2;
    case Row::Level:
      return static_cast<int>(std::size(kLevelLabels));
    case Row::Color:
      return 3;
    case Row::Time:
      return kCustomTimeOption + 1;
    case Row::Pieces:
      return chess::ui::PIECE_SET_COUNT;
    default:
      return 0;
  }
}

const char* ChessActivity::optionLabel(const Row row, const int option) const {
  switch (row) {
    case Row::Opponent:
      return option == 1 ? tr(STR_GAMES_TWO_PLAYERS) : tr(STR_CHESS_COMPUTER);
    case Row::Level:
      return kLevelLabels[std::clamp(option, 0, static_cast<int>(std::size(kLevelLabels)) - 1)];
    case Row::Color:
      return option == 1 ? tr(STR_CHESS_BLACK) : option == 2 ? tr(STR_CHESS_RANDOM) : tr(STR_CHESS_WHITE);
    case Row::Time:
      if (option == 0) return tr(STR_CHESS_UNLIMITED);
      if (option > 0 && option < kCustomTimeOption) return kTimePresetLabels[option];
      return tr(STR_CHESS_CUSTOM);
    case Row::Rotate:
      return option == 0 ? tr(STR_YES) : tr(STR_NO);
    case Row::Pieces:
      return I18N.get(kPieceSetNames[std::clamp(option, 0, chess::ui::PIECE_SET_COUNT - 1)]);
    default:
      return "";
  }
}

int ChessActivity::currentOption(const Row row) const {
  const auto& m = prefs.match;
  switch (row) {
    case Row::Opponent:
      return m.mode == chess::Mode::HotSeat ? 1 : 0;
    case Row::Level:
      return m.level - 1;
    case Row::Color:
      return m.humanSide == chess::SideChoice::Black ? 1 : m.humanSide == chess::SideChoice::Random ? 2 : 0;
    case Row::Time:
      return timePreset(m.time);
    case Row::Rotate:
      return prefs.rotateBoard ? 0 : 1;
    case Row::Pieces:
      return static_cast<int>(prefs.pieceSet);
    default:
      return 0;
  }
}

// --- game state helpers ----------------------------------------------------

bool ChessActivity::boardFlipped() const {
  const bool blackAtBottom = match->settings().mode == chess::Mode::VsComputer
                                 ? match->humanColor() == chess::BLACK
                                 : prefs.rotateBoard && match->game().sideToMove() == chess::BLACK;
  return blackAtBottom != manualFlip;
}

// Whose clock is the hero: yours against the computer, the side to move between friends.
chess::Color ChessActivity::heroSide() const {
  return match->settings().mode == chess::Mode::VsComputer ? match->humanColor() : match->game().sideToMove();
}

int ChessActivity::boardLeft() const { return (renderer.getScreenWidth() - kBoardSize) / 2; }

chess::Square ChessActivity::squareAtCell(const int cell) const {
  const bool flip = boardFlipped();
  const int col = cell % 8;
  const int row = cell / 8;
  return chess::makeSquare(flip ? 7 - col : col, flip ? row : 7 - row);
}

chess::Square ChessActivity::squareAt(const int x, const int y) const {
  const int left = boardLeft();
  if (x < left || x >= left + kBoardSize || y < kBoardTop || y >= kBoardTop + kBoardSize) return kNoSquare;
  return squareAtCell((y - kBoardTop) / kSquare * 8 + (x - left) / kSquare);
}

void ChessActivity::squareOrigin(const chess::Square square, const bool flip, int& x, int& y) const {
  const int col = flip ? 7 - chess::fileOf(square) : chess::fileOf(square);
  const int row = flip ? chess::rankOf(square) : 7 - chess::rankOf(square);
  x = boardLeft() + col * kSquare;
  y = kBoardTop + row * kSquare;
}

const char* ChessActivity::turnLabel() const {
  if (drawDeclined) return tr(STR_CHESS_DRAW_DECLINED);
  if (match->settings().mode == chess::Mode::VsComputer) {
    return match->isHumanTurn() ? tr(STR_CHESS_YOUR_MOVE) : tr(STR_CHESS_THINKING);
  }
  return match->game().sideToMove() == chess::WHITE ? tr(STR_CHESS_WHITE_TO_MOVE) : tr(STR_CHESS_BLACK_TO_MOVE);
}

void ChessActivity::lastMoveText(char* out, const size_t cap) const {
  out[0] = '\0';
  const chess::Game& game = match->game();
  if (game.plyCount() == 0) return;
  const int ply = game.plyCount() - 1;
  char san[24];
  chess::formatSan(game.ply(ply).san, notation(), san, sizeof(san));
  if (game.colorOfPly(ply) == chess::WHITE) {
    snprintf(out, cap, "%d. %s", game.moveNumberOfPly(ply), san);
  } else {
    snprintf(out, cap, "%d\xE2\x80\xA6 %s", game.moveNumberOfPly(ply), san);
  }
}

// --- render task -------------------------------------------------------------

void ChessActivity::render(RenderLock&&) {
  renderer.clearScreen();
  if (view == View::Game) {
    HeaderBackTapTarget::clear();  // the board screen has its own "‹ Выйти"
  } else {
    const auto& metrics = UITheme::getInstance().getMetrics();
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, listTitle());
  }
  renderUi();
  // Button boards only; the lists and the action bar end above the hints.
  const bool board = view == View::Game;
  const auto labels = mappedInput.mapLabels(!board                                            ? tr(STR_BACK)
                                            : selected != kNoSquare || promoFrom != kNoSquare ? tr(STR_CANCEL)
                                                                                              : tr(STR_CHESS_MENU),
                                            tr(STR_SELECT), board ? tr(STR_DIR_LEFT) : tr(STR_DIR_UP),
                                            board ? tr(STR_DIR_RIGHT) : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const uint8_t kind = repaint.exchange(RepaintNone);
  renderer.displayBuffer(kind == RepaintEntry   ? cadence.entry()
                         : kind == RepaintClock ? HalDisplay::FAST_REFRESH
                                                : cadence.update());
}

bool ChessActivity::hasBar() const { return UITheme::getInstance().getMetrics().buttonHintsHeight == 0; }

int ChessActivity::actionsTop() const {
  return renderer.getScreenHeight() - (hasBar() ? games::ui::kActionBarHeight : 0) -
         UITheme::getInstance().getMetrics().buttonHintsHeight;
}

void ChessActivity::screenFn(UiScreen& screen, void* user) { static_cast<ChessActivity*>(user)->build(screen); }

void ChessActivity::build(UiScreen& screen) {
  if (!match) return;
  switch (view) {
    case View::Setup:
    case View::Picker:
    case View::Menu:
      buildList(screen);
      break;
    case View::CustomTime:
      buildCustomTime(screen);
      break;
    case View::Game:
      buildGame(screen);
      break;
  }
}

const char* ChessActivity::listTitle() const {
  switch (view) {
    case View::Setup:
      return tr(STR_GAMES_NEW_GAME);
    case View::Menu:
      return tr(STR_CHESS);
    case View::CustomTime:
      return tr(STR_CHESS_CUSTOM_TIME);
    default:
      return rowLabel(pickerRow);
  }
}

// Under the theme header (arrow-left on touch boards, tap = Back).
int ChessActivity::listTop() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
}

// One list row: bar when focused, icon, label.
void ChessActivity::drawRow(UiScreen& screen, const int top, const int index, const freeink::Icon* icon,
                            const char* label) const {
  const bool focused = index == focus;
  if (focused) drawSelectionBar(renderer, kListBarX, top, kRowHeight);
  int x = kListTextX;
  if (icon != nullptr) {
    screen.target().bitmap(fui::makeRect(kListIconX, top + (kRowHeight - kIconSize) / 2, kIconSize, kIconSize),
                           fui::bitmapFromIcon(*icon), fui::BitmapMode::Center);
    x = kListIconX + kIconSize + kListIconGap;
  }
  renderer.drawText(kListFont, x, top + (kRowHeight - renderer.getLineHeight(kListFont)) / 2, label, true,
                    focused ? kSemiBold : EpdFontFamily::REGULAR);
}

void ChessActivity::buildList(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int textOffset = (kRowHeight - renderer.getLineHeight(kListFont)) / 2;
  const int count = rowCount();
  for (int i = 0; i < count; ++i) {
    const int top = listTop() + i * (kRowHeight + kRowGap);
    const RowText row = rowText(i);
    drawRow(screen, top, i, row.icon, row.label);
    if (row.preview >= 0) {
      const auto set = static_cast<chess::ui::PieceSet>(row.preview);
      constexpr int kPreviewGap = 8;
      int x = width - kListValueInset;
      for (int k = static_cast<int>(std::size(kPreviewKinds)) - 1; k >= 0; --k) {
        const auto& bitmap =
            chess::ui::pieceBitmap(set, chess::ui::PieceColor::White, kPreviewKinds[k], chess::ui::PieceSize::Small);
        x -= bitmap.size;
        chess::ui::drawPiece(renderer, bitmap, x, top + (kRowHeight - bitmap.size) / 2);
        x -= kPreviewGap;
      }
    } else if (row.value[0] != '\0') {
      // The row value role: small text on the label's baseline.
      const int valueTop =
          top + textOffset + renderer.getFontAscenderSize(kListFont) - renderer.getFontAscenderSize(kText10Font);
      drawRightAligned(renderer, kText10Font, width - kListValueInset, valueTop, row.value);
    }
    screen.frame().hit(fui::makeRect(0, top, width, kRowHeight), ACTION_ROW, static_cast<int16_t>(i), fui::InputTouch);
  }
}

void ChessActivity::buildCustomTime(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int textOffset = (kRowHeight - renderer.getLineHeight(kListFont)) / 2;
  const freeink::Icon* icons[] = {&icon_minutes_24, &icon_increment_24};
  const char* labels[] = {tr(STR_CHESS_MINUTES), tr(STR_CHESS_INCREMENT)};
  const int values[] = {kBaseMinutes[customBase], kIncrementSeconds[customIncrement]};
  const bool atMin[] = {customBase == 0, customIncrement == 0};
  const bool atMax[] = {customBase + 1 == static_cast<int>(std::size(kBaseMinutes)),
                        customIncrement + 1 == static_cast<int>(std::size(kIncrementSeconds))};
  // [−] value [+] at the right edge.
  const int plusX = width - kListValueInset - kStepperSize;
  const int valueX = plusX - kStepperSize;
  const int minusX = valueX - kStepperSize;
  int top = listTop();
  for (int i = 0; i < kCustomDoneRow; ++i) {
    const int textTop = top + textOffset;
    const int stepTop = top + (kRowHeight - kStepperSize) / 2;
    drawRow(screen, top, i, icons[i], labels[i]);
    char value[8];
    snprintf(value, sizeof(value), "%d", values[i]);
    drawCentered(renderer, kListFont, valueX, kStepperSize, textTop, value, kSemiBold);
    if (!atMin[i]) {
      drawCentered(renderer, kListFont, minusX, kStepperSize, textTop, "\xE2\x88\x92", kSemiBold);
      screen.frame().hit(fui::makeRect(minusX, stepTop, kStepperSize, kStepperSize), ACTION_STEP,
                         static_cast<int16_t>(i * 2), fui::InputTouch);
    }
    if (!atMax[i]) {
      drawCentered(renderer, kListFont, plusX, kStepperSize, textTop, "+", kSemiBold);
      screen.frame().hit(fui::makeRect(plusX, stepTop, kStepperSize, kStepperSize), ACTION_STEP,
                         static_cast<int16_t>(i * 2 + 1), fui::InputTouch);
    }
    top += kRowHeight + kRowGap;
  }
  drawRow(screen, top, kCustomDoneRow, &icon_done_24, tr(STR_CHESS_DONE));
  screen.frame().hit(fui::makeRect(0, top, width, kRowHeight), ACTION_ROW, kCustomDoneRow, fui::InputTouch);
}

void ChessActivity::buildGame(UiScreen& screen) {
  drawTopLine(screen);
  drawBoard();
  screen.frame().hit(fui::makeRect(boardLeft(), kBoardTop, kBoardSize, kBoardSize), ACTION_BOARD, 0, fui::InputTouch);
  if (promoFrom != kNoSquare) {
    drawPromotion(screen);
    return;
  }
  if (match->game().isOver()) {
    drawGameOver();
  } else if (match->timed()) {
    drawClockPanel();
  } else {
    drawStatusPanel();
  }
  drawActions(screen);
}

void ChessActivity::drawTopLine(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  drawStatusLine(renderer);
  int exitWidth = 0;
  if (hasBar()) {
    const char* exitLabel = tr(STR_CHESS_NAV_EXIT);
    renderer.drawText(kText12Font, kSidePadding, kHeaderTop, exitLabel);
    exitWidth = renderer.getTextWidth(kText12Font, exitLabel) + 2 * kSidePadding;
    screen.frame().hit(fui::makeRect(0, 0, exitWidth, kBoardTop), ACTION_BACK, 0, fui::InputTouch);
  }

  // The far side of the board: the computer, or between friends the side not to move; its clock when timed.
  const chess::Color far = chess::opposite(heroSide());
  const bool timed = match->timed();
  char level[12];
  snprintf(level, sizeof(level), "%u", static_cast<unsigned>(match->settings().level));
  char clock[24] = "";
  if (timed) {
    const chess::ui::ClockText time = chess::ui::clockText(match->remainingMs(far));
    if (time.minutes) {
      snprintf(clock, sizeof(clock), "%s %s", time.text, tr(STR_CHESS_MIN));
    } else {
      snprintf(clock, sizeof(clock), "%s", time.text);
    }
  }
  games::ui::StatusItem status[3];
  int count = 0;
  const bool vs = match->settings().mode == chess::Mode::VsComputer;
  if (vs) {
    status[count++] = {tr(STR_GAMES_COMPUTER_LOWER), nullptr};
    status[count++] = {tr(STR_GAMES_LEVEL), level};
  } else if (timed) {
    status[count++] = {far == chess::WHITE ? tr(STR_CHESS_WHITE) : tr(STR_CHESS_BLACK), nullptr};
  }
  if (timed) status[count++] = {nullptr, clock};
  // The status keeps kExitStatusGap from "‹ Выйти": too long, the level drops "компьютер", then with a clock
  // the clock stands alone.
  const int room = exitWidth > 0 ? width - exitWidth - kExitStatusGap : width - kSidePadding;
  if (vs && games::ui::statusWidth(renderer, status, count) > room) {
    std::copy(status + 1, status + count, status);
    --count;
  }
  if (timed && games::ui::statusWidth(renderer, status, count) > room) {
    status[0] = status[count - 1];
    count = 1;
  }
  if (count > 0) games::ui::drawStatus(renderer, width - kSidePadding, kHeaderTop, status, count);
}

void ChessActivity::drawBoard() const {
  const chess::Game& game = match->game();
  const chess::Position& position = game.position();
  const bool flip = boardFlipped();
  for (int square = 0; square < 64; ++square) {
    int x = 0;
    int y = 0;
    squareOrigin(square, flip, x, y);
    if ((chess::fileOf(square) + chess::rankOf(square)) % 2 == 0) {
      renderer.fillRectDither(x, y, kSquare, kSquare, Color::LightGray);
    }
    const chess::Piece piece = position.at(square);
    if (piece == 0) continue;
    chess::ui::drawPiece(renderer,
                         chess::ui::pieceBitmap(prefs.pieceSet, pieceColor(chess::colorOf(piece)),
                                                pieceKind(chess::typeOf(piece)), chess::ui::PieceSize::Board),
                         x + kPieceInset, y + kPieceInset);
  }

  int x = 0;
  int y = 0;
  chess::Move last;
  if (game.lastMove(last)) {
    for (const chess::Square square : {last.from, last.to}) {
      squareOrigin(square, flip, x, y);
      drawDashedFrame(renderer, x, y, kSquare);
    }
  }
  const bool cursorShown = cursor >= 0 && cursor < kCells && !game.isOver();
  if (selected != kNoSquare) {
    squareOrigin(selected, flip, x, y);
    drawPick(renderer, x, y, cursorShown && squareAtCell(cursor) == selected);
  }
  if (cursorShown) {
    drawCellFrame(renderer, boardLeft() + cursor % 8 * kSquare, kBoardTop + cursor / 8 * kSquare, kSquare, kSquare);
  }
  for (int square = 0; square < 64; ++square) {
    if (((targets >> square) & 1) == 0) continue;
    squareOrigin(square, flip, x, y);
    if (position.at(square) != 0) {
      renderer.drawRoundedRect(x + 1, y + 1, kSquare - 2, kSquare - 2, kRingWidth, (kSquare - 2) / 2, true);
    } else {
      renderer.fillRoundedRect(x + (kSquare - kDotSize) / 2, y + (kSquare - kDotSize) / 2, kDotSize, kDotSize,
                               kDotSize / 2, Color::Black);
    }
  }
}

// Turn label on the left (SemiBold when a person is to move), last move on the right.
void ChessActivity::drawTurnLine(const int top) const {
  const int width = renderer.getScreenWidth();
  char last[32];
  lastMoveText(last, sizeof(last));
  int lastWidth = 0;
  if (last[0] != '\0') {
    drawRightAligned(renderer, kText12Font, width - kSidePadding, top, last);
    lastWidth = renderer.getTextWidth(kText12Font, last) + kTurnLineGap;
  }
  drawFitted(renderer, kText12Font, kSidePadding, top, turnLabel(), width - kSidePadding - lastWidth - kSidePadding,
             match->isHumanTurn() ? kSemiBold : EpdFontFamily::REGULAR);
}

void ChessActivity::drawClockPanel() const {
  const chess::Color hero = heroSide();
  const chess::ui::ClockText time = chess::ui::clockText(match->remainingMs(hero));
  const char* clock = time.text;
  // Above a minute the number is minutes with a small "мин" after it.
  const char* unit = time.minutes ? tr(STR_CHESS_MIN) : nullptr;
  const int unitWidth = unit != nullptr ? kUnitGap + renderer.getTextWidth(kText12Font, unit) : 0;
  // Unbounded is wide: a long number may not fit at 96 px. Scale the face down
  // just enough, measured with the widest digit in every place so the size
  // holds while the time runs down.
  int32_t digitAdvance = 0;
  int32_t advance = 0;
  int glyphTop = 0;
  for (char digit = '0'; digit <= '9'; ++digit) {
    if (renderer.getCodepointMetrics(kHeroFont, digit, EpdFontFamily::REGULAR, advance, glyphTop)) {
      digitAdvance = std::max(digitAdvance, advance);
    }
  }
  int32_t colonAdvance = 0;
  renderer.getCodepointMetrics(kHeroFont, ':', EpdFontFamily::REGULAR, colonAdvance, glyphTop);
  int32_t naturalWidth = 0;  // 12.4 fixed point
  for (const char* c = clock; *c != '\0'; ++c) naturalWidth += *c == ':' ? colonAdvance : digitAdvance;
  const int maxWidth = renderer.getScreenWidth() - kSidePadding - kSidePadding - unitWidth;
  int scale256 = kHeroCapTarget > 0 ? kHeroCapTarget * 256 / std::max(1, heroCapHeight(renderer, kHeroFont)) : 256;
  if (naturalWidth * scale256 > maxWidth * 16 * 256) scale256 = maxWidth * 16 * 256 / naturalWidth;
  // The digits shrink to the room above the turn line, which button boards cut by the hint row.
  const int room = actionsTop() - 6 - renderer.getLineHeight(kText12Font) - 14 - (kPanelTop + 22);
  const int naturalCap = std::max(1, heroCapHeight(renderer, kHeroFont));
  if (naturalCap * scale256 / 256 > room) scale256 = room * 256 / naturalCap;

  const int capTop = kPanelTop + 22;
  const int capHeight = heroCapHeight(renderer, kHeroFont) * scale256 / 256;
  const bool heroToMove = match->isHumanTurn() && match->game().sideToMove() == hero;
  if (heroToMove) drawSelectionBar(renderer, kSelectionBarX, capTop, capHeight);
  int x = kSidePadding;
  if (scale256 == 256) {
    x += drawHeroNumber(renderer, kHeroFont, x, capTop, clock);
  } else {
    for (const char* c = clock; *c != '\0'; ++c) {
      x += renderer.drawScaledCodepoint(kHeroFont, static_cast<uint8_t>(*c), EpdFontFamily::REGULAR, x,
                                        capTop + capHeight, scale256);
    }
  }
  if (unit != nullptr) {
    // On the digits' baseline.
    renderer.drawText(kText12Font, x + kUnitGap, capTop + capHeight - renderer.getFontAscenderSize(kText12Font), unit);
  }
  drawTurnLine(capTop + capHeight + 14);
}

void ChessActivity::drawStatusPanel() const {
  const int top = games::ui::captionTop(renderer, kPanelTop, kText12Font);
  drawTurnLine(top);
  drawMoveList(top + renderer.getLineHeight(kText12Font) + 12, actionsTop() - 6);
}

// Move list in Russian notation, newest last; only the lines that fit are shown.
void ChessActivity::drawMoveList(const int top, const int bottom) const {
  const chess::Game& game = match->game();
  const int plies = game.plyCount();
  const int lineHeight = renderer.getLineHeight(kText10Font);
  const int maxLines = (bottom - top) / lineHeight;
  if (plies == 0 || maxLines <= 0) return;

  // Lay out only the tail, anchored on a 16-ply boundary so lines reflow rarely.
  constexpr int kMaxUnits = 24;
  struct Unit {
    char text[48];
    int width;
  };
  Unit units[kMaxUnits];
  int count = 0;
  int ply = std::max(0, plies - 32) / 16 * 16;
  while (ply < plies && count < kMaxUnits) {
    const int number = game.moveNumberOfPly(ply);
    char white[24] = "";
    char black[24] = "";
    if (game.colorOfPly(ply) == chess::WHITE) chess::formatSan(game.ply(ply++).san, notation(), white, 24);
    if (ply < plies && game.colorOfPly(ply) == chess::BLACK && game.moveNumberOfPly(ply) == number) {
      chess::formatSan(game.ply(ply++).san, notation(), black, 24);
    }
    Unit& unit = units[count++];
    if (white[0] == '\0') {
      snprintf(unit.text, sizeof(unit.text), "%d\xE2\x80\xA6 %s", number, black);
    } else if (black[0] == '\0') {
      snprintf(unit.text, sizeof(unit.text), "%d. %s", number, white);
    } else {
      snprintf(unit.text, sizeof(unit.text), "%d. %s %s", number, white, black);
    }
    unit.width = renderer.getTextWidth(kText10Font, unit.text);
  }

  const int maxWidth = renderer.getScreenWidth() - kSidePadding - kSidePadding;
  const int gap = 2 * renderer.getSpaceWidth(kText10Font);
  int lineStart[kMaxUnits + 1];
  int lines = 0;
  int x = 0;
  for (int u = 0; u < count; ++u) {
    if (u == 0 || x + gap + units[u].width > maxWidth) {
      lineStart[lines++] = u;
      x = units[u].width;
    } else {
      x += gap + units[u].width;
    }
  }
  lineStart[lines] = count;
  const int firstLine = std::max(0, lines - maxLines);
  for (int line = firstLine; line < lines; ++line) {
    int lineX = kSidePadding;
    const int y = top + (line - firstLine) * lineHeight;
    for (int u = lineStart[line]; u < lineStart[line + 1]; ++u) {
      renderer.drawText(kText10Font, lineX, y, units[u].text);
      lineX += units[u].width + gap;
    }
  }
}

void ChessActivity::drawPromotion(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  const int top = games::ui::captionTop(renderer, kPanelTop, kText12Font);
  renderer.drawText(kText12Font, kSidePadding, top, tr(STR_CHESS_PROMOTE), true, kSemiBold);
  const int cellTop = top + renderer.getLineHeight(kText12Font) + 8;
  const int cellWidth = width / static_cast<int>(std::size(kPromotions));
  const int cellHeight = actionsTop() - cellTop;
  const char* names[] = {tr(STR_CHESS_QUEEN), tr(STR_CHESS_ROOK), tr(STR_CHESS_BISHOP), tr(STR_CHESS_KNIGHT)};
  const auto color = pieceColor(match->game().sideToMove());
  for (int i = 0; i < static_cast<int>(std::size(kPromotions)); ++i) {
    const int x = i * cellWidth;
    const auto& bitmap =
        chess::ui::pieceBitmap(prefs.pieceSet, color, pieceKind(kPromotions[i]), chess::ui::PieceSize::Small);
    // A short cell (button boards) lifts the name above the focus frame, and the piece above the name.
    const int nameTop = std::min(cellTop + 16 + bitmap.size,
                                 cellTop + cellHeight - kCellFrameWidth - 2 - renderer.getLineHeight(kText10Font));
    chess::ui::drawPiece(renderer, bitmap, x + (cellWidth - bitmap.size) / 2,
                         std::max(cellTop + kCellFrameWidth, std::min(cellTop + 8, nameTop - 4 - bitmap.size)));
    drawCentered(renderer, kText10Font, x, cellWidth, nameTop, names[i]);
    if (i == promoFocus) drawCellFrame(renderer, x, cellTop, cellWidth, cellHeight);
    screen.frame().hit(fui::makeRect(x, cellTop, cellWidth, cellHeight), ACTION_PROMOTE, static_cast<int16_t>(i),
                       fui::InputTouch);
  }
}

// Who won first and how as the detail, as in draughts; a draw leads with how.
void ChessActivity::drawGameOver() const {
  const bool draw = match->game().outcome() == chess::Outcome::Draw;
  const char* how = overTitle(match->game());
  const char* who = overSubtitle(*match);
  games::ui::drawResult(renderer, games::ui::captionTop(renderer, kPanelTop, kText12Font), draw ? how : who,
                        draw ? who : how, hasBar() ? nullptr : tr(STR_GAMES_NEW_GAME_KEY));
}

void ChessActivity::drawActions(UiScreen& screen) const {
  if (!hasBar()) return;
  const GameAction focused = focusedAction();
  const bool over = match->game().isOver();
  games::ui::ActionSlot slots[] = {
      {&icon_undo_move_24, tr(STR_GAMES_UNDO), false, ACTION_UNDO, 0},
      over ? games::ui::ActionSlot{&icon_bar_new_game_24, tr(STR_GAMES_NEW_GAME), false, ACTION_NEW_GAME, 0}
           : games::ui::ActionSlot{&icon_flip_24, tr(STR_CHESS_FLIP_SHORT), false, ACTION_FLIP, 0},
      {&icon_menu_24, tr(STR_CHESS_MENU), false, ACTION_MENU, 0},
  };
  if (!match->canUndo()) slots[0].label = nullptr;
  const int focusedSlot = focused == GameAction::Undo                                     ? 0
                          : focused == GameAction::Flip || focused == GameAction::NewGame ? 1
                          : focused == GameAction::None                                   ? -1
                                                                                          : 2;
  games::ui::drawActionBar(renderer, screen, actionsTop(), slots, static_cast<int>(std::size(slots)), focusedSlot,
                           fui::InputTouch);
}
