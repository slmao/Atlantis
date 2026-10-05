#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace atlantis::schema {

// Spec 0048 / ADR-0099: the engine's descriptive schema vocabulary. Modules
// author their own immutable descriptor tables beside the types they describe
// (worldSchema(), assetSystemSchema()); Core owns only this metalanguage.
// Descriptive only: no value access, serialization, defaults or ranges.
// Thread safety: value types and pure functions only; descriptor tables are
// immutable static data, safe for concurrent reads.

// FNV-1a-64 of a namespace-qualified type name below `atlantis::`, e.g.
// "world::Camera" (Plan 0048 P3).
struct TypeId {
  std::uint64_t value = 0;
  friend constexpr auto operator<=>(const TypeId&, const TypeId&) = default;
};

// FNV-1a-64 of "<qualified type>.<field>", e.g. "world::Camera.nearZ".
struct FieldId {
  std::uint64_t value = 0;
  friend constexpr auto operator<=>(const FieldId&, const FieldId&) = default;
};

// Primitive is a field kind only; a TypeDescriptor is Struct or Enum.
enum class TypeKind : std::uint8_t { Primitive, Struct, Enum };

// Kinds describe shape, not a C++ type (Plan 0048 P4): Vec3Float32 is three
// contiguous floats, whether a struct or a float[3].
enum class PrimitiveKind : std::uint8_t {
  UInt64,
  Float32,
  Vec3Float32,
  Vec4Float32,
  AssetGuid,   // 16 bytes
  EntityGuid,  // 16 bytes
};

[[nodiscard]] constexpr std::size_t primitiveSize(PrimitiveKind kind) noexcept {
  switch (kind) {
    case PrimitiveKind::UInt64: return 8;
    case PrimitiveKind::Float32: return 4;
    case PrimitiveKind::Vec3Float32: return 12;
    case PrimitiveKind::Vec4Float32: return 16;
    case PrimitiveKind::AssetGuid: return 16;
    case PrimitiveKind::EntityGuid: return 16;
  }
  return 0;
}

[[nodiscard]] constexpr std::size_t primitiveAlignment(PrimitiveKind kind) noexcept {
  switch (kind) {
    case PrimitiveKind::UInt64: return 8;
    case PrimitiveKind::Float32: return 4;
    case PrimitiveKind::Vec3Float32: return 4;
    case PrimitiveKind::Vec4Float32: return 4;
    case PrimitiveKind::AssetGuid: return 1;
    case PrimitiveKind::EntityGuid: return 1;
  }
  return 0;
}

// Flags describe meaning (ADR-0099 D6). AssetReference and EntityReference
// are mutually exclusive. Optional marks a std::optional member.
enum class FieldFlags : std::uint16_t {
  None = 0,
  Serializable = 1u << 0,     // round-trips through a committed format or codec
  Editable = 1u << 1,         // on the live World editing surface
  AssetReference = 1u << 2,   // names an asset (AssetId key or AssetGuid)
  EntityReference = 1u << 3,  // names an entity (EntityGuid)
  Optional = 1u << 4,         // absent-able: std::optional in the C++ type
};

[[nodiscard]] constexpr FieldFlags operator|(FieldFlags a, FieldFlags b) noexcept {
  return static_cast<FieldFlags>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b));
}

[[nodiscard]] constexpr FieldFlags operator&(FieldFlags a, FieldFlags b) noexcept {
  return static_cast<FieldFlags>(static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(b));
}

[[nodiscard]] constexpr bool hasFlags(FieldFlags value, FieldFlags required) noexcept {
  return (value & required) == required;
}

struct FieldDescriptor {
  FieldId id;
  std::string_view name;
  TypeKind kind = TypeKind::Primitive;
  PrimitiveKind primitive = PrimitiveKind::UInt64;  // meaningful when kind == Primitive
  TypeId type;                                      // meaningful otherwise; {0} for primitives
  FieldFlags flags = FieldFlags::None;
  std::uint32_t byteOffset = 0;  // offsetof the member in its standard-layout type
};

struct EnumConstantDescriptor {
  std::string_view name;
  std::int64_t value = 0;
};

struct TypeDescriptor {
  TypeId id;
  std::string_view name;  // qualified below atlantis::, e.g. "world::Camera"
  TypeKind kind = TypeKind::Struct;
  std::uint32_t schemaVersion = 1;  // bumped on any field-set change (Spec 0048 R8)
  std::span<const FieldDescriptor> fields;            // Struct
  std::span<const EnumConstantDescriptor> constants;  // Enum
};

[[nodiscard]] constexpr std::uint64_t fnv1a64(std::string_view text) noexcept {
  std::uint64_t hash = 0xcbf29ce484222325ULL;
  for (const char c : text) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001b3ULL;
  }
  return hash;
}

[[nodiscard]] constexpr TypeId typeId(std::string_view qualifiedName) noexcept {
  return TypeId{fnv1a64(qualifiedName)};
}

// Equal to fnv1a64("<qualifiedType>.<fieldName>"), hashed without
// concatenating.
[[nodiscard]] constexpr FieldId fieldId(std::string_view qualifiedType, std::string_view fieldName) noexcept {
  std::uint64_t hash = fnv1a64(qualifiedType);
  hash ^= static_cast<unsigned char>('.');
  hash *= 0x100000001b3ULL;
  for (const char c : fieldName) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001b3ULL;
  }
  return FieldId{hash};
}

namespace detail {

[[nodiscard]] constexpr bool isIdentifier(std::string_view text) noexcept {
  if (text.empty()) return false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    const bool digit = c >= '0' && c <= '9';
    if (!(letter || (digit && i > 0))) return false;
  }
  return true;
}

// At least "namespace::Type"; every segment an identifier.
[[nodiscard]] constexpr bool isQualifiedName(std::string_view text) noexcept {
  std::size_t segments = 0;
  while (true) {
    const std::size_t separator = text.find("::");
    if (!isIdentifier(text.substr(0, separator))) return false;
    ++segments;
    if (separator == std::string_view::npos) break;
    text.remove_prefix(separator + 2);
  }
  return segments >= 2;
}

[[nodiscard]] constexpr const TypeDescriptor* findType(std::span<const TypeDescriptor> table, TypeId id) noexcept {
  for (const TypeDescriptor& type : table) {
    if (type.id == id) return &type;
  }
  return nullptr;
}

[[nodiscard]] constexpr bool isWellFormedField(std::span<const TypeDescriptor> table, const TypeDescriptor& owner,
                                               const FieldDescriptor& field) noexcept {
  if (!isIdentifier(field.name) || field.id != fieldId(owner.name, field.name)) return false;
  if (hasFlags(field.flags, FieldFlags::AssetReference | FieldFlags::EntityReference)) return false;
  if (field.kind == TypeKind::Primitive) return field.type == TypeId{};
  const TypeDescriptor* referenced = findType(table, field.type);
  return referenced != nullptr && referenced->kind == field.kind;
}

}  // namespace detail

// Compile-time verification of one module's table (Plan 0048 P7): IDs match
// their names, kinds match their contents, names are well formed and unique,
// reference flags are exclusive, and every Struct/Enum field resolves in the
// same table to a type of that kind.
[[nodiscard]] constexpr bool isWellFormed(std::span<const TypeDescriptor> table) noexcept {
  for (std::size_t t = 0; t < table.size(); ++t) {
    const TypeDescriptor& type = table[t];
    if (!detail::isQualifiedName(type.name) || type.id != typeId(type.name)) return false;
    if (type.schemaVersion < 1) return false;
    for (std::size_t other = 0; other < t; ++other) {
      if (table[other].name == type.name || table[other].id == type.id) return false;
    }
    switch (type.kind) {
      case TypeKind::Primitive: return false;
      case TypeKind::Struct:
        if (type.fields.empty() || !type.constants.empty()) return false;
        break;
      case TypeKind::Enum:
        if (type.constants.empty() || !type.fields.empty()) return false;
        break;
    }
    for (std::size_t f = 0; f < type.fields.size(); ++f) {
      if (!detail::isWellFormedField(table, type, type.fields[f])) return false;
      for (std::size_t other = 0; other < f; ++other) {
        if (type.fields[other].name == type.fields[f].name) return false;
      }
    }
    for (std::size_t c = 0; c < type.constants.size(); ++c) {
      if (!detail::isIdentifier(type.constants[c].name)) return false;
      for (std::size_t other = 0; other < c; ++other) {
        if (type.constants[other].name == type.constants[c].name) return false;
      }
    }
  }
  return true;
}

[[nodiscard]] std::string_view toString(TypeKind kind) noexcept;
[[nodiscard]] std::string_view toString(PrimitiveKind kind) noexcept;

}  // namespace atlantis::schema
