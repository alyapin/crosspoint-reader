#include <gtest/gtest.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include "LocalSendConnection.h"
#include "LocalSendHttp.h"
#include "LocalSendSession.h"

using namespace localsend;

namespace {

uint32_t counterRng() {
  static uint32_t v = 0x12345678;
  v = v * 1664525u + 1013904223u;
  return v;
}

std::string prepareBody(const std::vector<std::tuple<std::string, std::string, std::string>>& files,
                        const std::string& alias = "Pixel") {
  std::string body = "{\"info\":{\"alias\":\"" + alias +
                     "\",\"version\":\"2.1\",\"deviceType\":\"mobile\",\"fingerprint\":\"abc\","
                     "\"port\":53317,\"protocol\":\"https\",\"download\":false},\"files\":{";
  bool first = true;
  for (const auto& [id, name, size] : files) {
    if (!first) body += ",";
    first = false;
    body += "\"" + id + "\":{\"id\":\"" + id + "\",\"fileName\":\"" + name + "\",\"size\":" + size +
            ",\"fileType\":\"application/epub+zip\",\"sha256\":null,\"preview\":null,"
            "\"metadata\":{\"modified\":\"2026-01-01T00:00:00Z\"}}";
  }
  return body + "}}";
}

std::string sanitized(const char* in) {
  char out[FILE_NAME_MAX + 1];
  return sanitizeFileName(in, out, sizeof(out)) ? std::string(out) : std::string("<rejected>");
}

bool validUtf8(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0 || i + len > s.size()) return false;
    for (size_t k = 1; k < len; k++) {
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    }
    i += len;
  }
  return true;
}

// Prepares a session holding the given files, already accepted.
void acceptedSession(Session& s, const std::string& body, uint32_t now = 1000) {
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, now), 0);
  s.feedPrepare(body.data(), body.size());
  ASSERT_EQ(s.finishPrepare(UINT64_MAX, now), 0);
  s.decide(true, now);
  ASSERT_EQ(s.phase(), Phase::Transfer);
}

class FakeIo final : public Io {
 public:
  std::deque<std::string> input;
  bool closedAfterInput = false;
  std::string output;

  int read(uint8_t* buf, size_t cap) override {
    if (input.empty()) return closedAfterInput ? -1 : 0;
    std::string& front = input.front();
    const size_t n = std::min(cap, front.size());
    memcpy(buf, front.data(), n);
    front.erase(0, n);
    if (front.empty()) input.pop_front();
    return static_cast<int>(n);
  }
  bool write(const uint8_t* data, size_t len) override {
    output.append(reinterpret_cast<const char*>(data), len);
    return true;
  }
  int status() const { return output.size() > 12 ? std::stoi(output.substr(9, 3)) : 0; }
  std::string body() const {
    const size_t at = output.find("\r\n\r\n");
    return at == std::string::npos ? "" : output.substr(at + 4);
  }
};

class FakeStore final : public FileStore {
 public:
  uint64_t free = UINT64_MAX;
  bool failOpen = false;
  bool failWrite = false;
  int commitStatus = 0;
  std::string openedName;
  std::string data;
  int opens = 0;
  int commits = 0;
  int aborts = 0;

  uint64_t freeBytes() override { return free; }
  bool open(const FileEntry& entry) override {
    opens++;
    openedName = entry.name;
    data.clear();
    return !failOpen;
  }
  bool write(const uint8_t* buf, size_t len) override {
    if (failWrite) return false;
    data.append(reinterpret_cast<const char*>(buf), len);
    return true;
  }
  int commit(const FileEntry&) override {
    commits++;
    return commitStatus;
  }
  void abort() override { aborts++; }
};

struct Rig {
  Session session;
  Identity identity;
  FakeStore store;
  Connection conn{session, identity, store};

  Rig() {
    identity.alias = "Сова";
    identity.deviceModel = "Xteink X4";
    identity.fingerprint = "0011aabbccdd";
    session.configure("", false, &counterRng);
  }

  // Runs one exchange to completion (or until it holds) and returns the I/O.
  FakeIo run(std::vector<std::string> chunks, uint32_t now = 1000, bool close = false, const char* ip = "10.0.0.2") {
    FakeIo io;
    for (auto& c : chunks) io.input.push_back(std::move(c));
    io.closedAfterInput = close;
    conn.begin(io, ip, now);
    for (int i = 0; i < 1000 && conn.poll(now, 1 << 20); i++) {
      if (io.input.empty() && !close) break;
    }
    return io;
  }
};

std::string post(const std::string& target, const std::string& body, const std::string& extra = "") {
  return "POST " + target + " HTTP/1.1\r\nHost: x\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + extra +
         "\r\n" + body;
}

std::string chunked(const std::string& body, size_t piece) {
  std::string out;
  for (size_t i = 0; i < body.size(); i += piece) {
    const std::string part = body.substr(i, piece);
    char size[16];
    snprintf(size, sizeof(size), "%zx", part.size());
    out += std::string(size) + "\r\n" + part + "\r\n";
  }
  return out + "0\r\n\r\n";
}

std::string jsonField(const std::string& json, const std::string& key) {
  const std::string q = "\"" + key + "\":\"";
  const size_t at = json.find(q);
  if (at == std::string::npos) return "";
  const size_t end = json.find('"', at + q.size());
  return json.substr(at + q.size(), end - at - q.size());
}

}  // namespace

// ---- HTTP primitives ----

TEST(LocalSendHttp, RequestLine) {
  char method[8], target[64];
  EXPECT_TRUE(
      parseRequestLine("POST /api/localsend/v2/register HTTP/1.1", method, sizeof(method), target, sizeof(target)));
  EXPECT_STREQ(method, "POST");
  EXPECT_STREQ(target, "/api/localsend/v2/register");
  EXPECT_FALSE(parseRequestLine("POST /x", method, sizeof(method), target, sizeof(target)));
  EXPECT_FALSE(parseRequestLine("POST /x FTP/1.0", method, sizeof(method), target, sizeof(target)));
  EXPECT_FALSE(parseRequestLine("VERYLONGMETHOD /x HTTP/1.1", method, sizeof(method), target, sizeof(target)));
  const std::string longTarget = "GET /" + std::string(100, 'a') + " HTTP/1.1";
  EXPECT_FALSE(parseRequestLine(longTarget.c_str(), method, sizeof(method), target, sizeof(target)));
}

TEST(LocalSendHttp, Routes) {
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v2/register"), Route::Register);
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v2/prepare-upload?pin=1234"), Route::PrepareUpload);
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v2/upload?sessionId=a&fileId=b&token=c"), Route::Upload);
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v2/cancel?sessionId=a"), Route::Cancel);
  EXPECT_EQ(matchRoute("GET", "/api/localsend/v2/info"), Route::Info);
  EXPECT_EQ(matchRoute("GET", "/api/localsend/v2/upload"), Route::Unknown);
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v2/uploadx"), Route::Unknown);
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v2/../v2/upload"), Route::Unknown);
  EXPECT_EQ(matchRoute("POST", "/api/localsend/v1/register"), Route::Unknown);
}

TEST(LocalSendHttp, Headers) {
  HeaderParser h;
  EXPECT_EQ(h.feed("Content-Length:  42 "), HeaderFeed::More);
  EXPECT_EQ(h.feed(""), HeaderFeed::Done);
  EXPECT_TRUE(h.hasContentLength);
  EXPECT_EQ(h.contentLength, 42u);
  EXPECT_FALSE(h.invalid);

  HeaderParser te;
  te.feed("transfer-encoding: chunked");
  EXPECT_TRUE(te.chunked);
  EXPECT_FALSE(te.invalid);

  HeaderParser both;
  both.feed("Content-Length: 5");
  both.feed("Transfer-Encoding: chunked");
  EXPECT_TRUE(both.invalid);

  HeaderParser dup;
  dup.feed("Content-Length: 5");
  dup.feed("Content-Length: 5");
  EXPECT_TRUE(dup.invalid);

  HeaderParser bad;
  bad.feed("Content-Length: 5x");
  EXPECT_TRUE(bad.invalid);

  HeaderParser huge;
  huge.feed("Content-Length: 99999999999999999999999");
  EXPECT_TRUE(huge.invalid);

  HeaderParser gzip;
  gzip.feed("Transfer-Encoding: gzip, chunked");
  EXPECT_TRUE(gzip.invalid);

  HeaderParser many;
  for (int i = 0; i < HeaderParser::MAX_LINES; i++) EXPECT_EQ(many.feed("X-A: b"), HeaderFeed::More);
  EXPECT_EQ(many.feed("X-A: b"), HeaderFeed::Overflow);
}

TEST(LocalSendHttp, ChunkDecoderByteByByte) {
  const std::string wire = "5;ext=\"v\"\r\nhello\r\n6\r\n world\r\n0\r\nTrailer: x\r\n\r\n";
  ChunkDecoder d(11, true);
  std::string out;
  ChunkFeed last = ChunkFeed::NeedInput;
  for (char c : wire) {
    uint8_t in = static_cast<uint8_t>(c);
    uint8_t o[4];
    size_t used = 0, produced = 0;
    last = d.feed(&in, 1, &used, o, sizeof(o), &produced);
    ASSERT_NE(last, ChunkFeed::Invalid);
    out.append(reinterpret_cast<char*>(o), produced);
  }
  EXPECT_EQ(last, ChunkFeed::Complete);
  EXPECT_EQ(out, "hello world");
}

TEST(LocalSendHttp, ChunkDecoderLimits) {
  const auto decode = [](const std::string& wire, uint64_t limit, bool exact) {
    ChunkDecoder d(limit, exact);
    std::vector<uint8_t> buf(wire.begin(), wire.end());
    size_t used = 0, produced = 0;
    return d.feed(buf.data(), buf.size(), &used, buf.data(), buf.size(), &produced);
  };
  EXPECT_EQ(decode("3\r\nabc\r\n0\r\n\r\n", 3, true), ChunkFeed::Complete);
  EXPECT_EQ(decode("3\r\nabc\r\n0\r\n\r\n", 4, true), ChunkFeed::Invalid);  // short body
  EXPECT_EQ(decode("3\r\nabc\r\n0\r\n\r\n", 4, false), ChunkFeed::Complete);
  EXPECT_EQ(decode("5\r\nabcde\r\n0\r\n\r\n", 4, false), ChunkFeed::Invalid);  // over the limit
  EXPECT_EQ(decode("zz\r\nabc\r\n", 10, false), ChunkFeed::Invalid);
  EXPECT_EQ(decode("3\r\nabcX\r\n", 10, false), ChunkFeed::Invalid);
  EXPECT_EQ(decode("ffffffffffffffffff\r\n", UINT64_MAX, false), ChunkFeed::Invalid);
}

TEST(LocalSendHttp, QueryParam) {
  char out[32];
  EXPECT_TRUE(queryParam("/u?sessionId=abc&fileId=a%20b+c&token=t", "fileId", out, sizeof(out)));
  EXPECT_STREQ(out, "a b c");
  EXPECT_TRUE(queryParam("/u?sessionId=abc&fileId=x", "sessionId", out, sizeof(out)));
  EXPECT_STREQ(out, "abc");
  EXPECT_FALSE(queryParam("/u?xsessionId=abc", "sessionId", out, sizeof(out)));
  EXPECT_FALSE(queryParam("/u?id=%00", "id", out, sizeof(out)));
  EXPECT_FALSE(queryParam("/u?id=%4", "id", out, sizeof(out)));
  EXPECT_FALSE(queryParam("/u", "id", out, sizeof(out)));
  EXPECT_FALSE(queryParam("/u?id=0123456789", "id", out, 8));
}

// ---- Names and formats ----

TEST(LocalSendNames, PathTraversalAndReservedCharacters) {
  EXPECT_EQ(sanitized("../../.crosspoint/evil.epub"), "evil.epub");
  EXPECT_EQ(sanitized("..\\..\\boot.epub"), "boot.epub");
  EXPECT_EQ(sanitized("/abs/path/book.fb2"), "book.fb2");
  EXPECT_EQ(sanitized("a:b*c?d\"e<f>g|h.txt"), "abcdefgh.txt");
  EXPECT_EQ(sanitized("tab\there\x01.md"), "tabhere.md");
  EXPECT_EQ(sanitized("...hidden.epub"), "hidden.epub");
  EXPECT_EQ(sanitized("\x01.hidden.epub"), "hidden.epub");
  EXPECT_EQ(sanitized(" spaced name .epub"), "spaced name.epub");
  EXPECT_EQ(sanitized("Война и мир.fb2.zip"), "Война и мир.fb2.zip");
  EXPECT_EQ(sanitized("bad\xff\xfeutf8.epub"), "badutf8.epub");
  EXPECT_EQ(sanitized("overlong\xc0\xaf.epub"), "overlong.epub");
  EXPECT_EQ(sanitized(".."), "<rejected>");
  EXPECT_EQ(sanitized("."), "<rejected>");
  EXPECT_EQ(sanitized("   "), "<rejected>");
  EXPECT_EQ(sanitized("../"), "<rejected>");
  EXPECT_EQ(sanitized(" .epub"), "epub");
  EXPECT_EQ(sanitized(""), "<rejected>");
}

TEST(LocalSendNames, DosDeviceNames) {
  for (const char* bad : {"CON.epub", "con.fb2.zip", "Nul.txt", "aux .md", "PRN.x.epub", "COM1.epub", "lpt9.xtc"}) {
    EXPECT_EQ(sanitized(bad), "<rejected>") << bad;
  }
  for (const char* ok : {"CONSOLE.epub", "icon.epub", "COM10.epub", "LPT.epub", "nul1.txt", "a.con.epub"}) {
    EXPECT_EQ(sanitized(ok), ok);
  }
}

TEST(LocalSendNames, TruncationKeepsExtensionAndUtf8) {
  const std::string longLatin = std::string(300, 'a') + ".epub";
  const std::string a = sanitized(longLatin.c_str());
  EXPECT_LE(a.size(), FILE_NAME_MAX);
  EXPECT_EQ(a.substr(a.size() - 5), ".epub");

  std::string longCyr;
  for (int i = 0; i < 100; i++) longCyr += "ж";
  longCyr += ".fb2.zip";
  const std::string c = sanitized(longCyr.c_str());
  EXPECT_LE(c.size(), FILE_NAME_MAX);
  EXPECT_EQ(c.substr(c.size() - 8), ".fb2.zip");
  EXPECT_TRUE(validUtf8(c));
}

TEST(LocalSendNames, AcceptedFormats) {
  for (const char* ok : {"a.epub", "A.EPUB", "a.fb2", "a.FB2.ZIP", "a.txt", "a.md", "a.xtc", "a.xtch"}) {
    EXPECT_TRUE(isAcceptedBookName(ok)) << ok;
  }
  for (const char* bad : {"a.pdf", "a.zip", "a.epub.exe", "a.json", "a", "a.mobi"}) {
    EXPECT_FALSE(isAcceptedBookName(bad)) << bad;
  }
}

TEST(LocalSendNames, CollisionNames) {
  char out[FILE_NAME_MAX + 16];
  EXPECT_TRUE(makeCollisionName("a.epub", 0, out, sizeof(out)));
  EXPECT_STREQ(out, "a.epub");
  EXPECT_TRUE(makeCollisionName("a.epub", 1, out, sizeof(out)));
  EXPECT_STREQ(out, "a (1).epub");
  EXPECT_TRUE(makeCollisionName("b.fb2.zip", 12, out, sizeof(out)));
  EXPECT_STREQ(out, "b (12).fb2.zip");
  EXPECT_TRUE(makeCollisionName("noext", 3, out, sizeof(out)));
  EXPECT_STREQ(out, "noext (3)");
  EXPECT_FALSE(makeCollisionName("a.epub", 1, out, 8));
}

// ---- Identity documents and discovery ----

TEST(LocalSendIdentity, Documents) {
  Identity id;
  id.alias = "Сова \"x\"";
  id.deviceModel = "X4";
  id.fingerprint = "0011";
  char buf[512];
  ASSERT_GT(writeInfoJson(id, buf, sizeof(buf)), 0u);
  EXPECT_EQ(std::string(buf),
            "{\"alias\":\"Сова \\\"x\\\"\",\"version\":\"2.0\",\"deviceModel\":\"X4\",\"deviceType\":\"mobile\","
            "\"fingerprint\":\"0011\",\"download\":false}");
  ASSERT_GT(writeAnnounceJson(id, true, buf, sizeof(buf)), 0u);
  const std::string announce(buf);
  EXPECT_NE(announce.find("\"port\":53317,\"protocol\":\"http\""), std::string::npos);
  EXPECT_NE(announce.find("\"announce\":true}"), std::string::npos);
  EXPECT_EQ(writeInfoJson(id, buf, 20), 0u);
}

TEST(LocalSendIdentity, DatagramReply) {
  const std::string other = R"({"alias":"P","fingerprint":"ffff","port":53317,"protocol":"https","announce":true})";
  EXPECT_TRUE(datagramWantsReply(other.data(), other.size(), "0011"));
  const std::string spaced = R"({"fingerprint" : "ffff", "announce" : true })";
  EXPECT_TRUE(datagramWantsReply(spaced.data(), spaced.size(), "0011"));
  const std::string own = R"({"fingerprint":"0011","announce":true})";
  EXPECT_FALSE(datagramWantsReply(own.data(), own.size(), "0011"));
  const std::string reply = R"({"fingerprint":"ffff","announce":false})";
  EXPECT_FALSE(datagramWantsReply(reply.data(), reply.size(), "0011"));
  EXPECT_FALSE(datagramWantsReply("garbage", 7, "0011"));
}

// ---- Session state machine ----

TEST(LocalSendSession, PrepareValidatesEachFile) {
  Session s;
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 1000), 0);
  const std::string body = prepareBody({{"f1", "../Книга.epub", "1000"},
                                        {"f2", "scan.pdf", "1000"},
                                        {"f3", "huge.epub", std::to_string(MAX_FILE_BYTES + 1)},
                                        {"f4", "empty.txt", "0"},
                                        {"f5", "...", "10"}},
                                       "Alex's Pixel");
  s.feedPrepare(body.data(), body.size());
  EXPECT_EQ(s.finishPrepare(UINT64_MAX, 1000), 0);
  EXPECT_EQ(s.phase(), Phase::AwaitingDecision);
  EXPECT_STREQ(s.senderAlias(), "Alex's Pixel");
  ASSERT_EQ(s.entryCount(), 5u);
  EXPECT_EQ(s.entry(0).state, FileState::Accepted);
  EXPECT_STREQ(s.entry(0).name, "Книга.epub");
  EXPECT_EQ(s.entry(1).reason, Reject::Format);
  EXPECT_EQ(s.entry(2).reason, Reject::TooLarge);
  EXPECT_EQ(s.entry(3).reason, Reject::Empty);
  EXPECT_EQ(s.entry(4).reason, Reject::BadName);

  EXPECT_EQ(s.writePrepareResponse(nullptr, 0), 0u);  // nothing to send before the decision
  s.decide(true, 1100);
  char resp[512];
  ASSERT_GT(s.writePrepareResponse(resp, sizeof(resp)), 0u);
  const std::string json(resp);
  EXPECT_EQ(jsonField(json, "sessionId").size(), TOKEN_LEN);
  EXPECT_EQ(jsonField(json, "f1"), s.entry(0).token);
  EXPECT_EQ(json.find("f2"), std::string::npos);
}

TEST(LocalSendSession, AllRejectedIsForbiddenAndEmptyIsNoContent) {
  Session s;
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  const std::string pdf = prepareBody({{"f", "doc.pdf", "5"}});
  s.feedPrepare(pdf.data(), pdf.size());
  EXPECT_EQ(s.finishPrepare(UINT64_MAX, 0), 403);
  EXPECT_EQ(s.phase(), Phase::Finished);

  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  const std::string none = prepareBody({});
  s.feedPrepare(none.data(), none.size());
  EXPECT_EQ(s.finishPrepare(UINT64_MAX, 0), 204);
  EXPECT_EQ(s.phase(), Phase::Idle);
}

TEST(LocalSendSession, MalformedBodies) {
  const std::string valid = prepareBody({{"a", "a.epub", "10"}});
  for (const std::string& body :
       {std::string("{\"info\":{}}"), std::string("{\"files\":{\"a\":{\"fileName\":\"x"), std::string("[1,2,3]"),
        std::string(""), valid + "{}", valid + " \"x", valid + "1", valid + "tru", valid + "}"}) {
    Session s;
    s.configure("", false, &counterRng);
    ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
    s.feedPrepare(body.data(), body.size());
    EXPECT_EQ(s.finishPrepare(UINT64_MAX, 0), 400) << body;
    EXPECT_EQ(s.phase(), Phase::Idle);
  }
  Session s;
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  const std::string spaced = valid + " \r\n\t";
  s.feedPrepare(spaced.data(), spaced.size());
  EXPECT_EQ(s.finishPrepare(UINT64_MAX, 0), 0);
}

TEST(LocalSendSession, IdFallbackDuplicatesAndControlBytes) {
  Session s;
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  const std::string body =
      "{\"files\":{\"key1\":{\"fileName\":\"a.epub\",\"size\":1},"
      "\"k2\":{\"id\":\"key1\",\"fileName\":\"b.epub\",\"size\":1},"
      "\"k3\":{\"id\":\"q\\\"uote\",\"fileName\":\"c.epub\",\"size\":1},"
      "\"k4\":{\"id\":\"ctl\\u0001\",\"fileName\":\"d.epub\",\"size\":1},"
      "\"k5\":{\"fileName\":\"e.epub\",\"size\":\"1\"}}}";
  s.feedPrepare(body.data(), body.size());
  EXPECT_EQ(s.finishPrepare(UINT64_MAX, 0), 0);
  ASSERT_EQ(s.entryCount(), 5u);
  EXPECT_STREQ(s.entry(0).id, "key1");
  EXPECT_EQ(s.entry(0).state, FileState::Accepted);
  EXPECT_EQ(s.entry(1).reason, Reject::Duplicate);
  EXPECT_EQ(s.entry(2).state, FileState::Accepted);
  EXPECT_EQ(s.entry(3).reason, Reject::BadName);
  EXPECT_EQ(s.entry(4).reason, Reject::BadName);  // size must be a number
  s.decide(true, 0);
  char resp[512];
  ASSERT_GT(s.writePrepareResponse(resp, sizeof(resp)), 0u);
  EXPECT_NE(std::string(resp).find("\"q\\\"uote\":"), std::string::npos);
}

TEST(LocalSendSession, FileLimitAndFreeSpace) {
  Session s;
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  std::vector<std::tuple<std::string, std::string, std::string>> files;
  for (size_t i = 0; i < MAX_FILES + 3; i++) {
    files.emplace_back("id" + std::to_string(i), "b" + std::to_string(i) + ".epub", "4000000");
  }
  const std::string body = prepareBody(files);
  s.feedPrepare(body.data(), body.size());
  // Room for two files after the reserve.
  EXPECT_EQ(s.finishPrepare(FREE_SPACE_RESERVE + 8000000, 0), 0);
  EXPECT_EQ(s.entryCount(), MAX_FILES);
  EXPECT_EQ(s.droppedCount(), 3u);
  EXPECT_EQ(s.entry(0).state, FileState::Accepted);
  EXPECT_EQ(s.entry(1).state, FileState::Accepted);
  EXPECT_EQ(s.entry(2).reason, Reject::NoSpace);
}

TEST(LocalSendSession, PinAndLockout) {
  Session s;
  s.configure("4821", false, &counterRng);
  EXPECT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 401);
  EXPECT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 401);  // asking for the PIN costs nothing
  EXPECT_EQ(s.checkPrepare("10.0.0.2", "0000", 0), 401);
  EXPECT_EQ(s.checkPrepare("10.0.0.2", "1111", 0), 401);
  EXPECT_FALSE(s.pinLocked(0));
  EXPECT_EQ(s.checkPrepare("10.0.0.2", "48210", 0), 401);
  EXPECT_TRUE(s.pinLocked(0));
  EXPECT_EQ(s.checkPrepare("10.0.0.2", "4821", 1000), 429);  // locked even with the right PIN
  EXPECT_EQ(s.checkPrepare("10.0.0.2", "4821", PIN_LOCKOUT_MS + 1), 0);
  EXPECT_EQ(s.phase(), Phase::Preparing);
}

TEST(LocalSendSession, BusyWhileActive) {
  Session s;
  acceptedSession(s, prepareBody({{"f", "a.epub", "3"}}));
  EXPECT_EQ(s.checkPrepare("10.0.0.3", nullptr, 1000), 409);
  size_t index = 0;
  ASSERT_EQ(s.beginUpload(jsonField(
                              [&] {
                                char r[256];
                                s.writePrepareResponse(r, sizeof(r));
                                return std::string(r);
                              }(),
                              "sessionId")
                              .c_str(),
                          "f", s.entry(0).token, "10.0.0.2", 1000, &index),
            0);
  s.endUpload(index, true, 1000);
  EXPECT_EQ(s.phase(), Phase::Finished);
  EXPECT_EQ(s.checkPrepare("10.0.0.3", nullptr, 1000), 0);
}

TEST(LocalSendSession, UploadChecks) {
  Session s;
  acceptedSession(s, prepareBody({{"f", "a.epub", "3"}, {"g", "b.epub", "3"}}));
  char r[256];
  ASSERT_GT(s.writePrepareResponse(r, sizeof(r)), 0u);
  const std::string sid = jsonField(r, "sessionId");
  const std::string token = s.entry(0).token;
  size_t index = 99;
  EXPECT_EQ(s.beginUpload(sid.c_str(), "f", token.c_str(), "10.0.0.9", 1000, &index), 403);  // other host
  EXPECT_EQ(s.beginUpload("0000000000000000", "f", token.c_str(), "10.0.0.2", 1000, &index), 403);
  EXPECT_EQ(s.beginUpload(sid.c_str(), "f", "0000000000000000", "10.0.0.2", 1000, &index), 403);
  EXPECT_EQ(s.beginUpload(sid.c_str(), "nope", token.c_str(), "10.0.0.2", 1000, &index), 403);
  EXPECT_EQ(s.beginUpload(sid.c_str(), "g", token.c_str(), "10.0.0.2", 1000, &index), 403);  // f's token
  EXPECT_EQ(s.beginUpload(sid.c_str(), "f", token.c_str(), "10.0.0.2", 1000, &index), 0);
  EXPECT_EQ(index, 0u);
  EXPECT_EQ(s.beginUpload(sid.c_str(), "f", token.c_str(), "10.0.0.2", 1000, &index), 409);
  s.endUpload(0, false, 1000);
  EXPECT_EQ(s.entry(0).state, FileState::Failed);
  EXPECT_EQ(s.phase(), Phase::Transfer);  // g still expected
  EXPECT_TRUE(s.cancel(sid.c_str(), "10.0.0.2"));
  EXPECT_EQ(s.phase(), Phase::Finished);
  EXPECT_EQ(s.entry(1).state, FileState::Failed);
}

TEST(LocalSendSession, Timeouts) {
  Session s;
  s.configure("", false, &counterRng);
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  const std::string body = prepareBody({{"f", "a.epub", "3"}});
  s.feedPrepare(body.data(), body.size());
  ASSERT_EQ(s.finishPrepare(UINT64_MAX, 0), 0);
  s.tick(DECISION_TIMEOUT_MS);
  EXPECT_EQ(s.phase(), Phase::AwaitingDecision);
  s.tick(DECISION_TIMEOUT_MS + 1);
  EXPECT_EQ(s.phase(), Phase::Finished);
  EXPECT_EQ(s.entry(0).reason, Reject::Declined);

  Session t;
  acceptedSession(t, body, 0);
  t.tick(SESSION_IDLE_MS + 1);
  EXPECT_EQ(t.phase(), Phase::Finished);
  EXPECT_EQ(t.entry(0).state, FileState::Failed);
}

TEST(LocalSendSession, AutoAcceptAndRevision) {
  Session s;
  s.configure("", true, &counterRng);
  const uint32_t r0 = s.revision();
  ASSERT_EQ(s.checkPrepare("10.0.0.2", nullptr, 0), 0);
  const std::string body = prepareBody({{"f", "a.epub", "300"}});
  s.feedPrepare(body.data(), body.size());
  EXPECT_EQ(s.finishPrepare(UINT64_MAX, 0), 200);
  EXPECT_EQ(s.phase(), Phase::Transfer);
  EXPECT_GT(s.revision(), r0);
  size_t index = 0;
  char r[256];
  s.writePrepareResponse(r, sizeof(r));
  ASSERT_EQ(s.beginUpload(jsonField(r, "sessionId").c_str(), "f", s.entry(0).token, "10.0.0.2", 0, &index), 0);
  const uint32_t r1 = s.revision();
  s.uploadProgress(index, 1, 0);  // under 1 %: no repaint
  EXPECT_EQ(s.revision(), r1);
  s.uploadProgress(index, 150, 0);
  EXPECT_GT(s.revision(), r1);
}

// ---- Full HTTP exchanges ----

TEST(LocalSendConnection, InfoAndRegister) {
  Rig rig;
  FakeIo info = rig.run({"GET /api/localsend/v2/info HTTP/1.1\r\nHost: x\r\n\r\n"});
  EXPECT_EQ(info.status(), 200);
  EXPECT_EQ(jsonField(info.body(), "alias"), "Сова");
  EXPECT_FALSE(rig.conn.active());

  FakeIo reg = rig.run({post("/api/localsend/v2/register", "{\"alias\":\"Phone\"}")});
  EXPECT_EQ(reg.status(), 200);
  EXPECT_EQ(jsonField(reg.body(), "fingerprint"), "0011aabbccdd");
}

TEST(LocalSendConnection, RejectsMalformedRequests) {
  Rig rig;
  EXPECT_EQ(rig.run({"BROKEN\r\n\r\n"}).status(), 400);
  EXPECT_EQ(rig.run({"GET /nope HTTP/1.1\r\n\r\n"}).status(), 404);
  EXPECT_EQ(rig.run({"GET /api/localsend/v2/info HTTP/1.1\r\nX: " + std::string(600, 'a') + "\r\n\r\n"}).status(), 431);
  EXPECT_EQ(rig.run({"POST /api/localsend/v2/register HTTP/1.1\r\nContent-Length: 3\r\n"
                     "Transfer-Encoding: chunked\r\n\r\nabc"})
                .status(),
            400);
  EXPECT_EQ(rig.run({post("/api/localsend/v2/register", std::string(SMALL_BODY_MAX + 1, 'a'))}).status(), 413);
  FakeIo big = rig.run({"POST /api/localsend/v2/prepare-upload HTTP/1.1\r\nContent-Length: " +
                        std::to_string(JSON_BODY_MAX + 1) + "\r\n\r\n"});
  EXPECT_EQ(big.status(), 413);
  EXPECT_EQ(rig.session.phase(), Phase::Idle);
}

TEST(LocalSendConnection, FullTransferWithDecision) {
  Rig rig;
  const std::string body = prepareBody({{"f1", "../../Мастер.epub", "11"}, {"f2", "x.pdf", "4"}});
  FakeIo io;
  io.input.push_back(post("/api/localsend/v2/prepare-upload", body));
  rig.conn.begin(io, "10.0.0.2", 1000);
  EXPECT_TRUE(rig.conn.poll(1000, 1 << 20));
  EXPECT_EQ(rig.session.phase(), Phase::AwaitingDecision);
  EXPECT_TRUE(io.output.empty());  // held open until the user decides
  EXPECT_TRUE(rig.conn.poll(2000, 1 << 20));
  rig.session.decide(true, 3000);
  EXPECT_FALSE(rig.conn.poll(3000, 1 << 20));
  ASSERT_EQ(io.status(), 200);
  const std::string sid = jsonField(io.body(), "sessionId");
  const std::string token = jsonField(io.body(), "f1");
  EXPECT_EQ(token, rig.session.entry(0).token);

  const std::string target = "/api/localsend/v2/upload?sessionId=" + sid + "&fileId=f1&token=" + token;
  // Content arrives in small TCP segments.
  FakeIo up;
  const std::string req = post(target, "hello world");
  for (size_t i = 0; i < req.size(); i += 7) up.input.push_back(req.substr(i, 7));
  rig.conn.begin(up, "10.0.0.2", 4000);
  while (rig.conn.poll(4000, 16)) {
  }
  EXPECT_EQ(up.status(), 200);
  EXPECT_EQ(rig.store.openedName, "Мастер.epub");
  EXPECT_EQ(rig.store.data, "hello world");
  EXPECT_EQ(rig.store.commits, 1);
  EXPECT_EQ(rig.session.entry(0).state, FileState::Done);
  EXPECT_EQ(rig.session.phase(), Phase::Finished);
}

TEST(LocalSendConnection, ChunkedUploadAndErrors) {
  Rig rig;
  acceptedSession(rig.session, prepareBody({{"a", "a.epub", "26"}, {"b", "b.epub", "5"}, {"c", "c.epub", "5"}}));
  char r[512];
  rig.session.writePrepareResponse(r, sizeof(r));
  const std::string sid = jsonField(r, "sessionId");
  const auto target = [&](const char* id, size_t i) {
    return "/api/localsend/v2/upload?sessionId=" + sid + "&fileId=" + id + "&token=" + rig.session.entry(i).token;
  };

  const std::string payload = "abcdefghijklmnopqrstuvwxyz";
  FakeIo ok =
      rig.run({"POST " + target("a", 0) + " HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n" + chunked(payload, 5)});
  EXPECT_EQ(ok.status(), 200);
  EXPECT_EQ(rig.store.data, payload);

  // Declared size and Content-Length disagree: nothing is opened.
  const int opens = rig.store.opens;
  EXPECT_EQ(rig.run({post(target("b", 1), "123")}).status(), 400);
  EXPECT_EQ(rig.store.opens, opens);
  EXPECT_EQ(rig.session.entry(1).state, FileState::Failed);

  // Checksum mismatch reported by the store.
  rig.store.commitStatus = 422;
  EXPECT_EQ(rig.run({post(target("c", 2), "12345")}).status(), 422);
  EXPECT_EQ(rig.session.entry(2).state, FileState::Failed);
  EXPECT_EQ(rig.session.phase(), Phase::Finished);
}

TEST(LocalSendConnection, DisconnectAndStorageFailures) {
  Rig rig;
  acceptedSession(rig.session, prepareBody({{"a", "a.epub", "10"}, {"b", "b.epub", "10"}, {"c", "c.epub", "10"}}));
  char r[512];
  rig.session.writePrepareResponse(r, sizeof(r));
  const std::string sid = jsonField(r, "sessionId");
  const auto request = [&](const char* id, size_t i, const std::string& body) {
    const std::string head = "POST /api/localsend/v2/upload?sessionId=" + sid + "&fileId=" + id +
                             "&token=" + rig.session.entry(i).token + " HTTP/1.1\r\nContent-Length: 10\r\n\r\n";
    return head + body;
  };

  FakeIo cut = rig.run({request("a", 0, "12345")}, 1000, true);
  EXPECT_EQ(rig.store.aborts, 1);
  EXPECT_EQ(rig.session.entry(0).state, FileState::Failed);
  EXPECT_TRUE(cut.output.empty());

  rig.store.failWrite = true;
  EXPECT_EQ(rig.run({request("b", 1, "1234567890")}).status(), 500);
  EXPECT_EQ(rig.store.aborts, 2);
  rig.store.failWrite = false;

  // A stalled sender is dropped after STALL_TIMEOUT_MS.
  FakeIo stall;
  stall.input.push_back(request("c", 2, "12"));
  rig.conn.begin(stall, "10.0.0.2", 5000);
  EXPECT_TRUE(rig.conn.poll(5000, 1 << 20));
  EXPECT_TRUE(rig.conn.poll(5000 + STALL_TIMEOUT_MS, 1 << 20));
  EXPECT_FALSE(rig.conn.poll(5001 + STALL_TIMEOUT_MS, 1 << 20));
  EXPECT_EQ(rig.store.aborts, 3);
  EXPECT_EQ(rig.session.phase(), Phase::Finished);
}

TEST(LocalSendConnection, PinIsCheckedBeforeTheBodyIsParsed) {
  Rig rig;
  rig.session.configure("1234", false, &counterRng);
  const std::string body = prepareBody({{"f", "a.epub", "3"}});
  EXPECT_EQ(rig.run({post("/api/localsend/v2/prepare-upload", body)}).status(), 401);
  EXPECT_EQ(rig.session.phase(), Phase::Idle);
  EXPECT_EQ(rig.run({post("/api/localsend/v2/prepare-upload?pin=1234", body)}).status(), 0);  // held
  EXPECT_EQ(rig.session.phase(), Phase::AwaitingDecision);
}

TEST(LocalSendConnection, HoldEndsOnDeclineOrSenderLeaving) {
  Rig rig;
  const std::string body = prepareBody({{"f", "a.epub", "3"}});
  FakeIo io;
  io.input.push_back(post("/api/localsend/v2/prepare-upload", body));
  rig.conn.begin(io, "10.0.0.2", 0);
  EXPECT_TRUE(rig.conn.poll(0, 1 << 20));
  rig.session.decide(false, 10);
  EXPECT_FALSE(rig.conn.poll(10, 1 << 20));
  EXPECT_EQ(io.status(), 403);

  FakeIo gone;
  gone.input.push_back(post("/api/localsend/v2/prepare-upload", body));
  rig.conn.begin(gone, "10.0.0.2", 0);
  EXPECT_TRUE(rig.conn.poll(0, 1 << 20));
  gone.closedAfterInput = true;
  EXPECT_FALSE(rig.conn.poll(10, 1 << 20));
  EXPECT_EQ(rig.session.phase(), Phase::Idle);
}

TEST(LocalSendConnection, HeadTimeout) {
  Rig rig;
  FakeIo io;
  io.input.push_back("GET /api/localsend/v2/in");
  rig.conn.begin(io, "10.0.0.2", 0);
  EXPECT_TRUE(rig.conn.poll(0, 1 << 20));
  EXPECT_FALSE(rig.conn.poll(HEAD_TIMEOUT_MS + 1, 1 << 20));
  EXPECT_EQ(io.status(), 408);
}

TEST(LocalSendConnection, TricklingBodyHitsTheRequestDeadline) {
  Rig rig;
  FakeIo io;
  io.input.push_back("POST /api/localsend/v2/prepare-upload HTTP/1.1\r\nContent-Length: 1000\r\n\r\n{");
  rig.conn.begin(io, "10.0.0.2", 0);
  uint32_t now = 0;
  EXPECT_TRUE(rig.conn.poll(now, 1 << 20));
  // One byte just inside every stall window keeps the stall timer happy.
  while (now + STALL_TIMEOUT_MS / 2 <= REQUEST_TIMEOUT_MS) {
    now += STALL_TIMEOUT_MS / 2;
    io.input.push_back(" ");
    ASSERT_TRUE(rig.conn.poll(now, 1 << 20));
  }
  now = REQUEST_TIMEOUT_MS + 1;
  io.input.push_back(" ");
  EXPECT_FALSE(rig.conn.poll(now, 1 << 20));
  EXPECT_EQ(io.status(), 408);
  EXPECT_EQ(rig.session.phase(), Phase::Idle);
}

TEST(LocalSendConnection, CancelFromSender) {
  Rig rig;
  acceptedSession(rig.session, prepareBody({{"f", "a.epub", "3"}}));
  char r[256];
  rig.session.writePrepareResponse(r, sizeof(r));
  const std::string sid = jsonField(r, "sessionId");
  EXPECT_EQ(rig.run({post("/api/localsend/v2/cancel?sessionId=" + sid, "")}, 1000, false, "10.0.0.7").status(), 200);
  EXPECT_EQ(rig.session.phase(), Phase::Transfer);  // only the sender may cancel
  EXPECT_EQ(rig.run({post("/api/localsend/v2/cancel?sessionId=" + sid, "")}).status(), 200);
  EXPECT_EQ(rig.session.phase(), Phase::Finished);
}
