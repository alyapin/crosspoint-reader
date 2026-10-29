#include "SpiderSolver.h"

#include <queue>
#include <string>
#include <unordered_set>
#include <utility>

namespace spider {
namespace {

using namespace cards;

constexpr char END = static_cast<char>(0xFE);

struct Node {
  std::string key;
  int parent;
  Step step;
};

std::string encode(const Board& b) {
  std::string s;
  for (const Pile& p : b.column) {
    s.append(reinterpret_cast<const char*>(p.cards), p.count);
    s.push_back(END);
  }
  s.push_back(static_cast<char>(b.stockCount));
  s.push_back(static_cast<char>(b.doneCount));
  return s;
}

// The stock never changes but for its count, so the start board supplies it.
Board decode(const std::string& s, const Board& start) {
  Board b = start;
  size_t i = 0;
  for (Pile& p : b.column) {
    p.count = 0;
    while (s[i] != END) p.cards[p.count++] = static_cast<uint8_t>(s[i++]);
    ++i;
  }
  b.stockCount = static_cast<uint8_t>(s[i]);
  b.doneCount = static_cast<uint8_t>(s[i + 1]);
  return b;
}

int score(const Board& b) {
  int value = 400 * b.doneCount;
  for (const Pile& p : b.column) {
    if (p.count == 0) value += 30;
    for (int i = 0; i < p.count; ++i) {
      if (!isFaceUp(p.cards[i])) {
        value -= 20;
      } else if (i > 0 && isFaceUp(p.cards[i - 1]) && rankOf(p.cards[i - 1]) == rankOf(p.cards[i]) + 1) {
        value += suitOf(p.cards[i - 1]) == suitOf(p.cards[i]) ? 10 : 3;
      }
    }
  }
  return value;
}

// Moves worth trying: no run leaves its own parent for a card of another suit,
// no whole column moves to an empty one, and one empty column stands for all.
void candidates(const Board& b, std::vector<Step>& out) {
  out.clear();
  int firstEmpty = -1;
  for (int c = 0; c < COLUMNS && firstEmpty < 0; ++c) firstEmpty = b.column[c].count == 0 ? c : -1;
  for (int from = 0; from < COLUMNS; ++from) {
    const Pile& p = b.column[from];
    for (int n = 1; runLength(b, from, p.count - n) > 0; ++n) {
      const int base = p.count - n;
      const uint8_t card = p.cards[base];
      const bool onParent = base > 0 && isFaceUp(p.cards[base - 1]) && rankOf(p.cards[base - 1]) == rankOf(card) + 1;
      for (int to = 0; to < COLUMNS; ++to) {
        if (!canMove(b, from, to, n)) continue;
        if (b.column[to].count == 0) {
          if (to != firstEmpty || base == 0) continue;
        } else if (onParent &&
                   (suitOf(b.column[to].top()) != suitOf(card) || suitOf(p.cards[base - 1]) == suitOf(card))) {
          continue;
        }
        out.push_back(Step{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(n)});
      }
    }
  }
  if (canDeal(b)) out.push_back(Step{STOCK, 0, 0});
}

}  // namespace

bool solve(const Board& start, const uint32_t nodeLimit, std::vector<Step>& line) {
  line.clear();
  std::vector<Node> nodes;
  std::unordered_set<std::string> seen;
  std::priority_queue<std::pair<int, int>> open;
  nodes.push_back(Node{encode(start), -1, {}});
  seen.insert(nodes[0].key);
  open.emplace(score(start), 0);
  std::vector<Step> moves;
  while (!open.empty() && nodes.size() < nodeLimit) {
    const int index = open.top().second;
    open.pop();
    const Board board = decode(nodes[index].key, start);
    candidates(board, moves);
    for (const Step& step : moves) {
      Board next = board;
      if (!(step.from == STOCK ? applyDeal(next) : applyMove(next, step.from, step.to, step.count))) continue;
      std::string key = encode(next);
      if (!seen.insert(key).second) continue;
      nodes.push_back(Node{std::move(key), index, step});
      if (isWon(next)) {
        for (int i = static_cast<int>(nodes.size()) - 1; nodes[i].parent >= 0; i = nodes[i].parent) {
          line.insert(line.begin(), nodes[i].step);
        }
        return true;
      }
      open.emplace(score(next), static_cast<int>(nodes.size()) - 1);
    }
  }
  return false;
}

}  // namespace spider
