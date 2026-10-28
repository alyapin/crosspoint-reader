#include "ReadingStats.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#if defined(ARDUINO_ARCH_ESP32)
#include "util/TaskWatchdog.h"
#else
inline void resetTaskWatchdogIfSubscribed() {}
#endif

namespace stats {
namespace {

constexpr char STATS_DIR[] = "/.crosspoint/stats";
constexpr char LOG_PATH[] = "/.crosspoint/stats/log.bin";
constexpr char TOTALS_PATH[] = "/.crosspoint/stats/totals.bin";
constexpr char BOOKS_PATH[] = "/.crosspoint/stats/books.bin";

constexpr uint8_t VERSION = 1;
constexpr char LOG_MAGIC[4] = {'S', 'L', 'O', 'G'};
constexpr char TOTALS_MAGIC[4] = {'S', 'T', 'O', 'T'};
constexpr char BOOKS_MAGIC[4] = {'S', 'B', 'O', 'K'};

constexpr uint16_t NO_BOOK_ID = 0xFFFF;
constexpr uint8_t SEGMENT_CONTINUES = 1;
constexpr uint8_t BOOK_MANUAL = 1;
// The book's file was deleted or replaced. Kept so its id is never reused and its
// finished date still counts.
constexpr uint8_t BOOK_REMOVED = 2;
constexpr uint8_t BOOK_OPENED = 4;

constexpr size_t LOG_HEADER = 8;
constexpr size_t LOG_RECORD = 24;
constexpr size_t TOTALS_HEADER = 24;
constexpr size_t TOTALS_SIZE = TOTALS_HEADER + DayWindow::ENCODED_SIZE + sizeof(uint32_t);
constexpr size_t BOOKS_FIXED_HEADER = 12;
constexpr size_t BOOKS_HEADER = BOOKS_FIXED_HEADER + MAX_SHELVES * SHELF_NAME_BYTES + sizeof(uint32_t);
constexpr size_t BOOK_FIXED = 34;
// Text lengths are stored in one byte each.
static_assert(PATH_BYTES <= 256 && TITLE_BYTES <= 256 && AUTHOR_BYTES <= 256);

uint32_t crc32(uint32_t crc, const void* data, const size_t len) {
  static constexpr uint32_t TABLE[16] = {0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, 0x76DC4190, 0x6B6B51F4,
                                         0x4DB26158, 0x5005713C, 0xEDB88320, 0xF00F9344, 0xD6D6A3E8, 0xCB61B38C,
                                         0x9B64C2B0, 0x86D3D2D4, 0xA00AE278, 0xBDBDF21C};
  const auto* p = static_cast<const uint8_t*>(data);
  crc = ~crc;
  for (size_t i = 0; i < len; i++) {
    crc = TABLE[(crc ^ p[i]) & 0x0F] ^ (crc >> 4);
    crc = TABLE[(crc ^ (p[i] >> 4)) & 0x0F] ^ (crc >> 4);
  }
  return ~crc;
}

void put16(uint8_t* p, const uint16_t v) { std::memcpy(p, &v, sizeof(v)); }
void put32(uint8_t* p, const uint32_t v) { std::memcpy(p, &v, sizeof(v)); }
uint16_t get16(const uint8_t* p) {
  uint16_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}
uint32_t get32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

// Copies at most cap-1 bytes, never splitting a UTF-8 sequence.
void copyText(char* out, const size_t cap, const char* text, size_t len) {
  if (len >= cap) {
    len = cap - 1;
    while (len > 0 && (static_cast<unsigned char>(text[len]) & 0xC0) == 0x80) len--;
  }
  std::memcpy(out, text, len);
  out[len] = '\0';
}
void copyText(char* out, const size_t cap, const std::string& text) { copyText(out, cap, text.data(), text.size()); }

uint32_t epoch32(const int64_t epoch) { return static_cast<uint32_t>(std::clamp<int64_t>(epoch, 0, UINT32_MAX)); }

// --- Atomic replacement -----------------------------------------------------

std::string withSuffix(const char* path, const char* suffix) { return std::string(path) + suffix; }

// A replacement interrupted after the old file moved aside: bring back the
// old copy, or the new one when only that survived.
void recover(const char* path) {
  if (Storage.exists(path)) return;
  for (const char* suffix : {".bak", ".tmp"}) {
    const std::string other = withSuffix(path, suffix);
    if (Storage.exists(other.c_str())) {
      if (!Storage.rename(other.c_str(), path)) LOG_ERR("STAT", "Cannot recover %s", other.c_str());
      return;
    }
  }
}

// Moves the written .tmp over `path`. keepOld keeps a damaged original as .bad.
bool commit(const char* path, const bool keepOld) {
  const std::string tmp = withSuffix(path, ".tmp");
  const std::string old = withSuffix(path, keepOld ? ".bad" : ".bak");
  if (Storage.exists(path)) {
    if (Storage.exists(old.c_str())) Storage.remove(old.c_str());
    if (!Storage.rename(path, old.c_str())) {
      LOG_ERR("STAT", "Cannot move aside %s", path);
      Storage.remove(tmp.c_str());
      return false;
    }
  }
  if (!Storage.rename(tmp.c_str(), path)) {
    LOG_ERR("STAT", "Cannot replace %s", path);
    Storage.rename(old.c_str(), path);
    return false;
  }
  if (!keepOld) Storage.remove(old.c_str());
  return true;
}

bool writeAll(HalFile& f, const void* data, const size_t size) {
  return size == 0 || f.write(static_cast<const uint8_t*>(data), size) == size;
}

bool readAll(HalFile& f, void* data, const size_t size) {
  return size == 0 || f.read(data, size) == static_cast<int>(size);
}

void keepAsBad(const char* path) {
  const std::string bad = withSuffix(path, ".bad");
  if (Storage.exists(bad.c_str())) Storage.remove(bad.c_str());
  if (!Storage.rename(path, bad.c_str())) LOG_ERR("STAT", "Cannot set aside damaged %s", path);
}

enum class Load : uint8_t { Ok, Missing, Damaged, Failed };

// --- Log ---------------------------------------------------------------------

struct LogEntry {
  uint32_t seq = 0;
  uint16_t bookId = NO_BOOK_ID;
  ReadingSegment segment;
};

void encodeLog(uint8_t (&out)[LOG_RECORD], const uint32_t seq, const uint16_t bookId, const ReadingSegment& s) {
  put32(out, seq);
  put32(out + 4, epoch32(s.startEpoch));
  put16(out + 8, s.day);
  put16(out + 10, bookId);
  put16(out + 12, s.seconds);
  put16(out + 14, s.pages);
  put16(out + 16, s.progressBp);
  out[18] = s.continues ? SEGMENT_CONTINUES : 0;
  out[19] = 0;
  put32(out + 20, crc32(0, out, 20));
}

bool decodeLog(const uint8_t (&in)[LOG_RECORD], LogEntry& e) {
  if (get32(in + 20) != crc32(0, in, 20) || get32(in) == 0) return false;
  e.seq = get32(in);
  e.segment.startEpoch = get32(in + 4);
  e.segment.day = get16(in + 8);
  e.bookId = get16(in + 10);
  e.segment.seconds = get16(in + 12);
  e.segment.pages = get16(in + 14);
  e.segment.progressBp = std::min<uint16_t>(get16(in + 16), 10000);
  e.segment.continues = (in[18] & SEGMENT_CONTINUES) != 0;
  return true;
}

using LogVisitor = bool (*)(const LogEntry& entry, void* ctx);

struct LogInfo {
  uint16_t count = 0;
  uint32_t lastSeq = 0;
  size_t validSize = 0;
  size_t fileSize = 0;
};

// Visits valid records in file order (ascending sequence). A record failing
// its CRC is skipped; a partial record ends the file.
Load scanLog(const LogVisitor visit, void* ctx, LogInfo* info = nullptr) {
  recover(LOG_PATH);
  if (!Storage.exists(LOG_PATH)) return Load::Missing;
  HalFile f;
  if (!Storage.openFileForRead("STAT", LOG_PATH, f)) return Load::Failed;
  uint8_t head[LOG_HEADER];
  if (!readAll(f, head, sizeof(head)) || std::memcmp(head, LOG_MAGIC, sizeof(LOG_MAGIC)) != 0 || head[4] != VERSION) {
    return Load::Damaged;
  }
  LogInfo scanned;
  scanned.fileSize = f.size();
  scanned.validSize = LOG_HEADER;
  uint8_t record[LOG_RECORD];
  while (true) {
    resetTaskWatchdogIfSubscribed();
    const int got = f.read(record, sizeof(record));
    if (got < 0) return Load::Failed;
    if (got != static_cast<int>(sizeof(record))) break;
    scanned.validSize += sizeof(record);
    LogEntry entry;
    if (!decodeLog(record, entry)) {
      LOG_ERR("STAT", "Skipping damaged log record at %u", static_cast<unsigned>(scanned.validSize));
      continue;
    }
    if (scanned.count < UINT16_MAX) scanned.count++;
    scanned.lastSeq = std::max(scanned.lastSeq, entry.seq);
    if (visit && !visit(entry, ctx)) break;
  }
  if (info) *info = scanned;
  return Load::Ok;
}

// --- Totals --------------------------------------------------------------

struct Totals {
  uint32_t applied = 0;
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;

  void add(const ReadingSegment& s, DayWindow& days) {
    days.add(s.day, s.seconds);
    seconds += s.seconds;
    pages += s.pages;
    if (!s.continues) sessions++;
  }
};

Load loadTotals(Totals& totals, DayWindow& days) {
  totals = Totals{};
  days = DayWindow{};
  recover(TOTALS_PATH);
  if (!Storage.exists(TOTALS_PATH)) return Load::Missing;
  auto data = makeUniqueNoThrow<uint8_t[]>(TOTALS_SIZE);
  if (!data) {
    LOG_ERR("STAT", "OOM: totals %u B", static_cast<unsigned>(TOTALS_SIZE));
    return Load::Failed;
  }
  HalFile f;
  if (!Storage.openFileForRead("STAT", TOTALS_PATH, f)) return Load::Failed;
  const uint8_t* p = data.get();
  if (f.size() != TOTALS_SIZE || !readAll(f, data.get(), TOTALS_SIZE) ||
      std::memcmp(p, TOTALS_MAGIC, sizeof(TOTALS_MAGIC)) != 0 || p[4] != VERSION ||
      get32(p + TOTALS_SIZE - 4) != crc32(0, p, TOTALS_SIZE - 4) ||
      !days.decode(p + TOTALS_HEADER, DayWindow::ENCODED_SIZE)) {
    LOG_ERR("STAT", "Damaged totals file");
    days = DayWindow{};
    return Load::Damaged;
  }
  totals.applied = get32(p + 8);
  totals.seconds = get32(p + 12);
  totals.pages = get32(p + 16);
  totals.sessions = get32(p + 20);
  return Load::Ok;
}

bool saveTotals(const Totals& totals, const DayWindow& days, const bool keepOld) {
  auto data = makeUniqueNoThrow<uint8_t[]>(TOTALS_SIZE);
  if (!data) {
    LOG_ERR("STAT", "OOM: totals %u B", static_cast<unsigned>(TOTALS_SIZE));
    return false;
  }
  uint8_t* p = data.get();
  std::memset(p, 0, TOTALS_HEADER);
  std::memcpy(p, TOTALS_MAGIC, sizeof(TOTALS_MAGIC));
  p[4] = VERSION;
  put32(p + 8, totals.applied);
  put32(p + 12, totals.seconds);
  put32(p + 16, totals.pages);
  put32(p + 20, totals.sessions);
  days.encode(p + TOTALS_HEADER);
  put32(p + TOTALS_SIZE - 4, crc32(0, p, TOTALS_SIZE - 4));
  Storage.mkdir(STATS_DIR);
  const std::string tmp = withSuffix(TOTALS_PATH, ".tmp");
  HalFile f;
  if (!Storage.openFileForWrite("STAT", tmp, f) || !writeAll(f, p, TOTALS_SIZE) || !f.close()) {
    LOG_ERR("STAT", "Cannot write totals");
    Storage.remove(tmp.c_str());
    return false;
  }
  return commit(TOTALS_PATH, keepOld);
}

// --- Catalog -------------------------------------------------------------

struct Catalog {
  uint32_t applied = 0;
  uint16_t nextId = 0;
  ShelfNames shelves;
  Load state = Load::Missing;
  bool damagedRecord = false;
  BookInfo book;   // scratch for walks and rewrites
  BookInfo found;  // the record an operation edits or appends
};

void encodeCatalogHeader(const Catalog& c, uint8_t* out) {
  std::memcpy(out, BOOKS_MAGIC, sizeof(BOOKS_MAGIC));
  out[4] = VERSION;
  out[5] = 0;
  put16(out + 6, c.nextId);
  put32(out + 8, c.applied);
  std::memcpy(out + BOOKS_FIXED_HEADER, c.shelves.names, sizeof(c.shelves.names));
  put32(out + BOOKS_HEADER - 4, crc32(0, out, BOOKS_HEADER - 4));
}

// Reads the header; on Missing or Damaged the catalog starts empty.
Load openCatalog(Catalog& c, HalFile& f) {
  c.applied = 0;
  c.nextId = 0;
  c.shelves = ShelfNames{};
  c.damagedRecord = false;
  recover(BOOKS_PATH);
  if (!Storage.exists(BOOKS_PATH)) return c.state = Load::Missing;
  if (!Storage.openFileForRead("STAT", BOOKS_PATH, f)) return c.state = Load::Failed;
  auto head = makeUniqueNoThrow<uint8_t[]>(BOOKS_HEADER);
  if (!head) {
    LOG_ERR("STAT", "OOM: catalog header %u B", static_cast<unsigned>(BOOKS_HEADER));
    return c.state = Load::Failed;
  }
  const uint8_t* p = head.get();
  if (!readAll(f, head.get(), BOOKS_HEADER) || std::memcmp(p, BOOKS_MAGIC, sizeof(BOOKS_MAGIC)) != 0 ||
      p[4] != VERSION || get32(p + BOOKS_HEADER - 4) != crc32(0, p, BOOKS_HEADER - 4)) {
    LOG_ERR("STAT", "Damaged catalog header");
    return c.state = Load::Damaged;
  }
  c.nextId = get16(p + 6);
  c.applied = get32(p + 8);
  std::memcpy(c.shelves.names, p + BOOKS_FIXED_HEADER, sizeof(c.shelves.names));
  for (auto& name : c.shelves.names) name[SHELF_NAME_BYTES - 1] = '\0';
  return c.state = Load::Ok;
}

enum class Read : uint8_t { Record, End, Damaged, Failed };

Read readBook(HalFile& f, BookInfo& b) {
  uint8_t fixed[BOOK_FIXED];
  const int got = f.read(fixed, sizeof(fixed));
  if (got == 0) return Read::End;
  if (got < 0) return Read::Failed;
  if (got != static_cast<int>(sizeof(fixed))) return Read::Damaged;
  const uint8_t pathLen = fixed[30];
  const uint8_t titleLen = fixed[31];
  const uint8_t authorLen = fixed[32];
  if (pathLen == 0 || titleLen >= TITLE_BYTES || authorLen >= AUTHOR_BYTES ||
      fixed[2] > static_cast<uint8_t>(BookStatus::Paused)) {
    return Read::Damaged;
  }
  uint8_t crcBytes[4];
  if (!readAll(f, b.path, pathLen) || !readAll(f, b.title, titleLen) || !readAll(f, b.author, authorLen) ||
      !readAll(f, crcBytes, sizeof(crcBytes))) {
    return Read::Damaged;
  }
  uint32_t crc = crc32(0, fixed, sizeof(fixed));
  crc = crc32(crc, b.path, pathLen);
  crc = crc32(crc, b.title, titleLen);
  crc = crc32(crc, b.author, authorLen);
  if (crc != get32(crcBytes)) return Read::Damaged;
  b.path[pathLen] = '\0';
  b.title[titleLen] = '\0';
  b.author[authorLen] = '\0';
  b.id = get16(fixed);
  b.status = static_cast<BookStatus>(fixed[2]);
  b.manualStatus = (fixed[3] & BOOK_MANUAL) != 0;
  b.removed = (fixed[3] & BOOK_REMOVED) != 0;
  b.opened = (fixed[3] & BOOK_OPENED) != 0;
  b.firstDay = get16(fixed + 4);
  b.finishedDay = get16(fixed + 6);
  b.shelves = get32(fixed + 8);
  b.seconds = get32(fixed + 12);
  b.pages = get32(fixed + 16);
  b.sessions = get16(fixed + 20);
  b.progressBp = get16(fixed + 22);
  b.lastReadEpoch = get32(fixed + 24);
  b.startProgressBp = std::min<uint16_t>(get16(fixed + 28), 10000);
  return Read::Record;
}

bool writeBook(HalFile& f, const BookInfo& b) {
  uint8_t fixed[BOOK_FIXED] = {};
  put16(fixed, b.id);
  fixed[2] = static_cast<uint8_t>(b.status);
  fixed[3] = (b.manualStatus ? BOOK_MANUAL : 0) | (b.removed ? BOOK_REMOVED : 0) | (b.opened ? BOOK_OPENED : 0);
  put16(fixed + 4, b.firstDay);
  put16(fixed + 6, b.finishedDay);
  put32(fixed + 8, b.shelves);
  put32(fixed + 12, b.seconds);
  put32(fixed + 16, b.pages);
  put16(fixed + 20, b.sessions);
  put16(fixed + 22, b.progressBp);
  put32(fixed + 24, epoch32(b.lastReadEpoch));
  const size_t pathLen = strnlen(b.path, PATH_BYTES - 1);
  const size_t titleLen = strnlen(b.title, TITLE_BYTES - 1);
  const size_t authorLen = strnlen(b.author, AUTHOR_BYTES - 1);
  put16(fixed + 28, b.startProgressBp);
  fixed[30] = static_cast<uint8_t>(pathLen);
  fixed[31] = static_cast<uint8_t>(titleLen);
  fixed[32] = static_cast<uint8_t>(authorLen);
  uint32_t crc = crc32(0, fixed, sizeof(fixed));
  crc = crc32(crc, b.path, pathLen);
  crc = crc32(crc, b.title, titleLen);
  crc = crc32(crc, b.author, authorLen);
  uint8_t crcBytes[4];
  put32(crcBytes, crc);
  return writeAll(f, fixed, sizeof(fixed)) && writeAll(f, b.path, pathLen) && writeAll(f, b.title, titleLen) &&
         writeAll(f, b.author, authorLen) && writeAll(f, crcBytes, sizeof(crcBytes));
}

using BookEdit = void (*)(BookInfo& book, void* ctx);

// Visits records until `visit` returns false. A damaged record ends the walk
// (its length cannot be trusted) and marks the catalog for keeping as .bad.
bool walkCatalog(Catalog& c, bool (*visit)(Catalog& c, void* ctx), void* ctx) {
  HalFile f;
  const Load state = openCatalog(c, f);
  if (state == Load::Failed) return false;
  if (state != Load::Ok) return true;
  while (true) {
    resetTaskWatchdogIfSubscribed();
    switch (readBook(f, c.book)) {
      case Read::Record:
        if (!visit(c, ctx)) return true;
        break;
      case Read::End:
        return true;
      case Read::Damaged:
        LOG_ERR("STAT", "Damaged catalog record after id %u", static_cast<unsigned>(c.book.id));
        c.damagedRecord = true;
        return true;
      case Read::Failed:
        return false;
    }
  }
}

// Streams books.bin into a replacement with the in-memory header, passing
// every record through `edit` and appending c.found when `append` is set. A
// damaged record ends the copy: the file is replaced with the readable prefix
// and kept as .bad, unless `keepOnDamage` leaves it as it is, returns false
// and sets c.damagedRecord.
bool rewriteCatalog(Catalog& c, const BookEdit edit, void* ctx, const bool append, const bool keepOnDamage = false) {
  Storage.mkdir(STATS_DIR);
  const std::string tmp = withSuffix(BOOKS_PATH, ".tmp");
  auto head = makeUniqueNoThrow<uint8_t[]>(BOOKS_HEADER);
  if (!head) {
    LOG_ERR("STAT", "OOM: catalog header %u B", static_cast<unsigned>(BOOKS_HEADER));
    return false;
  }
  encodeCatalogHeader(c, head.get());
  HalFile out;
  bool ok = Storage.openFileForWrite("STAT", tmp, out) && writeAll(out, head.get(), BOOKS_HEADER);
  head.reset();
  bool damaged = c.state == Load::Damaged;
  if (ok && c.state == Load::Ok) {
    HalFile in;
    ok = Storage.openFileForRead("STAT", BOOKS_PATH, in) && in.seekSet(BOOKS_HEADER);
    while (ok) {
      resetTaskWatchdogIfSubscribed();
      const Read r = readBook(in, c.book);
      if (r == Read::End) break;
      if (r == Read::Damaged) {
        damaged = true;
        if (keepOnDamage) {
          c.damagedRecord = true;
          ok = false;
        }
        break;
      }
      if (r == Read::Failed) {
        ok = false;
        break;
      }
      if (edit) edit(c.book, ctx);
      ok = writeBook(out, c.book);
    }
  }
  if (ok && append) ok = writeBook(out, c.found);
  if (!out.close()) ok = false;
  if (!ok) {
    LOG_ERR("STAT", "Cannot write catalog");
    Storage.remove(tmp.c_str());
    return false;
  }
  if (!commit(BOOKS_PATH, damaged || c.damagedRecord)) return false;
  c.state = Load::Ok;
  c.damagedRecord = false;
  return true;
}

// Replaces the record carrying c.found.id with c.found.
void replaceFound(BookInfo& book, void* ctx) {
  const auto& found = static_cast<Catalog*>(ctx)->found;
  if (book.id == found.id) book = found;
}

bool findById(Catalog& c, const int bookId) {
  bool hit = false;
  struct Ctx {
    int id;
    bool* hit;
  } ctx{bookId, &hit};
  if (!walkCatalog(
          c,
          [](Catalog& cat, void* raw) {
            auto* x = static_cast<Ctx*>(raw);
            if (cat.book.id != x->id || cat.book.removed) return true;
            cat.found = cat.book;
            *x->hit = true;
            return false;
          },
          &ctx)) {
    return false;
  }
  return hit;
}

bool findByPath(Catalog& c, const std::string& path, bool& hit) {
  hit = false;
  struct Ctx {
    const std::string* path;
    bool* hit;
  } ctx{&path, &hit};
  return walkCatalog(
      c,
      [](Catalog& cat, void* raw) {
        auto* x = static_cast<Ctx*>(raw);
        if (cat.book.removed || *x->path != cat.book.path) return true;
        cat.found = cat.book;
        *x->hit = true;
        return false;
      },
      &ctx);
}

// --- Live totals ---------------------------------------------------------

struct BookDelta {
  uint16_t id;
  uint16_t sessions;
  uint16_t progressBp;
  uint32_t seconds;
  uint32_t pages;
  uint32_t lastRead;
};

// Per-book sums of the log records not yet in the catalog. Bounded: a record
// for a book beyond CAP is not added (add() returns false).
struct BookDeltas {
  static constexpr size_t CAP = 64;
  BookDelta items[CAP];
  size_t count = 0;
  bool overflow = false;

  BookDelta* find(const uint16_t id) {
    for (size_t i = 0; i < count; i++) {
      if (items[i].id == id) return &items[i];
    }
    return nullptr;
  }
  bool add(const LogEntry& e) {
    BookDelta* d = find(e.bookId);
    if (!d) {
      if (count == CAP) {
        overflow = true;
        return false;
      }
      d = &items[count++];
      *d = BookDelta{e.bookId, 0, 0, 0, 0, 0};
    }
    d->seconds += e.segment.seconds;
    d->pages += e.segment.pages;
    if (!e.segment.continues && d->sessions < UINT16_MAX) d->sessions++;
    d->progressBp = e.segment.progressBp;
    if (e.segment.startEpoch > 0)
      d->lastRead = std::max(d->lastRead, epoch32(e.segment.startEpoch + e.segment.seconds));
    return true;
  }
};

void applyDelta(BookInfo& b, const BookDelta& d) {
  b.seconds += d.seconds;
  b.pages += d.pages;
  b.sessions = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, b.sessions + d.sessions));
  b.progressBp = d.progressBp;
  if (d.lastRead > b.lastReadEpoch) b.lastReadEpoch = d.lastRead;
}

struct DeltaScan {
  uint32_t after;
  int onlyId;  // ReadingStats::NO_BOOK for every book
  BookDeltas* deltas;
};

bool collectDeltas(const LogEntry& e, void* raw) {
  auto* scan = static_cast<DeltaScan*>(raw);
  if (e.seq <= scan->after || e.bookId == NO_BOOK_ID) return true;
  if (scan->onlyId != ReadingStats::NO_BOOK && e.bookId != scan->onlyId) return true;
  scan->deltas->add(e);
  return true;
}

bool trimShelfName(const char* name, char (&out)[SHELF_NAME_BYTES]) {
  if (!name) return false;
  size_t start = 0;
  size_t end = strlen(name);
  while (start < end && name[start] == ' ') start++;
  while (end > start && name[end - 1] == ' ') end--;
  if (start == end) return false;
  copyText(out, sizeof(out), name + start, end - start);
  return true;
}

}  // namespace

ReadingStats& ReadingStats::getInstance() {
  static ReadingStats instance;
  return instance;
}

bool ReadingStats::prepareLog() {
  if (logReady) return true;
  LogInfo info;
  const Load state = scanLog(nullptr, nullptr, &info);
  if (state == Load::Failed) return false;
  if (state == Load::Damaged) {
    LOG_ERR("STAT", "Damaged reading log header");
    keepAsBad(LOG_PATH);
    info = LogInfo{};
  } else if (state == Load::Ok && info.validSize != info.fileSize) {
    // A torn append: cut it off so the next record lands on a record boundary.
    HalFile f = Storage.open(LOG_PATH, O_RDWR);
    if (!f || !f.truncate(info.validSize)) {
      LOG_ERR("STAT", "Cannot trim reading log");
      return false;
    }
    f.close();
  }
  auto days = makeUniqueNoThrow<DayWindow>();
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!days || !catalog) {
    LOG_ERR("STAT", "OOM: preparing reading log");
    return false;
  }
  Totals totals;
  if (loadTotals(totals, *days) == Load::Failed) return false;
  HalFile f;
  if (openCatalog(*catalog, f) == Load::Failed) return false;
  // New records must sort after everything the other files already hold.
  nextSeq = std::max({info.lastSeq, totals.applied, catalog->applied}) + 1;
  logCount = info.count;
  logReady = true;
  return true;
}

bool ReadingStats::appendSegment(const int bookId, const ReadingSegment& segment) {
  if (segment.seconds == 0 && segment.pages == 0) return true;
  if (!prepareLog()) return false;
  if (logCount >= LOG_HARD_MAX) {
    LOG_ERR("STAT", "Reading log full; segment dropped");
    return false;
  }
  Storage.mkdir(STATS_DIR);
  HalFile f = Storage.open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) {
    LOG_ERR("STAT", "Cannot open reading log");
    return false;
  }
  bool ok = true;
  if (f.size() == 0) {
    uint8_t head[LOG_HEADER] = {};
    std::memcpy(head, LOG_MAGIC, sizeof(LOG_MAGIC));
    head[4] = VERSION;
    ok = writeAll(f, head, sizeof(head));
  }
  uint8_t record[LOG_RECORD];
  encodeLog(record, nextSeq, bookId >= 0 ? static_cast<uint16_t>(bookId) : NO_BOOK_ID, segment);
  ok = ok && writeAll(f, record, sizeof(record));
  if (!f.close() || !ok) {
    LOG_ERR("STAT", "Cannot append reading log");
    // Rescan and trim a torn record; one that reached the card counts, so a retry does not add it twice.
    const uint32_t written = nextSeq;
    logReady = false;
    return prepareLog() && nextSeq > written;
  }
  logCount++;
  nextSeq++;
  return true;
}

void ReadingStats::compactIfNeeded() {
  if (!prepareLog() || logCount < LOG_MAX) return;
  auto days = makeUniqueNoThrow<DayWindow>();
  auto catalog = makeUniqueNoThrow<Catalog>();
  auto deltas = makeUniqueNoThrow<BookDeltas>();
  if (!days || !catalog || !deltas) {
    LOG_ERR("STAT", "OOM: log compaction");
    return;
  }
  Totals totals;
  const Load totalsState = loadTotals(totals, *days);
  if (totalsState == Load::Failed) return;
  {
    HalFile f;
    if (openCatalog(*catalog, f) == Load::Failed) return;
  }
  if (catalog->state != Load::Ok) catalog->applied = nextSeq - 1;

  struct Fold {
    Totals* totals;
    DayWindow* days;
    BookDeltas* deltas;
    uint32_t catalogApplied;
    uint32_t through;
  } fold{&totals, days.get(), deltas.get(), catalog->applied, nextSeq - 1 - LOG_KEEP};
  if (scanLog(
          [](const LogEntry& e, void* raw) {
            auto* x = static_cast<Fold*>(raw);
            if (e.seq > x->through) return false;
            if (e.seq > x->catalogApplied && e.bookId != NO_BOOK_ID && !x->deltas->add(e)) {
              x->through = e.seq - 1;
              return false;
            }
            if (e.seq > x->totals->applied) x->totals->add(e.segment, *x->days);
            return true;
          },
          &fold) != Load::Ok) {
    return;
  }
  totals.applied = std::max(totals.applied, fold.through);
  if (!saveTotals(totals, *days, totalsState == Load::Damaged)) return;
  days.reset();

  catalog->applied = std::max(catalog->applied, fold.through);
  if (!rewriteCatalog(
          *catalog,
          [](BookInfo& book, void* raw) {
            if (const BookDelta* d = static_cast<BookDeltas*>(raw)->find(book.id)) applyDelta(book, *d);
          },
          deltas.get(), false, true)) {
    if (catalog->damagedRecord) {
      // The books behind a damaged record cannot be read: set the file aside
      // unchanged and keep every log record this time; the next compaction folds.
      LOG_ERR("STAT", "Damaged catalog record; compaction deferred");
      catalog->applied = fold.catalogApplied;
      rewriteCatalog(*catalog, nullptr, nullptr, false);
    }
    return;
  }
  deltas.reset();
  catalog.reset();

  // Keep the records past the fold point.
  const std::string tmp = withSuffix(LOG_PATH, ".tmp");
  struct Keep {
    HalFile out;
    uint32_t after;
    uint16_t count;
    bool ok;
  } keep{HalFile(), fold.through, 0, true};
  uint8_t head[LOG_HEADER] = {};
  std::memcpy(head, LOG_MAGIC, sizeof(LOG_MAGIC));
  head[4] = VERSION;
  keep.ok = Storage.openFileForWrite("STAT", tmp, keep.out) && writeAll(keep.out, head, sizeof(head));
  const Load scanned = !keep.ok ? Load::Failed
                                : scanLog(
                                      [](const LogEntry& e, void* raw) {
                                        auto* k = static_cast<Keep*>(raw);
                                        if (e.seq <= k->after) return true;
                                        uint8_t record[LOG_RECORD];
                                        encodeLog(record, e.seq, e.bookId, e.segment);
                                        k->ok = writeAll(k->out, record, sizeof(record));
                                        k->count++;
                                        return k->ok;
                                      },
                                      &keep);
  if (!keep.out.close() || !keep.ok || scanned != Load::Ok || !commit(LOG_PATH, false)) {
    LOG_ERR("STAT", "Cannot rewrite reading log");
    Storage.remove(tmp.c_str());
    logReady = false;
    return;
  }
  logCount = keep.count;
  LOG_INF("STAT", "Log compacted through seq %u, %u records kept", static_cast<unsigned>(fold.through),
          static_cast<unsigned>(keep.count));
}

bool ReadingStats::summarize(const uint16_t today, const uint16_t goalMinutes, StatsSummary& out, DayWindow& days) {
  out = StatsSummary{};
  out.today = today;
  out.goalMinutes = goalMinutes;
  if (!prepareLog()) return false;
  Totals totals;
  if (loadTotals(totals, days) == Load::Failed) return false;
  struct Replay {
    Totals* totals;
    DayWindow* days;
  } replay{&totals, &days};
  if (scanLog(
          [](const LogEntry& e, void* raw) {
            auto* r = static_cast<Replay*>(raw);
            if (e.seq > r->totals->applied) r->totals->add(e.segment, *r->days);
            return true;
          },
          &replay) == Load::Failed) {
    return false;
  }
  out.totalSeconds = totals.seconds;
  out.totalPages = totals.pages;
  out.sessions = totals.sessions;
  out.bestStreak = days.bestStreak();
  if (today != NO_DAY) {
    out.todaySeconds = days.secondsOn(today);
    out.goalMet = goalMinutes > 0 && out.todaySeconds >= goalMinutes * 60u;
    out.currentStreak = days.currentStreak(today);
    out.weekSeconds = days.secondsBetween(weekStart(today), today);
    out.monthSeconds = days.secondsBetween(monthStart(today), today);
    out.yearSeconds = days.secondsBetween(yearStart(today), today);
  }

  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: catalog walk");
    return false;
  }
  return walkCatalog(
      *catalog,
      [](Catalog& c, void* raw) {
        auto* s = static_cast<StatsSummary*>(raw);
        if (c.book.status == BookStatus::Reading && !c.book.removed) s->booksReading++;
        if (c.book.status == BookStatus::Finished) {
          s->booksFinished++;
          if (s->today != NO_DAY && c.book.finishedDay >= yearStart(s->today) && c.book.finishedDay <= s->today) {
            s->booksFinishedThisYear++;
          }
        }
        return true;
      },
      &out);
}

bool ReadingStats::findBook(const std::string& path, BookInfo& out) {
  if (!prepareLog()) return false;
  auto catalog = makeUniqueNoThrow<Catalog>();
  auto deltas = makeUniqueNoThrow<BookDeltas>();
  if (!catalog || !deltas) {
    LOG_ERR("STAT", "OOM: book lookup");
    return false;
  }
  bool hit = false;
  if (!findByPath(*catalog, path, hit) || !hit) return false;
  DeltaScan scan{catalog->applied, catalog->found.id, deltas.get()};
  if (scanLog(collectDeltas, &scan) == Load::Failed) return false;
  out = catalog->found;
  if (deltas->count > 0) applyDelta(out, deltas->items[0]);
  return true;
}

bool ReadingStats::forEachBook(const BookVisitor visit, void* ctx) {
  if (!visit || !prepareLog()) return false;
  auto catalog = makeUniqueNoThrow<Catalog>();
  auto deltas = makeUniqueNoThrow<BookDeltas>();
  if (!catalog || !deltas) {
    LOG_ERR("STAT", "OOM: book walk");
    return false;
  }
  // The header is read again by the walk; this read only finds the applied sequence number.
  {
    HalFile f;
    if (openCatalog(*catalog, f) == Load::Failed) return false;
  }
  DeltaScan scan{catalog->applied, NO_BOOK, deltas.get()};
  if (scanLog(collectDeltas, &scan) == Load::Failed) return false;
  if (deltas->overflow) LOG_ERR("STAT", "More than %u books in the log; some totals lag", BookDeltas::CAP);
  struct Walk {
    BookVisitor visit;
    void* ctx;
    BookDeltas* deltas;
  } walk{visit, ctx, deltas.get()};
  return walkCatalog(
      *catalog,
      [](Catalog& c, void* raw) {
        auto* w = static_cast<Walk*>(raw);
        if (c.book.removed) return true;
        if (const BookDelta* d = w->deltas->find(c.book.id)) applyDelta(c.book, *d);
        return w->visit(c.book, w->ctx);
      },
      &walk);
}

bool ReadingStats::loadShelves(ShelfNames& out) {
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: shelves");
    return false;
  }
  HalFile f;
  if (openCatalog(*catalog, f) == Load::Failed) return false;
  out = catalog->shelves;
  return true;
}

namespace {

using BookChange = bool (*)(BookInfo& book, void* ctx);

// Rewrites one record through `change`; skips the write when nothing changed.
bool editBook(const int bookId, const BookChange change, void* ctx) {
  if (bookId < 0) return false;
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: catalog edit");
    return false;
  }
  if (!findById(*catalog, bookId)) return false;
  if (!change(catalog->found, ctx)) return true;
  return rewriteCatalog(*catalog, replaceFound, catalog.get(), false);
}

// Header-only change (shelf names), with every record passed through `edit`.
// lastSeq becomes the applied sequence number of a catalog that starts over.
bool editShelves(const uint32_t lastSeq, bool (*change)(ShelfNames& shelves, void* ctx), const BookEdit edit,
                 void* ctx) {
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: shelf edit");
    return false;
  }
  {
    HalFile f;
    if (openCatalog(*catalog, f) == Load::Failed) return false;
  }
  if (catalog->state != Load::Ok) catalog->applied = lastSeq;
  if (!change(catalog->shelves, ctx)) return false;
  return rewriteCatalog(*catalog, edit, ctx, false);
}

bool pathUnder(const char* path, const std::string& prefix) {
  const size_t n = prefix.size();
  return std::strncmp(path, prefix.c_str(), n) == 0 && (path[n] == '\0' || path[n] == '/');
}

}  // namespace

int ReadingStats::trackBook(const std::string& path, const std::string& title, const std::string& author,
                            const uint16_t today, const int progressBp, const bool opening, const BookStatus seed) {
  if (path.empty() || path.size() >= PATH_BYTES) {
    LOG_ERR("STAT", "Book path not tracked (%u B)", static_cast<unsigned>(path.size()));
    return NO_BOOK;
  }
  if (!prepareLog()) return NO_BOOK;
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: catalog lookup");
    return NO_BOOK;
  }
  Catalog& c = *catalog;
  bool hit = false;
  if (!findByPath(c, path, hit)) return NO_BOOK;
  // Without a readable catalog, logged records name ids that no longer exist.
  if (c.state != Load::Ok) c.applied = nextSeq - 1;
  BookInfo& b = c.found;
  bool changed = !hit;
  if (!hit) {
    if (c.nextId >= MAX_BOOKS) {
      LOG_ERR("STAT", "Book catalog full");
      return NO_BOOK;
    }
    b = BookInfo{};
    b.id = c.nextId++;
    copyText(b.path, sizeof(b.path), path);
  }
  if (opening && b.status == BookStatus::None && !b.manualStatus) {
    b.status = BookStatus::Reading;
    changed = true;
  }
  if (seed != BookStatus::None && b.status == BookStatus::None && !b.manualStatus) {
    b.status = seed;
    b.progressBp = static_cast<uint16_t>(std::clamp(progressBp, 0, 10000));
    changed = true;
  }
  if (opening && !b.opened) {
    b.opened = true;
    b.firstDay = today;
    b.startProgressBp = static_cast<uint16_t>(std::clamp(progressBp, 0, 10000));
    changed = true;
  }
  char text[TITLE_BYTES];
  copyText(text, sizeof(text), title);
  if (text[0] != '\0' && std::strcmp(text, b.title) != 0) {
    std::memcpy(b.title, text, std::strlen(text) + 1);
    changed = true;
  }
  copyText(text, AUTHOR_BYTES, author);
  if (text[0] != '\0' && std::strcmp(text, b.author) != 0) {
    std::memcpy(b.author, text, std::strlen(text) + 1);
    changed = true;
  }
  if (changed && !rewriteCatalog(c, hit ? replaceFound : nullptr, &c, !hit)) return NO_BOOK;
  return b.id;
}

int ReadingStats::openBook(const std::string& path, const std::string& title, const std::string& author,
                           const uint16_t today, const int progressBp) {
  return trackBook(path, title, author, today, progressBp, true);
}

int ReadingStats::ensureBook(const std::string& path, const std::string& title, const std::string& author) {
  return trackBook(path, title, author, NO_DAY, 0, false);
}

bool ReadingStats::catalogMissing() {
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) return false;
  HalFile f;
  return openCatalog(*catalog, f) == Load::Missing;
}

int ReadingStats::seedBook(const std::string& path, const std::string& title, const std::string& author,
                           const int progressBp) {
  const BookStatus status = seedStatus(progressBp);
  if (status == BookStatus::None) return NO_BOOK;
  return trackBook(path, title, author, NO_DAY, progressBp, false, status);
}

uint32_t estimateSecondsLeft(const BookInfo& book) {
  if (book.seconds < MIN_PACE_SECONDS || book.progressBp >= 10000 ||
      book.progressBp < book.startProgressBp + MIN_PACE_PROGRESS_BP) {
    return 0;
  }
  const uint64_t left = static_cast<uint64_t>(book.seconds) * (10000u - book.progressBp) /
                        static_cast<uint32_t>(book.progressBp - book.startProgressBp);
  return static_cast<uint32_t>(std::min<uint64_t>(left, UINT32_MAX));
}

bool ReadingStats::markFinished(const int bookId, const uint16_t day) {
  uint16_t finishedDay = day;
  return editBook(
      bookId,
      [](BookInfo& b, void* raw) {
        if (b.status == BookStatus::Finished) return false;
        b.status = BookStatus::Finished;
        b.finishedDay = *static_cast<const uint16_t*>(raw);
        return true;
      },
      &finishedDay);
}

bool ReadingStats::setStatus(const int bookId, const BookStatus status, const uint16_t today) {
  if (status > BookStatus::Paused) return false;
  struct Change {
    BookStatus status;
    uint16_t today;
  } change{status, today};
  return editBook(
      bookId,
      [](BookInfo& b, void* raw) {
        const auto* x = static_cast<const Change*>(raw);
        if (b.status == x->status && b.manualStatus) return false;
        if (x->status == BookStatus::Finished && b.status != BookStatus::Finished) b.finishedDay = x->today;
        if (x->status != BookStatus::Finished) b.finishedDay = NO_DAY;
        b.status = x->status;
        b.manualStatus = true;
        return true;
      },
      &change);
}

bool ReadingStats::setOnShelf(const int bookId, const uint8_t shelf, const bool onShelf) {
  if (shelf >= MAX_SHELVES) return false;
  ShelfNames names;
  if (!loadShelves(names) || !names.used(shelf)) return false;
  const uint32_t bit = 1u << shelf;
  struct Change {
    uint32_t bit;
    bool on;
  } change{bit, onShelf};
  return editBook(
      bookId,
      [](BookInfo& b, void* raw) {
        const auto* x = static_cast<const Change*>(raw);
        const uint32_t next = x->on ? (b.shelves | x->bit) : (b.shelves & ~x->bit);
        if (next == b.shelves) return false;
        b.shelves = next;
        return true;
      },
      &change);
}

int ReadingStats::createShelf(const char* name) {
  struct Create {
    char name[SHELF_NAME_BYTES];
    int slot;
  } create{};
  create.slot = -1;
  if (!trimShelfName(name, create.name)) return -1;
  const bool ok = prepareLog() && editShelves(
                                      nextSeq - 1,
                                      [](ShelfNames& shelves, void* raw) {
                                        auto* x = static_cast<Create*>(raw);
                                        for (uint8_t i = 0; i < MAX_SHELVES; i++) {
                                          if (shelves.used(i)) continue;
                                          std::memcpy(shelves.names[i], x->name, SHELF_NAME_BYTES);
                                          x->slot = i;
                                          return true;
                                        }
                                        return false;
                                      },
                                      nullptr, &create);
  return ok ? create.slot : -1;
}

bool ReadingStats::renameShelf(const uint8_t shelf, const char* name) {
  struct Rename {
    char name[SHELF_NAME_BYTES];
    uint8_t shelf;
  } rename{};
  rename.shelf = shelf;
  if (shelf >= MAX_SHELVES || !trimShelfName(name, rename.name)) return false;
  return prepareLog() && editShelves(
                             nextSeq - 1,
                             [](ShelfNames& shelves, void* raw) {
                               auto* x = static_cast<Rename*>(raw);
                               if (!shelves.used(x->shelf)) return false;
                               std::memcpy(shelves.names[x->shelf], x->name, SHELF_NAME_BYTES);
                               return true;
                             },
                             nullptr, &rename);
}

bool ReadingStats::deleteShelf(const uint8_t shelf) {
  if (shelf >= MAX_SHELVES) return false;
  uint32_t bit = 1u << shelf;
  return prepareLog() && editShelves(
                             nextSeq - 1,
                             [](ShelfNames& shelves, void* raw) {
                               const uint32_t mask = *static_cast<uint32_t*>(raw);
                               for (uint8_t i = 0; i < MAX_SHELVES; i++) {
                                 if ((mask >> i) & 1u) {
                                   if (!shelves.used(i)) return false;
                                   shelves.names[i][0] = '\0';
                                 }
                               }
                               return true;
                             },
                             [](BookInfo& b, void* raw) { b.shelves &= ~*static_cast<uint32_t*>(raw); }, &bit);
}

void ReadingStats::forgetPath(const std::string& path) {
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: catalog forget");
    return;
  }
  bool hit = false;
  if (!findByPath(*catalog, path, hit) || !hit) return;
  struct Forget {
    const std::string* path;
  } forget{&path};
  if (!rewriteCatalog(
          *catalog,
          [](BookInfo& b, void* raw) {
            if (!b.removed && *static_cast<const Forget*>(raw)->path == b.path) b.removed = true;
          },
          &forget, false)) {
    LOG_ERR("STAT", "Cannot forget %s", path.c_str());
  }
}

void ReadingStats::movePath(const std::string& from, const std::string& to) {
  if (from.empty() || to.empty() || from == to) return;
  auto catalog = makeUniqueNoThrow<Catalog>();
  if (!catalog) {
    LOG_ERR("STAT", "OOM: catalog move");
    return;
  }
  bool hit = false;
  struct Find {
    const std::string* from;
    const std::string* to;
    bool* hit;
  } find{&from, &to, &hit};
  if (!walkCatalog(
          *catalog,
          [](Catalog& c, void* raw) {
            auto* x = static_cast<Find*>(raw);
            *x->hit = !c.book.removed && (pathUnder(c.book.path, *x->from) || pathUnder(c.book.path, *x->to));
            return !*x->hit;
          },
          &find) ||
      !hit) {
    return;
  }
  struct Move {
    const std::string* from;
    const std::string* to;
  } move{&from, &to};
  if (!rewriteCatalog(
          *catalog,
          [](BookInfo& b, void* raw) {
            const auto* x = static_cast<const Move*>(raw);
            if (b.removed) return;
            // A rename never lands on an existing file: a record here whose file is gone was replaced.
            if (pathUnder(b.path, *x->to)) {
              if (!Storage.exists(b.path)) b.removed = true;
              return;
            }
            if (!pathUnder(b.path, *x->from)) return;
            const std::string moved = *x->to + (b.path + x->from->size());
            if (moved.size() >= PATH_BYTES) {
              LOG_ERR("STAT", "Moved path too long, keeping %s", b.path);
              return;
            }
            copyText(b.path, sizeof(b.path), moved);
          },
          &move, false)) {
    LOG_ERR("STAT", "Cannot follow move %s -> %s", from.c_str(), to.c_str());
  }
}

}  // namespace stats
