#pragma once

#include <atlantis/asset_system/scene_property_address.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <string>
#include <vector>

// Spec 0063 R1 / ADR-0124 D1 (Plan 0063 P1): the Editor Transaction -- one
// logical edit, the consumer Spec 0049 named. Its writes are addressed by
// Spec 0049 R7's (node/entity GUID, component TypeId, leaf FieldId), the
// address an authoring-scene target shares (ADR-0124 D4); their values are
// schema-primitive values. It names no connection and no World component.
// Plain values. UI-free.
namespace atlantis::editor {

struct PropertyWrite {
  atlantis::asset_system::scene::PropertyAddress address;
  atlantis::world::access::PropertyValue before;
  atlantis::world::access::PropertyValue after;
  friend bool operator==(const PropertyWrite&, const PropertyWrite&) = default;
};

// An ordered, non-empty list of writes under a label for the UI. Its forward
// direction writes every `after`, its inverse every `before`, in this order,
// each direction as one transaction.
struct EditTransaction {
  std::string label;
  std::vector<PropertyWrite> writes;
  friend bool operator==(const EditTransaction&, const EditTransaction&) = default;
};

}  // namespace atlantis::editor
