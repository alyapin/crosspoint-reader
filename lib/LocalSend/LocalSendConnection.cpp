#include "LocalSendConnection.h"

#include <cstdio>
#include <cstring>

namespace localsend {

Connection::Connection(Session& session, const Identity& identity, FileStore& store)
    : session(session), identity(identity), store(store) {}

void Connection::begin(Io& newIo, const char* ip, const uint32_t nowMs) {
  io = &newIo;
  state = State::Head;
  sink = Sink::Drain;
  route = Route::Unknown;
  pendingStatus = 0;
  requestLineSeen = false;
  chunked = false;
  fileOpen = false;
  bodyRemaining = 0;
  bodyReceived = 0;
  startedMs = nowMs;
  lastByteMs = nowMs;
  snprintf(clientIp, sizeof(clientIp), "%s", ip ? ip : "");
  lineLen = 0;
  headers = HeaderParser{};
  decoder = ChunkDecoder{};
  bufLen = 0;
  bufPos = 0;
}

bool Connection::poll(const uint32_t nowMs, const size_t maxBytes) {
  if (state == State::Idle) return false;

  if (state == State::Hold) {
    session.tick(nowMs);
    const Phase phase = session.phase();
    if (phase == Phase::Transfer) {
      const size_t n = session.writePrepareResponse(reinterpret_cast<char*>(buf), sizeof(buf));
      if (n == 0) session.reset();
      n ? respond(200, reinterpret_cast<const char*>(buf), n) : respond(500);
      return false;
    }
    if (phase != Phase::AwaitingDecision) {
      respond(403);  // declined, or the decision timed out
      return false;
    }
    uint8_t scratch[16];
    if (io->read(scratch, sizeof(scratch)) < 0) {
      session.abortPrepare();  // the sender gave up waiting
      finish();
      return false;
    }
    return true;
  }

  size_t taken = 0;
  while (state == State::Head || state == State::Body) {
    if (bufPos == bufLen) {
      if (taken >= maxBytes) break;
      const int n = io->read(buf, sizeof(buf));
      if (n < 0) {
        dropPartial(nowMs);
        finish();
        return false;
      }
      if (n == 0) break;
      bufLen = static_cast<size_t>(n);
      bufPos = 0;
      lastByteMs = nowMs;
      taken += bufLen;
    }
    if (state == State::Head) {
      consumeHead(nowMs);
    } else {
      consumeBody(nowMs);
    }
  }

  // A body that trickles in could hold the only connection slot forever;
  // only a token-checked file upload may take longer than REQUEST_TIMEOUT_MS.
  if ((state == State::Head && nowMs - startedMs > HEAD_TIMEOUT_MS) ||
      (state == State::Body && sink != Sink::File && nowMs - startedMs > REQUEST_TIMEOUT_MS)) {
    dropPartial(nowMs);
    respond(408);
  } else if (state == State::Body && nowMs - lastByteMs > STALL_TIMEOUT_MS) {
    dropPartial(nowMs);
    finish();
  }
  return state != State::Idle;
}

void Connection::abort() {
  if (state == State::Hold) {
    session.abortPrepare();
  } else if (state == State::Body) {
    dropPartial(lastByteMs);
  }
  finish();
}

void Connection::consumeHead(const uint32_t nowMs) {
  while (bufPos < bufLen && state == State::Head) {
    const char c = static_cast<char>(buf[bufPos++]);
    if (c == 0) {
      respond(400);
      return;
    }
    if (c != '\n') {
      if (lineLen + 1 >= sizeof(line)) {
        respond(431);
        return;
      }
      line[lineLen++] = c;
      continue;
    }
    if (lineLen > 0 && line[lineLen - 1] == '\r') lineLen--;
    line[lineLen] = 0;
    lineLen = 0;
    if (!requestLineSeen) {
      if (line[0] == 0) continue;  // RFC 9112 2.2: tolerate empty lines before the request line
      if (!parseRequestLine(line, method, sizeof(method), target, sizeof(target))) {
        respond(400);
        return;
      }
      requestLineSeen = true;
      continue;
    }
    const HeaderFeed fed = headers.feed(line);
    if (fed == HeaderFeed::Overflow) {
      respond(431);
    } else if (fed == HeaderFeed::Done) {
      dispatch(nowMs);
    }
  }
}

void Connection::dispatch(const uint32_t nowMs) {
  if (headers.invalid) {
    respond(400);
    return;
  }
  route = matchRoute(method, target);
  chunked = headers.chunked;
  switch (route) {
    case Route::Unknown:
      respond(404);
      return;
    case Route::Register:
    case Route::Info:
    case Route::Cancel:
      startBody(Sink::Drain, SMALL_BODY_MAX, false, nowMs);
      return;
    case Route::PrepareUpload: {
      char pin[PIN_MAX + 2];
      const bool hasPin = queryParam(target, "pin", pin, sizeof(pin));
      pendingStatus = session.checkPrepare(clientIp, hasPin ? pin : nullptr, nowMs);
      // Drain a rejected body before answering: closing with unread bytes
      // resets the connection and the sender would miss the status.
      startBody(pendingStatus ? Sink::Drain : Sink::Json, JSON_BODY_MAX, false, nowMs);
      return;
    }
    case Route::Upload: {
      char sid[TOKEN_LEN + 2];
      char fileId[ID_MAX + 2];
      char token[TOKEN_LEN + 2];
      if (!queryParam(target, "sessionId", sid, sizeof(sid)) || !queryParam(target, "fileId", fileId, sizeof(fileId)) ||
          !queryParam(target, "token", token, sizeof(token))) {
        respond(400);
        return;
      }
      const int status = session.beginUpload(sid, fileId, token, clientIp, nowMs, &fileIndex);
      if (status != 0) {
        respond(status);
        return;
      }
      const FileEntry& entry = session.entry(fileIndex);
      if (!chunked && (!headers.hasContentLength || headers.contentLength != entry.size)) {
        session.endUpload(fileIndex, false, nowMs);
        respond(400);
        return;
      }
      if (!store.open(entry)) {
        session.endUpload(fileIndex, false, nowMs);
        respond(500);
        return;
      }
      fileOpen = true;
      startBody(Sink::File, entry.size, true, nowMs);
      return;
    }
  }
}

void Connection::startBody(const Sink kind, const uint64_t limit, const bool exact, const uint32_t nowMs) {
  sink = kind;
  bodyReceived = 0;
  state = State::Body;
  if (chunked) {
    decoder = ChunkDecoder(limit, exact);
    return;
  }
  if (headers.contentLength > limit) {
    fail(413, nowMs);
    return;
  }
  bodyRemaining = headers.contentLength;
  if (bodyRemaining == 0) bodyComplete(nowMs);
}

void Connection::consumeBody(const uint32_t nowMs) {
  uint8_t* data = buf + bufPos;
  const size_t avail = bufLen - bufPos;
  if (chunked) {
    size_t used = 0;
    size_t produced = 0;
    const ChunkFeed fed = decoder.feed(data, avail, &used, data, avail, &produced);
    bufPos += used;
    if (fed == ChunkFeed::Invalid || (fed == ChunkFeed::NeedInput && used == 0)) {
      fail(400, nowMs);
      return;
    }
    if (produced > 0) deliver(data, produced, nowMs);
    if (state == State::Body && fed == ChunkFeed::Complete) bodyComplete(nowMs);
    return;
  }
  const size_t take = bodyRemaining < avail ? static_cast<size_t>(bodyRemaining) : avail;
  bufPos += take;
  bodyRemaining -= take;
  if (take > 0) deliver(data, take, nowMs);
  if (state == State::Body && bodyRemaining == 0) bodyComplete(nowMs);
}

void Connection::deliver(const uint8_t* data, const size_t len, const uint32_t nowMs) {
  bodyReceived += len;
  if (sink == Sink::Json) {
    session.feedPrepare(reinterpret_cast<const char*>(data), len);
  } else if (sink == Sink::File) {
    if (!store.write(data, len)) {
      fail(500, nowMs);
      return;
    }
    session.uploadProgress(fileIndex, bodyReceived, nowMs);
  }
}

void Connection::bodyComplete(const uint32_t nowMs) {
  switch (sink) {
    case Sink::Drain:
      if (pendingStatus != 0) {
        respond(pendingStatus);
      } else if (route == Route::Cancel) {
        char sid[TOKEN_LEN + 2];
        if (queryParam(target, "sessionId", sid, sizeof(sid))) session.cancel(sid, clientIp);
        respond(200);
      } else {
        respondInfo();
      }
      return;
    case Sink::Json: {
      const int status = session.finishPrepare(store.freeBytes(), nowMs);
      if (status == 0) {
        state = State::Hold;
      } else if (status == 200) {
        const size_t n = session.writePrepareResponse(reinterpret_cast<char*>(buf), sizeof(buf));
        if (n == 0) session.reset();
        n ? respond(200, reinterpret_cast<const char*>(buf), n) : respond(500);
      } else {
        respond(status);
      }
      return;
    }
    case Sink::File: {
      fileOpen = false;
      const int status = store.commit(session.entry(fileIndex));
      session.endUpload(fileIndex, status == 0, nowMs);
      respond(status == 0 ? 200 : status);
      return;
    }
  }
}

void Connection::fail(const int status, const uint32_t nowMs) {
  dropPartial(nowMs);
  respond(status);
}

void Connection::dropPartial(const uint32_t nowMs) {
  if (sink == Sink::File && fileOpen) {
    fileOpen = false;
    store.abort();
    session.endUpload(fileIndex, false, nowMs);
  } else if (sink == Sink::Json) {
    session.abortPrepare();
  }
}

void Connection::respond(const int status, const char* body, const size_t len) {
  char head[160];
  const int n =
      snprintf(head, sizeof(head), "HTTP/1.1 %d %s\r\n%sContent-Length: %u\r\nConnection: close\r\n\r\n", status,
               reasonPhrase(status), len ? "Content-Type: application/json\r\n" : "", static_cast<unsigned>(len));
  if (n > 0 && io->write(reinterpret_cast<const uint8_t*>(head), static_cast<size_t>(n)) && len > 0) {
    io->write(reinterpret_cast<const uint8_t*>(body), len);
  }
  finish();
}

void Connection::respondInfo() {
  const size_t n = writeInfoJson(identity, reinterpret_cast<char*>(buf), sizeof(buf));
  n ? respond(200, reinterpret_cast<const char*>(buf), n) : respond(500);
}

void Connection::finish() { state = State::Idle; }

}  // namespace localsend
