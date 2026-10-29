#pragma once

#include <CardDeck.h>
#include <CardStore.h>
#include <I18n.h>
#include <Icon.h>

#include <atomic>
#include <memory>
#include <string>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "components/setka/SetkaStyle.h"

// «Карточки» under «Ещё»: the deck list (or how to add decks), a deck's
// overview, the study session and its summary (ui-style Setka). Card text is
// set in the reader's Noto Sans. Progress lives in cards::DeckStore; only the
// open deck, its current card and the session queue stay in RAM.
//
// Keys (the X4 has no touch): Left/Right and the side buttons move the focus,
// Confirm acts, Back goes up a level. In a session Confirm shows the answer,
// then Left grades «Снова», Confirm «Хорошо», Right «Легко»; holding Back
// undoes the last answer. Input is ignored for FLIP_GUARD_MS after the answer
// is on screen.
class CardsActivity final : public Activity, protected UiAppHost {
 public:
  // deckFile: a file in cards::deckDir() to open straight at its overview.
  CardsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string deckFile = {});
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // «Ещё» row status: cards due today over all decks, "" when unknown.
  static void dueTodayText(char* out, size_t cap);

 private:
  enum class Screen : uint8_t { Loading, List, Empty, Overview, Front, Back, Done, Message };
  enum class Job : uint8_t { None, LoadList, OpenDeck };
  enum Repaint : uint8_t { RepaintNone, RepaintUpdate, RepaintEntry };

  static constexpr int MAX_DECKS = 64;
  static constexpr uint32_t FLIP_GUARD_MS = 300;
  static constexpr unsigned long UNDO_HOLD_MS = 700;

  struct DeckRow {
    char name[72];  // file name without its extension, maybe cut
    uint32_t id;    // cards::deckId of the file name
    uint32_t size;
    uint32_t time;
    char caption[56];
  };

  // Loop task.
  void loadList();
  void openDeck();
  void showOverview();
  bool reopen(cards::Reindex reindex);
  void startSession(uint16_t extraNew);
  void showCard(bool entry);
  void grade(cards::Grade g);
  void finishSession();
  void activate(int index);
  void goBack();
  void moveFocus(int delta);
  bool shown(int index) const;
  void showMessage(StrId text);
  void showStatus(cards::Status status);
  void runAfterPaint(Job job, StrId text);
  void setScreen(Screen next, int focusIndex);
  void requestRepaint(Repaint kind);
  uint16_t realDay() const;
  int focusCount() const;
  bool hasTouch() const;
  bool findDeckFile(uint32_t id, std::string& out) const;

  // Render task.
  static void screenFn(UiScreen& screen, void* user);
  void build(UiScreen& screen);
  void buildList(UiScreen& screen);
  void buildEmpty(UiScreen& screen);
  void buildOverview(UiScreen& screen);
  void buildCard(UiScreen& screen);
  void buildDone(UiScreen& screen);
  int headerBottom() const;
  void drawRow(UiScreen& screen, int top, const freeink::Icon* icon, const char* label, bool focused,
               const char* status = nullptr, const char* caption = nullptr, int value = 0);
  void drawStats(int y, const uint16_t (&numbers)[3], const StrId (&labels)[3]) const;

  std::string deckFile;
  const bool direct;  // opened from the file list, not from the deck list

  Screen view = Screen::Loading;
  std::atomic<Job> job{Job::None};
  std::atomic<bool> jobPainted{false};
  StrId message = StrId::STR_CARDS_PREPARING;
  int focus = 0;
  int listTop = 0;  // first deck row shown

  std::unique_ptr<DeckRow[]> decks;
  int deckCount = 0;
  int deckFocus = 0;

  std::unique_ptr<cards::DeckStore> store;
  cards::Summary summary;
  char deckTitle[72] = {};
  std::unique_ptr<cards::Fields> fields;
  // The guard starts when the answer is on screen, not when it is asked for.
  std::atomic<bool> flipPending{false};
  std::atomic<uint32_t> flipShownMs{0};
  uint32_t sessionStartMs = 0;
  uint32_t sessionMs = 0;
  uint16_t sessionCards = 0;
  uint16_t sessionCounts[3] = {};

  char scratch[256] = {};         // render task: one line of card text
  setka::RefreshCadence cadence;  // render task only
  std::atomic<uint8_t> repaint{RepaintEntry};
};
