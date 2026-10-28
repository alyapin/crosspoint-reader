#include <HalStorage.h>
#include <ReadingStats.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using stats::BookInfo;
using stats::BookStatus;
using stats::ReadingStats;

namespace {

constexpr char LOG[] = "/.crosspoint/stats/log.bin";
constexpr char TOTALS[] = "/.crosspoint/stats/totals.bin";
constexpr char BOOKS[] = "/.crosspoint/stats/books.bin";

const uint16_t TODAY = static_cast<uint16_t>(stats::daysFromCivil(2026, 10, 28));

ReadingStats& freshStore() {
  fake::reset();
  READING_STATS.reset();
  return READING_STATS;
}

// Simulates a reboot: in-memory state is gone, files stay.
ReadingStats& reboot() {
  READING_STATS.reset();
  return READING_STATS;
}

ReadingSegment segment(const uint16_t day, const uint16_t seconds, const uint16_t pages = 1,
                       const bool continues = false, const uint16_t progress = 0) {
  ReadingSegment s;
  s.startEpoch = day == stats::NO_DAY ? 0 : static_cast<int64_t>(day) * 86400 + 3600;
  s.day = day;
  s.seconds = seconds;
  s.pages = pages;
  s.continues = continues;
  s.progressBp = progress;
  return s;
}

stats::StatsSummary summary(ReadingStats& store, DayWindow& days, const uint16_t today = TODAY) {
  stats::StatsSummary out;
  EXPECT_TRUE(store.summarize(today, 30, out, days));
  return out;
}

BookInfo book(ReadingStats& store, const std::string& path) {
  BookInfo info;
  EXPECT_TRUE(store.findBook(path, info)) << path;
  return info;
}

std::vector<std::string> paths(ReadingStats& store) {
  std::vector<std::string> out;
  store.forEachBook(
      [](const BookInfo& b, void* raw) {
        static_cast<std::vector<std::string>*>(raw)->push_back(b.path);
        return true;
      },
      &out);
  return out;
}

}  // namespace

TEST(ReadingStatsStore, FirstOpenStartsReadingAndKeepsIdentity) {
  auto& store = freshStore();
  const int a = store.openBook("/Books/a.epub", "Анна Каренина", "Толстой", TODAY, 0);
  const int b = store.openBook("/Books/b.epub", "B", "", TODAY, 0);
  ASSERT_GE(a, 0);
  EXPECT_NE(a, b);
  EXPECT_EQ(store.openBook("/Books/a.epub", "Анна Каренина", "Толстой", TODAY + 1, 0), a);
  const BookInfo info = book(store, "/Books/a.epub");
  EXPECT_EQ(info.status, BookStatus::Reading);
  EXPECT_FALSE(info.manualStatus);
  EXPECT_EQ(info.firstDay, TODAY);
  EXPECT_STREQ(info.title, "Анна Каренина");
  EXPECT_STREQ(info.author, "Толстой");
  // Opening again must not rewrite an unchanged catalog.
  const unsigned writes = fake::writesByPath[std::string(BOOKS) + ".tmp"];
  store.openBook("/Books/a.epub", "Анна Каренина", "Толстой", TODAY, 0);
  EXPECT_EQ(fake::writesByPath[std::string(BOOKS) + ".tmp"], writes);
}

TEST(ReadingStatsStore, TodayGoalStreakAndTotals) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY - 2, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY - 2, 400, 3)));
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY - 1, 300, 2)));
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 900, 5)));
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 1000, 4, true)));
  ASSERT_TRUE(store.appendSegment(ReadingStats::NO_BOOK, segment(stats::NO_DAY, 50, 0)));

  DayWindow days;
  const auto s = summary(reboot(), days);
  EXPECT_EQ(s.todaySeconds, 1900u);
  EXPECT_TRUE(s.goalMet);
  EXPECT_EQ(s.currentStreak, 3u);
  EXPECT_EQ(s.bestStreak, 3u);
  EXPECT_EQ(s.weekSeconds, 2600u);
  EXPECT_EQ(s.totalSeconds, 2650u);  // undated reading counts in the lifetime total only
  EXPECT_EQ(s.sessions, 4u);
  EXPECT_EQ(s.booksReading, 1u);
  EXPECT_EQ(days.secondsOn(TODAY - 1), 300u);

  const BookInfo info = book(store, "/a.epub");
  EXPECT_EQ(info.seconds, 2600u);
  EXPECT_EQ(info.pages, 14u);
  EXPECT_EQ(info.sessions, 3u);
}

TEST(ReadingStatsStore, AutomaticAndManualStatus) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_TRUE(store.markFinished(a, TODAY));
  BookInfo info = book(store, "/a.epub");
  EXPECT_EQ(info.status, BookStatus::Finished);
  EXPECT_EQ(info.finishedDay, TODAY);
  DayWindow days;
  EXPECT_EQ(summary(store, days).booksFinishedThisYear, 1u);

  // Reading again: the reader moves it back; reopening does not touch a manual status.
  ASSERT_TRUE(store.setStatus(a, BookStatus::Reading, TODAY));
  info = book(store, "/a.epub");
  EXPECT_EQ(info.status, BookStatus::Reading);
  EXPECT_EQ(info.finishedDay, stats::NO_DAY);
  ASSERT_TRUE(store.setStatus(a, BookStatus::None, TODAY));
  store.openBook("/a.epub", "A", "", TODAY, 0);
  EXPECT_EQ(book(store, "/a.epub").status, BookStatus::None);

  // Reaching the end finishes a paused book too.
  ASSERT_TRUE(store.setStatus(a, BookStatus::Paused, TODAY));
  ASSERT_TRUE(store.markFinished(a, TODAY + 1));
  EXPECT_EQ(book(store, "/a.epub").finishedDay, TODAY + 1);

  // A book added from the library has no status until opened.
  const int b = store.ensureBook("/b.epub", "B", "");
  ASSERT_GE(b, 0);
  EXPECT_EQ(book(store, "/b.epub").status, BookStatus::None);
  EXPECT_EQ(store.openBook("/b.epub", "B", "", TODAY, 0), b);
  EXPECT_EQ(book(store, "/b.epub").status, BookStatus::Reading);
}

TEST(ReadingStatsStore, ShelvesRoundTrip) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  const int b = store.ensureBook("/b.epub", "B", "");
  const int fantasy = store.createShelf("  Фэнтези  ");
  const int later = store.createShelf("Потом");
  ASSERT_EQ(fantasy, 0);
  ASSERT_EQ(later, 1);
  EXPECT_EQ(store.createShelf("   "), -1);
  ASSERT_TRUE(store.setOnShelf(a, fantasy, true));
  ASSERT_TRUE(store.setOnShelf(b, fantasy, true));
  ASSERT_TRUE(store.setOnShelf(b, later, true));
  EXPECT_FALSE(store.setOnShelf(b, 7, true));  // no such shelf

  auto& again = reboot();
  stats::ShelfNames names;
  ASSERT_TRUE(again.loadShelves(names));
  EXPECT_STREQ(names.names[0], "Фэнтези");
  EXPECT_STREQ(names.names[1], "Потом");
  EXPECT_EQ(book(again, "/a.epub").shelves, 1u);
  EXPECT_EQ(book(again, "/b.epub").shelves, 3u);

  ASSERT_TRUE(again.renameShelf(1, "Отпуск"));
  ASSERT_TRUE(again.deleteShelf(0));
  ASSERT_TRUE(again.loadShelves(names));
  EXPECT_FALSE(names.used(0));
  EXPECT_STREQ(names.names[1], "Отпуск");
  EXPECT_EQ(book(again, "/a.epub").shelves, 0u);
  EXPECT_EQ(book(again, "/b.epub").shelves, 2u);
  EXPECT_EQ(again.createShelf("Новая"), 0);  // the freed slot is reused, with no books on it
  EXPECT_EQ(book(again, "/a.epub").shelves, 0u);
  ASSERT_TRUE(again.setOnShelf(b, 1, false));
  EXPECT_EQ(book(again, "/b.epub").shelves, 0u);
}

TEST(ReadingStatsStore, ShelfNameIsTruncatedOnACharacterBoundary) {
  auto& store = freshStore();
  const std::string longName(60, '\0');
  std::string cyr;
  for (int i = 0; i < 30; i++) cyr += "я";  // 60 bytes
  ASSERT_EQ(store.createShelf(cyr.c_str()), 0);
  stats::ShelfNames names;
  ASSERT_TRUE(store.loadShelves(names));
  EXPECT_EQ(std::string(names.names[0]).size(), 46u);
}

TEST(ReadingStatsStore, MovesFollowFilesAndFolders) {
  auto& store = freshStore();
  const int a = store.openBook("/Books/a.epub", "A", "", TODAY, 0);
  store.openBook("/Books/Sub/b.epub", "B", "", TODAY, 0);
  store.openBook("/Booksellers/c.epub", "C", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 600)));
  store.movePath("/Books/a.epub", "/read/a.epub");
  store.movePath("/Books", "/Library");
  EXPECT_EQ(paths(store), (std::vector<std::string>{"/read/a.epub", "/Library/Sub/b.epub", "/Booksellers/c.epub"}));
  EXPECT_EQ(book(store, "/read/a.epub").seconds, 600u);
  EXPECT_EQ(store.openBook("/read/a.epub", "A", "", TODAY, 0), a);
}

TEST(ReadingStatsStore, CompactionKeepsEveryTotal) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  const int b = store.openBook("/b.epub", "B", "", TODAY, 0);
  uint32_t expectedA = 0;
  for (uint16_t i = 0; i < ReadingStats::LOG_MAX; i++) {
    const uint16_t day = TODAY - (ReadingStats::LOG_MAX - 1 - i) / 4;
    ASSERT_TRUE(store.appendSegment(i % 2 ? b : a, segment(day, 100, 1, i % 4 >= 2, i)));
    if (i % 2 == 0) expectedA += 100;
  }
  DayWindow before;
  const auto s1 = summary(store, before);
  store.compactIfNeeded();
  const size_t logBytes = fake::files.at(LOG)->bytes.size();
  EXPECT_EQ(logBytes, 8u + ReadingStats::LOG_KEEP * 24u);
  ASSERT_TRUE(fake::files.count(TOTALS));

  DayWindow after;
  const auto s2 = summary(reboot(), after);
  EXPECT_EQ(s2.totalSeconds, s1.totalSeconds);
  EXPECT_EQ(s2.totalSeconds, ReadingStats::LOG_MAX * 100u);
  EXPECT_EQ(s2.sessions, s1.sessions);
  EXPECT_EQ(s2.todaySeconds, s1.todaySeconds);
  EXPECT_EQ(s2.currentStreak, s1.currentStreak);
  EXPECT_EQ(s2.currentStreak, ReadingStats::LOG_MAX / 4);
  EXPECT_EQ(s2.yearSeconds, s1.yearSeconds);
  for (uint16_t d = TODAY - 300; d <= TODAY; d++) EXPECT_EQ(after.secondsOn(d), before.secondsOn(d)) << d;
  const BookInfo info = book(store, "/a.epub");
  EXPECT_EQ(info.seconds, expectedA);
  EXPECT_EQ(info.progressBp, ReadingStats::LOG_MAX - 2);

  // New appends continue the sequence and are counted once.
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  DayWindow more;
  EXPECT_EQ(summary(reboot(), more).totalSeconds, ReadingStats::LOG_MAX * 100u + 100u);
}

TEST(ReadingStatsStore, CrashBetweenCompactionStepsNeitherLosesNorDoubles) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  for (uint16_t i = 0; i < ReadingStats::LOG_MAX; i++) ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 10)));
  const auto logBefore = fake::files.at(LOG)->bytes;
  store.compactIfNeeded();
  // Power cut before the log rewrite landed: the old log comes back next to the new totals and catalog.
  fake::files.at(LOG)->bytes = logBefore;
  DayWindow days;
  auto& again = reboot();
  EXPECT_EQ(summary(again, days).totalSeconds, ReadingStats::LOG_MAX * 10u);
  EXPECT_EQ(book(again, "/a.epub").seconds, ReadingStats::LOG_MAX * 10u);
  ASSERT_TRUE(again.appendSegment(a, segment(TODAY, 10)));
  again.compactIfNeeded();
  EXPECT_EQ(summary(reboot(), days).totalSeconds, ReadingStats::LOG_MAX * 10u + 10u);
}

TEST(ReadingStatsStore, InterruptedReplaceRecoversBackup) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_GE(a, 0);
  // Cut between moving books.bin aside and moving the new copy in.
  ASSERT_TRUE(Storage.rename(BOOKS, (std::string(BOOKS) + ".bak").c_str()));
  EXPECT_EQ(book(reboot(), "/a.epub").status, BookStatus::Reading);
  EXPECT_TRUE(fake::files.count(BOOKS));
}

TEST(ReadingStatsStore, TornLogTailIsTrimmedBeforeTheNextAppend) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 200)));
  auto& bytes = fake::files.at(LOG)->bytes;
  bytes.resize(bytes.size() - 5);  // power cut in the middle of the second record
  auto& again = reboot();
  ASSERT_TRUE(again.appendSegment(a, segment(TODAY, 300)));
  EXPECT_EQ(fake::files.at(LOG)->bytes.size(), 8u + 2 * 24u);
  DayWindow days;
  EXPECT_EQ(summary(again, days).todaySeconds, 400u);
}

TEST(ReadingStatsStore, DamagedRecordsAreSkippedOrSetAside) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  const int b = store.openBook("/b.epub", "B", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  ASSERT_TRUE(store.appendSegment(b, segment(TODAY, 200)));
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 300)));
  // Bit rot in the middle log record: only that record is lost.
  fake::files.at(LOG)->bytes[8 + 24 + 13] ^= 0xFF;
  DayWindow days;
  EXPECT_EQ(summary(reboot(), days).todaySeconds, 400u);

  // A damaged second catalog record: the first book stays, the file is kept as .bad on the next write.
  auto& books = fake::files.at(BOOKS)->bytes;
  const std::string bPath = "/b.epub";
  const auto at = std::search(books.begin(), books.end(), bPath.begin(), bPath.end());
  ASSERT_NE(at, books.end());
  *at = 'X';
  auto& again = reboot();
  EXPECT_EQ(paths(again), std::vector<std::string>{"/a.epub"});
  const int c = again.openBook("/c.epub", "C", "", TODAY, 0);
  EXPECT_GT(c, b);  // never reuses a lost id
  EXPECT_TRUE(fake::files.count(std::string(BOOKS) + ".bad"));
  EXPECT_EQ(paths(again), (std::vector<std::string>{"/a.epub", "/c.epub"}));
}

TEST(ReadingStatsStore, DamagedHeadersStartOverWithoutMisattribution) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  fake::files.at(BOOKS)->bytes[0] = 'X';
  auto& again = reboot();
  const int fresh = again.openBook("/new.epub", "N", "", TODAY, 0);
  EXPECT_EQ(fresh, 0);  // ids restart, but the old log record is not credited to it
  EXPECT_EQ(book(again, "/new.epub").seconds, 0u);
  EXPECT_TRUE(fake::files.count(std::string(BOOKS) + ".bad"));
  DayWindow days;
  EXPECT_EQ(summary(again, days).todaySeconds, 100u);  // the day still counts

  fake::files.at(LOG)->bytes[1] = 'X';
  auto& third = reboot();
  ASSERT_TRUE(third.appendSegment(fresh, segment(TODAY, 50)));
  EXPECT_TRUE(fake::files.count(std::string(LOG) + ".bad"));
  EXPECT_EQ(book(third, "/new.epub").seconds, 50u);

  fake::files[TOTALS] = std::make_shared<fake::Node>();
  fake::files[TOTALS]->bytes.assign(10, 0xAA);
  DayWindow days2;
  EXPECT_EQ(summary(reboot(), days2).todaySeconds, 50u);
}

TEST(ReadingStatsStore, PathsTooLongAreNotTracked) {
  auto& store = freshStore();
  EXPECT_EQ(store.openBook(std::string(300, 'a'), "A", "", TODAY, 0), ReadingStats::NO_BOOK);
  EXPECT_EQ(store.openBook("", "A", "", TODAY, 0), ReadingStats::NO_BOOK);
}

TEST(ReadingStatsStore, LostCatalogNeverCreditsOldReadingToNewBooks) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  fake::files.erase(BOOKS);
  auto& again = reboot();
  ASSERT_EQ(again.createShelf("Полка"), 0);  // a shelf edit starts the new catalog
  const int fresh = again.openBook("/new.epub", "N", "", TODAY, 0);
  EXPECT_EQ(fresh, 0);
  EXPECT_EQ(book(again, "/new.epub").seconds, 0u);
}

TEST(ReadingStatsStore, MoveOverAnExistingBookHidesTheReplacedOne) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  const int b = store.openBook("/b.epub", "B", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  ASSERT_TRUE(store.appendSegment(b, segment(TODAY, 200)));
  // WebDAV MOVE with Overwrite: /b.epub is deleted, then /a.epub takes its name.
  store.movePath("/a.epub", "/b.epub");
  EXPECT_EQ(paths(store), std::vector<std::string>{"/b.epub"});
  const BookInfo moved = book(store, "/b.epub");
  EXPECT_EQ(moved.id, a);
  EXPECT_EQ(moved.seconds, 100u);
  DayWindow days;
  EXPECT_EQ(summary(store, days).booksReading, 1u);
  EXPECT_EQ(store.openBook("/c.epub", "C", "", TODAY, 0), b + 1);
}

TEST(ReadingStatsStore, UndatedReadingKeepsTheLastReadTime) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 100)));
  ASSERT_TRUE(store.appendSegment(a, segment(stats::NO_DAY, 100)));
  ReadingSegment undated = segment(stats::NO_DAY, 50);
  undated.startEpoch = 0;
  ASSERT_TRUE(store.appendSegment(a, undated));
  EXPECT_EQ(book(store, "/a.epub").lastReadEpoch, static_cast<int64_t>(TODAY) * 86400 + 3600 + 100);
}

TEST(ReadingStatsStore, TimeLeftFollowsTheBooksOwnPace) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 2000);
  EXPECT_EQ(book(store, "/a.epub").startProgressBp, 2000u);
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 300, 10, false, 2400)));
  EXPECT_EQ(stats::estimateSecondsLeft(book(store, "/a.epub")), 0u);  // too little read to tell
  ASSERT_TRUE(store.appendSegment(a, segment(TODAY, 300, 10, false, 3000)));
  // 10 minutes moved it 10%: 70% left takes 70 minutes.
  EXPECT_EQ(stats::estimateSecondsLeft(book(store, "/a.epub")), 4200u);
  // Reopening keeps the starting point.
  store.openBook("/a.epub", "A", "", TODAY, 3000);
  EXPECT_EQ(book(store, "/a.epub").startProgressBp, 2000u);
}

TEST(ReadingStatsStore, DeletedBookLeavesListsButKeepsItsFinish) {
  auto& store = freshStore();
  const int a = store.openBook("/a.epub", "A", "", TODAY, 0);
  ASSERT_TRUE(store.markFinished(a, TODAY));
  const int b = store.openBook("/b.epub", "B", "", TODAY, 0);
  ASSERT_TRUE(store.appendSegment(b, segment(TODAY, 300)));
  store.forgetPath("/a.epub");
  store.forgetPath("/b.epub");
  EXPECT_TRUE(paths(store).empty());
  DayWindow days;
  const auto s = summary(store, days);
  EXPECT_EQ(s.booksFinished, 1u);
  EXPECT_EQ(s.booksReading, 0u);
  EXPECT_EQ(s.todaySeconds, 300u);
  // A different book downloaded to the same path starts afresh.
  const int again = store.openBook("/b.epub", "Other", "", TODAY, 0);
  EXPECT_GT(again, b);
  EXPECT_EQ(book(store, "/b.epub").seconds, 0u);
}
