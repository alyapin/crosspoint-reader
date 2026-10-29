#include "ChessSave.h"

#include <ChessMatch.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

namespace chesssave {
namespace {
constexpr const char* TMP_PATH = "/.crosspoint/games/chess.json.tmp";

bool unread = false;  // the save is there but did not read

// A power cut inside replaceFile, after the old file is removed, leaves only
// the tmp; one cut short in its own write is empty.
bool present(const char* path, const char* tmpPath) {
  if (Storage.exists(path)) return true;
  if (!Storage.exists(tmpPath)) return false;
  HalFile file;
  const bool empty = Storage.openFileForRead("CHESS", tmpPath, file) && file.fileSize() == 0;
  file.close();
  if (empty) {
    Storage.remove(tmpPath);
    return false;
  }
  LOG_INF("CHESS", "Recovering %s", path);
  if (!Storage.rename(tmpPath, path)) LOG_ERR("CHESS", "Cannot recover %s", path);
  return true;
}
}  // namespace

bool writeText(const char* path, const char* tmpPath, const char* text, const size_t len) {
  if (len == 0 || !Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("CHESS", "Cannot prepare %s", path);
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("CHESS", tmpPath, file)) return false;
  const size_t written = file.write(text, len);
  if (!file.close() || written != len) {
    LOG_ERR("CHESS", "Save failed (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
    Storage.remove(tmpPath);
    return false;
  }
  if (!Storage.replaceFile(tmpPath, path)) {
    LOG_ERR("CHESS", "Cannot replace %s", path);
    // replaceFile removes the old file first: if it is gone, the tmp is the only copy.
    if (Storage.exists(path)) Storage.remove(tmpPath);
    return false;
  }
  return true;
}

Read readText(const char* path, const char* tmpPath, const size_t cap, std::string& text) {
  if (!present(path, tmpPath)) return Read::Missing;
  if (!Storage.readFileToString("CHESS", path, cap, text)) {
    LOG_ERR("CHESS", "Cannot read %s", path);
    return Read::Failed;
  }
  return Read::Ok;
}

bool hasGameInProgress() { return present(SAVE_PATH, TMP_PATH); }

bool clear() {
  if (unread) return false;
  bool ok = true;
  for (const char* path : {SAVE_PATH, TMP_PATH}) ok = (!Storage.exists(path) || Storage.remove(path)) && ok;
  return ok;
}

bool save(const chess::Match& match) {
  if (match.game().isOver()) return clear();
  if (unread) return false;
  // Cold path (exit, sleep, after a move): one bounded buffer, freed on return.
  auto text = makeUniqueNoThrow<char[]>(chess::SAVE_MAX);
  if (!text) {
    LOG_ERR("CHESS", "OOM: %u bytes for save", static_cast<unsigned>(chess::SAVE_MAX));
    return false;
  }
  return writeText(SAVE_PATH, TMP_PATH, text.get(), match.serialize(text.get(), chess::SAVE_MAX));
}

bool load(chess::Match& match, const uint32_t seed) {
  std::string text;
  // A read error may be transient: keep the file. Only content that fails to parse is dropped.
  const Read result = readText(SAVE_PATH, TMP_PATH, chess::SAVE_MAX, text);
  unread = result == Read::Failed;
  if (result != Read::Ok) return false;
  if (!match.deserialize(text.data(), text.size(), seed)) {
    LOG_ERR("CHESS", "Discarding corrupt save");
    Storage.remove(SAVE_PATH);
    return false;
  }
  return true;
}

}  // namespace chesssave
