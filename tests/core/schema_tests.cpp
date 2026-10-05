#include <atlantis/schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

// Plan 0048 M1 (Spec 0048 R1-R3, ADR-0099 D3/D6): the vocabulary's identity
// derivation and its compile-time table check, on synthetic tables.

namespace {

using atlantis::schema::EnumConstantDescriptor;
using atlantis::schema::FieldDescriptor;
using atlantis::schema::FieldFlags;
using atlantis::schema::FieldId;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeId;
using atlantis::schema::TypeKind;
using atlantis::schema::fieldId;
using atlantis::schema::fnv1a64;
using atlantis::schema::isWellFormed;
using atlantis::schema::typeId;

// Reference vectors: the published FNV-1a-64 values for "", "a", "foobar".
static_assert(fnv1a64("") == 0xcbf29ce484222325ULL);
static_assert(fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
static_assert(fnv1a64("foobar") == 0x85944171f73967e8ULL);

// ID stability pins (Plan 0048 P3). Change only deliberately: a changed pin
// means every persisted or transmitted ID of that name changed.
static_assert(typeId("world::Camera").value == 0x8eeda43ea50d5544ULL);
static_assert(fieldId("world::Camera", "exposureCompensationEv").value == 0xdeb0a07e14e61af6ULL);
static_assert(typeId("asset_system::MaterialAssetData").value == 0x74e2bf2e4f643028ULL);
static_assert(fieldId("asset_system::EntityRef", "entity").value == 0xa63ced230044da30ULL);

static_assert(fieldId("world::Camera", "nearZ").value == fnv1a64("world::Camera.nearZ"));
static_assert(!std::is_convertible_v<TypeId, FieldId> && !std::is_convertible_v<FieldId, TypeId>);

constexpr std::string_view kInner = "test::Inner";
constexpr std::string_view kMode = "test::Mode";
constexpr std::string_view kOuter = "test::Outer";

constexpr FieldDescriptor primitiveField(std::string_view owner, std::string_view name, PrimitiveKind kind,
                                         FieldFlags flags, std::uint32_t offset) {
  return {fieldId(owner, name), name, TypeKind::Primitive, kind, TypeId{}, flags, offset};
}

constexpr FieldDescriptor referenceField(std::string_view owner, std::string_view name, TypeKind kind,
                                         std::string_view referenced, std::uint32_t offset) {
  return {fieldId(owner, name), name, kind, PrimitiveKind{}, typeId(referenced), FieldFlags::Serializable, offset};
}

// A valid three-type table: a struct, an enum, and a struct referencing both.
// Each test copies it, breaks exactly one rule, and re-points the spans.
struct Fixture {
  std::array<FieldDescriptor, 1> innerFields{
      primitiveField(kInner, "value", PrimitiveKind::Float32, FieldFlags::Serializable, 0)};
  std::array<EnumConstantDescriptor, 2> modeConstants{{{"A", 0}, {"B", 1}}};
  std::array<FieldDescriptor, 3> outerFields{
      referenceField(kOuter, "inner", TypeKind::Struct, kInner, 0),
      referenceField(kOuter, "mode", TypeKind::Enum, kMode, 4),
      primitiveField(kOuter, "asset", PrimitiveKind::UInt64,
                     FieldFlags::Serializable | FieldFlags::AssetReference | FieldFlags::Optional, 8)};
  std::array<TypeDescriptor, 3> types{{
      {typeId(kInner), kInner, TypeKind::Struct, 1, {}, {}},
      {typeId(kMode), kMode, TypeKind::Enum, 1, {}, {}},
      {typeId(kOuter), kOuter, TypeKind::Struct, 1, {}, {}},
  }};

  [[nodiscard]] std::span<const TypeDescriptor> table() {
    types[0].fields = innerFields;
    types[1].constants = modeConstants;
    types[2].fields = outerFields;
    return types;
  }
};

constexpr std::array<FieldDescriptor, 1> kStaticFields{
    primitiveField("test::Point", "x", PrimitiveKind::Float32, FieldFlags::Serializable, 0)};
constexpr std::array<TypeDescriptor, 1> kStaticTable{
    {{typeId("test::Point"), "test::Point", TypeKind::Struct, 1, kStaticFields, {}}}};
static_assert(isWellFormed(kStaticTable));

}  // namespace

TEST_CASE("schema: flag operators combine and test bits", "[core][schema]") {
  constexpr FieldFlags flags = FieldFlags::Serializable | FieldFlags::AssetReference;
  STATIC_REQUIRE(atlantis::schema::hasFlags(flags, FieldFlags::Serializable));
  STATIC_REQUIRE(atlantis::schema::hasFlags(flags, FieldFlags::Serializable | FieldFlags::AssetReference));
  STATIC_REQUIRE_FALSE(atlantis::schema::hasFlags(flags, FieldFlags::Editable));
  STATIC_REQUIRE((flags & FieldFlags::Optional) == FieldFlags::None);
}

TEST_CASE("schema: primitive shapes", "[core][schema]") {
  STATIC_REQUIRE(atlantis::schema::primitiveSize(PrimitiveKind::Vec3Float32) == 3 * sizeof(float));
  STATIC_REQUIRE(atlantis::schema::primitiveSize(PrimitiveKind::Vec4Float32) == 4 * sizeof(float));
  STATIC_REQUIRE(atlantis::schema::primitiveSize(PrimitiveKind::UInt64) == sizeof(std::uint64_t));
  STATIC_REQUIRE(atlantis::schema::primitiveAlignment(PrimitiveKind::Vec3Float32) == alignof(float));
  CHECK(atlantis::schema::toString(PrimitiveKind::EntityGuid) == "EntityGuid");
  CHECK(atlantis::schema::toString(TypeKind::Enum) == "Enum");
}

TEST_CASE("schema: isWellFormed accepts the valid fixture", "[core][schema]") {
  Fixture fixture;
  CHECK(isWellFormed(fixture.table()));
}

TEST_CASE("schema: isWellFormed rejects each rule violated alone", "[core][schema]") {
  Fixture f;
  std::span<const TypeDescriptor> table = f.table();  // spans alias f's arrays
  std::array<TypeDescriptor, 4> extended{};

  SECTION("type id does not match its name") { f.types[2].id = typeId("test::Other"); }
  SECTION("field id does not match its name") { f.innerFields[0].id = fieldId(kInner, "other"); }
  SECTION("schema version 0") { f.types[2].schemaVersion = 0; }
  SECTION("a type of kind Primitive") { f.types[2].kind = TypeKind::Primitive; }
  SECTION("struct without fields") { f.types[0].fields = {}; }
  SECTION("struct with constants") { f.types[0].constants = f.modeConstants; }
  SECTION("enum without constants") { f.types[1].constants = {}; }
  SECTION("field name not an identifier") {
    f.innerFields[0].name = "1value";
    f.innerFields[0].id = fieldId(kInner, "1value");
  }
  SECTION("duplicate field name") {
    f.outerFields[2].name = "inner";
    f.outerFields[2].id = fieldId(kOuter, "inner");
  }
  SECTION("duplicate constant name") { f.modeConstants[1].name = "A"; }
  SECTION("constant name not an identifier") { f.modeConstants[1].name = "B-"; }
  SECTION("duplicate type") {
    extended = {f.types[0], f.types[1], f.types[2], f.types[0]};
    table = extended;
  }
  SECTION("both reference flags on one field") {
    f.outerFields[2].flags = f.outerFields[2].flags | FieldFlags::EntityReference;
  }
  SECTION("primitive field naming a type") { f.innerFields[0].type = typeId(kMode); }
  SECTION("reference to a type missing from the table") { f.outerFields[0].type = typeId("test::Missing"); }
  SECTION("reference to a type of the other kind") { f.outerFields[1].type = typeId(kInner); }

  CHECK_FALSE(isWellFormed(table));
}

TEST_CASE("schema: isWellFormed rejects an unqualified or malformed type name", "[core][schema]") {
  for (const std::string_view name : {std::string_view{"Mode"}, std::string_view{"test::"},
                                      std::string_view{"test:Mode"}, std::string_view{"test::2Mode"}}) {
    constexpr std::array<EnumConstantDescriptor, 1> constants{{{"A", 0}}};
    const std::array<TypeDescriptor, 1> table{{{typeId(name), name, TypeKind::Enum, 1, {}, constants}}};
    INFO(name);
    CHECK_FALSE(isWellFormed(table));
  }
}
