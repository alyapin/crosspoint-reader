#include "PieceSets.h"

#if FREEINK_DEVICE_X4PRO

#include <GfxRenderer.h>

#include "PieceSetsData.h"

namespace chess::ui {

const PieceBitmap& pieceBitmap(PieceSet set, const PieceColor color, const PieceKind kind, const PieceSize size) {
  if (static_cast<uint8_t>(set) >= PIECE_SET_COUNT) set = PieceSet::Lucide;
  return piece_bitmaps[static_cast<uint8_t>(set)][static_cast<uint8_t>(color)][static_cast<uint8_t>(kind)]
                      [static_cast<uint8_t>(size)];
}

void drawPiece(const GfxRenderer& renderer, const PieceBitmap& piece, const int x, const int y) {
  const int rowBytes = (piece.size + 7) / 8;
  for (int row = 0; row < piece.size; row++) {
    for (int col = 0; col < rowBytes; col++) {
      const int i = row * rowBytes + col;
      const uint8_t covered = static_cast<uint8_t>(~piece.mask[i]);
      if (covered == 0) continue;
      const uint8_t black = static_cast<uint8_t>(~piece.ink[i]);
      for (int bit = 0; bit < 8; bit++) {
        const uint8_t m = 0x80 >> bit;
        if (covered & m) renderer.drawPixel(x + col * 8 + bit, y + row, (black & m) != 0);
      }
    }
  }
}

}  // namespace chess::ui

#endif  // FREEINK_DEVICE_X4PRO
