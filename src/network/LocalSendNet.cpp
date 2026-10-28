#include "LocalSendNet.h"

#include <Logging.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0  // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

namespace {
// LocalSend's default group 224.0.0.167; some Android devices accept only 224.0.0.0/24.
constexpr uint32_t MULTICAST_GROUP = (224u << 24) | 167u;
// Bounds a response write to a sender that stopped reading.
constexpr int SEND_TIMEOUT_S = 5;

void setNonBlocking(const int fd, const bool on) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK));
}

void enable(const int fd, const int level, const int option) {
  const int one = 1;
  setsockopt(fd, level, option, &one, sizeof(one));
}

sockaddr_in anyAddress(const uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  return addr;
}
}  // namespace

bool LocalSendNet::begin(const uint16_t newPort, const uint32_t interfaceAddr) {
  end();
  port = newPort;
  sockaddr_in addr = anyAddress(port);

  listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) {
    LOG_ERR("LSND", "TCP socket failed: %d", errno);
    return false;
  }
  enable(listenFd, SOL_SOCKET, SO_REUSEADDR);
  if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listenFd, 2) != 0) {
    LOG_ERR("LSND", "TCP listen on %u failed: %d", port, errno);
    end();
    return false;
  }
  setNonBlocking(listenFd, true);

  udpFd = socket(AF_INET, SOCK_DGRAM, 0);
  if (udpFd < 0) {
    LOG_ERR("LSND", "UDP socket failed: %d", errno);
    end();
    return false;
  }
  enable(udpFd, SOL_SOCKET, SO_REUSEADDR);
#ifdef SO_REUSEPORT
  enable(udpFd, SOL_SOCKET, SO_REUSEPORT);  // the simulator host may run LocalSend itself
#endif
  // Name the interface: with several (AP + STA, a host's VPN) the default
  // multicast route may not be the Wi-Fi network.
  ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = htonl(MULTICAST_GROUP);
  mreq.imr_interface.s_addr = interfaceAddr;
  in_addr outgoing{};
  outgoing.s_addr = interfaceAddr;
  if (bind(udpFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      setsockopt(udpFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0 ||
      setsockopt(udpFd, IPPROTO_IP, IP_MULTICAST_IF, &outgoing, sizeof(outgoing)) != 0) {
    LOG_ERR("LSND", "multicast join on %u failed: %d", port, errno);
    end();
    return false;
  }
  setNonBlocking(udpFd, true);
  return true;
}

void LocalSendNet::end() {
  closeClient();
  if (listenFd >= 0) close(listenFd);
  if (udpFd >= 0) close(udpFd);
  listenFd = -1;
  udpFd = -1;
}

bool LocalSendNet::accept(char* ip, const size_t ipCap) {
  if (listenFd < 0 || io.fd >= 0) return false;
  sockaddr_in peer{};
  socklen_t len = sizeof(peer);
  const int fd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
  if (fd < 0) return false;
  // BSD hosts let accepted sockets inherit O_NONBLOCK; sends block (bounded by
  // SO_SNDTIMEO) and reads pass MSG_DONTWAIT instead.
  setNonBlocking(fd, false);
  timeval timeout{};
  timeout.tv_sec = SEND_TIMEOUT_S;
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  enable(fd, IPPROTO_TCP, TCP_NODELAY);
#ifdef SO_NOSIGPIPE
  enable(fd, SOL_SOCKET, SO_NOSIGPIPE);
#endif
  if (!inet_ntop(AF_INET, &peer.sin_addr, ip, static_cast<socklen_t>(ipCap))) ip[0] = 0;
  io.fd = fd;
  return true;
}

void LocalSendNet::closeClient() {
  if (io.fd >= 0) close(io.fd);
  io.fd = -1;
}

int LocalSendNet::receive(char* buf, const size_t cap) {
  if (udpFd < 0 || cap == 0) return 0;
  const ssize_t n = recvfrom(udpFd, buf, cap, MSG_DONTWAIT, nullptr, nullptr);
  return n > 0 ? static_cast<int>(n) : 0;
}

void LocalSendNet::sendMulticast(const char* data, const size_t len) {
  if (udpFd < 0) return;
  sockaddr_in group = anyAddress(port);
  group.sin_addr.s_addr = htonl(MULTICAST_GROUP);
  if (sendto(udpFd, data, len, 0, reinterpret_cast<sockaddr*>(&group), sizeof(group)) < 0) {
    LOG_DBG("LSND", "announce failed: %d", errno);
  }
}

int LocalSendNet::ClientIo::read(uint8_t* buf, const size_t cap) {
  const ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);
  if (n > 0) return static_cast<int>(n);
  if (n == 0) return -1;  // orderly close
  return (errno == EWOULDBLOCK || errno == EAGAIN) ? 0 : -1;
}

bool LocalSendNet::ClientIo::write(const uint8_t* data, size_t len) {
  while (len > 0) {
    const ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
    if (n <= 0) return false;
    data += n;
    len -= static_cast<size_t>(n);
  }
  return true;
}
