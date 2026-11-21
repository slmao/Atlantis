#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/json.h>
#include <atlantis/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

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
  std::optional<std::string> displayName{};
  std::optional<std::string> logicalPath{};
  bool isPrefab = false;
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
  std::optional<std::string> displayName{};
  std::optional<std::string> logicalPath{};
  bool isPrefab = false;
  friend bool operator==(const AssetDetails&, const AssetDetails&) = default;
};

inline constexpr std::size_t kStaticGroupNodeLimit = 63;
inline constexpr std::size_t kStaticGroupByteLimit = 64 * 1024;
enum class StaticGroupError : std::uint8_t {
  Unsupported, CatalogUnavailable, UnknownAsset, UnsupportedGroup, InvalidGroup,
};
struct StaticGroupRenderable {
  asset_system::AssetGuid mesh;
  std::optional<asset_system::AssetGuid> material;
  friend bool operator==(const StaticGroupRenderable&, const StaticGroupRenderable&) = default;
};
struct StaticGroupNode {
  std::optional<std::size_t> parent;
  std::array<float, 3> position;
  std::array<float, 3> eulerRadians;
  std::array<float, 3> scale;
  std::optional<StaticGroupRenderable> renderable;
  std::string name{};
  friend bool operator==(const StaticGroupNode&, const StaticGroupNode&) = default;
};
struct StaticGroup {
  asset_system::AssetGuid scene;
  std::string name;
  std::vector<StaticGroupNode> nodes;
  friend bool operator==(const StaticGroup&, const StaticGroup&) = default;
};
struct PrefabRevision {
  asset_system::AssetGuid prefab;
  std::string revision;
};
using PrefabRevisionResult = Result<PrefabRevision, StaticGroupError>;
using StaticGroupResult = Result<StaticGroup, StaticGroupError>;
[[nodiscard]] std::string_view toString(StaticGroupError error) noexcept;
[[nodiscard]] std::optional<StaticGroupError> staticGroupError(std::string_view name) noexcept;
[[nodiscard]] std::optional<json::Value> encodeStaticGroup(const StaticGroup& group,
                                                          const asset_system::AssetGuid& requested, bool prefab = false);
[[nodiscard]] std::optional<StaticGroup> decodeStaticGroup(const json::Value& payload,
                                                           const asset_system::AssetGuid& requested, bool prefab = false);

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

enum class RenameTextureError : std::uint8_t {
  Unsupported, Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownAsset,
  UnsupportedProducer, InvalidName, NameConflict, StaleSnapshot, Unchanged,
};
[[nodiscard]] std::string_view toString(RenameTextureError value) noexcept;
struct RenameTextureRequest {
  asset_system::AssetGuid asset;
  GenerationId expectedGeneration;
  std::string displayName;
};
using RenameTextureResult = Result<RebuildJob, RenameTextureError>;

enum class MoveTextureError : std::uint8_t {
  Unsupported, Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownAsset,
  UnsupportedProducer, InvalidPath, PathConflict, StaleSnapshot, Unchanged,
};
[[nodiscard]] std::string_view toString(MoveTextureError value) noexcept;
struct MoveTextureRequest {
  asset_system::AssetGuid asset;
  GenerationId expectedGeneration;
  std::string expectedLogicalPath;
  std::string destinationLogicalPath;
};
using MoveTextureResult = Result<RebuildJob, MoveTextureError>;

struct TextureRemovalCheckId {
  asset_system::AssetGuid value;
  friend bool operator==(const TextureRemovalCheckId&, const TextureRemovalCheckId&) = default;
};
enum class TextureRemovalError : std::uint8_t {
  Unsupported, Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownAsset,
  UnsupportedProducer, StaleCheck, UnknownCheck,
};
enum class TextureRemovalState : std::uint8_t { Checking, Ready, Referenced, CheckIncomplete, StaleCheck };
enum class TextureReferenceSlot : std::uint8_t { BaseColor, Normal, MetallicRoughness, Occlusion, Emissive };
inline constexpr std::size_t kTextureRemovalBlockerLimit = 16;
[[nodiscard]] std::string_view toString(TextureRemovalError value) noexcept;
[[nodiscard]] std::string_view toString(TextureRemovalState value) noexcept;
[[nodiscard]] std::string_view toString(TextureReferenceSlot value) noexcept;
struct TextureRemovalTarget {
  asset_system::AssetGuid asset;
  GenerationId expectedGeneration;
  friend bool operator==(const TextureRemovalTarget&, const TextureRemovalTarget&) = default;
};
struct TextureRemovalBlocker {
  asset_system::AssetGuid asset;
  std::string source;
  std::optional<TextureReferenceSlot> slot;
  friend bool operator==(const TextureRemovalBlocker&, const TextureRemovalBlocker&) = default;
};
struct TextureRemovalCheck {
  TextureRemovalCheckId id;
  TextureRemovalTarget target;
  TextureRemovalState state = TextureRemovalState::Checking;
  std::vector<TextureRemovalBlocker> blockers;
  bool truncated = false;
  bool incomplete = false;
  std::string diagnostic;
  friend bool operator==(const TextureRemovalCheck&, const TextureRemovalCheck&) = default;
};
using TextureRemovalCheckResult = Result<TextureRemovalCheck, TextureRemovalError>;
using RemoveTextureResult = Result<RebuildJob, TextureRemovalError>;
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
[[nodiscard]] bool validTextureLogicalPath(std::string_view path) noexcept;
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

struct TextureImportId {
  asset_system::AssetGuid value;
  friend bool operator==(const TextureImportId&, const TextureImportId&) = default;
};
struct TextureImportJob {
  TextureImportId id;
  std::string name;
  ImportMode mode;
  RebuildState state;
  std::optional<asset_system::AssetGuid> rootGuid;
  std::optional<GenerationId> generation;
  std::optional<ImportFailure> failure;
};
struct TextureImportStatus {
  std::optional<GenerationId> activeGeneration;
  std::optional<TextureImportJob> job;
  bool workspaceNeedsRepair = false;
};
using TextureImportStartResult = Result<TextureImportJob, ImportError>;
using TextureImportStatusResult = Result<TextureImportStatus, ImportError>;

enum class MaterialRecipeVersion : std::uint8_t { Basic, WithMaps, WithMetallicRoughness, WithOcclusion, WithCombinedMaps };

struct MaterialValues {
  asset_system::AssetGuid texture;
  std::array<float, 3> color{1, 1, 1};
  float metallic = 0;
  float roughness = 0.5f;
  std::optional<asset_system::AssetGuid> normalMap{};
  std::optional<asset_system::AssetGuid> emissiveTexture{};
  std::array<float, 3> emissiveFactor{};
  std::optional<asset_system::AssetGuid> metallicRoughnessMap{};
  std::optional<asset_system::AssetGuid> occlusionMap{};
  friend bool operator==(const MaterialValues&, const MaterialValues&) = default;
};
enum class MaterialFilter : std::uint8_t { Nearest, Linear };
enum class MaterialAddress : std::uint8_t { Repeat, ClampToEdge };
struct MaterialRecipe {
  MaterialValues values;
  MaterialFilter filter = MaterialFilter::Linear;
  MaterialAddress address = MaterialAddress::Repeat;
  friend bool operator==(const MaterialRecipe&, const MaterialRecipe&) = default;
};
struct MaterialRead {
  asset_system::AssetGuid material;
  MaterialRecipe recipe;
  bool editable = false;
  std::optional<GenerationId> activeGeneration;
  friend bool operator==(const MaterialRead&, const MaterialRead&) = default;
};
struct CreateMaterial { std::string name; MaterialValues values; };
struct CopyMaterial { std::string name; asset_system::AssetGuid material; MaterialValues values; };
struct EditMaterial {
  asset_system::AssetGuid material;
  std::optional<GenerationId> activeGeneration;
  MaterialValues expected;
  MaterialValues values;
};
struct ResumeMaterial { asset_system::AssetGuid material; };
using MaterialSave = std::variant<CreateMaterial, CopyMaterial, EditMaterial, ResumeMaterial>;
enum class MaterialMode : std::uint8_t { Create, Copy, Edit, Resume };
struct MaterialTarget {
  MaterialMode mode;
  std::string name;
  std::optional<asset_system::AssetGuid> material;
  friend bool operator==(const MaterialTarget&, const MaterialTarget&) = default;
};
struct MaterialJobId {
  asset_system::AssetGuid value;
  friend bool operator==(const MaterialJobId&, const MaterialJobId&) = default;
};
enum class MaterialError : std::uint8_t {
  Unsupported, Unavailable, Busy, WorkspaceNeedsRepair, CatalogUnavailable, UnknownMaterial,
  NotMaterial, InvalidMaterial, UnsupportedMaterial, DependencyUnavailable, InvalidRecipe,
  UnknownMesh, NotMesh, InvalidMesh, IncompatiblePair, TooLarge, RenderFailed,
  NameConflict, StaleEdit, ActivationRequired, NothingToResume, Unchanged, UnknownJob, NotEditable,
};
enum class MaterialStage : std::uint8_t { Source, Pipeline, Validation, SourceCommit, Publication };
enum class MaterialFailureCode : std::uint8_t {
  InvalidSource, InvalidRecipe, DependencyUnavailable, NotEditable, NameConflict, StaleEdit, ActivationRequired,
  NothingToResume, Unchanged, LaunchFailed, BuildFailed, InvalidOutput, IdentityConflict, CommitFailed,
  PublishFailed, TimedOut,
};
struct MaterialFailure {
  MaterialStage stage;
  MaterialFailureCode code;
  std::string diagnostic;
  std::optional<std::uint32_t> exitCode;
  RegistrationState sourceState = RegistrationState::NotCommitted;
  std::optional<asset_system::AssetGuid> material;
  friend bool operator==(const MaterialFailure&, const MaterialFailure&) = default;
};
struct MaterialJob {
  MaterialJobId id;
  MaterialTarget target;
  RebuildState state = RebuildState::Running;
  std::optional<asset_system::AssetGuid> material;
  std::optional<GenerationId> generation;
  std::optional<MaterialFailure> failure;
  friend bool operator==(const MaterialJob&, const MaterialJob&) = default;
};
struct MaterialStatus {
  std::optional<GenerationId> activeGeneration;
  std::optional<MaterialJob> job;
  bool workspaceNeedsRepair = false;
  friend bool operator==(const MaterialStatus&, const MaterialStatus&) = default;
};
struct MaterialDraftPreview {
  asset_system::AssetGuid mesh;
  MaterialRecipe recipe;
  std::vector<std::uint8_t> png;
};
using MaterialReadResult = Result<MaterialRead, MaterialError>;
using MaterialSaveResult = Result<MaterialJob, MaterialError>;
using MaterialStatusResult = Result<MaterialStatus, MaterialError>;
using MaterialDraftPreviewResult = Result<MaterialDraftPreview, MaterialError>;
[[nodiscard]] std::string_view toString(MaterialMode value) noexcept;
[[nodiscard]] std::string_view toString(MaterialError value) noexcept;
[[nodiscard]] std::string_view toString(MaterialStage value) noexcept;
[[nodiscard]] std::string_view toString(MaterialFailureCode value) noexcept;
[[nodiscard]] bool basicMaterialValues(const MaterialValues& values) noexcept;
[[nodiscard]] bool representsMaterialValues(const MaterialValues& values, MaterialRecipeVersion version) noexcept;
[[nodiscard]] bool validMaterialValues(const MaterialValues& values) noexcept;
[[nodiscard]] bool validMaterialRecipe(const MaterialRecipe& recipe) noexcept;
[[nodiscard]] MaterialTarget materialTarget(const MaterialSave& request);
[[nodiscard]] json::Value encodeMaterialValues(const MaterialValues& values,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] std::optional<MaterialValues> decodeMaterialValues(const json::Value& value,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] json::Value encodeMaterialRecipe(const MaterialRecipe& recipe,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] std::optional<MaterialRecipe> decodeMaterialRecipe(const json::Value& value,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] json::Value encodeMaterialSave(const MaterialSave& input,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] std::optional<MaterialSave> decodeMaterialSave(const json::Value& value,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] json::Value encodeMaterialRead(const MaterialRead& value,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] std::optional<MaterialRead> decodeMaterialRead(const json::Value& value,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] json::Value encodeMaterialJob(const MaterialJob& value);
[[nodiscard]] std::optional<MaterialJob> decodeMaterialJob(const json::Value& value);
[[nodiscard]] json::Value encodeMaterialStatus(const MaterialStatus& value);
[[nodiscard]] std::optional<MaterialStatus> decodeMaterialStatus(const json::Value& value);
[[nodiscard]] std::optional<MaterialError> decodeMaterialError(const json::Value& value);
[[nodiscard]] std::optional<json::Value> encodeMaterialDraftPreview(const MaterialDraftPreview& image,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
[[nodiscard]] std::optional<MaterialDraftPreview> decodeMaterialDraftPreview(
    const json::Value& payload, const asset_system::AssetGuid& mesh, const MaterialRecipe& recipe,
    MaterialRecipeVersion version = MaterialRecipeVersion::Basic);
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
  // Spec 0130. Read from the same catalog as getAsset. Check RemoteSession
  // failure before using a transport failure's CatalogUnavailable sentinel.
  [[nodiscard]] virtual assets::StaticGroupResult staticGroup(const asset_system::AssetGuid&) const {
    return assets::StaticGroupResult::Err(assets::StaticGroupError::Unsupported);
  }
  [[nodiscard]] virtual assets::PrefabRevisionResult prefabRevision(const asset_system::AssetGuid&) const {
    return assets::PrefabRevisionResult::Err(assets::StaticGroupError::Unsupported);
  }
  [[nodiscard]] virtual assets::StaticGroupResult prefab(const asset_system::AssetGuid&) const {
    return assets::StaticGroupResult::Err(assets::StaticGroupError::Unsupported);
  }
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
  // On the connection owner thread; borrows the request only for this call.
  // A success admits one job, settled through rebuildStatus by its returned ID.
  [[nodiscard]] virtual assets::RenameTextureResult renameTexture(const assets::RenameTextureRequest&) {
    return assets::RenameTextureResult::Err(assets::RenameTextureError::Unsupported);
  }
  [[nodiscard]] virtual assets::MoveTextureResult moveTexture(const assets::MoveTextureRequest&) {
    return assets::MoveTextureResult::Err(assets::MoveTextureError::Unsupported);
  }
  [[nodiscard]] virtual assets::TextureRemovalCheckResult checkTextureRemoval(const assets::TextureRemovalTarget&) {
    return assets::TextureRemovalCheckResult::Err(assets::TextureRemovalError::Unsupported);
  }
  [[nodiscard]] virtual assets::TextureRemovalCheckResult textureRemovalCheckStatus(assets::TextureRemovalCheckId) const {
    return assets::TextureRemovalCheckResult::Err(assets::TextureRemovalError::Unsupported);
  }
  [[nodiscard]] virtual assets::RemoveTextureResult removeTexture(
      const assets::TextureRemovalTarget&, assets::TextureRemovalCheckId) {
    return assets::RemoveTextureResult::Err(assets::TextureRemovalError::Unsupported);
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
  [[nodiscard]] virtual assets::TextureImportStartResult textureImport(std::string_view, assets::ImportMode) {
    return assets::TextureImportStartResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::TextureImportStatusResult textureImportStatus(std::optional<assets::TextureImportId>) const {
    return assets::TextureImportStatusResult::Err(assets::ImportError::Unavailable);
  }
  // Spec 0097 / ADR-0156. Same worker and receipt history as textureImport.
  // Admission acknowledges a +Y linear PNG request only; status is scoped to that kind.
  [[nodiscard]] virtual assets::TextureImportStartResult normalMapImport(std::string_view, assets::ImportMode) {
    return assets::TextureImportStartResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::TextureImportStatusResult normalMapImportStatus(std::optional<assets::TextureImportId>) const {
    return assets::TextureImportStatusResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::TextureImportStartResult metallicRoughnessImport(std::string_view, assets::ImportMode) {
    return assets::TextureImportStartResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::TextureImportStartResult occlusionImport(std::string_view, assets::ImportMode) {
    return assets::TextureImportStartResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::TextureImportStatusResult metallicRoughnessImportStatus(std::optional<assets::TextureImportId>) const {
    return assets::TextureImportStatusResult::Err(assets::ImportError::Unavailable);
  }
  [[nodiscard]] virtual assets::TextureImportStatusResult occlusionImportStatus(std::optional<assets::TextureImportId>) const {
    return assets::TextureImportStatusResult::Err(assets::ImportError::Unavailable);
  }
  // Spec 0094 / ADR-0153. Read and preview use the retained cooked catalog.
  // Save returns admission only. A transport failure leaves settlement unknown.
  [[nodiscard]] virtual assets::MaterialReadResult materialRead(const asset_system::AssetGuid&) const {
    return assets::MaterialReadResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialSaveResult materialSave(const assets::MaterialSave&) {
    return assets::MaterialSaveResult::Err(assets::MaterialError::Unavailable);
  }
  [[nodiscard]] virtual assets::MaterialStatusResult materialStatus(std::optional<assets::MaterialJobId>) const {
    return assets::MaterialStatusResult::Err(assets::MaterialError::Unavailable);
  }
  [[nodiscard]] virtual assets::MaterialDraftPreviewResult previewMaterialDraft(
      const asset_system::AssetGuid&, const assets::MaterialRecipe&) {
    return assets::MaterialDraftPreviewResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialReadResult materialReadWithMaps(const asset_system::AssetGuid&) const {
    return assets::MaterialReadResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialSaveResult materialSaveWithMaps(const assets::MaterialSave&) {
    return assets::MaterialSaveResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialDraftPreviewResult previewMaterialDraftWithMaps(
      const asset_system::AssetGuid&, const assets::MaterialRecipe&) {
    return assets::MaterialDraftPreviewResult::Err(assets::MaterialError::Unsupported);
  }

  // Spec 0098 / ADR-0157. Complete recipe on the same snapshot and worker.
  [[nodiscard]] virtual assets::MaterialReadResult materialReadWithMetallicRoughness(const asset_system::AssetGuid&) const {
    return assets::MaterialReadResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialReadResult materialReadWithOcclusion(const asset_system::AssetGuid&) const {
    return assets::MaterialReadResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialReadResult materialReadWithCombinedMaps(const asset_system::AssetGuid&) const {
    return assets::MaterialReadResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialSaveResult materialSaveWithMetallicRoughness(const assets::MaterialSave&) {
    return assets::MaterialSaveResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialSaveResult materialSaveWithOcclusion(const assets::MaterialSave&) {
    return assets::MaterialSaveResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialSaveResult materialSaveWithCombinedMaps(const assets::MaterialSave&) {
    return assets::MaterialSaveResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialDraftPreviewResult previewMaterialDraftWithMetallicRoughness(
      const asset_system::AssetGuid&, const assets::MaterialRecipe&) {
    return assets::MaterialDraftPreviewResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialDraftPreviewResult previewMaterialDraftWithOcclusion(
      const asset_system::AssetGuid&, const assets::MaterialRecipe&) {
    return assets::MaterialDraftPreviewResult::Err(assets::MaterialError::Unsupported);
  }
  [[nodiscard]] virtual assets::MaterialDraftPreviewResult previewMaterialDraftWithCombinedMaps(
      const asset_system::AssetGuid&, const assets::MaterialRecipe&) {
    return assets::MaterialDraftPreviewResult::Err(assets::MaterialError::Unsupported);
  }
};
}  // namespace atlantis::connection
