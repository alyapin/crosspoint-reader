// Adapted from guloff/folio-firmware (MIT)

#include "Fb2ToEpub.h"

#include <Memory.h>
#include <expat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// Single-byte encodings expat lacks, upper half (0x80-0xFF) as code points.
// Russian FB2 files are often windows-1251, older ones KOI8-R.
constexpr uint16_t CP1251_HIGH[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457, 0x0410,
    0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D,
    0x041E, 0x041F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427, 0x0428, 0x0429, 0x042A,
    0x042B, 0x042C, 0x042D, 0x042E, 0x042F, 0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0444,
    0x0445, 0x0446, 0x0447, 0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F};

constexpr uint16_t KOI8R_HIGH[128] = {
    0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524, 0x252C, 0x2534, 0x253C, 0x2580, 0x2584,
    0x2588, 0x258C, 0x2590, 0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248, 0x2264, 0x2265,
    0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7, 0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555,
    0x2556, 0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E, 0x255F, 0x2560, 0x2561, 0x0401,
    0x2562, 0x2563, 0x2564, 0x2565, 0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9, 0x044E,
    0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433, 0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C,
    0x043D, 0x043E, 0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432, 0x044C, 0x044B, 0x0437,
    0x0448, 0x044D, 0x0449, 0x0447, 0x044A, 0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
    0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F, 0x042F, 0x0420, 0x0421, 0x0422,
    0x0423, 0x0416, 0x0412, 0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A};

// windows-1252 differs from ISO-8859-1 only in 0x80-0x9F.
constexpr uint16_t CP1252_C1[32] = {0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
                                    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178};

bool iequals(const char* a, const char* b) {
  for (; *a && *b; a++, b++) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
    if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
    if (x != y) return false;
  }
  return *a == *b;
}

int XMLCALL unknownEncoding(void*, const XML_Char* name, XML_Encoding* info) {
  const uint16_t* high = nullptr;
  bool cp1252 = false;
  if (iequals(name, "windows-1251") || iequals(name, "cp1251")) {
    high = CP1251_HIGH;
  } else if (iequals(name, "koi8-r")) {
    high = KOI8R_HIGH;
  } else if (iequals(name, "windows-1252") || iequals(name, "cp1252")) {
    cp1252 = true;
  } else {
    return XML_STATUS_ERROR;
  }
  for (int i = 0; i < 128; i++) info->map[i] = i;
  for (int i = 128; i < 256; i++) {
    if (high) {
      info->map[i] = high[i - 128];
    } else {
      info->map[i] = (cp1252 && i < 0xA0) ? CP1252_C1[i - 0x80] : i;
    }
  }
  info->data = nullptr;
  info->convert = nullptr;
  info->release = nullptr;
  return XML_STATUS_OK;
}

const char* localName(const char* name) {
  const char* colon = strrchr(name, ':');
  return colon ? colon + 1 : name;
}

// Attribute by local name, so l:href, xlink:href and href all match.
const char* attribute(const XML_Char** atts, const char* name) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(localName(atts[i]), name) == 0) return atts[i + 1];
  }
  return nullptr;
}

// Attribute by its full name: FB2 marks notes with a plain type="note", while
// xlink:type="simple" on the same link must not hide it.
const char* exactAttribute(const XML_Char** atts, const char* name) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

const char* imageExtension(const char* contentType) {
  if (!contentType) return nullptr;
  if (iequals(contentType, "image/jpeg") || iequals(contentType, "image/jpg")) return "jpg";
  if (iequals(contentType, "image/png")) return "png";
  if (iequals(contentType, "image/gif")) return "gif";
  return nullptr;
}

const char* mediaType(const char* extension) {
  if (strcmp(extension, "jpg") == 0) return "image/jpeg";
  if (strcmp(extension, "png") == 0) return "image/png";
  return "image/gif";
}

void appendEscaped(std::string& out, const char* s, size_t len, bool attr = false) {
  for (size_t i = 0; i < len; i++) {
    switch (s[i]) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        if (attr) {
          out += "&quot;";
          break;
        }
        [[fallthrough]];
      default:
        out += s[i];
    }
  }
}

void appendEscaped(std::string& out, const std::string& s, bool attr = false) {
  appendEscaped(out, s.data(), s.size(), attr);
}

bool isXmlSpace(const char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// One UTF-8 character; expat hands over whole ones.
uint32_t decodeUtf8(const char* s, const int n) {
  const auto b = [s](const int i) { return static_cast<uint32_t>(static_cast<uint8_t>(s[i])); };
  if (n == 2) return (b(0) & 0x1F) << 6 | (b(1) & 0x3F);
  if (n == 3) return (b(0) & 0x0F) << 12 | (b(1) & 0x3F) << 6 | (b(2) & 0x3F);
  if (n == 4) return (b(0) & 0x07) << 18 | (b(1) & 0x3F) << 12 | (b(2) & 0x3F) << 6 | (b(3) & 0x3F);
  return b(0);
}

int utf8Length(const char lead) {
  const auto c = static_cast<uint8_t>(lead);
  return c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
}

bool isLatinLetter(const uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isUpperLetter(const uint32_t c) { return (c >= 'A' && c <= 'Z') || (c >= 0x400 && c <= 0x42F); }
bool isLowerLetter(const uint32_t c) { return (c >= 'a' && c <= 'z') || (c >= 0x430 && c <= 0x45F); }
bool isTextSpace(const uint32_t c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == 0xA0; }
// ")", "»" and "”": a note marker may follow a closing quote or parenthesis.
bool isCloser(const uint32_t c) { return c == ')' || c == 0xBB || c == 0x201D; }
// What may follow a bare marker: a space, punctuation or a closing quote, not a letter or another digit.
bool endsMarker(const uint32_t c) {
  return isTextSpace(c) || isCloser(c) || c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' ||
         c == 0x2026;
}

void collapseSpaces(std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool space = false;
  for (const char c : s) {
    if (isXmlSpace(c)) {
      space = !out.empty();
    } else {
      if (space) out += ' ';
      space = false;
      out += c;
    }
  }
  s.swap(out);
}

// Appends while `s` is under `max` bytes. Expat hands over whole characters, so
// cutting before a lead byte keeps the UTF-8 valid.
void appendBounded(std::string& s, const char* data, size_t len, size_t max) {
  if (s.size() >= max) return;
  size_t n = std::min(len, max - s.size());
  while (n > 0 && n < len && (static_cast<uint8_t>(data[n]) & 0xC0) == 0x80) n--;
  s.append(data, n);
}

// Cuts `s` to at most `max` bytes without splitting a UTF-8 sequence.
void truncateUtf8(std::string& s, size_t max) {
  if (s.size() <= max) return;
  size_t n = max;
  while (n > 0 && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) n--;
  s.resize(n);
}

bool idFits(const char* id) { return strlen(id) <= Fb2ToEpub::MAX_ID_BYTES; }

// Calls f(character, its bytes, their count) for each UTF-8 character of s.
template <typename F>
void forEachChar(const std::string& s, F f) {
  for (size_t i = 0; i < s.size();) {
    const int n = std::min(utf8Length(s[i]), static_cast<int>(s.size() - i));
    f(decodeUtf8(s.data() + i, n), s.data() + i, n);
    i += n;
  }
}

// A line short enough for a heading, with a letter, that does not end as a sentence or a clause
// does: "Глава 1" or "Я ДОЛЖНА ЗАБРАТЬ РЕБЕНКА ДОМОЙ?..", not "Он ушёл." or "Итак:".
bool titleLike(const std::string& s) {
  size_t chars = 0;
  bool letter = false;
  uint32_t last = 0, beforeLast = 0;  // closing quotes and parentheses aside
  forEachChar(s, [&](const uint32_t c, const char*, int) {
    chars++;
    letter = letter || isUpperLetter(c) || isLowerLetter(c);
    if (!isCloser(c)) {
      beforeLast = last;
      last = c;
    }
  });
  if (!letter || chars > Fb2ToEpub::MAX_BOLD_TITLE_CHARS || last == ',' || last == ';' || last == ':') return false;
  return last != '.' || beforeLast == '.' || beforeLast == '?' || beforeLast == '!';
}

bool allCapitals(const std::string& s) {
  bool upper = false, lower = false;
  forEachChar(s, [&](const uint32_t c, const char*, int) {
    upper = upper || isUpperLetter(c);
    lower = lower || isLowerLetter(c);
  });
  return upper && !lower;
}

uint32_t toLowerLetter(const uint32_t c) {
  if ((c >= 'A' && c <= 'Z') || (c >= 0x410 && c <= 0x42F)) return c + 0x20;
  if (c >= 0x400 && c <= 0x40F) return c + 0x50;  // Ё and the like
  return c;
}

// Lower case with ё as е, for comparing words whatever their case.
uint32_t foldLetter(const uint32_t c) {
  const uint32_t lower = toLowerLetter(c);
  return lower == 0x451 ? 0x435 : lower;
}

uint32_t toUpperLetter(const uint32_t c) {
  if ((c >= 'a' && c <= 'z') || (c >= 0x430 && c <= 0x44F)) return c - 0x20;
  if (c >= 0x450 && c <= 0x45F) return c - 0x50;
  return c;
}

bool isLetterOrDigit(const uint32_t c) { return isUpperLetter(c) || isLowerLetter(c) || (c >= '0' && c <= '9'); }

// The letters and digits of s, as they are: "Глава 14. Маленькие" gives "Глава14Маленькие".
std::string lettersOf(const std::string& s) {
  std::string out;
  forEachChar(s, [&](const uint32_t c, const char* bytes, const int n) {
    if (isLetterOrDigit(c)) out.append(bytes, n);
  });
  return out;
}

// Appends a character below U+0800, as all the letters a case change gives are.
void appendShortUtf8(std::string& s, const uint32_t c) {
  if (c < 0x80) {
    s += static_cast<char>(c);
  } else {
    s += static_cast<char>(0xC0 | (c >> 6));
    s += static_cast<char>(0x80 | (c & 0x3F));
  }
}

// Two lines of a heading as one TOC label: "Глава 1" and "ПЕРВЫЕ ТРИ ДНЯ" make "Глава 1. Первые
// три дня"; lines not all in capitals stay as they are, as in "Часть 1 · Начало". Returns where
// the second line starts in the label.
size_t joinTitleLines(std::string& label, const std::string& second) {
  if (!allCapitals(second)) {
    label += " \xC2\xB7 ";
    label += second;
    return label.size() - second.size();
  }
  const char end = label.empty() ? '.' : label.back();
  const bool ellipsis = label.size() >= 3 && label.compare(label.size() - 3, 3, "\xE2\x80\xA6") == 0;
  label += end == '.' || end == '!' || end == '?' || end == ':' || ellipsis ? " " : ". ";
  const size_t start = label.size();
  bool capital = true;  // a sentence's first letter, as in "Вакцинация? Да, пожалуйста"
  forEachChar(second, [&](const uint32_t c, const char* bytes, const int n) {
    const uint32_t lower = capital ? c : toLowerLetter(c);
    capital = (capital && !isUpperLetter(c)) || c == '.' || c == '!' || c == '?' || c == 0x2026;
    if (lower == c) {
      label.append(bytes, n);
    } else {
      appendShortUtf8(label, lower);
    }
  });
  return start;
}

bool isContentsTitle(const std::string& line) {
  std::string key;
  forEachChar(line, [&](const uint32_t c, const char*, int) {
    if (isUpperLetter(c) || isLowerLetter(c)) appendShortUtf8(key, foldLetter(c));
  });
  for (const char* title : {"оглавление", "содержание", "contents", "tableofcontents"}) {
    if (key == title) return true;
  }
  return false;
}

// `a`, as many bytes as `b` has, holds the same letters and digits whatever their case.
bool sameLetters(const char* a, const std::string& b) {
  for (size_t i = 0; i < b.size();) {
    const int n = utf8Length(b[i]);
    if (utf8Length(a[i]) != n || foldLetter(decodeUtf8(a + i, n)) != foldLetter(decodeUtf8(b.data() + i, n))) {
      return false;
    }
    i += n;
  }
  return true;
}

// Cases `label` from byte `from` on as the book's contents page does, when one of its lines ends
// with the same letters and digits, a page number aside: "Глава 14. Маленькие эйнштейны против
// телевизора" takes "Эйнштейны" from the line "Глава 14. Маленькие Эйнштейны против телевизора".
// A line in capitals tells nothing, nor one the match starts in lower case, inside a longer title.
void caseAsContents(std::string& label, const size_t from, const std::string& contents) {
  if (from >= label.size()) return;
  const std::string key = lettersOf(label.substr(from));
  if (key.size() < 4) return;
  for (size_t start = 0, end; start < contents.size(); start = end + 1) {
    end = contents.find('\n', start);
    size_t numberless = end;
    while (numberless > start && contents[numberless - 1] >= '0' && contents[numberless - 1] <= '9') numberless--;
    for (const size_t last : {end, numberless}) {
      if (last - start < key.size()) continue;
      const char* match = contents.data() + last - key.size();
      if ((static_cast<uint8_t>(*match) & 0xC0) == 0x80 || !sameLetters(match, key)) continue;
      const std::string model(match, key.size());
      const uint32_t first = decodeUtf8(model.data(), utf8Length(model[0]));
      if (!allCapitals(model) && !isLowerLetter(first)) {
        std::string out = label.substr(0, from);
        size_t at = 0;
        forEachChar(label.substr(from), [&](const uint32_t c, const char* bytes, const int n) {
          uint32_t cased = c;
          if (isLetterOrDigit(c)) {
            const int m = utf8Length(model[at]);
            const uint32_t like = decodeUtf8(model.data() + at, m);
            at += m;
            cased = isLowerLetter(like) ? toLowerLetter(c) : isUpperLetter(like) ? toUpperLetter(c) : c;
          }
          if (cased == c) {
            out.append(bytes, n);
          } else {
            appendShortUtf8(out, cased);
          }
        });
        label.swap(out);
        return;
      }
    }
  }
}

// A heading's number, its second word in lower case: "3", "iv", "первая", "three" or "fourth".
// 0 for any other word.
unsigned headingNumber(const std::string& word) {
  if (word.empty() || word.size() > 24) return 0;
  if (std::all_of(word.begin(), word.end(), [](const char c) { return c >= '0' && c <= '9'; })) {
    return word.size() <= 4 ? static_cast<unsigned>(strtoul(word.c_str(), nullptr, 10)) : 0;
  }
  if (word.find_first_not_of("ivxlc") == std::string::npos) {
    int value = 0, largest = 0;
    for (auto it = word.rbegin(); it != word.rend(); ++it) {
      const int v = *it == 'i' ? 1 : *it == 'v' ? 5 : *it == 'x' ? 10 : *it == 'l' ? 50 : 100;
      value += v < largest ? -v : v;
      largest = std::max(largest, v);
    }
    return value > 0 ? static_cast<unsigned>(value) : 0;
  }
  // Stems, so every gender and case of the word matches; the teens before the units they start with.
  static constexpr const char* RU[] = {"одиннадцат", "двенадцат", "тринадцат",   "четырнадцат", "пятнадцат",
                                       "шестнадцат", "семнадцат", "восемнадцат", "девятнадцат", "двадцат",
                                       "перв",       "втор",      "трет",        "четв",        "пят",
                                       "шест",       "седьм",     "восьм",       "девят",       "десят"};
  static constexpr unsigned RU_VALUES[] = {11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (size_t i = 0; i < sizeof(RU) / sizeof(RU[0]); i++) {
    if (word.compare(0, strlen(RU[i]), RU[i]) == 0) return RU_VALUES[i];
  }
  static constexpr const char* EN[][2] = {{"one", "first"},     {"two", "second"},      {"three", "third"},
                                          {"four", "fourth"},   {"five", "fifth"},      {"six", "sixth"},
                                          {"seven", "seventh"}, {"eight", "eighth"},    {"nine", "ninth"},
                                          {"ten", "tenth"},     {"eleven", "eleventh"}, {"twelve", "twelfth"}};
  for (size_t i = 0; i < sizeof(EN) / sizeof(EN[0]); i++) {
    if (word == EN[i][0] || word == EN[i][1]) return static_cast<unsigned>(i + 1);
  }
  return 0;
}

enum class Heading : uint8_t { Other, Group, Chapter };

// "Часть первая" or "Part II" opens a group of chapters; "Глава 3" or "Chapter three" is a
// chapter, numbered in `number`.
Heading headingKind(const std::string& label, unsigned& number) {
  std::string words[2];
  size_t word = 0;
  forEachChar(label, [&](const uint32_t c, const char*, int) {
    if (word > 1) return;
    if (isUpperLetter(c) || isLowerLetter(c) || (c >= '0' && c <= '9')) {
      appendShortUtf8(words[word], foldLetter(c));
    } else if (!words[word].empty()) {
      word++;
    }
  });
  number = headingNumber(words[1]);
  if (number == 0) return Heading::Other;
  for (const char* group : {"часть", "книга", "part", "book"}) {
    if (words[0] == group) return Heading::Group;
  }
  for (const char* chapter : {"глава", "chapter"}) {
    if (words[0] == chapter) return Heading::Chapter;
  }
  return Heading::Other;
}

// The inline styles a bold title line may carry: a link, an image or a note marker makes it text.
bool isLineStyle(const char* name) {
  return strcmp(name, "strong") == 0 || strcmp(name, "emphasis") == 0 || strcmp(name, "strikethrough") == 0;
}

constexpr const char* XHTML_HEAD =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title></title>"
    "<link rel=\"stylesheet\" type=\"text/css\" href=\"style.css\"/></head><body>";
constexpr const char* XHTML_TAIL = "</body></html>";
constexpr const char* SECTION_OPEN = "<div class=\"section\">";
// calibre writes runs of 20 <empty-line/>s for vertical layout; past this they only push text onto blank pages.
constexpr uint8_t MAX_EMPTY_LINES = 2;

constexpr const char* STYLE_CSS =
    "p{text-indent:1.5em;margin:0}"
    "h1,h2,h3,h4,h5,h6{text-align:center;text-indent:0}"
    // Insets go on the wrappers, so a text-author lines up under its epigraph or cite.
    "div.epigraph{margin-left:30%}"
    "p.epigraph{font-style:italic;text-indent:0}"
    "blockquote.cite{margin-left:1.5em;margin-right:1.5em}"
    "p.annotation{font-style:italic}"
    ".poem{margin-left:1.5em}"
    // The reader indents every first line unless its CSS indent is negative; -1px lines it up with the next verses.
    "p.stanza{text-indent:-1px}"
    ".v{text-indent:0}"
    ".text-author{text-align:right;font-style:italic;text-indent:0}"
    ".date{text-align:right;text-indent:0}"
    ".empty-line{text-indent:0}"
    ".note-title{font-weight:bold;text-indent:0}"
    ".image,.cover{text-align:center;text-indent:0}";

constexpr const char* CONTAINER_XML =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
    "<container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
    "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
    "</rootfiles></container>";

bool writeWhole(Fb2ToEpub::Output& out, const char* name, const char* data, size_t len) {
  return out.beginFile(name) && out.write(data, len) && out.endFile();
}

bool writeWhole(Fb2ToEpub::Output& out, const char* name, const std::string& data) {
  return writeWhole(out, name, data.data(), data.size());
}

// Streams one package file into the Output, which batches the small writes.
class PackageWriter {
 public:
  PackageWriter(Fb2ToEpub::Output& out, const char* name) : out(out), ok(out.beginFile(name)) {}
  void put(const char* s, size_t len) {
    if (ok && !out.write(s, len)) ok = false;
  }
  void put(const char* s) { put(s, strlen(s)); }
  void putEscaped(const std::string& s, bool attr = false) {
    buf.clear();
    appendEscaped(buf, s, attr);
    put(buf.data(), buf.size());
  }
  bool end() { return ok && out.endFile(); }

 private:
  Fb2ToEpub::Output& out;
  bool ok;
  std::string buf;
};

enum class Kind : uint8_t {
  Other,
  Description,
  TitleInfo,
  Author,
  CoverPage,
  Body,
  Section,
  Title,
  Line,       // a title's <p> or a stanza's <v>: text inside a paragraph its parent opened
  Wrapper,    // epigraph, cite, poem, ... : blocks splitting while open
  Stanza,     // a wrapper whose <v>s are the lines of one paragraph
  TextBlock,  // p, v, subtitle, td, ... : character data is content
  Inline,
  NoteLink,  // <a type="note">: its marker stays out of TOC labels
  Binary,
};

enum class Capture : uint8_t { None, Title, Language, AuthorPart };

struct Frame {
  Kind kind;
  const char* close;      // markup emitted at the end, or nullptr
  const char* paraClass;  // Wrapper: class for its plain paragraphs (the reader does not inherit div styles)
};

}  // namespace

struct Fb2ToEpub::Pass {
  Fb2ToEpub& self;
  Output* out;  // nullptr: measuring pass
  bool metadataOnly = false;

  XML_Parser parser = nullptr;
  bool sawRoot = false;
  bool notRoot = false;
  bool stopped = false;
  bool xmlError = false;
  bool outputError = false;

  std::vector<Frame> stack;
  uint16_t openKinds[static_cast<int>(Kind::Binary) + 1] = {};

  // Description.
  Capture capture = Capture::None;
  std::string captured;
  std::string authorParts[4];  // first, middle, last, nickname
  int authorPart = 0;

  // Bodies.
  bool notesBody = false;
  bool bodyContent = false;  // text or an image seen in a body
  uint8_t emptyLines = 0;    // <empty-line/>s since the last text or image
  uint8_t sectionDepth = 0;
  uint8_t sectionDivs = 0;  // section <div>s open in the output
  uint32_t sectionSerial = 0;
  int textDepth = 0;
  int titleLines = 0;
  bool verses = false;  // a stanza's paragraph is open

  // Note markers a converter (calibre) left as plain text: "часов1." or "скрининг[1]". Digits are
  // held back until what follows shows whether they are a marker, which is then raised. The
  // measuring pass only counts them: the write pass raises each form only in a book whose
  // markers number up in runs and that has no real note links (Fb2ToEpub::bareMarkers).
  uint8_t letters = 0;       // letters right before the current position
  bool lowerLetter = false;  // ... one of them lowercase
  bool latinWord = false;    // ... one of them Latin, as in "Win32" or "Кf3"
  bool afterLatin = false;   // the last character is a Latin letter
  bool afterCloser = false;  // the last character closes a quote, parenthesis or superscript
  bool glued = false;        // the last character is not a space
  bool path = false;         // the current word has a '/', as a URL does
  bool heldBracket = false;  // a '[' is held
  char held[3];              // digits held
  uint8_t heldLen = 0;
  uint16_t bareNext = 1, bracketNext = 1;  // the next number a run of markers expects
  uint16_t bareRun = 0, bracketRun = 0;
  bool tocPending = false;
  uint8_t tocDepth = 0;
  std::string tocAnchor;
  std::string tocLabel;
  // The last TOC entry's anchor while only titles follow it in its file. A section opening
  // there takes the same anchor, so the reader keeps "Part" and "Chapter 1" on one page
  // instead of breaking before each.
  bool tocShareable = false;
  std::string lastTocAnchor;

  // Bold opening lines standing in for a section's title (Fb2ToEpub::boldTitles). Measuring, a
  // paragraph at a section's start is tried as one and judged when it ends; writing sets the
  // lines the measuring pass chose as a title.
  uint32_t mainSections = 0;
  uint8_t boldSeek = 0;    // measuring: 1 looks for a first line, 2 a second, 3 a third (a list, not a title)
  bool boldProbe = false;  // measuring: the open paragraph is being tried
  bool probeFails = false;
  bool probeBold = false;
  std::string probeText;
  size_t nextBold = 0;              // writing: the next of self.boldTitles
  uint8_t boldLines = 0;            // writing: lines of this section's title not yet ended
  const char* boldClose = nullptr;  // writing: the end tag of the title open
  // Measuring: the book's own contents page, its first section titled so, read into self.contents.
  bool contentsSeen = false;
  uint8_t contentsDepth = 0;  // its section's depth while open, 0 otherwise
  size_t contentsLines = 0;
  std::string contentsLine;

  // Current content file.
  bool fileOpen = false;
  uint16_t fileNo = 0;
  size_t fileText = 0;          // character data only, so both passes split alike
  bool fileHasContent = false;  // text or an image, not just open section markup

  // Binary being decoded.
  bool decoding = false;
  uint32_t b64 = 0;
  int b64Bits = 0;

  std::string buf;

  Pass(Fb2ToEpub& self, Output* out) : self(self), out(out) {
    stack.reserve(32);
    buf.reserve(256);
  }

  bool measuring() const { return out == nullptr; }
  bool open(Kind k) const { return openKinds[static_cast<int>(k)] > 0; }

  void writeRaw(const char* s, size_t len) {
    if (out && !outputError && !out->write(s, len)) outputError = true;
  }
  void writeRaw(const char* s) { writeRaw(s, strlen(s)); }

  void openFile() {
    fileNo = ++self.fileCount;
    fileOpen = true;
    fileText = 0;
    fileHasContent = false;
    tocShareable = false;
    emptyLines = 0;  // a run before the break does not eat the new page's top spacing
    if (out) {
      char name[32];
      snprintf(name, sizeof(name), "OEBPS/s%u.xhtml", static_cast<unsigned>(fileNo));
      if (!out->beginFile(name)) outputError = true;
    }
    writeRaw(XHTML_HEAD);
    for (int i = 0; i < sectionDivs; i++) writeRaw(SECTION_OPEN);
  }

  void closeFile() {
    if (!fileOpen) return;
    for (int i = 0; i < sectionDivs; i++) writeRaw("</div>");
    writeRaw(XHTML_TAIL);
    if (out && !outputError && !out->endFile()) outputError = true;
    fileOpen = false;
  }

  void emit(const char* s, size_t len) {
    if (!fileOpen) openFile();
    writeRaw(s, len);
  }
  void emit(const char* s) { emit(s, strlen(s)); }
  void emit(const std::string& s) { emit(s.data(), s.size()); }

  bool wrapped() const { return open(Kind::Wrapper) || open(Kind::Stanza); }
  bool canSplit() const { return fileOpen && textDepth == 0 && !wrapped() && !open(Kind::Title); }

  void recordId(const char* id) {
    if (measuring() && id && *id && self.idFile.size() < MAX_IDS && idFits(id))
      self.idFile.emplace(id, fileOpen ? fileNo : static_cast<uint16_t>(self.fileCount + 1));
  }

  // "<tag class="cls" id="id">", recording the id against the file it lands in.
  void emitOpen(const char* tag, const char* cls, const char* id, const bool linkable = true) {
    buf = "<";
    buf += tag;
    if (cls) {
      buf += " class=\"";
      buf += cls;
      buf += '"';
    }
    if (id && *id) {
      buf += " id=\"";
      appendEscaped(buf, id, strlen(id), true);
      buf += '"';
    }
    buf += '>';
    if (!fileOpen) openFile();
    if (linkable) recordId(id);
    writeRaw(buf.data(), buf.size());
  }

  void push(Kind kind, const char* close = nullptr, const char* paraClass = nullptr) {
    stack.push_back({kind, close, paraClass});
    openKinds[static_cast<int>(kind)]++;
  }

  void startElement(const char* rawName, const XML_Char** atts) {
    endHeld();
    const char* name = localName(rawName);
    if (stack.size() >= MAX_DEPTH || self.fileCount >= MAX_FILES) {
      // Pushed because Expat still ends an empty element after a stop.
      push(Kind::Other);
      XML_StopParser(parser, XML_FALSE);
      return;
    }
    if (!sawRoot) {
      sawRoot = true;
      if (strcmp(name, "FictionBook") != 0) {
        notRoot = true;
        XML_StopParser(parser, XML_FALSE);
        return;
      }
      push(Kind::Other);
      return;
    }

    if (open(Kind::Description)) {
      startDescription(name, atts);
      return;
    }
    if (open(Kind::Binary)) {
      push(Kind::Other);
      return;
    }
    if (stack.size() == 1) {
      if (strcmp(name, "description") == 0) {
        push(Kind::Description);
      } else if (strcmp(name, "body") == 0) {
        startBody(atts);
      } else if (strcmp(name, "binary") == 0) {
        startBinary(atts);
      } else {
        push(Kind::Other);
      }
      return;
    }
    if (open(Kind::Body)) {
      startContent(name, atts);
      return;
    }
    push(Kind::Other);
  }

  void startDescription(const char* name, const XML_Char** atts) {
    if (strcmp(name, "title-info") == 0 && !open(Kind::TitleInfo)) {
      push(Kind::TitleInfo);
      return;
    }
    if (!open(Kind::TitleInfo) || !measuring()) {
      push(Kind::Other);
      return;
    }
    if (strcmp(name, "author") == 0) {
      for (auto& part : authorParts) part.clear();
      push(Kind::Author);
    } else if (strcmp(name, "coverpage") == 0) {
      push(Kind::CoverPage);
    } else if (open(Kind::CoverPage) && strcmp(name, "image") == 0) {
      const char* href = attribute(atts, "href");
      if (href && href[0] == '#' && self.meta.coverId.empty() && idFits(href + 1)) self.meta.coverId = href + 1;
      push(Kind::Other);
    } else if (open(Kind::Author)) {
      static constexpr const char* PARTS[] = {"first-name", "middle-name", "last-name", "nickname"};
      authorPart = -1;
      for (int i = 0; i < 4; i++) {
        if (strcmp(name, PARTS[i]) == 0) authorPart = i;
      }
      if (authorPart >= 0) beginCapture(Capture::AuthorPart);
      push(Kind::Other);
    } else if (strcmp(name, "book-title") == 0) {
      beginCapture(Capture::Title);
      push(Kind::Other);
    } else if (strcmp(name, "lang") == 0) {
      beginCapture(Capture::Language);
      push(Kind::Other);
    } else {
      push(Kind::Other);
    }
  }

  void beginCapture(const Capture c) {
    capture = c;
    captured.clear();
  }

  void endCapture() {
    collapseSpaces(captured);
    truncateUtf8(captured, MAX_METADATA_BYTES);
    switch (capture) {
      case Capture::Title:
        if (self.meta.title.empty()) self.meta.title = captured;
        break;
      case Capture::Language:
        if (self.meta.language.empty()) self.meta.language = captured;
        break;
      case Capture::AuthorPart:
        authorParts[authorPart] = captured;
        break;
      case Capture::None:
        break;
    }
    capture = Capture::None;
  }

  void endAuthor() {
    std::string author;
    for (int i = 0; i < 3; i++) {
      if (authorParts[i].empty()) continue;
      if (!author.empty()) author += ' ';
      author += authorParts[i];
    }
    if (author.empty()) author = authorParts[3];
    if (!author.empty() && self.meta.authors.size() < MAX_AUTHORS) self.meta.authors.push_back(std::move(author));
  }

  void startBody(const XML_Char** atts) {
    closeFile();
    const char* bodyName = attribute(atts, "name");
    notesBody = bodyName && *bodyName;
    sectionDepth = 0;
    sectionDivs = 0;
    textDepth = 0;
    // A notes body's title is its TOC entry; the main body's is the book title.
    tocPending = notesBody;
    tocDepth = 1;
    tocAnchor.clear();
    boldSeek = 0;
    push(Kind::Body);
  }

  void startBinary(const XML_Char** atts) {
    closeFile();
    push(Kind::Binary);
    const char* id = attribute(atts, "id");
    if (!id || !*id) return;
    if (measuring()) {
      if (self.binaries.size() >= MAX_BINARIES || !idFits(id)) return;
      const Binary binary{static_cast<uint16_t>(self.binaries.size()), imageExtension(attribute(atts, "content-type"))};
      self.binaries.emplace(id, binary);
      return;
    }
    const auto it = self.binaries.find(id);
    if (it == self.binaries.end() || !it->second.extension) return;
    char name[40];
    snprintf(name, sizeof(name), "OEBPS/img%u.%s", static_cast<unsigned>(it->second.index), it->second.extension);
    if (!out->beginFile(name)) {
      outputError = true;
      return;
    }
    decoding = true;
    b64 = 0;
    b64Bits = 0;
  }

  void startSection(const char* id) {
    const size_t threshold = notesBody ? NOTES_SPLIT_BYTES : MAIN_SPLIT_BYTES;
    // A top-level section of the main body (chapter, part or front-matter page) opens a page of its own,
    // titled or not, however short the one before it, until the book has MAX_OWN_PAGE_FILES files.
    const bool ownPage = !notesBody && sectionDepth == 0 && fileHasContent && self.fileCount < MAX_OWN_PAGE_FILES;
    if (canSplit() && (ownPage || fileText >= threshold)) closeFile();
    const bool startsFile = !fileOpen;
    char synthetic[16];
    const bool named = id && *id;
    if (!named) {
      snprintf(synthetic, sizeof(synthetic), "fb2s%lu", static_cast<unsigned long>(++sectionSerial));
      id = synthetic;
    }
    // Only TOC entries point at a synthetic id, and they carry it themselves: no link-table entry.
    emitOpen("div", "section", id, named);
    sectionDivs++;
    sectionDepth++;
    push(Kind::Section, "</div>");
    if (!notesBody) {
      tocPending = true;
      tocDepth = sectionDepth;
      tocAnchor = tocShareable ? lastTocAnchor : (startsFile ? "" : id);
      mainSections++;
      if (measuring()) {
        boldSeek = self.findBoldTitles && !self.sectionTitles ? 1 : 0;
      } else if (nextBold < self.boldTitles.size() && self.boldTitles[nextBold].section == mainSections) {
        boldLines = self.boldTitles[nextBold++].lines;
      }
    }
  }

  // A poem's or stanza's title sits a level below its section's. Past h4 the reader sets
  // headings smaller than the text, so deeper titles stay at h4.
  int titleLevel() const {
    const int level = sectionDepth + 1 + (wrapped() ? 1 : 0);
    return level > 4 ? 4 : level;
  }
  static constexpr const char* TITLE_CLOSE[] = {"</h1>", "</h2>", "</h3>", "</h4>"};

  void startTitle(const char* id) {
    if (!notesBody && stack.back().kind == Kind::Section) self.sectionTitles = true;
    if (notesBody && sectionDepth > 0) {
      emitOpen("p", "note-title", id);
      push(Kind::Title, "</p>");
    } else {
      const int level = titleLevel();
      const char tag[3] = {'h', static_cast<char>('0' + level), '\0'};
      emitOpen(tag, "title", id);
      push(Kind::Title, TITLE_CLOSE[level - 1]);
    }
    titleLines = 0;
    tocLabel.clear();
  }

  void endTitle() {
    if (!tocPending) return;
    tocPending = false;
    collapseSpaces(tocLabel);
    if (!measuring() || tocLabel.empty() || self.toc.size() >= MAX_TOC_ENTRIES) return;
    truncateUtf8(tocLabel, MAX_LABEL_BYTES);
    self.toc.push_back({fileNo, tocDepth, tocAnchor, tocLabel});
    tocShareable = true;
    lastTocAnchor = tocAnchor;
  }

  // Measuring: what opens a section decides whether its first paragraphs are its title.
  void seekBold(const char* name) {
    if (textDepth > 0) {
      if (boldProbe && !isLineStyle(name)) probeFails = true;
      return;
    }
    if (stack.back().kind != Kind::Section) return;
    if (strcmp(name, "p") == 0) {
      boldProbe = true;
      probeFails = probeBold = false;
      probeText.clear();
    } else if (boldSeek > 1 || strcmp(name, "empty-line") != 0) {
      boldSeek = 0;
    }
  }

  // Measuring: a paragraph tried as a title line ends. True when it is one.
  bool endProbe() {
    boldProbe = false;
    collapseSpaces(probeText);
    // One the file would split after stays text, as the writing pass splits there.
    const bool line = !probeFails && probeBold && titleLike(probeText) && !(canSplit() && fileText >= MAX_FILE_BYTES);
    if (!line || boldSeek == 3 || (boldSeek == 1 && self.toc.size() >= MAX_TOC_ENTRIES)) {
      if (line && boldSeek == 3) {
        // A third bold line in a row: a list, not a title.
        self.boldTitles.pop_back();
        self.toc.pop_back();
      }
      boldSeek = 0;
      return false;
    }
    if (boldSeek == 1) {
      const uint16_t capitals = allCapitals(probeText) ? 0 : NO_CAPITALS;
      self.boldTitles.push_back({mainSections, static_cast<uint16_t>(self.toc.size()), capitals, 1});
      self.toc.push_back({fileNo, tocDepth, tocAnchor, probeText});
      if (!contentsSeen && isContentsTitle(probeText)) {
        contentsSeen = true;
        contentsDepth = sectionDepth;
      }
    } else {
      BoldTitle& title = self.boldTitles.back();
      title.lines = 2;
      const size_t second = joinTitleLines(self.toc.back().label, probeText);
      if (title.capitals == NO_CAPITALS && allCapitals(probeText)) title.capitals = static_cast<uint16_t>(second);
    }
    truncateUtf8(self.toc.back().label, MAX_LABEL_BYTES);
    boldSeek++;
    tocShareable = true;
    lastTocAnchor = tocAnchor;
    return true;
  }

  void addContentsLine() {
    const std::string line = lettersOf(contentsLine);
    contentsLine.clear();
    if (line.empty() || line.size() > MAX_LABEL_BYTES || contentsLines >= MAX_CONTENTS_LINES ||
        self.contents.size() + line.size() + 1 > MAX_CONTENTS_BYTES) {
      return;
    }
    self.contents += line;
    self.contents += '\n';
    contentsLines++;
  }

  // Writing: a section's first paragraph opens the title the measuring pass found for it. It
  // has no element of its own: its lines are the paragraphs.
  void startBoldTitle(const char* id) {
    const int level = titleLevel();
    const char tag[3] = {'h', static_cast<char>('0' + level), '\0'};
    emitOpen(tag, "title", id);
    boldClose = TITLE_CLOSE[level - 1];
    openKinds[static_cast<int>(Kind::Title)]++;
    tocPending = false;
    titleLines = 1;
    resetMarkerScan();
    textDepth++;
    push(Kind::Line);
  }

  void endBoldTitle() {
    emit(boldClose);
    boldClose = nullptr;
    boldLines = 0;
    openKinds[static_cast<int>(Kind::Title)]--;
  }

  void startImage(const XML_Char** atts) {
    // Open the file whether or not the image resolves, so both passes number files alike.
    if (!fileOpen) openFile();
    fileHasContent = true;
    tocShareable = false;
    push(Kind::Other);
    const char* href = attribute(atts, "href");
    const char* id = attribute(atts, "id");
    if (!href || href[0] != '#') return;
    // Converters such as calibre repeat the <coverpage> image as the body's first content.
    if (!bodyContent && !notesBody && !self.meta.coverId.empty() && self.meta.coverId == href + 1) {
      self.coverInBody = true;
    }
    bodyContent = true;
    emptyLines = 0;
    const auto it = self.binaries.find(href + 1);
    if (measuring() || it == self.binaries.end() || !it->second.extension) {
      recordId(id);
      return;
    }
    char src[96];
    snprintf(src, sizeof(src), "<img src=\"img%u.%s\" alt=\"\"/>", static_cast<unsigned>(it->second.index),
             it->second.extension);
    if (textDepth > 0) {
      emit(src);
    } else {
      emitOpen("div", "image", id);
      emit(src);
      emit("</div>");
    }
  }

  void startLink(const XML_Char** atts) {
    if (!fileOpen) openFile();
    const char* type = exactAttribute(atts, "type");
    const bool note = type && strcmp(type, "note") == 0;
    if (note) self.noteLinks = true;
    // Note markers are raised, as in print: at the baseline "word2" reads as one word.
    const bool raise = note && !inSup();
    const char* href = attribute(atts, "href");
    buf.clear();
    if (href && href[0] == '#') {
      const auto it = self.idFile.find(href + 1);
      if (it != self.idFile.end()) {
        char file[24];
        snprintf(file, sizeof(file), "s%u.xhtml", static_cast<unsigned>(it->second));
        buf = "<a href=\"";
        buf += file;
        appendEscaped(buf, href, strlen(href), true);
        buf += "\">";
      }
    } else if (href && *href) {
      buf = "<a href=\"";
      appendEscaped(buf, href, strlen(href), true);
      buf += "\">";
    }
    const bool link = !buf.empty();
    if (raise) buf += "<sup>";
    if (!buf.empty()) emit(buf);
    const char* close = link ? (raise ? "</sup></a>" : "</a>") : (raise ? "</sup>" : nullptr);
    push(note ? Kind::NoteLink : Kind::Inline, close);
  }

  bool inBold() const {
    return std::any_of(stack.begin(), stack.end(),
                       [](const Frame& f) { return f.close && strcmp(f.close, "</strong>") == 0; });
  }

  bool inSup() const {
    return std::any_of(stack.begin(), stack.end(),
                       [](const Frame& f) { return f.close && strcmp(f.close, "</sup>") == 0; });
  }

  void startText(const char* tag, const char* cls, const char* id, const char* close) {
    tocPending = false;  // a section's title comes first; a later one belongs to a poem or stanza
    if (contentsDepth) contentsLine.clear();
    for (auto it = stack.rbegin(); !cls && it != stack.rend(); ++it) {
      if (it->kind == Kind::Wrapper) cls = it->paraClass;
    }
    emitOpen(tag, cls, id);
    resetMarkerScan();
    textDepth++;
    push(Kind::TextBlock, close);
  }

  void startWrapper(const char* tag, const char* cls, const char* id, const char* close,
                    const char* paraClass = nullptr) {
    tocPending = false;
    emitOpen(tag, cls, id);
    push(Kind::Wrapper, close, paraClass);
  }

  // A stanza's verses are the lines of one paragraph: as paragraphs of their own each would
  // get the reader's paragraph spacing, first-line indent and justification.
  void startVerse(const char* id) {
    if (verses) {
      emit("<br/>");
      if (id && *id) {
        // The reader keeps no anchor for an id on a <span>.
        emitOpen("a", nullptr, id);
        emit("</a>");
      }
    } else {
      emitOpen("p", "stanza", id);
      verses = true;
    }
    resetMarkerScan();
    textDepth++;
    push(Kind::Line);
  }

  void endVerses() {
    if (!verses) return;
    emit("</p>");
    verses = false;
  }

  void startInline(const char* tag, const char* close) {
    emit(tag);
    push(Kind::Inline, close);
  }

  void startContent(const char* name, const XML_Char** atts) {
    const char* id = attribute(atts, "id");
    const bool verse = strcmp(name, "v") == 0;
    if (verses && textDepth == 0 && !verse) endVerses();  // a title or subtitle among the verses
    if (boldSeek > 0) seekBold(name);
    if (boldLines > 0 && !open(Kind::Title) && strcmp(name, "p") == 0 && stack.back().kind == Kind::Section) {
      startBoldTitle(id);
      return;
    }
    if (open(Kind::Title)) {
      if (strcmp(name, "p") == 0) {
        if (titleLines++ > 0) {
          emit("<br/>");
          appendBounded(tocLabel, " ", 1, 2 * MAX_LABEL_BYTES);
        }
        recordId(id);
        textDepth++;
        push(Kind::Line);
        return;
      }
      if (strcmp(name, "empty-line") == 0) {
        push(Kind::Other);
        return;
      }
    }

    if (strcmp(name, "section") == 0) {
      startSection(id);
    } else if (strcmp(name, "title") == 0) {
      startTitle(id);
    } else if (strcmp(name, "p") == 0) {
      startText("p", nullptr, id, "</p>");
    } else if (verse) {
      if (!stack.empty() && stack.back().kind == Kind::Stanza) {
        startVerse(id);
      } else {
        startText("p", "v", id, "</p>");
      }
    } else if (strcmp(name, "subtitle") == 0) {
      // A heading, as the reader's paragraph alignment and indent override CSS on paragraphs: "* * *" stays centered.
      startText("h4", "subtitle", id, "</h4>");
    } else if (strcmp(name, "text-author") == 0) {
      startText("p", "text-author", id, "</p>");
    } else if (strcmp(name, "date") == 0) {
      startText("p", "date", id, "</p>");
    } else if (strcmp(name, "empty-line") == 0) {
      if (emptyLines < MAX_EMPTY_LINES) {
        emit("<p class=\"empty-line\">&#160;</p>");
        emptyLines++;
      }
      push(Kind::Other);
    } else if (strcmp(name, "epigraph") == 0) {
      startWrapper("div", "epigraph", id, "</div>", "epigraph");
    } else if (strcmp(name, "cite") == 0) {
      startWrapper("blockquote", "cite", id, "</blockquote>", "cite");
    } else if (strcmp(name, "poem") == 0) {
      startWrapper("div", "poem", id, "</div>");
    } else if (strcmp(name, "stanza") == 0) {
      if (textDepth > 0) {
        // Inside a verse or paragraph (malformed FB2) it cannot open a paragraph of its own.
        startWrapper("div", "stanza", id, "</div>");
      } else {
        tocPending = false;
        emitOpen("div", "stanza", id);
        push(Kind::Stanza, "</div>");
      }
    } else if (strcmp(name, "annotation") == 0) {
      startWrapper("div", "annotation", id, "</div>", "annotation");
    } else if (strcmp(name, "table") == 0) {
      startWrapper("table", nullptr, id, "</table>");
    } else if (strcmp(name, "tr") == 0) {
      startWrapper("tr", nullptr, id, "</tr>");
    } else if (strcmp(name, "td") == 0 || strcmp(name, "th") == 0) {
      startText(name, nullptr, id, name[1] == 'd' ? "</td>" : "</th>");
    } else if (strcmp(name, "image") == 0) {
      startImage(atts);
    } else if (strcmp(name, "a") == 0) {
      startLink(atts);
    } else if (strcmp(name, "emphasis") == 0) {
      startInline("<em>", "</em>");
    } else if (strcmp(name, "strong") == 0) {
      startInline("<strong>", "</strong>");
    } else if (strcmp(name, "strikethrough") == 0) {
      startInline("<s>", "</s>");
    } else if (strcmp(name, "sub") == 0) {
      startInline("<sub>", "</sub>");
    } else if (strcmp(name, "sup") == 0) {
      startInline("<sup>", "</sup>");
    } else if (strcmp(name, "code") == 0) {
      startInline("<code>", "</code>");
    } else {
      recordId(id);
      push(Kind::Other);
    }
  }

  void endElement() {
    endHeld();
    if (stack.empty()) return;
    const Frame frame = stack.back();
    stack.pop_back();
    openKinds[static_cast<int>(frame.kind)]--;
    // Digits glued after a superscript continue it: "человек[26]<sup>, </sup>1".
    if (frame.close && strcmp(frame.close, "</sup>") == 0) afterCloser = true;

    switch (frame.kind) {
      case Kind::Description:
        if (metadataOnly) {
          stopped = true;
          XML_StopParser(parser, XML_FALSE);
        }
        return;
      case Kind::Author:
        endAuthor();
        return;
      case Kind::Other:
        if (capture != Capture::None && open(Kind::Description)) endCapture();
        return;
      case Kind::Binary:
        endBinary();
        return;
      case Kind::Body:
        closeFile();
        sectionDivs = 0;
        return;
      case Kind::Section:
        if (boldClose) endBoldTitle();  // cut short by an XML error
        boldSeek = 0;
        boldLines = 0;
        if (sectionDepth == contentsDepth) contentsDepth = 0;
        sectionDepth--;
        sectionDivs--;
        // The file may have been split after this section's last paragraph.
        if (fileOpen) writeRaw(frame.close);
        return;
      case Kind::Line:
        textDepth--;
        if (boldClose && --boldLines == 0) endBoldTitle();
        return;
      case Kind::Stanza:
        endVerses();
        emit(frame.close);
        return;
      case Kind::Title:
        emit(frame.close);
        endTitle();
        return;
      case Kind::TextBlock:
        emit(frame.close);
        textDepth--;
        if (boldProbe && endProbe()) return;  // a title line: no split under it
        if (contentsDepth) addContentsLine();
        if (canSplit() && fileText >= MAX_FILE_BYTES) closeFile();
        return;
      default:
        if (frame.close) emit(frame.close);
        return;
    }
  }

  void endBinary() {
    if (!decoding) return;
    decoding = false;
    if (!outputError && !out->endFile()) outputError = true;
  }

  void decodeBase64(const char* s, const int len) {
    char chunk[128];
    size_t n = 0;
    for (int i = 0; i < len; i++) {
      const char c = s[i];
      int v;
      if (c >= 'A' && c <= 'Z') {
        v = c - 'A';
      } else if (c >= 'a' && c <= 'z') {
        v = c - 'a' + 26;
      } else if (c >= '0' && c <= '9') {
        v = c - '0' + 52;
      } else if (c == '+' || c == '-') {
        v = 62;
      } else if (c == '/' || c == '_') {
        v = 63;
      } else {
        continue;  // whitespace and '=' padding
      }
      b64 = (b64 << 6) | static_cast<uint32_t>(v);
      b64Bits += 6;
      if (b64Bits >= 8) {
        b64Bits -= 8;
        chunk[n++] = static_cast<char>((b64 >> b64Bits) & 0xFF);
        if (n == sizeof(chunk)) {
          writeRaw(chunk, n);
          n = 0;
        }
      }
    }
    if (n > 0) writeRaw(chunk, n);
  }

  void resetMarkerScan() {
    letters = 0;
    lowerLetter = latinWord = afterLatin = afterCloser = glued = path = false;
  }

  // Digits a link, <sup>, <sub> or <code> already sets apart stay as they are.
  bool setApart() const {
    return std::any_of(stack.begin(), stack.end(), [](const Frame& f) {
      return f.close && (strstr(f.close, "</a>") || strstr(f.close, "</su") || strcmp(f.close, "</code>") == 0);
    });
  }

  void noteChar(const uint32_t c) {
    const bool lower = isLowerLetter(c);
    const bool latin = isLatinLetter(c);
    if (lower || isUpperLetter(c)) {
      if (letters < UINT8_MAX) letters++;
      lowerLetter = lowerLetter || lower;
      latinWord = latinWord || latin;
    } else {
      letters = 0;
      lowerLetter = latinWord = false;
    }
    // "Ca(OH)2" is a formula, "сна)15" a marker.
    afterCloser = isCloser(c) && !afterLatin;
    afterLatin = latin;
    glued = !isTextSpace(c);
    if (!glued) {
      path = false;
    } else if (c == '/') {
      path = true;
    }
  }

  // Appends one character to buf, holding back digits that may be a note marker. A bare marker
  // follows a Cyrillic word of two letters or more (three when all capitals, so "СО2" stays) or
  // a closing quote, and ends at a space, punctuation or an element boundary; a bracketed one is
  // glued to text other than a Latin letter. Numbers of four digits, such as years, and digits
  // after Latin letters ("Win32", "mp3", "x[1]") never qualify.
  void scanMarker(const char* p, const int n, const uint32_t c, const bool bare, const bool bracket) {
    const bool digit = c >= '0' && c <= '9';
    if (heldBracket || heldLen > 0) {
      if (digit && heldLen < sizeof(held)) {
        held[heldLen++] = static_cast<char>(c);
        return;
      }
      const bool closes = heldBracket && c == ']' && heldLen > 0;
      if (closes || (!heldBracket && !digit && endsMarker(c))) {
        raiseHeld();
      } else {
        releaseHeld();
      }
      if (closes) return;
    }
    if (bare && !path && digit && (afterCloser || (letters >= 2 && !latinWord && (lowerLetter || letters >= 3)))) {
      held[0] = static_cast<char>(c);
      heldLen = 1;
      return;
    }
    if (bracket && !path && c == '[' && glued && !afterLatin) {
      heldBracket = true;
      return;
    }
    appendEscaped(buf, p, static_cast<size_t>(n));
    noteChar(c);
  }

  void raiseHeld() {
    if (measuring()) countMarker();
    buf += "<sup>";
    if (heldBracket) buf += '[';
    buf.append(held, heldLen);
    if (heldBracket) buf += ']';
    buf += "</sup>";
    heldBracket = false;
    heldLen = 0;
    resetMarkerScan();
    glued = true;
  }

  void releaseHeld() {
    if (heldBracket) {
      buf += '[';
      noteChar('[');
      heldBracket = false;
    }
    for (uint8_t i = 0; i < heldLen; i++) {
      buf += held[i];
      noteChar(static_cast<uint8_t>(held[i]));
    }
    heldLen = 0;
  }

  // Markers numbered 1, 2, 3... with nothing out of step between them; a 1 starts a new run, as
  // the numbering restarts per chapter.
  void countMarker() {
    unsigned value = 0;
    for (uint8_t i = 0; i < heldLen; i++) value = value * 10 + static_cast<unsigned>(held[i] - '0');
    uint16_t& next = heldBracket ? bracketNext : bareNext;
    uint16_t& run = heldBracket ? bracketRun : bareRun;
    if (value == next) {
      run++;
    } else {
      run = value == 1 ? 1 : 0;
    }
    next = static_cast<uint16_t>(run + 1);
    uint16_t& best = heldBracket ? self.bracketMarkerRun : self.bareMarkerRun;
    best = std::max(best, run);
  }

  // An element starts or ends: a bare marker held so far is complete, a bracketed one is not.
  void endHeld() {
    if (!heldBracket && heldLen == 0) return;
    buf.clear();
    if (heldBracket) {
      releaseHeld();
    } else {
      raiseHeld();
    }
    emit(buf);
  }

  void characters(const XML_Char* s, const int len) {
    if (capture != Capture::None) {
      appendBounded(captured, s, static_cast<size_t>(len), 2 * MAX_METADATA_BYTES);
      return;
    }
    if (decoding) {
      decodeBase64(s, len);
      return;
    }
    if (textDepth <= 0 || !open(Kind::Body)) return;
    buf.clear();
    const bool inText = !open(Kind::Title) && !setApart();
    const bool bare = inText && (measuring() || self.bareMarkers());
    const bool bracket = inText && (measuring() || self.bracketMarkers());
    for (int i = 0; i < len;) {
      const int n = std::min(utf8Length(s[i]), len - i);
      scanMarker(s + i, n, decodeUtf8(s + i, n), bare, bracket);
      i += n;
    }
    emit(buf);
    fileText += static_cast<size_t>(len);
    const bool text = std::any_of(s, s + len, [](const char c) { return !isXmlSpace(c); });
    if (text) {
      fileHasContent = true;
      bodyContent = true;
      emptyLines = 0;
    }
    if (contentsDepth) appendBounded(contentsLine, s, static_cast<size_t>(len), 2 * MAX_LABEL_BYTES);
    if (boldProbe) {
      appendBounded(probeText, s, static_cast<size_t>(len), 2 * MAX_LABEL_BYTES);
      if (text) (inBold() ? probeBold : probeFails) = true;
    }
    if (!open(Kind::Title)) tocShareable = false;
    if (open(Kind::Title) && tocPending && !open(Kind::NoteLink)) {
      appendBounded(tocLabel, s, static_cast<size_t>(len), 2 * MAX_LABEL_BYTES);
    }
  }

  static void XMLCALL onStart(void* ud, const XML_Char* name, const XML_Char** atts) {
    static_cast<Pass*>(ud)->startElement(name, atts);
  }
  static void XMLCALL onEnd(void* ud, const XML_Char*) { static_cast<Pass*>(ud)->endElement(); }
  static void XMLCALL onText(void* ud, const XML_Char* s, int len) { static_cast<Pass*>(ud)->characters(s, len); }

  static bool feed(void* ctx, const char* data, size_t len) {
    auto* pass = static_cast<Pass*>(ctx);
    if (pass->stopped || pass->xmlError || pass->notRoot || pass->outputError) return false;
    const XML_Status status = XML_Parse(pass->parser, data, static_cast<int>(len), XML_FALSE);
    if (status == XML_STATUS_ERROR) {
      if (!pass->stopped && !pass->notRoot) pass->xmlError = true;
      return false;
    }
    return !pass->stopped && !pass->notRoot && !pass->outputError;
  }

  // Runs the document through this pass. False only when it could not be read.
  bool run(Input& input) {
    parser = XML_ParserCreate(nullptr);
    if (!parser) return false;
    XML_SetUserData(parser, this);
    XML_SetElementHandler(parser, onStart, onEnd);
    XML_SetCharacterDataHandler(parser, onText);
    XML_SetUnknownEncodingHandler(parser, unknownEncoding, nullptr);
    const bool streamed = input.stream(feed, this);
    if (streamed && !stopped && !xmlError && !notRoot && !outputError) {
      if (XML_Parse(parser, nullptr, 0, XML_TRUE) == XML_STATUS_ERROR) xmlError = true;
    }
    XML_ParserFree(parser);
    parser = nullptr;
    // A book cut short by an XML error keeps what came before it: close
    // whatever was open so every written file is well-formed.
    if (xmlError && !metadataOnly) {
      while (!stack.empty()) endElement();
    }
    return streamed;
  }
};

namespace {

// One pass over the book for its cover: the <coverpage> reference in
// <title-info>, then the <binary> it names, base64-decoded straight into the
// output in small chunks.
struct CoverPass {
  Fb2ToEpub::Output& out;
  XML_Parser parser = nullptr;
  int depth = 0;
  bool description = false;
  int titleInfoDepth = 0;  // depth of <title-info>, 0 outside it
  bool coverpage = false;
  char coverId[Fb2ToEpub::MAX_ID_BYTES + 1] = {};
  const char* extension = nullptr;  // set once the cover binary opens
  bool decoding = false;
  bool done = false;
  bool failed = false;
  uint32_t bits = 0;
  int bitCount = 0;

  explicit CoverPass(Fb2ToEpub::Output& out) : out(out) {}

  void start(const char* rawName, const XML_Char** atts) {
    const char* name = localName(rawName);
    depth++;
    if (depth == 2 && strcmp(name, "description") == 0) {
      description = true;
    } else if (depth == 3 && strcmp(name, "title-info") == 0) {
      titleInfoDepth = depth;
    } else if (titleInfoDepth && depth == titleInfoDepth + 1 && strcmp(name, "coverpage") == 0) {
      coverpage = true;
    } else if (coverpage && strcmp(name, "image") == 0 && !coverId[0]) {
      const char* href = attribute(atts, "href");
      if (href && href[0] == '#' && idFits(href + 1)) snprintf(coverId, sizeof(coverId), "%s", href + 1);
    } else if (depth == 2 && strcmp(name, "binary") == 0 && coverId[0]) {
      const char* id = attribute(atts, "id");
      if (!id || strcmp(id, coverId) != 0) return;
      extension = imageExtension(attribute(atts, "content-type"));
      // GIF covers have no thumbnail converter.
      if (!extension || strcmp(extension, "gif") == 0 ||
          !out.beginFile(strcmp(extension, "jpg") == 0 ? "cover.jpg" : "cover.png")) {
        extension = nullptr;
        stop(true);
        return;
      }
      decoding = true;
    }
  }

  void end() {
    if (decoding) {
      decoding = false;
      if (!out.endFile()) failed = true;
      stop(failed);
      return;
    }
    if (depth == titleInfoDepth) titleInfoDepth = 0;
    if (titleInfoDepth && depth == titleInfoDepth + 1) coverpage = false;
    // No <coverpage> in the header: no cover, and the rest of the book need not be read.
    if (depth == 2 && description) {
      description = false;
      if (!coverId[0]) stop(false);
    }
    depth--;
  }

  void text(const XML_Char* s, const int len) {
    if (!decoding) return;
    uint8_t chunk[128];
    size_t n = 0;
    for (int i = 0; i < len; i++) {
      const char c = s[i];
      int v;
      if (c >= 'A' && c <= 'Z') {
        v = c - 'A';
      } else if (c >= 'a' && c <= 'z') {
        v = c - 'a' + 26;
      } else if (c >= '0' && c <= '9') {
        v = c - '0' + 52;
      } else if (c == '+' || c == '-') {
        v = 62;
      } else if (c == '/' || c == '_') {
        v = 63;
      } else {
        continue;  // whitespace and '=' padding
      }
      bits = (bits << 6) | static_cast<uint32_t>(v);
      bitCount += 6;
      if (bitCount >= 8) {
        bitCount -= 8;
        chunk[n++] = static_cast<uint8_t>((bits >> bitCount) & 0xFF);
        if (n == sizeof(chunk)) {
          if (!out.write(reinterpret_cast<const char*>(chunk), n)) return stop(true);
          n = 0;
        }
      }
    }
    if (n > 0 && !out.write(reinterpret_cast<const char*>(chunk), n)) stop(true);
  }

  void stop(const bool failure) {
    failed = failed || failure;
    done = true;
    XML_StopParser(parser, XML_FALSE);
  }

  static void XMLCALL onStart(void* ud, const XML_Char* name, const XML_Char** atts) {
    static_cast<CoverPass*>(ud)->start(name, atts);
  }
  static void XMLCALL onEnd(void* ud, const XML_Char*) { static_cast<CoverPass*>(ud)->end(); }
  static void XMLCALL onText(void* ud, const XML_Char* s, int len) { static_cast<CoverPass*>(ud)->text(s, len); }
  static bool feed(void* ctx, const char* data, size_t len) {
    auto* pass = static_cast<CoverPass*>(ctx);
    if (pass->done) return false;
    if (XML_Parse(pass->parser, data, static_cast<int>(len), XML_FALSE) == XML_STATUS_ERROR) {
      if (!pass->done) pass->stop(true);
      return false;
    }
    return !pass->done;
  }
};

}  // namespace

const char* Fb2ToEpub::readCover(Input& input, Output& out) {
  auto pass = makeUniqueNoThrow<CoverPass>(out);
  if (!pass) return nullptr;
  pass->parser = XML_ParserCreate(nullptr);
  if (!pass->parser) return nullptr;
  XML_SetUserData(pass->parser, pass.get());
  XML_SetElementHandler(pass->parser, CoverPass::onStart, CoverPass::onEnd);
  XML_SetCharacterDataHandler(pass->parser, CoverPass::onText);
  XML_SetUnknownEncodingHandler(pass->parser, unknownEncoding, nullptr);
  const bool streamed = input.stream(CoverPass::feed, pass.get());
  XML_ParserFree(pass->parser);
  // An unfinished binary (the input ended inside it) is no cover either.
  const bool complete = streamed && pass->done && !pass->failed && pass->extension && !pass->decoding;
  return complete ? pass->extension : nullptr;
}

bool Fb2ToEpub::readMetadata(Input& input, Metadata& out) {
  Fb2ToEpub converter;
  // Passes hold several strings and the element stack: heap, not the reader task's stack.
  auto pass = makeUniqueNoThrow<Pass>(converter, nullptr);
  if (!pass) return false;
  pass->metadataOnly = true;
  if (!pass->run(input) || pass->xmlError || pass->notRoot || !pass->sawRoot) return false;
  out = std::move(converter.meta);
  return true;
}

bool Fb2ToEpub::convert(Input& input, Output& out) {
  // A book found to have both titles and bold lines taken for them is measured again without.
  for (findBoldTitles = true;; findBoldTitles = false) {
    meta = Metadata{};
    idFile.clear();
    binaries.clear();
    toc.clear();
    boldTitles.clear();
    contents.clear();
    sectionTitles = false;
    cover = nullptr;
    coverInBody = false;
    noteLinks = false;
    bareMarkerRun = bracketMarkerRun = 0;
    fileCount = 0;
    auto measure = makeUniqueNoThrow<Pass>(*this, nullptr);
    if (!measure || !measure->run(input) || measure->notRoot || !measure->sawRoot || fileCount == 0) return false;
    if (!sectionTitles || boldTitles.empty()) break;
  }
  if (!boldTitles.empty()) shapeBoldToc();
  std::string().swap(contents);
  const uint16_t measuredFiles = fileCount;
  if (!meta.coverId.empty()) {
    const auto it = binaries.find(meta.coverId);
    if (it != binaries.end() && it->second.extension) cover = &it->second;
  }

  if (!writeWhole(out, "mimetype", "application/epub+zip", 20) ||
      !writeWhole(out, "META-INF/container.xml", CONTAINER_XML, strlen(CONTAINER_XML)) ||
      !writeWhole(out, "OEBPS/style.css", STYLE_CSS, strlen(STYLE_CSS))) {
    return false;
  }
  if (coverPage()) {
    std::string page = XHTML_HEAD;
    char img[64];
    snprintf(img, sizeof(img), "<div class=\"cover\"><img src=\"img%u.%s\" alt=\"\"/></div>",
             static_cast<unsigned>(cover->index), cover->extension);
    page += img;
    page += XHTML_TAIL;
    if (!writeWhole(out, "OEBPS/cover.xhtml", page)) return false;
  }

  fileCount = 0;
  auto write = makeUniqueNoThrow<Pass>(*this, &out);
  if (!write || !write->run(input) || write->outputError || fileCount != measuredFiles) return false;

  return writeOpf(out) && writeNcx(out);
}

// A book of bold titles (calibre's) has its parts and chapters as sibling sections: its TOC
// nests each "Глава N" under the "Часть" before it, until a heading of another kind. Chapters
// numbered from the start again right after such a heading ("Библиография", then "Глава 1",
// "Глава 2"...) nest under it, so the chapter list is not repeated at the top level. Labels in
// capitals take the case of the book's contents page where it lists them.
void Fb2ToEpub::shapeBoldToc() {
  for (const BoldTitle& title : boldTitles) {
    if (title.capitals != NO_CAPITALS) caseAsContents(toc[title.entry].label, title.capitals, contents);
  }
  uint8_t group = 0;  // depth of the entry the chapters after it nest under, 0: none
  uint8_t other = 0;  // depth of the heading of another kind just before, 0: none
  unsigned lastChapter = 0;
  for (TocEntry& e : toc) {
    if (e.depth < group) group = 0;
    if (group && e.depth > group) continue;  // nested already
    unsigned number = 0;
    const Heading kind = headingKind(e.label, number);
    if (kind == Heading::Group) {
      group = e.depth;
      other = 0;
    } else if (kind == Heading::Chapter) {
      if (!group && other == e.depth && number <= lastChapter) group = other;
      if (group == e.depth) e.depth++;
      other = 0;
      lastChapter = number;
    } else {
      group = 0;
      other = e.depth;
    }
  }
}

bool Fb2ToEpub::writeOpf(Output& out) const {
  PackageWriter w(out, "OEBPS/content.opf");
  w.put(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"uid\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:opf=\"http://www.idpf.org/2007/opf\">"
      "<dc:title>");
  w.putEscaped(meta.title);
  w.put("</dc:title>");
  for (const auto& author : meta.authors) {
    w.put("<dc:creator opf:role=\"aut\">");
    w.putEscaped(author);
    w.put("</dc:creator>");
  }
  w.put("<dc:language>");
  w.putEscaped(meta.language.empty() ? std::string("und") : meta.language);
  w.put("</dc:language><dc:identifier id=\"uid\">fb2</dc:identifier>");
  char line[160];
  if (cover) {
    snprintf(line, sizeof(line), "<meta name=\"cover\" content=\"img%u\"/>", static_cast<unsigned>(cover->index));
    w.put(line);
  }
  w.put(
      "</metadata><manifest>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"
      "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>");
  if (coverPage()) {
    w.put("<item id=\"cover\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>");
  }
  for (unsigned i = 1; i <= fileCount; i++) {
    snprintf(line, sizeof(line), "<item id=\"s%u\" href=\"s%u.xhtml\" media-type=\"application/xhtml+xml\"/>", i, i);
    w.put(line);
  }
  for (const auto& entry : binaries) {
    const Binary& b = entry.second;
    if (!b.extension) continue;
    snprintf(line, sizeof(line), "<item id=\"img%u\" href=\"img%u.%s\" media-type=\"%s\"/>",
             static_cast<unsigned>(b.index), static_cast<unsigned>(b.index), b.extension, mediaType(b.extension));
    w.put(line);
  }
  w.put("</manifest><spine toc=\"ncx\">");
  if (coverPage()) w.put("<itemref idref=\"cover\"/>");
  for (unsigned i = 1; i <= fileCount; i++) {
    snprintf(line, sizeof(line), "<itemref idref=\"s%u\"/>", i);
    w.put(line);
  }
  w.put("</spine></package>");
  return w.end();
}

bool Fb2ToEpub::writeNcx(Output& out) const {
  PackageWriter w(out, "OEBPS/toc.ncx");
  w.put(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\">"
      "<head><meta name=\"dtb:uid\" content=\"fb2\"/></head><docTitle><text>");
  w.putEscaped(meta.title);
  w.put("</text></docTitle><navMap>");

  int openDepth = 0;
  unsigned order = 0;
  char line[96];
  const auto navPoint = [&](const uint16_t file, const uint8_t entryDepth, const std::string& anchor,
                            const std::string& label) {
    // A level with no titled parent hangs off the nearest open one.
    int depth = entryDepth < 1 ? 1 : entryDepth;
    if (depth > openDepth + 1) depth = openDepth + 1;
    for (; openDepth >= depth; openDepth--) w.put("</navPoint>");
    order++;
    snprintf(line, sizeof(line), "<navPoint id=\"n%u\" playOrder=\"%u\"><navLabel><text>", order, order);
    w.put(line);
    w.putEscaped(label);
    if (file == 0) {
      w.put("</text></navLabel><content src=\"cover.xhtml");
    } else {
      snprintf(line, sizeof(line), "</text></navLabel><content src=\"s%u.xhtml", static_cast<unsigned>(file));
      w.put(line);
    }
    if (!anchor.empty()) {
      w.put("#");
      w.putEscaped(anchor, true);
    }
    w.put("\"/>");
    openDepth = depth;
  };

  // File 0 is the cover page, titled with the book so the status bar has a name for it.
  const std::string noAnchor;
  const std::string bookLabel = meta.title.empty() ? std::string("1") : meta.title;
  if (coverPage()) navPoint(0, 1, noAnchor, bookLabel);
  // So do the pages before the first bold title: calibre puts the cover and title pages in the body.
  if (toc.empty() || (!boldTitles.empty() && !coverPage() && toc.front().file > 1)) {
    navPoint(1, 1, noAnchor, bookLabel);
  }
  for (const TocEntry& e : toc) navPoint(e.file, e.depth, e.anchor, e.label);
  for (; openDepth > 0; openDepth--) w.put("</navPoint>");
  w.put("</navMap></ncx>");
  return w.end();
}
