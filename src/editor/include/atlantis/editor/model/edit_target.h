#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/scene_property_address.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Spec 0063 R8 / ADR-0124 D4 (Plan 0063 P2): the edit target -- the one
// boundary through which the edit history reaches a world. E2 implements it
// over RuntimeConnection (RuntimeWorldEditTarget); an authoring-scene target
// is E3's and is not built. UI-free.
namespace atlantis::editor {

// One write to submit: an address and the value to write there.
using AddressedValue = std::pair<atlantis::asset_system::scene::PropertyAddress, atlantis::world::access::PropertyValue>;

// A submitted transaction, as its target numbers it (E2: a TransactionTicket's
// first ticket and count).
struct EditHandle {
  std::uint64_t first = 0;
  std::uint64_t count = 0;
  friend bool operator==(const EditHandle&, const EditHandle&) = default;
};

enum class Settlement : std::uint8_t { Pending, Applied, Refused };

// What the world reported gone since the last drain: a whole entity (no
// component), or one of its components.
struct Invalidation {
  atlantis::asset_system::EntityGuid entity;
  std::optional<schema::TypeId> component;
  friend bool operator==(const Invalidation&, const Invalidation&) = default;
};

// Frame thread only, between the world's frames (ADR-0105's contract);
// not thread-safe.
class EditTarget {
 public:
  virtual ~EditTarget() = default;

  // The address's current value; empty when it cannot be read (the entity or
  // component is gone).
  [[nodiscard]] virtual std::optional<atlantis::world::access::PropertyValue> read(
      const atlantis::asset_system::scene::PropertyAddress& address) = 0;

  // Submits `writes`, in order, as one transaction and tracks its settlement.
  virtual EditHandle submit(std::span<const AddressedValue> writes) = 0;

  // Tracks a transaction submitted elsewhere (an Inspector commit, a Gizmo
  // release) whose writes are `writes`.
  virtual void track(EditHandle handle, std::span<const AddressedValue> writes) = 0;

  // A tracked handle's settlement as of the last update(); Pending for a
  // handle no longer tracked. A settled handle is reported until the next
  // update(), then forgotten.
  [[nodiscard]] virtual Settlement settlement(EditHandle handle) const = 0;
  // Why a Refused handle was refused (empty otherwise).
  [[nodiscard]] virtual std::string refusalText(EditHandle handle) const = 0;

  // Once per editor frame, with the failures the editor drained this frame.
  virtual void update(std::span<const atlantis::world::access::CommandFailure> failures) = 0;

  // The invalidations observed since the last drain, in order.
  [[nodiscard]] virtual std::vector<Invalidation> drainInvalidations() = 0;
};

}  // namespace atlantis::editor
