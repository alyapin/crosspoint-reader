#if FREEINK_DEVICE_X4PRO

#include "ChessActivity.h"

#include <ChessEngine.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <new>

#include "PieceSets.h"
#include "games/chess/ChessSave.h"

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

// List screens: same metrics as the other Setka lists.
constexpr int kHeaderTop = 16;  // clears the X4 Pro top bezel
constexpr int kTitleGap = 20;
constexpr int kRuleGap = 16;
constexpr int kBarX = kSidePadding - kSelectionBarWidth;
constexpr int kRowTextX = kSidePadding + 12;
constexpr int kStepperSize = 56;
constexpr int kCustomDoneRow = 2;

// Game screen: top line, full-bleed 8x8 board, panel, actions.
constexpr int kSquare = 60;
constexpr int kBoardTop = 60;
constexpr int kBoardSize = 8 * kSquare;
constexpr int kPanelTop = kBoardTop + kBoardSize;
constexpr int kActionsTop = 718;
constexpr int kPieceInset = (kSquare - 56) / 2;
constexpr int kDotSize = 16;
constexpr int kRingWidth = 3;
constexpr int kTurnLineGap = 16;

constexpr uint8_t kBaseMinutes[] = {1, 2, 3, 5, 10, 15, 20, 25, 30, 45, 60, 90, 120, 180};
constexpr uint8_t kIncrementSeconds[] = {0, 1, 2, 3, 5, 10, 15, 20, 30, 45, 60};
constexpr uint32_t kDefaultCustomMinutes = 25;
constexpr uint32_t kDefaultCustomSeconds = 10;
constexpr chess::TimeControl kTimePresets[] = {chess::TIME_UNLIMITED, chess::TIME_5_3, chess::TIME_10_5,
                                               chess::TIME_15_10};
constexpr int kCustomTimeOption = static_cast<int>(std::size(kTimePresets));
constexpr const char* kTimePresetLabels[] = {nullptr, "5+3", "10+5", "15+10"};
constexpr const char* kLevelLabels[] = {"1", "2", "3", "4", "5"};
constexpr const char* kPieceSetNames[chess::ui::PIECE_SET_COUNT] = {"Lucide", "Tabler", "Material", "Font Awesome"};
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

uint32_t clockSeconds(const uint32_t ms) { return (ms + 999) / 1000; }

void formatClock(const uint32_t ms, char* out, const size_t cap) {
  const uint32_t seconds = clockSeconds(ms);
  snprintf(out, cap, "%u:%02u", static_cast<unsigned>(seconds / 60), static_cast<unsigned>(seconds % 60));
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

// Last move marker: 2 px dashes inset 2 px, one dash in each corner.
void drawDashedFrame(const GfxRenderer& renderer, const int x, const int y) {
  constexpr int kInset = 2;
  constexpr int kWidth = 2;
  constexpr int kDash = 8;
  constexpr int kGap = 4;
  constexpr int kLength = kSquare - 2 * kInset;
  const int left = x + kInset;
  const int top = y + kInset;
  for (int d = 0; d < kLength; d += kDash + kGap) {
    const int n = std::min(kDash, kLength - d);
    renderer.fillRect(left + d, top, n, kWidth, true);
    renderer.fillRect(left + d, top + kLength - kWidth, n, kWidth, true);
    renderer.fillRect(left, top + d, kWidth, n, true);
    renderer.fillRect(left + kLength - kWidth, top + d, kWidth, n, true);
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
    inGame = true;
    rotateGame = prefs.rotateBoard;
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

bool ChessActivity::skipLoopDelay() { return match && match->engineThinking(); }

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
  if (view == View::Game) {
    {
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock() && match->update()) afterPly();
    }
    tickClocks();
  }

  if (const auto touch = routeTouch(mappedInput)) {
    RenderLock lock(*this);
    handleAction(touch.event, touch.snap.touchX, touch.snap.touchY);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    RenderLock lock(*this);
    goBack();
  } else if (view != View::Game) {
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

void ChessActivity::tickClocks() {
  // The computer's reply is a few seconds at most: its clock catches up when it moves.
  if (!match->timed() || match->paused() || match->game().isOver() || match->engineThinking()) return;
  bool changed = false;
  for (const chess::Color side : {chess::WHITE, chess::BLACK}) {
    const uint32_t seconds = clockSeconds(match->remainingMs(side));
    if (seconds != shownSeconds[side]) {
      shownSeconds[side] = seconds;
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
      if (match->undo()) afterPly();
      break;
    case ACTION_MENU:
      setView(View::Menu);
      break;
    case ACTION_PROMOTE:
      if (promoFrom != kNoSquare && event.value >= 0 && event.value < static_cast<int>(std::size(kPromotions))) {
        playMove(promoFrom, promoTo, kPromotions[event.value]);
      }
      break;
    case ACTION_NEW_GAME:
      setView(View::Setup);
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
      setView(pickerReturn);
      break;
    case View::CustomTime:
      setView(View::Picker);
      break;
    case View::Menu:
      setView(View::Game);
      break;
    case View::Game:
      if (promoFrom != kNoSquare) {
        promoFrom = promoTo = kNoSquare;
        clearSelection();
        requestRepaint(RepaintUpdate);
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
    match->resume();
    syncShownClocks();
  }
  if (next == View::Setup) {
    focus = rowCount() - 1;  // "Начать партию"
  } else if (next == View::Picker) {
    focus = currentOption(pickerRow);
  } else if (next == View::CustomTime) {
    focus = kCustomDoneRow;
  } else {
    focus = -1;  // the menu shows no bar until a key moves it
  }
  requestRepaint(RepaintEntry);
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
    if (index != kCustomDoneRow) return;
    prefs.match.time.baseMs = kBaseMinutes[customBase] * 60000u;
    prefs.match.time.incrementMs = kIncrementSeconds[customIncrement] * 1000u;
    chess::ui::savePrefs(prefs);
    setView(pickerReturn);
    return;
  }
  Row rows[kMaxRows];
  if (index < 0 || index >= listRows(rows)) return;
  switch (rows[index]) {
    case Row::Start:
      startGame();
      break;
    case Row::Resign:
      match->resign();
      afterPly();
      setView(View::Game);
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
    default:
      openPicker(rows[index]);
      break;
  }
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
  setView(pickerReturn);
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
  rotateGame = prefs.rotateBoard;
  manualFlip = false;
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
    shownSeconds[side] = clockSeconds(match->remainingMs(side));
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
    if (!match->game().isOver()) {
      out[n++] = Row::Resign;
      out[n++] = Row::Draw;
    }
    out[n++] = Row::NewGame;
    out[n++] = Row::Flip;
    out[n++] = Row::Pieces;
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
    if (pickerRow == Row::Pieces) text.preview = static_cast<int8_t>(index);
    if (pickerRow == Row::Time && index == kCustomTimeOption && currentOption(Row::Time) == kCustomTimeOption) {
      formatTime(prefs.match.time, text.value, sizeof(text.value));
    }
    return text;
  }
  Row rows[kMaxRows];
  if (index < 0 || index >= listRows(rows)) return text;
  text.label = rowLabel(rows[index]);
  rowValue(rows[index], text.value, sizeof(text.value));
  text.ruleAbove = rows[index] == Row::Start;
  return text;
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
      return tr(STR_CHESS_NEW_GAME);
    case Row::Flip:
      return tr(STR_CHESS_FLIP);
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
      return option == 1 ? tr(STR_CHESS_FRIEND) : tr(STR_CHESS_COMPUTER);
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
      return kPieceSetNames[std::clamp(option, 0, chess::ui::PIECE_SET_COUNT - 1)];
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
                                 : rotateGame && match->game().sideToMove() == chess::BLACK;
  return blackAtBottom != manualFlip;
}

// Whose clock is the hero: yours against the computer, the side to move between friends.
chess::Color ChessActivity::heroSide() const {
  return match->settings().mode == chess::Mode::VsComputer ? match->humanColor() : match->game().sideToMove();
}

chess::Square ChessActivity::squareAt(const int x, const int y) const {
  if (x < 0 || x >= kBoardSize || y < kBoardTop || y >= kBoardTop + kBoardSize) return kNoSquare;
  const int col = x / kSquare;
  const int row = (y - kBoardTop) / kSquare;
  const bool flip = boardFlipped();
  return chess::makeSquare(flip ? 7 - col : col, flip ? row : 7 - row);
}

void ChessActivity::squareOrigin(const chess::Square square, const bool flip, int& x, int& y) const {
  const int col = flip ? 7 - chess::fileOf(square) : chess::fileOf(square);
  const int row = flip ? chess::rankOf(square) : 7 - chess::rankOf(square);
  x = col * kSquare;
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
  chess::formatSan(game.ply(ply).san, chess::NOTATION_RU, san, sizeof(san));
  if (game.colorOfPly(ply) == chess::WHITE) {
    snprintf(out, cap, "%d. %s", game.moveNumberOfPly(ply), san);
  } else {
    snprintf(out, cap, "%d\xE2\x80\xA6 %s", game.moveNumberOfPly(ply), san);
  }
}

// --- render task -------------------------------------------------------------

void ChessActivity::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const uint8_t kind = repaint.exchange(RepaintNone);
  renderer.displayBuffer(kind == RepaintEntry   ? cadence.entry()
                         : kind == RepaintClock ? HalDisplay::FAST_REFRESH
                                                : cadence.update());
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

int ChessActivity::drawHeader(UiScreen& screen, const char* back, const char* title) const {
  const int lineHeight = renderer.getLineHeight(kText12Font);
  renderer.drawText(kText12Font, kSidePadding, kHeaderTop, back);
  screen.frame().hit(
      fui::makeRect(0, 0, renderer.getTextWidth(kText12Font, back) + 2 * kSidePadding, lineHeight + 2 * kHeaderTop),
      ACTION_BACK, 0, fui::InputTouch);
  const int titleTop = kHeaderTop + lineHeight + kTitleGap;
  renderer.drawText(kText12Font, kSidePadding, titleTop, title, true, kSemiBold);
  const int ruleY = titleTop + lineHeight + kRuleGap;
  drawRule(renderer, 0, ruleY, renderer.getScreenWidth());
  return ruleY + kRuleWidth;
}

void ChessActivity::buildList(UiScreen& screen) {
  const char* title = view == View::Setup  ? tr(STR_CHESS_NEW_GAME)
                      : view == View::Menu ? tr(STR_CHESS_MENU)
                                           : rowLabel(pickerRow);
  int top = drawHeader(screen, tr(STR_CHESS_NAV_BACK), title);
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int rowHeight = std::max(kListRowHeight, lineHeight + 2 * kRuleGap);
  const int count = rowCount();
  for (int i = 0; i < count; ++i) {
    const RowText row = rowText(i);
    if (row.ruleAbove) {
      drawRule(renderer, 0, top, width);
      top += kRuleWidth;
    }
    const int textTop = top + (rowHeight - lineHeight) / 2;
    const bool focused = i == focus;
    if (focused) drawSelectionBar(renderer, kBarX, textTop, lineHeight);
    renderer.drawText(kText12Font, kRowTextX, textTop, row.label, true, focused ? kSemiBold : EpdFontFamily::REGULAR);
    if (row.preview >= 0) {
      const auto set = static_cast<chess::ui::PieceSet>(row.preview);
      constexpr int kPreviewGap = 8;
      int x = width - kSidePadding;
      for (int k = static_cast<int>(std::size(kPreviewKinds)) - 1; k >= 0; --k) {
        const auto& bitmap =
            chess::ui::pieceBitmap(set, chess::ui::PieceColor::White, kPreviewKinds[k], chess::ui::PieceSize::Small);
        x -= bitmap.size;
        chess::ui::drawPiece(renderer, bitmap, x, top + (rowHeight - bitmap.size) / 2);
        x -= kPreviewGap;
      }
    } else if (row.value[0] != '\0') {
      drawRightAligned(renderer, kText12Font, width - kSidePadding, textTop, row.value);
    }
    screen.frame().hit(fui::makeRect(0, top, width, rowHeight), ACTION_ROW, static_cast<int16_t>(i), fui::InputTouch);
    top += rowHeight;
  }
}

void ChessActivity::buildCustomTime(UiScreen& screen) {
  int top = drawHeader(screen, tr(STR_CHESS_NAV_BACK), tr(STR_CHESS_CUSTOM_TIME));
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(kText12Font);
  const int rowHeight = std::max(kListRowHeight, lineHeight + 2 * kRuleGap);
  const char* labels[] = {tr(STR_CHESS_MINUTES), tr(STR_CHESS_INCREMENT)};
  const int values[] = {kBaseMinutes[customBase], kIncrementSeconds[customIncrement]};
  const bool atMin[] = {customBase == 0, customIncrement == 0};
  const bool atMax[] = {customBase + 1 == static_cast<int>(std::size(kBaseMinutes)),
                        customIncrement + 1 == static_cast<int>(std::size(kIncrementSeconds))};
  // [−] value [+] at the right edge.
  const int plusX = width - kSidePadding - kStepperSize;
  const int valueX = plusX - kStepperSize;
  const int minusX = valueX - kStepperSize;
  for (int i = 0; i < kCustomDoneRow; ++i) {
    const int textTop = top + (rowHeight - lineHeight) / 2;
    const int stepTop = top + (rowHeight - kStepperSize) / 2;
    if (i == focus) drawSelectionBar(renderer, kBarX, textTop, lineHeight);
    renderer.drawText(kText12Font, kRowTextX, textTop, labels[i], true,
                      i == focus ? kSemiBold : EpdFontFamily::REGULAR);
    char value[8];
    snprintf(value, sizeof(value), "%d", values[i]);
    drawCentered(renderer, kText12Font, valueX, kStepperSize, textTop, value, kSemiBold);
    if (!atMin[i]) {
      drawCentered(renderer, kText12Font, minusX, kStepperSize, textTop, "\xE2\x88\x92", kSemiBold);
      screen.frame().hit(fui::makeRect(minusX, stepTop, kStepperSize, kStepperSize), ACTION_STEP,
                         static_cast<int16_t>(i * 2), fui::InputTouch);
    }
    if (!atMax[i]) {
      drawCentered(renderer, kText12Font, plusX, kStepperSize, textTop, "+", kSemiBold);
      screen.frame().hit(fui::makeRect(plusX, stepTop, kStepperSize, kStepperSize), ACTION_STEP,
                         static_cast<int16_t>(i * 2 + 1), fui::InputTouch);
    }
    top += rowHeight;
  }
  drawRule(renderer, 0, top, width);
  top += kRuleWidth;
  const int textTop = top + (rowHeight - lineHeight) / 2;
  const bool focused = focus == kCustomDoneRow;
  if (focused) drawSelectionBar(renderer, kBarX, textTop, lineHeight);
  renderer.drawText(kText12Font, kRowTextX, textTop, tr(STR_CHESS_DONE), true,
                    focused ? kSemiBold : EpdFontFamily::REGULAR);
  screen.frame().hit(fui::makeRect(0, top, width, rowHeight), ACTION_ROW, kCustomDoneRow, fui::InputTouch);
}

void ChessActivity::buildGame(UiScreen& screen) {
  drawTopLine(screen);
  drawBoard();
  screen.frame().hit(fui::makeRect(0, kBoardTop, kBoardSize, kBoardSize), ACTION_BOARD, 0, fui::InputTouch);
  if (promoFrom != kNoSquare) {
    drawPromotion(screen);
    return;
  }
  if (match->game().isOver()) {
    drawGameOver(screen);
  } else if (match->timed()) {
    drawClockPanel();
  } else {
    drawStatusPanel();
  }
  drawActions(screen);
}

void ChessActivity::drawTopLine(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  const char* exitLabel = tr(STR_CHESS_NAV_EXIT);
  renderer.drawText(kText12Font, kSidePadding, kHeaderTop, exitLabel);
  const int exitWidth = renderer.getTextWidth(kText12Font, exitLabel) + 2 * kSidePadding;
  screen.frame().hit(fui::makeRect(0, 0, exitWidth, kBoardTop), ACTION_BACK, 0, fui::InputTouch);

  // The far side of the board: the computer, or between friends the side not to move.
  const chess::Color far = chess::opposite(heroSide());
  char label[48] = "";
  if (match->settings().mode == chess::Mode::VsComputer) {
    snprintf(label, sizeof(label), tr(STR_CHESS_COMPUTER_LEVEL), match->settings().level);
  } else if (match->timed()) {
    snprintf(label, sizeof(label), "%s", far == chess::WHITE ? tr(STR_CHESS_WHITE) : tr(STR_CHESS_BLACK));
  }
  int right = width - kSidePadding;
  if (match->timed()) {
    char clock[12];
    formatClock(match->remainingMs(far), clock, sizeof(clock));
    drawRightAligned(renderer, kText12Font, right, kHeaderTop, clock, kSemiBold);
    right -= renderer.getTextWidth(kText12Font, clock, kSemiBold) + kTurnLineGap;
  }
  if (label[0] == '\0') return;
  const int x = std::max(exitWidth, right - renderer.getTextWidth(kText12Font, label));
  drawFitted(renderer, kText12Font, x, kHeaderTop, label, right - x);
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
      drawDashedFrame(renderer, x, y);
    }
  }
  if (selected != kNoSquare) {
    squareOrigin(selected, flip, x, y);
    drawCellFrame(renderer, x, y, kSquare, kSquare);
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

// Turn label on the left (with the bar when a person is to move), last move on the right.
void ChessActivity::drawTurnLine(const int top, const bool bar) const {
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(kText12Font);
  char last[32];
  lastMoveText(last, sizeof(last));
  int lastWidth = 0;
  if (last[0] != '\0') {
    drawRightAligned(renderer, kText12Font, width - kSidePadding, top, last);
    lastWidth = renderer.getTextWidth(kText12Font, last) + kTurnLineGap;
  }
  if (bar) drawSelectionBar(renderer, kBarX, top, lineHeight);
  drawFitted(renderer, kText12Font, kRowTextX, top, turnLabel(), width - kSidePadding - lastWidth - kRowTextX,
             kSemiBold);
}

void ChessActivity::drawClockPanel() const {
  const chess::Color hero = heroSide();
  char clock[12];
  formatClock(match->remainingMs(hero), clock, sizeof(clock));
  // Unbounded is wide: "45:00" does not fit at 96 px. Scale the face down just
  // enough, measured with the widest digit in every place so the size holds
  // while the time ticks.
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
  const int maxWidth = renderer.getScreenWidth() - kRowTextX - kSidePadding;
  const int scale256 = naturalWidth > maxWidth * 16 ? maxWidth * 16 * 256 / naturalWidth : 256;

  const int capTop = kPanelTop + 22;
  const int capHeight = heroCapHeight(renderer, kHeroFont) * scale256 / 256;
  const bool heroToMove = match->isHumanTurn() && match->game().sideToMove() == hero;
  if (heroToMove) drawSelectionBar(renderer, kBarX, capTop, capHeight);
  if (scale256 == 256) {
    drawHeroNumber(renderer, kHeroFont, kRowTextX, capTop, clock);
  } else {
    int x = kRowTextX;
    for (const char* c = clock; *c != '\0'; ++c) {
      x += renderer.drawScaledCodepoint(kHeroFont, static_cast<uint8_t>(*c), EpdFontFamily::REGULAR, x,
                                        capTop + capHeight, scale256);
    }
  }
  drawTurnLine(capTop + capHeight + 14, false);
}

void ChessActivity::drawStatusPanel() const {
  const int top = kPanelTop + 18;
  drawTurnLine(top, match->isHumanTurn());
  drawMoveList(top + renderer.getLineHeight(kText12Font) + 12, kActionsTop - 6);
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
    if (game.colorOfPly(ply) == chess::WHITE) chess::formatSan(game.ply(ply++).san, chess::NOTATION_RU, white, 24);
    if (ply < plies && game.colorOfPly(ply) == chess::BLACK && game.moveNumberOfPly(ply) == number) {
      chess::formatSan(game.ply(ply++).san, chess::NOTATION_RU, black, 24);
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

  const int maxWidth = renderer.getScreenWidth() - kRowTextX - kSidePadding;
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
    int lineX = kRowTextX;
    const int y = top + (line - firstLine) * lineHeight;
    for (int u = lineStart[line]; u < lineStart[line + 1]; ++u) {
      renderer.drawText(kText10Font, lineX, y, units[u].text);
      lineX += units[u].width + gap;
    }
  }
}

void ChessActivity::drawPromotion(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  const int top = kPanelTop + 18;
  renderer.drawText(kText12Font, kRowTextX, top, tr(STR_CHESS_PROMOTE), true, kSemiBold);
  const int cellTop = top + renderer.getLineHeight(kText12Font) + 8;
  const int cellWidth = width / static_cast<int>(std::size(kPromotions));
  const int cellHeight = kActionsTop - cellTop;
  const char* names[] = {tr(STR_CHESS_QUEEN), tr(STR_CHESS_ROOK), tr(STR_CHESS_BISHOP), tr(STR_CHESS_KNIGHT)};
  const auto color = pieceColor(match->game().sideToMove());
  for (int i = 0; i < static_cast<int>(std::size(kPromotions)); ++i) {
    const int x = i * cellWidth;
    const auto& bitmap =
        chess::ui::pieceBitmap(prefs.pieceSet, color, pieceKind(kPromotions[i]), chess::ui::PieceSize::Small);
    chess::ui::drawPiece(renderer, bitmap, x + (cellWidth - bitmap.size) / 2, cellTop + 8);
    drawCentered(renderer, kText10Font, x, cellWidth, cellTop + 16 + bitmap.size, names[i]);
    screen.frame().hit(fui::makeRect(x, cellTop, cellWidth, cellHeight), ACTION_PROMOTE, static_cast<int16_t>(i),
                       fui::InputTouch);
  }
}

void ChessActivity::drawGameOver(UiScreen& screen) const {
  const int lineHeight = renderer.getLineHeight(kText12Font);
  int top = kPanelTop + 18;
  renderer.drawText(kText12Font, kRowTextX, top, overTitle(match->game()), true, kSemiBold);
  top += lineHeight + 4;
  renderer.drawText(kText12Font, kRowTextX, top, overSubtitle(*match));
  top += lineHeight + 8;
  const int rowHeight = std::max(kListRowHeight, lineHeight + 2 * kRuleGap);
  const int textTop = top + (rowHeight - lineHeight) / 2;
  drawSelectionBar(renderer, kBarX, textTop, lineHeight);
  renderer.drawText(kText12Font, kRowTextX, textTop, tr(STR_CHESS_NEW_GAME), true, kSemiBold);
  screen.frame().hit(fui::makeRect(0, top, renderer.getScreenWidth(), rowHeight), ACTION_NEW_GAME, 0, fui::InputTouch);
}

void ChessActivity::drawActions(UiScreen& screen) const {
  const int width = renderer.getScreenWidth();
  drawRule(renderer, 0, kActionsTop, width);
  const int bandTop = kActionsTop + kRuleWidth;
  const int bandHeight = renderer.getScreenHeight() - bandTop;
  const int textTop = bandTop + (bandHeight - renderer.getLineHeight(kText12Font)) / 2;
  if (match->canUndo()) {
    renderer.drawText(kText12Font, kSidePadding, textTop, tr(STR_CHESS_UNDO));
    screen.frame().hit(fui::makeRect(0, bandTop, width / 2, bandHeight), ACTION_UNDO, 0, fui::InputTouch);
  }
  drawRightAligned(renderer, kText12Font, width - kSidePadding, textTop, tr(STR_CHESS_MENU));
  screen.frame().hit(fui::makeRect(width / 2, bandTop, width / 2, bandHeight), ACTION_MENU, 0, fui::InputTouch);
}

#endif  // FREEINK_DEVICE_X4PRO
