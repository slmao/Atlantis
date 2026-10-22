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
  friend bool operator==(const DocumentInfo&, const DocumentInfo&) = default;
};

// One node, in the document's order (the instantiation order, immutable after
// creation; Spec 0065 Non-Goals).
struct NodeSummary {
  EntityGuid node;
  std::optional<EntityGuid> parent;
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
using Command =
    std::variant<SetProperty, CreateNode, DestroyNode, AddComponent, RemoveComponent, SetParent, SetActiveCamera>;

// ---------------------------------------------------------------- events

struct PropertyChanged {
  PropertyAddress address;
  PropertyValue value;  // the value applied
  friend bool operator==(const PropertyChanged&, const PropertyChanged&) = default;
};
struct NodeCreated {  // followed by one ComponentAdded per initial component
  EntityGuid node;
  std::uint64_t position = 0;
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
using Event = std::variant<PropertyChanged, NodeCreated, NodeDestroyed, ComponentAdded, ComponentRemoved, ParentChanged,
                           ActiveCameraChanged, DocumentReopened, Applied>;

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
};

[[nodiscard]] EventKind kindOf(const Event& event) noexcept;

class EventKindSet {
 public:
  constexpr EventKindSet() noexcept = default;  // empty
  [[nodiscard]] static constexpr EventKindSet all() noexcept { return EventKindSet(0x1FF); }
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
  EventKindSet kinds = EventKindSet::all();
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
  virtual atlantis::Result<std::monostate, authoring::SaveError> save() = 0;
};

}  // namespace atlantis::connection
