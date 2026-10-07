// Winsock implementation of Atlantis Remote's private socket layer
// (socket.h). Compiled only on Windows (src/remote/CMakeLists.txt); the only
// Remote translation unit that includes a Windows header.

#include "socket.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

namespace atlantis::remote::os {

namespace {

// Winsock must be started once per process before any socket call. The
// process-lifetime initialization is never cleaned up: WSACleanup at exit is
// not required, and a function-local static is the narrowest owner.
[[nodiscard]] bool ensureStarted() {
  static const bool started = [] {
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return started;
}

[[nodiscard]] SOCKET native(const Socket& socket) noexcept { return static_cast<SOCKET>(socket.handle()); }

[[nodiscard]] bool configure(SOCKET s) {
  u_long nonBlocking = 1;
  if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0) return false;
  BOOL noDelay = TRUE;
  return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay)) == 0;
}

[[nodiscard]] sockaddr_in loopback(std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return address;
}

}  // namespace

Socket::~Socket() {
  if (valid()) closesocket(static_cast<SOCKET>(handle_));
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    if (valid()) closesocket(static_cast<SOCKET>(handle_));
    handle_ = other.release();
  }
  return *this;
}

atlantis::Result<Socket, SocketError> listenLoopback(std::uint16_t port) {
  using ResultT = atlantis::Result<Socket, SocketError>;
  if (!ensureStarted()) return ResultT::Err(SocketError::InitializationFailed);
  const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return ResultT::Err(SocketError::CreateFailed);
  Socket owned(static_cast<std::intptr_t>(s));
  // Exclusive use: no other socket may bind the same address meanwhile.
  BOOL exclusive = TRUE;
  if (setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) !=
      0) {
    return ResultT::Err(SocketError::OptionFailed);
  }
  const sockaddr_in address = loopback(port);
  if (::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    return ResultT::Err(SocketError::BindFailed);
  }
  if (::listen(s, SOMAXCONN) != 0) return ResultT::Err(SocketError::ListenFailed);
  u_long nonBlocking = 1;
  if (ioctlsocket(s, FIONBIO, &nonBlocking) != 0) return ResultT::Err(SocketError::OptionFailed);
  return ResultT::Ok(std::move(owned));
}

std::uint16_t localPort(const Socket& socket) {
  sockaddr_in address{};
  int length = sizeof(address);
  if (getsockname(native(socket), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
  return ntohs(address.sin_port);
}

Socket acceptPending(const Socket& listener) {
  const SOCKET s = ::accept(native(listener), nullptr, nullptr);
  if (s == INVALID_SOCKET) return Socket();
  Socket owned(static_cast<std::intptr_t>(s));
  if (!configure(s)) return Socket();
  return owned;
}

atlantis::Result<Socket, SocketError> connectLoopback(std::uint16_t port, int timeoutMilliseconds) {
  using ResultT = atlantis::Result<Socket, SocketError>;
  if (!ensureStarted()) return ResultT::Err(SocketError::InitializationFailed);
  const SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return ResultT::Err(SocketError::CreateFailed);
  Socket owned(static_cast<std::intptr_t>(s));
  if (!configure(s)) return ResultT::Err(SocketError::OptionFailed);
  const sockaddr_in address = loopback(port);
  if (::connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 &&
      WSAGetLastError() != WSAEWOULDBLOCK) {
    return ResultT::Err(SocketError::ConnectFailed);
  }
  fd_set writable;
  FD_ZERO(&writable);
  FD_SET(s, &writable);
  fd_set failed;
  FD_ZERO(&failed);
  FD_SET(s, &failed);
  timeval timeout{timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000};
  if (::select(0, nullptr, &writable, &failed, &timeout) <= 0 || FD_ISSET(s, &failed)) {
    return ResultT::Err(SocketError::ConnectFailed);
  }
  return ResultT::Ok(std::move(owned));
}

IoResult send(const Socket& socket, std::span<const char> data) {
  const int size = static_cast<int>(data.size() > 0x40000000 ? 0x40000000 : data.size());
  const int sent = ::send(native(socket), data.data(), size, 0);
  if (sent >= 0) return IoResult{IoResult::Status::Ok, static_cast<std::size_t>(sent)};
  if (WSAGetLastError() == WSAEWOULDBLOCK) return IoResult{IoResult::Status::WouldBlock, 0};
  return IoResult{IoResult::Status::Closed, 0};
}

IoResult receive(const Socket& socket, std::span<char> buffer) {
  const int size = static_cast<int>(buffer.size() > 0x40000000 ? 0x40000000 : buffer.size());
  const int received = ::recv(native(socket), buffer.data(), size, 0);
  if (received > 0) return IoResult{IoResult::Status::Ok, static_cast<std::size_t>(received)};
  if (received == 0) return IoResult{IoResult::Status::Closed, 0};
  if (WSAGetLastError() == WSAEWOULDBLOCK) return IoResult{IoResult::Status::WouldBlock, 0};
  return IoResult{IoResult::Status::Closed, 0};
}

bool waitReadable(const Socket& socket, int timeoutMilliseconds) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(native(socket), &readable);
  timeval timeout{timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000};
  return ::select(0, &readable, nullptr, nullptr, &timeout) > 0;
}

std::uint32_t currentProcessId() { return static_cast<std::uint32_t>(GetCurrentProcessId()); }

}  // namespace atlantis::remote::os
