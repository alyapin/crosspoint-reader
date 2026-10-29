#pragma once

#include <TrustedTime.h>

#include <cstddef>
#include <cstdint>
#include <string>

#include "DayWindow.h"
#include "ReadingTracker.h"
#include "StatsCalendar.h"

// Reading statistics, book statuses and shelves, kept on SD in
// /.crosspoint/stats/. Nothing stays in RAM between calls except the log's
// length and next sequence number; every query streams the files with small
// fixed buffers, so the store also fits the ESP32-C3.
//
// Files (little-endian, every record CRC32-checked):
//   log.bin     append-only journal of ReadingSegments: stretches of reading
//               in one book within one local day, cut every
//               ReadingTracker::SEGMENT_SECONDS, so a crash loses at most one. Each record has a sequence
//               number. Past LOG_MAX records the oldest are folded into the
//               two files below and dropped (compaction).
//   totals.bin  lifetime totals and a DayWindow of per-day seconds, as of
//               its applied sequence number.
//   books.bin   the book catalog: path, title, author, status, dates, shelf
//               membership and reading totals as of its own applied sequence
//               number, plus the shelf names.
//   seeded      empty; written once the books read before statistics started
//               are seeded.
//   seed_tries  one byte: boots that retried seeding after a read or write
//               failure.
// A query adds the log records past a file's applied sequence number, so the
// files may be rewritten in any order and a crash between two rewrites
// neither loses nor double-counts reading. totals.bin and books.bin are
// replaced through .tmp/.bak renames; a damaged file is kept as .bad and the
// store carries on with what still validates.
//
// A book is identified by its path, the same key RecentBooksStore and the
// book cache use; movePath() follows renames and moves the firmware performs.
//
// Read-only API for screens (all return false when SD storage fails):
//   summarize()     today's minutes against the daily goal, current and
//                   record streak, week/month/year/lifetime time, finished
//                   books; fills a caller-owned DayWindow for heatmaps and
//                   calendars (DayWindow::secondsOn(day)).
//   findBook()      one book's status, dates, shelves and time read.
//   forEachBook()   every catalog book with live totals, in first-seen order;
//                   filter by status or shelf in the visitor.
//   loadShelves()   shelf names; a book's shelves are BookInfo::shelves bits.
// Mutations for screens: ensureBook(), setStatus(), createShelf(),
// renameShelf(), deleteShelf(), setOnShelf().
// Reader hooks (ReaderActivity): openBook(), appendSegment(), markFinished(),
// compactIfNeeded().
namespace stats {

// The `today` argument: local day of the trusted clock, NO_DAY while unknown.
inline uint16_t today() { return localDay(trustedtime::trustedNow()); }

enum class BookStatus : uint8_t { None = 0, Reading = 1, Finished = 2, Paused = 3 };

// Status of a book already being read when statistics start: Reading, or
// Finished from FINISHED_BP; None without saved progress.
inline BookStatus seedStatus(const int progressBp) {
  if (progressBp <= 0) return BookStatus::None;
  return progressBp >= ReadingTracker::FINISHED_BP ? BookStatus::Finished : BookStatus::Reading;
}

inline constexpr uint8_t MAX_SHELVES = 32;
inline constexpr size_t SHELF_NAME_BYTES = 48;  // UTF-8 with its terminator
inline constexpr size_t PATH_BYTES = 256;
inline constexpr size_t TITLE_BYTES = 96;
inline constexpr size_t AUTHOR_BYTES = 64;

struct BookInfo {
  uint16_t id = 0;
  BookStatus status = BookStatus::None;
  bool manualStatus = false;  // the reader chose the status; openBook() leaves it
  bool removed = false;       // deleted or replaced; never visible through the API
  bool opened = false;        // opened in the reader at least once
  uint16_t firstDay = NO_DAY;
  uint16_t finishedDay = NO_DAY;  // set while status is Finished
  uint32_t shelves = 0;           // bit i: on shelf i
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint16_t sessions = 0;
  uint16_t progressBp = 0;       // at the end of the last segment read
  uint16_t startProgressBp = 0;  // when first opened
  int64_t lastReadEpoch = 0;
  char path[PATH_BYTES] = "";
  char title[TITLE_BYTES] = "";
  char author[AUTHOR_BYTES] = "";
};

struct StatsSummary {
  uint16_t today = NO_DAY;
  uint32_t todaySeconds = 0;
  uint16_t goalMinutes = 0;
  bool goalMet = false;
  uint16_t currentStreak = 0;
  uint16_t bestStreak = 0;
  uint32_t weekSeconds = 0;  // Monday to today
  uint32_t monthSeconds = 0;
  uint32_t yearSeconds = 0;
  uint32_t totalSeconds = 0;  // lifetime, undated reading included
  uint32_t totalPages = 0;
  uint32_t sessions = 0;
  uint16_t booksReading = 0;
  uint16_t booksFinished = 0;
  uint16_t booksFinishedThisYear = 0;
};

struct ShelfNames {
  char names[MAX_SHELVES][SHELF_NAME_BYTES] = {};  // "" = free slot
  bool used(uint8_t shelf) const { return shelf < MAX_SHELVES && names[shelf][0] != '\0'; }
};

// Returns false to stop the walk.
using BookVisitor = bool (*)(const BookInfo& book, void* ctx);

// Reading time left at the book's own pace since it was first opened; 0 until
// MIN_PACE_SECONDS of reading moved it MIN_PACE_PROGRESS_BP.
inline constexpr uint32_t MIN_PACE_SECONDS = 10 * 60;
inline constexpr uint16_t MIN_PACE_PROGRESS_BP = 100;
uint32_t estimateSecondsLeft(const BookInfo& book);

class ReadingStats {
 public:
  static constexpr uint16_t LOG_MAX = 1024;
  static constexpr uint16_t LOG_KEEP = 128;
  // Appends stop here when compaction keeps failing.
  static constexpr uint16_t LOG_HARD_MAX = 4096;
  static constexpr uint16_t MAX_BOOKS = 4096;
  static constexpr int NO_BOOK = -1;

  static ReadingStats& getInstance();

  // Reader hooks.
  // Catalog id for the book being opened, creating it as Reading when new
  // (or None and not chosen manually). NO_BOOK on storage failure.
  int openBook(const std::string& path, const std::string& title, const std::string& author, uint16_t today,
               int progressBp);
  bool appendSegment(int bookId, const ReadingSegment& segment);
  bool markFinished(int bookId, uint16_t day);
  // Folds old log records once the log reaches LOG_MAX.
  void compactIfNeeded();

  // Queries.
  bool summarize(uint16_t today, uint16_t goalMinutes, StatsSummary& out, DayWindow& days);
  bool findBook(const std::string& path, BookInfo& out);
  bool forEachBook(BookVisitor visit, void* ctx);
  bool loadShelves(ShelfNames& out);

  // Mutations. ensureBook() adds a book without changing its status.
  int ensureBook(const std::string& path, const std::string& title, const std::string& author);
  // First run: books read before statistics started are seeded until
  // markSeedingDone(), so seeding cut short by a reset resumes on the next boot.
  bool seedingDone();
  bool markSeedingDone();
  // Counts a boot whose seeding could not read or store everything; false once
  // enough such boots have passed, or the count cannot be stored, so seeding
  // stops retrying.
  bool consumeSeedRetry();
  // Such a book gets seedStatus(progressBp) and that progress, unless it
  // already has a status. False when it could not be stored (storage, memory
  // or a full catalog); nothing to seed counts as stored.
  bool seedBook(const std::string& path, const std::string& title, const std::string& author, int progressBp);
  bool setStatus(int bookId, BookStatus status, uint16_t today);
  // Free shelf slot holding `name`, or -1 when all are used or storage fails.
  int createShelf(const char* name);
  bool renameShelf(uint8_t shelf, const char* name);
  // Also takes every book off the shelf.
  bool deleteShelf(uint8_t shelf);
  bool setOnShelf(int bookId, uint8_t shelf, bool onShelf);

  // A book file or folder was renamed or moved.
  void movePath(const std::string& from, const std::string& to);
  // A book file was deleted: it leaves every list, and a new file at the same
  // path starts afresh. A finished book still counts as finished.
  void forgetPath(const std::string& path);

  // Forget cached log state, e.g. after the SD card changed under the store.
  void reset() {
    logReady = false;
    uncertain.seq = 0;
  }

 private:
  ReadingStats() = default;
  // Scans the log once: trims a torn tail and finds the next sequence number.
  bool prepareLog();
  int trackBook(const std::string& path, const std::string& title, const std::string& author, uint16_t today,
                int progressBp, bool opening, BookStatus seed = BookStatus::None);

  bool logReady = false;
  uint16_t logCount = 0;
  uint32_t nextSeq = 1;
  // An append whose record may have reached the card: retrying the same segment
  // checks the log instead of writing it a second time.
  struct {
    uint32_t seq = 0;
    int bookId = 0;
    ReadingSegment segment;
  } uncertain;
};

}  // namespace stats

#define READING_STATS stats::ReadingStats::getInstance()
