#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "Hangman.h"
#include "HangmanWords.h"

using namespace hangman;

namespace {

// кошка, самолёт, яблоко, дом
constexpr char kTestWords[] =
    "kowka:domawnee jivotnoe\n"
    "samolEt:letatelxnYy apparat\n"
    "Qbloko:plod\n"
    "dom:jiliWe\n";
const WordList kTestList(kTestWords, 4);
// The first three words, then all four.
const WordList kTestLists[] = {{kTestWords, 3}, {kTestWords, 4}};

uint8_t L(const char c) { return static_cast<uint8_t>(strchr(ALPHABET, c) - ALPHABET + 1); }

std::string save(const Game& g) {
  std::string s(SAVE_MAX, '\0');
  s.resize(g.serialize(s.data(), s.size()));
  return s;
}

// Russian in ALPHABET, the form the word list stores.
std::string translit(const char* utf8) {
  static const char32_t kRussian[] = U"абвгдеёжзийклмнопрстуфхцчшщъыьэюя";
  std::string out;
  for (const auto* s = reinterpret_cast<const unsigned char*>(utf8); *s != 0; s += 2) {
    const char32_t cp = static_cast<char32_t>((s[0] & 0x1F) << 6 | (s[1] & 0x3F));
    for (int i = 0; i < LETTER_COUNT; ++i) {
      if (kRussian[i] == cp) out += ALPHABET[i];
    }
  }
  return out;
}

std::string shownWord(const Game& g) {
  std::string s;
  for (int i = 0; i < g.length(); ++i) s += g.shown(i) ? ALPHABET[g.letter(i) - 1] : '_';
  return s;
}

}  // namespace

TEST(HangmanTest, LettersMapToUpperCaseCyrillic) {
  EXPECT_EQ(letterCodepoint(1), 0x410u);
  EXPECT_EQ(letterCodepoint(LETTER_E), 0x415u);
  EXPECT_EQ(letterCodepoint(LETTER_YO), 0x401u);
  EXPECT_EQ(letterCodepoint(8), 0x416u);
  EXPECT_EQ(letterCodepoint(LETTER_COUNT), 0x42Fu);
  EXPECT_EQ(letterCodepoint(0), 0u);
  EXPECT_EQ(letterCodepoint(LETTER_COUNT + 1), 0u);
}

TEST(HangmanTest, WordListDecodesWordsAndClues) {
  uint8_t letters[MAX_LENGTH];
  ASSERT_EQ(kTestList.word(1, letters, MAX_LENGTH), 7);
  EXPECT_EQ(letters[0], L('s'));
  EXPECT_EQ(letters[5], LETTER_YO);
  char clue[MAX_CLUE_BYTES];
  kTestList.clue(0, clue, sizeof(clue));
  EXPECT_STREQ(clue, "Домашнее животное");
  kTestList.clue(1, clue, sizeof(clue));
  EXPECT_STREQ(clue, "Летательный аппарат");
  EXPECT_EQ(kTestList.word(4, letters, MAX_LENGTH), 0);
  EXPECT_EQ(kTestList.word(1, letters, 3), 0);
}

TEST(HangmanTest, BuiltInListDecodes) {
  const WordList list(WORDS, WORD_COUNT + RUDE_WORD_COUNT);
  EXPECT_LE(sizeof(WORDS), 40u * 1024u);
  EXPECT_GE(WORD_COUNT, 900);
  EXPECT_GE(RUDE_WORD_COUNT, 20);
  std::set<std::string> seen;
  for (uint16_t i = 0; i < list.size(); ++i) {
    uint8_t letters[MAX_LENGTH];
    const int n = list.word(i, letters, MAX_LENGTH);
    ASSERT_GE(n, 5) << i;
    ASSERT_LE(n, 10) << i;
    char clue[MAX_CLUE_BYTES];
    const size_t clueLen = list.clue(i, clue, sizeof(clue));
    ASSERT_LT(clueLen + 2, sizeof(clue)) << i;  // not cut
    // Two words at least: «Вещество» alone fits too many answers.
    EXPECT_NE(strchr(clue, ' '), nullptr) << clue;
    EXPECT_TRUE(seen.insert(std::string(reinterpret_cast<const char*>(letters), n)).second) << i;
  }
  uint8_t letters[MAX_LENGTH];
  EXPECT_EQ(list.word(list.size(), letters, MAX_LENGTH), 0);
}

TEST(HangmanTest, RudeWordsOnlyInTheirModeAndExcludedNowhere) {
  const WordList list(WORDS, WORD_COUNT + RUDE_WORD_COUNT);
  std::set<std::string> normal;
  std::set<std::string> rude;
  for (uint16_t i = 0; i < list.size(); ++i) {
    uint8_t letters[MAX_LENGTH];
    const int n = list.word(i, letters, MAX_LENGTH);
    std::string word;
    for (int k = 0; k < n; ++k) word += ALPHABET[letters[k] - 1];
    (i < WORD_COUNT ? normal : rude).insert(word);
  }
  for (const char* w : {"шлюха", "ублюдок", "пенис", "девка", "тварь", "скотина", "дурак", "морда", "киска", "аборт"}) {
    EXPECT_EQ(normal.count(translit(w)), 0u) << w;
    EXPECT_EQ(rude.count(translit(w)), 1u) << w;
  }
  for (const char* w : {"китаец", "японец", "мерседес", "линкольн", "москвич", "твиттер", "наполеон", "ньютон",
                        "максим", "христос", "майдан", "хонда", "волга", "боинг", "генри", "стоун", "марго", "целое"}) {
    EXPECT_EQ(normal.count(translit(w)) + rude.count(translit(w)), 0u) << w;
  }
}

TEST(HangmanTest, HitOpensEveryPlaceAndMissCosts) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 0));
  EXPECT_FALSE(g.inProgress());
  EXPECT_EQ(g.guess(L('k')), Guess::Hit);
  EXPECT_EQ(shownWord(g), "k__k_");
  EXPECT_TRUE(g.inProgress());
  EXPECT_EQ(g.guess(L('e')), Guess::Miss);
  EXPECT_EQ(g.mistakes(), 1);
  EXPECT_EQ(g.key(L('k')), Key::Right);
  EXPECT_EQ(g.key(L('e')), Key::Wrong);
  EXPECT_EQ(g.key(L('a')), Key::Untried);
}

TEST(HangmanTest, RepeatsCostNothing) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 0));
  g.guess(L('e'));
  g.guess(L('k'));
  EXPECT_EQ(g.guess(L('e')), Guess::Repeat);
  EXPECT_EQ(g.guess(L('k')), Guess::Repeat);
  EXPECT_EQ(g.mistakes(), 1);
  EXPECT_EQ(g.guess(0), Guess::Ignored);
  EXPECT_EQ(g.guess(LETTER_COUNT + 1), Guess::Ignored);
}

TEST(HangmanTest, YoIsALetterOfItsOwn) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 1));  // самолёт
  EXPECT_EQ(g.guess(LETTER_E), Guess::Miss);
  EXPECT_FALSE(g.shown(5));
  EXPECT_EQ(g.guess(LETTER_YO), Guess::Hit);
  EXPECT_TRUE(g.shown(5));
  EXPECT_EQ(g.key(LETTER_E), Key::Wrong);
  EXPECT_EQ(g.key(LETTER_YO), Key::Right);
}

TEST(HangmanTest, AllLettersWin) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 0));
  for (const char c : {'k', 'o', 'w'}) EXPECT_EQ(g.guess(L(c)), Guess::Hit);
  EXPECT_EQ(g.status(), Status::Playing);
  g.guess(L('a'));
  EXPECT_EQ(g.status(), Status::Won);
  EXPECT_TRUE(g.isOver());
  EXPECT_FALSE(g.inProgress());
  EXPECT_EQ(g.guess(L('b')), Guess::Ignored);
}

TEST(HangmanTest, SixMissesLose) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 0));
  const char misses[] = "bvgdez";
  for (int i = 0; i < 5; ++i) g.guess(L(misses[i]));
  EXPECT_EQ(g.status(), Status::Playing);
  EXPECT_EQ(g.guess(L(misses[5])), Guess::Miss);
  EXPECT_EQ(g.status(), Status::Lost);
  EXPECT_EQ(g.mistakes(), MAX_MISTAKES);
  EXPECT_EQ(g.guess(L('k')), Guess::Ignored);
}

TEST(HangmanTest, HintOpensTheFirstHiddenLetterForAMistake) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 2));  // яблоко
  g.guess(L('Q'));
  ASSERT_TRUE(g.hint());
  EXPECT_EQ(shownWord(g), "Qb____");
  EXPECT_EQ(g.mistakes(), 1);
  EXPECT_EQ(g.key(L('b')), Key::Right);
  ASSERT_TRUE(g.hint());
  EXPECT_EQ(shownWord(g), "Qbl___");
}

TEST(HangmanTest, HintNeverTakesTheLastMistake) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 0));
  for (const char c : {'b', 'v', 'g', 'd'}) g.guess(L(c));
  EXPECT_TRUE(g.canHint());
  ASSERT_TRUE(g.hint());
  EXPECT_EQ(g.mistakes(), 5);
  EXPECT_FALSE(g.canHint());
  EXPECT_FALSE(g.hint());
  EXPECT_EQ(g.status(), Status::Playing);
}

TEST(HangmanTest, HintOnTheLastLetterWins) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 3));  // дом
  g.guess(L('d'));
  g.guess(L('o'));
  ASSERT_TRUE(g.hint());
  EXPECT_EQ(g.status(), Status::Won);
}

TEST(HangmanTest, NewGamesSkipRecentWords) {
  std::string data;
  for (int i = 0; i < 40; ++i) {
    data += "slovo";
    data += ALPHABET[i % 33];
    data += ALPHABET[i / 33];
    data += ":opisanie\n";
  }
  const WordList list(data.c_str(), 40);
  Game g(&list, 1);
  std::set<int> picked;
  for (uint32_t seed = 1; seed <= RECENT_COUNT; ++seed) {
    g.newGame(0, seed * 2654435761u);
    picked.insert(g.wordIndex());
  }
  EXPECT_EQ(static_cast<int>(picked.size()), RECENT_COUNT);
}

TEST(HangmanTest, SmallListsStillDeal) {
  Game g(kTestLists, 2);
  for (uint32_t seed = 1; seed < 50; ++seed) {
    g.newGame(1, seed);
    EXPECT_LT(g.wordIndex(), 4);
    EXPECT_GT(g.length(), 0);
  }
}

TEST(HangmanTest, SaveRoundTrips) {
  Game g(kTestLists, 2);
  g.newGame(1, 7);
  g.newGame(1, 8);
  ASSERT_TRUE(g.start(1, 1));
  g.guess(L('s'));
  g.guess(L('b'));
  g.guess(LETTER_E);
  ASSERT_TRUE(g.hint());
  const std::string text = save(g);
  ASSERT_FALSE(text.empty());

  Game r(kTestLists, 2);
  ASSERT_TRUE(r.deserialize(text.data(), text.size()));
  EXPECT_EQ(r.wordIndex(), 1);
  EXPECT_EQ(shownWord(r), shownWord(g));
  EXPECT_EQ(r.mistakes(), 3);
  EXPECT_EQ(r.key(L('b')), Key::Wrong);
  EXPECT_EQ(r.key(L('a')), Key::Right);
  EXPECT_EQ(save(r), text);
}

TEST(HangmanTest, SaveOfAnotherListIsRejected) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 1));
  g.guess(L('s'));
  const std::string text = save(g);
  constexpr char kOther[] = "kowka:domawnee jivotnoe\nmolokoo:napitok\n";
  const WordList other(kOther, 2);
  Game r(&other, 1);
  EXPECT_FALSE(r.deserialize(text.data(), text.size()));
  EXPECT_EQ(r.wordIndex(), 0);
}

TEST(HangmanTest, BadSavesAreRejected) {
  Game g(kTestLists, 2);
  for (const char* bad : {
           "",
           "{\"v\":2,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"w\":0,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"list\":2,\"w\":0,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"list\":0,\"w\":3,\"word\":\"dom\",\"tried\":\"d\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"list\":0,\"w\":9,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"kk\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"k1\",\"hints\":0,\"recent\":\"\"}",
           "{\"v\":1,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":6,\"recent\":\"\"}",
           "{\"v\":1,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":0,\"recent\":\"001\"}",
           "{\"v\":1,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"k\",\"hints\":0,\"recent\":\"0009\"}",
       }) {
    EXPECT_FALSE(g.deserialize(bad, strlen(bad))) << bad;
  }
  const char* good =
      "{\"v\":1,\"list\":0,\"w\":0,\"word\":\"kowka\",\"tried\":\"ke\",\"hints\":1,\"recent\":\"00000003\"}";
  ASSERT_TRUE(g.deserialize(good, strlen(good)));
  EXPECT_EQ(g.mistakes(), 2);
  EXPECT_EQ(shownWord(g), "k__k_");
}

TEST(HangmanTest, ANewGameStaysInItsList) {
  Game g(kTestLists, 2);
  for (uint32_t seed = 1; seed < 50; ++seed) {
    g.newGame(0, seed);
    EXPECT_EQ(g.list(), 0);
    EXPECT_LT(g.wordIndex(), 3);
  }
  bool last = false;
  for (uint32_t seed = 1; seed < 50; ++seed) {
    g.newGame(1, seed);
    last = last || g.wordIndex() == 3;
  }
  EXPECT_TRUE(last);
}

TEST(HangmanTest, SaveCarriesItsList) {
  Game g(kTestLists, 2);
  ASSERT_TRUE(g.start(1, 3));  // дом, only in the second list
  g.guess(L('d'));
  const std::string text = save(g);
  Game r(kTestLists, 2);
  ASSERT_TRUE(r.deserialize(text.data(), text.size()));
  EXPECT_EQ(r.list(), 1);
  EXPECT_EQ(r.wordIndex(), 3);
  Game one(kTestLists, 1);
  EXPECT_FALSE(one.deserialize(text.data(), text.size()));
}
