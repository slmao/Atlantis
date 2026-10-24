#pragma once

#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Plan 0065 P2 (private to Atlantis Authoring): property access on the
// semantic scene types (scene::Transform, Camera, Renderable, Light) through
// assetSystemSchema()'s descriptors -- the same reflection World's access
// uses on its components (byteOffset into a standard-layout type, a leaf
// FieldId reached through exactly one field chain; Plan 0049 ruling J8).

namespace atlantis::authoring::detail {

namespace ac = atlantis::connection::authoring;

// The node components of sceneSchema(), in its order.
[[nodiscard]] std::span<const schema::TypeId> nodeComponentTypes() noexcept;
[[nodiscard]] bool isNodeComponent(schema::TypeId type) noexcept;
[[nodiscard]] bool isRequiredComponent(schema::TypeId type) noexcept;  // Transform

// The scene types reachable from the node components (structs and enums),
// assetSystemSchema()'s own descriptors, in its order.
[[nodiscard]] std::span<const schema::TypeDescriptor> authoringTypes();

[[nodiscard]] const schema::TypeDescriptor* findSceneType(schema::TypeId id) noexcept;

struct ResolvedField {
  schema::TypeKind kind = schema::TypeKind::Primitive;
  schema::PrimitiveKind primitive = schema::PrimitiveKind::UInt64;
  schema::TypeId enumType;  // Enum fields
  schema::FieldFlags flags = schema::FieldFlags::None;
  std::uint32_t byteOffset = 0;  // within the node component
};

// The leaf `field` under the node component `component`: UnknownComponentType
// if `component` is not a node component, UnknownField unless exactly one
// field chain reaches it.
[[nodiscard]] atlantis::Result<ResolvedField, ac::Error> resolveField(schema::TypeId component, schema::FieldId field);

// The node's component as bytes, or nullptr when the node lacks it.
[[nodiscard]] const std::byte* componentBytes(const asset_system::scene::AuthoringNode& node,
                                              schema::TypeId component) noexcept;
[[nodiscard]] std::byte* componentBytes(asset_system::scene::AuthoringNode& node, schema::TypeId component) noexcept;

[[nodiscard]] bool hasComponent(const asset_system::scene::AuthoringNode& node, schema::TypeId component) noexcept;
// Adds the component at its default values; false if present already.
bool addComponent(asset_system::scene::AuthoringNode& node, schema::TypeId component);
// Removes an optional component; false if absent.
bool removeComponent(asset_system::scene::AuthoringNode& node, schema::TypeId component);
// The node's component TypeIds, sorted by value.
[[nodiscard]] std::vector<schema::TypeId> componentsOf(const asset_system::scene::AuthoringNode& node);

// The value's kind against the field's: KindMismatch, EnumValueOutOfRange.
[[nodiscard]] atlantis::Result<std::monostate, ac::Error> checkKind(const ResolvedField& field,
                                                                    const ac::PropertyValue& value);
[[nodiscard]] ac::PropertyValue readField(const std::byte* component, schema::TypeId type, schema::FieldId field,
                                          const ResolvedField& resolved);
void writeField(std::byte* component, schema::TypeId type, schema::FieldId field, const ResolvedField& resolved,
                const ac::PropertyValue& value);

}  // namespace atlantis::authoring::detail
