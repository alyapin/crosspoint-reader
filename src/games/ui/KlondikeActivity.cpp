#include "KlondikeActivity.h"

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
using namespace klondike;

namespace {
// Seven 62 px columns with 4 px gaps across the width; the top row holds the
// stock, the waste (fanned in draw-3), a gap and the foundations.
constexpr int kCardWidth = 62;
constexpr int kCardHeight = 88;
constexpr int kCardGap = 4;
constexpr int kFan = 18;
constexpr int kTableauTop = GameScreen::BOARD_TOP + kCardHeight + 14;
constexpr int kTableauGap = 8;  // between the columns and the bar rule
constexpr int kResultGap = 24;  // a won board: the top row to the result
constexpr int kDownStep = 10;
constexpr int kUpStep = 30;
constexpr int kPickFrame = 4;
constexpr games::ui::CardStyle kCardStyle{kCardWidth, kCardHeight, kText12Font, 16, 5};
constexpr int kBarFocus = PILES;  // focus values past the piles are bar slots
constexpr int kFirstFoundationColumn = 3;

int columnX(const int screenWidth, const int column) {
  constexpr int width = TABLEAU_PILES * kCardWidth + (TABLEAU_PILES - 1) * kCardGap;
  return (screenWidth - width) / 2 + column * (kCardWidth + kCardGap);
}

// The tableau column a top-row pile sits above, and back.
int columnBelow(const int pile) {
  return pile <= WASTE ? TABLEAU + pile : TABLEAU + kFirstFoundationColumn + pile - FOUNDATION;
}
int pileAbove(const int pile) {
  const int column = pile - TABLEAU;
  return column < kFirstFoundationColumn ? std::min(column, static_cast<int>(WASTE))
                                         : FOUNDATION + column - kFirstFoundationColumn;
}
}  // namespace

const char* klondikeListStatus() {
  return gamesave::hasGameInProgress(gamesave::Kind::Klondike) ? tr(STR_GAMES_IN_PROGRESS) : nullptr;
}

std::unique_ptr<Activity> openKlondike(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<KlondikeActivity>(renderer, mappedInput);
}

KlondikeActivity::KlondikeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : GameScreen("Klondike", StrId::STR_KLONDIKE, renderer, mappedInput), game(&GameScreen::nowMs) {}

bool KlondikeActivity::restore() {
  if (!gamesave::load(game)) return false;
  solvableChoice = game.solvableOnly();
  focus = mappedInput.hasTouch() ? kNone : STOCK;
  return true;
}

void KlondikeActivity::saveGame() { gamesave::save(game); }

void KlondikeActivity::clearSave() { gamesave::clear(gamesave::Kind::Klondike); }

void KlondikeActivity::openNewGame() {
  const GamePickerActivity::Row rows[] = {
      {tr(STR_KLONDIKE_DRAW_ONE), &icon_card_24},
      {tr(STR_KLONDIKE_DRAW_THREE), &icon_cards_24},
      {tr(STR_KLONDIKE_SOLVABLE), &icon_circle_check_24},
  };
  openPicker(tr(STR_GAMES_NEW_GAME), rows, static_cast<int>(std::size(rows)), hasGame && game.drawThree() ? 1 : 0, 2,
             solvableChoice);
}

void KlondikeActivity::pickerChosen(const int row, const bool toggle) {
  solvableChoice = toggle;
  game.newGame(esp_random(), row == 1, toggle);
  clearPick();
  focus = mappedInput.hasTouch() ? kNone : STOCK;
}

// --- input -------------------------------------------------------------------

void KlondikeActivity::clearPick() {
  pickPile = kNone;
  pickIndex = kNone;
  hintPile = kNone;
  noMoves = false;
}

void KlondikeActivity::pick(const int pile, const int index) {
  clearPick();
  if (pile == STOCK || index < 0 || game.runLength(pile, index) == 0) return;
  pickPile = pile;
  pickIndex = index;
}

void KlondikeActivity::afterMove() {
  clearPick();
  if (game.canAutoFinish()) game.autoFinish();
  markChanged();
  if (game.isWon()) requestRepaint(RepaintEntry);
}

void KlondikeActivity::activate(const int pile, int index) {
  if (game.isWon()) return;
  const int before[] = {pickPile, pickIndex, hintPile, noMoves ? 1 : 0};
  auto repaintIfChanged = [&] {
    const int after[] = {pickPile, pickIndex, hintPile, noMoves ? 1 : 0};
    if (!std::equal(std::begin(before), std::end(before), std::begin(after))) requestRepaint(RepaintUpdate);
  };
  if (pile == STOCK) {
    clearPick();
    if (game.draw()) {
      afterMove();
    } else {
      repaintIfChanged();
    }
    return;
  }
  const Pile& p = game.pile(pile);
  const bool keys = index == kNone;
  if (keys) {
    // Keys pick the whole face-up run; Confirm on the picked pile tries a move, then shortens the run.
    index = p.count - 1;
    while (index > 0 && game.runLength(pile, index - 1) > 0) --index;
  }
  if (pickPile == kNone) {
    pick(pile, index);
  } else if (pile == pickPile && (keys || index == pickIndex)) {
    const int count = p.count - pickIndex;
    const int to = game.autoTarget(pickPile, count);
    if (to >= 0 && game.move(pickPile, to, count)) {
      afterMove();
      return;
    }
    if (keys && pickIndex + 1 < p.count) {
      const int shorter = pickIndex + 1;
      pick(pile, shorter);
    } else {
      clearPick();
    }
  } else if (game.move(pickPile, pile, game.pile(pickPile).count - pickIndex)) {
    afterMove();
    return;
  } else {
    pick(pile, index);
  }
  repaintIfChanged();
}

void KlondikeActivity::showHint() {
  Move m;
  clearPick();
  if (!game.hint(m)) {
    noMoves = true;
  } else if (m.flags == Move::DRAW) {
    hintPile = STOCK;
  } else {
    pickPile = m.from;
    pickIndex = game.pile(m.from).count - m.count;
    hintPile = m.to;
  }
  requestRepaint(RepaintUpdate);
}

void KlondikeActivity::runBar(const int slot) {
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

void KlondikeActivity::onTouch(const fui::ActionEvent& event, int, const int y) {
  if (event.action == ACTION_PILE) {
    activate(event.value, cardAt(event.value, y));
  } else if (event.action == ACTION_BAR) {
    runBar(event.value);
  }
}

bool KlondikeActivity::barShown(const int slot) const {
  if (slot == 0) return game.canUndo();
  if (slot == 1) return !game.isWon();
  return slot == 2;
}

int KlondikeActivity::nextBarSlot(const int from, const int step) const {
  for (int i = 1; i <= BAR_SLOTS; ++i) {
    const int slot = (from + step * i + 2 * BAR_SLOTS) % BAR_SLOTS;
    if (barShown(slot)) return slot;
  }
  return from;
}

void KlondikeActivity::onKey(const int dx, const int dy, const bool confirm) {
  const int before = focus;
  if (focus == kNone) {
    focus = STOCK;
  } else if (focus >= kBarFocus) {
    const int slot = focus - kBarFocus;
    if (confirm) {
      runBar(slot);
      // The action may hide its own slot (the last undo, a win).
      if (focus >= kBarFocus && !barShown(focus - kBarFocus)) focus = kBarFocus + nextBarSlot(slot, 1);
    } else if (dx != 0) {
      focus = kBarFocus + nextBarSlot(slot, dx);
    } else if (dy < 0) {
      focus = TABLEAU;
    }
  } else if (confirm) {
    activate(focus, kNone);
  } else if (dx != 0) {
    // Left/Right walk the row they are in.
    const int first = focus < TABLEAU ? STOCK : TABLEAU;
    const int count = focus < TABLEAU ? TABLEAU : TABLEAU_PILES;
    focus = first + (focus - first + dx + count) % count;
  } else if (dy > 0 && focus < TABLEAU) {
    focus = columnBelow(focus);
  } else if (dy > 0 && hasBar()) {
    focus = kBarFocus + nextBarSlot(BAR_SLOTS - 1, 1);
  } else if (dy < 0 && focus >= TABLEAU) {
    focus = pileAbove(focus);
  }
  if (focus != before) requestRepaint(RepaintUpdate);
}

bool KlondikeActivity::onBack() {
  if (pickPile != kNone || hintPile != kNone || noMoves) {
    clearPick();
  } else if (focus >= kBarFocus) {
    focus = TABLEAU;
  } else {
    return false;
  }
  requestRepaint(RepaintUpdate);
  return true;
}

// --- geometry ----------------------------------------------------------------

int KlondikeActivity::pileX(const int pile) const {
  const int width = renderer.getScreenWidth();
  if (pile <= WASTE) return columnX(width, pile);
  if (isFoundation(pile)) return columnX(width, kFirstFoundationColumn + pile - FOUNDATION);
  return columnX(width, pile - TABLEAU);
}

// The playable (rightmost) card of the waste fan.
int KlondikeActivity::wasteTopX() const {
  const int count = game.pile(WASTE).count;
  return pileX(WASTE) + kFan * std::max(0, std::min(game.drawThree() ? 2 : 0, count - 1));
}

int KlondikeActivity::cardY(const int pile, const int index) const {
  if (!isTableau(pile)) return BOARD_TOP;
  const Pile& p = game.pile(pile);
  int down = 0;
  while (down < p.count && !isFaceUp(p.cards[down])) ++down;
  const int up = p.count - down;
  // Face-up cards close up when a long run would run past the bar.
  const int room = (barTop() - kTableauGap) - kTableauTop - kCardHeight - down * kDownStep;
  const int step = up > 1 ? std::min(kUpStep, room / (up - 1)) : kUpStep;
  return kTableauTop + std::min(index, down) * kDownStep + std::max(0, index - down) * step;
}

int KlondikeActivity::cardAt(const int pile, const int y) const {
  const Pile& p = game.pile(pile);
  if (!isTableau(pile)) return p.count - 1;
  for (int i = p.count - 1; i >= 0; --i) {
    if (y >= cardY(pile, i)) return i;
  }
  return p.count - 1;
}

// --- render task -------------------------------------------------------------

void KlondikeActivity::build(UiScreen& screen) {
  char value[16];
  snprintf(value, sizeof(value), "%d", game.moveCount());
  const char* label = noMoves && !game.isWon() ? tr(STR_KLONDIKE_NO_MOVES) : tr(STR_KLONDIKE_MOVES);
  drawTopLine(screen, label, noMoves && !game.isWon() ? "" : value, false);

  for (int pile = 0; pile < PILES; ++pile) {
    drawPile(pile);
    const int x = pileX(pile);
    const int width = pile == WASTE && game.drawThree() ? kCardWidth + 2 * kFan : kCardWidth;
    const int top = isTableau(pile) ? kTableauTop : BOARD_TOP;
    const int height = isTableau(pile) ? (barTop() - kTableauGap) - kTableauTop : kCardHeight;
    screen.frame().hit(fui::makeRect(x, top, width, height), ACTION_PILE, static_cast<int16_t>(pile), fui::InputTouch);
  }

  if (pickPile != kNone) {
    const Pile& p = game.pile(pickPile);
    const int top = cardY(pickPile, pickIndex);
    const int bottom = cardY(pickPile, p.count - 1) + kCardHeight;
    const int x = pickPile == WASTE ? wasteTopX() : pileX(pickPile);
    renderer.drawRect(x, top, kCardWidth, bottom - top, kPickFrame, true);
  }
  if (hintPile != kNone) {
    const int pad = 3;
    const int top = isTableau(hintPile) ? cardY(hintPile, std::max(0, game.pile(hintPile).count - 1)) : BOARD_TOP;
    games::ui::drawDashedRect(renderer, pileX(hintPile) - pad, top - pad, kCardWidth + 2 * pad, kCardHeight + 2 * pad);
  }
  if (focus != kNone && focus < kBarFocus) {
    const Pile& p = game.pile(focus);
    const int top = isTableau(focus) && p.count > 0 ? cardY(focus, p.count - 1)
                    : isTableau(focus)              ? kTableauTop
                                                    : BOARD_TOP;
    drawCursor(focus == WASTE ? wasteTopX() : pileX(focus), top, kCardWidth, kCardHeight);
  }

  const bool won = game.isWon();
  if (won) drawResult(kTableauTop + kResultGap, tr(STR_GAMES_WON));

  BarItem items[BAR_SLOTS];
  const char* labels[BAR_SLOTS] = {tr(STR_GAMES_UNDO), tr(STR_KLONDIKE_HINT),
                                   won ? tr(STR_GAMES_NEW_GAME) : tr(STR_GAMES_MENU)};
  const freeink::Icon* icons[BAR_SLOTS] = {&icon_bar_undo_24, &icon_bar_hint_24,
                                           won ? &icon_bar_new_game_24 : &icon_menu_24};
  const BarItem::Kind kinds[BAR_SLOTS] = {BarItem::Action, BarItem::Action, won ? BarItem::NewGame : BarItem::Menu};
  for (int i = 0; i < BAR_SLOTS; ++i) {
    if (barShown(i)) items[i] = {icons[i], labels[i], false, kinds[i]};
  }
  drawBar(screen, items, focus >= kBarFocus ? focus - kBarFocus : -1);
}

void KlondikeActivity::drawPile(const int pile) const {
  const Pile& p = game.pile(pile);
  const int x = pileX(pile);
  if (p.count == 0) {
    // A won board leaves the columns blank under the result line.
    if (isTableau(pile) && game.isWon()) return;
    // Empty stock, foundations and columns: a dashed outline to drop on.
    games::ui::drawDashedRect(renderer, x, isTableau(pile) ? kTableauTop : BOARD_TOP, kCardWidth, kCardHeight);
    return;
  }
  if (pile == STOCK) {
    drawCard(x, BOARD_TOP, p.top());
    return;
  }
  if (pile == WASTE) {
    // Draw-3 fans the top three cards; only the last one plays.
    const int shown = game.drawThree() ? std::min(3, static_cast<int>(p.count)) : 1;
    for (int i = 0; i < shown; ++i) drawCard(x + i * kFan, BOARD_TOP, p.cards[p.count - shown + i]);
    return;
  }
  if (isFoundation(pile)) {
    drawCard(x, BOARD_TOP, p.top());
    return;
  }
  for (int i = 0; i < p.count; ++i) drawCard(x, cardY(pile, i), p.cards[i]);
}

void KlondikeActivity::drawCard(const int x, const int y, const uint8_t card) const {
  games::ui::drawCard(renderer, kCardStyle, x, y, rankOf(card), suitOf(card), isFaceUp(card));
}
