#include <HalStorage.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <memory>
#include <string>

#include "CardStore.h"

using namespace cards;

namespace {

constexpr uint16_t DAY = 20500;
constexpr char DECK[] = "/Карточки/es.txt";

std::string pathOf(const char* ext) {
  char path[64];
  snprintf(path, sizeof(path), "%s/%08lx.%s", PROGRESS_DIR, static_cast<unsigned long>(deckId("es.txt")), ext);
  return path;
}

std::string deckText(const int cards) {
  std::string text = "#separator:tab\n#html:true\n";
  for (int i = 0; i < cards; i++) text += "front " + std::to_string(i) + "\tback " + std::to_string(i) + "\n";
  return text;
}

// A deep copy of the card, to step back to the moment of a crash.
std::map<std::string, std::shared_ptr<fake::Node>> snapshot() {
  std::map<std::string, std::shared_ptr<fake::Node>> copy;
  for (const auto& [path, node] : fake::files) copy[path] = std::make_shared<fake::Node>(*node);
  return copy;
}

std::string front(DeckStore& store) {
  auto fields = std::make_unique<Fields>();
  EXPECT_TRUE(store.loadCard(*fields));
  return fields->text[0];
}

Summary summary(DeckStore& store) {
  Summary s;
  EXPECT_TRUE(store.summarize(s));
  return s;
}

void addDeck(const std::string& text, const uint32_t time = 1) { fake::add(DECK, text, time); }

}  // namespace

TEST(CardStore, IndexesAndSummarizes) {
  fake::reset();
  addDeck(deckText(15) + "front 3\tduplicate\n\tno front\n");
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  const Summary s = summary(store);
  EXPECT_EQ(s.cards, 15);
  EXPECT_EQ(s.fresh, 15);
  EXPECT_EQ(s.newCards, DEFAULT_NEW_PER_DAY);
  EXPECT_EQ(s.reviews, 0);
  EXPECT_EQ(s.skipped, 1);
  EXPECT_FALSE(s.dateUnknown);
  EXPECT_TRUE(Storage.exists(pathOf("dat").c_str()));
}

TEST(CardStore, SessionSurvivesACrash) {
  fake::reset();
  addDeck(deckText(15));
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
    ASSERT_EQ(store.startSession(0), Status::Ok);
    EXPECT_EQ(store.length(), 10);
    EXPECT_EQ(front(store), "front 0");
    ASSERT_TRUE(store.grade(Grade::Good));
    EXPECT_EQ(front(store), "front 1");
    ASSERT_TRUE(store.grade(Grade::Again));
    EXPECT_EQ(store.length(), 11);
    for (int i = 2; i <= 4; i++) {
      EXPECT_EQ(front(store), "front " + std::to_string(i));
      ASSERT_TRUE(store.grade(Grade::Good));
    }
    // «Снова» brings the card back after three others.
    EXPECT_EQ(front(store), "front 1");
    EXPECT_EQ(store.currentState().box, 1 | AGAIN_FLAG);
    const auto crash = snapshot();
    store.close();
    fake::files = crash;
  }
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  EXPECT_FALSE(Storage.exists(pathOf("log").c_str()));
  const Summary s = summary(store);
  EXPECT_EQ(s.newCards, DEFAULT_NEW_PER_DAY - 5);
  EXPECT_EQ(s.reviews, 1);  // the «Снова» card, still due today
  EXPECT_EQ(s.tomorrow, 5);
  EXPECT_EQ(s.learning, 5);
  ASSERT_EQ(store.startSession(0), Status::Ok);
  EXPECT_EQ(store.length(), 6);
  EXPECT_EQ(front(store), "front 1");
}

TEST(CardStore, UndoRestoresTheCard) {
  fake::reset();
  addDeck(deckText(5));
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  ASSERT_EQ(store.startSession(0), Status::Ok);
  EXPECT_FALSE(store.canUndo());
  ASSERT_TRUE(store.grade(Grade::Again));
  EXPECT_EQ(store.length(), 6);
  ASSERT_TRUE(store.canUndo());
  ASSERT_TRUE(store.undo());
  EXPECT_FALSE(store.canUndo());
  EXPECT_EQ(store.position(), 0);
  EXPECT_EQ(store.length(), 5);
  EXPECT_EQ(store.graded(Grade::Again), 0);
  EXPECT_EQ(front(store), "front 0");
  EXPECT_EQ(store.currentState().box, 0);
  store.endSession();
  const Summary s = summary(store);
  EXPECT_EQ(s.fresh, 5);
  EXPECT_EQ(s.newCards, 5);
  EXPECT_EQ(s.reviews, 0);
}

TEST(CardStore, EditedDeckKeepsProgressByKey) {
  fake::reset();
  addDeck("A\ta\nB\tb\nC\tc\n");
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
    ASSERT_EQ(store.startSession(0), Status::Ok);
    ASSERT_TRUE(store.grade(Grade::Good));   // A: 1 day
    ASSERT_TRUE(store.grade(Grade::Again));  // B: back in the session
    ASSERT_TRUE(store.grade(Grade::Easy));   // C: 3 days
    ASSERT_TRUE(store.grade(Grade::Good));   // B again: 1 day
    EXPECT_TRUE(store.finished());
  }
  addDeck("C\tc\nX\tx\nA\tchanged answer\n", 2);
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY + 3), Status::Ok);
  const Summary s = summary(store);
  EXPECT_EQ(s.cards, 3);
  EXPECT_EQ(s.fresh, 1);
  EXPECT_EQ(s.reviews, 2);
  ASSERT_EQ(store.startSession(0), Status::Ok);
  ASSERT_EQ(store.length(), 3);
  EXPECT_EQ(front(store), "A");  // the most overdue first
  ASSERT_TRUE(store.grade(Grade::Good));
  EXPECT_EQ(front(store), "C");
  ASSERT_TRUE(store.grade(Grade::Good));
  EXPECT_EQ(front(store), "X");
  EXPECT_EQ(store.currentState().box, 0);
}

TEST(CardStore, RecoversTheTmpAndATornLog) {
  fake::reset();
  addDeck(deckText(4));
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
    ASSERT_EQ(store.startSession(0), Status::Ok);
    ASSERT_TRUE(store.grade(Grade::Easy));
    const auto crash = snapshot();
    store.close();
    fake::files = crash;
  }
  ASSERT_TRUE(Storage.rename(pathOf("dat").c_str(), (pathOf("dat") + ".tmp").c_str()));
  fake::files[pathOf("log")]->bytes.resize(fake::files[pathOf("log")]->bytes.size() + 5, 0x55);
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  EXPECT_FALSE(Storage.exists((pathOf("dat") + ".tmp").c_str()));
  const Summary s = summary(store);
  EXPECT_EQ(s.learning, 1);
  EXPECT_EQ(s.newCards, 3);
}

TEST(CardStore, FailedMergeKeepsTheLog) {
  fake::reset();
  addDeck(deckText(4));
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  ASSERT_EQ(store.startSession(0), Status::Ok);
  ASSERT_TRUE(store.grade(Grade::Good));
  const auto before = fake::files.at(pathOf("dat"))->bytes;
  fake::failClosePath = pathOf("dat") + ".tmp";
  store.endSession();
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_EQ(fake::files.at(pathOf("dat"))->bytes, before);
  EXPECT_TRUE(Storage.exists(pathOf("log").c_str()));
  EXPECT_FALSE(Storage.exists((pathOf("dat") + ".tmp").c_str()));

  DeckStore again;
  ASSERT_EQ(again.open("es.txt", DAY), Status::Ok);
  EXPECT_EQ(summary(again).learning, 1);
  EXPECT_EQ(summary(again).newCards, 3);
}

TEST(CardStore, DamagedProgressIsRebuilt) {
  fake::reset();
  addDeck(deckText(4));
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  }
  fake::files.at(pathOf("dat"))->bytes[60] ^= 0xFF;
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  EXPECT_EQ(summary(store).cards, 4);
}

TEST(CardStore, UnknownClockStartsAVirtualCalendar) {
  fake::reset();
  addDeck(deckText(3));
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", NO_DAY), Status::Ok);
    EXPECT_EQ(store.day(), 1);
    EXPECT_TRUE(summary(store).dateUnknown);
    ASSERT_EQ(store.startSession(0), Status::Ok);
    ASSERT_TRUE(store.grade(Grade::Good));
  }
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", NO_DAY), Status::Ok);
    EXPECT_EQ(store.day(), 1);
    EXPECT_EQ(summary(store).reviews, 0);  // nothing comes due while the date is unknown
    EXPECT_EQ(summary(store).newCards, 2);
  }
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  EXPECT_EQ(summary(store).reviews, 0);
  EXPECT_EQ(summary(store).tomorrow, 1);
  EXPECT_EQ(summary(store).newCards, 2);  // the virtual day's counters moved onto today
  store.close();
  ASSERT_EQ(store.open("es.txt", DAY + 1), Status::Ok);
  EXPECT_EQ(summary(store).reviews, 1);
  EXPECT_EQ(summary(store).newCards, 2);
}

TEST(CardStore, DayCountersAndNewPerDay) {
  fake::reset();
  addDeck(deckText(40));
  {
    DeckStore store;
    ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
    ASSERT_TRUE(store.setNewPerDay(20));
    ASSERT_EQ(store.startSession(0), Status::Ok);
    EXPECT_EQ(store.length(), 20);
    while (!store.finished()) ASSERT_TRUE(store.grade(Grade::Easy));
    store.endSession();
    EXPECT_EQ(summary(store).newCards, 0);
    ASSERT_EQ(store.startSession(EXTRA_NEW), Status::Ok);
    EXPECT_EQ(store.length(), EXTRA_NEW);
  }
  DeckStore store;
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  EXPECT_EQ(store.newPerDay(), 20);
  EXPECT_EQ(summary(store).newCards, 0);
  store.close();
  ASSERT_EQ(store.open("es.txt", DAY + 1), Status::Ok);
  EXPECT_EQ(summary(store).newCards, 20);
}

TEST(CardStore, LimitsAndTotals) {
  fake::reset();
  addDeck(std::string(MAX_DECK_BYTES + 1, 'a'));
  DeckStore store;
  EXPECT_EQ(store.open("es.txt", DAY), Status::TooLarge);
  EXPECT_EQ(store.open("missing.txt", DAY), Status::Missing);

  addDeck(deckText(MAX_CARDS + 5));
  ASSERT_EQ(store.open("es.txt", DAY), Status::Ok);
  EXPECT_EQ(summary(store).cards, MAX_CARDS);
  EXPECT_EQ(summary(store).rows, MAX_CARDS + 5);

  uint16_t total = 0;
  EXPECT_FALSE(loadDueTotal(DAY, total));
  saveDueTotal(DAY, 24);
  EXPECT_TRUE(loadDueTotal(DAY, total));
  EXPECT_EQ(total, 24);
  EXPECT_FALSE(loadDueTotal(DAY + 1, total));
}

TEST(CardStore, FailedAllocationIsReported) {
  fake::reset();
  addDeck(deckText(3));
  DeckStore store;
  fake::failAlloc = 1;  // the index buffers
  EXPECT_EQ(store.open("es.txt", DAY), Status::NoMemory);
  EXPECT_EQ(store.open("es.txt", DAY), Status::Ok);
}
