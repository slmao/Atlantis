#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/remote/session_file.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Plan 0055 P1 (ADR-0106 D2, D7): the client half of Atlantis Remote, linked
// by the `atlantis` executable (and tests).
namespace atlantis::remote {

// P4's resolution order: `explicitPath` (`atlantis --session <path>`), else
// `environmentValue` (ATLANTIS_SESSION; nullptr or empty when unset), else
// defaultSessionPath().
[[nodiscard]] std::filesystem::path resolveSessionPath(const std::optional<std::string>& explicitPath,
                                                       const char* environmentValue);
[[nodiscard]] atlantis::Result<SessionInfo, SessionFileError> readSessionFile(const std::filesystem::path& path);

enum class RemoteError {
  ConnectFailed,  // nothing listening on the session's port
  BadToken,       // the Runtime refused the session token
  WrongProtocol,  // the Runtime speaks another protocol
  Disconnected,   // the connection closed
  Timeout,        // no response in time
  ProtocolError,  // a response that does not decode, or a refused request
};

[[nodiscard]] std::string_view toString(RemoteError error) noexcept;

struct RemoteOptions {
  // Plan 0055 J4: called repeatedly while a call waits for its response, so
  // a single-threaded test can run frames and RemoteServer::poll() meanwhile.
  // Production passes none and waits on the socket.
  std::function<void()> whileWaiting;
  int connectTimeoutMilliseconds = 5000;
  // How long one response may take. A response comes at the Runtime's next
  // frame boundary, but a first frame that realizes a large scene's
  // materials can take long.
  int responseTimeoutMilliseconds = 120000;
};

// One attached session: the socket, a RuntimeConnection over it (Spec 0055
// R2: RemoteConnection implements the interface; ADR-0105 D1: the transport
// does not shape it), a RuntimeControl over it (ADR-0106 D3; its step()
// blocks until the Runtime completes the step and calls `done` before
// returning), and the batch queries below.
//
// RuntimeConnection has no transport-error channel. After a transport
// failure every call returns an empty value -- false, no entities,
// UnknownEntity, ticket 0, UnknownSubscription -- and failure() names what
// happened; a client checks it after a command. The schema is fetched once at
// connect and owned here, valid while the session lives (Spec 0054 ruling Q8,
// K1).
//
// Not thread-safe; one thread. Each call blocks until its answer arrives (at
// the Runtime's next frame boundary, ADR-0106 D1).
class RemoteSession {
 public:
  ~RemoteSession();
  RemoteSession(const RemoteSession&) = delete;
  RemoteSession& operator=(const RemoteSession&) = delete;

  [[nodiscard]] atlantis::connection::RuntimeConnection& connection() noexcept;
  [[nodiscard]] atlantis::connection::RuntimeControl& control() noexcept;
  [[nodiscard]] const atlantis::asset_system::AssetGuid& scene() const noexcept;
  [[nodiscard]] std::optional<RemoteError> failure() const noexcept;

  // Pipelined queries (Spec 0055 non-functional latency; Plan 0055 P9): all
  // requests are sent before any answer is awaited, so the Runtime answers
  // them in one frame boundary. The answers are in input order and equal
  // what the one-at-a-time calls would give in that boundary.
  [[nodiscard]] std::vector<atlantis::Result<std::vector<schema::TypeId>, atlantis::world::access::AccessError>>
  listComponents(std::span<const atlantis::asset_system::EntityGuid> entities);
  [[nodiscard]] std::vector<atlantis::Result<atlantis::world::access::PropertyValue, atlantis::world::access::AccessError>>
  getProperties(std::span<const atlantis::world::access::PropertyAddress> addresses);

  struct State;  // private to remote_client.cpp
  explicit RemoteSession(std::unique_ptr<State> state);

 private:
  std::unique_ptr<State> state_;
};

// Connects to the session's port, says hello with its token, and fetches the
// schema.
[[nodiscard]] atlantis::Result<std::unique_ptr<RemoteSession>, RemoteError> connectRemote(const SessionInfo& session,
                                                                                         RemoteOptions options = {});

}  // namespace atlantis::remote
