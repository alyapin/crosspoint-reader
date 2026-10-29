#include "CardDeck.h"

#include <cstdlib>
#include <cstring>

#include "CardWork.h"

namespace cards {
namespace {

// CP1251 0x80..0xBF; 0xC0..0xFF are А..я.
constexpr uint16_t CP1251_HIGH[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457};

struct NamedEntity {
  const char* name;
  uint16_t cp;
};
constexpr NamedEntity ENTITIES[] = {
    {"nbsp", ' '},     {"amp", '&'},      {"lt", '<'},        {"gt", '>'},       {"quot", '"'},     {"apos", '\''},
    {"mdash", 0x2014}, {"ndash", 0x2013}, {"hellip", 0x2026}, {"laquo", 0x00AB}, {"raquo", 0x00BB}, {"lsquo", 0x2018},
    {"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},  {"bdquo", 0x201E}, {"times", 0x00D7}, {"deg", 0x00B0},
    {"copy", 0x00A9},  {"shy", 0},        {"middot", 0x00B7}, {"euro", 0x20AC},  {"rarr", 0x2192},  {"larr", 0x2190},
};

bool endsWith(std::string_view name, const char* ext) {
  const size_t n = strlen(ext);
  if (name.size() <= n) return false;
  for (size_t i = 0; i < n; i++) {
    char c = name[name.size() - n + i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != ext[i]) return false;
  }
  return true;
}

bool startsWith(const uint8_t* p, const size_t len, const char* prefix) {
  const size_t n = strlen(prefix);
  return len >= n && memcmp(p, prefix, n) == 0;
}

size_t skipBom(const uint8_t* p, const size_t len) {
  return len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF ? 3 : 0;
}

// CP1251 when some byte is not UTF-8 and no UTF-8 letter was seen. A sequence
// cut by the end of a full probe buffer is not a broken one.
bool looksCp1251(const uint8_t* p, const size_t len, const bool cutOk) {
  bool multibyte = false;
  bool broken = false;
  size_t i = 0;
  while (i < len) {
    const uint8_t c = p[i];
    size_t extra = 0;
    if (c < 0x80) {
      i++;
      continue;
    } else if (c >= 0xC2 && c <= 0xDF) {
      extra = 1;
    } else if (c >= 0xE0 && c <= 0xEF) {
      extra = 2;
    } else if (c >= 0xF0 && c <= 0xF4) {
      extra = 3;
    }
    bool ok = extra > 0;
    if (ok && i + extra >= len) {
      broken = broken || !cutOk;
      break;
    }
    for (size_t k = 1; ok && k <= extra; k++) ok = (p[i + k] & 0xC0) == 0x80;
    if (ok) {
      multibyte = true;
      i += extra + 1;
    } else {
      broken = true;
      i++;
    }
  }
  return broken && !multibyte;
}

bool sameLower(std::string_view a, const char* b) {
  if (a.size() != strlen(b)) return false;
  for (size_t i = 0; i < a.size(); i++) {
    char c = a[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (c != b[i]) return false;
  }
  return true;
}

bool containsLower(const uint8_t* p, const size_t len, const char* needle) {
  const size_t n = strlen(needle);
  for (size_t i = 0; i + n <= len; i++) {
    if (sameLower(std::string_view(reinterpret_cast<const char*>(p + i), n), needle)) return true;
  }
  return false;
}

// {{c1::answer::hint}}: the front hides each deletion as «[…]», the back shows its answer.
void cloze(char* text, const bool reveal) {
  static constexpr char HIDDEN[] = "[\xE2\x80\xA6]";
  const std::string_view all(text);
  size_t r = 0;
  size_t w = 0;
  while (r < all.size()) {
    if (all.compare(r, 3, "{{c") == 0) {
      const size_t colon = all.find("::", r + 3);
      const size_t close = all.find("}}", r + 3);
      size_t digits = r + 3;
      while (digits < all.size() && all[digits] >= '0' && all[digits] <= '9') digits++;
      if (colon != std::string_view::npos && close != std::string_view::npos && colon < close && digits == colon &&
          digits > r + 3) {
        const size_t from = colon + 2;
        const size_t hint = all.find("::", from);
        const size_t to = hint != std::string_view::npos && hint < close ? hint : close;
        if (reveal) {
          memmove(text + w, text + from, to - from);
          w += to - from;
        } else {
          memcpy(text + w, HIDDEN, sizeof(HIDDEN) - 1);
          w += sizeof(HIDDEN) - 1;
        }
        r = close + 2;
        continue;
      }
    }
    text[w++] = text[r++];
  }
  text[w] = '\0';
}

bool isAlpha(const uint8_t c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAlnum(const uint8_t c) { return isAlpha(c) || (c >= '0' && c <= '9'); }

bool equals(const char* a, const size_t n, const char* b) { return strlen(b) == n && memcmp(a, b, n) == 0; }

std::string_view trimmed(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

int columnNumber(std::string_view value) {
  int n = 0;
  for (const char c : value) {
    if (c < '0' || c > '9') return -1;
    n = n * 10 + (c - '0');
    if (n > 64) return -1;
  }
  return n - 1;
}

void hideColumn(Format& f, const int column) {
  if (column >= 0 && column < 16) f.hiddenColumns |= static_cast<uint16_t>(1u << column);
}

void readHeader(Format& f, std::string_view key, std::string_view value, bool& sawSeparator, bool& sawHtml) {
  key = trimmed(key);
  value = trimmed(value);
  if (key == "separator") {
    sawSeparator = true;
    if (sameLower(value, "tab"))
      f.separator = '\t';
    else if (sameLower(value, "comma"))
      f.separator = ',';
    else if (sameLower(value, "semicolon"))
      f.separator = ';';
    else if (sameLower(value, "pipe"))
      f.separator = '|';
    else if (sameLower(value, "space"))
      f.separator = ' ';
    else if (value.size() == 1)
      f.separator = value[0];
    else
      sawSeparator = false;
  } else if (key == "html") {
    sawHtml = true;
    f.html = sameLower(value, "true");
  } else if (key == "guid column") {
    f.guidColumn = static_cast<int8_t>(columnNumber(value));
    hideColumn(f, f.guidColumn);
  } else if (key == "notetype column" || key == "deck column" || key == "tags column") {
    hideColumn(f, columnNumber(value));
  }
}

char guessSeparator(const uint8_t* p, const size_t len) {
  bool tab = false;
  bool semicolon = false;
  bool quoted = false;
  for (size_t i = 0; i < len && (quoted || p[i] != '\n'); i++) {
    if (p[i] == '"') quoted = !quoted;
    if (quoted) continue;
    tab = tab || p[i] == '\t';
    semicolon = semicolon || p[i] == ';';
  }
  return tab ? '\t' : semicolon ? ';' : ',';
}

void removeSounds(char* text, size_t& len, uint8_t& media) {
  const std::string_view open = "[sound:";
  size_t at;
  while ((at = std::string_view(text, len).find(open)) != std::string_view::npos) {
    const size_t close = std::string_view(text, len).find(']', at);
    if (close == std::string_view::npos) break;
    memmove(text + at, text + close + 1, len - close - 1);
    len -= close + 1 - at;
    media |= HasSound;
  }
}

// Collapses spaces, drops them around line breaks and at the ends.
void normalize(char* text, size_t& len) {
  size_t out = 0;
  bool space = false;
  bool lineBreak = false;
  for (size_t i = 0; i < len; i++) {
    const char c = text[i];
    if (c == ' ') {
      space = true;
    } else if (c == '\n') {
      lineBreak = true;
    } else {
      if (out > 0 && lineBreak)
        text[out++] = '\n';
      else if (out > 0 && space)
        text[out++] = ' ';
      space = lineBreak = false;
      text[out++] = c;
    }
  }
  len = out;
}

}  // namespace

uint32_t fnv1a(const void* data, const size_t len, uint32_t hash) {
  const auto* p = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; i++) {
    hash ^= p[i];
    hash *= 16777619u;
  }
  return hash;
}

bool isCsvName(const std::string_view name) { return endsWith(name, ".csv"); }

bool isTableName(const std::string_view name) { return endsWith(name, ".tsv") || isCsvName(name); }

bool isDeckName(const std::string_view name) { return isTableName(name) || endsWith(name, ".txt"); }

bool hasAnkiHeader(const uint8_t* head, const size_t len) {
  const size_t at = skipBom(head, len);
  return startsWith(head + at, len - at, "#separator:") || startsWith(head + at, len - at, "#html:");
}

Format probe(const uint8_t* head, const size_t len, const bool csv) {
  Format f;
  if (len >= 2 && ((head[0] == 0xFF && head[1] == 0xFE) || (head[0] == 0xFE && head[1] == 0xFF))) {
    f.utf16 = true;
    return f;
  }
  size_t at = skipBom(head, len);
  f.cp1251 = looksCp1251(head + at, len - at, len >= PROBE_BYTES);
  bool sawSeparator = false;
  bool sawHtml = false;
  while (at < len && head[at] == '#') {
    const auto* end = static_cast<const uint8_t*>(memchr(head + at, '\n', len - at));
    if (end == nullptr) break;
    const std::string_view line(reinterpret_cast<const char*>(head + at + 1), static_cast<size_t>(end - head - at - 1));
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) break;
    readHeader(f, line.substr(0, colon), line.substr(colon + 1), sawSeparator, sawHtml);
    at = static_cast<size_t>(end - head) + 1;
  }
  f.dataStart = static_cast<uint32_t>(at);
  if (!sawHtml) {
    const uint8_t* data = head + at;
    const size_t n = len - at;
    f.html = containsLower(data, n, "<br") || containsLower(data, n, "<div") || containsLower(data, n, "&nbsp;") ||
             containsLower(data, n, "</");
  }
  if (!sawSeparator && csv) f.separator = guessSeparator(head + at, len - at);
  return f;
}

uint32_t cardKey(const Fields& fields, const uint32_t guidHash) {
  if (guidHash != 0) return guidHash;
  if (fields.text[0][0] != '\0') return fnv1a(fields.text[0], strlen(fields.text[0]));
  // A front that is only a picture: told apart by its answer.
  return fnv1a(fields.text[1], strlen(fields.text[1]), fnv1a("\1", 1));
}

uint32_t cardAltKey(const Fields& fields) {
  return fnv1a(fields.text[1], strlen(fields.text[1]), fnv1a(fields.text[0], strlen(fields.text[0]), fnv1a("\2", 1)));
}

RecordReader::RecordReader(ByteSource& source, const Format& format, const uint32_t offset)
    : source(source), format(format), pos(offset) {}

int RecordReader::get() {
  const int c = peek();
  if (c >= 0) {
    bufPos++;
    pos++;
  }
  return c;
}

int RecordReader::peek() {
  if (bufPos >= bufLen) {
    if (error) return -1;
    serviceWork(work);
    const int n = source.read(buf, sizeof(buf));
    if (n < 0) error = true;
    if (n <= 0) return -1;
    bufLen = static_cast<uint16_t>(n);
    bufPos = 0;
  }
  return buf[bufPos];
}

bool RecordReader::next(Fields& out, uint32_t& offset, uint32_t& guidHash) {
  fields = &out;
  while (true) {
    for (int i = 0; i < FIELDS; i++) {
      out.text[i][0] = '\0';
      out.media[i] = NoMedia;
    }
    guid = 2166136261u;
    guidLen = 0;
    offset = pos;
    column = 0;
    beginColumn(out);
    bool any = false;
    bool atStart = true;
    bool quoted = false;
    size_t quotedBytes = 0;
    while (true) {
      const int c = get();
      if (c < 0) break;
      any = true;
      if (atStart) {
        atStart = false;
        if (c == '"') {
          quoted = true;
          quotedBytes = 0;
          continue;
        }
      }
      // A quote left open would swallow the rest of the file.
      if (quoted && c == '\n' && quotedBytes > FIELD_MAX) break;
      if (quoted) {
        quotedBytes++;
        if (c == '"') {
          if (peek() == '"') {
            get();
            put('"');
          } else {
            quoted = false;
          }
        } else if (c != '\r') {
          put(static_cast<uint8_t>(c));
        }
        continue;
      }
      if (c == format.separator) {
        endColumn();
        column++;
        beginColumn(out);
        atStart = true;
      } else if (c == '\n') {
        break;
      } else if (c != '\r') {
        put(static_cast<uint8_t>(c));
      }
    }
    endColumn();
    if (error || !any) return false;
    guidHash = guidLen > 0 ? guid : 0;
    bool blank = true;
    for (int i = 0; i < FIELDS && blank; i++) blank = out.text[i][0] == '\0' && out.media[i] == NoMedia;
    if (blank) continue;
    if (strstr(out.text[0], "{{c") != nullptr) {
      memcpy(out.text[2], out.text[1], FIELD_CAP);
      out.media[2] = out.media[1];
      memcpy(out.text[1], out.text[0], FIELD_CAP);
      out.media[1] = out.media[0];
      cloze(out.text[0], false);
      cloze(out.text[1], true);
    }
    return true;
  }
}

void RecordReader::beginColumn(Fields& out) {
  if (column == format.guidColumn) {
    target = -2;
  } else if (column < 16 && (format.hiddenColumns & (1u << column))) {
    target = -1;
  } else {
    int visible = column;
    for (int i = 0; i < column && i < 16; i++) {
      if (format.hiddenColumns & (1u << i)) visible--;
    }
    target = visible < FIELDS ? visible : -1;
  }
  text = target >= 0 ? out.text[target] : nullptr;
  len = 0;
  full = false;
  pendingSpace = pendingBreak = pendingLt = inTag = inEntity = inComment = false;
  commentDashes = 0;
}

void RecordReader::put(const uint8_t b) {
  if (target == -2) {
    guid = fnv1a(&b, 1, guid);
    guidLen++;
    return;
  }
  if (target < 0) return;
  if (format.cp1251 && b >= 0x80) {
    emitCodepoint(b >= 0xC0 ? 0x0410u + (b - 0xC0) : CP1251_HIGH[b - 0x80]);
    return;
  }
  emit(b);
}

void RecordReader::emitCodepoint(const uint32_t cp) {
  if (cp == 0) return;
  if (cp < 0x80) {
    emit(static_cast<uint8_t>(cp));
  } else if (cp < 0x800) {
    emit(static_cast<uint8_t>(0xC0 | (cp >> 6)));
    emit(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    emit(static_cast<uint8_t>(0xE0 | (cp >> 12)));
    emit(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
    emit(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x110000) {
    emit(static_cast<uint8_t>(0xF0 | (cp >> 18)));
    emit(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)));
    emit(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
    emit(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
  }
}

void RecordReader::emit(const uint8_t b) {
  if (inComment) {
    if (b == '>' && commentDashes >= 2) inComment = false;
    commentDashes = b == '-' ? commentDashes + 1 : 0;
    return;
  }
  if (inTag) {
    // "<!--" opens a comment, skipped whole.
    if (commentDashes > 0) {
      if (b == '-' && ++commentDashes == 3) {
        inTag = false;
        inComment = true;
        commentDashes = 0;
        return;
      }
      if (b != '-') commentDashes = 0;
    }
    if (b == '>') {
      inTag = false;
      endTag();
    } else if (tagNameOpen && (isAlnum(b) || (tagLen == 0 && b == '/')) && tagLen < sizeof(tag)) {
      tag[tagLen++] = static_cast<char>(b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b);
    } else {
      tagNameOpen = false;
    }
    return;
  }
  if (inEntity) {
    if (b == ';') {
      inEntity = false;
      endEntity();
      return;
    }
    if (entityLen < sizeof(entity) && (isAlnum(b) || (entityLen == 0 && b == '#'))) {
      entity[entityLen++] = static_cast<char>(b);
      return;
    }
    inEntity = false;
    append('&');
    for (uint8_t i = 0; i < entityLen; i++) append(static_cast<uint8_t>(entity[i]));
  }
  if (pendingLt) {
    pendingLt = false;
    if (isAlpha(b) || b == '/' || b == '!') {
      inTag = true;
      tagNameOpen = b != '!';
      commentDashes = b == '!' ? 1 : 0;
      tagLen = 0;
      if (tagNameOpen) tag[tagLen++] = static_cast<char>(b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b);
      return;
    }
    append('<');
  }
  if (format.html && b == '<') {
    pendingLt = true;
  } else if (format.html && b == '&') {
    inEntity = true;
    entityLen = 0;
  } else {
    append(b);
  }
}

void RecordReader::endTag() {
  const bool closing = tagLen > 0 && tag[0] == '/';
  const char* name = tag + (closing ? 1 : 0);
  const size_t n = tagLen - (closing ? 1 : 0);
  if (equals(name, n, "br") || equals(name, n, "div") || equals(name, n, "p") || equals(name, n, "li") ||
      equals(name, n, "tr")) {
    append('\n');
  } else if (!closing && equals(name, n, "img")) {
    fields->media[target] |= HasImage;
  }
}

void RecordReader::endEntity() {
  uint32_t cp = 0;
  bool known = false;
  if (entityLen > 1 && entity[0] == '#') {
    const bool hex = entity[1] == 'x' || entity[1] == 'X';
    char digits[sizeof(entity) + 1];
    const size_t from = hex ? 2 : 1;
    memcpy(digits, entity + from, entityLen - from);
    digits[entityLen - from] = '\0';
    char* end = nullptr;
    cp = static_cast<uint32_t>(strtoul(digits, &end, hex ? 16 : 10));
    known = end != digits && *end == '\0' && cp < 0x110000;
  } else {
    for (const NamedEntity& e : ENTITIES) {
      if (sameLower(std::string_view(entity, entityLen), e.name)) {
        cp = e.cp;
        known = true;
        break;
      }
    }
  }
  if (!known) {
    append('&');
    for (uint8_t i = 0; i < entityLen; i++) append(static_cast<uint8_t>(entity[i]));
    append(';');
    return;
  }
  // Decoded markup characters are text.
  if (cp == '<' || cp == '&') {
    append(static_cast<uint8_t>(cp));
    return;
  }
  emitCodepoint(cp);
}

void RecordReader::append(const uint8_t b) {
  if (text == nullptr || full) return;
  if (b == ' ' || b == '\t') {
    pendingSpace = len > 0;
    return;
  }
  if (b == '\n') {
    pendingBreak = len > 0;
    return;
  }
  const size_t need = 1 + (pendingSpace || pendingBreak ? 1 : 0);
  if (len + need > FIELD_MAX) {
    full = true;
    return;
  }
  if (pendingBreak)
    text[len++] = '\n';
  else if (pendingSpace)
    text[len++] = ' ';
  pendingSpace = pendingBreak = false;
  text[len++] = static_cast<char>(b);
}

void RecordReader::endColumn() {
  if (text == nullptr) return;
  if (inEntity) {
    inEntity = false;
    append('&');
    for (uint8_t i = 0; i < entityLen; i++) append(static_cast<uint8_t>(entity[i]));
  }
  if (pendingLt) append('<');
  removeSounds(text, len, fields->media[target]);
  normalize(text, len);
  if (full) {
    while (len > 0 && (static_cast<uint8_t>(text[len - 1]) & 0xC0) == 0x80) len--;
    if (len > 0 && static_cast<uint8_t>(text[len - 1]) >= 0xC0) len--;
    memcpy(text + len, "\xE2\x80\xA6", 3);
    len += 3;
  }
  text[len] = '\0';
  text = nullptr;
}

}  // namespace cards
