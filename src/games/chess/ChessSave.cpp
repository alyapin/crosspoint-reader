#include "ChessSave.h"

#include <ChessMatch.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <string>

namespace chesssave {
namespace {
constexpr const char* TMP_PATH = "/.crosspoint/games/chess.json.tmp";
}

bool hasGameInProgress() { return Storage.exists(SAVE_PATH); }

void clear() {
  if (Storage.exists(SAVE_PATH)) Storage.remove(SAVE_PATH);
}

bool save(const chess::Match& match) {
  if (match.game().isOver()) {
    clear();
    return true;
  }
  // Cold path (exit, sleep, after a move): one bounded buffer, freed on return.
  auto text = makeUniqueNoThrow<char[]>(chess::SAVE_MAX);
  if (!text) {
    LOG_ERR("CHESS", "OOM: %u bytes for save", static_cast<unsigned>(chess::SAVE_MAX));
    return false;
  }
  const size_t len = match.serialize(text.get(), chess::SAVE_MAX);
  if (len == 0 || !Storage.ensureDirectoryExists(SAVE_DIR)) {
    LOG_ERR("CHESS", "Cannot prepare save");
    return false;
  }
  HalFile file;
  if (!Storage.openFileForWrite("CHESS", TMP_PATH, file)) return false;
  const size_t written = file.write(text.get(), len);
  file.close();
  if (written != len || !Storage.replaceFile(TMP_PATH, SAVE_PATH)) {
    LOG_ERR("CHESS", "Save failed (%u of %u bytes)", static_cast<unsigned>(written), static_cast<unsigned>(len));
    Storage.remove(TMP_PATH);
    return false;
  }
  return true;
}

bool load(chess::Match& match, const uint32_t seed) {
  if (!Storage.exists(SAVE_PATH)) return false;
  std::string text;
  // A read error may be transient: keep the file. Only content that fails to parse is dropped.
  if (!Storage.readFileToString("CHESS", SAVE_PATH, chess::SAVE_MAX, text)) {
    LOG_ERR("CHESS", "Cannot read save");
    return false;
  }
  if (!match.deserialize(text.data(), text.size(), seed)) {
    LOG_ERR("CHESS", "Discarding corrupt save");
    Storage.remove(SAVE_PATH);
    return false;
  }
  return true;
}

}  // namespace chesssave
