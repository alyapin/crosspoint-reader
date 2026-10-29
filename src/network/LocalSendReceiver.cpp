#include "LocalSendReceiver.h"

#include <FsHelpers.h>
#include <LibraryBuilder.h>
#include <Logging.h>
#include <WiFi.h>
#include <arpa/inet.h>
#include <esp_random.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "CardDeck.h"
#include "CardStore.h"
#include "CrossPointSettings.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr char DEFAULT_DIR[] = "/Книги";
constexpr int MAX_COLLISIONS = 100;
// Input read per loop() call, so a streaming file leaves room for input and rendering.
constexpr size_t POLL_BUDGET = 32 * 1024;
// A burst from start(): a datagram is easily lost, and the phone may join late.
constexpr uint32_t ANNOUNCE_AT_MS[] = {0, 1000, 3000};
constexpr uint32_t REPLY_INTERVAL_MS = 500;
constexpr int DATAGRAMS_PER_LOOP = 4;
// Not a book extension, so no received file ends with it; .part belongs to web uploads.
constexpr char PART_SUFFIX[] = ".localsend";
static_assert(std::string_view(PART_SUFFIX) == cards::DECK_PARTIAL_SUFFIX);

bool endsWith(const char* name, const char* suffix) {
  const size_t len = strlen(name);
  const size_t n = strlen(suffix);
  return len > n && strcmp(name + len - n, suffix) == 0;
}

uint32_t hardwareRandom() { return esp_random(); }
}  // namespace

LocalSendReceiver::LocalSendReceiver() : store(sessionState), connection(sessionState, identity, store) {}

void LocalSendReceiver::defaultDestDir(char* out, const size_t cap) {
  snprintf(out, cap, "%s", SETTINGS.opdsDownloadFolder[0] ? SETTINGS.opdsDownloadFolder : DEFAULT_DIR);
}

bool LocalSendReceiver::start(const Config& config) {
  stop();
  if (!Storage.ready()) {
    LOG_ERR("LSND", "start without an SD card");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED && !(WiFi.getMode() & WIFI_MODE_AP)) {
    LOG_ERR("LSND", "start without a network");
    return false;
  }

  snprintf(alias, sizeof(alias), "%s", config.alias ? config.alias : "");
  snprintf(deviceModel, sizeof(deviceModel), "%s", config.deviceModel ? config.deviceModel : "");
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  snprintf(fingerprint, sizeof(fingerprint), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
           mac[5]);
  identity.alias = alias;
  identity.deviceModel = deviceModel;
  identity.fingerprint = fingerprint;
  identity.port = localsend::DEFAULT_PORT;

  if (config.destDir) {
    snprintf(store.dir, sizeof(store.dir), "%s", config.destDir);
  } else {
    defaultDestDir(store.dir, sizeof(store.dir));
  }
  size_t len = strlen(store.dir);
  while (len > 0 && store.dir[len - 1] == '/') store.dir[--len] = 0;  // "/" becomes the root ""
  if (len > 0 && !Storage.exists(store.dir) && !Storage.mkdir(store.dir)) {
    LOG_ERR("LSND", "mkdir %s failed, using SD root", store.dir);
    store.dir[0] = 0;
  }
  cards::restoreReplacedDecks();
  store.reclaimPartials(store.dir);
  if (Storage.exists(cards::deckDir())) store.reclaimPartials(cards::deckDir());
  store.forgetFreeSpace();
  // The first query scans the FAT (seconds on a large card): do it now, while
  // no sender waits on a prepare-upload.
  store.freeBytes();

  sessionState.configure(config.pin, config.autoAccept, &hardwareRandom);
  const bool ap = WiFi.status() != WL_CONNECTED;
  const IPAddress ip = ap ? WiFi.softAPIP() : WiFi.localIP();
  const uint32_t interfaceAddr = htonl((static_cast<uint32_t>(ip[0]) << 24) | (static_cast<uint32_t>(ip[1]) << 16) |
                                       (static_cast<uint32_t>(ip[2]) << 8) | ip[3]);
  if (!net.begin(localsend::DEFAULT_PORT, interfaceAddr)) return false;
  // Modem sleep drops multicast frames between beacons and stalls transfers.
  powerSave.emplace();
  started = true;
  startedMs = millis();
  announcesSent = 0;
  LOG_INF("LSND", "receiving into '%s/' as %s", store.dir, alias);
  return true;
}

void LocalSendReceiver::stop() {
  if (!started) return;
  connection.abort();
  net.end();
  powerSave.reset();
  sessionState.reset();
  started = false;
  LOG_INF("LSND", "stopped");
}

void LocalSendReceiver::loop() {
  if (!started) return;
  const uint32_t now = millis();
  pollDiscovery(now);

  if (!connection.active()) {
    net.closeClient();
    char ip[46];
    if (net.accept(ip, sizeof(ip))) connection.begin(net.client(), ip, now);
  }
  if (connection.active() && !connection.poll(now, POLL_BUDGET)) net.closeClient();
  resetTaskWatchdogIfSubscribed();

  sessionState.tick(now);
}

void LocalSendReceiver::accept() { sessionState.decide(true, millis()); }

void LocalSendReceiver::decline() { sessionState.decide(false, millis()); }

bool LocalSendReceiver::busy() const {
  const localsend::Phase phase = sessionState.phase();
  return started &&
         (connection.active() || phase == localsend::Phase::AwaitingDecision || phase == localsend::Phase::Transfer);
}

void LocalSendReceiver::announce(const bool isAnnouncement) {
  const size_t n = localsend::writeAnnounceJson(identity, isAnnouncement, datagram, sizeof(datagram));
  if (n > 0) net.sendMulticast(datagram, n);
}

void LocalSendReceiver::pollDiscovery(const uint32_t nowMs) {
  if (announcesSent < sizeof(ANNOUNCE_AT_MS) / sizeof(ANNOUNCE_AT_MS[0]) &&
      nowMs - startedMs >= ANNOUNCE_AT_MS[announcesSent]) {
    announce(true);
    announcesSent++;
  }
  // A sender that announces itself expects our announcement back; it then
  // registers with us over HTTP.
  for (int i = 0; i < DATAGRAMS_PER_LOOP; i++) {
    const int n = net.receive(datagram, sizeof(datagram));
    if (n <= 0) break;
    if (!localsend::datagramWantsReply(datagram, static_cast<size_t>(n), fingerprint)) continue;
    if (nowMs - lastReplyMs < REPLY_INTERVAL_MS) continue;
    lastReplyMs = nowMs;
    announce(false);
  }
}

// ---- Store ----

uint64_t LocalSendReceiver::Store::freeBytes() {
#ifdef SIMULATOR
  return UINT64_MAX;  // the simulator HAL has no free-space query
#else
  // One FAT scan per session start; later commits are subtracted.
  if (!freeKnown) {
    freeCache = Storage.freeBytes();
    freeKnown = true;
  }
  return freeCache;
#endif
}

int LocalSendReceiver::Store::entryIndex(const localsend::FileEntry& entry) const {
  for (size_t i = 0; i < session.entryCount() && i < 16; i++) {
    if (&session.entry(i) == &entry) return static_cast<int>(i);
  }
  return -1;
}

// Picks a free final path in `folder` (or the same name, to replace) and its partial next to it.
bool LocalSendReceiver::Store::placeIn(const char* folder, const char* entryName) {
  char name[localsend::FILE_NAME_MAX + 16];
  for (int i = 0; i < MAX_COLLISIONS; i++) {
    if (!localsend::makeCollisionName(entryName, i, name, sizeof(name))) return false;
    const int n = snprintf(finalPath, sizeof(finalPath), "%s/%s", folder, name);
    const int m = snprintf(tmpPath, sizeof(tmpPath), "%s%s", finalPath, PART_SUFFIX);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(finalPath) || m <= 0 || static_cast<size_t>(m) >= sizeof(tmpPath)) {
      return false;
    }
    // A partial without its book is left over from an interrupted transfer.
    if (!Storage.exists(finalPath) && (!Storage.exists(tmpPath) || Storage.remove(tmpPath))) return true;
  }
  return false;
}

bool LocalSendReceiver::Store::open(const localsend::FileEntry& entry) {
  abort();
  const int index = entryIndex(entry);
  if (index >= 0) toCards &= static_cast<uint16_t>(~(1u << index));
  // Every file arrives in the books folder; commit() moves a deck to the deck folder.
  deck = false;
  if (!placeIn(dir, entry.name) || !Storage.openFileForWrite("LSND", tmpPath, file)) {
    LOG_ERR("LSND", "cannot create a file for %s", entry.name);
    tmpPath[0] = 0;
    return false;
  }
#ifndef SIMULATOR  // the simulator's SHA-256 is a stand-in
  if (entry.hasSha256) {
    mbedtls_sha256_init(&sha);
    hashing = true;
    if (mbedtls_sha256_starts(&sha, 0) != 0) stopHashing();
  }
#endif
  return true;
}

bool LocalSendReceiver::Store::write(const uint8_t* data, const size_t len) {
  if (file.write(data, len) != len) {
    LOG_ERR("LSND", "write failed: %s", tmpPath);
    return false;
  }
  if (hashing) mbedtls_sha256_update(&sha, data, len);
  return true;
}

int LocalSendReceiver::Store::commit(const localsend::FileEntry& entry) {
  // close() writes the directory entry; a failed close is a failed file.
  if (!file.close()) {
    LOG_ERR("LSND", "close failed: %s", tmpPath);
    abort();
    return 500;
  }
  if (hashing) {
    uint8_t digest[32];
    const bool done = mbedtls_sha256_finish(&sha, digest) == 0;
    stopHashing();
    if (!done || memcmp(digest, entry.sha256, sizeof(digest)) != 0) {
      LOG_ERR("LSND", "checksum mismatch: %s", entry.name);
      abort();
      return 422;
    }
  }
  // Tables are decks; a .txt is one when it opens with an Anki export header.
  deck = cards::isTableName(entry.name);
  if (!deck && FsHelpers::hasTxtExtension(std::string_view(entry.name))) {
    uint8_t head[64];
    HalFile f;
    int got = -1;
    if (Storage.openFileForRead("LSND", tmpPath, f)) {
      got = f.read(head, sizeof(head));
      f.close();
    }
    deck = got > 0 && cards::hasAnkiHeader(head, static_cast<size_t>(got));
  }
  const cards::Placed placed =
      deck ? cards::placeReceivedDeck(tmpPath, sizeof(tmpPath), entry.name, finalPath, sizeof(finalPath))
      : Storage.rename(tmpPath, finalPath) ? cards::Placed::Done
                                           : cards::Placed::Failed;
  if (placed != cards::Placed::Done) {
    LOG_ERR("LSND", "rename failed: %s", finalPath);
    if (placed == cards::Placed::PartialKept) tmpPath[0] = 0;
    abort();
    return 500;
  }
  const int index = entryIndex(entry);
  if (index >= 0 && deck) toCards |= static_cast<uint16_t>(1u << index);
  tmpPath[0] = 0;
  clearBookCache(finalPath);
  if (library::isLibraryBookFile(finalPath)) library::markLibraryIndexDirty();
  if (freeKnown) freeCache = freeCache > entry.size ? freeCache - entry.size : 0;
  LOG_INF("LSND", "stored %s (%llu bytes)", finalPath, static_cast<unsigned long long>(entry.size));
  return 0;
}

void LocalSendReceiver::Store::abort() {
  if (file.isOpen()) file.close();
  stopHashing();
  if (tmpPath[0]) Storage.remove(tmpPath);
  tmpPath[0] = 0;
}

// A transfer cut by a reset leaves its partial, which would count against the free space.
void LocalSendReceiver::Store::reclaimPartials(const char* folder) {
  std::vector<std::string> stale;
  {
    HalFile root = Storage.open(folder[0] ? folder : "/");
    if (!root || !root.isDirectory()) return;
    char name[256];
    for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
      resetTaskWatchdogIfSubscribed();
      if (entry.isDirectory()) continue;
      entry.getName(name, sizeof(name));
      if (endsWith(name, PART_SUFFIX)) stale.emplace_back(name);
    }
  }
  for (const std::string& name : stale) {
    const std::string path = std::string(folder) + "/" + name;
    if (Storage.remove(path.c_str())) LOG_INF("LSND", "removed partial %s", path.c_str());
  }
}

void LocalSendReceiver::Store::stopHashing() {
  if (hashing) mbedtls_sha256_free(&sha);
  hashing = false;
}
