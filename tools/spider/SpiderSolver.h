#pragma once

// Host-only Spider solver with every card known, used to build and check the
// solvable-deal bank. Best-first over spider's own board rules with a
// visited-state set, so any line it returns is legal by construction.

#include <cstdint>
#include <vector>

#include "Spider.h"

namespace spider {

// True with a winning line when one is found within `nodeLimit` positions.
bool solve(const Board& start, uint32_t nodeLimit, std::vector<Step>& line);

}  // namespace spider
