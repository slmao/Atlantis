#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/access_error.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace atlantis::connection {

// Spec 0054 / ADR-0105: the one interface through which a client reaches a
// running Runtime World. It carries exactly Spec 0052's Query, Command and
// Event and Spec 0053's Transaction -- the same value types, the same
// semantics (commands apply at the owner's next applyPending(), validated,
// atomic per transaction) -- and nothing a transport would invent. A
// transport (0055) implements this interface; it does not shape it.

// The five event kinds (Spec 0052 R5), for filtering.
enum class EventKind : std::uint8_t { EntityCreated, EntityDestroyed, ComponentAdded, ComponentRemoved, PropertyChanged };

[[nodiscard]] EventKind kindOf(const atlantis::world::access::Event& event) noexcept;

// A set of EventKinds.
class EventKindSet {
 public:
  constexpr EventKindSet() noexcept = default;  // empty
  [[nodiscard]] static constexpr EventKindSet all() noexcept { return EventKindSet(0x1F); }
  [[nodiscard]] static constexpr EventKindSet only(EventKind kind) noexcept { return EventKindSet().with(kind); }
  [[nodiscard]] constexpr EventKindSet with(EventKind kind) const noexcept {
    return EventKindSet(static_cast<std::uint8_t>(bits_ | bit(kind)));
  }
  [[nodiscard]] constexpr bool contains(EventKind kind) const noexcept { return (bits_ & bit(kind)) != 0; }
  friend bool operator==(const EventKindSet&, const EventKindSet&) = default;

 private:
  constexpr explicit EventKindSet(std::uint8_t bits) noexcept : bits_(bits) {}
  [[nodiscard]] static constexpr std::uint8_t bit(EventKind kind) noexcept {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(kind));
  }
  std::uint8_t bits_ = 0;
};

// Which events a subscription receives (Spec 0054 ruling Q5, S-b): an event
// matches when its kind is in `kinds`, its entity is `entity` (if set) and
// its component is `component` (if set). EntityCreated/EntityDestroyed carry
// no component, so they never match a filter that names one.
struct EventFilter {
  EventKindSet kinds = EventKindSet::all();
  std::optional<atlantis::asset_system::EntityGuid> entity;
  std::optional<schema::TypeId> component;

  [[nodiscard]] bool matches(const atlantis::world::access::Event& event) const;
};

// A subscription's id: unique within its endpoint, from 1 (Plan 0054 J2).
struct SubscriptionId {
  std::uint64_t value = 0;
  friend auto operator<=>(const SubscriptionId&, const SubscriptionId&) = default;
};

// A connection-level refusal (Plan 0054 J5) -- not a World error.
enum class ConnectionError {
  UnknownSubscription,  // the id names no subscription of this connection
};

[[nodiscard]] std::string_view toString(ConnectionError error) noexcept;

// Not thread-safe (ADR-0004): every call on the owner's frame thread,
// between frames -- the same contract as RuntimeWorldAccess.
class RuntimeConnection {
 public:
  virtual ~RuntimeConnection() = default;

  // Query (Spec 0052 R3; Spec 0054 rulings Q1, Q8): values only.
  [[nodiscard]] virtual bool findEntity(const atlantis::asset_system::EntityGuid& entity) const = 0;
  [[nodiscard]] virtual std::vector<atlantis::asset_system::EntityGuid> listEntities() const = 0;
  [[nodiscard]] virtual atlantis::Result<std::vector<schema::TypeId>, atlantis::world::access::AccessError>
  listComponents(const atlantis::asset_system::EntityGuid& entity) const = 0;
  [[nodiscard]] virtual atlantis::Result<atlantis::world::access::PropertyValue, atlantis::world::access::AccessError>
  getProperty(const atlantis::world::access::PropertyAddress& address) const = 0;
  // The World schema's descriptors; the views stay valid while this
  // connection lives (ruling Q8, K1).
  [[nodiscard]] virtual std::span<const schema::TypeDescriptor> schema() const = 0;

  // Command and Transaction (Spec 0052 R4; Spec 0053): recorded now, applied
  // at the owner's next applyPending(), in submission order across every
  // connection.
  virtual atlantis::world::access::CommandTicket submit(atlantis::world::access::Command command) = 0;
  virtual atlantis::world::access::TransactionTicket submitTransaction(
      std::vector<atlantis::world::access::Command> commands) = 0;

  // Event (ruling Q5, S-b): pull-only, per subscription, from the moment it
  // is made; and the failures of this connection's own submissions.
  [[nodiscard]] virtual SubscriptionId subscribe(EventFilter filter) = 0;
  virtual atlantis::Result<std::monostate, ConnectionError> unsubscribe(SubscriptionId subscription) = 0;
  [[nodiscard]] virtual atlantis::Result<std::vector<atlantis::world::access::Event>, ConnectionError> drainEvents(
      SubscriptionId subscription) = 0;
  [[nodiscard]] virtual std::vector<atlantis::world::access::CommandFailure> drainFailures() = 0;
};

}  // namespace atlantis::connection
