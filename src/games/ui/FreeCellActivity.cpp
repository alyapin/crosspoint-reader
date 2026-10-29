#include "FreeCellActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "CardRenderer.h"
#include "gameIcons.h"
#include "games/GameSave.h"
#include "games/chess/ui/chessIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using namespace freecell;

namespace {
// Eight 54 px slots with 4 px gaps: cells and foundations on top, columns below.
constexpr int kCardWidth = 54;
constexpr int kCardHeight = 78;
constexpr int kCardGap = 4;
constexpr int kTableauTop = GameScreen::BOARD_TOP + kCardHeight + 14;
constexpr int kTableauGap = 8;  // between the columns and the bar rule
constexpr int kStep = 30;
constexpr int kPickFrame = 4;
constexpr int kResultGap = 40;
constexpr games::ui::CardStyle kCardStyle{kCardWidth, kCardHeight, kText12Font, 14, 4};
constexpr int kBarFocus = PLACES;  // focus values past the places are bar slots

int slotX(const int screenWidth, const int slot) {
  constexpr int width = COLUMNS * kCardWidth + (COLUMNS - 1) * kCardGap;
  return (screenWidth - width) / 2 + slot * (kCardWidth + kCardGap);
}
}  // namespace

const char* freecellListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::FreeCell) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openFreeCell(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<FreeCellActivity>(renderer, mappedInput);
}

FreeCellActivity::FreeCellActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("FreeCell", renderer, mappedInput) {}

// Without a save a new deal starts right away.
bool FreeCellActivity::restore() {
  gamesave::loadBest(gamesave::Kind::FreeCell, best);
  if (gamesave::load(game)) {
    focus = mappedInput.hasTouch() ? kNone : COLUMN;
  } else {
    startGame();
  }
  return true;
}

void FreeCellActivity::saveGame() {
  gamesave::save(game);
  if (bestPending) {
    bestPending = false;
    gamesave::saveBest(gamesave::Kind::FreeCell, best);
  }
}

void FreeCellActivity::clearSave() { gamesave::clear(gamesave::Kind::FreeCell); }

void FreeCellActivity::startGame() {
  uint32_t deal = esp_random() % (DEALS - 1) + 1;
  if (deal >= UNSOLVABLE_DEAL) ++deal;
  game.newGame(deal);
  clearPick();
  newBest = false;
  focus = mappedInput.hasTouch() ? kNone : COLUMN;
}

void FreeCellActivity::openNewGame() {
  startGame();
  markChanged();
  requestRepaint(RepaintEntry);
}

// --- input -------------------------------------------------------------------

void FreeCellActivity::clearPick() {
  pickPlace = kNone;
  pickIndex = kNone;
  hintPlace = kNone;
  noMoves = false;
}

void FreeCellActivity::pick(const int place, const int index) {
  clearPick();
  if (index < 0 || game.runLength(place, index) == 0) return;
  pickPlace = place;
  pickIndex = index;
}

void FreeCellActivity::afterMove() {
  clearPick();
  if (game.isWon()) {
    newBest = best.record(0, game.moveCount());
    bestPending = bestPending || newBest;
    if (focus != kNone) focus = kBarFocus + BAR_SLOTS - 1;
    requestRepaint(RepaintEntry);
  }
  markChanged();
}

void FreeCellActivity::activate(const int place, int index) {
  if (game.isWon()) return;
  const int before[] = {pickPlace, pickIndex, hintPlace, noMoves ? 1 : 0};
  const Pile& p = game.pile(place);
  const bool keys = index == kNone;
  if (keys) {
    // Keys pick the longest run; Confirm on the picked place tries a move, then shortens the run.
    index = p.count - 1;
    while (index > 0 && game.runLength(place, index - 1) > 0) --index;
  }
  if (pickPlace == kNone) {
    pick(place, index);
  } else if (place == pickPlace && (keys || index == pickIndex)) {
    const int count = p.count - pickIndex;
    const int to = game.autoTarget(pickPlace, count);
    if (to >= 0 && game.move(pickPlace, to, count)) {
      afterMove();
      return;
    }
    if (keys && pickIndex + 1 < p.count) {
      pick(place, pickIndex + 1);
    } else {
      clearPick();
    }
  } else if (game.move(pickPlace, place, game.pile(pickPlace).count - pickIndex)) {
    afterMove();
    return;
  } else {
    pick(place, index);
  }
  const int after[] = {pickPlace, pickIndex, hintPlace, noMoves ? 1 : 0};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

void FreeCellActivity::showHint() {
  Step step;
  clearPick();
  if (game.hint(step)) {
    pickPlace = step.from;
    pickIndex = game.pile(step.from).count - step.count;
    hintPlace = step.to;
  } else {
    noMoves = true;
  }
  requestRepaint(RepaintUpdate);
}

void FreeCellActivity::runBar(const int slot) {
  if (!barShown(slot)) return;
  if (slot == 0 && game.undo()) {
    clearPick();
    markChanged();
  } else if (slot == 1) {
    showHint();
  } else if (slot == 2) {
    if (game.isWon()) {
      openNewGame();
    } else {
      openGameMenu();
    }
  }
}

void FreeCellActivity::onTouch(const fui::ActionEvent& event, int, const int y) {
  if (event.action == ACTION_PLACE) {
    activate(event.value, cardAt(event.value, y));
  } else if (event.action == ACTION_BAR) {
    runBar(event.value);
  }
}

bool FreeCellActivity::barShown(const int slot) const {
  if (slot == 0) return game.canUndo();
  if (slot == 1) return !game.isWon();
  return slot == 2;
}

int FreeCellActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

// Left/Right walk all sixteen places, Up/Down switch rows and reach the bar.
void FreeCellActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before = focus;
  if (focus == kNone) {
    focus = COLUMN;
  } else if (focus >= kBarFocus) {
    const int slot = focus - kBarFocus;
    if (confirm) {
      runBar(slot);
      if (focus >= kBarFocus && !barShown(focus - kBarFocus)) focus = kBarFocus + nextBarSlot(slot, 1);
    } else if (dx != 0) {
      focus = kBarFocus + nextBarSlot(slot, dx);
    } else if (dy < 0 && !game.isWon()) {
      focus = COLUMN;
    }
  } else if (confirm) {
    activate(focus, kNone);
  } else if (dx != 0) {
    focus = (focus + dx + PLACES) % PLACES;
  } else if (dy > 0) {
    focus = isColumn(focus) ? kBarFocus + nextBarSlot(BAR_SLOTS - 1, 1) : focus + COLUMN;
  } else if (dy < 0 && isColumn(focus)) {
    focus -= COLUMN;
  }
  if (focus != before) requestRepaint(RepaintUpdate);
}

bool FreeCellActivity::onBack() {
  if (pickPlace != kNone || hintPlace != kNone || noMoves) {
    clearPick();
  } else if (focus >= kBarFocus && !game.isWon()) {
    focus = COLUMN;
  } else {
    return false;
  }
  requestRepaint(RepaintUpdate);
  return true;
}

// --- geometry ----------------------------------------------------------------

int FreeCellActivity::placeX(const int place) const {
  return slotX(renderer.getScreenWidth(), isColumn(place) ? place - COLUMN : place);
}

int FreeCellActivity::cardY(const int place, const int index) const {
  if (!isColumn(place)) return BOARD_TOP;
  const int count = game.pile(place).count;
  // Long columns close up before they reach the bar.
  const int room = (barTop() - kTableauGap) - kTableauTop - kCardHeight;
  const int step = count > 1 ? std::min(kStep, room / (count - 1)) : kStep;
  return kTableauTop + index * step;
}

int FreeCellActivity::cardAt(const int place, const int y) const {
  const Pile& p = game.pile(place);
  if (!isColumn(place)) return p.count - 1;
  for (int i = p.count - 1; i >= 0; --i) {
    if (y >= cardY(place, i)) return i;
  }
  return p.count - 1;
}

// --- render task -------------------------------------------------------------

void FreeCellActivity::build(UiScreen& screen) {
  const bool won = game.isWon();
  char value[16];
  snprintf(value, sizeof(value), "%d", game.moveCount());
  const char* label = won ? tr(STR_KLONDIKE_WON) : noMoves ? tr(STR_KLONDIKE_NO_MOVES) : tr(STR_KLONDIKE_MOVES);
  drawTopLine(screen, label, noMoves ? "" : value, false);

  for (int place = 0; place < PLACES; ++place) {
    drawPlace(place);
    const int top = isColumn(place) ? kTableauTop : BOARD_TOP;
    const int height = isColumn(place) ? (barTop() - kTableauGap) - kTableauTop : kCardHeight;
    screen.frame().hit(fui::makeRect(placeX(place), top, kCardWidth, height), ACTION_PLACE, static_cast<int16_t>(place),
                       fui::InputTouch);
  }

  if (pickPlace != kNone) {
    const int top = cardY(pickPlace, pickIndex);
    const int bottom = cardY(pickPlace, game.pile(pickPlace).count - 1) + kCardHeight;
    renderer.drawRect(placeX(pickPlace), top, kCardWidth, bottom - top, kPickFrame, true);
  }
  if (hintPlace != kNone) {
    const int pad = 3;
    const int top = cardY(hintPlace, std::max(0, game.pile(hintPlace).count - 1));
    games::ui::drawDashedRect(renderer, placeX(hintPlace) - pad, top - pad, kCardWidth + 2 * pad,
                              kCardHeight + 2 * pad);
  }
  if (focus != kNone && focus < kBarFocus) {
    const int count = game.pile(focus).count;
    drawCursor(placeX(focus), isColumn(focus) && count > 0 ? cardY(focus, count - 1) : cardY(focus, 0), kCardWidth,
               kCardHeight);
  }
  if (won) {
    char line[48];
    if (newBest) {
      snprintf(line, sizeof(line), "%s", tr(STR_FIFTEEN_NEW_BEST));
    } else {
      snprintf(line, sizeof(line), "%s %u", tr(STR_2048_BEST), static_cast<unsigned>(best.moves[0]));
    }
    drawCentered(kText12Font, 0, renderer.getScreenWidth(), kTableauTop + kResultGap, line, kSemiBold);
  }

  BarItem items[BAR_SLOTS];
  const char* labels[BAR_SLOTS] = {tr(STR_GAMES_UNDO), tr(STR_KLONDIKE_HINT),
                                   won ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU)};
  const freeink::Icon* icons[BAR_SLOTS] = {&icon_bar_undo_24, &icon_bar_hint_24,
                                           won ? &icon_bar_new_game_24 : &icon_menu_24};
  for (int i = 0; i < BAR_SLOTS; ++i) {
    if (barShown(i)) items[i] = {icons[i], labels[i], false};
  }
  drawBar(screen, items, focus >= kBarFocus ? focus - kBarFocus : -1);
}

void FreeCellActivity::drawPlace(const int place) const {
  const Pile& p = game.pile(place);
  const int x = placeX(place);
  if (p.count == 0) {
    const int y = isColumn(place) ? kTableauTop : BOARD_TOP;
    games::ui::drawDashedRect(renderer, x, y, kCardWidth, kCardHeight);
    if (isFoundation(place)) {
      drawCentered(kText12Font, x, kCardWidth, y + (kCardHeight - renderer.getLineHeight(kText12Font)) / 2, "A");
    }
    return;
  }
  const int first = isColumn(place) ? 0 : p.count - 1;
  for (int i = first; i < p.count; ++i) {
    games::ui::drawCard(renderer, kCardStyle, x, cardY(place, i), cards::rankOf(p.cards[i]), cards::suitOf(p.cards[i]),
                        true);
  }
}
