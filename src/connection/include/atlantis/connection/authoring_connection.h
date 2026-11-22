#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/scene_property_address.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Spec 0065 R1-R6, R9 / ADR-0130 D1 (Q3, I-a + V-1): the interface through
// which every client edits the one authoring document (ADR-0129 D1) -- a
// sibling of RuntimeConnection with the same four concepts (Query, Command,
// Event, Transaction) over the document instead of the Runtime World. It
// names no Runtime, ECS or GPU type. Values are World access's
// PropertyValue, addressed by scene::PropertyAddress (the node's EntityGuid,
// the node component's TypeId, the leaf field's FieldId; Spec 0049 R7).

namespace atlantis::connection {

// The value types, in their own namespace so they never collide with World
// access's command and event names.
namespace authoring {

using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using atlantis::asset_system::scene::PropertyAddress;
using atlantis::world::access::PropertyValue;

// -------------------------------------------------------------- queries

enum class Provenance : std::uint8_t {
  HandAuthored,  // opened from its committed source file; save writes it back (ADR-0129 D4)
  Imported,      // opened from the build's generated source; editable and applyable, never saved
};

struct DocumentInfo {
  AssetGuid scene;  // the scene's AssetGuid
  Provenance provenance = Provenance::HandAuthored;
  std::string sourcePath;  // the source file (HandAuthored) or the generated source (Imported)
  std::uint64_t nodeCount = 0;
  bool supportsGuardedPropertyTransactions = false;
  bool supportsGuardedTransformTransactions = false;
  bool supportsGuardedDuplicateTransactions = false;
  bool supportsPrefabOrigin = false;
  bool supportsPrefabRegistration = false;
  bool supportsPrefabSync = false;
  bool supportsGuardedNameTransactions = false;
  friend bool operator==(const DocumentInfo&, const DocumentInfo&) = default;
};

// One node, in the document's canonical order (also the bake's instantiation order).
struct NodeSummary {
  EntityGuid node;
  std::optional<EntityGuid> parent;
  std::string name;  // Spec 0069: empty means unnamed; identity remains the GUID
  std::optional<AssetGuid> prefabOrigin{};
  std::optional<std::string> prefabBaseline{};
  friend bool operator==(const NodeSummary&, const NodeSummary&) = default;
};

// -------------------------------------------------------------- commands

// The node's component must be present; the value's kind must be the field's
// (Absent only for an Optional field). Validity is the transaction's.
struct SetProperty {
  PropertyAddress address;
  PropertyValue value;
};
// A node at `position` in the node order (0 = first; at most the node count),
// with no parent, a default Transform and each of `components` at its default
// values (Transform is always present and must not be listed). `node` names
// its EntityGuid, or is minted when absent (ADR-0097 D1, Plan 0065 J8).
struct CreateNode {
  std::optional<EntityGuid> node;
  std::uint64_t position = 0;
  std::vector<schema::TypeId> components;
};
struct DestroyNode {
  EntityGuid node;
};
// Added at its default values.
struct AddComponent {
  EntityGuid node;
  schema::TypeId component;
};
struct RemoveComponent {
  EntityGuid node;
  schema::TypeId component;
};
// The parent only: the node keeps its place in the order.
struct SetParent {
  EntityGuid node;
  std::optional<EntityGuid> parent;
};
struct SetActiveCamera {
  std::optional<EntityGuid> camera;
};
struct SetNodeName {
  EntityGuid node;
  std::string name;
};
struct SetPrefabOrigin {
  EntityGuid node;
  AssetGuid prefab;
};
enum class SiblingDirection : std::uint8_t { Up, Down };

struct SwapAdjacentSiblings {
  EntityGuid node;
  EntityGuid sibling;
  std::optional<EntityGuid> parent;
  SiblingDirection direction = SiblingDirection::Up;
  std::uint64_t expectedRevision = 0;
};

enum class SiblingPlacement : std::uint8_t { Before, After };

struct MoveSiblingRelative {
  EntityGuid node;
  EntityGuid anchor;
  std::optional<EntityGuid> parent;
  SiblingPlacement placement = SiblingPlacement::Before;
  std::uint64_t expectedRevision = 0;
};
// ADR-0162: only as the first command in a property-only transaction.
struct RequireDocumentRevision {
  std::uint64_t expectedRevision = 0;
};
inline constexpr std::size_t kMaxGuardedPropertyWrites = 64;

/// First command of a guarded transaction of Transform leaves (ADR-0163).
struct RequireTransformBatchRevision {
  std::uint64_t expectedRevision = 0;
};
inline constexpr std::size_t kMaxGuardedTransformNodes = 64;
inline constexpr std::size_t kMaxGuardedTransformWrites = 3 * kMaxGuardedTransformNodes;

/// First command of a guarded append or suffix removal (ADR-0164).
struct RequireDuplicateBatchRevision {
  std::uint64_t expectedRevision = 0;
};
inline constexpr std::size_t kMaxGuardedDuplicateNodes = 64;

struct DocumentToken {
  asset_system::EntityGuid documentId;
  std::uint64_t documentRevision = 0;
  friend bool operator==(const DocumentToken&, const DocumentToken&) = default;
};
/// First command of a guarded, names-only transaction (ADR-0189).
struct RequireNameBatchDocument {
  DocumentToken expectedDocument;
};
inline constexpr std::size_t kMaxGuardedNameWrites = 64;

using Command =
    std::variant<SetProperty, CreateNode, DestroyNode, AddComponent, RemoveComponent, SetParent, SetActiveCamera,
                 SetNodeName, SwapAdjacentSiblings, MoveSiblingRelative, RequireDocumentRevision,
                 RequireTransformBatchRevision, RequireDuplicateBatchRevision, SetPrefabOrigin, RequireNameBatchDocument>;

// ---------------------------------------------------------------- events

struct PropertyChanged {
  PropertyAddress address;
  PropertyValue value;  // the value applied
  friend bool operator==(const PropertyChanged&, const PropertyChanged&) = default;
};
struct NodeCreated {  // followed by one ComponentAdded per initial component
  EntityGuid node;
  std::uint64_t position = 0;
  std::optional<AssetGuid> prefabOrigin{};
  friend bool operator==(const NodeCreated&, const NodeCreated&) = default;
};
struct NodeDestroyed {
  EntityGuid node;
  friend bool operator==(const NodeDestroyed&, const NodeDestroyed&) = default;
};
struct ComponentAdded {
  EntityGuid node;
  schema::TypeId component;
  friend bool operator==(const ComponentAdded&, const ComponentAdded&) = default;
};
struct ComponentRemoved {
  EntityGuid node;
  schema::TypeId component;
  friend bool operator==(const ComponentRemoved&, const ComponentRemoved&) = default;
};
struct ParentChanged {
  EntityGuid node;
  std::optional<EntityGuid> parent;
  friend bool operator==(const ParentChanged&, const ParentChanged&) = default;
};
struct ActiveCameraChanged {
  std::optional<EntityGuid> camera;
  friend bool operator==(const ActiveCameraChanged&, const ActiveCameraChanged&) = default;
};
// The document was (re)read from its source by open(): every earlier read of
// it is stale.
struct DocumentReopened {
  AssetGuid scene;
  friend bool operator==(const DocumentReopened&, const DocumentReopened&) = default;
};
// The Runtime swapped in an applied document (ADR-0131 D5): its new
// RuntimeStatus.sceneGeneration.
struct Applied {
  std::uint64_t generation = 0;
  friend bool operator==(const Applied&, const Applied&) = default;
};
struct NodeNameChanged {
  EntityGuid node;
  std::string name;
  friend bool operator==(const NodeNameChanged&, const NodeNameChanged&) = default;
};
struct NodeOrderChanged {
  EntityGuid node;
  EntityGuid sibling;
  std::uint64_t position = 0;
  std::uint64_t siblingPosition = 0;
  std::uint64_t revision = 0;
  friend bool operator==(const NodeOrderChanged&, const NodeOrderChanged&) = default;
};
struct NodeMovedInSiblingOrder {
  EntityGuid node;
  std::optional<EntityGuid> parent;
  std::uint64_t fromPosition;
  std::uint64_t position;
  std::uint64_t revision;
  friend bool operator==(const NodeMovedInSiblingOrder&, const NodeMovedInSiblingOrder&) = default;
};
using Event = std::variant<PropertyChanged, NodeCreated, NodeDestroyed, ComponentAdded, ComponentRemoved, ParentChanged,
                           ActiveCameraChanged, DocumentReopened, Applied, NodeNameChanged, NodeOrderChanged,
                           NodeMovedInSiblingOrder>;

enum class EventKind : std::uint8_t {
  PropertyChanged,
  NodeCreated,
  NodeDestroyed,
  ComponentAdded,
  ComponentRemoved,
  ParentChanged,
  ActiveCameraChanged,
  DocumentReopened,
  Applied,
  NodeNameChanged,
  NodeOrderChanged,
  NodeMovedInSiblingOrder,
};

[[nodiscard]] EventKind kindOf(const Event& event) noexcept;

class EventKindSet {
 public:
  constexpr EventKindSet() noexcept = default;  // empty
  [[nodiscard]] static constexpr EventKindSet all() noexcept { return EventKindSet(0x7FF); }
  [[nodiscard]] static constexpr EventKindSet withSiblingMove() noexcept { return EventKindSet(0xFFF); }
  [[nodiscard]] static constexpr EventKindSet legacy() noexcept { return EventKindSet(0x3FF); }
  [[nodiscard]] static constexpr EventKindSet only(EventKind kind) noexcept { return EventKindSet().with(kind); }
  [[nodiscard]] constexpr EventKindSet with(EventKind kind) const noexcept {
    return EventKindSet(static_cast<std::uint16_t>(bits_ | bit(kind)));
  }
  [[nodiscard]] constexpr bool contains(EventKind kind) const noexcept { return (bits_ & bit(kind)) != 0; }
  friend bool operator==(const EventKindSet&, const EventKindSet&) = default;

 private:
  constexpr explicit EventKindSet(std::uint16_t bits) noexcept : bits_(bits) {}
  [[nodiscard]] static constexpr std::uint16_t bit(EventKind kind) noexcept {
    return static_cast<std::uint16_t>(1u << static_cast<unsigned>(kind));
  }
  std::uint16_t bits_ = 0;
};

// As RuntimeConnection's EventFilter: an event matches when its kind is in
// `kinds`, its node is `node` (if set) and its component is `component` (if
// set). The node of PropertyChanged is its address's; ActiveCameraChanged,
// DocumentReopened and Applied carry no node, and only PropertyChanged,
// ComponentAdded and ComponentRemoved carry a component.
struct EventFilter {
  EventKindSet kinds = EventKindSet::legacy();
  std::optional<EntityGuid> node;
  std::optional<schema::TypeId> component;

  [[nodiscard]] bool matches(const Event& event) const;
};

// ---------------------------------------------------------------- errors

// Why a query or a submission was refused (ADR-0130 D1: the addressing errors
// plus the Spec 0049 constraint names). A refused transaction changes nothing
// and emits no event.
enum class Error : std::uint8_t {
  // Addressing: queries, and a command against the document as the earlier
  // commands of its transaction left it.
  NoDocument,               // no document is open
  UnknownNode,              // no node with that EntityGuid
  UnknownComponentType,     // not a node component of the scene schema
  ComponentMissing,         // the node has no such component
  ComponentAlreadyPresent,  // already there (Transform always is)
  ComponentRequired,        // removing a Required component (Transform)
  UnknownField,             // the FieldId names no leaf reachable from the component
  KindMismatch,             // the value's kind is not the field's (Absent only for Optional)
  EnumValueOutOfRange,      // an enum value that is not one of its declared constants
  NodePositionOutOfRange,   // CreateNode past the end of the node order
  // Validity (V-1), of the document a transaction would leave: a field domain
  // of sceneSchema(), then each of its constraints by name.
  FieldOutOfDomain,
  NonEmptyDocument,
  NodeGuidNonNil,
  NodeGuidUnique,
  ParentExists,
  ParentAcyclic,
  ActiveCameraExists,
  ActiveCameraHasCamera,
  ComponentExclusivity,
  LightRangeMatchesKind,
  MaxDirectionalLights,
  MaxPointLights,
  // The build's own cook of the document's text (Plan 0065 J2): refused for a
  // reason none of the above names, or the text cannot hold the document.
  CookRefused,
  NotRepresentableInSource,
  InvalidNodeName,
  SiblingOrderStaleRevision,
  SiblingOrderRequiresHandAuthored,
  InvalidSiblingSwap,
  InvalidSiblingMove,
  InvalidGuardedPropertyBatch,
  PropertyBatchStaleRevision,
  InvalidGuardedDuplicateBatch,
  DuplicateBatchStaleRevision,
  InvalidGuardedNameBatch,
  NameBatchStaleDocument,
};

[[nodiscard]] std::string_view toString(Error error) noexcept;

// A refused submission, reported to the connection that submitted it. An
// addressing refusal names the refused command; a validity refusal names the
// transaction's first command.
struct CommandFailure {
  atlantis::world::access::CommandTicket ticket;
  Error error = Error::NoDocument;
  friend bool operator==(const CommandFailure&, const CommandFailure&) = default;
};

enum class OpenError : std::uint8_t {
  SourceUnreadable,   // the scene's source file could not be read
  SourceParseFailed,  // its text is not the scene source grammar
  SourceInvalid,      // it does not map to a valid document (references, schema, cook)
};
enum class SaveError : std::uint8_t {
  NoDocument,
  ImportedSceneNotSavable,  // ADR-0129 D4
  WriteFailed,              // the temporary file could not be written or renamed; the source is untouched
};
struct DocumentState {
  DocumentToken token;
  asset_system::AssetGuid scene;
  std::string source;
  Provenance provenance = Provenance::HandAuthored;
  std::uint64_t lastPersistedRevision = 0;
  bool dirty = false;
  friend bool operator==(const DocumentState&, const DocumentState&) = default;
};
enum class DocumentError : std::uint8_t {
  NoDocument, StaleDocument, ImportedSceneNotSavable, WriteFailed,
  SourceUnreadable, SourceParseFailed, SourceInvalid,
  Unsupported, NoAuthoring, TransportFailure, ProtocolFailure, InvalidRequest,
};
struct DocumentFailure {
  DocumentError error;
  bool outcomeUnknown = false;  // The mutation may already have reached the host.
  friend bool operator==(const DocumentFailure&, const DocumentFailure&) = default;
};
[[nodiscard]] std::string_view toString(DocumentError error) noexcept;
// Spec 0122: copied authoring state and official source are separate.
inline constexpr std::size_t kRecoveryMaxSourceBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kRecoveryPreviewLines = 20;
inline constexpr std::size_t kRecoveryPreviewScalars = 256;
enum class RecoveryCapture : std::uint8_t { Idle, Writing, Failed, Unavailable };
enum class RecoverySource : std::uint8_t { Matches, Changed, Unreadable, AlreadySaved, DirtyDocument };
enum class RecoveryError : std::uint8_t {
  Unsupported, NoDocument, NoAuthoring, Busy, Unavailable, StaleDocument, StaleCopy, DirtyDocument,
  SourceChanged, SourceUnreadable, InvalidCopy, IoFailed, TransportFailure, ProtocolFailure, InvalidRequest,
};
struct RecoveryFailure {
  RecoveryError error;
  bool outcomeUnknown = false;
  friend bool operator==(const RecoveryFailure&, const RecoveryFailure&) = default;
};
struct RecoveryCopy {
  asset_system::EntityGuid id;
  std::uint64_t revision = 0;
  std::uint64_t capturedUnixSeconds = 0;
  bool pending = false;
  friend bool operator==(const RecoveryCopy&, const RecoveryCopy&) = default;
};
struct RecoveryPreview {
  RecoverySource source;
  std::uint64_t copyNodes = 0;
  std::optional<std::uint64_t> sourceNodes;
  std::vector<std::string> sourceLines;
  std::vector<std::string> copyLines;
  bool truncated = false;
  friend bool operator==(const RecoveryPreview&, const RecoveryPreview&) = default;
};
struct RecoveryStatus {
  RecoveryCapture capture = RecoveryCapture::Unavailable;
  std::optional<std::uint64_t> protectedRevision;
  std::optional<RecoveryCopy> copy;
  std::optional<RecoveryPreview> preview;
  friend bool operator==(const RecoveryStatus&, const RecoveryStatus&) = default;
};
struct RecoveryReceipt {
  asset_system::EntityGuid copyId;
  DocumentState document;
  friend bool operator==(const RecoveryReceipt&, const RecoveryReceipt&) = default;
};
[[nodiscard]] std::string_view toString(RecoveryError error) noexcept;
[[nodiscard]] std::string_view toString(RecoveryCapture state) noexcept;
[[nodiscard]] std::string_view toString(RecoverySource state) noexcept;
enum class SaveAsError : std::uint8_t {
  Unavailable, InvalidName, NoDocument, ImportedSceneNotSavable, UnsafeTarget,
  Busy, NameConflict, IdentityConflict, WriteFailed, Unsupported, UnknownOutcome, StaleDocument, UnsupportedSubtree, StaleSource,
};
struct SavedScene {
  asset_system::AssetGuid scene;
  std::string source;
  std::uint64_t documentRevision = 0;
  friend bool operator==(const SavedScene&, const SavedScene&) = default;
};
[[nodiscard]] bool validSaveAsName(std::string_view name) noexcept;
[[nodiscard]] std::string saveAsSource(std::string_view name);
[[nodiscard]] std::string_view toString(SaveAsError error) noexcept;
struct PrefabRegistration {
  AssetGuid prefab;
  std::string source;
  std::uint64_t documentRevision = 0;
};
struct PrefabUpdate {
  PrefabRegistration registration;
  std::string previousRevision;
  std::string revision;
  bool changed = false;
};
using PrefabUpdateResult = Result<PrefabUpdate, SaveAsError>;
enum class PrefabEditError : std::uint8_t {
  Unsupported, NoDocument, StaleDocument, StaleSource, InvalidInstance, LocalConflict,
  BaselineUnavailable, ReplayConflict, UnknownOutcome,
};
[[nodiscard]] std::string_view toString(PrefabEditError error) noexcept;
[[nodiscard]] bool validPrefabDigest(std::string_view value) noexcept;

struct PrefabSubtree {
  // Canonical bounded authoring subtree bytes. Its root's outside parent
  // is stored separately so the existing scene codec can validate the tree.
  std::string source;
  std::optional<EntityGuid> parent;
  std::vector<std::uint64_t> slots;
  std::string order;
  friend bool operator==(const PrefabSubtree&, const PrefabSubtree&) = default;
};
struct PrefabEdit {
  DocumentToken document;
  PrefabSubtree before;
  PrefabSubtree after;
  bool changed = false;
  bool affectsProjection = false;
};
using PrefabEditResult = Result<PrefabEdit, PrefabEditError>;
[[nodiscard]] std::string prefabSource(std::string_view name);

enum class CreateSceneError : std::uint8_t {
  Unavailable, InvalidName, UnsafeTarget, Busy, NameConflict, IdentityConflict, WriteFailed,
  Unsupported, UnknownOutcome, NoAuthoring,  // client-side only
};
struct CreatedScene {
  asset_system::AssetGuid scene;
  std::string source;
  friend bool operator==(const CreatedScene&, const CreatedScene&) = default;
};
[[nodiscard]] std::string_view toString(CreateSceneError error) noexcept;
enum class ApplyError : std::uint8_t {
  NoDocument,
  NoSceneSink,  // the host gave the session no way to reach the Runtime (ADR-0131 D3)
  CookFailed,
  DecodeFailed,
  ReloadRefused,  // the sink refused the decoded scene
};

[[nodiscard]] std::string_view toString(OpenError error) noexcept;
[[nodiscard]] std::string_view toString(SaveError error) noexcept;
[[nodiscard]] std::string_view toString(ApplyError error) noexcept;

// Spec 0083 / ADR-0144. Owned observation values; no Runtime or asset identity.
struct ApplyId {
  asset_system::GuidBytes bytes{};
  friend auto operator<=>(const ApplyId&, const ApplyId&) = default;
};
[[nodiscard]] Result<ApplyId, asset_system::GuidParseError> parseApplyId(std::string_view text);
[[nodiscard]] std::string toString(const ApplyId& id);
struct ApplyReceipt {
  ApplyId id;
  std::uint64_t documentRevision = 0;
  friend bool operator==(const ApplyReceipt&, const ApplyReceipt&) = default;
};
enum class ApplyState : std::uint8_t { Pending, Applied, Failed, Superseded };
enum class ReloadError : std::uint8_t { ScenePreparationFailed, RenderPreparationFailed, RuntimeStopped };
enum class ApplyLookupError : std::uint8_t { UnknownApply };
[[nodiscard]] std::string_view toString(ApplyState state) noexcept;
[[nodiscard]] std::string_view toString(ReloadError error) noexcept;
[[nodiscard]] std::string_view toString(ApplyLookupError error) noexcept;
struct ApplyRecord {
  ApplyReceipt receipt;
  ApplyState state = ApplyState::Pending;
  std::optional<std::uint64_t> generation;
  std::optional<ReloadError> error;
  std::optional<ApplyId> supersededBy;
  friend bool operator==(const ApplyRecord&, const ApplyRecord&) = default;
};
struct ApplyProjection {
  ApplyReceipt receipt;
  std::uint64_t generation = 0;
  friend bool operator==(const ApplyProjection&, const ApplyProjection&) = default;
};
struct ApplySnapshot {
  std::optional<std::uint64_t> currentDocumentRevision;
  std::optional<ApplyRecord> request;
  std::optional<ApplyProjection> projection;
  friend bool operator==(const ApplySnapshot&, const ApplySnapshot&) = default;
};

}  // namespace authoring

// Not thread-safe (ADR-0004): every call on the host's frame thread, between
// frames -- the same contract as RuntimeConnection. Commands take effect when
// submitted: a transaction is applied to a copy of the document, validated and
// swapped in, or refused whole (V-1). Every connection sees every connection's
// changes as events.
class AuthoringConnection {
 public:
  virtual ~AuthoringConnection() = default;

  // Query (R3): values only. Every query but schema() is NoDocument until
  // open() has succeeded.
  [[nodiscard]] virtual std::optional<authoring::DocumentInfo> document() const = 0;
  [[nodiscard]] virtual atlantis::Result<std::vector<authoring::NodeSummary>, authoring::Error> listNodes() const = 0;
  // The node's component TypeIds, sorted by value.
  [[nodiscard]] virtual atlantis::Result<std::vector<schema::TypeId>, authoring::Error> listComponents(
      const authoring::EntityGuid& node) const = 0;
  [[nodiscard]] virtual atlantis::Result<authoring::PropertyValue, authoring::Error> getProperty(
      const authoring::PropertyAddress& address) const = 0;
  [[nodiscard]] virtual atlantis::Result<std::optional<authoring::EntityGuid>, authoring::Error> activeCamera()
      const = 0;
  // The descriptors of the scene's component types (assetSystemSchema()'s
  // scene types); the views stay valid for the process.
  [[nodiscard]] virtual std::span<const schema::TypeDescriptor> schema() const = 0;

  // Command and Transaction (R4, R5): one command, or several all or nothing.
  // A refusal is reported by drainFailures().
  virtual atlantis::world::access::CommandTicket submit(authoring::Command command) = 0;
  virtual atlantis::world::access::TransactionTicket submitTransaction(std::vector<authoring::Command> commands) = 0;

  // Remote connections establish this through the order-aware event subscription.
  [[nodiscard]] virtual bool supportsSiblingOrder() { return true; }
  [[nodiscard]] virtual bool supportsSiblingMove() { return true; }

  // Event (R6): pull-only, per subscription, from the moment it is made; and
  // the failures of this connection's own submissions.
  [[nodiscard]] virtual SubscriptionId subscribe(authoring::EventFilter filter) = 0;
  virtual atlantis::Result<std::monostate, ConnectionError> unsubscribe(SubscriptionId subscription) = 0;
  [[nodiscard]] virtual atlantis::Result<std::vector<authoring::Event>, ConnectionError> drainEvents(
      SubscriptionId subscription) = 0;
  [[nodiscard]] virtual std::vector<authoring::CommandFailure> drainFailures() = 0;

  // Session calls (R2, R7, R9). open() (re)reads the Runtime's scene from its
  // source, discarding unsaved edits, and emits DocumentReopened; apply()
  // hands the document to the Runtime (ADR-0131); save() writes a
  // hand-authored source atomically (ADR-0129 D4).
  virtual atlantis::Result<std::monostate, authoring::OpenError> open() = 0;
  virtual atlantis::Result<std::monostate, authoring::ApplyError> apply() = 0;
  // Acceptance only; the immutable snapshot has an identity. Query until terminal,
  // not until a different client's Applied event. No implicit retry/cancellation.
  virtual Result<authoring::ApplyReceipt, authoring::ApplyError> applyTracked() = 0;
  // Non-destructive, shared, bounded metadata (64 terminals + pending + projection).
  // No id means global newest, not this connection's last apply. Unknown is not failure.
  [[nodiscard]] virtual Result<authoring::ApplySnapshot, authoring::ApplyLookupError> applyStatus(
      std::optional<authoring::ApplyId> id = std::nullopt) const = 0;
  virtual atlantis::Result<std::monostate, authoring::SaveError> save() = 0;
  // Host-dispatch calls with owned values. A null expected token on reopen means
  // first open only; neither mutation applies the document to the Runtime.
  [[nodiscard]] virtual Result<std::optional<authoring::DocumentState>, authoring::DocumentFailure>
  documentState() const {
    return decltype(documentState())::Err({authoring::DocumentError::Unsupported});
  }
  virtual Result<authoring::DocumentState, authoring::DocumentFailure>
  saveIfCurrent(const authoring::DocumentToken&) {
    return Result<authoring::DocumentState, authoring::DocumentFailure>::Err({authoring::DocumentError::Unsupported});
  }
  virtual Result<authoring::DocumentState, authoring::DocumentFailure>
  reopenIfCurrent(std::optional<authoring::DocumentToken>) {
    return Result<authoring::DocumentState, authoring::DocumentFailure>::Err({authoring::DocumentError::Unsupported});
  }
  [[nodiscard]] virtual Result<authoring::RecoveryStatus, authoring::RecoveryFailure>
  recoveryStatus(std::optional<asset_system::EntityGuid> = std::nullopt) const {
    return Result<authoring::RecoveryStatus, authoring::RecoveryFailure>::Err({authoring::RecoveryError::Unsupported});
  }
  virtual Result<authoring::RecoveryReceipt, authoring::RecoveryFailure>
  restoreRecovery(const authoring::DocumentToken&, const asset_system::EntityGuid&) {
    return Result<authoring::RecoveryReceipt, authoring::RecoveryFailure>::Err({authoring::RecoveryError::Unsupported});
  }
  virtual Result<authoring::RecoveryReceipt, authoring::RecoveryFailure>
  discardRecovery(const authoring::DocumentToken&, const asset_system::EntityGuid&) {
    return Result<authoring::RecoveryReceipt, authoring::RecoveryFailure>::Err({authoring::RecoveryError::Unsupported});
  }
  virtual Result<authoring::SavedScene, authoring::SaveAsError> saveAs(std::string_view name) = 0;
  virtual Result<authoring::CreatedScene, authoring::CreateSceneError> createScene(std::string_view) {
    return Result<authoring::CreatedScene, authoring::CreateSceneError>::Err(authoring::CreateSceneError::Unavailable);
  }
  virtual authoring::PrefabEditResult syncPrefab(const authoring::EntityGuid&, std::string_view,
      const authoring::DocumentToken&, std::optional<authoring::AssetGuid> = std::nullopt) {
    return authoring::PrefabEditResult::Err(authoring::PrefabEditError::Unsupported);
  }
  virtual authoring::PrefabEditResult replayPrefab(const authoring::EntityGuid&,
      const authoring::PrefabSubtree&, const authoring::PrefabSubtree&, const authoring::DocumentToken&) {
    return authoring::PrefabEditResult::Err(authoring::PrefabEditError::Unsupported);
  }
  virtual authoring::PrefabUpdateResult updatePrefab(const authoring::AssetGuid&, std::string_view,
      const authoring::EntityGuid&, const authoring::DocumentToken&) {
    return authoring::PrefabUpdateResult::Err(authoring::SaveAsError::Unsupported);
  }
  virtual Result<authoring::PrefabRegistration, authoring::SaveAsError> createPrefab(
      std::string_view, const authoring::EntityGuid&, const authoring::DocumentToken&) {
    return Result<authoring::PrefabRegistration, authoring::SaveAsError>::Err(authoring::SaveAsError::Unsupported);
  }
};

}  // namespace atlantis::connection
