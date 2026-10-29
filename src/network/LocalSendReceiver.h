#pragma once

#include <HalStorage.h>
#include <LocalSendConnection.h>
#include <mbedtls/sha256.h>

#include <cstddef>
#include <cstdint>
#include <optional>

#include "LocalSendNet.h"
#include "WifiPowerSaveGuard.h"

// Receives books from the LocalSend app (protocol v2 over plain HTTP; the
// sender uses the protocol a receiver announces). It runs only between start()
// and stop(), which the receive screen calls on enter/exit and before sleep;
// the screen owns Wi-Fi. loop() never blocks on the network: a held
// prepare-upload waits for accept()/decline() while the screen keeps drawing.
class LocalSendReceiver {
 public:
  struct Config {
    const char* alias = "Сова";
    const char* deviceModel = "";
    const char* pin = "";           // empty: no PIN
    bool autoAccept = false;        // skip accept()/decline()
    const char* destDir = nullptr;  // nullptr: defaultDestDir()
  };

  LocalSendReceiver();
  LocalSendReceiver(const LocalSendReceiver&) = delete;
  LocalSendReceiver& operator=(const LocalSendReceiver&) = delete;
  ~LocalSendReceiver() { stop(); }

  // Wi-Fi must already be connected. Returns false when the sockets fail.
  bool start(const Config& config);
  void stop();
  void loop();
  bool running() const { return started; }

  void accept();
  void decline();

  // session().revision() changes with everything the screen shows.
  const localsend::Session& session() const { return sessionState; }
  // A request awaits the decision or a file is streaming: keep the device awake.
  bool busy() const;
  // An exchange is moving bytes (not waiting for the user): run the loop at full speed.
  bool streaming() const { return started && connection.active() && !connection.holding(); }
  const char* destDir() const { return store.dir; }
  // Entry i went to the flashcard folder.
  bool toCards(const size_t i) const { return i < 16 && (store.toCards & (1u << i)); }

  // The configured download folder, else /Книги.
  static void defaultDestDir(char* out, size_t cap);

 private:
  class Store final : public localsend::FileStore {
   public:
    explicit Store(const localsend::Session& session) : session(session) {}
    char dir[64] = {};
    uint16_t toCards = 0;  // a bit per session entry
    uint64_t freeBytes() override;
    bool open(const localsend::FileEntry& entry) override;
    bool write(const uint8_t* data, size_t len) override;
    int commit(const localsend::FileEntry& entry) override;
    void abort() override;
    void forgetFreeSpace() { freeKnown = false; }
    void reclaimPartials(const char* folder);

   private:
    const localsend::Session& session;
    HalFile file;
    char finalPath[208] = {};
    char tmpPath[216] = {};
    mbedtls_sha256_context sha = {};
    bool hashing = false;
    bool deck = false;  // a flashcard deck: replaces one of the same name, so its progress carries over
    uint64_t freeCache = 0;
    bool freeKnown = false;
    void stopHashing();
    bool placeIn(const char* folder, const char* name, bool replace);
    int entryIndex(const localsend::FileEntry& entry) const;
  };

  localsend::Session sessionState;
  Store store;
  localsend::Identity identity;
  localsend::Connection connection;
  LocalSendNet net;
  std::optional<WifiPowerSaveGuard> powerSave;

  char alias[localsend::ALIAS_MAX + 1] = {};
  char deviceModel[32] = {};
  char fingerprint[13] = {};
  char datagram[768] = {};
  bool started = false;
  uint8_t announcesSent = 0;
  uint32_t startedMs = 0;
  uint32_t lastReplyMs = 0;

  void pollDiscovery(uint32_t nowMs);
  void announce(bool isAnnouncement);
};
