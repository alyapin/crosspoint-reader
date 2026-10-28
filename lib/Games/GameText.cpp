#include "GameText.h"

#include <cstring>

namespace games {

TextWriter::TextWriter(char* out, const size_t cap) : out(out), cap(out != nullptr ? cap : 0), ok(cap > 0) { put('{'); }

void TextWriter::put(const char c) {
  // One byte stays reserved for the terminator.
  if (len + 1 < cap) {
    out[len++] = c;
  } else {
    ok = false;
  }
}

void TextWriter::key(const char* name) {
  if (!first) put(',');
  first = false;
  put('"');
  while (*name != '\0') put(*name++);
  put('"');
  put(':');
}

void TextWriter::number(const char* name, uint32_t value) {
  key(name);
  char digits[10];
  int n = 0;
  do {
    digits[n++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (n > 0) put(digits[--n]);
}

void TextWriter::beginString(const char* name) {
  key(name);
  put('"');
}

size_t TextWriter::finish() {
  put('}');
  if (!ok) return 0;
  out[len] = '\0';
  return len;
}

const char* TextReader::value(const char* key) const {
  const size_t keyLen = strlen(key);
  for (size_t i = 0; i + keyLen + 2 <= len; ++i) {
    if (text[i] != '"' || strncmp(text + i + 1, key, keyLen) != 0 || text[i + 1 + keyLen] != '"') continue;
    size_t j = i + keyLen + 2;
    while (j < len && text[j] == ' ') ++j;
    if (j >= len || text[j] != ':') continue;
    ++j;
    while (j < len && text[j] == ' ') ++j;
    return j < len ? text + j : nullptr;
  }
  return nullptr;
}

bool TextReader::number(const char* key, uint32_t& out) const {
  const char* p = value(key);
  if (p == nullptr || *p < '0' || *p > '9') return false;
  uint64_t v = 0;
  while (p < text + len && *p >= '0' && *p <= '9') {
    v = v * 10 + static_cast<uint64_t>(*p - '0');
    if (v > UINT32_MAX) return false;
    ++p;
  }
  if (p < text + len && *p != ',' && *p != '}' && *p != ' ') return false;
  out = static_cast<uint32_t>(v);
  return true;
}

bool TextReader::string(const char* key, const char*& begin, size_t& n) const {
  const char* p = value(key);
  if (p == nullptr || *p != '"') return false;
  begin = ++p;
  while (p < text + len && *p != '"') {
    if (*p == '\\') return false;
    ++p;
  }
  if (p >= text + len) return false;
  n = static_cast<size_t>(p - begin);
  return true;
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

char hexDigit(const int value) { return "0123456789abcdef"[value & 0xF]; }

}  // namespace games
