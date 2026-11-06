#pragma once

#include <cstddef>
#include <cstdint>

// The converted EPUB of an FB2 book in its cache directory, and its replacement
// when the converter changes.
//
// The archive's name carries the converter version (fb2.v2.epub; the first
// converter wrote fb2.epub), so renaming a finished conversion into place is
// the stamp. A newer converter lays the book out in other files: before
// anything is deleted the reading position goes to fb2.pos as a book fraction,
// the book is converted beside the old archive, and only then are the caches
// derived from the old archive removed and the new one renamed into place.
// Power lost at any step leaves a book that opens, old or new, and a position
// that still applies. A migration that fails or dies (a book too big for the
// heap, a full card) is not tried again on the next open: an attempt marker
// sends the reader to the old archive until the cache is deleted.
namespace Fb2Cache {

constexpr const char* NEXT = "fb2.next.epub";  // a conversion not yet in place
constexpr const char* NEXT_TMP = "fb2.next.epub.tmp";
constexpr const char* SOURCE = "fb2.src";   // the archive the derived caches were built from
constexpr const char* PENDING = "fb2.pos";  // the position a new layout reopens at

// "fb2.epub" for version 1, "fb2.v<N>.epub" after. `out` holds at least 16 bytes.
const char* archiveName(uint8_t version, char* out, size_t size);
// "fb2.v<N>.try": a migration to version N was started.
const char* attemptName(uint8_t version, char* out, size_t size);
// The version an archive name was written by, 0 when it is none.
uint8_t archiveVersion(const char* name);
// What Store::clearDerived keeps: archives, NEXT, progress.bin, the position,
// the source record and attempt markers.
bool survivesClear(const char* name);

// One book's cache directory; names are relative to it.
class Store {
 public:
  virtual ~Store() = default;
  virtual bool exists(const char* name) = 0;
  virtual bool remove(const char* name) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  // Converts the FB2 into `name`, written beside it and renamed when complete.
  virtual bool convert(const char* name) = 0;
  // Removes what the reader derived from an archive: what survivesClear() does not keep.
  virtual bool clearDerived() = 0;
  // The saved reading position as a fraction of `archive`'s book; < 0 without one.
  virtual float savedFraction(const char* archive) = 0;
  // Small files: the bytes read (0 when missing), whether all were written.
  virtual size_t read(const char* name, char* buf, size_t size) = 0;
  virtual bool write(const char* name, const char* data, size_t len) = 0;
};

// The archive to read, written into `out` (at least 16 bytes), or nullptr when
// there is none. Only the reader may convert (`mayConvert`): elsewhere an older
// archive is read as it is, and a book never converted has none.
const char* prepare(Store& store, bool mayConvert, char* out, size_t size);

}  // namespace Fb2Cache
