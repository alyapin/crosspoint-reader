#include "GameScreen.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <variant>

#include "ElapsedText.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {
constexpr int kCursorArm = 14;
}  // namespace

GameScreen::GameScreen(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity(name, renderer, mappedInput), UiAppHost(renderer) {}

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
      leave();
      return;
    }
  } else {
    int dx = 0;
    int dy = 0;
    if (mappedInput.wasPressed(Button::Left)) dx = -1;
    if (mappedInput.wasPressed(Button::Right)) dx = 1;
    if (mappedInput.wasPressed(Button::Up)) dy = -1;
    if (mappedInput.wasPressed(Button::Down)) dy = 1;
    const bool confirm = mappedInput.wasReleased(Button::Confirm);
    if (dx != 0 || dy != 0 || confirm) {
      RenderLock lock(*this);
      onKey(dx, dy, confirm);
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

void GameScreen::render(RenderLock&&) {
  renderer.clearScreen();
  renderUi();
  const uint8_t kind = repaint.exchange(RepaintNone);
  renderer.displayBuffer(kind == RepaintEntry   ? cadence.entry()
                         : kind == RepaintClock ? HalDisplay::FAST_REFRESH
                                                : cadence.update());
}

void GameScreen::screenFn(UiScreen& screen, void* user) {
  auto* self = static_cast<GameScreen*>(user);
  if (self->hasGame) self->build(screen);
}

void GameScreen::drawTopLine(UiScreen& screen, const char* label, const char* value, const bool tappable,
                             const bool focused) const {
  const int width = renderer.getScreenWidth();
  drawStatusLine(renderer);

  const char* exitLabel = tr(STR_GAMES_EXIT);
  renderer.drawText(kText12Font, kSidePadding, TOP_LINE_Y, exitLabel);
  const int exitWidth = renderer.getTextWidth(kText12Font, exitLabel) + 2 * kSidePadding;
  screen.frame().hit(fui::makeRect(0, 0, exitWidth, BOARD_TOP), ACTION_EXIT, 0, shellInput());

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

void GameScreen::drawBar(UiScreen& screen, const BarItem (&items)[BAR_SLOTS], const int focused) const {
  const int width = renderer.getScreenWidth();
  const int slot = width / BAR_SLOTS;
  const int lineHeight = renderer.getLineHeight(kText12Font);
  drawRule(renderer, 0, barTop(), width);
  const int top = barTop() + kRuleWidth;
  const int textTop = top + (BAR_HEIGHT - lineHeight) / 2;
  for (int i = 0; i < BAR_SLOTS; ++i) {
    const BarItem& item = items[i];
    if (item.label == nullptr) continue;
    const auto style = item.active ? kSemiBold : EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(kText12Font, item.label, style);
    // First slot left-aligned past the bar, last right-aligned, middle centred.
    const int x = i == 0 ? kRowTextX : i == BAR_SLOTS - 1 ? width - kSidePadding - textWidth : (width - textWidth) / 2;
    if (item.active) drawSelectionBar(renderer, x - kRowTextX + kSelectionBarX, textTop, lineHeight);
    renderer.drawText(kText12Font, x, textTop, item.label, true, style);
    if (i == focused) drawCellFrame(renderer, i * slot, top, slot, BAR_HEIGHT - kRuleWidth);
    screen.frame().hit(fui::makeRect(i * slot, top, slot, BAR_HEIGHT - kRuleWidth), ACTION_BAR, static_cast<int16_t>(i),
                       shellInput());
  }
}

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

void GameScreen::formatElapsed(const uint32_t ms, char* out, const size_t cap) {
  games::ui::formatElapsed(out, cap, ms, tr(STR_GAMES_MINUTES), tr(STR_GAMES_UNDER_MINUTE));
}

uint32_t GameScreen::nowMs() { return static_cast<uint32_t>(millis()); }

int GameScreen::barTop() const { return renderer.getScreenHeight() - BAR_HEIGHT; }

uint16_t GameScreen::shellInput() const {
  // A held tap on a board that takes long presses must still work on the shell controls.
  return static_cast<uint16_t>(wantsLongPress() ? fui::InputTouch | fui::InputLongPress : fui::InputTouch);
}

void GameScreen::fillTriangle(const GfxRenderer& renderer, const int x0, const int y0, const int x1, const int y1,
                              const int x2, const int y2) {
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
    if (left <= right) renderer.fillRect(left, y, right - left + 1, 1, true);
  }
}
