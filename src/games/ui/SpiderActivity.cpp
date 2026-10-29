#include "SpiderActivity.h"

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
#include "levelIcons.h"

namespace fui = freeink::ui;
using namespace setka;
using namespace spider;

namespace {
// Ten 44 px columns with 3 px gaps; the stock and the collected suits above them.
constexpr int kCardWidth = 44;
constexpr int kCardHeight = 64;
constexpr int kCardGap = 3;
constexpr int kTableauTop = GameScreen::BOARD_TOP + kCardHeight + 12;
constexpr int kTableauGap = 8;  // between the columns and the bar rule
constexpr int kDownStep = 8;
constexpr int kUpStep = 24;
constexpr int kPickFrame = 4;
constexpr int kStockFan = 3;  // per deal left in the stock
constexpr int kDoneFan = 8;   // per collected suit
constexpr int kResultGap = 40;
constexpr games::ui::CardStyle kCardStyle{kCardWidth, kCardHeight, kText10Font, 12, 2};
constexpr int kBarFocus = STOCK + 1;  // focus values past the stock are bar slots

int modeOf(const int suits) { return suits == 1 ? 0 : suits == 2 ? 1 : 2; }
}  // namespace

const char* spiderListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Spider) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openSpider(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SpiderActivity>(renderer, mappedInput);
}

SpiderActivity::SpiderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Spider", renderer, mappedInput) {}

bool SpiderActivity::restore() {
  gamesave::loadBest(gamesave::Kind::Spider, best);
  if (!gamesave::load(game)) return false;
  solvableChoice = game.solvableOnly() || game.suits() == 4;
  focus = mappedInput.hasTouch() ? kNone : STOCK;
  return true;
}

void SpiderActivity::saveGame() {
  gamesave::save(game);
  if (bestPending) {
    bestPending = false;
    gamesave::saveBest(gamesave::Kind::Spider, best);
  }
}

void SpiderActivity::clearSave() { gamesave::clear(gamesave::Kind::Spider); }

void SpiderActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_SPIDER_ONE_SUIT), &icon_level_1_24, tr(STR_SPIDER_EASY)},
      {tr(STR_SPIDER_TWO_SUITS), &icon_level_2_24, tr(STR_SPIDER_MEDIUM)},
      {tr(STR_SPIDER_FOUR_SUITS), &icon_level_3_24, tr(STR_SPIDER_HARD)},
      {tr(STR_KLONDIKE_SOLVABLE), &icon_circle_check_24},
  };
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), hasGame ? modeOf(game.suits()) : 1, 3,
             solvableChoice);
}

void SpiderActivity::pickerChosen(const int row, const bool toggle) {
  solvableChoice = toggle;
  game.newGame(esp_random(), row == 0 ? 1 : row == 1 ? 2 : 4, toggle);
  clearPick();
  newBest = false;
  focus = mappedInput.hasTouch() ? kNone : STOCK;
}

// --- input -------------------------------------------------------------------

void SpiderActivity::clearPick() {
  pickColumn = kNone;
  pickIndex = kNone;
  hintPlace = kNone;
  noMoves = false;
  stockBlocked = false;
}

void SpiderActivity::pick(const int column, const int index) {
  clearPick();
  if (index < 0 || game.runLength(column, index) == 0) return;
  pickColumn = column;
  pickIndex = index;
}

void SpiderActivity::afterMove() {
  clearPick();
  if (game.isWon()) {
    newBest = best.record(modeOf(game.suits()), game.moveCount());
    bestPending = bestPending || newBest;
    if (focus != kNone) focus = kBarFocus + BAR_SLOTS - 1;
    requestRepaint(RepaintEntry);
  }
  markChanged();
}

void SpiderActivity::dealStock() {
  clearPick();
  if (game.deal()) {
    afterMove();
    return;
  }
  // The stock waits while a column is empty.
  stockBlocked = game.board().stockCount > 0;
  requestRepaint(RepaintUpdate);
}

void SpiderActivity::activate(const int column, int index) {
  if (game.isWon()) return;
  const int before[] = {pickColumn, pickIndex, hintPlace, noMoves ? 1 : 0, stockBlocked ? 1 : 0};
  const Pile& p = game.column(column);
  const bool keys = index == kNone;
  if (keys) {
    // Keys pick the longest run; Confirm on the picked column tries a move, then shortens the run.
    index = p.count - 1;
    while (index > 0 && game.runLength(column, index - 1) > 0) --index;
  }
  if (pickColumn == kNone) {
    pick(column, index);
  } else if (column == pickColumn && (keys || index == pickIndex)) {
    const int count = p.count - pickIndex;
    const int to = game.autoTarget(pickColumn, count);
    if (to >= 0 && game.move(pickColumn, to, count)) {
      afterMove();
      return;
    }
    if (keys && pickIndex + 1 < p.count) {
      pick(column, pickIndex + 1);
    } else {
      clearPick();
    }
  } else if (game.move(pickColumn, column, game.column(pickColumn).count - pickIndex)) {
    afterMove();
    return;
  } else {
    pick(column, index);
  }
  const int after[] = {pickColumn, pickIndex, hintPlace, noMoves ? 1 : 0, stockBlocked ? 1 : 0};
  if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
}

void SpiderActivity::showHint() {
  Step step;
  clearPick();
  if (!game.hint(step)) {
    noMoves = true;
  } else if (step.from == STOCK) {
    hintPlace = STOCK;
  } else {
    pickColumn = step.from;
    pickIndex = game.column(step.from).count - step.count;
    hintPlace = step.to;
  }
  requestRepaint(RepaintUpdate);
}

void SpiderActivity::runBar(const int slot) {
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

void SpiderActivity::onTouch(const fui::ActionEvent& event, int, const int y) {
  if (event.action == ACTION_COLUMN) {
    activate(event.value, cardAt(event.value, y));
  } else if (event.action == ACTION_STOCK) {
    dealStock();
  } else if (event.action == ACTION_BAR) {
    runBar(event.value);
  }
}

bool SpiderActivity::barShown(const int slot) const {
  if (slot == 0) return game.canUndo();
  if (slot == 1) return !game.isWon();
  return slot == 2;
}

int SpiderActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

// Left/Right walk the stock and the ten columns, Up/Down reach the stock and the bar.
void SpiderActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before = focus;
  if (focus == kNone) {
    focus = STOCK;
  } else if (focus >= kBarFocus) {
    const int slot = focus - kBarFocus;
    if (confirm) {
      runBar(slot);
      if (focus >= kBarFocus && !barShown(focus - kBarFocus)) focus = kBarFocus + nextBarSlot(slot, 1);
    } else if (dx != 0) {
      focus = kBarFocus + nextBarSlot(slot, dx);
    } else if (dy < 0 && !game.isWon()) {
      focus = 0;
    }
  } else if (confirm) {
    if (focus == STOCK) {
      dealStock();
    } else {
      activate(focus, kNone);
    }
  } else if (dx != 0) {
    const int places = COLUMNS + 1;
    const int position = (focus == STOCK ? 0 : focus + 1) + dx;
    const int next = (position + places) % places;
    focus = next == 0 ? STOCK : next - 1;
  } else if (dy > 0) {
    focus = focus == STOCK ? 0 : kBarFocus + nextBarSlot(BAR_SLOTS - 1, 1);
  } else if (dy < 0 && focus != STOCK) {
    focus = STOCK;
  }
  if (focus != before) requestRepaint(RepaintUpdate);
}

bool SpiderActivity::onBack() {
  if (pickColumn != kNone || hintPlace != kNone || noMoves || stockBlocked) {
    clearPick();
  } else if (focus >= kBarFocus && !game.isWon()) {
    focus = 0;
  } else {
    return false;
  }
  requestRepaint(RepaintUpdate);
  return true;
}

// --- geometry ----------------------------------------------------------------

int SpiderActivity::columnX(const int column) const {
  constexpr int width = COLUMNS * kCardWidth + (COLUMNS - 1) * kCardGap;
  return (renderer.getScreenWidth() - width) / 2 + column * (kCardWidth + kCardGap);
}

int SpiderActivity::cardY(const int column, const int index) const {
  const Pile& p = game.column(column);
  int down = 0;
  while (down < p.count && !cards::isFaceUp(p.cards[down])) ++down;
  const int up = p.count - down;
  // Face-up cards close up when a long run would run past the bar.
  const int room = (barTop() - kTableauGap) - kTableauTop - kCardHeight - down * kDownStep;
  const int step = up > 1 ? std::min(kUpStep, room / (up - 1)) : kUpStep;
  return kTableauTop + std::min(index, down) * kDownStep + std::max(0, index - down) * step;
}

int SpiderActivity::cardAt(const int column, const int y) const {
  const Pile& p = game.column(column);
  for (int i = p.count - 1; i >= 0; --i) {
    if (y >= cardY(column, i)) return i;
  }
  return p.count - 1;
}

// --- render task -------------------------------------------------------------

void SpiderActivity::build(UiScreen& screen) {
  const bool won = game.isWon();
  char value[16];
  snprintf(value, sizeof(value), "%d", game.moveCount());
  const char* label = won            ? tr(STR_KLONDIKE_WON)
                      : noMoves      ? tr(STR_KLONDIKE_NO_MOVES)
                      : stockBlocked ? tr(STR_SPIDER_EMPTY_COLUMN)
                                     : tr(STR_KLONDIKE_MOVES);
  drawTopLine(screen, label, noMoves || stockBlocked ? "" : value, false);

  drawTop();
  if (game.board().stockCount > 0) {
    screen.frame().hit(fui::makeRect(columnX(0), BOARD_TOP, 2 * kCardWidth, kCardHeight), ACTION_STOCK, 0,
                       fui::InputTouch);
  }
  for (int column = 0; column < COLUMNS; ++column) {
    drawColumn(column);
    screen.frame().hit(fui::makeRect(columnX(column), kTableauTop, kCardWidth, (barTop() - kTableauGap) - kTableauTop),
                       ACTION_COLUMN, static_cast<int16_t>(column), fui::InputTouch);
  }

  if (pickColumn != kNone) {
    const int top = cardY(pickColumn, pickIndex);
    const int bottom = cardY(pickColumn, game.column(pickColumn).count - 1) + kCardHeight;
    renderer.drawRect(columnX(pickColumn), top, kCardWidth, bottom - top, kPickFrame, true);
  }
  if (hintPlace != kNone) {
    const int pad = 3;
    const int top = hintPlace == STOCK ? BOARD_TOP : cardY(hintPlace, std::max(0, game.column(hintPlace).count - 1));
    games::ui::drawDashedRect(renderer, columnX(hintPlace == STOCK ? 0 : hintPlace) - pad, top - pad,
                              kCardWidth + 2 * pad, kCardHeight + 2 * pad);
  }
  if (focus != kNone && focus < kBarFocus) {
    const int count = focus == STOCK ? 0 : game.column(focus).count;
    drawCursor(columnX(focus == STOCK ? 0 : focus), focus == STOCK ? BOARD_TOP : cardY(focus, std::max(0, count - 1)),
               kCardWidth, kCardHeight);
  }
  if (won) {
    char line[48];
    if (newBest) {
      snprintf(line, sizeof(line), "%s", tr(STR_FIFTEEN_NEW_BEST));
    } else {
      snprintf(line, sizeof(line), "%s %u", tr(STR_2048_BEST), static_cast<unsigned>(best.moves[modeOf(game.suits())]));
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

// The stock fans one back per deal left, with the count beside it; collected
// suits stack up at the right as their kings.
void SpiderActivity::drawTop() const {
  const Board& b = game.board();
  const int deals = b.stockCount / COLUMNS;
  const int x = columnX(0);
  for (int i = 0; i < deals; ++i) games::ui::drawCard(renderer, kCardStyle, x + i * kStockFan, BOARD_TOP, 0, 0, false);
  if (deals > 0) {
    char text[8];
    snprintf(text, sizeof(text), "\xC3\x97%d", deals);
    renderer.drawText(kText10Font, x + (deals - 1) * kStockFan + kCardWidth + 6,
                      BOARD_TOP + (kCardHeight - renderer.getLineHeight(kText10Font)) / 2, text);
  }
  const int right = columnX(COLUMNS - 1);
  for (int i = 0; i < b.doneCount; ++i) {
    games::ui::drawCard(renderer, kCardStyle, right - (b.doneCount - 1 - i) * kDoneFan, BOARD_TOP, cards::RANKS - 1,
                        b.done[i], true);
  }
}

void SpiderActivity::drawColumn(const int column) const {
  const Pile& p = game.column(column);
  const int x = columnX(column);
  if (p.count == 0) {
    games::ui::drawDashedRect(renderer, x, kTableauTop, kCardWidth, kCardHeight);
    return;
  }
  for (int i = 0; i < p.count; ++i) {
    games::ui::drawCard(renderer, kCardStyle, x, cardY(column, i), cards::rankOf(p.cards[i]), cards::suitOf(p.cards[i]),
                        cards::isFaceUp(p.cards[i]));
  }
}
