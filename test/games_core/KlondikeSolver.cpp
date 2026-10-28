#include "KlondikeSolver.h"

#include <algorithm>
#include <unordered_set>

namespace klondike {
namespace {

uint64_t mix(uint64_t h, const uint8_t v) { return (h ^ v) * 1099511628211ull; }

struct Search {
  bool drawThree;
  uint32_t limit;
  uint32_t nodes = 0;
  std::unordered_set<uint64_t> seen;
  std::vector<SolverStep>& line;

  // Columns are interchangeable, so they are hashed as a sorted set.
  uint64_t hash(const Board& b) const {
    uint64_t columns[TABLEAU_PILES];
    for (int t = 0; t < TABLEAU_PILES; ++t) {
      const Pile& p = b.pile[TABLEAU + t];
      uint64_t h = 1469598103934665603ull;
      for (int c = 0; c < p.count; ++c) h = mix(h, p.cards[c]);
      columns[t] = mix(h, 0xFE);
    }
    std::sort(columns, columns + TABLEAU_PILES);
    uint64_t h = 1469598103934665603ull;
    for (int i = STOCK; i <= WASTE; ++i) {
      for (int c = 0; c < b.pile[i].count; ++c) h = mix(h, b.pile[i].cards[c]);
      h = mix(h, 0xFD);
    }
    for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) h = mix(h, b.pile[f].top());
    for (const uint64_t c : columns) h = (h ^ c) * 1099511628211ull;
    return h;
  }

  bool tryStep(Board& b, const SolverStep& s, const int depth) {
    Move m;
    if (s.draw ? !applyDraw(b, drawThree, m) : !applyMove(b, s.from, s.to, s.count, m)) return false;
    line.push_back(s);
    if (dfs(b, depth + 1)) return true;
    line.pop_back();
    revert(b, m);
    return false;
  }

  bool dfs(Board& b, const int depth) {
    if (isWon(b)) return true;
    if (++nodes > limit || depth > 1000) return false;
    if (!seen.insert(hash(b)).second) return false;

    // A safe foundation move never hurts, so it is played without branching.
    for (int from = WASTE; from < PILES; ++from) {
      if (isFoundation(from) || b.pile[from].count == 0 || !isSafeToFoundation(b, b.pile[from].top())) continue;
      for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
        if (canMove(b, from, f, 1))
          return tryStep(b, {static_cast<uint8_t>(from), static_cast<uint8_t>(f), 1, false}, depth);
      }
    }

    std::vector<SolverStep> steps;
    auto add = [&](const int from, const int to, const int count) {
      steps.push_back({static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(count), false});
    };
    for (int from = WASTE; from < PILES; ++from) {
      if (isFoundation(from)) continue;
      for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
        if (canMove(b, from, f, 1)) add(from, f, 1);
      }
    }
    for (int from = TABLEAU; from < PILES; ++from) {
      const Pile& p = b.pile[from];
      int firstUp = p.count;
      while (firstUp > 0 && isFaceUp(p.cards[firstUp - 1])) --firstUp;
      for (int j = firstUp; j < p.count; ++j) {
        const int count = p.count - j;
        bool useful;
        if (j == firstUp) {
          // Uncovers a card, or empties a column for a king.
          useful = firstUp > 0 || rankOf(p.cards[j]) != RANKS - 1;
        } else {
          // Frees the card underneath for a foundation.
          useful = false;
          for (int f = FOUNDATION; f < FOUNDATION + FOUNDATIONS; ++f) {
            const Pile& fp = b.pile[f];
            const uint8_t under = p.cards[j - 1];
            useful |= fp.count == 0 ? rankOf(under) == 0
                                    : suitOf(fp.top()) == suitOf(under) && rankOf(under) == rankOf(fp.top()) + 1;
          }
        }
        if (!useful) continue;
        bool triedEmpty = false;
        for (int to = TABLEAU; to < PILES; ++to) {
          if (!canMove(b, from, to, count)) continue;
          if (b.pile[to].count == 0) {
            if (triedEmpty) continue;
            triedEmpty = true;
          }
          add(from, to, count);
        }
      }
    }
    bool triedEmpty = false;
    for (int to = TABLEAU; to < PILES; ++to) {
      if (!canMove(b, WASTE, to, 1)) continue;
      if (b.pile[to].count == 0) {
        if (triedEmpty) continue;
        triedEmpty = true;
      }
      add(WASTE, to, 1);
    }
    for (int from = FOUNDATION; from < FOUNDATION + FOUNDATIONS; ++from) {
      for (int to = TABLEAU; to < PILES; ++to) {
        if (b.pile[to].count > 0 && canMove(b, from, to, 1)) add(from, to, 1);
      }
    }
    if (b.pile[STOCK].count > 0 || b.pile[WASTE].count > 0) steps.push_back({STOCK, WASTE, 0, true});

    for (const SolverStep& s : steps) {
      if (tryStep(b, s, depth)) return true;
      if (nodes > limit) return false;
    }
    return false;
  }
};

}  // namespace

bool solve(const Board& start, const bool drawThree, const uint32_t nodeLimit, std::vector<SolverStep>& line) {
  line.clear();
  Board b = start;
  Search search{drawThree, nodeLimit, 0, {}, line};
  return search.dfs(b, 0);
}

}  // namespace klondike
