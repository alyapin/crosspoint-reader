#pragma once

#include <cstddef>
#include <cstdint>

// HTTP/1.1 primitives for the LocalSend receiver. Arduino-free so the host tests
// drive them with plain strings.
//
// Ported from bigbag/papyrix-reader (MIT, (c) 2026 Pavel Liashkov),
// lib/Localsend/src/LocalsendHttp.{h,cpp} at a7d15e0, with a non-exact body
// limit for JSON bodies and the extra status codes the receiver sends.

namespace localsend {

enum class Route : uint8_t { Unknown, Register, PrepareUpload, Upload, Cancel, Info };

// Matches method + request target against the v2 endpoints. Matching is exact on
// the path; the query string is ignored.
Route matchRoute(const char* method, const char* target);

// Splits "METHOD SP request-target SP HTTP/x.y" into the caller's buffers.
// Returns false on a malformed line or a token that does not fit.
bool parseRequestLine(const char* line, char* method, size_t methodCap, char* target, size_t targetCap);

enum class HeaderFeed : uint8_t { More, Done, Overflow };

// Consumes one header line (without CRLF) at a time: Done on the blank line,
// Overflow past MAX_LINES. Conflicting or repeated framing headers set invalid.
class HeaderParser {
 public:
  static constexpr int MAX_LINES = 32;

  uint64_t contentLength = 0;
  bool hasContentLength = false;
  bool chunked = false;
  bool invalid = false;

  HeaderFeed feed(const char* line);

 private:
  int lines = 0;
  bool hasTransferEncoding = false;
};

enum class ChunkFeed : uint8_t { NeedInput, Complete, Invalid };

// Incremental HTTP chunked-body decoder. Decoded bytes never exceed `limit`;
// with `exact`, the body must decode to exactly `limit` bytes. Output may alias
// input: decoded bytes are never written ahead of the bytes consumed.
class ChunkDecoder {
 public:
  ChunkDecoder() = default;
  ChunkDecoder(uint64_t limit, bool exact) : limit(limit), exact(exact) {}

  ChunkFeed feed(const uint8_t* input, size_t inputLen, size_t* inputUsed, uint8_t* output, size_t outputCap,
                 size_t* outputLen);

 private:
  enum class State : uint8_t { Line, Data, DataCR, DataLF, Trailers, Done, Failed };
  State state = State::Line;
  uint64_t limit = 0;
  bool exact = false;
  uint64_t decoded = 0;
  uint64_t chunkRemaining = 0;
  char line[128] = {};
  size_t lineLen = 0;
  int trailerLines = 0;
  bool sawCR = false;
};

const char* reasonPhrase(int code);

// Copies the percent-decoded value of query parameter `key` out of `target`.
// '+' decodes to a space (form encoding). Returns false when absent, too long,
// or carrying a malformed or NUL escape.
bool queryParam(const char* target, const char* key, char* out, size_t cap);

}  // namespace localsend
