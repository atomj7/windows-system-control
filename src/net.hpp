#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
inline constexpr Socket kInvalidSocket = INVALID_SOCKET;
inline void close_socket(Socket socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
inline constexpr Socket kInvalidSocket = -1;
inline void close_socket(Socket socket) {
  if (socket >= 0) ::close(socket);
}
#endif

bool net_startup();
void net_shutdown();
Socket connect_local(uint16_t port, int timeout_ms);
bool send_all(Socket socket, const uint8_t* data, size_t size);
bool recv_full(Socket socket, uint8_t* data, size_t size, int timeout_ms);
bool recv_some(Socket socket, uint8_t* data, size_t capacity, size_t& got, int timeout_ms);
