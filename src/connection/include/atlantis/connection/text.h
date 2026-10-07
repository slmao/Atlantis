#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

// Spec 0054 R6 / ADR-0105 D5 (ruling Q2: T2 + F1 + the value table + J-i):
// the canonical text of an entity, a property path and a value -- client
// text, shared by the CLI and a future transport, not a wire protocol. Pure
// functions over a schema span; nothing here checks what the boundary checks
// (finiteness, editability, whether a type is a component): those stay its
// refusals, so there is one rule source.
namespace atlantis::connection::text {

enum class TextError {
  MalformedGuid,        // not RFC 9562 8-4-4-4-12 text
  UnknownType,          // no schema type has that short or qualified name
  UnknownField,         // a path segment names no field of the type it is in
  NotALeaf,             // the path stops at a struct, or names only a type
  WrongValueCount,      // not the number of value tokens the field's kind takes
  MalformedNumber,      // not a number of the field's kind
  UnknownEnumConstant,  // not one of the enum's declared constant names
};

[[nodiscard]] std::string_view toString(TextError error) noexcept;

// Entity: lowercase RFC 9562 text (Spec 0047's canonical form).
[[nodiscard]] atlantis::Result<atlantis::asset_system::EntityGuid, TextError> parseEntity(std::string_view text);
[[nodiscard]] std::string formatEntity(const atlantis::asset_system::EntityGuid& entity);

// A type's short name: its qualified name after the last "::" (Light for
// world::Light).
[[nodiscard]] std::string_view shortName(std::string_view qualifiedName) noexcept;
// The type with that short or qualified name, or nullptr.
[[nodiscard]] const schema::TypeDescriptor* findType(std::span<const schema::TypeDescriptor> schema,
                                                     std::string_view name) noexcept;
[[nodiscard]] const schema::TypeDescriptor* findType(std::span<const schema::TypeDescriptor> schema,
                                                     schema::TypeId id) noexcept;

// A property path, <Type>.<field>[.<field>...], resolved to its component
// and the leaf field (Spec 0049 J8 addressing).
struct ResolvedPath {
  schema::TypeId component;
  const schema::FieldDescriptor* leaf = nullptr;
};
[[nodiscard]] atlantis::Result<ResolvedPath, TextError> parsePath(std::span<const schema::TypeDescriptor> schema,
                                                                  std::string_view text);

// Every leaf of a type, in descriptor order, with its canonical path.
struct LeafPath {
  std::string path;  // "Camera.fog.density"
  const schema::FieldDescriptor* leaf = nullptr;
};
[[nodiscard]] std::vector<LeafPath> leavesOf(std::span<const schema::TypeDescriptor> schema, schema::TypeId type);

// A value of `leaf`'s kind from its tokens: one for a scalar, three or four
// for a vector, an enum constant's name, GUID text, or `none` for an absent
// Optional. Formatting yields tokens that parse back to an equal value.
[[nodiscard]] atlantis::Result<atlantis::world::access::PropertyValue, TextError> parseValue(
    std::span<const schema::TypeDescriptor> schema, const schema::FieldDescriptor& leaf,
    std::span<const std::string_view> tokens);
[[nodiscard]] std::string formatValue(std::span<const schema::TypeDescriptor> schema,
                                      const schema::FieldDescriptor& leaf,
                                      const atlantis::world::access::PropertyValue& value);

}  // namespace atlantis::connection::text
