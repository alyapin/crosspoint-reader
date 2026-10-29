#include "CardsActivity.h"

#include <Arduino.h>
#include <CardLayout.h>
#include <FreeInkUIIcon.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <TrustedTime.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "activities/network/CrossPointWebServerActivity.h"
#include "activities/network/LocalSendActivity.h"
#include "cardsIcons.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "components/setka/setkaIcons.h"
#include "games/ui/ActionBar.h"
#include "games/ui/gameIcons.h"
#include "stats/StatsCalendar.h"
#include "util/ButtonNavigator.h"

namespace fui = freeink::ui;
using namespace setka;

namespace {

constexpr fui::ActionId ACTION_ROW = 1;
constexpr fui::ActionId ACTION_EXIT = 2;
constexpr fui::ActionId ACTION_CARD = 3;
constexpr fui::ActionId ACTION_SLOT = 4;

constexpr int kRowHeight = kNavRowHeight;
constexpr int kIconSize = 24;
constexpr int kIconGap = 16;
constexpr int kNoteGap = 12;
constexpr int kCardWidth = 440;
constexpr int kFrontBottomTouch = 400;
constexpr int kFrontBottomButtons = 360;
constexpr int kAnswerGap = 28;
constexpr int kCenteredMaxLines = 6;
constexpr int kSecondsPerCard = 10;
constexpr int kStatColumn = 150;
constexpr size_t kNameBytes = 256;

constexpr int kFrontFonts[] = {NOTOSANS_18_FONT_ID, NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID};
constexpr int kBackFonts[][2] = {
    {NOTOSANS_16_FONT_ID, NOTOSANS_14_FONT_ID},
    {NOTOSANS_14_FONT_ID, NOTOSANS_12_FONT_ID},
    {NOTOSANS_12_FONT_ID, NOTOSANS_12_FONT_ID},
};

enum OverviewRow : uint8_t { RowStart, RowNewPerDay, RowMoreNew };
enum DoneRow : uint8_t { RowToDecks, RowDoneMoreNew, RowUndo };

// Decks whose indexing ran out of memory this boot, by file size and time:
// the list does not read them again until the file changes.
struct OutOfMemory {
  uint32_t id;
  uint32_t size;
  uint32_t time;
};
OutOfMemory outOfMemory[8];
uint8_t outOfMemoryCount = 0;
uint8_t outOfMemoryNext = 0;

bool knownOutOfMemory(const uint32_t id, const uint32_t size, const uint32_t time) {
  for (uint8_t i = 0; i < outOfMemoryCount; i++) {
    const OutOfMemory& d = outOfMemory[i];
    if (d.id == id && d.size == size && d.time == time) return true;
  }
  return false;
}

void rememberOutOfMemory(const uint32_t id, const uint32_t size, const uint32_t time) {
  outOfMemory[outOfMemoryNext] = {id, size, time};
  outOfMemoryNext = static_cast<uint8_t>((outOfMemoryNext + 1) % std::size(outOfMemory));
  if (outOfMemoryCount < std::size(outOfMemory)) outOfMemoryCount++;
}

// Russian forms; a language whose "few" and "many" match (English) has one and other.
const char* plural(const unsigned n, const StrId one, const StrId few, const StrId many) {
  if (strcmp(I18N.get(few), I18N.get(many)) == 0) return I18N.get(n == 1 ? one : many);
  const unsigned mod10 = n % 10;
  const unsigned mod100 = n % 100;
  if (mod10 == 1 && mod100 != 11) return I18N.get(one);
  if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return I18N.get(few);
  return I18N.get(many);
}

const char* cardsWord(const unsigned n) {
  return plural(n, StrId::STR_CARDS_CARD_ONE, StrId::STR_CARDS_CARD_FEW, StrId::STR_CARDS_CARD_MANY);
}

// Copies at most cap-1 bytes, never splitting a UTF-8 sequence.
void copyCut(char* out, const size_t cap, const char* text, size_t len) {
  if (len >= cap) {
    len = cap - 1;
    while (len > 0 && (static_cast<uint8_t>(text[len]) & 0xC0) == 0x80) len--;
  }
  memcpy(out, text, len);
  out[len] = '\0';
}

void deckCaption(const cards::Summary& s, char* out, const size_t cap) {
  char text[96];
  if (s.reviews > 0 && s.newCards > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_REVIEW_NEW), s.reviews, s.newCards);
  } else if (s.reviews > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_REVIEW), s.reviews);
  } else if (s.newCards > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_NEW), s.newCards);
  } else if (s.cards == 0) {
    snprintf(text, sizeof(text), "%s", tr(STR_CARDS_NO_CARDS));
  } else if (s.tomorrow > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_ALL_DONE_TOMORROW), s.tomorrow);
  } else {
    snprintf(text, sizeof(text), "%s", tr(STR_CARDS_ALL_DONE));
  }
  copyCut(out, cap, text, strlen(text));
}

void intervalText(const uint16_t days, char* out, const size_t cap) {
  if (days == 0) {
    snprintf(out, cap, "%s", tr(STR_CARDS_TODAY));
  } else if (days == 1) {
    snprintf(out, cap, "%s", tr(STR_CARDS_TOMORROW));
  } else if (days < 7) {
    snprintf(out, cap, tr(STR_CARDS_IN), days,
             plural(days, StrId::STR_CARDS_DAY_ONE, StrId::STR_CARDS_DAY_FEW, StrId::STR_CARDS_DAY_MANY));
  } else if (days < 30) {
    snprintf(out, cap, tr(STR_CARDS_IN), days / 7,
             plural(days / 7, StrId::STR_CARDS_WEEK_ONE, StrId::STR_CARDS_WEEK_FEW, StrId::STR_CARDS_WEEK_MANY));
  } else {
    snprintf(out, cap, tr(STR_CARDS_IN), days / 30,
             plural(days / 30, StrId::STR_CARDS_MONTH_ONE, StrId::STR_CARDS_MONTH_FEW, StrId::STR_CARDS_MONTH_MANY));
  }
}

void stemOf(const char* file, char* out, const size_t cap) {
  const char* dot = strrchr(file, '.');
  copyCut(out, cap, file, dot && dot != file ? static_cast<size_t>(dot - file) : strlen(file));
}

struct Measure {
  const GfxRenderer* renderer;
  int font;
  EpdFontFamily::Style style;
  char* buf;  // kNameBytes
};

int measureText(void* ctx, const char* text, const size_t len) {
  const auto* m = static_cast<const Measure*>(ctx);
  copyCut(m->buf, kNameBytes, text, len);
  return m->renderer->getTextWidth(m->font, m->buf, m->style);
}

struct Block {
  const char* text = "";
  int font = NOTOSANS_14_FONT_ID;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  int lineHeight = 0;
  cards::Lines lines;
  int height() const { return lines.count * lineHeight; }
};

// Lines that fit maxHeight; at least one when `atLeastOne`.
void layoutBlock(const GfxRenderer& renderer, Block& b, const int font, const int maxHeight, char* scratch,
                 const bool atLeastOne = true) {
  b.font = font;
  b.lineHeight = renderer.getLineHeight(font);
  const int maxLines = std::max(atLeastOne ? 1 : 0, maxHeight / b.lineHeight);
  if (maxLines == 0 || *b.text == '\0') {
    b.lines.count = 0;
    b.lines.overflow = *b.text != '\0';
    return;
  }
  Measure m{&renderer, font, b.style, scratch};
  cards::wrapText(b.text, kCardWidth, maxLines, &measureText, &m, b.lines);
}

// Lines centred, the block left-aligned when it runs long; text cut short ends with "…".
void drawBlock(const GfxRenderer& renderer, const Block& b, int y, char* scratch) {
  const int width = renderer.getScreenWidth();
  const bool left = b.lines.count > kCenteredMaxLines;
  for (int i = 0; i < b.lines.count; i++) {
    const char* line = b.text + b.lines.line[i].start;
    size_t len = b.lines.line[i].len;
    const bool cut = b.lines.overflow && i == b.lines.count - 1;
    while (true) {
      copyCut(scratch, kNameBytes - 4, line, len);
      if (!cut) break;
      strcat(scratch, "\xE2\x80\xA6");
      if (len == 0 || renderer.getTextWidth(b.font, scratch, b.style) <= kCardWidth) break;
      len = strlen(scratch) - 3;
      do len--;
      while (len > 0 && (static_cast<uint8_t>(line[len]) & 0xC0) == 0x80);
    }
    const int w = renderer.getTextWidth(b.font, scratch, b.style);
    renderer.drawText(b.font, left ? (width - kCardWidth) / 2 : (width - w) / 2, y, scratch, true, b.style);
    y += b.lineHeight;
  }
}

const char* sideText(const cards::Fields& f, const int side) {
  if (f.text[side][0] != '\0') return f.text[side];
  if (f.media[side] & cards::HasImage) return tr(STR_CARDS_IMAGE);
  if (f.media[side] & cards::HasSound) return tr(STR_CARDS_SOUND);
  return "";
}

uint16_t cardDay() { return stats::localDay(cards::cardEpoch(trustedtime::trustedNow())); }

}  // namespace

CardsActivity::CardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckFile)
    : Activity("Cards", renderer, mappedInput),
      UiAppHost(renderer),
      deckFile(std::move(deckFile)),
      direct(!this->deckFile.empty()) {}

void CardsActivity::dueTodayText(char* out, const size_t cap) {
  uint16_t total = 0;
  if (cards::loadDueTotal(cardDay(), total) && total > 0) {
    snprintf(out, cap, tr(STR_CARDS_DUE_TODAY), total);
  } else if (cap > 0) {
    out[0] = '\0';
  }
}

void CardsActivity::onEnter() {
  RenderLock lock(*this);
  Activity::onEnter();
  resetUi();
  app.setScreen(&CardsActivity::screenFn, this);
  store = makeUniqueNoThrow<cards::DeckStore>();
  if (!store) {
    showMessage(StrId::STR_CARDS_NO_MEMORY);
    return;
  }
  runAfterPaint(direct ? Job::OpenDeck : Job::LoadList, StrId::STR_CARDS_PREPARING);
}

void CardsActivity::onExit() {
  store.reset();
  fields.reset();
  decks.reset();
  Activity::onExit();
}

uint16_t CardsActivity::realDay() const { return cardDay(); }

bool CardsActivity::hasTouch() const { return UITheme::getInstance().getMetrics().buttonHintsHeight == 0; }

void CardsActivity::requestRepaint(const Repaint kind) {
  uint8_t current = repaint.load();
  while (current < kind && !repaint.compare_exchange_weak(current, kind)) {
  }
  requestUpdate();
}

void CardsActivity::setScreen(const Screen next, const int focusIndex) {
  view = next;
  focus = focusIndex;
  app.clearTapFlash();
  requestRepaint(RepaintEntry);
}

void CardsActivity::showMessage(const StrId text) {
  message = text;
  setScreen(Screen::Message, 0);
}

void CardsActivity::showStatus(const cards::Status status) {
  switch (status) {
    case cards::Status::TooLarge:
      showMessage(StrId::STR_CARDS_TOO_LARGE_BODY);
      return;
    case cards::Status::NoMemory:
      showMessage(StrId::STR_CARDS_NO_MEMORY);
      return;
    case cards::Status::Unsupported:
      showMessage(StrId::STR_CARDS_UTF16);
      return;
    default:
      showMessage(StrId::STR_CARDS_FAILED);
      return;
  }
}

// Slow work (indexing a deck) runs after a "preparing" frame is on screen.
void CardsActivity::runAfterPaint(const Job next, const StrId text) {
  job = next;
  jobPainted = false;
  message = text;
  setScreen(Screen::Loading, 0);
}

// --- loop task ---------------------------------------------------------------

void CardsActivity::loop() {
  if (job != Job::None) {
    if (!jobPainted) return;
    RenderLock lock(*this);
    const Job current = job;
    job = Job::None;
    if (current == Job::LoadList) {
      loadList();
    } else {
      openDeck();
    }
    return;
  }

  const bool guarded = view == Screen::Back && (flipPending || millis() - flipShownMs < FLIP_GUARD_MS);
  if (const auto touch = routeTouch(mappedInput)) {
    if (guarded) return;
    RenderLock lock(*this);
    switch (touch.event.action) {
      case ACTION_EXIT:
        goBack();
        break;
      case ACTION_CARD:
        if (view == Screen::Front) activate(1);
        break;
      case ACTION_ROW:
      case ACTION_SLOT:
        activate(touch.event.value);
        break;
      default:
        break;
    }
    return;
  }
  if (guarded) return;

  using Button = MappedInputManager::Button;
  const bool card = view == Screen::Front || view == Screen::Back;
  // Button boards grade in two presses: Confirm shows the answer, then Left =
  // «Снова», Confirm = «Хорошо», Right = «Легко». Holding Back undoes.
  if (card && !hasTouch()) {
    if (view == Screen::Front && store->canUndo() && mappedInput.wasLongPressed(Button::Back, UNDO_HOLD_MS)) {
      RenderLock lock(*this);
      activate(0);
      return;
    }
    if (view == Screen::Back && (mappedInput.wasPressed(Button::Left) || mappedInput.wasPressed(Button::Right))) {
      RenderLock lock(*this);
      grade(mappedInput.wasPressed(Button::Left) ? cards::Grade::Again : cards::Grade::Easy);
      return;
    }
  }
  if (mappedInput.wasReleased(Button::Back)) {
    RenderLock lock(*this);
    goBack();
    return;
  }
  if (mappedInput.wasReleased(Button::Confirm)) {
    RenderLock lock(*this);
    activate(card ? 1 : focus);
    return;
  }
  if (card) return;
  if (view == Screen::List) {
    const auto swipe = mappedInput.wasSwipe();
    if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
      RenderLock lock(*this);
      const int rows = std::max(1, (renderer.getScreenHeight() - 160 - headerBottom()) / kRowHeight);
      moveFocus(swipe == MappedInputManager::SwipeDir::Up ? rows : -rows);
      return;
    }
  }
  ButtonNavigator::onNextPress([this] {
    RenderLock lock(*this);
    moveFocus(1);
  });
  ButtonNavigator::onPreviousPress([this] {
    RenderLock lock(*this);
    moveFocus(-1);
  });
}

int CardsActivity::focusCount() const {
  switch (view) {
    case Screen::List:
      return deckCount;
    case Screen::Empty:
      return 2;
    case Screen::Overview:
    case Screen::Done:
    case Screen::Front:
    case Screen::Back:
      return 3;
    default:
      return 0;
  }
}

bool CardsActivity::shown(const int i) const {
  switch (view) {
    case Screen::Overview:
      return i == RowNewPerDay || (i == RowStart && summary.dueToday() > 0) ||
             (i == RowMoreNew && summary.dueToday() == 0 && summary.fresh > 0);
    case Screen::Done:
      return i == RowToDecks || (i == RowDoneMoreNew && summary.fresh > 0) || (i == RowUndo && store->canUndo());
    case Screen::Front:
      return i == 1 || (i == 0 && store->canUndo());
    default:
      return i >= 0 && i < focusCount();
  }
}

// Steps the focus over the items the screen shows.
void CardsActivity::moveFocus(const int delta) {
  const int count = focusCount();
  if (count == 0) return;
  if (view == Screen::List) {
    focus = delta == 1 || delta == -1 ? (focus + delta + count) % count : std::clamp(focus + delta, 0, count - 1);
  } else {
    int next = focus;
    for (int step = 0; step < count; step++) {
      next = (next + (delta > 0 ? 1 : count - 1)) % count;
      if (shown(next)) break;
    }
    if (!shown(next) || next == focus) return;
    focus = next;
  }
  requestRepaint(RepaintUpdate);
}

void CardsActivity::goBack() {
  switch (view) {
    case Screen::Front:
    case Screen::Back:
      if (store) store->endSession();
      showOverview();
      return;
    case Screen::Overview:
    case Screen::Done:
    case Screen::Message:
      if (store) store->endSession();
      if (!direct && (view != Screen::Message || deckTitle[0] != '\0')) {
        runAfterPaint(Job::LoadList, StrId::STR_CARDS_PREPARING);
        return;
      }
      break;
    default:
      break;
  }
  app.clearTapFlash();
  finish();
}

void CardsActivity::activate(const int index) {
  if (view != Screen::List && view != Screen::Empty && view != Screen::Back && view != Screen::Message &&
      !shown(index)) {
    return;
  }
  switch (view) {
    case Screen::List:
      if (index < 0 || index >= deckCount) return;
      focus = deckFocus = index;
      if (!findDeckFile(decks[index].id, deckFile)) {
        showMessage(StrId::STR_CARDS_FAILED);
        return;
      }
      runAfterPaint(Job::OpenDeck, StrId::STR_CARDS_PREPARING);
      return;
    case Screen::Empty: {
      app.clearTapFlash();
      std::unique_ptr<Activity> child;
      if (index == 0) {
        child = std::make_unique<LocalSendActivity>(renderer, mappedInput);
      } else {
        child = std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput);
      }
      startActivityForResult(std::move(child), [this](const ActivityResult&) {
        RenderLock lock(*this);
        runAfterPaint(Job::LoadList, StrId::STR_CARDS_PREPARING);
      });
      return;
    }
    case Screen::Overview:
      if (index == RowStart) {
        startSession(0);
      } else if (index == RowMoreNew) {
        startSession(cards::EXTRA_NEW);
      } else {
        const auto& choices = cards::NEW_PER_DAY_CHOICES;
        const auto* at = std::find(std::begin(choices), std::end(choices), summary.newPerDay);
        const uint8_t next = at == std::end(choices) || at + 1 == std::end(choices) ? choices[0] : *(at + 1);
        if (store->setNewPerDay(next)) store->summarize(summary);
        focus = RowNewPerDay;
        requestRepaint(RepaintUpdate);
      }
      return;
    case Screen::Front:
      if (index == 0) {
        if (store->undo()) showCard(false);
      } else {
        view = Screen::Back;
        focus = 1;
        flipPending = true;
        requestRepaint(RepaintUpdate);
      }
      return;
    case Screen::Back:
      if (index >= 0 && index < 3) grade(static_cast<cards::Grade>(index));
      return;
    case Screen::Done:
      if (index == RowToDecks) {
        goBack();
      } else if (index == RowDoneMoreNew) {
        startSession(cards::EXTRA_NEW);
      } else if (store->undo()) {
        showCard(true);
      }
      return;
    case Screen::Message:
      goBack();
      return;
    default:
      return;
  }
}

bool CardsActivity::findDeckFile(const uint32_t id, std::string& out) const {
  auto name = makeUniqueNoThrow<char[]>(kNameBytes);
  HalFile dir = Storage.open(cards::deckDir());
  if (!name || !dir || !dir.isDirectory()) return false;
  for (HalFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name.get(), kNameBytes);
    const bool isDir = entry.isDirectory();
    entry.close();
    if (!isDir && cards::deckId(name.get()) == id) {
      out = name.get();
      return true;
    }
  }
  return false;
}

void CardsActivity::loadList() {
  deckTitle[0] = '\0';
  cards::restoreReplacedDecks();
  const char* folder = cards::deckDir();
  if (!Storage.exists(folder)) Storage.mkdir(folder);
  if (!decks) decks = makeUniqueNoThrow<DeckRow[]>(MAX_DECKS);
  auto name = makeUniqueNoThrow<char[]>(kNameBytes);
  if (!decks || !name) {
    showMessage(StrId::STR_CARDS_NO_MEMORY);
    return;
  }
  // Pass one keeps the first MAX_DECKS decks by name; pass two reads their progress.
  deckCount = 0;
  HalFile dir = Storage.open(folder);
  for (HalFile entry = dir ? dir.openNextFile() : HalFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name.get(), kNameBytes);
    const bool isDir = entry.isDirectory();
    const uint32_t size = isDir ? 0 : static_cast<uint32_t>(entry.fileSize());
    const uint32_t time = isDir ? 0 : entry.modificationTime();
    entry.close();
    if (isDir || name[0] == '.' || !cards::isDeckName(name.get())) continue;
    DeckRow row{};
    stemOf(name.get(), row.name, sizeof(row.name));
    row.id = cards::deckId(name.get());
    row.size = size;
    row.time = time;
    int at = deckCount;
    while (at > 0 && strcmp(row.name, decks[at - 1].name) < 0) at--;
    if (at >= MAX_DECKS) continue;
    const int moved = std::min(deckCount, MAX_DECKS - 1) - at;
    memmove(&decks[at + 1], &decks[at], static_cast<size_t>(std::max(0, moved)) * sizeof(DeckRow));
    decks[at] = row;
    deckCount = std::min(deckCount + 1, MAX_DECKS);
  }
  if (dir) dir.close();

  const uint16_t day = realDay();
  uint32_t total = 0;
  dir = Storage.open(folder);
  for (HalFile entry = dir ? dir.openNextFile() : HalFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name.get(), kNameBytes);
    const bool isDir = entry.isDirectory();
    entry.close();
    if (isDir) continue;
    const uint32_t id = cards::deckId(name.get());
    DeckRow* row = std::find_if(decks.get(), decks.get() + deckCount, [id](const DeckRow& r) { return r.id == id; });
    if (row == decks.get() + deckCount || row->caption[0] != '\0') continue;
    if (knownOutOfMemory(id, row->size, row->time)) {
      snprintf(row->caption, sizeof(row->caption), "%s", tr(STR_CARDS_NO_MEMORY_SHORT));
      continue;
    }
    cards::Summary s;
    const cards::Status status = store->open(name.get(), day, cards::Reindex::Never);
    switch (status) {
      case cards::Status::Ok:
        store->summarize(s);
        deckCaption(s, row->caption, sizeof(row->caption));
        total += s.dueToday();
        break;
      case cards::Status::TooLarge:
        snprintf(row->caption, sizeof(row->caption), "%s", tr(STR_CARDS_TOO_LARGE));
        break;
      case cards::Status::NoMemory:
        rememberOutOfMemory(id, row->size, row->time);
        snprintf(row->caption, sizeof(row->caption), "%s", tr(STR_CARDS_NO_MEMORY_SHORT));
        break;
      case cards::Status::Unsupported:
        snprintf(row->caption, sizeof(row->caption), "%s", tr(STR_CARDS_UTF16_SHORT));
        break;
      default:
        snprintf(row->caption, sizeof(row->caption), "%s", tr(STR_CARDS_FAILED));
        break;
    }
    store->close();
  }
  if (dir) dir.close();
  if (day != cards::NO_DAY) cards::saveDueTotal(day, static_cast<uint16_t>(std::min<uint32_t>(total, UINT16_MAX)));
  listTop = 0;
  setScreen(deckCount > 0 ? Screen::List : Screen::Empty, deckCount > 0 ? std::min(deckFocus, deckCount - 1) : 0);
}

bool CardsActivity::reopen(const cards::Reindex reindex) {
  const cards::Status status = store->open(deckFile.c_str(), realDay(), reindex);
  if (status != cards::Status::Ok) {
    showStatus(status);
    return false;
  }
  return true;
}

void CardsActivity::openDeck() {
  stemOf(deckFile.c_str(), deckTitle, sizeof(deckTitle));
  if (reopen(cards::Reindex::IfChanged)) showOverview();
}

// The day may have turned at 04:00 since the deck was opened.
void CardsActivity::showOverview() {
  if (store->openedOn() != realDay() && !reopen(cards::Reindex::IfChanged)) return;
  if (!store->summarize(summary)) {
    showMessage(StrId::STR_CARDS_FAILED);
    return;
  }
  if (summary.cards == 0) {
    showMessage(StrId::STR_CARDS_EMPTY_DECK);
    return;
  }
  setScreen(Screen::Overview, summary.dueToday() > 0 ? RowStart : RowNewPerDay);
}

void CardsActivity::startSession(const uint16_t extraNew) {
  if (store->openedOn() != realDay() && !reopen(cards::Reindex::IfChanged)) return;
  if (!fields) fields = makeUniqueNoThrow<cards::Fields>();
  const cards::Status status = fields ? store->startSession(extraNew) : cards::Status::NoMemory;
  if (status != cards::Status::Ok) {
    showStatus(status);
    return;
  }
  sessionStartMs = millis();
  showCard(true);
}

void CardsActivity::showCard(const bool entry) {
  if (store->finished()) {
    finishSession();
    setScreen(Screen::Done, RowToDecks);
    return;
  }
  const cards::Status status = store->loadCard(*fields);
  if (status == cards::Status::Changed) {
    // Edited in place, keeping its size and time: index it again.
    store->endSession();
    if (reopen(cards::Reindex::Always)) showOverview();
    return;
  }
  if (status != cards::Status::Ok) {
    store->endSession();
    showStatus(status);
    return;
  }
  view = Screen::Front;
  focus = 1;
  app.clearTapFlash();
  requestRepaint(entry ? RepaintEntry : RepaintUpdate);
}

void CardsActivity::grade(const cards::Grade g) {
  if (!store->grade(g)) {
    store->endSession();
    showMessage(StrId::STR_CARDS_FAILED);
    return;
  }
  showCard(false);
}

// The session stays open on the summary, so its last answer can be undone.
void CardsActivity::finishSession() {
  sessionMs = millis() - sessionStartMs;
  sessionCards = 0;
  for (int i = 0; i < 3; i++) {
    sessionCounts[i] = store->graded(static_cast<cards::Grade>(i));
    sessionCards += sessionCounts[i];
  }
  const uint16_t before = summary.dueToday();
  store->flush();
  store->summarize(summary);
  // Keeps the «Ещё» count honest when the deck list is not visited again.
  uint16_t total = 0;
  const uint16_t day = realDay();
  if (cards::loadDueTotal(day, total)) {
    const uint16_t done = before > summary.dueToday() ? before - summary.dueToday() : 0;
    cards::saveDueTotal(day, total > done ? total - done : 0);
  }
}

// --- render task -------------------------------------------------------------

void CardsActivity::screenFn(UiScreen& screen, void* user) { static_cast<CardsActivity*>(user)->build(screen); }

int CardsActivity::headerBottom() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return metrics.topPadding + metrics.headerHeight;
}

void CardsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const bool card = view == Screen::Front || view == Screen::Back;
  if (!card) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const bool deckScreen =
        view == Screen::Overview || view == Screen::Done || (view == Screen::Message && deckTitle[0] != '\0');
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                   deckScreen ? deckTitle : tr(STR_CARDS));
  }
  renderUi();
  if (!hasTouch()) {
    const auto labels =
        view == Screen::Front ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_CARDS_SHOW_HINT), "", "")
        : card ? mappedInput.mapLabels(tr(STR_BACK), tr(STR_CARDS_GOOD), tr(STR_CARDS_AGAIN), tr(STR_CARDS_EASY))
               : mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }
  const uint8_t kind = repaint.exchange(RepaintNone);
  renderer.displayBuffer(kind == RepaintEntry ? cadence.entry() : cadence.update());
  if (view == Screen::Back && flipPending) {
    flipShownMs = millis();
    flipPending = false;
  }
  if (job != Job::None) jobPainted = true;
}

void CardsActivity::build(UiScreen& screen) {
  switch (view) {
    case Screen::List:
      buildList(screen);
      break;
    case Screen::Empty:
      buildEmpty(screen);
      break;
    case Screen::Overview:
      buildOverview(screen);
      break;
    case Screen::Front:
    case Screen::Back:
      buildCard(screen);
      break;
    case Screen::Done:
      buildDone(screen);
      break;
    case Screen::Loading:
    case Screen::Message: {
      int y = headerBottom() + 20;
      const int line = renderer.getLineHeight(kText12Font);
      for (const auto& text : renderer.wrappedText(kText12Font, I18N.get(message), kCardWidth, 6)) {
        renderer.drawText(kText12Font, kSidePadding, y, text.c_str());
        y += line;
      }
      break;
    }
  }
}

void CardsActivity::drawRow(UiScreen& screen, const int top, const freeink::Icon* icon, const char* label,
                            const bool focused, const char* status, const char* caption, const int value) {
  const int width = renderer.getScreenWidth();
  if (focused) drawSelectionBar(renderer, kSelectionBarX, top, kRowHeight);
  int x = kRowTextX;
  if (icon != nullptr) {
    screen.target().bitmap(fui::makeRect(x, top + (kRowHeight - kIconSize) / 2, kIconSize, kIconSize),
                           fui::bitmapFromIcon(*icon), fui::BitmapMode::Center);
    x += kIconSize + kIconGap;
  }
  const EpdFontFamily::Style style = focused ? kSemiBold : EpdFontFamily::REGULAR;
  const int line12 = renderer.getLineHeight(kText12Font);
  int statusWidth = 0;
  if (status != nullptr && *status) statusWidth = renderer.getTextWidth(kText10Font, status) + kSidePadding;
  const std::string fitted = renderer.truncatedText(kText12Font, label, width - kSidePadding - x - statusWidth, style);
  int textTop = top + (kRowHeight - line12) / 2;
  if (caption != nullptr && *caption) {
    const int captionLine = renderer.getLineHeight(kCaptionFont);
    textTop = top + (kRowHeight - line12 - captionLine) / 2;
    const std::string cut = renderer.truncatedText(kCaptionFont, caption, width - kSidePadding - x);
    renderer.drawText(kCaptionFont, x, textTop + line12, cut.c_str());
  }
  renderer.drawText(kText12Font, x, textTop, fitted.c_str(), true, style);
  if (statusWidth > 0) {
    const int statusTop =
        textTop + renderer.getFontAscenderSize(kText12Font) - renderer.getFontAscenderSize(kText10Font);
    renderer.drawText(kText10Font, width - kSidePadding - renderer.getTextWidth(kText10Font, status), statusTop,
                      status);
  }
  screen.frame().hit(fui::makeRect(0, top, width, kRowHeight), ACTION_ROW, static_cast<int16_t>(value),
                     fui::InputTouch);
}
void CardsActivity::drawStats(const int y, const uint16_t (&numbers)[3], const StrId (&labels)[3]) const {
  const int line12 = renderer.getLineHeight(kText12Font);
  for (int i = 0; i < 3; i++) {
    char number[8];
    snprintf(number, sizeof(number), "%u", numbers[i]);
    const int x = kSidePadding + i * kStatColumn;
    renderer.drawText(kText12Font, x, y, number, true, kSemiBold);
    renderer.drawText(kCaptionFont, x, y + line12, I18N.get(labels[i]));
  }
}

void CardsActivity::buildList(UiScreen& screen) {
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int height = safe.y + safe.height;
  const int captionLine = renderer.getLineHeight(kCaptionFont);
  // The formats wrap once when a language runs long.
  const auto formats =
      renderer.wrappedText(kCaptionFont, tr(STR_CARDS_LIST_FORMATS), renderer.getScreenWidth() - 2 * kSidePadding, 2);
  const int noteTop = hintCaptionTop(renderer, kCaptionFont, height) - static_cast<int>(formats.size()) * captionLine;
  const int first = headerBottom() + 14;
  const int rows = std::max(1, (noteTop - 8 - first) / kRowHeight);
  if (focus < listTop) listTop = focus;
  if (focus >= listTop + rows) listTop = focus - rows + 1;
  for (int i = listTop; i < deckCount && i < listTop + rows; i++) {
    drawRow(screen, first + (i - listTop) * kRowHeight, &icon_cards_24, decks[i].name, i == focus, nullptr,
            decks[i].caption, i);
  }
  int y = noteTop;
  renderer.drawText(kCaptionFont, kSidePadding, y, tr(STR_CARDS_LIST_NOTE));
  for (const auto& line : formats) {
    y += captionLine;
    renderer.drawText(kCaptionFont, kSidePadding, y, line.c_str());
  }
}
void CardsActivity::buildEmpty(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int line12 = renderer.getLineHeight(kText12Font);
  const int line10 = renderer.getLineHeight(kText10Font);
  const int captionLine = renderer.getLineHeight(kCaptionFont);
  int y = headerBottom() + 18;
  renderer.drawText(kText12Font, kSidePadding, y, tr(STR_CARDS_EMPTY_TITLE), true, kSemiBold);
  y += line12 + 4;
  for (const auto& text : renderer.wrappedText(kText10Font, tr(STR_CARDS_EMPTY_BODY), width - 2 * kSidePadding, 4)) {
    renderer.drawText(kText10Font, kSidePadding, y, text.c_str());
    y += line10;
  }
  y += 8;
  renderer.drawText(kCaptionFont, kSidePadding, y, tr(STR_CARDS_EXAMPLE));
  y += captionLine + 2;
  const int exampleLine = renderer.getLineHeight(NOTOSANS_14_FONT_ID);
  const char* const examples[2][2] = {{"el perro", "собака"}, {"la casa", "дом"}};
  for (const auto& pair : examples) {
    renderer.drawText(NOTOSANS_14_FONT_ID, kSidePadding, y, pair[0]);
    const int mid = y + exampleLine / 2;
    renderer.fillRect(150, mid - 1, 34, 2, true);
    renderer.drawLine(184, mid, 177, mid - 5, 2, true);
    renderer.drawLine(184, mid, 177, mid + 5, 2, true);
    renderer.fillRect(187, mid - 6, 2, 13, true);
    renderer.drawText(NOTOSANS_14_FONT_ID, 210, y, pair[1]);
    y += exampleLine + 2;
  }
  y += 10;
  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 14;
  renderer.drawText(kCaptionFont, kSidePadding, y, tr(STR_CARDS_HOW_TO_ADD));
  y += captionLine + 4;
  drawRow(screen, y, &icon_smartphone_24, tr(STR_CARDS_FROM_PHONE), focus == 0, nullptr, tr(STR_CARDS_FROM_PHONE_HINT),
          0);
  y += kRowHeight;
  drawRow(screen, y, &icon_wifi_24, tr(STR_CARDS_FROM_WIFI), focus == 1, nullptr, tr(STR_CARDS_FROM_WIFI_HINT), 1);
  y += kRowHeight + 14;
  for (const auto& text : renderer.wrappedText(kCaptionFont, tr(STR_CARDS_COPY_HINT), width - 2 * kSidePadding, 4)) {
    renderer.drawText(kCaptionFont, kSidePadding, y, text.c_str());
    y += captionLine;
  }
}

void CardsActivity::buildOverview(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int line12 = renderer.getLineHeight(kText12Font);
  const int captionLine = renderer.getLineHeight(kCaptionFont);
  const uint16_t today = summary.dueToday();
  char text[96];
  char sub[64];
  int y = headerBottom() + 20;
  if (today > 0) {
    snprintf(text, sizeof(text), "%u", today);
    drawHeroNumber(renderer, kHeroFont, kSidePadding - 6, y, text);
    y += heroCapHeight(renderer, kHeroFont) + 20;
    snprintf(text, sizeof(text), tr(STR_CARDS_FOR_TODAY), cardsWord(today));
    renderer.drawText(kText12Font, kSidePadding, y, text);
    y += line12 + 4;
    deckCaption(summary, sub, sizeof(sub));
    const unsigned minutes = std::max<unsigned>(1, (today * kSecondsPerCard + 30) / 60);
    snprintf(text, sizeof(text), tr(STR_CARDS_MINUTES_APPROX), sub, minutes);
  } else {
    renderer.drawText(kText12Font, kSidePadding, y, tr(STR_CARDS_NOTHING_TODAY), true, kSemiBold);
    y += line12 + 4;
    snprintf(text, sizeof(text), tr(STR_CARDS_TOMORROW_COUNT), summary.tomorrow, cardsWord(summary.tomorrow));
  }
  renderer.drawText(kCaptionFont, kSidePadding, y, text);
  y += captionLine;
  if (summary.dateUnknown) {
    renderer.drawText(kCaptionFont, kSidePadding, y, tr(STR_CARDS_DATE_UNKNOWN));
    y += captionLine;
  }
  y += 14;
  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 18;
  drawStats(y, {summary.fresh, summary.learning, summary.known},
            {StrId::STR_CARDS_FRESH, StrId::STR_CARDS_LEARNING, StrId::STR_CARDS_KNOWN});
  y += line12 + captionLine + 14;
  const int known = summary.cards ? summary.known * 100 / summary.cards : 0;
  drawProgressBar(renderer, kSidePadding, y, width - 2 * kSidePadding, known);
  y += kProgressHeight + 10;
  snprintf(text, sizeof(text), tr(STR_CARDS_KNOWN_SHARE), known, summary.cards, cardsWord(summary.cards));
  renderer.drawText(kCaptionFont, kSidePadding, y, text);
  y += captionLine;
  if (summary.rows > summary.cards) {
    snprintf(text, sizeof(text), tr(STR_CARDS_TRUNCATED), summary.cards, summary.rows);
    renderer.drawText(kCaptionFont, kSidePadding, y, text);
    y += captionLine;
  }
  if (summary.skipped > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_SKIPPED), summary.skipped);
    renderer.drawText(kCaptionFont, kSidePadding, y, text);
    y += captionLine;
  }
  if (summary.dropped > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_DROPPED), summary.dropped);
    renderer.drawText(kCaptionFont, kSidePadding, y, text);
    y += captionLine;
  }
  y += 12;
  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 14;
  if (today > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_COUNT), today, cardsWord(today));
    drawRow(screen, y, &icon_play_24, tr(STR_CARDS_START), focus == RowStart, text, nullptr, RowStart);
    y += kRowHeight;
  }
  snprintf(text, sizeof(text), "%u", summary.newPerDay);
  drawRow(screen, y, &icon_sliders_24, tr(STR_CARDS_NEW_PER_DAY), focus == RowNewPerDay, text, nullptr, RowNewPerDay);
  y += kRowHeight;
  if (today == 0 && summary.fresh > 0) {
    snprintf(text, sizeof(text), tr(STR_CARDS_MORE_NEW), cards::EXTRA_NEW);
    snprintf(sub, sizeof(sub), tr(STR_CARDS_LEFT), summary.fresh);
    drawRow(screen, y, &icon_bar_new_game_24, text, focus == RowMoreNew, sub, nullptr, RowMoreNew);
  }
}
void CardsActivity::buildDone(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int line12 = renderer.getLineHeight(kText12Font);
  const int line10 = renderer.getLineHeight(kText10Font);
  const int captionLine = renderer.getLineHeight(kCaptionFont);
  char text[96];
  int y = headerBottom() + 20;
  snprintf(text, sizeof(text), "%u", sessionCards);
  drawHeroNumber(renderer, kHeroFont, kSidePadding - 6, y, text);
  y += heroCapHeight(renderer, kHeroFont) + 20;
  const unsigned minutes = std::max<unsigned>(1, (sessionMs + 30000) / 60000);
  snprintf(text, sizeof(text), tr(STR_CARDS_DONE_IN), cardsWord(sessionCards), minutes,
           plural(minutes, StrId::STR_CARDS_MINUTE_ONE, StrId::STR_CARDS_MINUTE_FEW, StrId::STR_CARDS_MINUTE_MANY));
  renderer.drawText(kText12Font, kSidePadding, y, text);
  y += line12 + 18;
  drawStats(y, sessionCounts, {StrId::STR_CARDS_AGAIN_DONE, StrId::STR_CARDS_GOOD_DONE, StrId::STR_CARDS_EASY_DONE});
  y += line12 + captionLine + 14;
  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 16;
  snprintf(text, sizeof(text), tr(STR_CARDS_TOMORROW_LINE), summary.tomorrow, cardsWord(summary.tomorrow));
  renderer.drawText(kText10Font, kSidePadding, y, text);
  y += line10 + 4;
  snprintf(text, sizeof(text), tr(STR_CARDS_WEEK), summary.week);
  renderer.drawText(kCaptionFont, kSidePadding, y, text);
  y += captionLine + 14;
  drawRule(renderer, 0, y, width);
  y += kRuleWidth + 14;
  drawRow(screen, y, &icon_cards_24, tr(STR_CARDS_BACK_TO_DECKS), focus == RowToDecks, nullptr, nullptr, RowToDecks);
  y += kRowHeight;
  if (summary.fresh > 0) {
    char left[32];
    snprintf(text, sizeof(text), tr(STR_CARDS_MORE_NEW), cards::EXTRA_NEW);
    snprintf(left, sizeof(left), tr(STR_CARDS_LEFT), summary.fresh);
    drawRow(screen, y, &icon_bar_new_game_24, text, focus == RowDoneMoreNew, left, nullptr, RowDoneMoreNew);
    y += kRowHeight;
  }
  if (store->canUndo()) {
    drawRow(screen, y, &icon_bar_undo_24, tr(STR_CARDS_UNDO_LAST), focus == RowUndo, nullptr, nullptr, RowUndo);
  }
}
void CardsActivity::buildCard(UiScreen& screen) {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const bool touch = hasTouch();
  const bool back = view == Screen::Back;
  drawStatusLine(renderer);

  int exitWidth = 0;
  if (touch) {
    const char* exitLabel = tr(STR_GAMES_EXIT);
    renderer.drawText(kText12Font, kSidePadding, kGameTopLineY, exitLabel);
    exitWidth = renderer.getTextWidth(kText12Font, exitLabel) + 2 * kSidePadding;
    screen.frame().hit(fui::makeRect(0, 0, exitWidth, kGameBoardTop), ACTION_EXIT, 0, fui::InputTouch);
  }
  char text[48];
  snprintf(text, sizeof(text), tr(STR_CARDS_OF), store->position() + 1, store->length());
  renderer.drawText(kText12Font, width - kSidePadding - renderer.getTextWidth(kText12Font, text, kSemiBold),
                    kGameTopLineY, text, true, kSemiBold);

  // Touch grades on the games' action bar with the intervals in a line over
  // it; button boards say what the keys do above the hints. The line wraps
  // once when the intervals run long.
  // The bar runs to the screen bottom; the key line keys to the hint band.
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int panelTop = touch ? height - games::ui::kActionBarHeight : safe.y + safe.height;
  const int noteLine = renderer.getLineHeight(kDenseFont);
  const int noteTop = hintCaptionTop(renderer, kDenseFont, panelTop);
  const int zoneBottom = noteTop - noteLine - kNoteGap;
  const int frontBottom = touch ? kFrontBottomTouch : kFrontBottomButtons;

  // The front stays put when the answer shows, so the flip is a FAST refresh.
  Block front;
  front.text = sideText(*fields, 0);
  for (const int font : kFrontFonts) {
    layoutBlock(renderer, front, font, frontBottom - kGameBoardTop, scratch);
    if (!front.lines.overflow) break;
  }
  drawBlock(renderer, front, kGameBoardTop + (frontBottom - kGameBoardTop - front.height()) / 2, scratch);

  games::ui::ActionSlot slots[3] = {};
  if (!back) {
    if (touch) {
      const char* hint = tr(STR_CARDS_TAP_TO_SHOW);
      // The text role of the games' captions.
      renderer.drawText(kText10Font, (width - renderer.getTextWidth(kText10Font, hint)) / 2,
                        (frontBottom + zoneBottom) / 2 + 20, hint);
      screen.frame().hit(fui::makeRect(exitWidth, kGameBoardTop, width - exitWidth, panelTop - kGameBoardTop),
                         ACTION_CARD, 0, fui::InputTouch);
    }
    if (store->canUndo()) {
      slots[0] = {&icon_bar_undo_24, tr(STR_CARDS_UNDO)};
      if (!touch) renderer.drawText(kDenseFont, kSidePadding, noteTop, tr(STR_CARDS_UNDO_HOLD));
    }
    slots[1] = {&icon_show_24, tr(STR_CARDS_SHOW)};
  } else {
    drawRule(renderer, 60, frontBottom + 12, width - 120);
    const int zoneTop = frontBottom + 12 + kRuleWidth + 12;
    const int zone = zoneBottom - 10 - zoneTop;
    Block answer;
    Block extra;
    answer.text = sideText(*fields, 1);
    extra.text = sideText(*fields, 2);
    extra.style = EpdFontFamily::ITALIC;
    int total = 0;
    // The example only where a line of it still fits.
    for (const auto& fonts : kBackFonts) {
      layoutBlock(renderer, answer, fonts[0], zone, scratch);
      layoutBlock(renderer, extra, fonts[1], zone - answer.height() - kAnswerGap, scratch, false);
      total = answer.height() + (extra.lines.count > 0 ? kAnswerGap + extra.height() : 0);
      if (!answer.lines.overflow && !extra.lines.overflow && total <= zone) break;
    }
    // An example with no room left is cut too: the answer says so.
    if (extra.lines.count == 0 && *extra.text) answer.lines.overflow = true;
    int y = zoneTop + std::max(0, (zone - total) / 2);
    drawBlock(renderer, answer, y, scratch);
    if (extra.lines.count > 0) drawBlock(renderer, extra, y + answer.height() + kAnswerGap, scratch);

    const cards::CardState state = store->currentState();
    const StrId labels[3] = {StrId::STR_CARDS_AGAIN, StrId::STR_CARDS_GOOD, StrId::STR_CARDS_EASY};
    const freeink::Icon* icons[3] = {&icon_bar_restart_24, &icon_good_24, &icon_easy_24};
    // "Again: today · Good: tomorrow · …" in the order of the slots, or of the keys under it.
    char parts[3][64];
    for (int i = 0; i < 3; i++) {
      slots[i] = {icons[i], I18N.get(labels[i])};
      char interval[40];
      intervalText(cards::intervalAfter(state.box, static_cast<cards::Grade>(i)), interval, sizeof(interval));
      snprintf(parts[i], sizeof(parts[i]), "%s: %s", slots[i].label, interval);
    }
    const char* order[4] = {};
    int count = 0;
    if (touch) {
      for (const auto& part : parts) order[count++] = part;
    } else {
      const auto keys = mappedInput.mapLabels("", parts[1], parts[0], parts[2]);
      for (const char* part : {keys.btn1, keys.btn2, keys.btn3, keys.btn4}) {
        if (part != nullptr && *part != '\0') order[count++] = part;
      }
    }
    // Breaks only between pairs; the last line sits on noteTop.
    const int maxWidth = width - 2 * kSidePadding;
    const int separator = renderer.getTextWidth(kDenseFont, " · ");
    bool breakBefore[4] = {};
    int lineWidth = 0;
    int breaks = 0;
    for (int i = 0; i < count; i++) {
      const int partWidth = renderer.getTextWidth(kDenseFont, order[i]);
      if (i > 0 && lineWidth + separator + partWidth > maxWidth) {
        breakBefore[i] = true;
        breaks++;
        lineWidth = partWidth;
      } else {
        lineWidth += (i > 0 ? separator : 0) + partWidth;
      }
    }
    y = noteTop - breaks * noteLine;
    scratch[0] = '\0';
    for (int i = 0; i < count; i++) {
      if (breakBefore[i]) {
        renderer.drawText(kDenseFont, kSidePadding, y, scratch);
        y += noteLine;
        scratch[0] = '\0';
      }
      const size_t used = strlen(scratch);
      snprintf(scratch + used, kNameBytes - used, "%s%s", used > 0 ? " · " : "", order[i]);
    }
    renderer.drawText(kDenseFont, kSidePadding, y, scratch);
  }
  if (!touch) return;

  for (int i = 0; i < 3; i++) {
    slots[i].action = ACTION_SLOT;
    slots[i].value = static_cast<int16_t>(i);
  }
  games::ui::drawActionBar(renderer, screen, panelTop, slots, 3, -1, fui::InputTouch);
}
