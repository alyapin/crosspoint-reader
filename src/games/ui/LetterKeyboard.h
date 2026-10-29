#pragma once

#include <cstdint>

class GfxRenderer;

namespace games::ui {

// The Russian keyboard of the word games (Сканворд, 5 букв, Виселица): ЙЦУКЕН in three full-width rows of
// eleven keys split by 1 px lines, Ё and Ъ closing the last row. A 2 px rule runs above the keys; the
// action bar's rule closes the last row, and the game's actions (Стереть, Готово) are bar slots.
constexpr int kKeyboardRows = 3;
constexpr int kKeyboardColumns = 11;
constexpr int kKeyboardKeyHeight = 43;
constexpr int kKeyboardHeight = kKeyboardRows * kKeyboardKeyHeight;  // the rule above not included
constexpr int kKeyboardStartKey = kKeyboardColumns + 6;              // О, where a key cursor starts

// The letter of a key (row * kKeyboardColumns + column), numbered А=1 … Я=33 with Ё=7 like the scanword and
// hangman letters, and its upper-case code point.
uint8_t keyboardLetter(int key);
uint32_t keyboardCodepoint(int key);

// Plain: the letter. Placed: white on black. Present: the letter in a 4 px inset frame. Spent: light grey,
// no letter.
enum class KeyLook : uint8_t { Plain, Placed, Present, Spent };
using KeyLookFn = KeyLook (*)(const void* ctx, int key);

// Draws the rule and the keys from `top` (the first key row); `cursor` is a key or -1. The key cursor is the
// 4 px inset frame: inside a Present key's own, and white on a Placed key.
void drawKeyboard(const GfxRenderer& renderer, int top, KeyLookFn look, const void* ctx, int cursor);
// The key under a point, -1 off the keys.
int keyboardKeyAt(const GfxRenderer& renderer, int top, int x, int y);

}  // namespace games::ui
