#include <gtest/gtest.h>

#include <string>

#include "FiveLetters.h"
#include "FiveLettersWords.h"

using namespace fiveletters;

namespace {

// Two-byte UTF-8 Cyrillic only, which is all the tests spell.
bool lettersOf(const char* utf8, uint8_t (&out)[LETTERS]) {
  const auto* s = reinterpret_cast<const unsigned char*>(utf8);
  int n = 0;
  while (*s != 0) {
    if ((s[0] & 0xE0) != 0xC0 || (s[1] & 0xC0) != 0x80 || n == LETTERS) return false;
    const uint8_t l = letterOf(static_cast<uint32_t>((s[0] & 0x1F) << 6 | (s[1] & 0x3F)));
    if (l == NO_LETTER) return false;
    out[n++] = l;
    s += 2;
  }
  return n == LETTERS;
}

uint32_t word(const char* utf8) {
  uint8_t l[LETTERS];
  EXPECT_TRUE(lettersOf(utf8, l)) << utf8;
  return pack(l);
}

std::string marks(const char* guess, const char* answer) {
  uint8_t g[LETTERS];
  uint8_t a[LETTERS];
  EXPECT_TRUE(lettersOf(guess, g));
  EXPECT_TRUE(lettersOf(answer, a));
  Mark m[LETTERS];
  score(g, a, m);
  std::string s;
  for (const Mark x : m) s += x == Mark::Placed ? 'P' : x == Mark::Present ? 'p' : '.';
  return s;
}

void typeWord(Game& g, const char* utf8) {
  uint8_t l[LETTERS];
  ASSERT_TRUE(lettersOf(utf8, l)) << utf8;
  for (const uint8_t x : l) g.type(x);
}

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

}  // namespace

TEST(FiveLetters, LettersFoldYo) {
  EXPECT_EQ(letterOf(0x430), 0);                // а
  EXPECT_EQ(letterOf(0x44F), 31);               // я
  EXPECT_EQ(letterOf(0x410), 0);                // А
  EXPECT_EQ(letterOf(0x451), letterOf(0x435));  // ё = е
  EXPECT_EQ(letterOf(0x401), letterOf(0x415));  // Ё = Е
  EXPECT_EQ(letterOf('a'), NO_LETTER);
  EXPECT_EQ(codepointOf(letterOf(0x44A)), 0x42Au);  // ъ -> Ъ
  EXPECT_EQ(word("актёр"), word("актер"));
}

TEST(FiveLetters, PackRoundTrip) {
  uint8_t l[LETTERS];
  ASSERT_TRUE(lettersOf("щётка", l));
  uint8_t back[LETTERS];
  unpack(pack(l), back);
  for (int i = 0; i < LETTERS; ++i) EXPECT_EQ(back[i], l[i]);
}

TEST(FiveLetters, TablesSortedAndDisjoint) {
  const int answers = answerCount();
  const int extra = static_cast<int>(sizeof(EXTRA_GUESSES) / sizeof(EXTRA_GUESSES[0]));
  EXPECT_GT(answers, 500);
  EXPECT_GT(extra, 1000);
  for (int i = 1; i < answers; ++i) EXPECT_LT(ANSWERS[i - 1], ANSWERS[i]);
  for (int i = 1; i < extra; ++i) EXPECT_LT(EXTRA_GUESSES[i - 1], EXTRA_GUESSES[i]);
  int i = 0;
  int j = 0;
  while (i < answers && j < extra) {
    ASSERT_NE(ANSWERS[i], EXTRA_GUESSES[j]);
    ANSWERS[i] < EXTRA_GUESSES[j] ? ++i : ++j;
  }
  for (int k = 0; k < answers; ++k) EXPECT_TRUE(isWord(answerAt(k)));
  for (int k = 0; k < extra; ++k) EXPECT_TRUE(isWord(EXTRA_GUESSES[k]));
}

TEST(FiveLetters, DictionaryLookup) {
  EXPECT_TRUE(isWord(word("кошка")));
  EXPECT_TRUE(isWord(word("объём")));  // stored folded, found either way
  EXPECT_TRUE(isWord(word("объем")));
  EXPECT_FALSE(isWord(word("абвгд")));
  EXPECT_FALSE(isWord(word("ккккк")));
  EXPECT_FALSE(isWord(0));
}

TEST(FiveLetters, ScoringBasics) {
  EXPECT_EQ(marks("кошка", "кошка"), "PPPPP");
  EXPECT_EQ(marks("лампа", "кошка"), "....P");
  EXPECT_EQ(marks("аксон", "кошка"), "pp.p.");
}

TEST(FiveLetters, ScoringDuplicatesAreCountLimited) {
  // One К left unmatched after the placed one: only the first spare К is marked.
  EXPECT_EQ(marks("кукла", "кошка"), "P.p.P");
  // The answer has one А: a placed А leaves none for the other copies.
  EXPECT_EQ(marks("ааааа", "абвгд"), "P....");
  // Placed copies are matched first, even when they come later.
  EXPECT_EQ(marks("аббба", "бвгда"), ".p..P");
  // Spare copies are marked left to right.
  EXPECT_EQ(marks("ббааа", "ааабв"), "p.Ppp");
}

TEST(FiveLetters, TypingEditsOnlyTheCurrentRow) {
  Game g;
  g.start(word("кошка"));
  EXPECT_FALSE(g.erase());
  typeWord(g, "лампа");
  EXPECT_EQ(g.typed(), 5);
  EXPECT_FALSE(g.type(0));
  EXPECT_TRUE(g.erase());
  EXPECT_EQ(g.typed(), 4);
  EXPECT_EQ(g.letter(0, 4), NO_LETTER);
  EXPECT_EQ(g.submit(), Submit::Incomplete);
  EXPECT_FALSE(g.type(ALPHABET));
  EXPECT_EQ(g.guesses(), 0);
}

TEST(FiveLetters, UnknownWordKeepsTheRow) {
  Game g;
  g.start(word("кошка"));
  typeWord(g, "абвгд");
  EXPECT_EQ(g.submit(), Submit::Unknown);
  EXPECT_EQ(g.guesses(), 0);
  EXPECT_EQ(g.typed(), 5);
  EXPECT_FALSE(g.inProgress());
}

TEST(FiveLetters, WinOnTheThirdTry) {
  Game g;
  g.start(word("кошка"));
  typeWord(g, "лампа");
  EXPECT_EQ(g.submit(), Submit::Scored);
  EXPECT_TRUE(g.inProgress());
  typeWord(g, "кукла");
  EXPECT_EQ(g.submit(), Submit::Scored);
  EXPECT_EQ(g.mark(1, 2), Mark::Present);
  typeWord(g, "кошка");
  EXPECT_EQ(g.submit(), Submit::Scored);
  EXPECT_EQ(g.status(), Status::Won);
  EXPECT_EQ(g.guesses(), 3);
  EXPECT_FALSE(g.inProgress());
  EXPECT_FALSE(g.type(0));
  EXPECT_EQ(g.submit(), Submit::Over);
}

TEST(FiveLetters, LoseAfterSixGuesses) {
  Game g;
  g.start(word("кошка"));
  for (int i = 0; i < ATTEMPTS; ++i) {
    EXPECT_FALSE(g.isOver());
    typeWord(g, "лампа");
    EXPECT_EQ(g.submit(), Submit::Scored);
  }
  EXPECT_EQ(g.status(), Status::Lost);
  char text[16];
  ASSERT_EQ(g.answerText(text, sizeof(text)), 10u);
  EXPECT_STREQ(text, "КОШКА");
}

TEST(FiveLetters, KeyMarksKeepTheBest) {
  Game g;
  g.start(word("кошка"));
  typeWord(g, "кукла");
  g.submit();
  uint8_t l[LETTERS];
  ASSERT_TRUE(lettersOf("кулаб", l));
  EXPECT_EQ(g.keyMark(l[0]), Mark::Placed);  // К placed once, present once
  EXPECT_EQ(g.keyMark(l[1]), Mark::Absent);
  EXPECT_EQ(g.keyMark(l[2]), Mark::Absent);
  EXPECT_EQ(g.keyMark(l[3]), Mark::Placed);
  EXPECT_EQ(g.keyMark(l[4]), Mark::None);
  typeWord(g, "шапка");
  g.submit();
  ASSERT_TRUE(lettersOf("шапка", l));
  EXPECT_EQ(g.keyMark(l[0]), Mark::Present);
}

TEST(FiveLetters, NewGameIsSeededAndPicksAnAnswer) {
  Game a;
  Game b;
  a.newGame(42);
  b.newGame(42);
  EXPECT_EQ(a.answer(), b.answer());
  bool known = false;
  for (int i = 0; i < answerCount(); ++i) known |= answerAt(i) == a.answer();
  EXPECT_TRUE(known);
  int differ = 0;
  for (uint32_t seed = 1; seed < 20; ++seed) {
    b.newGame(seed);
    differ += b.answer() != a.answer();
  }
  EXPECT_GT(differ, 15);
}

TEST(FiveLetters, SaveRoundTrip) {
  Game g;
  g.start(word("кошка"));
  typeWord(g, "лампа");
  g.submit();
  typeWord(g, "кукла");
  g.submit();
  g.type(3);
  g.type(4);
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());
  Game back;
  ASSERT_TRUE(back.deserialize(text.data(), text.size()));
  EXPECT_EQ(back.answer(), g.answer());
  EXPECT_EQ(back.guesses(), 2);
  EXPECT_EQ(back.typed(), 2);
  for (int r = 0; r < ATTEMPTS; ++r) {
    for (int c = 0; c < LETTERS; ++c) {
      EXPECT_EQ(back.letter(r, c), g.letter(r, c));
      EXPECT_EQ(back.mark(r, c), g.mark(r, c));
    }
  }
  EXPECT_EQ(save(back), text);
}

TEST(FiveLetters, FullUnsubmittedRowStaysTyped) {
  Game g;
  g.start(word("кошка"));
  typeWord(g, "лампа");
  g.submit();
  typeWord(g, "абвгд");
  const std::string text = save(g);
  Game back;
  ASSERT_TRUE(back.deserialize(text.data(), text.size()));
  EXPECT_EQ(back.guesses(), 1);
  EXPECT_EQ(back.typed(), 5);
}

TEST(FiveLetters, BadSavesAreRejected) {
  Game g;
  g.start(word("кошка"));
  const uint32_t before = g.answer();
  const char* bad[] = {
      "",
      R"({"v":2,"answer":1,"rows":0,"board":""})",
      R"({"v":1,"answer":33554432,"rows":0,"board":""})",
      R"({"v":1,"answer":1,"rows":7,"board":""})",
      R"({"v":1,"answer":1,"rows":1,"board":"abc"})",
      R"({"v":1,"answer":1,"rows":0,"board":"abcdef"})",
      R"({"v":1,"answer":1,"rows":0,"board":"ab!"})",
      R"({"v":1,"answer":1,"rows":2,"board":"0000100001"})",  // won on the first row, then more
  };
  for (const char* text : bad) {
    EXPECT_FALSE(g.deserialize(text, strlen(text))) << text;
    EXPECT_EQ(g.answer(), before);
  }
}

TEST(FiveLetters, StatsCountStreaks) {
  Stats s;
  s.record(true);
  s.record(true);
  s.record(false);
  s.record(true);
  EXPECT_EQ(s.played, 4u);
  EXPECT_EQ(s.wins, 3u);
  EXPECT_EQ(s.streak, 1u);
  EXPECT_EQ(s.bestStreak, 2u);

  char text[STATS_MAX];
  const size_t len = s.serialize(text, sizeof(text));
  ASSERT_GT(len, 0u);
  Stats back;
  ASSERT_TRUE(back.deserialize(text, len));
  EXPECT_EQ(back.played, 4u);
  EXPECT_EQ(back.wins, 3u);
  EXPECT_EQ(back.streak, 1u);
  EXPECT_EQ(back.bestStreak, 2u);

  const char* bad = R"({"v":1,"played":1,"wins":2,"streak":0,"best":0})";
  EXPECT_FALSE(back.deserialize(bad, strlen(bad)));
  EXPECT_EQ(back.played, 0u);
}
