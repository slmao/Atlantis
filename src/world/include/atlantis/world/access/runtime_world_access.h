#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/scene_types.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/access_error.h>

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <variant>
#include <vector>

namespace atlantis::world {
struct BakedScene;
}

namespace atlantis::world::access {

// Spec 0052 / ADR-0103: the Runtime World's public operation boundary --
// Query, Command and Event, addressed by Stable Identity + Schema
// (EntityGuid, TypeId, FieldId against worldSchema(); ruling Q7). Nothing in
// this header names an ECS handle, archetype or component C++ type: clients
// hold only GUIDs, schema ids and values (ADR-0033).

// A property of an entity: a World component's TypeId and the FieldId of one
// of its leaf fields. A leaf inside a nested struct (Camera.fog.density) is
// addressed under its component by the leaf's own FieldId (Spec 0049 J8).
struct PropertyAddress {
  atlantis::asset_system::EntityGuid entity;
  schema::TypeId component;
  schema::FieldId field;
  friend bool operator==(const PropertyAddress&, const PropertyAddress&) = default;
};

// An enum field's value, one of its declared constants.
struct EnumValue {
  std::int64_t value = 0;
  friend bool operator==(const EnumValue&, const EnumValue&) = default;
};

// An Optional field with no value.
struct Absent {
  friend bool operator==(const Absent&, const Absent&) = default;
};

// Ruling Q2 (V-a): one alternative per schema::PrimitiveKind, in its order
// (UInt64, Float32, Vec3Float32, Vec4Float32, AssetGuid, EntityGuid), then an
// enum's value and Absent. Always a value; never a reference into the world.
using PropertyValue = std::variant<std::uint64_t, float, std::array<float, 3>, std::array<float, 4>,
                                   atlantis::asset_system::AssetGuid, atlantis::asset_system::EntityGuid,
                                   EnumValue, Absent>;

// Ruling Q6 (Plan 0052 P8): the light counts extraction can hold. A command
// that would exceed one is refused where it is applied, so a client can never
// trip scene_extraction's fatal check. The Point limit is the scene grammar's.
inline constexpr std::uint32_t kMaxDirectionalLights = 1;
inline constexpr std::uint32_t kMaxPointLights = atlantis::asset_system::kMaxPointLightsPerScene;

// The five commands (R4). Exactly these; additions are later Specs.
struct CreateEntity {  // the caller supplies the GUID: non-nil, unique (ruling Q1, a1)
  atlantis::asset_system::EntityGuid entity;
};
struct DestroyEntity {
  atlantis::asset_system::EntityGuid entity;
};
struct AddComponent {  // added with the component's default member values
  atlantis::asset_system::EntityGuid entity;
  schema::TypeId component;
};
struct RemoveComponent {
  atlantis::asset_system::EntityGuid entity;
  schema::TypeId component;
};
struct SetProperty {
  PropertyAddress address;
  PropertyValue value;
};
using Command = std::variant<CreateEntity, DestroyEntity, AddComponent, RemoveComponent, SetProperty>;

// The five events (R5): one per successfully applied command, in application
// order. Only commands applied through this boundary produce events -- this
// is not a reactive ECS, and an edit made directly on the ECS produces none.
struct EntityCreated {
  atlantis::asset_system::EntityGuid entity;
  friend bool operator==(const EntityCreated&, const EntityCreated&) = default;
};
struct EntityDestroyed {
  atlantis::asset_system::EntityGuid entity;
  friend bool operator==(const EntityDestroyed&, const EntityDestroyed&) = default;
};
struct ComponentAdded {
  atlantis::asset_system::EntityGuid entity;
  schema::TypeId component;
  friend bool operator==(const ComponentAdded&, const ComponentAdded&) = default;
};
struct ComponentRemoved {
  atlantis::asset_system::EntityGuid entity;
  schema::TypeId component;
  friend bool operator==(const ComponentRemoved&, const ComponentRemoved&) = default;
};
struct PropertyChanged {
  PropertyAddress address;
  PropertyValue value;  // the value applied
  friend bool operator==(const PropertyChanged&, const PropertyChanged&) = default;
};
using Event = std::variant<EntityCreated, EntityDestroyed, ComponentAdded, ComponentRemoved, PropertyChanged>;

// A submitted command's sequence number (Plan 0052 J4), increasing from 1.
struct CommandTicket {
  std::uint64_t value = 0;
  friend auto operator<=>(const CommandTicket&, const CommandTicket&) = default;
};

// A submitted transaction's commands' tickets (Spec 0053 ruling Q2; Plan 0053
// J5): consecutive, from `first`. An empty transaction has none and returns
// {CommandTicket{}, 0} -- ticket value 0 is never issued.
struct TransactionTicket {
  CommandTicket first;
  std::uint64_t count = 0;
  // Whether `ticket` is one of this transaction's commands. A failure whose
  // ticket it contains means the transaction was aborted at that command, at
  // position ticket.value - first.value.
  [[nodiscard]] constexpr bool contains(CommandTicket ticket) const noexcept {
    return count != 0 && first.value <= ticket.value && ticket.value - first.value < count;
  }
  friend bool operator==(const TransactionTicket&, const TransactionTicket&) = default;
};

struct CommandFailure {
  CommandTicket ticket;
  AccessError error = AccessError::UnknownEntity;
  friend bool operator==(const CommandFailure&, const CommandFailure&) = default;
};

struct ApplyReport {
  std::size_t applied = 0;               // commands that took effect
  std::vector<CommandFailure> failures;  // in submission order; one per aborted transaction
};

// The boundary over one Runtime World (rulings Q3, Q5; ADR-0103 D5, D7).
//
// Ownership: it borrows the BakedScene its owner (Runtime) holds and must not
// outlive it; it owns only its GUID index, the pending commands and the
// event/failure queues. The owner keeps reading its BakedScene directly for
// its own frame (ADR-0033: owner-internal access); clients use only this
// object.
//
// Commands follow the maintainer's ECS/CommandBuffer boundary in intent
// (Plan 0052 J5), with each of its four properties kept:
//   - deferred: submit() only records; nothing changes until applyPending();
//   - validated: each command is checked in full against the world as it is
//     when it applies, and a refused command has no effect (ruling Q9); a
//     transaction is checked whole before any of it applies (Spec 0053);
//   - applied together on the frame thread: the owner calls applyPending()
//     once per frame (Runtime: the first statement of runFrame(), J6);
//   - no pointers out: queries, events and failures are values.
// Only the ECS's CommandBuffer type is not used: a field write must read the
// component as it is at apply time, and the GUID index changes in lockstep
// with each command.
//
// Transform (ruling Q8): its fields are readable and writable, and a write
// emits PropertyChanged, but it does not affect rendering -- the baked world
// has no hierarchy and WorldMatrix is the render-authoritative placement
// (ADR-0102 D4). Move an entity by setting its WorldMatrix.
//
// Not thread-safe (ADR-0004): all calls on the frame thread, between frames.
class RuntimeWorldAccess {
 public:
  // Seeds the GUID index from the bake's EntityGuidMap (ruling Q1).
  explicit RuntimeWorldAccess(BakedScene& scene);
  ~RuntimeWorldAccess();
  RuntimeWorldAccess(const RuntimeWorldAccess&) = delete;
  RuntimeWorldAccess& operator=(const RuntimeWorldAccess&) = delete;
  RuntimeWorldAccess(RuntimeWorldAccess&&) noexcept;
  RuntimeWorldAccess& operator=(RuntimeWorldAccess&&) noexcept;

  // Query (R3): by value.
  [[nodiscard]] bool findEntity(const atlantis::asset_system::EntityGuid& entity) const;
  // The entity's component TypeIds, sorted by value.
  [[nodiscard]] atlantis::Result<std::vector<schema::TypeId>, AccessError> listComponents(
      const atlantis::asset_system::EntityGuid& entity) const;
  [[nodiscard]] atlantis::Result<PropertyValue, AccessError> getProperty(const PropertyAddress& address) const;

  // Command (R4): recorded now, applied by the owner's next applyPending().
  CommandTicket submit(Command command);

  // Transaction (Spec 0053; ADR-0104): `commands`, recorded now and applied
  // all or nothing at their place in submission order. If each would be
  // accepted against the world as the earlier ones leave it, all are
  // applied, each with its usual event; otherwise none is, no event is
  // emitted, and one CommandFailure names the first refused command. There
  // is no transaction event. An empty transaction does nothing and takes no
  // ticket (ruling Q4).
  TransactionTicket submitTransaction(std::vector<Command> commands);

  // Owner only: applies every pending command and transaction in submission
  // order -- a single command on its own (ruling Q9), a transaction all or
  // nothing (Spec 0053); queues one event per applied command and one
  // failure per refused command or aborted transaction.
  ApplyReport applyPending();

  // Event (R5) and command outcomes (J4): taken by value; each call empties
  // its queue.
  [[nodiscard]] std::vector<Event> drainEvents();
  [[nodiscard]] std::vector<CommandFailure> drainFailures();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace atlantis::world::access
