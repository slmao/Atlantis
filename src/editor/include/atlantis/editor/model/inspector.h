#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Plan 0056 P9 (Spec 0056 R3): the Inspector model -- the selected entity's
// fields, generated from the schema's descriptors: every component's leaves in
// descriptor order (Spec 0054's text::leavesOf()), grouped by nested struct.
// No component has a hand-written inspector. An edit is one SetProperty; its
// outcome (the event, or the refusal) is kept beside the field. UI-free: no
// UI-library header is included by any model file.
namespace atlantis::editor {

// How a leaf is edited, from its PrimitiveKind (or Enum kind).
enum class FieldControl : std::uint8_t {
  Integer,  // UInt64
  Float,    // Float32
  Float3,   // Vec3Float32
  Float4,   // Vec4Float32
  Guid,     // AssetGuid / EntityGuid, as canonical text
  Enum,     // one of the enum's constants
};

struct EnumChoice {
  std::string name;
  std::int64_t value = 0;
  friend bool operator==(const EnumChoice&, const EnumChoice&) = default;
};

struct InspectorField {
  schema::TypeId component;
  std::string path;                 // Spec 0054's text form: "Camera.fog.density"
  std::vector<std::string> groups;  // the nested structs between component and leaf: {"fog"}
  std::string label;                // the leaf's own name
  schema::FieldId field;            // the leaf's id (its PropertyAddress field)
  schema::TypeKind kind = schema::TypeKind::Primitive;
  schema::PrimitiveKind primitive = schema::PrimitiveKind::UInt64;  // meaningful when kind == Primitive
  FieldControl control = FieldControl::Integer;
  bool editable = false;
  bool optional = false;            // may be Absent
  std::vector<EnumChoice> choices;  // Enum only, in declaration order
  // The value as last read (getProperty()); empty when the read failed.
  std::optional<atlantis::world::access::PropertyValue> value;
};

enum class EditStatus : std::uint8_t { Pending, Applied, Refused };

struct EditOutcome {
  EditStatus status = EditStatus::Pending;
  atlantis::world::access::CommandTicket ticket;
  std::string detail;  // the refusal's AccessError text
};

// The note the Inspector shows on Transform (Spec 0056 ruling Q5; Spec 0052
// ruling Q8, T-a): editable like any component, but WorldMatrix places it.
inline constexpr std::string_view kTransformNote =
    "Transform does not affect rendering (Spec 0052 ruling Q8): WorldMatrix places the entity.";

// Borrows the connection, which must outlive it. Frame thread only, between
// Runtime frames (ADR-0105); not thread-safe.
class InspectorModel {
 public:
  explicit InspectorModel(atlantis::connection::RuntimeConnection& connection);
  ~InspectorModel();
  InspectorModel(const InspectorModel&) = delete;
  InspectorModel& operator=(const InspectorModel&) = delete;

  // The entity to inspect. The field table is rebuilt from the schema when
  // the subject or its component set changes (update() checks the latter).
  void setSubject(std::optional<atlantis::asset_system::EntityGuid> entity);
  [[nodiscard]] const std::optional<atlantis::asset_system::EntityGuid>& subject() const noexcept {
    return subject_;
  }

  // Between frames: settles pending edits -- refused if `failures` (this
  // frame's drained failures of the editor's connection) names the edit's
  // ticket, applied once the subject's PropertyChanged event for the field
  // arrives -- rebuilds the table if the component set changed, and re-reads
  // every field's value.
  void update(std::span<const atlantis::world::access::CommandFailure> failures);

  [[nodiscard]] const std::vector<InspectorField>& fields() const noexcept { return fields_; }
  [[nodiscard]] std::string_view componentName(schema::TypeId component) const;
  // kTransformNote for Transform; empty for every other component.
  [[nodiscard]] std::string_view componentNote(schema::TypeId component) const;

  // One edit: exactly one SetProperty of `value` to fields()[index]. The
  // field must exist and be editable (programmer error otherwise). Its
  // outcome is Pending until update() settles it.
  atlantis::world::access::CommandTicket commit(std::size_t index, atlantis::world::access::PropertyValue value);
  [[nodiscard]] const std::optional<EditOutcome>& outcome(std::size_t index) const;

 private:
  void rebuild();
  void readValues();

  atlantis::connection::RuntimeConnection& connection_;
  std::optional<atlantis::asset_system::EntityGuid> subject_;
  std::optional<atlantis::connection::SubscriptionId> subscription_;
  std::vector<schema::TypeId> components_;
  std::vector<InspectorField> fields_;
  std::vector<std::optional<EditOutcome>> outcomes_;  // parallel to fields_
};

}  // namespace atlantis::editor
