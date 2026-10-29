#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

// Pure pieces of the sleep picture picker (Settings → Display → Sleep image),
// kept free of device code so test/sleep_image_pick covers them on the host.
namespace sleep_pick {

// The folders the Custom sleep screen picks from, in its random-pick order.
inline constexpr std::array<std::string_view, 2> kFolders = {"/.sleep", "/sleep"};
// Room for a chosen picture's full path, including the terminator
// (CrossPointSettings::sleepImagePath).
inline constexpr size_t kMaxPathBytes = 192;

// True for a path the picker may store: a visible .bmp directly inside one of
// kFolders that fits kMaxPathBytes.
bool isChoicePath(std::string_view path);

// The sources the Custom sleep screen tries, in order. A chosen picture comes
// first and falls back to a random one. /sleep.bmp keeps its old first place
// only while nothing is picked, not even the Random tile (randomPicked), and
// is otherwise the last resort when the folders are empty.
enum class Source : uint8_t { Chosen, RootBmp, Random };
struct SourceOrder {
  std::array<Source, 3> items{};
  size_t count = 0;
  const Source* begin() const { return items.data(); }
  const Source* end() const { return items.data() + count; }
};
SourceOrder sourceOrder(bool hasChoice, bool randomPicked);

// Thumbnail cache key: FNV-1a over everything that changes a thumbnail.
uint32_t thumbKey(std::string_view path, uint32_t fileSize, uint32_t mtime, int thumbW, int thumbH, bool crop);

// Where a sleep image lands in a thumbnail of the whole sleep screen, mirroring
// SleepActivity's fit/crop placement and GfxRenderer::drawBitmap's scaling:
// the source window in image pixels (top-down rows) and the destination rect
// in thumbnail pixels. dstW/dstH are 0 when nothing of the image is visible.
struct ThumbLayout {
  int srcX = 0;
  int srcY = 0;
  int srcW = 0;
  int srcH = 0;
  int dstX = 0;
  int dstY = 0;
  int dstW = 0;
  int dstH = 0;
};
ThumbLayout thumbLayout(int imageW, int imageH, int screenW, int screenH, int thumbW, int thumbH, bool crop);

}  // namespace sleep_pick
