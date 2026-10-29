#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "CardDeck.h"
#include "CardLayout.h"

using namespace cards;

namespace {

class StringSource final : public ByteSource {
 public:
  explicit StringSource(std::string data, size_t chunk = 7) : data(std::move(data)), chunk(chunk) {}
  int read(uint8_t* buf, size_t cap) override {
    const size_t n = std::min({cap, chunk, data.size() - at});
    memcpy(buf, data.data() + at, n);
    at += n;
    return static_cast<int>(n);
  }

 private:
  std::string data;
  size_t chunk;
  size_t at = 0;
};

struct Card {
  std::string front, back, extra;
  uint8_t media[3];
  uint32_t offset;
  uint32_t guid;
};

std::vector<Card> parse(const std::string& file, const bool csv = false) {
  const Format f = probe(reinterpret_cast<const uint8_t*>(file.data()), std::min(file.size(), PROBE_BYTES), csv);
  StringSource source(file.substr(f.dataStart));
  RecordReader reader(source, f, f.dataStart);
  auto fields = std::make_unique<Fields>();
  std::vector<Card> cards;
  uint32_t offset = 0;
  uint32_t guid = 0;
  while (reader.next(*fields, offset, guid)) {
    cards.push_back({fields->text[0],
                     fields->text[1],
                     fields->text[2],
                     {fields->media[0], fields->media[1], fields->media[2]},
                     offset,
                     guid});
  }
  EXPECT_FALSE(reader.failed());
  return cards;
}

}  // namespace

TEST(CardDeck, Names) {
  EXPECT_TRUE(isDeckName("Español.txt"));
  EXPECT_TRUE(isDeckName("a.TSV"));
  EXPECT_TRUE(isDeckName("a.csv"));
  EXPECT_FALSE(isDeckName("a.epub"));
  EXPECT_TRUE(isTableName("a.csv"));
  EXPECT_FALSE(isTableName("a.txt"));
  const std::string anki = "\xEF\xBB\xBF#separator:tab\n";
  EXPECT_TRUE(hasAnkiHeader(reinterpret_cast<const uint8_t*>(anki.data()), anki.size()));
  EXPECT_TRUE(hasAnkiHeader(reinterpret_cast<const uint8_t*>("#html:true\n"), 11));
  EXPECT_FALSE(hasAnkiHeader(reinterpret_cast<const uint8_t*>("Chapter 1\n"), 10));
}

TEST(CardDeck, AnkiExportWithHeaders) {
  const std::string file =
      "#separator:tab\n#html:true\n#guid column:1\n#notetype column:2\n#deck column:3\n#tags column:7\n"
      "Ab1\tBasic\tEspañol\tel perro\tсобака\tEl perro ladra.\tanimals\n"
      "Cd2\tBasic\tEspañol\tla casa\tдом\t\t\n";
  const Format f = probe(reinterpret_cast<const uint8_t*>(file.data()), file.size(), false);
  EXPECT_EQ(f.separator, '\t');
  EXPECT_EQ(f.guidColumn, 0);
  EXPECT_EQ(f.hiddenColumns, (1u << 0) | (1u << 1) | (1u << 2) | (1u << 6));
  const auto cards = parse(file);
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "el perro");
  EXPECT_EQ(cards[0].back, "собака");
  EXPECT_EQ(cards[0].extra, "El perro ladra.");
  EXPECT_EQ(cards[0].guid, fnv1a("Ab1", 3));
  EXPECT_EQ(cards[1].front, "la casa");
  EXPECT_EQ(cards[1].extra, "");
  EXPECT_EQ(file.substr(cards[1].offset, 3), "Cd2");
}

TEST(CardDeck, CsvQuotingAndSeparatorGuess) {
  const std::string file = "\"a; b\";\"line1\nline2\";\"he said \"\"hi\"\"\"\r\nplain;two\r\n\r\n";
  const auto cards = parse(file, true);
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "a; b");
  EXPECT_EQ(cards[0].back, "line1\nline2");
  EXPECT_EQ(cards[0].extra, "he said \"hi\"");
  EXPECT_EQ(cards[1].front, "plain");
  EXPECT_EQ(cards[1].back, "two");

  auto sep = [](const char* text) {
    return probe(reinterpret_cast<const uint8_t*>(text), strlen(text), true).separator;
  };
  EXPECT_EQ(sep("a\tb;c,d\n"), '\t');
  EXPECT_EQ(sep("a;b,c\n"), ';');
  EXPECT_EQ(sep("a,b\n"), ',');
  EXPECT_EQ(sep("\"x;y\",b\n"), ',');
  EXPECT_EQ(probe(reinterpret_cast<const uint8_t*>("a;b\n"), 4, false).separator, '\t');
  EXPECT_EQ(probe(reinterpret_cast<const uint8_t*>("#separator:semicolon\na;b\n"), 25, false).separator, ';');
}

TEST(CardDeck, Cp1251Fallback) {
  // "Привет;мир" and "Ёж;ёлка" in Windows-1251.
  const std::string file = "\xCF\xF0\xE8\xE2\xE5\xF2;\xEC\xE8\xF0\n\xA8\xE6;\xB8\xEB\xEA\xE0\n";
  const Format f = probe(reinterpret_cast<const uint8_t*>(file.data()), file.size(), true);
  EXPECT_TRUE(f.cp1251);
  const auto cards = parse(file, true);
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "Привет");
  EXPECT_EQ(cards[0].back, "мир");
  EXPECT_EQ(cards[1].front, "Ёж");
  EXPECT_EQ(cards[1].back, "ёлка");
  const std::string utf8 = "Привет;мир\n";
  EXPECT_FALSE(probe(reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size(), true).cp1251);
  // One stray byte in a UTF-8 file is not CP1251.
  const std::string stray = "Привет;мир\xFF\n";
  EXPECT_FALSE(probe(reinterpret_cast<const uint8_t*>(stray.data()), stray.size(), true).cp1251);
  // CP1251 text with pairs that happen to be valid UTF-8 (Е+ё, В+С+Ё).
  const std::string russian = "\xC5\xB8 \xE7\xEE\xE2\xF3\xF2;\xC2\xD1\xA8 \xEE\xEA\n";
  EXPECT_TRUE(probe(reinterpret_cast<const uint8_t*>(russian.data()), russian.size(), true).cp1251);
  const auto ee = parse(russian, true);
  ASSERT_EQ(ee.size(), 1u);
  EXPECT_EQ(ee[0].front, "Её зовут");
  EXPECT_EQ(ee[0].back, "ВСЁ ок");
  const std::string utf16(
      "\xFF\xFE"
      "a\0;\0",
      6);
  EXPECT_TRUE(probe(reinterpret_cast<const uint8_t*>(utf16.data()), utf16.size(), true).utf16);
  // A sequence cut by the end of a full probe is not a reason to switch.
  std::string cut(PROBE_BYTES - 1, 'a');
  cut += "\xD0";
  EXPECT_FALSE(probe(reinterpret_cast<const uint8_t*>(cut.data()), cut.size(), true).cp1251);
}

TEST(CardDeck, HtmlIsStripped) {
  const std::string file =
      "<b>bold</b>  text<br>next&nbsp;line\t<div>one</div><div>two</div>[sound:x.mp3]\t<img src=\"a.png\">\n"
      "<img src=\"cat.jpg\">\tкошка [sound:cat.mp3]\n";
  const auto cards = parse(file);
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].front, "bold text\nnext line");
  EXPECT_EQ(cards[0].back, "one\ntwo");
  EXPECT_EQ(cards[0].media[1], HasSound);
  EXPECT_EQ(cards[0].extra, "");
  EXPECT_EQ(cards[0].media[2], HasImage);
  EXPECT_EQ(cards[1].front, "");
  EXPECT_EQ(cards[1].media[0], HasImage);
  EXPECT_EQ(cards[1].back, "кошка");
  EXPECT_TRUE(hasFront(Fields{{"", "", ""}, {HasImage, 0, 0}}));
}

TEST(CardDeck, EntitiesAndLiteralAngles) {
  const auto cards = parse("#html:true\n&amp; &lt;tag&gt; &#1078;&#x436; &MDASH; &bogus; a < b<!-- <br>note -->\tx\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front, "& <tag> жж — &bogus; a < b");
  const auto plain = parse("#html:False\n<b>&amp;</b>\tx\n");
  ASSERT_EQ(plain.size(), 1u);
  EXPECT_EQ(plain[0].front, "<b>&amp;</b>");
  // Without a header, markup is read only when the file shows some.
  const auto code = parse("vector<int>\t&amp; <b>\n", true);
  ASSERT_EQ(code.size(), 1u);
  EXPECT_EQ(code[0].front, "vector<int>");
  EXPECT_EQ(code[0].back, "&amp; <b>");
  const auto sniffed = parse("a<br>b\t&nbsp;c\n", true);
  ASSERT_EQ(sniffed.size(), 1u);
  EXPECT_EQ(sniffed[0].front, "a\nb");
}

TEST(CardDeck, Cloze) {
  const auto cards = parse("#separator:tab\n#html:true\n{{c1::Москва}} — столица {{c2::России::страна}}\tгород\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front, "[…] — столица […]");
  EXPECT_EQ(cards[0].back, "Москва — столица России");
  EXPECT_EQ(cards[0].extra, "город");
  const auto plain = parse("#html:true\n{{count}} words\tx\n");
  ASSERT_EQ(plain.size(), 1u);
  EXPECT_EQ(plain[0].front, "{{count}} words");
  EXPECT_EQ(plain[0].back, "x");
}

TEST(CardDeck, OpenQuoteDoesNotSwallowTheFile) {
  const auto cards = parse("\"" + std::string(FIELD_MAX + 10, 'a') + "\nnext\tcard\n");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[1].front, "next");
  EXPECT_EQ(cards[1].back, "card");
}

TEST(CardDeck, FieldLimit) {
  const std::string longField(3000, 'a');
  const auto cards = parse(longField + "\t" + std::string(1500, 'b') + "ж\n");
  ASSERT_EQ(cards.size(), 1u);
  EXPECT_EQ(cards[0].front.size(), FIELD_MAX + 3);
  EXPECT_EQ(cards[0].front.substr(FIELD_MAX), "\xE2\x80\xA6");
  EXPECT_LE(cards[0].back.size(), FIELD_MAX + 3);
}

TEST(CardDeck, BomCrlfBlankLinesAndMissingFront) {
  const auto cards = parse(
      "\xEF\xBB\xBF"
      "one\tодин\r\n\r\n\t\tonly extra\r\n\ttwo\r\nthree\n");
  ASSERT_EQ(cards.size(), 4u);
  EXPECT_EQ(cards[0].front, "one");
  EXPECT_EQ(cards[0].offset, 3u);
  EXPECT_FALSE(hasFront(Fields{{"", "x", ""}, {0, 0, 0}}));
  EXPECT_EQ(cards[1].front, "");
  EXPECT_EQ(cards[3].front, "three");
  EXPECT_EQ(cards[3].back, "");
}

TEST(CardDeck, KeysPreferTheGuid) {
  Fields f{{"front", "back", ""}, {0, 0, 0}};
  EXPECT_EQ(cardKey(f, 0), fnv1a("front", 5));
  EXPECT_EQ(cardKey(f, 7), 7u);
  Fields picture{{"", "a", ""}, {HasImage, 0, 0}};
  Fields picture2{{"", "b", ""}, {HasImage, 0, 0}};
  EXPECT_NE(cardKey(picture, 0), cardKey(picture2, 0));
  Fields bank2{{"front", "other", ""}, {0, 0, 0}};
  EXPECT_NE(cardAltKey(f), cardAltKey(bank2));
  // An empty guid falls back to the front.
  const auto cards = parse("#separator:tab\n#guid column:1\n\tfront\tback\nG\tfront2\tback\n");
  ASSERT_EQ(cards.size(), 2u);
  EXPECT_EQ(cards[0].guid, 0u);
  EXPECT_EQ(cards[1].guid, fnv1a("G", 1));
}

namespace {
int monoWidth(void*, const char* text, size_t len) {
  int chars = 0;
  for (size_t i = 0; i < len; i++) chars += (static_cast<uint8_t>(text[i]) & 0xC0) != 0x80;
  return chars * 10;
}

std::vector<std::string> wrapped(const char* text, int width, int maxLines = MAX_LINES) {
  Lines lines;
  wrapText(text, width, maxLines, &monoWidth, nullptr, lines);
  std::vector<std::string> out;
  for (int i = 0; i < lines.count; i++) out.emplace_back(text + lines.line[i].start, lines.line[i].len);
  if (lines.overflow) out.emplace_back("+");
  return out;
}
}  // namespace

TEST(CardLayout, Wraps) {
  EXPECT_EQ(wrapped("Без труда не выловишь", 100), (std::vector<std::string>{"Без труда", "не", "выловишь"}));
  EXPECT_EQ(wrapped("one\ntwo three", 100), (std::vector<std::string>{"one", "two three"}));
  EXPECT_EQ(wrapped("well-known/path", 60), (std::vector<std::string>{"well-", "known/", "path"}));
  EXPECT_EQ(wrapped("abcdefghijkl", 50), (std::vector<std::string>{"abcde", "fghij", "kl"}));
  EXPECT_EQ(wrapped("a b c d", 10, 2), (std::vector<std::string>{"a", "b", "+"}));
}
