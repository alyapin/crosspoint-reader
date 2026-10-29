#include <gtest/gtest.h>

#include <string>

#include "TicTacToe.h"

using namespace tictactoe;

namespace {

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

void playAll(Game& g, std::initializer_list<int> cells) {
  for (const int c : cells) ASSERT_TRUE(g.play(c)) << c;
}

// Every line of play of the player against the computer; counts the player's wins and losses.
void explore(const Game& g, int& wins, int& losses) {
  for (int c = 0; c < CELLS; ++c) {
    Game next = g;
    if (!next.play(c)) continue;
    if (next.status() == Status::XWon) {
      ++wins;
    } else if (next.status() == Status::OWon) {
      ++losses;
    } else if (!next.isOver()) {
      explore(next, wins, losses);
    }
  }
}

// The player opens in the centre, then plays perfectly against the computer.
Status playPerfect(Game& g, const uint32_t salt) {
  EXPECT_TRUE(g.play(4));
  while (!g.isOver()) {
    Mark board[CELLS];
    for (int c = 0; c < CELLS; ++c) board[c] = g.at(c);
    EXPECT_TRUE(g.play(bestMove(board, Mark::X, salt + g.moves())));
  }
  return g.status();
}

}  // namespace

TEST(TicTacToe, NewGameIsEmptyAndNotInProgress) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  for (int c = 0; c < CELLS; ++c) EXPECT_EQ(g.at(c), Mark::None);
  EXPECT_EQ(g.toMove(), Mark::X);
  EXPECT_FALSE(g.inProgress());
  EXPECT_FALSE(g.isOver());
  EXPECT_FALSE(g.canUndo());
  EXPECT_EQ(g.winLine(), -1);
}

TEST(TicTacToe, TwoPlayersAlternateAndScoreAWin) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  playAll(g, {0, 3, 1, 4});
  EXPECT_TRUE(g.inProgress());
  EXPECT_EQ(g.at(3), Mark::O);
  EXPECT_FALSE(g.play(3));
  ASSERT_TRUE(g.play(2));
  EXPECT_EQ(g.status(), Status::XWon);
  EXPECT_EQ(g.winLine(), 0);
  EXPECT_FALSE(g.play(5));
  EXPECT_FALSE(g.canUndo());
  EXPECT_EQ(g.score().x, 1);
  EXPECT_EQ(g.score().o, 0);
}

TEST(TicTacToe, FullBoardWithoutLineIsADraw) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  playAll(g, {0, 1, 2, 4, 3, 5, 7, 6, 8});
  EXPECT_EQ(g.status(), Status::Draw);
  EXPECT_EQ(g.winLine(), -1);
  EXPECT_EQ(g.score().draws, 1);
}

TEST(TicTacToe, ScoreCarriesOverPerOpponent) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  playAll(g, {0, 3, 1, 4, 2});
  g.newGame(Mode::TwoPlayers, 2);
  playAll(g, {0, 3, 1, 4, 8, 5});
  EXPECT_EQ(g.score().x, 1);
  EXPECT_EQ(g.score().o, 1);
  g.newGame(Mode::Hard, 3);
  EXPECT_EQ(g.score().x + g.score().o + g.score().draws, 0);
  g.newGame(Mode::TwoPlayers, 4);
  EXPECT_EQ(g.score().x, 1);
}

TEST(TicTacToe, BestMoveWinsFirstThenBlocks) {
  using M = Mark;
  const M winOrBlock[CELLS] = {M::O, M::O, M::None, M::X, M::X, M::None, M::X, M::None, M::None};
  for (uint32_t salt = 0; salt < 20; ++salt) EXPECT_EQ(bestMove(winOrBlock, M::O, salt), 2);
  const M block[CELLS] = {M::X, M::X, M::None, M::None, M::O, M::None, M::None, M::None, M::None};
  for (uint32_t salt = 0; salt < 20; ++salt) EXPECT_EQ(bestMove(block, M::O, salt), 2);
}

TEST(TicTacToe, ComputerAnswersAtOnce) {
  Game g;
  g.newGame(Mode::Hard, 7);
  ASSERT_TRUE(g.play(4));
  EXPECT_EQ(g.moves(), 2);
  EXPECT_EQ(g.toMove(), Mark::X);
  int noughts = 0;
  for (int c = 0; c < CELLS; ++c) noughts += g.at(c) == Mark::O;
  EXPECT_EQ(noughts, 1);
}

TEST(TicTacToe, HardNeverLoses) {
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Game g;
    g.newGame(Mode::Hard, seed);
    int wins = 0;
    int losses = 0;
    explore(g, wins, losses);
    EXPECT_EQ(wins, 0) << seed;
    EXPECT_GT(losses, 0) << seed;
  }
}

TEST(TicTacToe, EasySlipsSometimes) {
  int wins = 0;
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    Game g;
    g.newGame(Mode::Easy, seed);
    const Status result = playPerfect(g, seed);
    EXPECT_NE(result, Status::OWon) << seed;
    wins += result == Status::XWon;
  }
  EXPECT_GT(wins, 20);
  EXPECT_LT(wins, 200);
}

TEST(TicTacToe, UndoTakesBackThePlayerAndTheAnswer) {
  Game g;
  g.newGame(Mode::Hard, 3);
  EXPECT_FALSE(g.undo());
  ASSERT_TRUE(g.play(0));
  ASSERT_TRUE(g.canUndo());
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(g.moves(), 0);
  for (int c = 0; c < CELLS; ++c) EXPECT_EQ(g.at(c), Mark::None);

  g.newGame(Mode::TwoPlayers, 3);
  playAll(g, {0, 4});
  ASSERT_TRUE(g.undo());
  EXPECT_EQ(g.moves(), 1);
  EXPECT_EQ(g.at(4), Mark::None);
  EXPECT_EQ(g.toMove(), Mark::O);
}

TEST(TicTacToe, SaveRoundTrips) {
  Game g;
  g.newGame(Mode::TwoPlayers, 1);
  playAll(g, {0, 3, 1, 4, 2});
  g.newGame(Mode::Easy, 99);
  ASSERT_TRUE(g.play(4));
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());

  Game back;
  ASSERT_TRUE(back.deserialize(text.data(), text.size()));
  EXPECT_EQ(save(back), text);
  EXPECT_EQ(back.mode(), Mode::Easy);
  EXPECT_TRUE(back.inProgress());
  for (int c = 0; c < CELLS; ++c) EXPECT_EQ(back.at(c), g.at(c));
  back.newGame(Mode::TwoPlayers, 5);
  EXPECT_EQ(back.score().x, 1);
}

TEST(TicTacToe, BadSavesAreRejected) {
  Game g;
  g.newGame(Mode::Hard, 1);
  ASSERT_TRUE(g.play(4));
  const std::string good = save(g);
  const std::string bad[] = {
      "",
      "{\"v\":1}",
      "{\"v\":1,\"mode\":3,\"seed\":1,\"moves\":\"\",\"cx\":0,\"co\":0,\"cd\":0,\"px\":0,\"po\":0,\"pd\":0}",
      "{\"v\":1,\"mode\":2,\"seed\":1,\"moves\":\"00\",\"cx\":0,\"co\":0,\"cd\":0,\"px\":0,\"po\":0,\"pd\":0}",
      "{\"v\":1,\"mode\":2,\"seed\":1,\"moves\":\"0314258\",\"cx\":0,\"co\":0,\"cd\":0,\"px\":0,\"po\":0,\"pd\":0}",
      "{\"v\":1,\"mode\":1,\"seed\":1,\"moves\":\"4\",\"cx\":0,\"co\":0,\"cd\":0,\"px\":0,\"po\":0,\"pd\":0}",
      "{\"v\":1,\"mode\":2,\"seed\":1,\"moves\":\"9\",\"cx\":0,\"co\":0,\"cd\":0,\"px\":0,\"po\":0,\"pd\":0}",
  };
  for (const std::string& text : bad) {
    Game target = g;
    EXPECT_FALSE(target.deserialize(text.data(), text.size())) << text;
    EXPECT_EQ(save(target), good);
  }
}
