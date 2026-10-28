#pragma once

// Host-only Klondike solver with every card known, used to build and check the
// solvable-deal bank. Depth-first over klondike's own board rules with a
// visited-state set, so any line it returns is legal by construction.

#include <cstdint>
#include <vector>

#include "Klondike.h"

namespace klondike {

struct SolverStep {
  uint8_t from;
  uint8_t to;
  uint8_t count;
  bool draw;
};

// True with a winning line when one is found within `nodeLimit` positions.
bool solve(const Board& start, bool drawThree, uint32_t nodeLimit, std::vector<SolverStep>& line);

}  // namespace klondike
