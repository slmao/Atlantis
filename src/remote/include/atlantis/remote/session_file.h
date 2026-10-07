#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// Plan 0055 P4 (ADR-0106 D1): how a client finds a listening Runtime. The
// Runtime writes one JSON object to its session file once it listens:
//   {"protocol":"atlantis.remote/1","port":<n>,"token":"<32 hex>","pid":<n>,"scene":"<guid>"}
// The token is the per-run secret a client presents in its hello. Pure value
// functions here; the file operations are each half's (remote_server.h writes,
// remote_client.h reads).
namespace atlantis::remote {

// The one wire protocol this build speaks (P2).
inline constexpr std::string_view kProtocol = "atlantis.remote/1";

struct SessionInfo {
  std::uint16_t port = 0;
  std::string token;  // 32 lowercase hex digits (128 bits)
  std::uint32_t pid = 0;
  atlantis::asset_system::AssetGuid scene;
  friend bool operator==(const SessionInfo&, const SessionInfo&) = default;
};

enum class SessionFileError {
  NotFound,       // no file at the resolved path
  Unreadable,     // the file exists but could not be read
  Malformed,      // not a session object
  WrongProtocol,  // written by a Runtime speaking another protocol
  WriteFailed,    // the Runtime could not write or replace it
};

[[nodiscard]] std::string_view toString(SessionFileError error) noexcept;

[[nodiscard]] std::string formatSession(const SessionInfo& session);
[[nodiscard]] atlantis::Result<SessionInfo, SessionFileError> parseSession(std::string_view text);

// `./.atlantis/runtime.session.json`, relative to the working directory.
[[nodiscard]] std::filesystem::path defaultSessionPath();

}  // namespace atlantis::remote
