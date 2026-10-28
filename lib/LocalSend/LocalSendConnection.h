#pragma once

#include <cstddef>
#include <cstdint>

#include "LocalSendHttp.h"
#include "LocalSendSession.h"

// One HTTP/1.1 exchange with a LocalSend sender, driven by poll() without
// blocking: the screen keeps rendering while a file streams to storage, and a
// prepare-upload stays open while the screen asks the user. Every response
// closes the connection.

namespace localsend {

// One accepted TCP connection.
class Io {
 public:
  // Copies received bytes into buf: the count, 0 when none are pending, or -1
  // once the peer has closed.
  virtual int read(uint8_t* buf, size_t cap) = 0;
  // Sends all of data; false when the connection failed.
  virtual bool write(const uint8_t* data, size_t len) = 0;

 protected:
  ~Io() = default;
};

// Where accepted files go.
class FileStore {
 public:
  // Free bytes on the card, or UINT64_MAX when unknown.
  virtual uint64_t freeBytes() = 0;
  virtual bool open(const FileEntry& entry) = 0;
  virtual bool write(const uint8_t* data, size_t len) = 0;
  // After the last byte: 0 when stored, else the HTTP status (422 checksum
  // mismatch, 500 storage failure). The partial file is removed unless 0.
  virtual int commit(const FileEntry& entry) = 0;
  virtual void abort() = 0;

 protected:
  ~FileStore() = default;
};

constexpr size_t IO_BUFFER_SIZE = 4096;
constexpr size_t HEAD_LINE_MAX = 512;
constexpr size_t TARGET_MAX = 384;  // longest upload target with a percent-encoded 64-byte id
constexpr uint64_t JSON_BODY_MAX = 64 * 1024;
constexpr uint64_t SMALL_BODY_MAX = 4096;
constexpr uint32_t HEAD_TIMEOUT_MS = 10000;
constexpr uint32_t REQUEST_TIMEOUT_MS = 30000;  // whole request, except file uploads
constexpr uint32_t STALL_TIMEOUT_MS = 15000;

class Connection {
 public:
  Connection(Session& session, const Identity& identity, FileStore& store);
  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;

  void begin(Io& io, const char* clientIp, uint32_t nowMs);
  // Advances the exchange, reading at most maxBytes. Returns false once it is
  // over; the caller then closes the socket.
  bool poll(uint32_t nowMs, size_t maxBytes);
  // Ends the exchange without a response, dropping any partial file.
  void abort();
  bool active() const { return state != State::Idle; }
  // A prepare-upload waits for the user's decision; no bytes are moving.
  bool holding() const { return state == State::Hold; }

 private:
  enum class State : uint8_t { Idle, Head, Body, Hold };
  enum class Sink : uint8_t { Drain, Json, File };

  Session& session;
  const Identity& identity;
  FileStore& store;
  Io* io = nullptr;

  State state = State::Idle;
  Sink sink = Sink::Drain;
  Route route = Route::Unknown;
  int pendingStatus = 0;  // non-zero: answer with it once the body is drained
  bool requestLineSeen = false;
  bool chunked = false;
  bool fileOpen = false;
  uint64_t bodyRemaining = 0;  // Content-Length framing
  uint64_t bodyReceived = 0;
  size_t fileIndex = 0;
  uint32_t startedMs = 0;
  uint32_t lastByteMs = 0;
  char clientIp[46] = {};
  char method[8] = {};
  char target[TARGET_MAX] = {};
  char line[HEAD_LINE_MAX] = {};
  size_t lineLen = 0;
  HeaderParser headers;
  ChunkDecoder decoder;
  uint8_t buf[IO_BUFFER_SIZE] = {};
  size_t bufLen = 0;
  size_t bufPos = 0;

  void consumeHead(uint32_t nowMs);
  void dispatch(uint32_t nowMs);
  void startBody(Sink kind, uint64_t limit, bool exact, uint32_t nowMs);
  void consumeBody(uint32_t nowMs);
  void deliver(const uint8_t* data, size_t len, uint32_t nowMs);
  void bodyComplete(uint32_t nowMs);
  void fail(int status, uint32_t nowMs);
  void dropPartial(uint32_t nowMs);
  void respond(int status, const char* body = nullptr, size_t len = 0);
  void respondInfo();
  void finish();
};

}  // namespace localsend
