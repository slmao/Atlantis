#pragma once

#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <span>

// Plan 0055 P1 (ADR-0106 D7, Spec 0055 ruling Q6): Atlantis Remote's private
// OS socket layer. Like the Vulkan Backend's WSI code
// (src/vulkan_backend/src/wsi/), it is one private header with one
// translation unit per OS (socket_win32.cpp: Winsock; socket_posix.cpp: BSD
// sockets for Android), chosen by CMake. No OS type appears here or in any
// public Remote header: a socket is an opaque integer handle. Remote does not
// depend on Atlantis Platform.
//
// Loopback IPv4 TCP only. Every socket this layer returns is non-blocking and
// has Nagle's algorithm off (small request/response lines). Not thread-safe;
// one thread per socket (ADR-0004: the frame thread on the server).
namespace atlantis::remote::os {

enum class SocketError {
  InitializationFailed,  // the OS socket library could not start (Winsock)
  CreateFailed,
  BindFailed,  // the port is taken, or not bindable
  ListenFailed,
  ConnectFailed,  // nothing listening, refused, or timed out
  OptionFailed,   // a socket option or non-blocking mode could not be set
};

// An owned socket; closed on destruction. Move-only.
class Socket {
 public:
  Socket() noexcept = default;
  explicit Socket(std::intptr_t handle) noexcept : handle_(handle) {}
  ~Socket();
  Socket(Socket&& other) noexcept : handle_(other.release()) {}
  Socket& operator=(Socket&& other) noexcept;
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  [[nodiscard]] bool valid() const noexcept { return handle_ != kInvalid; }
  [[nodiscard]] std::intptr_t handle() const noexcept { return handle_; }
  std::intptr_t release() noexcept {
    const std::intptr_t handle = handle_;
    handle_ = kInvalid;
    return handle;
  }

  static constexpr std::intptr_t kInvalid = -1;

 private:
  std::intptr_t handle_ = kInvalid;
};

// A listening socket bound to 127.0.0.1:`port` (0: an ephemeral port).
[[nodiscard]] atlantis::Result<Socket, SocketError> listenLoopback(std::uint16_t port);
// The port a bound socket has (the ephemeral one, after listenLoopback(0)).
[[nodiscard]] std::uint16_t localPort(const Socket& socket);
// The next pending connection on a listening socket, without waiting; an
// invalid Socket when none is pending.
[[nodiscard]] Socket acceptPending(const Socket& listener);
// A connection to 127.0.0.1:`port`, waiting at most `timeoutMilliseconds`.
[[nodiscard]] atlantis::Result<Socket, SocketError> connectLoopback(std::uint16_t port, int timeoutMilliseconds);

struct IoResult {
  enum class Status { Ok, WouldBlock, Closed };
  Status status = Status::Ok;
  std::size_t bytes = 0;  // transferred, when Ok
};
// Non-blocking. Closed covers an orderly close by the peer and any error.
[[nodiscard]] IoResult send(const Socket& socket, std::span<const char> data);
[[nodiscard]] IoResult receive(const Socket& socket, std::span<char> buffer);

// Waits up to `timeoutMilliseconds` (0: just checks) for `socket` to be
// readable or closed. The client's only wait; the server never waits.
[[nodiscard]] bool waitReadable(const Socket& socket, int timeoutMilliseconds);

// This process's id, for the session file (Plan 0055 P4).
[[nodiscard]] std::uint32_t currentProcessId();

}  // namespace atlantis::remote::os
