#pragma once

#include <memory>

class Activity;
class GfxRenderer;
class MappedInputManager;

// Entry points for the home tab bar: each replaces the current screen, as tabs do.
// libraryTab: 0 Читаю, 1 Прочитано, 2 Отложено, 3 Полки.
void openStatistics(GfxRenderer& renderer, MappedInputManager& mappedInput);
void openLibraryTabs(GfxRenderer& renderer, MappedInputManager& mappedInput, int libraryTab = 0);
