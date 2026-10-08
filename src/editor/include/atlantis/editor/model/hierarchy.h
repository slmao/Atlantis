#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/schema.h>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Plan 0056 P9 (Spec 0056 R2, ruling Q4): the Hierarchy model -- the flat list
// of addressable entities (the baked world has no hierarchy), each with its
// components, kept current by events: one full listing, then only what the
// subscription reports. UI-free: no UI-library header is included by any model
// file (tests/editor/editor_boundary_tests.cpp).
namespace atlantis::editor {

struct HierarchyRow {
  atlantis::asset_system::EntityGuid entity;
  std::vector<schema::TypeId> components;  // as RuntimeConnection::listComponents() orders them
  friend bool operator==(const HierarchyRow&, const HierarchyRow&) = default;
};

// Borrows the connection, which must outlive it. Frame thread only, between
// Runtime frames (ADR-0105); not thread-safe.
class HierarchyModel {
 public:
  explicit HierarchyModel(atlantis::connection::RuntimeConnection& connection);
  ~HierarchyModel();
  HierarchyModel(const HierarchyModel&) = delete;
  HierarchyModel& operator=(const HierarchyModel&) = delete;

  // The first call subscribes to EntityCreated/EntityDestroyed/
  // ComponentAdded/ComponentRemoved and lists every entity with its
  // components. Every later call drains that subscription once and re-reads
  // only the entities its events name: listed again if they still exist,
  // dropped (and deselected) if not.
  void update();

  // In GUID order (listEntities()'s).
  [[nodiscard]] const std::vector<HierarchyRow>& rows() const noexcept { return rows_; }

  // Indices into rows() of the rows whose GUID text starts with `filter`, or
  // one of whose component short names contains it (ASCII case-insensitive);
  // every row when `filter` is empty.
  [[nodiscard]] std::vector<std::size_t> filtered(std::string_view filter) const;

  // A component's short name ("Light" for world::Light), from the schema.
  [[nodiscard]] std::string_view componentName(schema::TypeId component) const;
  // A row's component short names, ", "-separated.
  [[nodiscard]] std::string componentNames(const HierarchyRow& row) const;

  // The Inspector's and Gizmo's subject. Selecting a GUID that is not listed
  // clears the selection.
  void select(std::optional<atlantis::asset_system::EntityGuid> entity);
  [[nodiscard]] const std::optional<atlantis::asset_system::EntityGuid>& selection() const noexcept {
    return selection_;
  }

 private:
  void listAll();
  void refresh(const atlantis::asset_system::EntityGuid& entity);

  atlantis::connection::RuntimeConnection& connection_;
  std::optional<atlantis::connection::SubscriptionId> subscription_;
  std::vector<HierarchyRow> rows_;
  std::optional<atlantis::asset_system::EntityGuid> selection_;
};

}  // namespace atlantis::editor
