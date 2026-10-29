#include "SleepThumbCache.h"

#include <Bitmap.h>
#include <BitmapHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "SleepImagePick.h"

namespace sleep_thumbs {

namespace {
constexpr char kExt[] = ".bmp";
constexpr size_t kNameChars = 8;  // hex key

// Streams a sleep BMP into a 1-bit thumbnail file row by row, so the working
// set stays a few KB (no frame or thumbnail buffer, C3 included): the
// dithered 4-level source rows are box-filtered into one destination row at a
// time, which is Atkinson-dithered and written as soon as it is complete.
// Thumbnail rows go out in the source's file order (bottom-up BMPs stay
// bottom-up), so a destination row is final when the source moves past it.
// The B/W filters keep only white pixels white, as the sleep screen's B/W
// frame does, and the inverted one flips the whole screen, margins included.
class ThumbStreamer {
 public:
  ThumbStreamer(const Bitmap& bitmap, const sleep_pick::ThumbLayout& layout, const int thumbW, const int thumbH,
                const uint8_t filter, HalFile& out)
      : bitmap(bitmap),
        layout(layout),
        thumbW(thumbW),
        thumbH(thumbH),
        blackWhite(filter != CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER),
        inverted(filter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE),
        out(out),
        ditherer(thumbW) {}

  bool run() {
    const int levelBytes = (bitmap.getWidth() + 3) / 4;
    outRowBytes = static_cast<size_t>(thumbW + 31) / 32 * 4;
    scratch = makeUniqueNoThrow<uint8_t[]>(levelBytes + bitmap.getRowBytes() + outRowBytes);
    const size_t columns = std::max(layout.dstW, 1);
    sums = makeUniqueNoThrow<uint32_t[]>(columns);
    counts = makeUniqueNoThrow<uint16_t[]>(columns);
    if (!scratch || !sums || !counts || !ditherer.isValid()) {
      LOG_ERR("STH", "OOM: thumbnail rows");
      return false;
    }
    levels = scratch.get();
    raw = levels + levelBytes;
    row = raw + bitmap.getRowBytes();
    std::fill_n(sums.get(), columns, 0);
    std::fill_n(counts.get(), columns, 0);

    BmpHeader header;
    createBmpHeader(&header, thumbW, thumbH, bitmap.isTopDown() ? BmpRowOrder::TopDown : BmpRowOrder::BottomUp);
    if (out.write(&header, sizeof(header)) != sizeof(header)) return false;

    int pending = -1;  // destination row being accumulated
    for (int fileRow = 0; fileRow < bitmap.getHeight(); ++fileRow) {
      // Every row goes through readNextRow: its error diffusion runs in file order.
      if (bitmap.readNextRow(levels, raw) != BmpReaderError::Ok) return false;
      const int y = bitmap.isTopDown() ? fileRow : bitmap.getHeight() - 1 - fileRow;
      if (layout.dstW <= 0 || y < layout.srcY || y >= layout.srcY + layout.srcH) continue;
      const int destRow = layout.dstY + (y - layout.srcY) * layout.dstH / layout.srcH;
      if (destRow != pending) {
        if (pending >= 0 && !writeImageRow(pending)) return false;
        pending = destRow;
      }
      for (int x = layout.srcX; x < layout.srcX + layout.srcW; ++x) {
        const int col = (x - layout.srcX) * layout.dstW / layout.srcW;
        const int level = (levels[x >> 2] >> (6 - (x & 3) * 2)) & 0x03;
        sums[col] += !blackWhite ? level : (level == 3) != inverted ? 3 : 0;
        ++counts[col];
      }
    }
    if (pending >= 0 && !writeImageRow(pending)) return false;
    return writeBlankUntil(thumbH);
  }

 private:
  int fileIndexOf(const int thumbRow) const { return bitmap.isTopDown() ? thumbRow : thumbH - 1 - thumbRow; }

  // Margins (and any row the downscale skipped) are plain background, with no
  // dither error carried across them.
  bool writeBlankUntil(const int fileIndex) {
    for (; written < fileIndex; ++written) {
      std::fill_n(row, outRowBytes, inverted ? 0x00 : 0xFF);  // palette index 1 is white
      ditherer.reset();
      if (out.write(row, outRowBytes) != outRowBytes) return false;
    }
    return true;
  }

  bool writeImageRow(const int thumbRow) {
    // Destination rows complete in file order, so this never moves backwards.
    if (!writeBlankUntil(fileIndexOf(thumbRow))) return false;
    std::fill_n(row, outRowBytes, 0);
    for (int x = 0; x < thumbW; ++x) {
      const int col = x - layout.dstX;
      int gray = inverted ? 0 : 255;
      if (col >= 0 && col < layout.dstW && counts[col] > 0) {
        gray = static_cast<int>(sums[col] * 85 / counts[col]);
        sums[col] = 0;
        counts[col] = 0;
      }
      if (ditherer.processPixel(gray, x)) row[x >> 3] |= 0x80 >> (x & 7);
    }
    ditherer.nextRow();
    ++written;
    return out.write(row, outRowBytes) == outRowBytes;
  }

  const Bitmap& bitmap;
  const sleep_pick::ThumbLayout& layout;
  const int thumbW;
  const int thumbH;
  const bool blackWhite;
  const bool inverted;
  HalFile& out;
  Atkinson1BitDitherer ditherer;
  std::unique_ptr<uint8_t[]> scratch;
  std::unique_ptr<uint32_t[]> sums;
  std::unique_ptr<uint16_t[]> counts;
  uint8_t* levels = nullptr;
  uint8_t* raw = nullptr;
  uint8_t* row = nullptr;
  size_t outRowBytes = 0;
  int written = 0;  // thumbnail rows written, in file order
};

bool parseKeyName(const char* name, uint32_t& key) {
  if (strlen(name) != kNameChars + strlen(kExt) || strcmp(name + kNameChars, kExt) != 0) return false;
  for (size_t i = 0; i < kNameChars; ++i) {
    const char c = name[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  key = static_cast<uint32_t>(strtoul(name, nullptr, 16));
  return true;
}
}  // namespace

std::string pathFor(const uint32_t key) {
  char path[sizeof(kDir) + 1 + kNameChars + sizeof(kExt)];
  snprintf(path, sizeof(path), "%s/%08" PRIx32 "%s", kDir, key, kExt);
  return path;
}

bool generate(const std::string& source, const uint32_t key, const int thumbW, const int thumbH, const int screenW,
              const int screenH, const bool crop, const uint8_t filter) {
  if (thumbW <= 0 || thumbH <= 0) return false;
  HalFile in;
  if (!Storage.openFileForRead("STH", source, in)) return false;
  // Even thresholds: the 4 levels average back to gray as level * 85. The B/W
  // filters use the legacy ones, like the sleep screen (originalSleepThresholds).
  Bitmap bitmap(in, true, filter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  const auto layout =
      sleep_pick::thumbLayout(bitmap.getWidth(), bitmap.getHeight(), screenW, screenH, thumbW, thumbH, crop);

  const std::string path = pathFor(key);
  const std::string tmp = path.substr(0, path.size() - strlen(kExt)) + ".tmp";
  HalFile out;
  if (!Storage.ensureDirectoryExists(kDir) || !Storage.openFileForWrite("STH", tmp, out)) return false;
  const bool written = ThumbStreamer(bitmap, layout, thumbW, thumbH, filter, out).run();
  out.close();
  if (!written || !Storage.replaceFile(tmp.c_str(), path.c_str())) {
    LOG_ERR("STH", "Failed to build %s from %s", path.c_str(), source.c_str());
    Storage.remove(tmp.c_str());
    return false;
  }
  return true;
}

void prune(const std::vector<uint32_t>& keep) {
  std::vector<std::string> stale;
  {
    auto dir = Storage.open(kDir);
    if (!dir || !dir.isDirectory()) return;
    char name[32];
    for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
      if (file.isDirectory()) continue;
      file.getName(name, sizeof(name));
      uint32_t key = 0;
      if (!parseKeyName(name, key) || !std::binary_search(keep.begin(), keep.end(), key)) stale.emplace_back(name);
    }
  }
  for (const auto& name : stale) {
    const std::string path = std::string(kDir) + "/" + name;
    Storage.remove(path.c_str());
  }
  if (!stale.empty()) LOG_DBG("STH", "Pruned %u thumbnails", static_cast<unsigned>(stale.size()));
}

}  // namespace sleep_thumbs
