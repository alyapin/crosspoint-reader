#pragma once

#include <LocalSendConnection.h>

#include <cstddef>
#include <cstdint>

// Sockets for the LocalSend receiver: the TCP listener, one client at a time,
// and the multicast group. Plain BSD sockets, which lwIP provides on the device
// and the host provides in the simulator. Arduino's NetworkClient is not used
// because it reports a peer's orderly close as still connected.
class LocalSendNet {
 public:
  LocalSendNet() = default;
  LocalSendNet(const LocalSendNet&) = delete;
  LocalSendNet& operator=(const LocalSendNet&) = delete;
  ~LocalSendNet() { end(); }

  // TCP listener and multicast socket on `port`. Multicast joins and sends on
  // the interface with address `interfaceAddr` (network byte order). False
  // when either socket fails.
  bool begin(uint16_t port, uint32_t interfaceAddr);
  void end();

  // Takes a pending connection when none is open; ip receives its address.
  bool accept(char* ip, size_t ipCap);
  bool hasClient() const { return io.fd >= 0; }
  localsend::Io& client() { return io; }
  void closeClient();

  // Next datagram from the group: its length, 0 when none.
  int receive(char* buf, size_t cap);
  void sendMulticast(const char* data, size_t len);

 private:
  class ClientIo final : public localsend::Io {
   public:
    int fd = -1;
    int read(uint8_t* buf, size_t cap) override;
    bool write(const uint8_t* data, size_t len) override;
  };

  int listenFd = -1;
  int udpFd = -1;
  uint16_t port = 0;
  ClientIo io;
};
