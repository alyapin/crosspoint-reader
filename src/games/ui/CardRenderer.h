#pragma once

#include <GfxRenderer.h>

// Playing cards of the solitaire boards (Klondike, FreeCell, Spider): a white
// card in a 1 px rounded outline with the rank and suit in its top-left
// corner, the back dithered. Red suits are outlines, black ones filled. Suits
// in Klondike order: spades, hearts, diamonds, clubs.
namespace games::ui {

struct CardStyle {
  int width;
  int height;
  int font;      // rank, SemiBold
  int suitSize;  // suit glyph box
  int inset;     // card edge to the rank
};

void drawSuit(const GfxRenderer& renderer, int x, int y, int size, int suit);
void drawCard(const GfxRenderer& renderer, const CardStyle& style, int x, int y, int rank, int suit, bool faceUp);
void drawDashedRect(const GfxRenderer& renderer, int x, int y, int width, int height);

}  // namespace games::ui
