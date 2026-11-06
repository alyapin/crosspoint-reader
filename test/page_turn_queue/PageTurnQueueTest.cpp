#include <gtest/gtest.h>

#include "activities/reader/PageTurnQueue.h"

TEST(PageTurnQueue, StartsEmpty) {
  PageTurnQueue queue;
  EXPECT_FALSE(queue.pending());
  EXPECT_EQ(queue.pop(), 0);
}

// Five presses during one refresh are five pages.
TEST(PageTurnQueue, KeepsEveryPress) {
  PageTurnQueue queue;
  for (int i = 0; i < 5; i++) queue.push(true);
  int pages = 0;
  while (queue.pending()) pages += queue.pop();
  EXPECT_EQ(pages, 5);
  EXPECT_EQ(queue.pop(), 0);
}

TEST(PageTurnQueue, TurnsBack) {
  PageTurnQueue queue;
  queue.push(false);
  queue.push(false);
  EXPECT_EQ(queue.pop(), -1);
  EXPECT_EQ(queue.pop(), -1);
  EXPECT_FALSE(queue.pending());
}

TEST(PageTurnQueue, OppositePressesCancel) {
  PageTurnQueue queue;
  queue.push(true);
  queue.push(true);
  queue.push(false);
  EXPECT_EQ(queue.pop(), 1);
  EXPECT_FALSE(queue.pending());
  queue.push(true);
  queue.push(false);
  EXPECT_FALSE(queue.pending());
}

TEST(PageTurnQueue, StopsAtTheCap) {
  PageTurnQueue queue;
  for (int i = 0; i < PageTurnQueue::kMaxTurns + 5; i++) queue.push(true);
  int pages = 0;
  while (queue.pending()) pages += queue.pop();
  EXPECT_EQ(pages, PageTurnQueue::kMaxTurns);
  for (int i = 0; i < PageTurnQueue::kMaxTurns + 5; i++) queue.push(false);
  queue.push(true);
  pages = 0;
  while (queue.pending()) pages += queue.pop();
  EXPECT_EQ(pages, 1 - PageTurnQueue::kMaxTurns);
}

TEST(PageTurnQueue, ClearDropsEverything) {
  PageTurnQueue queue;
  queue.push(true);
  queue.push(true);
  queue.clear();
  EXPECT_FALSE(queue.pending());
  EXPECT_EQ(queue.pop(), 0);
}
