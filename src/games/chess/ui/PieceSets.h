#pragma once

#include <cstdint>

class GfxRenderer;

namespace chess::ui {

enum class PieceSet : uint8_t { Lucide, Tabler, Material, FontAwesome };
constexpr uint8_t PIECE_SET_COUNT = 4;

enum class PieceColor : uint8_t { White, Black };

// Values match chess::PieceType (PAWN = 1 ... KING = 6), so a static_cast converts.
enum class PieceKind : uint8_t { Pawn = 1, Knight, Bishop, Rook, Queen, King };

// Board: 56 px for 60 px squares. Small: 32 px for captured pieces and the promotion picker.
enum class PieceSize : uint8_t { Board, Small };

// A size x size piece as two 1-bpp planes, rows top-to-bottom, (size + 7) / 8
// bytes per row, MSB-first, bit 0 = set (the freeink::Icon convention).
// mask: the pixels the piece covers, i.e. the artwork, its interior and a 2 px
// halo. ink: the black pixels; every other mask pixel is white.
struct PieceBitmap {
  uint8_t size;
  const uint8_t* mask;
  const uint8_t* ink;
};

// Out-of-range arguments (e.g. a stale saved set) fall back to Lucide / White / Pawn / Board.
const PieceBitmap& pieceBitmap(PieceSet set, PieceColor color, PieceKind kind, PieceSize size);

// Paints the piece's mask pixels with its top-left corner at (x, y): black where
// inked, white elsewhere. Pixels outside the mask keep what is underneath.
void drawPiece(const GfxRenderer& renderer, const PieceBitmap& piece, int x, int y);

}  // namespace chess::ui
