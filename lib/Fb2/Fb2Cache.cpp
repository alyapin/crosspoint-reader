#include "Fb2Cache.h"

#include <cstdio>
#include <cstring>

#include "Fb2Position.h"
#include "Fb2ToEpub.h"

namespace Fb2Cache {

const char* archiveName(const uint8_t version, char* out, const size_t size) {
  if (version <= 1) {
    snprintf(out, size, "fb2.epub");
  } else {
    snprintf(out, size, "fb2.v%u.epub", static_cast<unsigned>(version));
  }
  return out;
}

const char* attemptName(const uint8_t version, char* out, const size_t size) {
  snprintf(out, size, "fb2.v%u.try", static_cast<unsigned>(version));
  return out;
}

uint8_t archiveVersion(const char* name) {
  if (strcmp(name, "fb2.epub") == 0) return 1;
  unsigned v = 0;
  char tail[8] = {};
  if (sscanf(name, "fb2.v%u%7s", &v, tail) == 2 && strcmp(tail, ".epub") == 0 && v > 1 && v <= UINT8_MAX) {
    return static_cast<uint8_t>(v);
  }
  return 0;
}

bool survivesClear(const char* name) {
  const size_t len = strlen(name);
  const bool attempt = strncmp(name, "fb2.v", 5) == 0 && len > 4 && strcmp(name + len - 4, ".try") == 0;
  return archiveVersion(name) != 0 || attempt || strcmp(name, NEXT) == 0 || strcmp(name, SOURCE) == 0 ||
         strcmp(name, PENDING) == 0 || strcmp(name, "progress.bin") == 0;
}

namespace {

// The newest archive an earlier converter left, or 0.
uint8_t outdatedVersion(Store& store, char* name, const size_t size) {
  for (uint8_t v = Fb2ToEpub::FORMAT_VERSION - 1; v >= 1; v--) {
    if (store.exists(archiveName(v, name, size))) return v;
  }
  return 0;
}

void removeIfThere(Store& store, const char* name) {
  if (store.exists(name)) store.remove(name);
}

// The derived caches belong to `archive`: cleared when the last recorded archive
// was another one (a card moved between boards on different firmware). Caches
// from before the record (the first converter's) can only be from its archive.
void claimDerived(Store& store, const char* archive) {
  char source[16] = {};
  const size_t n = store.read(SOURCE, source, sizeof(source) - 1);
  if (n > 0 && strcmp(source, archive) == 0) return;
  // Recorded only once the caches are gone, so a partial clear is tried again.
  if (n > 0 && !store.clearDerived()) return;
  store.write(SOURCE, archive, strlen(archive));
}

}  // namespace

const char* prepare(Store& store, const bool mayConvert, char* out, const size_t size) {
  archiveName(Fb2ToEpub::FORMAT_VERSION, out, size);
  char old[16];
  const uint8_t oldVersion = outdatedVersion(store, old, sizeof(old));
  char attempt[16];
  attemptName(Fb2ToEpub::FORMAT_VERSION, attempt, sizeof(attempt));
  if (store.exists(out)) {
    // Leftovers of a migration interrupted after its rename.
    if (oldVersion != 0) store.remove(old);
    removeIfThere(store, NEXT);
    removeIfThere(store, NEXT_TMP);
    removeIfThere(store, attempt);
    claimDerived(store, out);
    return out;
  }
  const auto readOld = [&]() -> const char* {
    if (oldVersion == 0) return nullptr;
    claimDerived(store, old);
    return archiveName(oldVersion, out, size);
  };
  if (!mayConvert) return readOld();
  // A migration tried before and never finished: it may have run out of heap and
  // rebooted, so the old archive serves until the cache is deleted. What the attempt
  // left goes, as the card may be full.
  if (oldVersion != 0 && store.exists(attempt)) {
    removeIfThere(store, NEXT);
    removeIfThere(store, NEXT_TMP);
    return readOld();
  }

  if (oldVersion != 0) {
    // 1. The position, while the old layout can still place it. Without one (an
    // interrupted attempt already removed the spine) an earlier fb2.pos stays.
    const float fraction = store.savedFraction(old);
    char pending[32];
    const size_t n = Fb2Position::formatPending(pending, sizeof(pending), fraction, oldVersion);
    if (fraction >= 0.0f && n > 0) store.write(PENDING, pending, n);
    // 2. The attempt, before anything that can fail or die.
    if (!store.write(attempt, "", 0)) return readOld();
  }
  // 3. The new book beside the old one, which stays readable if this fails.
  if (!store.convert(NEXT)) return readOld();
  // 4. What the reader built from the old archive no longer matches.
  if (!store.clearDerived()) return readOld();
  // 5. The rename is the stamp.
  if (!store.rename(NEXT, out)) return readOld();
  if (oldVersion != 0) store.remove(old);
  removeIfThere(store, attempt);
  store.write(SOURCE, out, strlen(out));
  return out;
}

}  // namespace Fb2Cache
