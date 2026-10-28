#include "ScanwordClueLayout.h"

#include <cstring>

#include "ScanwordPuzzle.h"

namespace scanword {
namespace {

constexpr char ELLIPSIS[] = "\xE2\x80\xA6";
constexpr size_t MAX_WORD_LETTERS = 48;

bool isVowel(const uint8_t letter) {
  switch (letter) {
    case 1:   // А
    case 6:   // Е
    case 7:   // Ё
    case 10:  // И
    case 16:  // О
    case 21:  // У
    case 29:  // Ы
    case 31:  // Э
    case 32:  // Ю
    case 33:  // Я
      return true;
    default:
      return false;
  }
}

// A part starts on a consonant other than Ь, Ъ, Й (those stay with the letter before).
bool startsPart(const uint8_t letter) { return letter != 30 && letter != 28 && letter != 11 && !isVowel(letter); }

// Л, М, Н, Р.
bool isSonorant(const uint8_t letter) { return letter == 13 || letter == 14 || letter == 15 || letter == 18; }

// Break before letters[split] (a consonant that starts a part): after a vowel or Ь/Ъ/Й ("Боже-ство",
// "Свой-ство"), between two single consonants ("Кош-ка"), after Л/М/Н/Р closing a syllable
// ("Отвер-стие"), or before "ств" ("Цар-ство"); never inside "ств" ("Божес-тво", "Свойст-во").
bool goodBreak(const uint8_t* letters, const size_t count, const size_t split) {
  const uint8_t before = letters[split - 1];
  if (!startsPart(before)) return true;
  if (split + 2 < count && letters[split] == 19 && letters[split + 1] == 20 && letters[split + 2] == 3) return true;
  if (!isVowel(letters[split - 2])) return false;
  return isVowel(letters[split + 1]) || isSonorant(before);
}

uint8_t letterAt(const char* s, const size_t len) {
  const auto c = static_cast<uint8_t>(s[0]);
  if ((c == 0xD0 || c == 0xD1) && len >= 2) {
    return letterFromCodepoint(((c & 0x1Fu) << 6) | (static_cast<uint8_t>(s[1]) & 0x3Fu));
  }
  return NO_LETTER;
}

size_t codepointLength(const char* s, const size_t len) {
  size_t n = 1;
  while (n < len && (static_cast<uint8_t>(s[n]) & 0xC0) == 0x80) ++n;
  return n;
}

class Builder {
 public:
  Builder(const int maxWidth, const uint8_t maxLines, const MeasureFn measure, void* ctx, ClueLines& out)
      : maxWidth(maxWidth), maxLines(maxLines), measure(measure), ctx(ctx), out(out) {}

  void word(const char* w, size_t n) {
    if (fitsAfterCurrent(w, n, "")) {
      append(w, n, "");
      return;
    }
    if (curLen > 0 && fitsAlone(w, n)) {
      flush();
      append(w, n, "");
      return;
    }
    while (n > 0) {
      if (fitsAfterCurrent(w, n, "")) {
        append(w, n, "");
        return;
      }
      size_t cut = hyphenBreak(w, n);
      if (cut == 0 && curLen > 0) {
        flush();
        continue;
      }
      if (cut == 0) cut = forcedBreak(w, n);
      append(w, cut, "-");
      flush();
      w += cut;
      n -= cut;
    }
  }

  void finish() {
    if (curLen > 0) flush();
    if (produced <= maxLines) return;
    // Mark the cut with an ellipsis on the last kept line.
    char* line = out.line[maxLines - 1];
    size_t len = strlen(line);
    char probe[CLUE_LINE_BYTES + sizeof(ELLIPSIS)];
    while (true) {
      while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '-')) --len;
      memcpy(probe, line, len);
      memcpy(probe + len, ELLIPSIS, sizeof(ELLIPSIS));
      if (len == 0 || (measure(ctx, probe) <= maxWidth && len + sizeof(ELLIPSIS) <= CLUE_LINE_BYTES)) break;
      do --len;
      while (len > 0 && (static_cast<uint8_t>(line[len]) & 0xC0) == 0x80);
    }
    memcpy(line, probe, len + sizeof(ELLIPSIS));
  }

 private:
  // Current line + space + w[0..n) + suffix, if it fits the buffer.
  bool compose(const char* w, const size_t n, const char* suffix, char* buf) const {
    const size_t suffixLen = strlen(suffix);
    const size_t sep = curLen > 0 ? 1 : 0;
    if (curLen + sep + n + suffixLen + 1 > CLUE_LINE_BYTES) return false;
    memcpy(buf, cur, curLen);
    if (sep) buf[curLen] = ' ';
    memcpy(buf + curLen + sep, w, n);
    memcpy(buf + curLen + sep + n, suffix, suffixLen + 1);
    return true;
  }

  bool fitsAfterCurrent(const char* w, const size_t n, const char* suffix) const {
    char buf[CLUE_LINE_BYTES];
    return compose(w, n, suffix, buf) && measure(ctx, buf) <= maxWidth;
  }

  bool fitsAlone(const char* w, const size_t n) const {
    if (n + 1 > CLUE_LINE_BYTES) return false;
    char buf[CLUE_LINE_BYTES];
    memcpy(buf, w, n);
    buf[n] = '\0';
    return measure(ctx, buf) <= maxWidth;
  }

  void append(const char* w, const size_t n, const char* suffix) {
    char buf[CLUE_LINE_BYTES];
    if (!compose(w, n, suffix, buf)) return;
    curLen = strlen(buf);
    memcpy(cur, buf, curLen + 1);
  }

  void flush() {
    if (produced < maxLines) memcpy(out.line[produced], cur, curLen + 1);
    ++produced;
    out.count = produced < maxLines ? produced : maxLines;
    curLen = 0;
    cur[0] = '\0';
  }

  // Byte length of the longest prefix that may end a line with a hyphen; 0 if none fits.
  size_t hyphenBreak(const char* w, const size_t n) const {
    if (n > UINT8_MAX) return 0;  // byte offsets below fit uint8_t
    uint8_t offsets[MAX_WORD_LETTERS + 1];
    uint8_t letters[MAX_WORD_LETTERS];
    size_t count = 0;
    for (size_t i = 0; i < n && count < MAX_WORD_LETTERS; i += codepointLength(w + i, n - i)) {
      offsets[count] = static_cast<uint8_t>(i);
      letters[count++] = letterAt(w + i, n - i);
    }
    offsets[count] = static_cast<uint8_t>(n);
    for (size_t split = count >= 2 ? count - 2 : 0; split >= 2; --split) {
      if (!startsPart(letters[split]) || !goodBreak(letters, count, split)) continue;
      bool headVowel = false;
      bool tailVowel = false;
      for (size_t k = 0; k < count; ++k) (k < split ? headVowel : tailVowel) |= isVowel(letters[k]);
      if (headVowel && tailVowel && fitsAfterCurrent(w, offsets[split], "-")) return offsets[split];
    }
    return 0;
  }

  // Longest prefix of at least one character that fits an empty line with a hyphen.
  size_t forcedBreak(const char* w, const size_t n) const {
    size_t best = codepointLength(w, n);
    for (size_t i = best; i < n; i += codepointLength(w + i, n - i)) {
      if (!fitsAfterCurrent(w, i, "-")) break;
      best = i;
    }
    return best;
  }

  int maxWidth;
  uint8_t maxLines;
  MeasureFn measure;
  void* ctx;
  ClueLines& out;
  uint8_t produced = 0;
  size_t curLen = 0;
  char cur[CLUE_LINE_BYTES] = {};
};

}  // namespace

void layoutClue(const char* text, const int maxWidth, uint8_t maxLines, const MeasureFn measure, void* ctx,
                ClueLines& out) {
  out.count = 0;
  if (maxLines == 0 || text == nullptr) return;
  if (maxLines > CLUE_MAX_LINES) maxLines = CLUE_MAX_LINES;
  Builder builder(maxWidth, maxLines, measure, ctx, out);
  const char* p = text;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    const char* start = p;
    while (*p != '\0' && *p != ' ') ++p;
    if (p > start) builder.word(start, static_cast<size_t>(p - start));
  }
  builder.finish();
}

}  // namespace scanword
