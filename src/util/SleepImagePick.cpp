#include "SleepImagePick.h"

#include <algorithm>
#include <cmath>

namespace sleep_pick {

namespace {
bool endsWithBmp(const std::string_view name) {
  constexpr std::string_view ext = ".bmp";
  if (name.size() <= ext.size()) return false;
  const auto tail = name.substr(name.size() - ext.size());
  return std::equal(tail.begin(), tail.end(), ext.begin(), [](const char a, const char b) {
    return (a >= 'A' && a <= 'Z' ? static_cast<char>(a - 'A' + 'a') : a) == b;
  });
}

// Bump when the thumbnail pixels change for the same inputs.
constexpr uint8_t kThumbFormat = 1;

uint32_t fnv1a(uint32_t hash, const void* data, const size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t fnv1aLe(const uint32_t hash, const uint32_t value) {
  const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
  return fnv1a(hash, bytes, sizeof(bytes));
}
}  // namespace

bool isChoicePath(const std::string_view path) {
  if (path.size() >= kMaxPathBytes) return false;
  for (const auto folder : kFolders) {
    if (path.size() <= folder.size() + 1 || path.substr(0, folder.size()) != folder || path[folder.size()] != '/') {
      continue;
    }
    const auto name = path.substr(folder.size() + 1);
    return name.front() != '.' && name.find_first_of("/\\") == std::string_view::npos && endsWithBmp(name);
  }
  return false;
}

SourceOrder sourceOrder(const bool hasChoice) {
  if (hasChoice) return {{Source::Chosen, Source::Random, Source::RootBmp}, 3};
  return {{Source::RootBmp, Source::Random}, 2};
}

uint32_t thumbKey(const std::string_view path, const uint32_t fileSize, const uint32_t mtime, const int thumbW,
                  const int thumbH, const bool crop) {
  uint32_t hash = 2166136261u;
  hash = fnv1a(hash, path.data(), path.size());
  const uint8_t separator = 0;
  hash = fnv1a(hash, &separator, 1);
  hash = fnv1aLe(hash, fileSize);
  hash = fnv1aLe(hash, mtime);
  hash = fnv1aLe(hash, static_cast<uint32_t>(thumbW));
  hash = fnv1aLe(hash, static_cast<uint32_t>(thumbH));
  const uint8_t tail[2] = {static_cast<uint8_t>(crop ? 1 : 0), kThumbFormat};
  return fnv1a(hash, tail, sizeof(tail));
}

ThumbLayout thumbLayout(const int imageW, const int imageH, const int screenW, const int screenH, const int thumbW,
                        const int thumbH, const bool crop) {
  ThumbLayout out;
  if (imageW <= 0 || imageH <= 0 || screenW <= 0 || screenH <= 0 || thumbW <= 0 || thumbH <= 0) return out;

  // SleepActivity's calculateBitmapPlacement().
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
  if (imageW > screenW || imageH > screenH) {
    float ratio = static_cast<float>(imageW) / static_cast<float>(imageH);
    const float screenRatio = static_cast<float>(screenW) / static_cast<float>(screenH);
    if (ratio > screenRatio) {
      if (crop) {
        cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - cropX) * static_cast<float>(imageW) / static_cast<float>(imageH);
      }
      y = static_cast<int>(std::round((static_cast<float>(screenH) - static_cast<float>(screenW) / ratio) / 2));
    } else {
      if (crop) {
        cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(imageW) / ((1.0f - cropY) * static_cast<float>(imageH));
      }
      x = static_cast<int>(std::round((static_cast<float>(screenW) - static_cast<float>(screenH) * ratio) / 2));
    }
  } else {
    x = (screenW - imageW) / 2;
    y = (screenH - imageH) / 2;
  }

  // GfxRenderer::drawBitmap(): symmetric crop, then a downscale-only fit.
  const int cropPixX = static_cast<int>(std::floor(static_cast<float>(imageW) * cropX / 2.0f));
  const int cropPixY = static_cast<int>(std::floor(static_cast<float>(imageH) * cropY / 2.0f));
  const float croppedW = (1.0f - cropX) * static_cast<float>(imageW);
  const float croppedH = (1.0f - cropY) * static_cast<float>(imageH);
  const float scale = std::min({static_cast<float>(screenW) / croppedW, static_cast<float>(screenH) / croppedH, 1.0f});
  const int srcW = imageW - 2 * cropPixX;
  const int srcH = imageH - 2 * cropPixY;
  const int drawnW = std::max(1, static_cast<int>(std::lround(static_cast<float>(srcW) * scale)));
  const int drawnH = std::max(1, static_cast<int>(std::lround(static_cast<float>(srcH) * scale)));

  // Clip to the screen and carry the clipped edges back into the source window.
  const int left = std::max(x, 0);
  const int right = std::min(x + drawnW, screenW);
  const int top = std::max(y, 0);
  const int bottom = std::min(y + drawnH, screenH);
  if (right <= left || bottom <= top) return out;
  const auto toSource = [scale](const int screenPx) { return static_cast<int>(std::lround(screenPx / scale)); };
  out.srcX = std::clamp(cropPixX + toSource(left - x), cropPixX, cropPixX + srcW - 1);
  out.srcY = std::clamp(cropPixY + toSource(top - y), cropPixY, cropPixY + srcH - 1);
  out.srcW = std::clamp(toSource(right - left), 1, cropPixX + srcW - out.srcX);
  out.srcH = std::clamp(toSource(bottom - top), 1, cropPixY + srcH - out.srcY);

  // Screen pixels to thumbnail pixels.
  const auto toThumb = [](const int px, const int screen, const int thumb) {
    return static_cast<int>(std::lround(static_cast<double>(px) * thumb / screen));
  };
  out.dstX = std::min(toThumb(left, screenW, thumbW), thumbW - 1);
  out.dstY = std::min(toThumb(top, screenH, thumbH), thumbH - 1);
  out.dstW = std::clamp(toThumb(right, screenW, thumbW) - out.dstX, 1, thumbW - out.dstX);
  out.dstH = std::clamp(toThumb(bottom, screenH, thumbH) - out.dstY, 1, thumbH - out.dstY);
  return out;
}

}  // namespace sleep_pick
