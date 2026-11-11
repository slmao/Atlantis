#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/json.h>
#include <atlantis/result.h>

#include <cstddef>
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

inline constexpr std::uint32_t kPreviewDimension = 256;
inline constexpr std::size_t kPreviewPngMaxBytes = 512 * 1024;
inline constexpr std::string_view kPreviewMimeType = "image/png";

enum class PreviewError : std::uint8_t {
  Unsupported, Unavailable, Busy, CatalogUnavailable, UnknownAsset, NotMesh, TooLarge, InvalidMesh, RenderFailed,
};
[[nodiscard]] std::string_view toString(PreviewError error) noexcept;

struct MeshPreview {
  asset_system::AssetGuid guid;
  std::uint32_t width = kPreviewDimension;
  std::uint32_t height = kPreviewDimension;
  std::vector<std::uint8_t> png;
  friend bool operator==(const MeshPreview&, const MeshPreview&) = default;
};
using MeshPreviewResult = Result<MeshPreview, PreviewError>;

// Shared structured payload for Remote and CLI. These functions borrow only
// for the call; results own their contents. Safe on distinct values. Nullopt
// means malformed or out of bounds, not an asset refusal. Only the PNG header
// is validated here; the consumer owns complete image decoding.
[[nodiscard]] std::optional<json::Value> encodeMeshPreview(const MeshPreview& image,
                                                          const asset_system::AssetGuid& requested);
[[nodiscard]] std::optional<MeshPreview> decodeMeshPreview(const json::Value& payload,
                                                          const asset_system::AssetGuid& requested);

enum class MeshMaterialPreviewError : std::uint8_t {
  Unsupported, Unavailable, Busy, CatalogUnavailable, UnknownMesh, UnknownMaterial, NotMesh, NotMaterial,
  InvalidMesh, InvalidMaterial, UnsupportedMaterial, DependencyUnavailable, IncompatiblePair, TooLarge, RenderFailed,
};
[[nodiscard]] std::string_view toString(MeshMaterialPreviewError error) noexcept;

struct MeshMaterialPreview {
  asset_system::AssetGuid meshGuid;
  asset_system::AssetGuid materialGuid;
  std::uint32_t width = kPreviewDimension;
  std::uint32_t height = kPreviewDimension;
  std::vector<std::uint8_t> png;
  friend bool operator==(const MeshMaterialPreview&, const MeshMaterialPreview&) = default;
};
using MeshMaterialPreviewResult = Result<MeshMaterialPreview, MeshMaterialPreviewError>;

// Same bounded PNG payload as a geometry preview, with both identities.
// Encoding and decoding borrow inputs only for the call and return owned values.
[[nodiscard]] std::optional<json::Value> encodeMeshMaterialPreview(
    const MeshMaterialPreview& image, const asset_system::AssetGuid& meshGuid,
    const asset_system::AssetGuid& materialGuid);
[[nodiscard]] std::optional<MeshMaterialPreview> decodeMeshMaterialPreview(
    const json::Value& payload, const asset_system::AssetGuid& meshGuid, const asset_system::AssetGuid& materialGuid);

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
struct ImportId {
  asset_system::AssetGuid value;
  friend bool operator==(const ImportId&, const ImportId&) = default;
};
enum class ImportMode : std::uint8_t { Register, Resume };
enum class ImportError : std::uint8_t {
  Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, NameConflict, ActivationRequired,
  NothingToResume, UnknownJob, Unsupported,
};
enum class ImportStage : std::uint8_t { Source, Pipeline, Validation, Registration, Publication };
enum class ImportFailureCode : std::uint8_t {
  InvalidSource, LaunchFailed, BuildFailed, InvalidOutput, IdentityConflict, RegisterFailed, PublishFailed, TimedOut,
};
enum class RegistrationState : std::uint8_t { NotCommitted, Committed, Unknown };
[[nodiscard]] std::string_view toString(ImportMode value) noexcept;
[[nodiscard]] std::string_view toString(ImportError value) noexcept;
[[nodiscard]] std::string_view toString(ImportStage value) noexcept;
[[nodiscard]] std::string_view toString(ImportFailureCode value) noexcept;
[[nodiscard]] std::string_view toString(RegistrationState value) noexcept;
[[nodiscard]] bool validImportName(std::string_view name) noexcept;
[[nodiscard]] bool validFailure(ImportStage stage, ImportFailureCode code) noexcept;
struct ImportFailure {
  ImportStage stage;
  ImportFailureCode code;
  std::string diagnostic;
  std::optional<std::uint32_t> exitCode;
  RegistrationState registration = RegistrationState::NotCommitted;
  std::optional<asset_system::AssetGuid> registeredRootGuid;
  friend bool operator==(const ImportFailure&, const ImportFailure&) = default;
};
struct ImportJob {
  ImportId id;
  std::string name;
  ImportMode mode;
  RebuildState state = RebuildState::Running;
  std::optional<asset_system::AssetGuid> rootGuid;
  std::optional<GenerationId> generation;
  std::optional<ImportFailure> failure;
  friend bool operator==(const ImportJob&, const ImportJob&) = default;
};
struct ImportStatus {
  std::optional<GenerationId> activeGeneration;
  std::optional<ImportJob> job;
  friend bool operator==(const ImportStatus&, const ImportStatus&) = default;
};
using ImportStartResult = Result<ImportJob, ImportError>;
using ImportStatusResult = Result<ImportStatus, ImportError>;
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
  // Spec 0091 / ADR-0151: a synchronous geometry still from this Runtime's
  // catalog snapshot. Precondition: guid is non-nil. No World or file changes.
  // Remote failures use Unavailable as a sentinel; check the session first.
  [[nodiscard]] virtual assets::MeshPreviewResult previewMesh(const asset_system::AssetGuid&) {
    return assets::MeshPreviewResult::Err(assets::PreviewError::Unsupported);
  }
  // Spec 0092 / ADR-0152: a synchronous still of a cooked mesh and material.
  // Both GUIDs must be non-nil. Same ownership and Remote failure rules as previewMesh.
  [[nodiscard]] virtual assets::MeshMaterialPreviewResult previewMeshMaterial(
      const asset_system::AssetGuid&, const asset_system::AssetGuid&) {
    return assets::MeshMaterialPreviewResult::Err(assets::MeshMaterialPreviewError::Unsupported);
  }
  // Spec 0085. Accepts a non-nil catalog GUID and returns an admission receipt.
  // Remote failures use Unavailable as a sentinel; check the session first.
  [[nodiscard]] virtual assets::RebuildStartResult rebuild(const asset_system::AssetGuid&) {
    return assets::RebuildStartResult::Err(assets::RebuildError::Unavailable);
  }
  [[nodiscard]] virtual assets::RebuildStatusResult rebuildStatus(std::optional<assets::RebuildId>) const {
    return assets::RebuildStatusResult::Err(assets::RebuildError::Unavailable);
  }
  [[nodiscard]] virtual assets::ImportStartResult importAsset(std::string_view, assets::ImportMode) {
    return assets::ImportStartResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::ImportStatusResult importStatus(std::optional<assets::ImportId>) const {
    return assets::ImportStatusResult::Err(assets::ImportError::Unavailable);
  }
};
}  // namespace atlantis::connection
