#pragma once

#include <atlantis/schema.h>

#include <array>
#include <span>
#include <string_view>

// Plan 0057 P3/M1 (Spec 0057 R3, R7, R10): a synthetic descriptor table that
// exercises what worldSchema() does not -- every PrimitiveKind, nesting, an
// Optional field, a read-only leaf, a long name, and enums whose values are
// non-contiguous, negative, nonzero first, or have no 0 at all. Its generated
// header is committed at tests/gameplay_sdk/generated/synthetic.h and checked
// for staleness like World's. Byte offsets are 0: nothing here is a C++ type.
namespace atlantis::test::synthetic_schema {

using schema::EnumConstantDescriptor;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeKind;

inline constexpr std::string_view kMode = "synthetic::Mode";
inline constexpr std::string_view kPhase = "synthetic::Phase";
inline constexpr std::string_view kInner = "synthetic::Inner";
inline constexpr std::string_view kProbe = "synthetic::Probe";
inline constexpr std::string_view kLocked = "synthetic::Locked";

inline constexpr FieldFlags kEditable = FieldFlags::Serializable | FieldFlags::Editable;

constexpr FieldDescriptor primitive(std::string_view owner, std::string_view name, PrimitiveKind kind,
                                    FieldFlags flags) {
  return {schema::fieldId(owner, name), name, TypeKind::Primitive, kind, schema::TypeId{}, flags, 0};
}

constexpr FieldDescriptor reference(std::string_view owner, std::string_view name, TypeKind kind,
                                    std::string_view referenced, FieldFlags flags) {
  return {schema::fieldId(owner, name), name, kind, PrimitiveKind{}, schema::typeId(referenced), flags, 0};
}

// No 0, a nonzero first constant, a negative value, non-contiguous.
inline constexpr std::array kModeConstants{
    EnumConstantDescriptor{"A", 3},
    EnumConstantDescriptor{"B", -2},
    EnumConstantDescriptor{"C", 7},
};

// 0 declared, but not first.
inline constexpr std::array kPhaseConstants{
    EnumConstantDescriptor{"First", 5},
    EnumConstantDescriptor{"Zero", 0},
    EnumConstantDescriptor{"Last", 9},
};

inline constexpr std::array kInnerFields{
    primitive(kInner, "weight", PrimitiveKind::Float32, kEditable),
    primitive(kInner, "tint", PrimitiveKind::Vec4Float32, kEditable),
};

inline constexpr std::array kProbeFields{
    primitive(kProbe, "count", PrimitiveKind::UInt64, kEditable),
    primitive(kProbe, "scale", PrimitiveKind::Float32, kEditable),
    primitive(kProbe, "offset", PrimitiveKind::Vec3Float32, kEditable),
    primitive(kProbe, "color", PrimitiveKind::Vec4Float32, kEditable),
    primitive(kProbe, "asset", PrimitiveKind::AssetGuid, kEditable | FieldFlags::AssetReference),
    primitive(kProbe, "target", PrimitiveKind::EntityGuid, kEditable | FieldFlags::EntityReference),
    primitive(kProbe, "maybe", PrimitiveKind::UInt64, kEditable | FieldFlags::Optional),
    reference(kProbe, "mode", TypeKind::Enum, kMode, kEditable),
    reference(kProbe, "phase", TypeKind::Enum, kPhase, kEditable),
    reference(kProbe, "inner", TypeKind::Struct, kInner, kEditable),
    primitive(kProbe, "aVeryLongFieldNameThatExercisesTheGeneratorWithoutAnyWrappingAtAll", PrimitiveKind::Float32,
              kEditable),
};

// One read-only leaf (no Editable flag) beside an editable one.
inline constexpr std::array kLockedFields{
    primitive(kLocked, "fixed", PrimitiveKind::Float32, FieldFlags::Serializable),
    primitive(kLocked, "free", PrimitiveKind::Float32, kEditable),
};

inline constexpr std::array kTable{
    TypeDescriptor{schema::typeId(kMode), kMode, TypeKind::Enum, 1, {}, kModeConstants},
    TypeDescriptor{schema::typeId(kPhase), kPhase, TypeKind::Enum, 1, {}, kPhaseConstants},
    TypeDescriptor{schema::typeId(kInner), kInner, TypeKind::Struct, 2, kInnerFields, {}},
    TypeDescriptor{schema::typeId(kProbe), kProbe, TypeKind::Struct, 3, kProbeFields, {}},
    TypeDescriptor{schema::typeId(kLocked), kLocked, TypeKind::Struct, 1, kLockedFields, {}},
};

static_assert(schema::isWellFormed(kTable));

[[nodiscard]] constexpr std::span<const TypeDescriptor> table() noexcept { return kTable; }

}  // namespace atlantis::test::synthetic_schema
