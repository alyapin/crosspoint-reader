#pragma once

#include <cstdint>
#include <string>
#include <vector>

// SD cache of the picker's 1-bit sleep picture thumbnails: one top-down 1-bit
// BMP per sleep_pick::thumbKey, so reopening the picker skips the decode.
namespace sleep_thumbs {

inline constexpr char kDir[] = "/.crosspoint/cache/sleepthumbs";

std::string pathFor(uint32_t key);

// Decodes the sleep BMP at `source` into a thumbW x thumbH thumbnail of the
// whole sleep screen (sleep_pick::thumbLayout) under the sleep screen's
// `filter` (CrossPointSettings::SLEEP_SCREEN_COVER_FILTER), Atkinson-dithered
// to 1 bit, and stores it at pathFor(key). Streams row by row: peak memory is
// a source row plus a few thumbnail rows (about 6 KB for a 480 px wide 24-bit
// picture).
bool generate(const std::string& source, uint32_t key, int thumbW, int thumbH, int screenW, int screenH, bool crop,
              uint8_t filter);

// Deletes cached thumbnails whose key is not in `keep` (sorted ascending), and
// leftovers of interrupted writes.
void prune(const std::vector<uint32_t>& keep);

}  // namespace sleep_thumbs
