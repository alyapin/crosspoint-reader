#include "Fb2Cache.h"

#include <cstdio>
#include <cstring>

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

uint8_t archiveVersion(const char* name) {
  if (strcmp(name, "fb2.epub") == 0) return 1;
  unsigned v = 0;
  char tail[8] = {};
  if (sscanf(name, "fb2.v%u%7s", &v, tail) == 2 && strcmp(tail, ".epub") == 0 && v > 1 && v <= UINT8_MAX) {
    return static_cast<uint8_t>(v);
  }
  return 0;
}

namespace {

// The newest archive an earlier converter left, or 0.
uint8_t outdatedVersion(Store& store, char* name, const size_t size) {
  for (uint8_t v = Fb2ToEpub::FORMAT_VERSION - 1; v >= 1; v--) {
    if (store.exists(archiveName(v, name, size))) return v;
  }
  return 0;
}

}  // namespace

const char* prepare(Store& store, const bool mayConvert, char* out, const size_t size) {
  archiveName(Fb2ToEpub::FORMAT_VERSION, out, size);
  char old[16];
  const uint8_t oldVersion = outdatedVersion(store, old, sizeof(old));
  if (store.exists(out)) {
    // An interrupted migration renamed the new archive in but kept the old one.
    if (oldVersion != 0) store.remove(old);
    return out;
  }
  if (!mayConvert) return oldVersion != 0 ? archiveName(oldVersion, out, size) : nullptr;

  if (oldVersion != 0) {
    // 1. The position, while the old layout can still place it. Without one (an
    // interrupted attempt already removed the spine) an earlier fb2.pos stays.
    const float fraction = store.savedFraction(old);
    if (fraction >= 0.0f) store.savePending(fraction, oldVersion);
  }
  // 2. The new book beside the old one, which stays readable if this fails.
  if (!store.convert(NEXT)) return oldVersion != 0 ? archiveName(oldVersion, out, size) : nullptr;
  // 3. What the reader built from the old archive no longer matches.
  if (!store.clearDerived()) return oldVersion != 0 ? archiveName(oldVersion, out, size) : nullptr;
  // 4. The rename is the stamp.
  if (!store.rename(NEXT, out)) return oldVersion != 0 ? archiveName(oldVersion, out, size) : nullptr;
  if (oldVersion != 0) store.remove(old);
  return out;
}

}  // namespace Fb2Cache
