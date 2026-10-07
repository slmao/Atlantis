// BSD-socket implementation of Atlantis Remote's private socket layer
// (socket.h), for Android. Compiled only there (src/remote/CMakeLists.txt);
// the only Remote translation unit that includes a POSIX socket header.

#include "socket.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

namespace atlantis::remote::os {

namespace {

[[nodiscard]] int native(const Socket& socket) noexcept { return static_cast<int>(socket.handle()); }

[[nodiscard]] bool configure(int s) {
  const int flags = fcntl(s, F_GETFL, 0);
  if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0) return false;
  int noDelay = 1;
  return setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay)) == 0;
}

[[nodiscard]] sockaddr_in loopback(std::uint16_t port) {
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  return address;
}

[[nodiscard]] bool wouldBlock() noexcept { return errno == EAGAIN || errno == EWOULDBLOCK; }

}  // namespace

Socket::~Socket() {
  if (valid()) ::close(static_cast<int>(handle_));
}

Socket& Socket::operator=(Socket&& other) noexcept {
  if (this != &other) {
    if (valid()) ::close(static_cast<int>(handle_));
    handle_ = other.release();
  }
  return *this;
}

atlantis::Result<Socket, SocketError> listenLoopback(std::uint16_t port) {
  using ResultT = atlantis::Result<Socket, SocketError>;
  const int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return ResultT::Err(SocketError::CreateFailed);
  Socket owned(s);
  const sockaddr_in address = loopback(port);
  if (::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
    return ResultT::Err(SocketError::BindFailed);
  }
  if (::listen(s, SOMAXCONN) != 0) return ResultT::Err(SocketError::ListenFailed);
  const int flags = fcntl(s, F_GETFL, 0);
  if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) != 0) return ResultT::Err(SocketError::OptionFailed);
  return ResultT::Ok(std::move(owned));
}

std::uint16_t localPort(const Socket& socket) {
  sockaddr_in address{};
  socklen_t length = sizeof(address);
  if (getsockname(native(socket), reinterpret_cast<sockaddr*>(&address), &length) != 0) return 0;
  return ntohs(address.sin_port);
}

Socket acceptPending(const Socket& listener) {
  const int s = ::accept(native(listener), nullptr, nullptr);
  if (s < 0) return Socket();
  Socket owned(s);
  if (!configure(s)) return Socket();
  return owned;
}

atlantis::Result<Socket, SocketError> connectLoopback(std::uint16_t port, int timeoutMilliseconds) {
  using ResultT = atlantis::Result<Socket, SocketError>;
  const int s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s < 0) return ResultT::Err(SocketError::CreateFailed);
  Socket owned(s);
  if (!configure(s)) return ResultT::Err(SocketError::OptionFailed);
  const sockaddr_in address = loopback(port);
  if (::connect(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 && errno != EINPROGRESS) {
    return ResultT::Err(SocketError::ConnectFailed);
  }
  fd_set writable;
  FD_ZERO(&writable);
  FD_SET(s, &writable);
  timeval timeout{timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000};
  if (::select(s + 1, nullptr, &writable, nullptr, &timeout) <= 0) return ResultT::Err(SocketError::ConnectFailed);
  int error = 0;
  socklen_t length = sizeof(error);
  if (getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0) {
    return ResultT::Err(SocketError::ConnectFailed);
  }
  return ResultT::Ok(std::move(owned));
}

IoResult send(const Socket& socket, std::span<const char> data) {
  // MSG_NOSIGNAL: a closed peer is an error result, never SIGPIPE.
  const ssize_t sent = ::send(native(socket), data.data(), data.size(), MSG_NOSIGNAL);
  if (sent >= 0) return IoResult{IoResult::Status::Ok, static_cast<std::size_t>(sent)};
  if (wouldBlock()) return IoResult{IoResult::Status::WouldBlock, 0};
  return IoResult{IoResult::Status::Closed, 0};
}

IoResult receive(const Socket& socket, std::span<char> buffer) {
  const ssize_t received = ::recv(native(socket), buffer.data(), buffer.size(), 0);
  if (received > 0) return IoResult{IoResult::Status::Ok, static_cast<std::size_t>(received)};
  if (received == 0) return IoResult{IoResult::Status::Closed, 0};
  if (wouldBlock()) return IoResult{IoResult::Status::WouldBlock, 0};
  return IoResult{IoResult::Status::Closed, 0};
}

bool waitReadable(const Socket& socket, int timeoutMilliseconds) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(native(socket), &readable);
  timeval timeout{timeoutMilliseconds / 1000, (timeoutMilliseconds % 1000) * 1000};
  return ::select(native(socket) + 1, &readable, nullptr, nullptr, &timeout) > 0;
}

std::uint32_t currentProcessId() { return static_cast<std::uint32_t>(::getpid()); }

}  // namespace atlantis::remote::os
