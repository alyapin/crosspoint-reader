#include "CardStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "CardWork.h"

namespace cards {

struct LogEntry {
  uint32_t key = 0;
  uint32_t seq = 0;
  CardState state;
  uint16_t day = NO_DAY;
  uint8_t flags = 0;
};

namespace {

constexpr char MAGIC[4] = {'C', 'R', 'D', '1'};
constexpr uint8_t VERSION = 1;
constexpr size_t FIXED = 48;
constexpr size_t DECK_NAME_MAX = 200;
constexpr size_t RECORD = 12;
constexpr size_t LOG_RECORD = 16;
constexpr uint16_t LOG_CHUNK = 256;
constexpr uint16_t LOG_HARD_MAX = 4096;
constexpr size_t IO_RECORDS = 16;
constexpr uint8_t LOG_UNDO = 1;
constexpr uint8_t LOG_WAS_NEW = 2;
constexpr uint8_t LOG_RELEARN = 4;  // an answer to a «Снова» copy: counts toward no limit
constexpr uint8_t FLAG_VIRTUAL = 1;
constexpr uint8_t FLAG_HTML = 2;
constexpr uint8_t FLAG_CP1251 = 4;
constexpr char DUE_PATH[] = "/.crosspoint/cards/due.bin";

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

void put16(uint8_t* p, const uint16_t v) { memcpy(p, &v, sizeof(v)); }
void put32(uint8_t* p, const uint32_t v) { memcpy(p, &v, sizeof(v)); }
uint16_t get16(const uint8_t* p) {
  uint16_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}
uint32_t get32(const uint8_t* p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

// Decks are at most 4 MiB: the offset's top 10 bits hold a mark of the card's
// answer (0: none), which tells cards with one front apart on re-index.
constexpr uint32_t OFFSET_BITS = 22;
constexpr uint32_t OFFSET_MASK = (1u << OFFSET_BITS) - 1;
static_assert(MAX_DECK_BYTES <= OFFSET_MASK + 1);

uint16_t answerMark(const uint32_t altKey) { return static_cast<uint16_t>(altKey % 1023 + 1); }

struct Record {
  uint32_t offset = 0;
  uint32_t key = 0;
  CardState state;
  uint16_t mark = 0;
};

void encodeRecord(uint8_t* p, const Record& r) {
  put32(p, (r.offset & OFFSET_MASK) | (static_cast<uint32_t>(r.mark) << OFFSET_BITS));
  put32(p + 4, r.key);
  put16(p + 8, r.state.due);
  p[10] = r.state.box;
  p[11] = r.state.lapses;
}

Record decodeRecord(const uint8_t* p) {
  Record r;
  r.offset = get32(p) & OFFSET_MASK;
  r.mark = static_cast<uint16_t>(get32(p) >> OFFSET_BITS);
  r.key = get32(p + 4);
  r.state.due = get16(p + 8);
  r.state.box = p[10];
  r.state.lapses = p[11];
  return r;
}

void encodeLog(uint8_t (&p)[LOG_RECORD], const LogEntry& e) {
  put32(p, e.key);
  put32(p + 4, e.seq);
  put16(p + 8, e.state.due);
  put16(p + 10, e.day);
  p[12] = e.state.box;
  p[13] = e.state.lapses;
  p[14] = e.flags;
  p[15] = static_cast<uint8_t>(crc32(0, p, 15));
}

bool decodeLog(const uint8_t* p, LogEntry& e) {
  if (p[15] != static_cast<uint8_t>(crc32(0, p, 15)) || get32(p + 4) == 0) return false;
  e.key = get32(p);
  e.seq = get32(p + 4);
  e.state.due = get16(p + 8);
  e.day = get16(p + 10);
  e.state.box = p[12];
  e.state.lapses = p[13];
  e.flags = p[14];
  return true;
}

struct OldState {
  uint32_t key;
  CardState state;
  uint16_t mark;
};

bool readAll(HalFile& f, void* data, const size_t size) {
  return size == 0 || f.read(data, size) == static_cast<int>(size);
}

bool writeAll(HalFile& f, const void* data, const size_t size) {
  return size == 0 || f.write(static_cast<const uint8_t*>(data), size) == size;
}

struct TmpPath {
  explicit TmpPath(const char* path) { snprintf(text, sizeof(text), "%s.tmp", path); }
  const char* c_str() const { return text; }
  char text[64];
};

// Small arrays (a log chunk, the review queue): no std::sort instantiation per type.
template <typename T, typename Less>
void insertionSort(T* items, const size_t n, Less less) {
  for (size_t i = 1; i < n; i++) {
    const T item = items[i];
    size_t j = i;
    for (; j > 0 && less(item, items[j - 1]); j--) items[j] = items[j - 1];
    items[j] = item;
  }
}

// A replace cut after the old file went leaves only the complete tmp.
void recover(const char* path) {
  const TmpPath tmp(path);
  if (Storage.exists(path) || !Storage.exists(tmp.c_str())) return;
  HalFile f;
  const bool empty = Storage.openFileForRead("CARD", tmp.c_str(), f) && f.fileSize() == 0;
  f.close();
  if (empty) {
    Storage.remove(tmp.c_str());
    return;
  }
  LOG_INF("CARD", "Recovering %s", path);
  if (!Storage.rename(tmp.c_str(), path)) LOG_ERR("CARD", "Cannot recover %s", path);
}

// Closes the written tmp and moves it over `path`.
bool commit(HalFile& out, const char* path, const bool ok) {
  const TmpPath tmp(path);
  if (!out.close() || !ok) {
    LOG_ERR("CARD", "Cannot write %s", tmp.c_str());
    Storage.remove(tmp.c_str());
    return false;
  }
  if (!Storage.replaceFile(tmp.c_str(), path)) {
    LOG_ERR("CARD", "Cannot replace %s", path);
    if (Storage.exists(path)) Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

class FileSource final : public ByteSource {
 public:
  explicit FileSource(HalFile& file) : file(file) {}
  int read(uint8_t* buf, const size_t cap) override { return file.read(buf, cap); }

 private:
  HalFile& file;
};

void deckPathOf(const char* folder, const char* name, char* out, const size_t cap) {
  snprintf(out, cap, "%s/%s", folder, name);
}

}  // namespace

struct DeckStore::Header {
  Format format;
  uint8_t newPerDay = DEFAULT_NEW_PER_DAY;
  bool virtualDays = false;
  uint32_t deckSize = 0;
  uint32_t deckTime = 0;
  uint16_t count = 0;
  uint16_t skipped = 0;
  uint16_t rows = 0;
  uint16_t lastDay = NO_DAY;
  uint16_t counterDay = NO_DAY;
  uint16_t newDone = 0;
  uint16_t reviewsDone = 0;
  uint16_t dropped = 0;
  uint32_t applied = 0;
  uint8_t nameLen = 0;
  char name[DECK_NAME_MAX + 1] = "";

  size_t size() const { return FIXED + nameLen + 4; }
  size_t fileSize() const { return size() + static_cast<size_t>(count) * RECORD + 4; }

  size_t encode(uint8_t* p) const {
    memset(p, 0, FIXED);
    memcpy(p, MAGIC, sizeof(MAGIC));
    p[4] = VERSION;
    p[5] = static_cast<uint8_t>((virtualDays ? FLAG_VIRTUAL : 0) | (format.html ? FLAG_HTML : 0) |
                                (format.cp1251 ? FLAG_CP1251 : 0));
    p[6] = static_cast<uint8_t>(format.separator);
    p[7] = static_cast<uint8_t>(format.guidColumn);
    put16(p + 8, format.hiddenColumns);
    p[10] = newPerDay;
    p[11] = nameLen;
    put32(p + 12, deckSize);
    put32(p + 16, deckTime);
    put32(p + 20, format.dataStart);
    put16(p + 24, count);
    put16(p + 26, skipped);
    put16(p + 28, rows);
    put16(p + 30, lastDay);
    put16(p + 32, counterDay);
    put16(p + 34, newDone);
    put16(p + 36, reviewsDone);
    put16(p + 38, dropped);
    put32(p + 40, applied);
    memcpy(p + FIXED, name, nameLen);
    put32(p + FIXED + nameLen, crc32(0, p, FIXED + nameLen));
    return size();
  }

  bool decode(HalFile& f) {
    uint8_t p[FIXED + DECK_NAME_MAX + 4];
    if (!readAll(f, p, FIXED) || memcmp(p, MAGIC, sizeof(MAGIC)) != 0 || p[4] != VERSION || p[11] > DECK_NAME_MAX) {
      return false;
    }
    const uint8_t len = p[11];
    if (!readAll(f, p + FIXED, len + 4u) || get32(p + FIXED + len) != crc32(0, p, FIXED + len)) return false;
    virtualDays = p[5] & FLAG_VIRTUAL;
    format.html = p[5] & FLAG_HTML;
    format.cp1251 = p[5] & FLAG_CP1251;
    format.separator = static_cast<char>(p[6]);
    format.guidColumn = static_cast<int8_t>(p[7]);
    format.hiddenColumns = get16(p + 8);
    newPerDay = p[10];
    nameLen = len;
    deckSize = get32(p + 12);
    deckTime = get32(p + 16);
    format.dataStart = get32(p + 20);
    count = get16(p + 24);
    skipped = get16(p + 26);
    rows = get16(p + 28);
    lastDay = get16(p + 30);
    counterDay = get16(p + 32);
    newDone = get16(p + 34);
    reviewsDone = get16(p + 36);
    dropped = get16(p + 38);
    applied = get32(p + 40);
    memcpy(name, p + FIXED, len);
    name[len] = '\0';
    return true;
  }
};

uint32_t deckId(const char* fileName) { return fnv1a(fileName, strlen(fileName)); }

const char* deckDir() {
  return !Storage.exists(DECK_DIR) && Storage.exists(DECK_DIR_LOWER) ? DECK_DIR_LOWER : DECK_DIR;
}

bool loadDueTotal(const uint16_t day, uint16_t& total) {
  HalFile f;
  uint8_t p[8];
  if (!Storage.exists(DUE_PATH) || !Storage.openFileForRead("CARD", DUE_PATH, f)) return false;
  const bool ok = readAll(f, p, sizeof(p));
  f.close();
  if (!ok || get32(p + 4) != crc32(0, p, 4) || get16(p) != day || day == NO_DAY) return false;
  total = get16(p + 2);
  return true;
}

void saveDueTotal(const uint16_t day, const uint16_t total) {
  uint8_t p[8];
  put16(p, day);
  put16(p + 2, total);
  put32(p + 4, crc32(0, p, 4));
  HalFile f;
  if (!Storage.ensureDirectoryExists(PROGRESS_DIR) || !Storage.openFileForWrite("CARD", TmpPath(DUE_PATH).c_str(), f)) {
    return;
  }
  commit(f, DUE_PATH, writeAll(f, p, sizeof(p)));
}

DeckStore::DeckStore() = default;
DeckStore::~DeckStore() { close(); }

void DeckStore::close() {
  if (inSession()) endSession();
  opened = false;
}

Status DeckStore::open(const char* fileName, const uint16_t realDay, const Reindex reindex) {
  close();
  const size_t nameLen = strlen(fileName);
  if (nameLen == 0 || nameLen > DECK_NAME_MAX) return Status::Missing;
  if (!head) head = makeUniqueNoThrow<Header>();
  if (!head) return Status::NoMemory;

  char deckPath[DECK_NAME_MAX + 32];
  folder = deckDir();
  deckPathOf(folder, fileName, deckPath, sizeof(deckPath));
  HalFile deck;
  if (!Storage.openFileForRead("CARD", deckPath, deck)) return Status::Missing;
  const uint32_t size = static_cast<uint32_t>(deck.fileSize());
  const uint32_t time = deck.modificationTime();
  deck.close();
  if (size > MAX_DECK_BYTES) return Status::TooLarge;
  if (!Storage.ensureDirectoryExists(PROGRESS_DIR)) return Status::Failed;

  const uint32_t id = deckId(fileName);
  snprintf(datPath, sizeof(datPath), "%s/%08lx.dat", PROGRESS_DIR, static_cast<unsigned long>(id));
  snprintf(logPath, sizeof(logPath), "%s/%08lx.log", PROGRESS_DIR, static_cast<unsigned long>(id));
  recover(datPath);

  bool valid = false;
  {
    HalFile f;
    if (Storage.exists(datPath) && Storage.openFileForRead("CARD", datPath, f)) {
      valid = head->decode(f) && f.fileSize() == head->fileSize() && strcmp(head->name, fileName) == 0;
      uint32_t crc = 0;
      uint8_t buf[IO_RECORDS * RECORD];
      uint32_t units = 0;
      for (uint16_t i = 0; valid && i < head->count; i += IO_RECORDS) {
        serviceWork(units);
        const size_t n = std::min<size_t>(IO_RECORDS, head->count - i) * RECORD;
        valid = readAll(f, buf, n);
        crc = crc32(crc, buf, n);
      }
      valid = valid && readAll(f, buf, 4) && get32(buf) == crc;
      f.close();
    }
  }
  if (!valid) {
    *head = Header{};
    LOG_INF("CARD", "Indexing %s", fileName);
  }
  head->nameLen = static_cast<uint8_t>(nameLen);
  memcpy(head->name, fileName, nameLen + 1);
  const bool changed = head->deckSize != size || head->deckTime != time;
  if (!valid || reindex == Reindex::Always || (reindex == Reindex::IfChanged && changed)) {
    const Status built = rebuild(valid, size, time);
    if (built != Status::Ok) return built;
  }

  uint32_t lastSeq = 0;
  logCount = 0;
  if (Storage.exists(logPath)) {
    HalFile f = Storage.open(logPath, O_RDWR);
    if (!f) return Status::Failed;
    const size_t whole = f.fileSize() / LOG_RECORD * LOG_RECORD;
    uint8_t rec[LOG_RECORD];
    LogEntry e;
    for (size_t at = 0; at < whole && readAll(f, rec, sizeof(rec)); at += LOG_RECORD) {
      if (!decodeLog(rec, e)) continue;
      lastSeq = std::max(lastSeq, e.seq);
      if (e.seq > head->applied) logCount++;
    }
    // A torn append: the next record must start on a record boundary.
    if (whole != f.fileSize() && !f.truncate(whole)) LOG_ERR("CARD", "Cannot trim %s", logPath);
    f.close();
  }
  sessionSeq = std::max(head->applied, lastSeq) + 1;
  nextSeq = sessionSeq;

  DeckDays days{head->lastDay, head->virtualDays};
  int32_t shift = 0;
  today = studyDay(realDay, days, shift);
  openDay = realDay;
  clockKnown = realDay != NO_DAY;
  head->virtualDays = days.virtualDays;
  if (logCount > 0 || shift != 0) {
    const Status merged = merge(shift);
    if (merged != Status::Ok) return merged;
  }
  if (head->counterDay != today) {
    head->counterDay = today;
    head->newDone = 0;
    head->reviewsDone = 0;
  }
  opened = true;
  return Status::Ok;
}

Status DeckStore::rebuild(const bool keepOld, const uint32_t size, const uint32_t time) {
  uint32_t units = 0;
  std::unique_ptr<OldState[]> old;
  uint16_t oldCount = 0;
  if (keepOld && head->count > 0) {
    old = makeUniqueNoThrow<OldState[]>(head->count);
    HalFile f;
    if (!old) return Status::NoMemory;
    if (!Storage.openFileForRead("CARD", datPath, f) || !f.seekSet(head->size())) return Status::Failed;
    uint8_t rec[RECORD];
    for (; oldCount < head->count && readAll(f, rec, sizeof(rec)); oldCount++) {
      const Record r = decodeRecord(rec);
      old[oldCount] = {r.key, r.state, r.mark};
    }
    f.close();
    std::sort(old.get(), old.get() + oldCount, [](const OldState& a, const OldState& b) { return a.key < b.key; });
  }

  char deckPath[DECK_NAME_MAX + 32];
  deckPathOf(folder, head->name, deckPath, sizeof(deckPath));
  HalFile deck;
  if (!Storage.openFileForRead("CARD", deckPath, deck)) return Status::Missing;
  {
    auto probeBuf = makeUniqueNoThrow<uint8_t[]>(PROBE_BYTES);
    if (!probeBuf) return Status::NoMemory;
    const int got = deck.read(probeBuf.get(), PROBE_BYTES);
    if (got < 0) return Status::Failed;
    head->format = probe(probeBuf.get(), static_cast<size_t>(got), isCsvName(head->name));
    if (head->format.utf16) return Status::Unsupported;
  }
  // Every card's key and its front+answer key, sorted: a repeated front is
  // told apart by its answer, a repeated card is dropped.
  auto keys = makeUniqueNoThrow<uint32_t[]>(2 * MAX_CARDS);
  auto fields = makeUniqueNoThrow<Fields>();
  // Old records matched exactly (pass one), and those already handed out (pass two).
  const size_t bitmapBytes = (oldCount + 7u) / 8u;
  auto claimed = makeUniqueNoThrow<uint8_t[]>(bitmapBytes + 1);
  auto given = makeUniqueNoThrow<uint8_t[]>(bitmapBytes + 1);
  if (!keys || !fields || !claimed || !given) return Status::NoMemory;
  const auto test = [](const uint8_t* bits, const int i) { return (bits[i >> 3] >> (i & 7)) & 1; };
  const auto mark = [](uint8_t* bits, const int i) { bits[i >> 3] |= static_cast<uint8_t>(1u << (i & 7)); };
  const auto find = [&](const uint32_t k) -> int {
    const OldState* at = std::lower_bound(old.get(), old.get() + oldCount, k,
                                          [](const OldState& o, const uint32_t v) { return o.key < v; });
    return at != old.get() + oldCount && at->key == k ? static_cast<int>(at - old.get()) : -1;
  };
  // The old record that is this very card: same guid, same front and answer, or
  // the same front with the same answer mark.
  const auto exact = [&](const uint32_t frontKey, const uint32_t alt, const uint32_t guidHash) {
    if (guidHash != 0) return find(guidHash);
    const int byAnswer = find(alt);
    if (byAnswer >= 0) return byAnswer;
    const int byFront = find(frontKey);
    return byFront >= 0 && old[byFront].mark == answerMark(alt) ? byFront : -1;
  };

  uint16_t keyCount = 0;
  const auto has = [&](const uint32_t k) { return std::binary_search(keys.get(), keys.get() + keyCount, k); };
  const auto add = [&](const uint32_t k) {
    uint32_t* end = keys.get() + keyCount;
    uint32_t* at = std::lower_bound(keys.get(), end, k);
    if (at != end && *at == k) return;
    memmove(at + 1, at, static_cast<size_t>(end - at) * sizeof(uint32_t));
    *at = k;
    keyCount++;
  };
  FileSource source(deck);
  std::unique_ptr<RecordReader> reader;
  // One pass over the deck; keep(frontKey, key, alt, offset, guidHash) sees each
  // card that is kept, and returns false to stop.
  const auto scan = [&](const auto& keep) {
    keyCount = 0;
    head->count = head->skipped = head->dropped = head->rows = 0;
    reader = makeUniqueNoThrow<RecordReader>(source, head->format, head->format.dataStart);
    if (!reader || !deck.seekSet(head->format.dataStart)) return false;
    uint32_t offset = 0;
    uint32_t guidHash = 0;
    while (reader->next(*fields, offset, guidHash)) {
      serviceWork(units);
      if (!hasFront(*fields)) {
        if (head->skipped < UINT16_MAX) head->skipped++;
        continue;
      }
      if (head->count < MAX_CARDS) {
        const uint32_t frontKey = cardKey(*fields, guidHash);
        const uint32_t alt = cardAltKey(*fields);
        if (has(alt) || (has(frontKey) && guidHash != 0)) {
          if (head->dropped < UINT16_MAX) head->dropped++;
          continue;
        }
        const uint32_t key = has(frontKey) ? alt : frontKey;
        add(key);
        add(alt);
        if (!keep(frontKey, key, alt, offset, guidHash)) return false;
        head->count++;
      }
      if (head->rows < UINT16_MAX) head->rows++;
    }
    return !reader->failed();
  };

  // Exact matches first, so a card that only shares a front with an old one
  // takes its progress only when no card claims it as its own.
  if (oldCount > 0 && !scan([&](const uint32_t frontKey, uint32_t, const uint32_t alt, uint32_t, const uint32_t guid) {
        const int j = exact(frontKey, alt, guid);
        if (j >= 0) mark(claimed.get(), j);
        return true;
      })) {
    return reader ? Status::Failed : Status::NoMemory;
  }

  head->deckSize = size;
  head->deckTime = time;
  const TmpPath tmp(datPath);
  HalFile out;
  if (!Storage.openFileForWrite("CARD", tmp.c_str(), out)) return Status::Failed;
  uint8_t headBuf[FIXED + DECK_NAME_MAX + 4];
  const size_t headSize = head->encode(headBuf);
  bool ok = writeAll(out, headBuf, headSize);
  uint8_t buf[IO_RECORDS * RECORD];
  size_t buffered = 0;
  uint32_t crc = 0;
  const bool scanned = scan(
      [&](const uint32_t frontKey, const uint32_t key, const uint32_t alt, const uint32_t offset, const uint32_t guid) {
        Record r;
        r.offset = offset;
        r.key = key;
        r.mark = answerMark(alt);
        int j = exact(frontKey, alt, guid);
        if (j < 0 && guid == 0) {
          j = find(frontKey);
          if (j >= 0 && test(claimed.get(), j)) j = -1;
        }
        if (j >= 0 && !test(given.get(), j)) {
          r.state = old[j].state;
          mark(given.get(), j);
        }
        encodeRecord(buf + buffered, r);
        buffered += RECORD;
        if (buffered == sizeof(buf)) {
          crc = crc32(crc, buf, buffered);
          ok = writeAll(out, buf, buffered);
          buffered = 0;
        }
        return ok;
      });
  if (!scanned) ok = false;
  deck.close();
  crc = crc32(crc, buf, buffered);
  ok = ok && writeAll(out, buf, buffered);
  uint8_t crcBytes[4];
  put32(crcBytes, crc);
  ok = ok && writeAll(out, crcBytes, sizeof(crcBytes));
  head->encode(headBuf);
  ok = ok && out.seekSet(0) && writeAll(out, headBuf, headSize);
  if (!commit(out, datPath, ok)) return Status::Failed;
  LOG_INF("CARD", "Indexed %s: %u cards, %u skipped", head->name, static_cast<unsigned>(head->count),
          static_cast<unsigned>(head->skipped));
  return Status::Ok;
}

Status DeckStore::merge(const int32_t shift, bool force) {
  auto entries = makeUniqueNoThrow<LogEntry[]>(LOG_CHUNK);
  // The header as on disk: a chunk that does not commit leaves it, and its
  // answers stay in the log for the next merge.
  auto saved = makeUniqueNoThrow<Header>();
  if (!entries || !saved) return Status::NoMemory;
  HalFile log;
  const bool haveLog = Storage.exists(logPath) && Storage.openFileForRead("CARD", logPath, log);
  int32_t pendingShift = shift;
  bool more = true;
  while (more) {
    uint16_t n = 0;
    uint8_t rec[LOG_RECORD];
    while (haveLog && n < LOG_CHUNK && readAll(log, rec, sizeof(rec))) {
      LogEntry e;
      if (decodeLog(rec, e) && e.seq > head->applied) entries[n++] = e;
    }
    more = n == LOG_CHUNK;
    if (n == 0 && pendingShift == 0 && !force) break;
    *saved = *head;
    if (!mergeChunk(entries.get(), n, pendingShift)) {
      *head = *saved;
      return Status::Failed;
    }
    pendingShift = 0;
    force = false;
  }
  if (haveLog) log.close();
  if (Storage.exists(logPath) && !Storage.remove(logPath)) LOG_ERR("CARD", "Cannot clear %s", logPath);
  logCount = 0;
  return Status::Ok;
}

bool DeckStore::mergeChunk(LogEntry* entries, const uint16_t n, const int32_t shift) {
  for (uint16_t i = 0; i < n; i++) {
    const LogEntry& e = entries[i];
    head->applied = std::max(head->applied, e.seq);
    // This open's answers were counted as they were given.
    if (e.seq >= sessionSeq) continue;
    head->lastDay = std::max(head->lastDay, e.day);
    if (e.day > head->counterDay) {
      head->counterDay = e.day;
      head->newDone = head->reviewsDone = 0;
    }
    if (e.day == head->counterDay) countAnswer(e.flags & (LOG_WAS_NEW | LOG_RELEARN), (e.flags & LOG_UNDO) ? -1 : 1);
  }
  insertionSort(entries, n,
                [](const LogEntry& a, const LogEntry& b) { return a.key != b.key ? a.key < b.key : a.seq < b.seq; });
  if (shift != 0) {
    if (head->lastDay != NO_DAY) head->lastDay = static_cast<uint16_t>(head->lastDay + shift);
    if (head->counterDay != NO_DAY) head->counterDay = static_cast<uint16_t>(head->counterDay + shift);
  }

  HalFile in;
  if (!Storage.openFileForRead("CARD", datPath, in) || !in.seekSet(head->size())) return false;
  const TmpPath tmp(datPath);
  HalFile out;
  if (!Storage.openFileForWrite("CARD", tmp.c_str(), out)) return false;
  uint8_t headBuf[FIXED + DECK_NAME_MAX + 4];
  bool ok = writeAll(out, headBuf, head->encode(headBuf));
  uint8_t buf[IO_RECORDS * RECORD];
  uint32_t crcIn = 0;
  uint32_t crcOut = 0;
  uint32_t units = 0;
  const LogEntry* end = entries + n;
  for (uint16_t i = 0; ok && i < head->count; i += IO_RECORDS) {
    serviceWork(units);
    const size_t k = std::min<size_t>(IO_RECORDS, head->count - i);
    ok = readAll(in, buf, k * RECORD);
    crcIn = crc32(crcIn, buf, k * RECORD);
    for (size_t j = 0; ok && j < k; j++) {
      Record r = decodeRecord(buf + j * RECORD);
      const LogEntry* last = std::upper_bound(static_cast<const LogEntry*>(entries), end, r.key,
                                              [](const uint32_t key, const LogEntry& e) { return key < e.key; });
      if (last != entries && (last - 1)->key == r.key) r.state = (last - 1)->state;
      if (shift != 0 && r.state.box != 0) {
        r.state.due = static_cast<uint16_t>(std::clamp<int32_t>(r.state.due + shift, 1, UINT16_MAX));
      }
      encodeRecord(buf + j * RECORD, r);
    }
    crcOut = crc32(crcOut, buf, k * RECORD);
    ok = ok && writeAll(out, buf, k * RECORD);
  }
  uint8_t crcBytes[4];
  ok = ok && readAll(in, crcBytes, 4) && get32(crcBytes) == crcIn;
  in.close();
  put32(crcBytes, crcOut);
  ok = ok && writeAll(out, crcBytes, sizeof(crcBytes));
  return commit(out, datPath, ok);
}

bool DeckStore::summarize(Summary& out) {
  out = Summary{};
  if (!opened) return false;
  // Answers a failed merge left in the log are not in the .dat yet.
  if (logCount > 0 && !inSession()) static_cast<void>(merge(0));
  out.newPerDay = head->newPerDay;
  out.skipped = head->skipped;
  out.dropped = head->dropped;
  out.rows = head->rows;
  out.dateUnknown = !clockKnown;
  HalFile f;
  if (!Storage.openFileForRead("CARD", datPath, f) || !f.seekSet(head->size())) return false;
  uint8_t buf[IO_RECORDS * RECORD];
  uint16_t due = 0;
  uint32_t units = 0;
  for (uint16_t i = 0; i < head->count; i += IO_RECORDS) {
    serviceWork(units);
    const size_t k = std::min<size_t>(IO_RECORDS, head->count - i);
    if (!readAll(f, buf, k * RECORD)) return false;
    for (size_t j = 0; j < k; j++) {
      const CardState s = decodeRecord(buf + j * RECORD).state;
      out.cards++;
      if (isNew(s)) {
        out.fresh++;
        continue;
      }
      if (isKnown(s))
        out.known++;
      else
        out.learning++;
      if (s.due <= today) due++;
      if (s.due <= today + 1) out.tomorrow++;
      if (s.due <= today + 7) out.week++;
    }
  }
  f.close();
  const uint16_t reviewRoom = head->reviewsDone < REVIEWS_PER_DAY ? REVIEWS_PER_DAY - head->reviewsDone : 0;
  const uint16_t newRoom = head->newDone < head->newPerDay ? head->newPerDay - head->newDone : 0;
  out.reviews = std::min(due, reviewRoom);
  out.newCards = std::min(out.fresh, newRoom);
  out.tomorrow = std::min(out.tomorrow, REVIEWS_PER_DAY);
  return true;
}

uint8_t DeckStore::newPerDay() const { return head ? head->newPerDay : DEFAULT_NEW_PER_DAY; }

bool DeckStore::setNewPerDay(const uint8_t count) {
  if (!opened) return false;
  head->newPerDay = count;
  return merge(0, true) == Status::Ok;
}

Status DeckStore::startSession(const uint16_t extraNew) {
  if (!opened) return Status::Failed;
  if (inSession()) endSession();
  // The queue is read from the .dat: it must hold every answer given.
  if (logCount > 0) {
    const Status merged = merge(0);
    if (merged != Status::Ok) return merged;
  }
  queue = makeUniqueNoThrow<Item[]>(QUEUE_CAP);
  if (!queue) return Status::NoMemory;
  queueLen = 0;
  pos = 0;
  undoValid = false;
  loadedIndex = -1;
  memset(counts, 0, sizeof(counts));

  const uint16_t reviewRoom = head->reviewsDone < REVIEWS_PER_DAY ? REVIEWS_PER_DAY - head->reviewsDone : 0;
  const uint16_t newRoom =
      static_cast<uint16_t>((head->newDone < head->newPerDay ? head->newPerDay - head->newDone : 0) + extraNew);
  const uint16_t reviewCap = std::min<uint16_t>(reviewRoom, QUEUE_CAP / 2 + QUEUE_CAP / 4);
  uint16_t reviews = 0;
  uint16_t fresh = 0;
  Item* freshItems = queue.get() + reviewCap;
  const uint16_t freshCap = std::min<uint16_t>(newRoom, QUEUE_CAP - reviewCap);

  HalFile f;
  if (!Storage.openFileForRead("CARD", datPath, f) || !f.seekSet(head->size())) {
    queue.reset();
    return Status::Failed;
  }
  uint8_t buf[IO_RECORDS * RECORD];
  uint32_t units = 0;
  for (uint16_t i = 0; i < head->count; i += IO_RECORDS) {
    serviceWork(units);
    const size_t k = std::min<size_t>(IO_RECORDS, head->count - i);
    if (!readAll(f, buf, k * RECORD)) {
      queue.reset();
      return Status::Failed;
    }
    for (size_t j = 0; j < k; j++) {
      const CardState s = decodeRecord(buf + j * RECORD).state;
      const Item item{static_cast<uint16_t>(i + j), s.due, s.box, s.lapses};
      if (isNew(s)) {
        if (fresh < freshCap) freshItems[fresh++] = item;
      } else if (isDue(s, today) && reviewCap > 0) {
        // Keep the most overdue: replace the latest one once full.
        if (reviews < reviewCap) {
          queue[reviews++] = item;
        } else {
          Item* latest = std::max_element(queue.get(), queue.get() + reviews,
                                          [](const Item& a, const Item& b) { return a.due < b.due; });
          if (item.due < latest->due) *latest = item;
        }
      }
    }
  }
  f.close();
  insertionSort(queue.get(), reviews,
                [](const Item& a, const Item& b) { return a.due != b.due ? a.due < b.due : a.index < b.index; });
  memmove(queue.get() + reviews, freshItems, fresh * sizeof(Item));
  queueLen = reviews + fresh;
  return Status::Ok;
}

bool DeckStore::readRecord(const uint16_t index, uint32_t& offset, uint32_t& key) {
  HalFile f;
  uint8_t rec[RECORD];
  const bool ok = Storage.openFileForRead("CARD", datPath, f) && f.seekSet(head->size() + index * RECORD) &&
                  readAll(f, rec, sizeof(rec));
  f.close();
  if (!ok) return false;
  const Record r = decodeRecord(rec);
  offset = r.offset;
  key = r.key;
  return true;
}

Status DeckStore::loadCard(Fields& out) {
  if (!inSession() || finished()) return Status::Failed;
  const uint16_t index = queue[pos].index;
  if (!readRecord(index, currentOffset, currentKey)) return Status::Failed;
  char deckPath[DECK_NAME_MAX + 32];
  deckPathOf(folder, head->name, deckPath, sizeof(deckPath));
  HalFile deck;
  if (!Storage.openFileForRead("CARD", deckPath, deck) || !deck.seekSet(currentOffset)) return Status::Failed;
  FileSource source(deck);
  auto reader = makeUniqueNoThrow<RecordReader>(source, head->format, currentOffset);
  if (!reader) return Status::NoMemory;
  uint32_t offset = 0;
  uint32_t guid = 0;
  const bool ok = reader->next(out, offset, guid);
  deck.close();
  // A line that is no longer there is an edit; a read error is not.
  if (!ok) return reader->failed() ? Status::Failed : Status::Changed;
  if (cardKey(out, guid) != currentKey && cardAltKey(out) != currentKey) {
    LOG_INF("CARD", "%s changed in place", head->name);
    return Status::Changed;
  }
  loadedIndex = index;
  return Status::Ok;
}

CardState DeckStore::currentState() const {
  if (!inSession() || finished()) return {};
  const Item& item = queue[pos];
  return {item.due, item.box, item.lapses};
}

bool DeckStore::appendLog(const uint32_t key, const CardState& state, const uint8_t flags) {
  if (logCount >= LOG_HARD_MAX) return false;
  LogEntry e;
  e.key = key;
  e.seq = nextSeq;
  e.state = state;
  e.day = today;
  e.flags = flags;
  uint8_t rec[LOG_RECORD];
  encodeLog(rec, e);
  HalFile f = Storage.open(logPath, O_WRONLY | O_CREAT | O_APPEND);
  if (!f) return false;
  const bool written = writeAll(f, rec, sizeof(rec));
  if (!f.close() || !written) {
    LOG_ERR("CARD", "Cannot append %s", logPath);
    return false;
  }
  nextSeq++;
  logCount++;
  return true;
}

uint8_t DeckStore::answerKind(const CardState& before) {
  if (isNew(before)) return LOG_WAS_NEW;
  return (before.box & AGAIN_FLAG) ? LOG_RELEARN : 0;
}

void DeckStore::countAnswer(const uint8_t kind, const int delta) {
  if (kind & LOG_RELEARN) return;
  uint16_t& counter = (kind & LOG_WAS_NEW) ? head->newDone : head->reviewsDone;
  counter = static_cast<uint16_t>(std::max(0, counter + delta));
}

bool DeckStore::grade(const Grade g) {
  if (!inSession() || finished()) return false;
  Item& item = queue[pos];
  if (loadedIndex != item.index && !readRecord(item.index, currentOffset, currentKey)) return false;
  const CardState before{item.due, item.box, item.lapses};
  const CardState after = answer(before, g, currentKey, today);
  const uint8_t kind = answerKind(before);
  if (!appendLog(currentKey, after, kind)) return false;
  last = {pos, item, -1, g, currentKey, kind};
  undoValid = true;
  countAnswer(kind, 1);
  head->lastDay = today;
  counts[static_cast<int>(g)]++;
  item.due = after.due;
  item.box = after.box;
  item.lapses = after.lapses;
  if (g == Grade::Again && queueLen < QUEUE_CAP) {
    const uint16_t at = std::min<uint16_t>(pos + 1 + AGAIN_GAP, queueLen);
    memmove(&queue[at + 1], &queue[at], (queueLen - at) * sizeof(Item));
    queue[at] = queue[pos];
    queueLen++;
    last.againAt = static_cast<int16_t>(at);
  }
  pos++;
  loadedIndex = -1;
  if (logCount >= LOG_MERGE_AT && merge(0) != Status::Ok) LOG_ERR("CARD", "Merge failed; the log keeps the answers");
  return true;
}

bool DeckStore::undo() {
  if (!undoValid || !inSession()) return false;
  const CardState before{last.item.due, last.item.box, last.item.lapses};
  if (!appendLog(last.key, before, static_cast<uint8_t>(LOG_UNDO | last.kind))) return false;
  if (last.againAt >= 0) {
    memmove(&queue[last.againAt], &queue[last.againAt + 1], (queueLen - last.againAt - 1) * sizeof(Item));
    queueLen--;
  }
  pos = last.pos;
  queue[pos] = last.item;
  countAnswer(last.kind, -1);
  if (counts[static_cast<int>(last.grade)] > 0) counts[static_cast<int>(last.grade)]--;
  undoValid = false;
  loadedIndex = -1;
  return true;
}

bool DeckStore::flush() { return logCount == 0 || merge(0) == Status::Ok; }

void DeckStore::endSession() {
  queue.reset();
  queueLen = 0;
  pos = 0;
  undoValid = false;
  if (opened && logCount > 0 && merge(0) != Status::Ok) LOG_ERR("CARD", "Merge failed; the log keeps the answers");
}

}  // namespace cards
