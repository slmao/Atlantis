#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/remote/session_file.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

// Plan 0055 P1 (ADR-0106 D1, D7): the server half of Atlantis Remote, linked
// by the atlantis_runtime executable only.
namespace atlantis::remote {

// A fresh session token: 128 bits from std::random_device, as 32 lowercase
// hex digits (P4).
[[nodiscard]] std::string generateToken();

// Writes `session` to `path` -- creating its directory -- through a
// temporary file renamed over it, so a reader never sees a partial file (P4).
[[nodiscard]] atlantis::Result<std::monostate, SessionFileError> writeSessionFile(const std::filesystem::path& path,
                                                                                  const SessionInfo& session);
// Removes the file, if present; errors are ignored (clean shutdown, P4).
void removeSessionFile(const std::filesystem::path& path) noexcept;

enum class ListenError {
  SocketUnavailable,  // the OS socket library could not start, or no socket could be made
  PortUnavailable,    // the port could not be bound on 127.0.0.1
};

[[nodiscard]] std::string_view toString(ListenError error) noexcept;

// P3's per-client limits; exceeding either disconnects the client.
struct ServerLimits {
  std::size_t maxLineBytes = 1024 * 1024;                // one request line
  std::size_t maxPendingWriteBytes = 64 * 1024 * 1024;  // queued output
};

// Plan 0055 P3 (ADR-0106 D1, D2): the server. It listens on 127.0.0.1 only,
// and the host polls it once after each runFrame(). Each poll accepts pending
// clients, reads every complete request line, answers what is answerable now,
// writes what it can, and drops clients that closed, misbehaved or overran a
// limit -- without ever waiting.
//
// Each client that completes its hello (token and protocol checked) gets its
// own connection from `openConnection` (the Runtime's InProcess endpoint,
// Spec 0054), so tickets, subscriptions and failure routing are exactly the
// endpoint's; the connection closes when the client goes, dropping its later
// failures (Plan 0054 J4) and its subscriptions.
//
// control.* requests go to `control` (ADR-0106 D3). A step is parked: its
// response is queued when the control completes it, after the frame it waited
// for, and written by the next poll -- in the order the steps were asked for.
// A parked step whose client has gone completes unanswered.
//
// Ownership: the server owns its sockets and every connection it opened; the
// borrowed endpoint behind `openConnection` and the borrowed `control` must
// outlive the server (a step the control completes after the server is gone
// is dropped safely).
// Not thread-safe: the host's frame thread only (ADR-0004).
class RemoteServer {
 public:
  using OpenConnection = std::function<std::unique_ptr<atlantis::connection::RuntimeConnection>()>;

  using Limits = ServerLimits;

  // `port` 0 picks an ephemeral port (read it back from session()).
  [[nodiscard]] static atlantis::Result<std::unique_ptr<RemoteServer>, ListenError> listen(
      std::uint16_t port, std::string token, atlantis::asset_system::AssetGuid scene, OpenConnection openConnection,
      atlantis::connection::RuntimeControl* control, ServerLimits limits = {});

  ~RemoteServer();
  RemoteServer(const RemoteServer&) = delete;
  RemoteServer& operator=(const RemoteServer&) = delete;

  // What the session file records for this server (P4).
  [[nodiscard]] SessionInfo session() const;

  void poll();

  // Clients currently connected (handshaken or not).
  [[nodiscard]] std::size_t clientCount() const noexcept;

  struct State;  // private to remote_server.cpp
  class Key {    // only listen() constructs a server
    friend class RemoteServer;
    Key() = default;
  };
  RemoteServer(Key, std::shared_ptr<State> state);

 private:
  std::shared_ptr<State> state_;
};

}  // namespace atlantis::remote
