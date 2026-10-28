#pragma once

// The save text of the game cores: one flat JSON object of unsigned numbers and
// quote-free strings, so a save stays readable without a JSON library.

#include <cstddef>
#include <cstdint>

namespace games {

// Writes into a caller buffer; `ok` turns false once anything did not fit.
class TextWriter {
 public:
  TextWriter(char* out, size_t cap);

  void number(const char* key, uint32_t value);
  // Opens a string value; put() its characters, then endString().
  void beginString(const char* key);
  void put(char c);
  void endString() { put('"'); }
  // Closes the object; returns the length (terminator excluded), or 0 on overflow.
  size_t finish();

 private:
  void key(const char* name);

  char* out;
  size_t cap;
  size_t len = 0;
  bool ok;
  bool first = true;
};

// Reads the text TextWriter produced. Unknown keys are ignored.
class TextReader {
 public:
  TextReader(const char* text, size_t len) : text(text), len(text != nullptr ? len : 0) {}

  bool number(const char* key, uint32_t& out) const;
  // Points `begin`/`n` at the characters of a string value.
  bool string(const char* key, const char*& begin, size_t& n) const;

 private:
  const char* value(const char* key) const;

  const char* text;
  size_t len;
};

// One hex digit, or -1.
int hexValue(char c);
char hexDigit(int value);

}  // namespace games
