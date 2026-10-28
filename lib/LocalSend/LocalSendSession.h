#pragma once

#include <StreamingJsonParser.h>

#include <cstddef>
#include <cstdint>

// LocalSend protocol v2 receive core: device identity documents, the
// prepare-upload parser, and the upload session state machine. No I/O, so the
// host tests drive it directly.
//
// Every sender-supplied file name goes through sanitizeFileName() before it can
// reach a path; only book formats the reader opens are accepted.

namespace localsend {

constexpr uint16_t DEFAULT_PORT = 53317;
constexpr size_t MAX_FILES = 16;  // entries kept per request, accepted or rejected
constexpr size_t ID_MAX = 64;
constexpr size_t TOKEN_LEN = 16;
constexpr size_t FILE_NAME_MAX = 120;  // bytes of a sanitized file name
constexpr size_t ALIAS_MAX = 48;       // bytes of the sender alias kept for display
constexpr size_t PIN_MAX = 8;
constexpr uint64_t MAX_FILE_BYTES = 256ull << 20;
constexpr uint64_t FREE_SPACE_RESERVE = 1ull << 20;  // left free for caches and metadata
constexpr uint32_t DECISION_TIMEOUT_MS = 120000;
constexpr uint32_t SESSION_IDLE_MS = 60000;
constexpr uint8_t PIN_MAX_FAILURES = 3;
constexpr uint32_t PIN_LOCKOUT_MS = 30000;

struct Identity {
  const char* alias = "";        // UTF-8
  const char* deviceModel = "";  // UTF-8
  const char* fingerprint = "";  // [0-9a-f]
  uint16_t port = DEFAULT_PORT;
};

// Response body of /info and /register. Returns its length, or 0 when it does not fit.
size_t writeInfoJson(const Identity& id, char* out, size_t cap);
// Multicast announcement (announce = true) or reply (announce = false).
size_t writeAnnounceJson(const Identity& id, bool announce, char* out, size_t cap);
// True for another device's multicast announcement that asks for a reply.
bool datagramWantsReply(const char* data, size_t len, const char* ownFingerprint);

// Turns a sender-supplied name into a single safe path component: takes the
// base name, drops control, FAT-reserved and invalid UTF-8 bytes, trims leading
// and trailing dots and spaces, and truncates to FILE_NAME_MAX bytes keeping the
// extension. Returns false when nothing usable remains.
bool sanitizeFileName(const char* in, char* out, size_t cap);
// index 0 copies the name; index N inserts " (N)" before the extension.
bool makeCollisionName(const char* name, int index, char* out, size_t cap);
// The formats the library indexes: epub, fb2, fb2.zip, txt, md, xtc, xtch.
bool isAcceptedBookName(const char* name);

enum class FileState : uint8_t { Rejected, Accepted, Receiving, Done, Failed };
enum class Reject : uint8_t { None, Format, TooLarge, Empty, BadName, NoSpace, Duplicate, Declined };

struct FileEntry {
  char id[ID_MAX + 1] = {};
  char name[FILE_NAME_MAX + 1] = {};  // sanitized
  char token[TOKEN_LEN + 1] = {};
  uint8_t sha256[32] = {};
  bool hasSha256 = false;
  uint64_t size = 0;
  uint64_t received = 0;
  FileState state = FileState::Rejected;
  Reject reason = Reject::None;
};

enum class Phase : uint8_t {
  Idle,              // no request yet
  Preparing,         // a prepare-upload body is being parsed
  AwaitingDecision,  // parsed; the screen must accept or decline
  Transfer,          // accepted; uploads expected
  Finished,          // every accepted file stored or failed, or the request was declined
};

class Session {
 public:
  using Rng = uint32_t (*)();

  Session();

  // pin: empty for none. autoAccept skips AwaitingDecision.
  void configure(const char* pin, bool autoAccept, Rng rng);
  void reset();

  // prepare-upload, step 1 (headers seen): 0 to read the body, else the HTTP status.
  int checkPrepare(const char* clientIp, const char* pinParam, uint32_t nowMs);
  // Step 2: body bytes as they arrive.
  void feedPrepare(const char* data, size_t len);
  // Step 3: body complete. freeBytes = UINT64_MAX when unknown. Returns 0 when
  // the screen must decide, otherwise the HTTP status to send (200 = accepted).
  int finishPrepare(uint64_t freeBytes, uint32_t nowMs);
  // The prepare connection failed before the decision was answered.
  void abortPrepare();
  void decide(bool accept, uint32_t nowMs);
  // Prepare-upload 200 body. Returns its length, or 0 when it does not fit.
  size_t writePrepareResponse(char* out, size_t cap) const;

  // Upload: 0 and the entry index when the parameters match, else the HTTP status.
  int beginUpload(const char* sessionId, const char* fileId, const char* token, const char* clientIp, uint32_t nowMs,
                  size_t* index);
  void uploadProgress(size_t index, uint64_t received, uint32_t nowMs);
  void endUpload(size_t index, bool stored, uint32_t nowMs);
  bool cancel(const char* sessionId, const char* clientIp);
  // Applies the decision and idle timeouts.
  void tick(uint32_t nowMs);

  Phase phase() const { return phaseValue; }
  // Bumped on every change the screen shows.
  uint32_t revision() const { return revisionValue; }
  const char* senderAlias() const { return alias; }
  size_t entryCount() const { return count; }
  // Files in the request beyond MAX_FILES; they are not accepted.
  size_t droppedCount() const { return dropped; }
  const FileEntry& entry(size_t i) const { return entries[i]; }
  bool pinLocked(uint32_t nowMs) const;

 private:
  enum class Ctx : uint8_t { Root, Info, Files, File, Other };
  static constexpr size_t MAX_DEPTH = 8;

  StreamingJsonParser parser;
  Rng rng = nullptr;
  char pin[PIN_MAX + 1] = {};
  bool autoAccept = false;

  Phase phaseValue = Phase::Idle;
  uint32_t revisionValue = 0;
  uint32_t lastActivityMs = 0;
  char sessionId[TOKEN_LEN + 1] = {};
  char senderIp[46] = {};  // INET6_ADDRSTRLEN
  char alias[ALIAS_MAX + 1] = {};
  FileEntry entries[MAX_FILES];
  size_t count = 0;
  size_t dropped = 0;
  uint8_t pinFailures = 0;
  uint32_t pinLockedAtMs = 0;
  bool pinLockActive = false;

  // JSON walk state.
  Ctx stack[MAX_DEPTH] = {};
  size_t depth = 0;
  char key[ID_MAX + 1] = {};
  bool keyValid = false;
  bool sawFiles = false;
  bool sawRoot = false;
  bool fileHasName = false;
  bool fileHasSize = false;
  bool fileBad = false;
  FileEntry* current = nullptr;

  static void onKey(void* ctx, const char* key, size_t len);
  static void onString(void* ctx, const char* value, size_t len);
  static void onNumber(void* ctx, const char* value, size_t len);
  static void onOther(void* ctx);
  static void onBoolValue(void* ctx, bool value);
  static void onObjectStart(void* ctx);
  static void onObjectEnd(void* ctx);
  static void onArrayStart(void* ctx);
  static void onArrayEnd(void* ctx);

  void push(Ctx ctx);
  // Containers nested past MAX_DEPTH are never interpreted.
  Ctx top() const { return depth == 0 || depth > MAX_DEPTH ? Ctx::Other : stack[depth - 1]; }
  void finishFile();
  void validate(uint64_t freeBytes);
  void finishSession();
  void makeToken(char* out);
  void touch(uint32_t nowMs);
};

}  // namespace localsend
