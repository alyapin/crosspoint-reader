#include "LocalSendSession.h"

#include <FsHelpers.h>
#include <strings.h>

#include <cstdio>
#include <cstring>

namespace localsend {

namespace {
constexpr char PROTOCOL_VERSION[] = "2.0";
constexpr char DEVICE_TYPE[] = "mobile";
constexpr char HEX_DIGITS[] = "0123456789abcdef";
constexpr char FB2_ZIP[] = ".fb2.zip";

// Appends s as JSON string content. Returns false when out runs out of room.
bool appendEscaped(char* out, const size_t cap, size_t* pos, const char* s) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; p++) {
    char esc[7];
    size_t n = 0;
    if (*p == '"' || *p == '\\') {
      esc[0] = '\\';
      esc[1] = static_cast<char>(*p);
      n = 2;
    } else if (*p < 0x20) {
      n = static_cast<size_t>(snprintf(esc, sizeof(esc), "\\u%04x", *p));
    } else {
      esc[0] = static_cast<char>(*p);
      n = 1;
    }
    if (*pos + n >= cap) return false;
    memcpy(out + *pos, esc, n);
    *pos += n;
  }
  out[*pos] = 0;
  return true;
}

bool appendRaw(char* out, const size_t cap, size_t* pos, const char* s) {
  const size_t n = strlen(s);
  if (*pos + n >= cap) return false;
  memcpy(out + *pos, s, n + 1);
  *pos += n;
  return true;
}

size_t writeIdentity(const Identity& id, const char* tail, char* out, const size_t cap) {
  if (!out || cap == 0) return 0;
  size_t pos = 0;
  const bool ok = appendRaw(out, cap, &pos, "{\"alias\":\"") && appendEscaped(out, cap, &pos, id.alias) &&
                  appendRaw(out, cap, &pos, "\",\"version\":\"") && appendRaw(out, cap, &pos, PROTOCOL_VERSION) &&
                  appendRaw(out, cap, &pos, "\",\"deviceModel\":\"") && appendEscaped(out, cap, &pos, id.deviceModel) &&
                  appendRaw(out, cap, &pos, "\",\"deviceType\":\"") && appendRaw(out, cap, &pos, DEVICE_TYPE) &&
                  appendRaw(out, cap, &pos, "\",\"fingerprint\":\"") && appendEscaped(out, cap, &pos, id.fingerprint) &&
                  appendRaw(out, cap, &pos, "\"") && appendRaw(out, cap, &pos, tail);
  return ok ? pos : 0;
}

// Length of the valid UTF-8 sequence at p (at most avail bytes), or 0. Rejects
// overlong forms, surrogates and code points past U+10FFFF.
size_t utf8SequenceLength(const unsigned char* p, const size_t avail) {
  const unsigned char c = p[0];
  if (c < 0x80) return 1;
  size_t len = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    len = 2;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    cp = c & 0x07;
  } else {
    return 0;
  }
  if (len > avail) return 0;
  for (size_t i = 1; i < len; i++) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  static constexpr uint32_t MIN_CP[] = {0, 0, 0x80, 0x800, 0x10000};
  if (cp < MIN_CP[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return len;
}

bool isUnsafeAscii(const unsigned char c) { return c < 0x20 || c == 0x7F || strchr("\"*/:<>?\\|", c) != nullptr; }

// Start of the extension kept through truncation and collision suffixes:
// ".fb2.zip", or a short ASCII suffix after the last dot. len when there is none.
size_t extensionStart(const char* s, const size_t len) {
  constexpr size_t FB2_ZIP_LEN = sizeof(FB2_ZIP) - 1;
  if (len > FB2_ZIP_LEN && FsHelpers::checkFileExtension(std::string_view(s, len), FB2_ZIP)) {
    return len - FB2_ZIP_LEN;
  }
  for (size_t i = len; i > 1; i--) {
    const char c = s[i - 1];
    if (c == '.') return (len - (i - 1) <= 16) ? i - 1 : len;
    const bool alnum = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (!alnum) return len;
  }
  return len;
}

// Copies the safe code points of [s, s + len) into out, at most room bytes.
size_t copySafe(const char* s, const size_t len, char* out, const size_t room) {
  size_t n = 0;
  size_t i = 0;
  while (i < len) {
    const auto* p = reinterpret_cast<const unsigned char*>(s + i);
    const size_t seq = utf8SequenceLength(p, len - i);
    if (seq == 0 || (seq == 1 && isUnsafeAscii(*p))) {
      i++;
      continue;
    }
    if (n + seq > room) break;
    memcpy(out + n, s + i, seq);
    n += seq;
    i += seq;
  }
  return n;
}

bool ctEqual(const char* a, const char* b) {
  const size_t la = strlen(a);
  if (la != strlen(b)) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < la; i++) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
  return diff == 0;
}

bool keyIs(const char* key, const char* name) { return strcmp(key, name) == 0; }

bool parseSize(const char* s, const size_t len, uint64_t* out) {
  if (len == 0) return false;
  uint64_t n = 0;
  for (size_t i = 0; i < len; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    const uint8_t digit = static_cast<uint8_t>(s[i] - '0');
    if (n > (UINT64_MAX - digit) / 10) return false;
    n = n * 10 + digit;
  }
  *out = n;
  return true;
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseSha256(const char* s, const size_t len, uint8_t* out) {
  if (len != 64) return false;
  for (size_t i = 0; i < 32; i++) {
    const int hi = hexValue(s[2 * i]);
    const int lo = hexValue(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

bool hasControl(const char* s) {
  for (; *s; s++) {
    if (static_cast<unsigned char>(*s) < 0x20 || *s == 0x7F) return true;
  }
  return false;
}

const char* findKeyValue(const char* data, const size_t len, const char* quotedKey) {
  const size_t keyLen = strlen(quotedKey);
  for (size_t i = 0; i + keyLen <= len; i++) {
    if (memcmp(data + i, quotedKey, keyLen) != 0) continue;
    size_t j = i + keyLen;
    while (j < len && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r' || data[j] == '\n')) j++;
    if (j >= len || data[j] != ':') continue;
    j++;
    while (j < len && (data[j] == ' ' || data[j] == '\t' || data[j] == '\r' || data[j] == '\n')) j++;
    return data + j;
  }
  return nullptr;
}

// Windows opens a device for these names, whatever follows the first dot.
bool isDosDevice(const char* name) {
  size_t len = strcspn(name, ".");
  while (len > 0 && name[len - 1] == ' ') len--;
  if (len == 3) {
    for (const char* device : {"CON", "PRN", "AUX", "NUL"}) {
      if (strncasecmp(name, device, 3) == 0) return true;
    }
    return false;
  }
  return len == 4 && (strncasecmp(name, "COM", 3) == 0 || strncasecmp(name, "LPT", 3) == 0) && name[3] >= '1' &&
         name[3] <= '9';
}
}  // namespace

size_t writeInfoJson(const Identity& id, char* out, const size_t cap) {
  return writeIdentity(id, ",\"download\":false}", out, cap);
}

size_t writeAnnounceJson(const Identity& id, const bool announce, char* out, const size_t cap) {
  char tail[96];
  snprintf(tail, sizeof(tail), ",\"port\":%u,\"protocol\":\"http\",\"download\":false,\"announce\":%s}",
           static_cast<unsigned>(id.port), announce ? "true" : "false");
  return writeIdentity(id, tail, out, cap);
}

bool datagramWantsReply(const char* data, const size_t len, const char* ownFingerprint) {
  if (!data || len == 0) return false;
  const char* end = data + len;
  const char* announce = findKeyValue(data, len, "\"announce\"");
  if (!announce || end - announce < 4 || memcmp(announce, "true", 4) != 0) return false;
  const char* fp = findKeyValue(data, len, "\"fingerprint\"");
  if (fp && ownFingerprint && *fp == '"') {
    const size_t ownLen = strlen(ownFingerprint);
    if (static_cast<size_t>(end - fp) > ownLen + 1 && memcmp(fp + 1, ownFingerprint, ownLen) == 0 &&
        fp[1 + ownLen] == '"') {
      return false;  // our own announcement looped back
    }
  }
  return true;
}

bool sanitizeFileName(const char* in, char* out, const size_t cap) {
  if (!in || !out || cap < 2) return false;
  const char* base = in;
  for (const char* p = in; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  while (*base == '.' || *base == ' ') base++;
  const size_t len = strlen(base);
  const size_t extAt = extensionStart(base, len);

  char ext[24];
  const size_t extLen = copySafe(base + extAt, len - extAt, ext, sizeof(ext) - 1);
  const size_t maxLen = (cap - 1 < FILE_NAME_MAX ? cap - 1 : FILE_NAME_MAX);
  if (extLen >= maxLen) return false;

  size_t n = copySafe(base, extAt, out, maxLen - extLen);
  // Dropped bytes can expose a leading dot, which would hide the book.
  size_t lead = 0;
  while (lead < n && (out[lead] == ' ' || out[lead] == '.')) lead++;
  while (n > lead && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
  if (n == lead) return false;
  n -= lead;
  memmove(out, out + lead, n);
  memcpy(out + n, ext, extLen);
  out[n + extLen] = 0;
  return !isDosDevice(out);
}

bool makeCollisionName(const char* name, const int index, char* out, const size_t cap) {
  if (!name || !out || cap == 0 || index < 0) return false;
  const size_t len = strlen(name);
  if (index == 0) {
    if (len >= cap) return false;
    memcpy(out, name, len + 1);
    return true;
  }
  const size_t extAt = extensionStart(name, len);
  const int n = snprintf(out, cap, "%.*s (%d)%s", static_cast<int>(extAt), name, index, name + extAt);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool isAcceptedBookName(const char* name) {
  const std::string_view n(name);
  return FsHelpers::hasEpubExtension(n) || FsHelpers::hasFb2Extension(n) || FsHelpers::hasTxtExtension(n) ||
         FsHelpers::hasMarkdownExtension(n) || FsHelpers::hasXtcExtension(n) ||
         FsHelpers::checkFileExtension(n, ".tsv") || FsHelpers::checkFileExtension(n, ".csv");
}

Session::Session()
    : parser(JsonCallbacks{this, &Session::onKey, &Session::onString, &Session::onNumber, &Session::onBoolValue,
                           &Session::onOther, &Session::onObjectStart, &Session::onObjectEnd, &Session::onArrayStart,
                           &Session::onArrayEnd}) {}

void Session::configure(const char* newPin, const bool newAutoAccept, const Rng newRng) {
  snprintf(pin, sizeof(pin), "%s", newPin ? newPin : "");
  autoAccept = newAutoAccept;
  rng = newRng;
  pinFailures = 0;
  pinLockActive = false;
  reset();
}

void Session::reset() {
  phaseValue = Phase::Idle;
  count = 0;
  dropped = 0;
  alias[0] = 0;
  sessionId[0] = 0;
  senderIp[0] = 0;
  revisionValue++;
}

bool Session::pinLocked(const uint32_t nowMs) const {
  return pinLockActive && static_cast<uint32_t>(nowMs - pinLockedAtMs) < PIN_LOCKOUT_MS;
}

void Session::touch(const uint32_t nowMs) { lastActivityMs = nowMs; }

int Session::checkPrepare(const char* clientIp, const char* pinParam, const uint32_t nowMs) {
  tick(nowMs);
  if (!clientIp || clientIp[0] == 0 || strlen(clientIp) >= sizeof(senderIp)) return 400;
  if (phaseValue == Phase::Preparing || phaseValue == Phase::AwaitingDecision || phaseValue == Phase::Transfer) {
    return 409;
  }
  if (pin[0]) {
    if (pinLocked(nowMs)) return 429;
    pinLockActive = false;
    if (!pinParam) return 401;  // the sender asks the user, then retries with ?pin=
    if (!ctEqual(pinParam, pin)) {
      if (++pinFailures >= PIN_MAX_FAILURES) {
        pinFailures = 0;
        pinLockActive = true;
        pinLockedAtMs = nowMs;
        revisionValue++;
      }
      return 401;
    }
    pinFailures = 0;
  }

  reset();
  phaseValue = Phase::Preparing;
  strcpy(senderIp, clientIp);
  parser.reset();
  depth = 0;
  keyValid = false;
  sawFiles = false;
  sawRoot = false;
  rootClosed = false;
  trailing = false;
  current = nullptr;
  touch(nowMs);
  return 0;
}

void Session::feedPrepare(const char* data, const size_t len) {
  if (phaseValue != Phase::Preparing) return;
  // A byte at a time: the parser does not report what follows the root object.
  for (size_t i = 0; i < len; i++) {
    if (!rootClosed) {
      parser.feed(data + i, 1);
    } else if (data[i] != ' ' && data[i] != '\t' && data[i] != '\r' && data[i] != '\n') {
      trailing = true;
    }
  }
}

int Session::finishPrepare(const uint64_t freeBytes, const uint32_t nowMs) {
  if (phaseValue != Phase::Preparing) return 500;
  if (parser.hasError() || !sawRoot || !sawFiles || depth != 0 || trailing) {
    reset();
    return 400;
  }
  if (count == 0 && dropped == 0) {
    reset();
    return 204;
  }
  validate(freeBytes);
  bool anyAccepted = false;
  for (size_t i = 0; i < count; i++) anyAccepted = anyAccepted || entries[i].state == FileState::Accepted;
  touch(nowMs);
  revisionValue++;
  if (!anyAccepted) {
    phaseValue = Phase::Finished;
    return 403;
  }
  makeToken(sessionId);
  if (autoAccept) {
    phaseValue = Phase::Transfer;
    return 200;
  }
  phaseValue = Phase::AwaitingDecision;
  return 0;
}

void Session::abortPrepare() {
  if (phaseValue == Phase::Preparing || phaseValue == Phase::AwaitingDecision) reset();
}

void Session::decide(const bool accept, const uint32_t nowMs) {
  if (phaseValue != Phase::AwaitingDecision) return;
  touch(nowMs);
  revisionValue++;
  if (accept) {
    phaseValue = Phase::Transfer;
    return;
  }
  for (size_t i = 0; i < count; i++) {
    if (entries[i].state == FileState::Accepted) {
      entries[i].state = FileState::Rejected;
      entries[i].reason = Reject::Declined;
    }
  }
  phaseValue = Phase::Finished;
}

size_t Session::writePrepareResponse(char* out, const size_t cap) const {
  if (!out || cap == 0 || phaseValue != Phase::Transfer) return 0;
  size_t pos = 0;
  if (!appendRaw(out, cap, &pos, "{\"sessionId\":\"") || !appendRaw(out, cap, &pos, sessionId) ||
      !appendRaw(out, cap, &pos, "\",\"files\":{")) {
    return 0;
  }
  bool first = true;
  for (size_t i = 0; i < count; i++) {
    const FileEntry& e = entries[i];
    if (e.state != FileState::Accepted) continue;
    if (!appendRaw(out, cap, &pos, first ? "\"" : ",\"") || !appendEscaped(out, cap, &pos, e.id) ||
        !appendRaw(out, cap, &pos, "\":\"") || !appendRaw(out, cap, &pos, e.token) ||
        !appendRaw(out, cap, &pos, "\"")) {
      return 0;
    }
    first = false;
  }
  return appendRaw(out, cap, &pos, "}}") ? pos : 0;
}

int Session::beginUpload(const char* sid, const char* fileId, const char* token, const char* clientIp,
                         const uint32_t nowMs, size_t* index) {
  tick(nowMs);
  if (!sid || !fileId || !token || !clientIp || !index) return 400;
  if (phaseValue != Phase::Transfer || strcmp(clientIp, senderIp) != 0 || !ctEqual(sid, sessionId)) return 403;
  for (size_t i = 0; i < count; i++) {
    FileEntry& e = entries[i];
    if (e.state == FileState::Rejected || strcmp(e.id, fileId) != 0) continue;
    if (!ctEqual(token, e.token)) return 403;
    if (e.state != FileState::Accepted) return 409;  // already receiving or finished
    e.state = FileState::Receiving;
    e.received = 0;
    *index = i;
    touch(nowMs);
    revisionValue++;
    return 0;
  }
  return 403;
}

void Session::uploadProgress(const size_t index, const uint64_t received, const uint32_t nowMs) {
  if (index >= count) return;
  FileEntry& e = entries[index];
  const uint64_t before = e.size ? e.received * 100 / e.size : 0;
  e.received = received;
  touch(nowMs);
  if ((e.size ? received * 100 / e.size : 0) != before) revisionValue++;
}

void Session::endUpload(const size_t index, const bool stored, const uint32_t nowMs) {
  if (index >= count || entries[index].state != FileState::Receiving) return;
  entries[index].state = stored ? FileState::Done : FileState::Failed;
  touch(nowMs);
  revisionValue++;
  for (size_t i = 0; i < count; i++) {
    if (entries[i].state == FileState::Accepted || entries[i].state == FileState::Receiving) return;
  }
  phaseValue = Phase::Finished;
}

bool Session::cancel(const char* sid, const char* clientIp) {
  if (phaseValue != Phase::Transfer || !sid || !clientIp || strcmp(clientIp, senderIp) != 0 ||
      !ctEqual(sid, sessionId)) {
    return false;
  }
  finishSession();
  return true;
}

void Session::finishSession() {
  for (size_t i = 0; i < count; i++) {
    if (entries[i].state == FileState::Accepted || entries[i].state == FileState::Receiving) {
      entries[i].state = FileState::Failed;
    }
  }
  phaseValue = Phase::Finished;
  revisionValue++;
}

void Session::tick(const uint32_t nowMs) {
  const uint32_t idle = nowMs - lastActivityMs;
  if (phaseValue == Phase::AwaitingDecision && idle > DECISION_TIMEOUT_MS) {
    decide(false, nowMs);
  } else if (phaseValue == Phase::Transfer && idle > SESSION_IDLE_MS) {
    for (size_t i = 0; i < count; i++) {
      if (entries[i].state == FileState::Receiving) return;  // the connection owns its stall timeout
    }
    finishSession();
  }
}

void Session::makeToken(char* out) {
  uint32_t v = 0;
  for (size_t i = 0; i < TOKEN_LEN; i++) {
    if (i % 8 == 0) v = rng ? rng() : 0;
    out[i] = HEX_DIGITS[v & 0xF];
    v >>= 4;
  }
  out[TOKEN_LEN] = 0;
}

void Session::validate(const uint64_t freeBytes) {
  uint64_t budget = freeBytes;
  if (freeBytes != UINT64_MAX) budget = freeBytes > FREE_SPACE_RESERVE ? freeBytes - FREE_SPACE_RESERVE : 0;
  for (size_t i = 0; i < count; i++) {
    FileEntry& e = entries[i];
    if (e.state != FileState::Accepted) continue;
    Reject reason = Reject::None;
    if (!isAcceptedBookName(e.name)) {
      reason = Reject::Format;
    } else if (e.size == 0) {
      reason = Reject::Empty;
    } else if (e.size > MAX_FILE_BYTES) {
      reason = Reject::TooLarge;
    } else if (e.size > budget) {
      reason = Reject::NoSpace;
    }
    if (reason != Reject::None) {
      e.state = FileState::Rejected;
      e.reason = reason;
      continue;
    }
    budget -= e.size;
    makeToken(e.token);
  }
}

void Session::push(const Ctx ctx) {
  if (depth < MAX_DEPTH) {
    stack[depth] = ctx;
  }
  depth++;
  keyValid = false;
}

void Session::finishFile() {
  if (!current) return;
  FileEntry& e = *current;
  current = nullptr;
  if (fileBad || e.id[0] == 0 || !fileHasName || !fileHasSize) {
    e.state = FileState::Rejected;
    e.reason = Reject::BadName;
  } else {
    for (size_t i = 0; i < count; i++) {
      if (strcmp(entries[i].id, e.id) == 0) {
        e.state = FileState::Rejected;
        e.reason = Reject::Duplicate;
        break;
      }
    }
  }
  count++;
}

void Session::onKey(void* ctx, const char* k, const size_t len) {
  auto* s = static_cast<Session*>(ctx);
  s->keyValid = len <= ID_MAX && strlen(k) == len;
  if (s->keyValid) memcpy(s->key, k, len + 1);
}

void Session::onString(void* ctx, const char* value, const size_t len) {
  auto* s = static_cast<Session*>(ctx);
  const bool valid = s->keyValid && strlen(value) == len;
  s->keyValid = false;
  if (!valid) return;
  if (s->top() == Ctx::Info && keyIs(s->key, "alias")) {
    const size_t n = copySafe(value, len, s->alias, ALIAS_MAX);
    s->alias[n] = 0;
    return;
  }
  if (s->top() != Ctx::File || !s->current) return;
  FileEntry& e = *s->current;
  if (keyIs(s->key, "id")) {
    if (len == 0 || len > ID_MAX || hasControl(value)) {
      s->fileBad = true;
    } else {
      memcpy(e.id, value, len + 1);
    }
  } else if (keyIs(s->key, "fileName")) {
    s->fileHasName = true;
    if (!sanitizeFileName(value, e.name, sizeof(e.name))) s->fileBad = true;
  } else if (keyIs(s->key, "sha256")) {
    e.hasSha256 = parseSha256(value, len, e.sha256);
  }
}

void Session::onNumber(void* ctx, const char* value, const size_t len) {
  auto* s = static_cast<Session*>(ctx);
  const bool sizeKey = s->keyValid && keyIs(s->key, "size");
  s->keyValid = false;
  if (!sizeKey || s->top() != Ctx::File || !s->current) return;
  if (parseSize(value, len, &s->current->size)) {
    s->fileHasSize = true;
  } else {
    s->fileBad = true;
  }
}

void Session::onOther(void* ctx) { static_cast<Session*>(ctx)->keyValid = false; }

void Session::onBoolValue(void* ctx, bool) { static_cast<Session*>(ctx)->keyValid = false; }

void Session::onObjectStart(void* ctx) {
  auto* s = static_cast<Session*>(ctx);
  Ctx next = Ctx::Other;
  if (s->depth == 0) {
    next = Ctx::Root;
    s->sawRoot = true;
  } else if (s->top() == Ctx::Root && s->keyValid) {
    if (keyIs(s->key, "info")) next = Ctx::Info;
    if (keyIs(s->key, "files")) {
      next = Ctx::Files;
      s->sawFiles = true;
    }
  } else if (s->top() == Ctx::Files) {
    next = Ctx::File;
    if (s->count < MAX_FILES) {
      FileEntry& e = s->entries[s->count];
      e = FileEntry{};
      e.state = FileState::Accepted;
      // The map key is the file id unless the object carries its own "id".
      if (s->keyValid) memcpy(e.id, s->key, strlen(s->key) + 1);
      s->current = &e;
      s->fileBad = !s->keyValid || hasControl(e.id);
      s->fileHasName = false;
      s->fileHasSize = false;
    } else {
      s->dropped++;
    }
  }
  s->push(next);
}

void Session::onObjectEnd(void* ctx) {
  auto* s = static_cast<Session*>(ctx);
  if (s->depth == 0) return;
  if (s->top() == Ctx::File) s->finishFile();
  s->depth--;
  s->rootClosed = s->depth == 0;
  s->keyValid = false;
}

void Session::onArrayStart(void* ctx) { static_cast<Session*>(ctx)->push(Ctx::Other); }

void Session::onArrayEnd(void* ctx) {
  auto* s = static_cast<Session*>(ctx);
  if (s->depth > 0) s->depth--;
  s->keyValid = false;
}

}  // namespace localsend
