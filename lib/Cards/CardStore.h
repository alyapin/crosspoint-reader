#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "CardDeck.h"
#include "CardSchedule.h"

// Study progress of the decks in DECK_DIR, kept in PROGRESS_DIR per deck
// (<id> = FNV-1a of the deck's file name, little-endian, CRC32-checked):
//   <id>.dat  header (deck size and time, format, day counters) and one
//             12-byte record per card in file order: line offset, key, due
//             day, box, lapses; the records' CRC32 at the end. Replaced
//             through .tmp; a .tmp left alone by a cut replace is recovered.
//   <id>.log  every answer as it is given, 16 bytes with a sequence number,
//             appended; merged into the .dat when a session ends, on the next
//             open and every LOG_MERGE_AT answers. The .dat keeps the last
//             merged number, so a merge cut short is replayed safely, and a
//             crash costs at most the answer being written.
//   due.bin   the day and the cards due on it over all decks, for «Ещё».
// A card is keyed by its Anki guid, else by its cleaned front; a deck that
// was edited is re-indexed on open: added cards are new, removed ones are
// forgotten, moved ones keep their progress.
namespace cards {

inline constexpr char PROGRESS_DIR[] = "/.crosspoint/cards";
inline constexpr uint16_t REVIEWS_PER_DAY = 200;
inline constexpr uint8_t DEFAULT_NEW_PER_DAY = 10;
inline constexpr uint8_t NEW_PER_DAY_CHOICES[] = {5, 10, 20, 30};
inline constexpr uint8_t EXTRA_NEW = 10;
inline constexpr uint16_t LOG_MERGE_AT = 256;
inline constexpr uint16_t QUEUE_CAP = 320;
inline constexpr uint8_t AGAIN_GAP = 3;  // other cards before an «Снова» card returns

struct Summary {
  uint16_t cards = 0;
  uint16_t fresh = 0;
  uint16_t learning = 0;
  uint16_t known = 0;
  uint16_t reviews = 0;   // due today, within the daily limit
  uint16_t newCards = 0;  // new for today, within the daily limit
  uint16_t tomorrow = 0;  // reviews due by tomorrow
  uint16_t week = 0;      // reviews due within a week
  uint16_t skipped = 0;   // lines without a front
  uint16_t rows = 0;      // cards in the file, past MAX_CARDS too
  uint8_t newPerDay = DEFAULT_NEW_PER_DAY;
  bool dateUnknown = false;
  uint16_t dueToday() const { return reviews + newCards; }
};

enum class Status : uint8_t { Ok, Missing, TooLarge, NoMemory, Failed };

uint32_t deckId(const char* fileName);
// Cards due today over all decks, for «Ещё»; false when not saved for `day`.
bool loadDueTotal(uint16_t day, uint16_t& total);
void saveDueTotal(uint16_t day, uint16_t total);

class DeckStore {
 public:
  DeckStore();
  ~DeckStore();
  DeckStore(const DeckStore&) = delete;
  DeckStore& operator=(const DeckStore&) = delete;

  // Opens DECK_DIR/<fileName> on `realDay` (NO_DAY when the clock is unknown):
  // merges logged answers and re-indexes a deck that changed or was never
  // indexed. With reindex false a changed deck keeps its old index.
  Status open(const char* fileName, uint16_t realDay, bool reindex = true);
  void close();
  bool isOpen() const { return opened; }
  bool summarize(Summary& out);
  bool setNewPerDay(uint8_t count);
  uint8_t newPerDay() const;
  uint16_t day() const { return today; }

  // A session: due reviews, most overdue first, then new cards in file order.
  Status startSession(uint16_t extraNew);
  bool inSession() const { return queue != nullptr; }
  uint16_t position() const { return pos; }
  uint16_t length() const { return queueLen; }
  bool finished() const { return pos >= queueLen; }
  // The current card's text; false on a read error.
  bool loadCard(Fields& out);
  CardState currentState() const;
  // Logs the answer to the current card and moves on; false when it could not be stored.
  bool grade(Grade g);
  bool canUndo() const { return undoValid; }
  bool undo();
  uint16_t graded(Grade g) const { return counts[static_cast<int>(g)]; }
  // Merges the log; the progress stays on disk either way.
  void endSession();

 private:
  struct Item {
    uint16_t index;
    uint16_t due;
    uint8_t box;
    uint8_t lapses;
  };
  struct Header;

  bool readRecord(uint16_t index, uint32_t& offset, uint32_t& key);
  bool appendLog(uint32_t key, const CardState& state, uint8_t flags);
  bool merge(int32_t shift, bool force = false);
  Status rebuild(bool keepOld, uint32_t size, uint32_t time);
  void countAnswer(bool wasNew, int delta);

  std::unique_ptr<Header> head;
  bool opened = false;
  char datPath[48] = {};
  char logPath[48] = {};
  uint16_t today = NO_DAY;
  bool clockKnown = false;
  uint32_t nextSeq = 1;
  uint32_t sessionSeq = 1;  // answers from here on are already in the day counters
  uint16_t logCount = 0;

  std::unique_ptr<Item[]> queue;
  uint16_t queueLen = 0;
  uint16_t pos = 0;
  uint32_t currentKey = 0;
  uint32_t currentOffset = 0;
  int32_t loadedIndex = -1;
  uint16_t counts[3] = {};
  struct {
    uint16_t pos;
    Item item;
    int16_t againAt;  // the «Снова» copy, -1 for none
    Grade grade;
    uint32_t key;
  } last = {};
  bool undoValid = false;
};

}  // namespace cards
