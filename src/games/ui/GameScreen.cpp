#include "GameScreen.h"

#include <Arduino.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <iterator>
#include <variant>

#include "ElapsedText.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "gameIcons.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
constexpr int kCursorArm = 14;
constexpr int kKeyCursorOut = 3;
constexpr unsigned long kConfirmHoldMs = 450;
constexpr int kModeIcon = 24;
constexpr int kModeIconGap = 8;
// Menu rows other than a bar slot (0..BAR_SLOTS-1); a game's own row is kRowOwn + its index.
enum MenuRow : int8_t { kRowContinue = -1, kRowNewGame = -2, kRowFinish = -3, kRowLeave = -4, kRowOwn = 16 };
}  // namespace

GameScreen::GameScreen(const char* name, const StrId title, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer), title(title) {}

void GameScreen::onEnter() {
  Activity::onEnter();
  resetUi();
  app.setScreen(&GameScreen::screenFn, this);
  {
    RenderLock lock(*this);
    hasGame = restore();
    if (hasGame) resumeGame();
    clockShown = shownMinutes();
  }
  if (!hasGame) openNewGame();
  requestRepaint(RepaintEntry);
}

void GameScreen::onExit() {
  if (hasGame) {
    pauseGame();
    saveGame();
  }
  Activity::onExit();
}

void GameScreen::prepareForSleep() {
  if (!hasGame) return;
  pauseGame();
  saveGame();
}

void GameScreen::leave() {
  app.clearTapFlash();
  finish();
}

void GameScreen::openPicker(const char* title, const GamePickerActivity::Row* rows, const int count, const int selected,
                            const int toggleRow, const bool toggleOn) {
  auto picker =
      makeUniqueNoThrow<GamePickerActivity>(renderer, mappedInput, title, rows, count, selected, toggleRow, toggleOn);
  if (!picker) {
    LOG_ERR("GAMES", "OOM: %u bytes for the picker", static_cast<unsigned>(sizeof(GamePickerActivity)));
    if (!hasGame) finish();
    return;
  }
  app.clearTapFlash();
  if (hasGame) pauseGame();
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    RenderLock lock(*this);
    const auto* choice = std::get_if<MenuResult>(&result.data);
    if (!result.isCancelled && choice != nullptr) {
      pickerChosen(choice->action, choice->pageTurnOption != 0);
      hasGame = true;
      savePending = true;
    } else if (!hasGame) {
      finish();
      return;
    }
    resumeGame();
    clockShown = shownMinutes();
    requestRepaint(RepaintEntry);
  });
}

void GameScreen::openGameMenu() {
  // One bar slot is always the menu's own.
  static_assert(GamePickerActivity::MAX_ROWS >= 3 + MAX_BAR_SLOTS + MENU_ROWS, "every menu row fits");
  GamePickerActivity::Row rows[GamePickerActivity::MAX_ROWS];
  int count = 0;
  const bool keys = !hasBar();
  const bool over = newGameSlot() >= 0;
  if (keys && !over) addMenuRow(rows, count, kRowContinue, {tr(STR_GAMES_CONTINUE), &icon_start_24});
  for (int i = 0; keys && i < shownCount; ++i) {
    const BarItem& item = shownBar[i];
    if (item.label == nullptr || item.kind == BarItem::Menu || item.kind == BarItem::NewGame) continue;
    const char* status = item.kind != BarItem::Mode ? nullptr : item.active ? tr(STR_GAMES_ON) : tr(STR_GAMES_OFF);
    addMenuRow(rows, count, static_cast<int8_t>(i), {item.label, item.icon, status});
  }
  addOwnRows(rows, count);
  addMenuRow(rows, count, kRowNewGame, {tr(STR_GAMES_NEW_GAME), &icon_bar_new_game_24});
  if (!over) {
    addMenuRow(rows, count, kRowFinish, {resigns() ? tr(STR_CHESS_RESIGN) : tr(STR_GAMES_FINISH), &icon_finish_24});
  }
  if (keys) addMenuRow(rows, count, kRowLeave, {tr(STR_GAMES_LEAVE), &icon_exit_24});
  showMenu(I18N.get(title), rows, count);
}

void GameScreen::addMenuRow(GamePickerActivity::Row* rows, int& count, const int8_t id,
                            const GamePickerActivity::Row& row) {
  if (count >= GamePickerActivity::MAX_ROWS) return;
  menuIds[count] = id;
  rows[count++] = row;
}

void GameScreen::addOwnRows(GamePickerActivity::Row* rows, int& count) {
  GamePickerActivity::Row own[MENU_ROWS];
  const int ownCount = std::clamp(menuRows(own), 0, MENU_ROWS);
  for (int i = 0; i < ownCount; ++i) addMenuRow(rows, count, static_cast<int8_t>(kRowOwn + i), own[i]);
}

void GameScreen::showMenu(const char* title, const GamePickerActivity::Row* rows, const int count) {
  // Button boards reopen on the last game action chosen here (another undo is one Confirm), else the first row.
  const int last = static_cast<int>(std::find(menuIds, menuIds + count, lastMenuRow) - menuIds);
  auto menu = makeUniqueNoThrow<GamePickerActivity>(renderer, mappedInput, title, rows, count,
                                                    !hasBar() && last < count ? last : 0);
  if (!menu) {
    LOG_ERR("GAMES", "OOM: %u bytes for the menu", static_cast<unsigned>(sizeof(GamePickerActivity)));
    return;
  }
  app.clearTapFlash();
  pauseGame();
  startActivityForResult(std::move(menu), [this, count](const ActivityResult& result) {
    RenderLock lock(*this);
    const auto* choice = std::get_if<MenuResult>(&result.data);
    const bool chosen = !result.isCancelled && choice != nullptr && choice->action >= 0 && choice->action < count;
    const int row = chosen ? menuIds[choice->action] : kRowContinue;
    lastMenuRow = static_cast<int8_t>(row >= 0 ? row : kRowContinue);
    if (row == kRowFinish) {
      confirmFinish();
      return;
    }
    resumeGame();
    if (row == kRowLeave) {
      leave();
      return;
    }
    clockShown = shownMinutes();
    requestRepaint(RepaintEntry);
    if (row == kRowNewGame) {
      openNewGame();
    } else if (row >= kRowOwn) {
      menuChosen(row - kRowOwn);
    } else if (row >= 0) {
      runBarSlot(row);
    }
  });
}

void GameScreen::runBarSlot(const int slot) {
  fui::ActionEvent event;
  event.action = ACTION_BAR;
  event.value = static_cast<int16_t>(slot);
  onTouch(event, 0, 0);
}

int GameScreen::newGameSlot() const {
  for (int i = 0; i < shownCount; ++i) {
    if (shownBar[i].label != nullptr && shownBar[i].kind == BarItem::NewGame) return i;
  }
  return -1;
}

void GameScreen::confirmFinish() {
  const bool concede = resigns();
  auto confirm = makeUniqueNoThrow<ConfirmationActivity>(
      renderer, mappedInput, concede ? tr(STR_CHESS_RESIGN_CONFIRM) : tr(STR_GAMES_FINISH_CONFIRM),
      concede ? tr(STR_CHESS_RESIGN_BODY) : tr(STR_GAMES_FINISH_BODY), tr(STR_CANCEL),
      concede ? tr(STR_CHESS_RESIGN) : tr(STR_GAMES_FINISH));
  if (!confirm) {
    LOG_ERR("GAMES", "OOM: finish confirmation");
    resumeGame();
    requestRepaint(RepaintEntry);
    return;
  }
  startActivityForResult(std::move(confirm), [this, concede](const ActivityResult& result) {
    RenderLock lock(*this);
    if (result.isCancelled || concede) {
      resumeGame();
      if (!result.isCancelled) resign();
      clockShown = shownMinutes();
      requestRepaint(RepaintEntry);
      return;
    }
    hasGame = false;
    savePending = false;
    clearSave();
    leave();
  });
}

void GameScreen::requestRepaint(const Repaint kind) {
  uint8_t current = repaint.load();
  while (current < kind && !repaint.compare_exchange_weak(current, kind)) {
  }
  requestUpdate();
}

// --- loop task ---------------------------------------------------------------

void GameScreen::loop() {
  if (!hasGame) return;
  using Button = MappedInputManager::Button;
  if (const auto touch = routeTouch(mappedInput, wantsLongPress())) {
    RenderLock lock(*this);
    if (touch.event.action == ACTION_EXIT) {
      leave();
      return;
    }
    onTouch(touch.event, touch.snap.touchX, touch.snap.touchY);
  } else if (mappedInput.wasReleased(Button::Back)) {
    RenderLock lock(*this);
    if (!onBack()) {
      if (hasBar()) {
        leave();
      } else {
        openGameMenu();
      }
      return;
    }
  } else if (holdsConfirm() && mappedInput.wasLongPressed(Button::Confirm, kConfirmHoldMs)) {
    RenderLock lock(*this);
    onConfirmHold();
  } else {
    int dx = 0;
    int dy = 0;
    keyRepeat.onPressAndContinuous({Button::Left}, [&dx] { dx = -1; });
    keyRepeat.onPressAndContinuous({Button::Right}, [&dx] { dx = 1; });
    keyRepeat.onPressAndContinuous({Button::Up}, [&dy] { dy = -1; });
    keyRepeat.onPressAndContinuous({Button::Down}, [&dy] { dy = 1; });
    const bool confirm = mappedInput.wasReleased(Button::Confirm);
    if (dx != 0 || dy != 0 || confirm) {
      RenderLock lock(*this);
      const int over = confirm && !hasBar() ? newGameSlot() : -1;
      if (over >= 0) {
        runBarSlot(over);
      } else {
        onKey(dx, dy, confirm);
      }
    }
  }

  // The timer repaints when its minute turns; a change in the same pass merges into one refresh.
  const int32_t minutes = shownMinutes();
  if (games::ui::timerStale(clockShown, minutes)) {
    clockShown = minutes;
    requestRepaint(RepaintClock);
  }
  if (savePending && hasGame) {
    savePending = false;
    saveGame();
  }
}

// --- render task -------------------------------------------------------------

void GameScreen::render(RenderLock&& lock) {
  renderer.clearScreen();
  renderUi();
  const bool over = newGameSlot() >= 0;
  const char* back = over ? nullptr : backHint();
  const char* confirm = over ? nullptr : confirmHint();
  const auto labels =
      mappedInput.mapLabels(back != nullptr ? back : tr(STR_GAMES_MENU), confirm != nullptr ? confirm : tr(STR_SELECT),
                            tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const uint8_t kind = repaint.exchange(RepaintNone);
  const HalDisplay::RefreshMode mode = kind == RepaintEntry   ? cadence.entry()
                                       : kind == RepaintClock ? HalDisplay::FAST_REFRESH
                                                              : cadence.update();
  // Keys pressed during the panel refresh still move the game; the next render shows where they ended.
  lock.unlock();
  renderer.displayBuffer(mode);
}

void GameScreen::screenFn(UiScreen& screen, void* user) {
  auto* self = static_cast<GameScreen*>(user);
  if (self->hasGame) self->build(screen);
}

void GameScreen::drawTopLine(UiScreen& screen, const char* label, const char* value, const bool tappable,
                             const bool focused) const {
  const int width = renderer.getScreenWidth();
  drawStatusLine(renderer);

  int exitWidth = 0;
  if (hasBar()) {
    const char* exitLabel = tr(STR_GAMES_EXIT);
    renderer.drawText(kText12Font, kSidePadding, TOP_LINE_Y, exitLabel);
    exitWidth = renderer.getTextWidth(kText12Font, exitLabel) + 2 * kSidePadding;
    screen.frame().hit(fui::makeRect(0, 0, exitWidth, BOARD_TOP), ACTION_EXIT, 0, shellInput());
  }

  int left = width - kSidePadding;
  if (value != nullptr && *value != '\0') {
    left -= renderer.getTextWidth(kText12Font, value, kSemiBold);
    renderer.drawText(kText12Font, left, TOP_LINE_Y, value, true, kSemiBold);
  }
  if (label != nullptr && *label != '\0') {
    left -= renderer.getTextWidth(kText12Font, label) + renderer.getSpaceWidth(kText12Font);
    renderer.drawText(kText12Font, left, TOP_LINE_Y, label);
  }
  if (focused) {
    constexpr int kPadX = 8;
    constexpr int kPadY = 3;  // clear of the clock/battery line above
    drawCellFrame(renderer, left - kPadX, TOP_LINE_Y - kPadY, width - kSidePadding - left + 2 * kPadX,
                  renderer.getLineHeight(kText12Font) + 2 * kPadY);
  }
  if (tappable) {
    const int x = std::max(exitWidth, left - kSidePadding);
    screen.frame().hit(fui::makeRect(x, 0, width - x, BOARD_TOP), ACTION_STATUS, 0, shellInput());
  }
}

void GameScreen::drawBar(UiScreen& screen, const BarItem* items, const int count, const int focused) const {
  shownCount = std::min(count, MAX_BAR_SLOTS);
  std::copy(items, items + shownCount, shownBar);
  if (!hasBar()) {
    drawMode(screen, items, shownCount);
    return;
  }
  games::ui::ActionSlot slots[MAX_BAR_SLOTS];
  for (int i = 0; i < shownCount; ++i) {
    slots[i] = {items[i].icon, items[i].label, items[i].active, ACTION_BAR, static_cast<int16_t>(i)};
  }
  games::ui::drawActionBar(renderer, screen, barTop(), slots, shownCount, focused, shellInput());
}

// Button boards have no bar: a mode that is on shows where touch boards have "‹ Выйти".
void GameScreen::drawMode(UiScreen& screen, const BarItem* items, const int count) const {
  for (int i = 0; i < count; ++i) {
    const BarItem& item = items[i];
    if (item.label == nullptr || item.kind != BarItem::Mode || !item.active) continue;
    int x = kSidePadding;
    if (item.icon != nullptr) {
      const int iconTop = TOP_LINE_Y + (renderer.getLineHeight(kText12Font) - kModeIcon) / 2;
      screen.target().bitmap(fui::makeRect(x, iconTop, kModeIcon, kModeIcon), fui::bitmapFromIcon(*item.icon),
                             fui::BitmapMode::Center);
      x += kModeIcon + kModeIconGap;
    }
    renderer.drawText(kText12Font, x, TOP_LINE_Y, item.label, true, kSemiBold);
    return;
  }
}

int GameScreen::drawResult(const int top, const char* result, const char* detail) const {
  return games::ui::drawResult(renderer, top, result, detail, hasBar() ? nullptr : tr(STR_GAMES_NEW_GAME_KEY));
}

int GameScreen::resultHeight(const bool detail) const { return games::ui::resultHeight(renderer, detail, !hasBar()); }

void GameScreen::drawCentered(const int font, const int x, const int width, const int y, const char* text,
                              const EpdFontFamily::Style style) const {
  renderer.drawText(font, x + (width - renderer.getTextWidth(font, text, style)) / 2, y, text, true, style);
}

void GameScreen::drawCursor(const int x, const int y, const int width, const int height) const {
  const int arm = std::min(kCursorArm, std::min(width, height) / 2);
  for (const int cx : {x, x + width - arm}) {
    for (const int cy : {y, y + height - kCellFrameWidth}) renderer.fillRect(cx, cy, arm, kCellFrameWidth, true);
  }
  for (const int cx : {x, x + width - kCellFrameWidth}) {
    for (const int cy : {y, y + height - arm}) renderer.fillRect(cx, cy, kCellFrameWidth, arm, true);
  }
}

void GameScreen::drawKeyCursor(const int x, const int y, const int width, const int height) const {
  drawCursor(x - kKeyCursorOut, y - kKeyCursorOut, width + 2 * kKeyCursorOut, height + 2 * kKeyCursorOut);
}

void GameScreen::formatElapsed(const uint32_t ms, char* out, const size_t cap) {
  games::ui::formatElapsed(out, cap, ms, tr(STR_GAMES_MINUTES), tr(STR_GAMES_UNDER_MINUTE));
}

uint32_t GameScreen::nowMs() { return static_cast<uint32_t>(millis()); }

bool GameScreen::hasBar() const { return UITheme::getInstance().getMetrics().buttonHintsHeight == 0; }

// Button boards have no bar: the board runs down to the button hints.
int GameScreen::barTop() const {
  return renderer.getScreenHeight() - (hasBar() ? BAR_HEIGHT : 0) -
         UITheme::getInstance().getMetrics().buttonHintsHeight;
}

uint16_t GameScreen::shellInput() const {
  // A held tap on a board that takes long presses must still work on the shell controls.
  return static_cast<uint16_t>(wantsLongPress() ? fui::InputTouch | fui::InputLongPress : fui::InputTouch);
}

void GameScreen::fillTriangle(const GfxRenderer& renderer, const int x0, const int y0, const int x1, const int y1,
                              const int x2, const int y2, const bool black) {
  const int xs[3] = {x0, x1, x2};
  const int ys[3] = {y0, y1, y2};
  const int top = std::min({y0, y1, y2});
  const int bottom = std::max({y0, y1, y2});
  for (int y = top; y <= bottom; ++y) {
    int left = INT_MAX;
    int right = INT_MIN;
    for (int i = 0; i < 3; ++i) {
      const int j = (i + 1) % 3;
      const int ya = ys[i];
      const int yb = ys[j];
      if (y < std::min(ya, yb) || y > std::max(ya, yb)) continue;
      const int x = ya == yb ? xs[i] : xs[i] + (xs[j] - xs[i]) * (y - ya) / (yb - ya);
      left = std::min({left, x, ya == yb ? xs[j] : x});
      right = std::max({right, x, ya == yb ? xs[j] : x});
    }
    if (left <= right) renderer.fillRect(left, y, right - left + 1, 1, black);
  }
}
