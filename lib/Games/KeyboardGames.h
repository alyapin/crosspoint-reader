#pragma once

// The keyboard games (Сканворды, Виселица, 5 букв) take too many key presses on the button-only X4 and
// X3, so their builds compile none of it: activities, logic and word lists. Host builds name no device
// and keep them for the tests.
#define SOVA_KEYBOARD_GAMES (!(FREEINK_DEVICE_X4 || FREEINK_DEVICE_X3))
