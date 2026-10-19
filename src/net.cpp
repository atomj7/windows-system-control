#include "net.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#endif

namespace {

bool wait_fd(Socket socket, bool write, int timeout_ms) {
#ifdef _WIN32
  fd_set set;
  FD_ZERO(&set);
  FD_SET(socket, &set);
  timeval tv{};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  const int ready = select(0, write ? nullptr : &set, write ? &set : nullptr, nullptr, &tv);
  return ready > 0;
#else
  pollfd pfd{};
  pfd.fd = socket;
  pfd.events = write ? POLLOUT : POLLIN;
  return poll(&pfd, 1, timeout_ms) > 0;
#endif
}

}  // namespace

bool net_startup() {
#ifdef _WIN32
  WSADATA data{};
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
  std::signal(SIGPIPE, SIG_IGN);
  return true;
#endif
}

void net_shutdown() {
#ifdef _WIN32
  WSACleanup();
#endif
}

Socket connect_local(uint16_t port, int timeout_ms) {
  Socket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket == kInvalidSocket) return kInvalidSocket;

#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(socket, FIONBIO, &mode);
#else
  const int flags = fcntl(socket, F_GETFL, 0);
  fcntl(socket, F_SETFL, flags | O_NONBLOCK);
#endif

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  const int result = ::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (result != 0) {
#ifdef _WIN32
    const int error = WSAGetLastError();
    if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
      close_socket(socket);
      return kInvalidSocket;
    }
#else
    if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
      close_socket(socket);
      return kInvalidSocket;
    }
#endif
    if (!wait_fd(socket, true, timeout_ms)) {
      close_socket(socket);
      return kInvalidSocket;
    }
    int so_error = 0;
#ifdef _WIN32
    int length = sizeof(so_error);
#else
    socklen_t length = sizeof(so_error);
#endif
    getsockopt(socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&so_error), &length);
    if (so_error != 0) {
      close_socket(socket);
      return kInvalidSocket;
    }
  }

#ifdef _WIN32
  mode = 0;
  ioctlsocket(socket, FIONBIO, &mode);
#else
  fcntl(socket, F_SETFL, flags);
#endif
  return socket;
}

bool send_all(Socket socket, const uint8_t* data, size_t size) {
  size_t sent = 0;
  while (sent < size) {
#ifdef _WIN32
    const int n = ::send(socket, reinterpret_cast<const char*>(data + sent), static_cast<int>(size - sent), 0);
#else
    const ssize_t n = ::send(socket, data + sent, size - sent, 0);
#endif
    if (n <= 0) return false;
    sent += static_cast<size_t>(n);
  }
  return true;
}

bool recv_some(Socket socket, uint8_t* data, size_t capacity, size_t& got, int timeout_ms) {
  got = 0;
  if (!wait_fd(socket, false, timeout_ms)) return false;
#ifdef _WIN32
  const int n = ::recv(socket, reinterpret_cast<char*>(data), static_cast<int>(capacity), 0);
#else
  const ssize_t n = ::recv(socket, data, capacity, 0);
#endif
  if (n <= 0) return false;
  got = static_cast<size_t>(n);
  return true;
}

bool recv_full(Socket socket, uint8_t* data, size_t size, int timeout_ms) {
  size_t got_total = 0;
  while (got_total < size) {
    size_t got = 0;
    if (!recv_some(socket, data + got_total, size - got_total, got, timeout_ms)) return false;
    got_total += got;
  }
  return true;
}
