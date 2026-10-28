#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Minesweeper.h"

using namespace minesweeper;

namespace {

uint32_t fakeNow = 0;
uint32_t fakeMillis() { return fakeNow; }

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

// Mines are read from the save text: '0' + bits, bit 0 = mine.
bool isMine(const Game& g, const int x, const int y) {
  const std::string s = save(g);
  const size_t at = s.find("\"cells\":\"") + 9;
  return (s[at + y * g.columns() + x] - '0') & 1;
}

int openCount(const Game& g) {
  int n = 0;
  for (int y = 0; y < g.rows(); ++y) {
    for (int x = 0; x < g.columns(); ++x) n += g.view(x, y) == View::Open;
  }
  return n;
}

// No open zero next to a covered unflagged cell: the flood went all the way.
void expectFloodComplete(const Game& g) {
  for (int y = 0; y < g.rows(); ++y) {
    for (int x = 0; x < g.columns(); ++x) {
      if (g.view(x, y) != View::Open || g.adjacent(x, y) != 0) continue;
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= g.columns() || ny >= g.rows()) continue;
          EXPECT_NE(g.view(nx, ny), View::Covered) << x << "," << y;
        }
      }
    }
  }
}

// A player that sees only what the screen shows and never guesses: counts,
// subsets of neighbouring numbers, then the mine counter. Independent of the
// core's own solver. Returns whether it won.
bool playLogically(Game& g) {
  const int w = g.columns();
  const int h = g.rows();
  auto inside = [&](const int x, const int y) { return x >= 0 && y >= 0 && x < w && y < h; };
  auto covered = [&](const int x, const int y) { return inside(x, y) && g.view(x, y) == View::Covered; };
  auto flagged = [&](const int x, const int y) { return inside(x, y) && g.view(x, y) == View::Flagged; };
  auto need = [&](const int x, const int y) {
    int n = g.adjacent(x, y);
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) n -= flagged(x + dx, y + dy);
    }
    return n;
  };
  auto unknowns = [&](const int x, const int y) {
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) n += covered(x + dx, y + dy);
    }
    return n;
  };
  while (g.status() == Status::Playing) {
    bool progress = false;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        if (g.view(x, y) != View::Open || unknowns(x, y) == 0) continue;
        const int n = need(x, y);
        if (n != 0 && n != unknowns(x, y)) continue;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (!covered(x + dx, y + dy)) continue;
            progress |= n == 0 ? g.reveal(x + dx, y + dy) : g.toggleFlag(x + dx, y + dy);
          }
        }
      }
    }
    if (progress) continue;
    for (int ay = 0; ay < h && !progress; ++ay) {
      for (int ax = 0; ax < w && !progress; ++ax) {
        if (g.view(ax, ay) != View::Open || unknowns(ax, ay) == 0) continue;
        for (int by = ay - 2; by <= ay + 2 && !progress; ++by) {
          for (int bx = ax - 2; bx <= ax + 2 && !progress; ++bx) {
            if ((bx == ax && by == ay) || !inside(bx, by) || g.view(bx, by) != View::Open) continue;
            bool subset = true;
            for (int dy = -1; dy <= 1; ++dy) {
              for (int dx = -1; dx <= 1; ++dx) {
                if (covered(ax + dx, ay + dy) && (abs(ax + dx - bx) > 1 || abs(ay + dy - by) > 1)) subset = false;
              }
            }
            if (!subset) continue;
            const int extra = unknowns(bx, by) - unknowns(ax, ay);
            const int extraMines = need(bx, by) - need(ax, ay);
            if (extra == 0 || (extraMines != 0 && extraMines != extra)) continue;
            for (int dy = -1; dy <= 1; ++dy) {
              for (int dx = -1; dx <= 1; ++dx) {
                const int cx = bx + dx;
                const int cy = by + dy;
                if (!covered(cx, cy) || (abs(cx - ax) <= 1 && abs(cy - ay) <= 1)) continue;
                progress |= extraMines == 0 ? g.reveal(cx, cy) : g.toggleFlag(cx, cy);
              }
            }
          }
        }
      }
    }
    if (progress) continue;
    int unknown = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) unknown += covered(x, y);
    }
    const int left = g.minesLeft();
    if (left != 0 && left != unknown) return false;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        if (covered(x, y)) left == 0 ? g.reveal(x, y) : g.toggleFlag(x, y);
      }
    }
  }
  return g.status() == Status::Won;
}

}  // namespace

TEST(Minesweeper, ConfigLimits) {
  EXPECT_TRUE(isValid(BEGINNER));
  EXPECT_TRUE(isValid(PORTRAIT));
  EXPECT_TRUE(isValid(Config{16, 30, 99, true}));
  EXPECT_FALSE(isValid(Config{4, 9, 5, true}));
  EXPECT_FALSE(isValid(Config{17, 9, 5, true}));
  EXPECT_FALSE(isValid(Config{9, 31, 5, true}));
  EXPECT_FALSE(isValid(Config{9, 9, 0, true}));
  EXPECT_FALSE(isValid(Config{9, 9, 73, true}));
  EXPECT_TRUE(isValid(Config{9, 9, 72, false}));
  Game g(fakeMillis);
  EXPECT_FALSE(g.newGame(Config{3, 3, 1, false}, 1));
  EXPECT_EQ(g.columns(), BEGINNER.columns);
}

TEST(Minesweeper, FirstDigIsSafeAndOpensAnArea) {
  const Config configs[] = {BEGINNER, PORTRAIT, Config{9, 9, 72, false}, Config{16, 30, 120, false}};
  for (const Config& config : configs) {
    for (uint32_t seed = 1; seed <= 60; ++seed) {
      Game g(fakeMillis);
      ASSERT_TRUE(g.newGame(config, seed));
      const int x = static_cast<int>(seed * 7 % config.columns);
      const int y = static_cast<int>(seed * 13 % config.rows);
      EXPECT_FALSE(g.toggleFlag(x, y));  // nothing to flag before the mines exist
      ASSERT_TRUE(g.reveal(x, y));
      EXPECT_NE(g.status(), Status::Lost);
      EXPECT_EQ(g.adjacent(x, y), 0);
      int mines = 0;
      for (int cy = 0; cy < config.rows; ++cy) {
        for (int cx = 0; cx < config.columns; ++cx) {
          const bool mine = isMine(g, cx, cy);
          mines += mine;
          if (abs(cx - x) <= 1 && abs(cy - y) <= 1) EXPECT_FALSE(mine);
        }
      }
      EXPECT_EQ(mines, config.mines);
      EXPECT_GE(openCount(g), 4);
      expectFloodComplete(g);
    }
  }
}

TEST(Minesweeper, SameSeedAndDigSameBoard) {
  Game a(fakeMillis);
  Game b(fakeMillis);
  a.newGame(PORTRAIT, 99);
  b.newGame(PORTRAIT, 99);
  a.reveal(5, 7);
  b.reveal(5, 7);
  EXPECT_EQ(save(a), save(b));
  b.newGame(PORTRAIT, 100);
  b.reveal(5, 7);
  EXPECT_NE(save(a), save(b));
}

TEST(Minesweeper, NoGuessBoardsAreWonByLogicAlone) {
  const Config configs[] = {BEGINNER, PORTRAIT, Config{16, 30, 99, true}};
  for (const Config& config : configs) {
    int proved = 0;
    int games = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint32_t seed = 1; seed <= 100; ++seed) {
      Game g(fakeMillis);
      g.newGame(config, seed);
      g.reveal(config.columns / 2, config.rows / 2);
      ++games;
      if (!g.provedNoGuess()) continue;
      ++proved;
      EXPECT_TRUE(playLogically(g)) << int(config.columns) << "x" << int(config.rows) << " seed " << seed;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("minesweeper %dx%d/%d: %d/%d proved no-guess, %.2f ms per first dig (incl. play)\n", config.columns,
           config.rows, config.mines, proved, games, ms / games);
    if (config.mines <= 30) EXPECT_EQ(proved, games);
  }
}

TEST(Minesweeper, NoGuessGenerationTime) {
  for (const Config& config : {BEGINNER, PORTRAIT}) {
    double worst = 0;
    double total = 0;
    for (uint32_t seed = 1; seed <= 200; ++seed) {
      Game g(fakeMillis);
      g.newGame(config, seed);
      const auto t0 = std::chrono::steady_clock::now();
      g.reveal(0, 0);
      const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      total += ms;
      if (ms > worst) worst = ms;
    }
    printf("minesweeper %dx%d/%d first dig: mean %.2f ms, worst %.2f ms\n", config.columns, config.rows, config.mines,
           total / 200, worst);
  }
}

TEST(Minesweeper, FloodStopsAtFlags) {
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    Game g(fakeMillis);
    g.newGame(Config{12, 16, 12, false}, seed);
    ASSERT_TRUE(g.reveal(0, 0));
    // Flag one covered cell, then open the board around it: the flag survives.
    int fx = -1;
    int fy = -1;
    for (int y = 0; y < g.rows() && fx < 0; ++y) {
      for (int x = 0; x < g.columns() && fx < 0; ++x) {
        if (g.view(x, y) == View::Covered && !isMine(g, x, y)) {
          fx = x;
          fy = y;
        }
      }
    }
    if (fx < 0) continue;
    ASSERT_TRUE(g.toggleFlag(fx, fy));
    for (int y = 0; y < g.rows(); ++y) {
      for (int x = 0; x < g.columns(); ++x) {
        if (!isMine(g, x, y) && g.view(x, y) == View::Covered) g.reveal(x, y);
      }
    }
    EXPECT_EQ(g.view(fx, fy), View::Flagged);
    EXPECT_FALSE(g.reveal(fx, fy));
    EXPECT_EQ(g.status(), Status::Playing);
    ASSERT_TRUE(g.toggleFlag(fx, fy));
    ASSERT_TRUE(g.reveal(fx, fy));
    EXPECT_EQ(g.status(), Status::Won);
  }
}

TEST(Minesweeper, ChordOpensTheRestOnlyWhenTheFlagsAddUp) {
  int chords = 0;
  int losses = 0;
  for (uint32_t seed = 1; seed <= 150; ++seed) {
    Game g(fakeMillis);
    g.newGame(PORTRAIT, seed);
    g.reveal(6, 8);
    for (int y = 0; y < g.rows(); ++y) {
      for (int x = 0; x < g.columns(); ++x) {
        if (g.view(x, y) != View::Open || g.adjacent(x, y) == 0) continue;
        int covered = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < g.columns() && ny < g.rows()) {
              covered += g.view(nx, ny) == View::Covered && !isMine(g, nx, ny);
            }
          }
        }
        if (covered == 0) continue;
        // Too few flags: nothing happens.
        EXPECT_FALSE(g.chord(x, y));
        EXPECT_FALSE(g.dig(x, y));

        // Right flags: chord equals revealing each neighbour by hand.
        Game byHand(fakeMillis);
        const std::string text = save(g);
        ASSERT_TRUE(byHand.deserialize(text.data(), text.size()));
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= g.columns() || ny >= g.rows() || g.view(nx, ny) != View::Covered) continue;
            if (isMine(g, nx, ny)) {
              g.toggleFlag(nx, ny);
              byHand.toggleFlag(nx, ny);
            }
          }
        }
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx >= 0 && ny >= 0 && nx < g.columns() && ny < g.rows() && byHand.view(nx, ny) == View::Covered) {
              byHand.reveal(nx, ny);
            }
          }
        }
        ASSERT_TRUE(g.dig(x, y));
        ++chords;
        // Elapsed time differs only by clock state; compare the boards.
        const std::string a = save(g);
        const std::string b = save(byHand);
        EXPECT_EQ(a.substr(a.find("\"cells\"")), b.substr(b.find("\"cells\"")));
        expectFloodComplete(g);
        y = g.rows();
        break;
      }
    }

    // Wrong flags with the right count: the chord digs a mine.
    Game h(fakeMillis);
    h.newGame(PORTRAIT, seed);
    h.reveal(6, 8);
    for (int y = 0; y < h.rows() && h.status() == Status::Playing; ++y) {
      for (int x = 0; x < h.columns() && h.status() == Status::Playing; ++x) {
        if (h.view(x, y) != View::Open || h.adjacent(x, y) != 1) continue;
        int safeX = -1;
        int safeY = -1;
        bool hasMine = false;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= h.columns() || ny >= h.rows() || h.view(nx, ny) != View::Covered) continue;
            if (isMine(h, nx, ny)) {
              hasMine = true;
            } else {
              safeX = nx;
              safeY = ny;
            }
          }
        }
        if (!hasMine || safeX < 0) continue;
        h.toggleFlag(safeX, safeY);
        EXPECT_TRUE(h.chord(x, y));
        EXPECT_EQ(h.status(), Status::Lost);
        EXPECT_EQ(h.view(safeX, safeY), View::WrongFlag);
        ++losses;
      }
    }
  }
  EXPECT_GT(chords, 50);
  EXPECT_GT(losses, 20);
}

TEST(Minesweeper, WinLoseAndClock) {
  fakeNow = 0;
  Game g(fakeMillis);
  g.newGame(BEGINNER, 7);
  fakeNow = 500;
  EXPECT_EQ(g.elapsedMs(), 0u);  // the clock starts with the first dig
  g.reveal(4, 4);
  fakeNow = 2500;
  EXPECT_EQ(g.elapsedMs(), 2000u);
  for (int y = 0; y < g.rows(); ++y) {
    for (int x = 0; x < g.columns(); ++x) {
      if (!isMine(g, x, y)) g.reveal(x, y);
    }
  }
  EXPECT_EQ(g.status(), Status::Won);
  EXPECT_EQ(g.minesLeft(), 0);
  fakeNow = 9000;
  EXPECT_EQ(g.elapsedMs(), 2000u);
  EXPECT_FALSE(g.toggleFlag(0, 0));

  g.newGame(BEGINNER, 8);
  g.reveal(4, 4);
  int mx = -1;
  int my = -1;
  int safeX = -1;
  int safeY = -1;
  for (int y = 0; y < g.rows(); ++y) {
    for (int x = 0; x < g.columns(); ++x) {
      if (g.view(x, y) != View::Covered) continue;
      if (isMine(g, x, y) && mx < 0) {
        mx = x;
        my = y;
      } else if (!isMine(g, x, y) && safeX < 0) {
        safeX = x;
        safeY = y;
      }
    }
  }
  ASSERT_GE(mx, 0);
  ASSERT_GE(safeX, 0);
  g.toggleFlag(safeX, safeY);
  EXPECT_EQ(g.minesLeft(), BEGINNER.mines - 1);
  EXPECT_TRUE(g.reveal(mx, my));
  EXPECT_EQ(g.status(), Status::Lost);
  EXPECT_TRUE(g.isOver());
  EXPECT_EQ(g.view(mx, my), View::Exploded);
  EXPECT_EQ(g.view(safeX, safeY), View::WrongFlag);
  int shown = 0;
  for (int y = 0; y < g.rows(); ++y) {
    for (int x = 0; x < g.columns(); ++x) shown += g.view(x, y) == View::Mine;
  }
  EXPECT_EQ(shown, BEGINNER.mines - 1);
  EXPECT_FALSE(g.reveal(safeX, safeY));
}

TEST(MinesweeperSave, RoundTripsEveryState) {
  fakeNow = 0;
  Game g(fakeMillis);
  g.newGame(PORTRAIT, 21);
  for (int step = 0; step < 4; ++step) {
    if (step == 1) {
      g.reveal(3, 3);
      for (int y = 0; y < g.rows(); ++y) {
        for (int x = 0; x < g.columns(); ++x) {
          if (g.view(x, y) == View::Covered && isMine(g, x, y) && g.minesLeft() > 25) g.toggleFlag(x, y);
        }
      }
      fakeNow += 4321;
    }
    if (step == 2) playLogically(g);
    if (step == 3) {
      g.newGame(PORTRAIT, 22);
      g.reveal(0, 0);
      for (int y = 0; y < g.rows() && !g.isOver(); ++y) {
        for (int x = 0; x < g.columns() && !g.isOver(); ++x) {
          if (isMine(g, x, y)) g.reveal(x, y);
        }
      }
    }
    const std::string text = save(g);
    ASSERT_FALSE(text.empty());
    ASSERT_LT(text.size(), SAVE_MAX);
    Game r(fakeMillis);
    ASSERT_TRUE(r.deserialize(text.data(), text.size())) << step;
    EXPECT_EQ(r.status(), g.status());
    EXPECT_EQ(r.provedNoGuess(), g.provedNoGuess());
    EXPECT_EQ(r.minesLeft(), g.minesLeft());
    EXPECT_TRUE(r.paused());
    EXPECT_EQ(r.elapsedMs(), g.elapsedMs());
    for (int y = 0; y < g.rows(); ++y) {
      for (int x = 0; x < g.columns(); ++x) {
        EXPECT_EQ(r.view(x, y), g.view(x, y));
        EXPECT_EQ(r.adjacent(x, y), g.adjacent(x, y));
      }
    }
    EXPECT_EQ(save(r), text);
  }
  // A restored untouched board deals the same mines as the original would.
  Game fresh(fakeMillis);
  fresh.newGame(BEGINNER, 5);
  const std::string text = save(fresh);
  Game r(fakeMillis);
  ASSERT_TRUE(r.deserialize(text.data(), text.size()));
  EXPECT_EQ(r.status(), Status::Ready);
  fresh.reveal(2, 2);
  r.reveal(2, 2);
  EXPECT_EQ(save(r), save(fresh));
}

TEST(MinesweeperSave, CorruptSavesAreRejected) {
  Game g(fakeMillis);
  g.newGame(BEGINNER, 3);
  g.reveal(4, 4);
  const std::string good = save(g);
  const size_t at = good.find("\"cells\":\"") + 9;

  Game r(fakeMillis);
  const std::string before = save(r);
  auto rejects = [&](const std::string& text) {
    EXPECT_FALSE(r.deserialize(text.data(), text.size())) << text;
    EXPECT_EQ(save(r), before);
  };
  auto edited = [&](const char* from, const char* to) {
    std::string t = good;
    const size_t p = t.find(from);
    return t.replace(p, strlen(from), to);
  };
  rejects("");
  rejects("{\"v\":1}");
  rejects(good.substr(0, good.size() - 5));
  rejects(edited("\"v\":1", "\"v\":9"));
  rejects(edited("\"cols\":9", "\"cols\":4"));
  rejects(edited("\"mines\":10", "\"mines\":11"));
  rejects(edited("\"ng\":1", "\"ng\":2"));
  std::string t = good;
  t[at] = '8';
  rejects(t);
  t = good;
  int covered = -1;
  for (int i = 0; i < 81 && covered < 0; ++i) {
    if (t[at + i] == '0') covered = i;
  }
  t[at + covered] = '6';  // open and flagged at once
  rejects(t);
  t = good;
  for (int i = 0, exploded = 0; i < 81 && exploded < 2; ++i) {
    if (t[at + i] == '1') {
      t[at + i] = '3';  // two mines gone off
      ++exploded;
    }
  }
  rejects(t);
  t = good;
  t.erase(at, 1);
  rejects(t);
  t = good;
  for (int i = 0; i < 81; ++i) t[at + i] = static_cast<char>('0' + ((t[at + i] - '0') & ~2));  // laid, never dug
  rejects(t);
  EXPECT_TRUE(r.deserialize(good.data(), good.size()));
}
