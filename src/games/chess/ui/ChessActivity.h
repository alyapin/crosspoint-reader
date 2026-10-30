#pragma once

#include <ChessMatch.h>
#include <Icon.h>
#include <OverGuard.h>

#include <atomic>
#include <memory>

#include "ChessPrefs.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"
#include "games/ui/BoardKeys.h"
#include "util/ButtonNavigator.h"

// Games list hook for the "Шахматы" row: chessListStatus() is the row's second
// line ("партия идёт" while a saved game exists) or nullptr, and openChess()
// builds the screen, which resumes the saved game or opens "Новая игра".
const char* chessListStatus();
std::unique_ptr<Activity> openChess(GfxRenderer& renderer, MappedInputManager& mappedInput);

// Chess in the Setka style. The setup list with its option pickers, the game
// screen (board, clocks or move list, promotion and game-over panels) and the
// game menu are views of one activity, so the Match (~150 KB, ~65 KB without
// PSRAM) is allocated once.
//
// The loop task alone mutates the Match and the view state, always under the
// RenderLock (engine slices take it with a try-lock, so a panel refresh never
// blocks the loop); the render task only reads them.
class ChessActivity final : public Activity, private UiAppHost {
 public:
  ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  void prepareForSleep() override;
  bool skipLoopDelay() override;
  uint32_t idleSleepBudgetMs() const override;

 private:
  enum class View : uint8_t { Setup, Picker, CustomTime, Game, Menu };
  enum class Row : uint8_t {
    Opponent,
    Level,
    Color,
    Time,
    Rotate,
    Pieces,
    Start,
    Resign,
    Draw,
    NewGame,
    Flip,
    Undo,
    Continue,
    Exit
  };
  // Pending repaint, strongest wins: Entry = HALF, Update = cadence FAST,
  // Clock = plain FAST that does not count toward the cadence.
  enum Repaint : uint8_t { RepaintNone, RepaintClock, RepaintUpdate, RepaintEntry };
  enum class GameAction : uint8_t { None, NewGame, Undo, Flip, Menu };
  static constexpr int kMaxActions = 4;
  static constexpr int kMaxRows = 8;
  static constexpr chess::Square kNoSquare = 0xFF;

  struct RowText {
    const char* label = "";
    const freeink::Icon* icon = nullptr;
    char value[24] = {};
    int8_t preview = -1;  // piece set drawn as a sample on the right
  };

  static void screenFn(UiScreen& screen, void* user);
  void build(UiScreen& screen);
  const char* listTitle() const;
  int listTop() const;
  void drawRow(UiScreen& screen, int top, int index, const freeink::Icon* icon, const char* label) const;
  void buildList(UiScreen& screen);
  void buildCustomTime(UiScreen& screen);
  void buildGame(UiScreen& screen);
  void drawBoard() const;
  void drawTopLine(UiScreen& screen) const;
  void drawClockPanel() const;
  void drawTurnLine(int top) const;
  void drawStatusPanel() const;
  void drawMoveList(int top, int bottom) const;
  void drawPromotion(UiScreen& screen) const;
  void drawGameOver() const;
  void drawActions(UiScreen& screen) const;

  int listRows(Row* out) const;
  int rowCount() const;
  RowText rowText(int index) const;
  const char* rowLabel(Row row) const;
  const freeink::Icon* rowIcon(Row row) const;
  void rowValue(Row row, char* out, size_t cap) const;
  int optionCount(Row row) const;
  const char* optionLabel(Row row, int option) const;
  int currentOption(Row row) const;

  void handleAction(const freeink::ui::ActionEvent& event, int x, int y);
  bool applyGameKeys();
  void gameKey(int dx, int dy, bool confirm);
  int gameActions(GameAction* out) const;
  GameAction focusedAction() const;
  void runAction(GameAction action);
  void activateRow(int index);
  void confirmResign();
  // Touch boards: "‹ Выйти" and the action bar; on button boards Back opens the menu, which holds the actions.
  bool hasBar() const;
  // Top of the action bar, above the button hints on button boards.
  int actionsTop() const;
  void applyOption(int option);
  void stepCustom(int field, int delta);
  void goBack();
  void setView(View next);
  void openPicker(Row row);
  void closePicker();
  void startGame();
  void tapSquare(chess::Square square);
  void playMove(chess::Square from, chess::Square to, chess::PieceType promo);
  void afterPly();
  void clearSelection();
  void syncShownClocks();
  void offerDraw();
  void tickClocks();
  void saveGame();
  void requestRepaint(Repaint kind);

  bool boardFlipped() const;
  chess::Color heroSide() const;
  int boardLeft() const;
  chess::Square squareAtCell(int cell) const;
  chess::Square squareAt(int x, int y) const;
  void squareOrigin(chess::Square square, bool flip, int& x, int& y) const;
  const char* turnLabel() const;
  void lastMoveText(char* out, size_t cap) const;

  std::unique_ptr<chess::Match> match;
  chess::ui::Prefs prefs;  // the setup screen edits prefs.match
  View view = View::Setup;
  bool inGame = false;  // a game was started or resumed: Setup's back returns to it
  bool manualFlip = false;
  int focus = 0;
  Row pickerRow = Row::Opponent;
  View pickerReturn = View::Setup;
  uint8_t customBase = 0;  // indices into the custom time steps
  uint8_t customIncrement = 0;
  chess::Square selected = kNoSquare;
  uint64_t targets = 0;
  chess::Square promoFrom = kNoSquare;
  chess::Square promoTo = kNoSquare;
  int promoFocus = 0;
  int cursor = -1;            // key focus in the game view: board cell (visual), then actions; -1 hidden
  bool drawDeclined = false;  // until the next move
  uint32_t shownSeconds[2] = {0, 0};
  bool savePending = false;

  ButtonNavigator buttonNavigator;
  games::ui::BoardKeys keys;      // the board cursor, loop task only
  games::OverGuard overGuard;     // loop task only
  setka::RefreshCadence cadence;  // render task only
  std::atomic<uint8_t> repaint{RepaintEntry};
};
