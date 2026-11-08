#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::connection {
namespace assets {
enum class AssetType : std::uint8_t { Mesh, Texture, Material, Scene, Environment };
enum class AssetError : std::uint8_t { CatalogUnavailable, UnknownAsset };
[[nodiscard]] std::string_view toString(AssetType type) noexcept;
[[nodiscard]] std::string_view toString(AssetError error) noexcept;
struct AssetSummary {
  asset_system::AssetGuid guid;
  AssetType type = AssetType::Mesh;
  std::string source;
  friend bool operator==(const AssetSummary&, const AssetSummary&) = default;
};
struct AssetDetails {
  asset_system::AssetGuid guid;
  AssetType type = AssetType::Mesh;
  std::string source;
  std::uint32_t artifactSchema = 0;
  std::optional<std::uint32_t> sourceSchema;
  std::string tool;
  std::vector<asset_system::AssetGuid> dependencies;
  friend bool operator==(const AssetDetails&, const AssetDetails&) = default;
};

// Canonical GUID values in separate domains from an asset identity.
struct RebuildId {
  asset_system::AssetGuid value;
  friend bool operator==(const RebuildId&, const RebuildId&) = default;
};
struct GenerationId {
  asset_system::AssetGuid value;
  friend bool operator==(const GenerationId&, const GenerationId&) = default;
};
enum class RebuildError : std::uint8_t {
  Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownAsset, UnsupportedProducer,
  UnknownJob, Unsupported,
};
enum class RebuildState : std::uint8_t { Running, Succeeded, Failed };
enum class RebuildStage : std::uint8_t { Launch, Pipeline, Validation, Publication };
enum class RebuildFailureCode : std::uint8_t {
  LaunchFailed, BuildFailed, InvalidOutput, IdentityChanged, PublishFailed, TimedOut,
};
[[nodiscard]] std::string_view toString(RebuildError value) noexcept;
[[nodiscard]] std::string_view toString(RebuildState value) noexcept;
[[nodiscard]] std::string_view toString(RebuildStage value) noexcept;
[[nodiscard]] std::string_view toString(RebuildFailureCode value) noexcept;
[[nodiscard]] bool validFailure(RebuildStage stage, RebuildFailureCode code) noexcept;
struct RebuildFailure {
  RebuildStage stage;
  RebuildFailureCode code;
  std::string diagnostic;
  std::optional<std::uint32_t> exitCode;
  friend bool operator==(const RebuildFailure&, const RebuildFailure&) = default;
};
struct RebuildJob {
  RebuildId id;
  asset_system::AssetGuid asset;
  RebuildState state = RebuildState::Running;
  std::optional<GenerationId> generation;
  std::optional<RebuildFailure> failure;
  friend bool operator==(const RebuildJob&, const RebuildJob&) = default;
};
struct RebuildStatus {
  std::optional<GenerationId> activeGeneration;
  std::optional<RebuildJob> job;
  friend bool operator==(const RebuildStatus&, const RebuildStatus&) = default;
};
using RebuildStartResult = Result<RebuildJob, RebuildError>;
using RebuildStatusResult = Result<RebuildStatus, RebuildError>;
}  // namespace assets

// Spec 0081 / ADR-0143: read-only cooked-catalog facts, not GPU residency.
// Not thread-safe: the connection's owning thread (in-process host queries run
// on the frame thread between frames). Connections borrow their host and must
// close before it. Results are independent owned copies. For either query, a
// Remote transport failure returns Err(CatalogUnavailable) as a sentinel;
// RemoteSession::failure() is authoritative and must be checked first.
class AssetConnection {
 public:
  virtual ~AssetConnection() = default;
  [[nodiscard]] virtual Result<std::vector<assets::AssetSummary>, assets::AssetError> listAssets() const = 0;
  // Precondition: guid is non-nil.
  [[nodiscard]] virtual Result<assets::AssetDetails, assets::AssetError> getAsset(
      const asset_system::AssetGuid& guid) const = 0;
  // Spec 0085. Accepts a non-nil catalog GUID and returns an admission receipt.
  // Remote failures use Unavailable as a sentinel; check the session first.
  [[nodiscard]] virtual assets::RebuildStartResult rebuild(const asset_system::AssetGuid&) {
    return assets::RebuildStartResult::Err(assets::RebuildError::Unavailable);
  }
  [[nodiscard]] virtual assets::RebuildStatusResult rebuildStatus(std::optional<assets::RebuildId>) const {
    return assets::RebuildStatusResult::Err(assets::RebuildError::Unavailable);
  }
};
}  // namespace atlantis::connection
