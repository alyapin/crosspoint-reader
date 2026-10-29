#include <KeyQueue.h>
#include <gtest/gtest.h>

#include <vector>

using games::BoardKey;
using games::KeyQueue;

namespace {
std::vector<BoardKey> drained;
void record(const BoardKey& key) { drained.push_back(key); }
}  // namespace

TEST(KeyQueue, StepsApplyInOrderAndAnActionEndsThePass) {
  KeyQueue q;
  q.push({1, 0, BoardKey::Step});
  q.push({1, 0, BoardKey::Step});
  q.push({0, 0, BoardKey::Hold});
  q.push({0, 1, BoardKey::Step});
  q.push({0, 0, BoardKey::Back});

  drained.clear();
  EXPECT_TRUE(q.drain(record));  // two Rights, then the held Confirm on the cell they reached
  ASSERT_EQ(drained.size(), 3u);
  EXPECT_EQ(drained[0].dx, 1);
  EXPECT_EQ(drained[1].dx, 1);
  EXPECT_EQ(drained[2].action, BoardKey::Hold);
  EXPECT_TRUE(q.pending());

  drained.clear();
  EXPECT_TRUE(q.drain(record));  // the Down, then Back
  ASSERT_EQ(drained.size(), 2u);
  EXPECT_EQ(drained[0].dy, 1);
  EXPECT_EQ(drained[1].action, BoardKey::Back);
  EXPECT_FALSE(q.pending());

  drained.clear();
  EXPECT_FALSE(q.drain(record));
  EXPECT_TRUE(drained.empty());
}

TEST(KeyQueue, StepsAloneDrainCompletely) {
  KeyQueue q;
  for (int i = 0; i < 3; ++i) q.push({0, -1, BoardKey::Step});
  drained.clear();
  EXPECT_FALSE(q.drain(record));
  EXPECT_EQ(drained.size(), 3u);
  EXPECT_FALSE(q.pending());
}

TEST(KeyQueue, AFullQueueDropsNewKeysAndClearEmptiesIt) {
  KeyQueue q;
  for (int i = 0; i < KeyQueue::kCapacity; ++i) EXPECT_TRUE(q.push({static_cast<int8_t>(i % 2 ? 1 : -1), 0}));
  EXPECT_FALSE(q.push({0, 0, BoardKey::Confirm}));
  drained.clear();
  EXPECT_FALSE(q.drain(record));
  EXPECT_EQ(drained.size(), static_cast<size_t>(KeyQueue::kCapacity));
  EXPECT_EQ(drained.front().dx, -1);

  q.push({0, 0, BoardKey::Confirm});
  q.clear();
  EXPECT_FALSE(q.pending());
}
