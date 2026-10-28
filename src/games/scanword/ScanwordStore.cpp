#include "ScanwordStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ScanwordGame.h>

#include <cstdio>

namespace scanwordstore {
namespace {

constexpr size_t CHUNK = 256;
constexpr size_t PATH_MAX_LEN = 64;
constexpr const char* STATS_PATH = "/.crosspoint/games/scanword/stats.json";
constexpr const char* STATS_TMP = "/.crosspoint/games/scanword/stats.json.tmp";

bool savePath(const char* id, const char* suffix, char (&out)[PATH_MAX_LEN]) {
  if (!scanword::validId(id)) {
    LOG_ERR("SCANW", "Bad puzzle id");
    return false;
  }
  return snprintf(out, sizeof(out), "%s/%s%s", SAVE_DIR, id, suffix) < static_cast<int>(sizeof(out));
}

using FeedFn = bool (*)(void* parser, const char* data, size_t len);

// Streams a file into a parser CHUNK bytes at a time; false on open, read or
// allocation errors. A parse error stops the stream and is left to the caller.
bool streamFile(const char* path, const FeedFn feed, void* parser) {
  HalFile file;
  if (!Storage.openFileForRead("SCANW", path, file)) return false;
  auto chunk = makeUniqueNoThrow<char[]>(CHUNK);
  if (!chunk) {
    LOG_ERR("SCANW", "OOM: %u bytes for read chunk", static_cast<unsigned>(CHUNK));
    return false;
  }
  while (true) {
    const int n = file.read(chunk.get(), CHUNK);
    if (n < 0) return false;
    if (n == 0) break;
    if (!feed(parser, chunk.get(), static_cast<size_t>(n))) break;
  }
  return true;
}

// Whole small file into a fresh buffer of `cap` bytes; `len` excludes the terminator.
std::unique_ptr<char[]> readSmall(const char* path, const size_t cap, size_t& len) {
  HalFile file;
  if (!Storage.openFileForRead("SCANW", path, file)) return nullptr;
  const size_t size = file.fileSize();
  if (size == 0 || size >= cap) return nullptr;
  auto text = makeUniqueNoThrow<char[]>(cap);
  if (!text) {
    LOG_ERR("SCANW", "OOM: %u bytes for save", static_cast<unsigned>(cap));
    return nullptr;
  }
  if (file.read(text.get(), size) != static_cast<int>(size)) return nullptr;
  text[size] = '\0';
  len = size;
  return text;
}

bool writeAtomic(const char* tmpPath, const char* path, const char* text, const size_t len) {
  if (!Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("SCANW", "Cannot create %s", SAVE_DIR);
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("SCANW", tmpPath, file)) return false;
  const size_t written = file.write(text, len);
  file.close();
  if (written != len || !Storage.replaceFile(tmpPath, path)) {
    LOG_ERR("SCANW", "Save failed (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
    Storage.remove(tmpPath);
    return false;
  }
  return true;
}

}  // namespace

bool readIndex(const scanword::IndexParser::EntryFn onEntry, void* ctx) {
  scanword::IndexParser parser(onEntry, ctx);
  const FeedFn feed = [](void* p, const char* data, size_t len) {
    return static_cast<scanword::IndexParser*>(p)->feed(data, len);
  };
  if (streamFile(INDEX_PATH, feed, &parser) && parser.finish()) return true;
  LOG_ERR("SCANW", "Bad index %s", INDEX_PATH);
  return false;
}

bool loadPuzzle(const char* id, scanword::Puzzle& puzzle) {
  char path[PATH_MAX_LEN];
  if (!scanword::validId(id) ||
      snprintf(path, sizeof(path), "%s/%s.scw", PUZZLE_DIR, id) >= static_cast<int>(sizeof(path))) {
    LOG_ERR("SCANW", "Bad puzzle id");
    return false;
  }
  scanword::PuzzleParser parser(puzzle);
  const FeedFn feed = [](void* p, const char* data, size_t len) {
    return static_cast<scanword::PuzzleParser*>(p)->feed(data, len);
  };
  if (streamFile(path, feed, &parser) && parser.finish()) return true;
  LOG_ERR("SCANW", "Cannot load %s: %s", path, parser.error() != nullptr ? parser.error() : "read error");
  return false;
}

bool hasProgress(const char* id) {
  char path[PATH_MAX_LEN];
  return savePath(id, ".json", path) && Storage.exists(path);
}

bool saveProgress(const char* id, const scanword::Game& game) {
  char path[PATH_MAX_LEN];
  char tmp[PATH_MAX_LEN];
  if (!savePath(id, ".json", path) || !savePath(id, ".json.tmp", tmp)) return false;
  // Cold path (exit, sleep, after an answer): one bounded buffer, freed on return.
  auto text = makeUniqueNoThrow<char[]>(scanword::SAVE_MAX);
  if (!text) {
    LOG_ERR("SCANW", "OOM: %u bytes for save", static_cast<unsigned>(scanword::SAVE_MAX));
    return false;
  }
  const size_t len = game.serialize(text.get(), scanword::SAVE_MAX);
  return len > 0 && writeAtomic(tmp, path, text.get(), len);
}

bool loadProgress(const char* id, scanword::Game& game) {
  char path[PATH_MAX_LEN];
  if (!savePath(id, ".json", path) || !Storage.exists(path)) return false;
  size_t len = 0;
  // A read error may be transient: keep the file. Only content that fails to parse is dropped.
  auto text = readSmall(path, scanword::SAVE_MAX, len);
  if (!text) {
    LOG_ERR("SCANW", "Cannot read %s", path);
    return false;
  }
  if (!game.deserialize(text.get(), len)) {
    LOG_ERR("SCANW", "Discarding save %s", path);
    Storage.remove(path);
    return false;
  }
  return true;
}

void clearProgress(const char* id) {
  char path[PATH_MAX_LEN];
  if (savePath(id, ".json", path) && Storage.exists(path)) Storage.remove(path);
}

bool loadStats(scanword::Stats& stats) {
  stats = scanword::Stats{};
  if (!Storage.exists(STATS_PATH)) return false;
  size_t len = 0;
  auto text = readSmall(STATS_PATH, scanword::STATS_MAX, len);
  return text && stats.deserialize(text.get(), len);
}

bool saveStats(const scanword::Stats& stats) {
  char text[scanword::STATS_MAX];
  const size_t len = stats.serialize(text, sizeof(text));
  return len > 0 && writeAtomic(STATS_TMP, STATS_PATH, text, len);
}

}  // namespace scanwordstore
