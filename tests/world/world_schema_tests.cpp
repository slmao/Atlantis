#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include <atlantis/world/camera.h>
#include <atlantis/world/light.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/vec3.h>

// Plan 0048 M2 / P9 (Spec 0048 Testing plan): World's descriptor tables
// against the C++ types they describe. A member added, removed or renamed
// without a table change fails this test's build or its checks.

namespace {

using atlantis::schema::FieldDescriptor;
using atlantis::schema::FieldFlags;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeKind;
using namespace atlantis::world;

template <typename Scope, typename Member>
struct Probe {
  std::string_view name;
  std::int64_t offsetOrValue = 0;  // offsetof for a member, the value for an enumerator
};

// Plan 0048 P9 / ruling J4: the one test-only stringizing macro. It ties a
// name string to a real member or enumerator token, so a header rename breaks
// this test's compile. Never used in headers or descriptor tables.
#define ATLANTIS_SCHEMA_PROBE(Scope, token)                                     \
  (Probe<Scope, decltype(Scope::token)>{#token, [](auto tag) {                  \
     using S = typename decltype(tag)::type;                                    \
     if constexpr (std::is_enum_v<S>) {                                         \
       return static_cast<std::int64_t>(S::token);                              \
     } else {                                                                   \
       return static_cast<std::int64_t>(offsetof(S, token));                    \
     }                                                                          \
   }(std::type_identity<Scope>{})})

template <typename T>
constexpr std::string_view kQualifiedName{};
template <>
constexpr std::string_view kQualifiedName<CameraFog> = "world::CameraFog";
template <>
constexpr std::string_view kQualifiedName<CameraBloom> = "world::CameraBloom";
template <>
constexpr std::string_view kQualifiedName<LightKind> = "world::LightKind";

template <typename T>
constexpr std::optional<PrimitiveKind> primitiveKindOf() {
  if constexpr (std::is_same_v<T, std::uint64_t>) {
    return PrimitiveKind::UInt64;
  } else if constexpr (std::is_same_v<T, float>) {
    return PrimitiveKind::Float32;
  } else if constexpr (std::is_same_v<T, Vec3>) {
    return PrimitiveKind::Vec3Float32;
  } else {
    return std::nullopt;
  }
}

template <typename T>
struct OptionalOf {
  using type = T;
  static constexpr bool value = false;
};
template <typename T>
struct OptionalOf<std::optional<T>> {
  using type = T;
  static constexpr bool value = true;
};

constexpr FieldFlags kComponent = FieldFlags::Serializable | FieldFlags::Editable;

[[nodiscard]] const TypeDescriptor& requireType(std::string_view name) {
  const TypeDescriptor* found = nullptr;
  for (const TypeDescriptor& type : worldSchema()) {
    if (type.name == name) found = &type;
  }
  INFO(name);
  REQUIRE(found != nullptr);
  return *found;
}

template <typename Scope, typename Member>
void expectField(const TypeDescriptor& owner, const Probe<Scope, Member>& probe, FieldFlags flags) {
  INFO(owner.name << "." << probe.name);
  const FieldDescriptor* field = nullptr;
  for (const FieldDescriptor& candidate : owner.fields) {
    if (candidate.name == probe.name) field = &candidate;
  }
  REQUIRE(field != nullptr);
  CHECK(field->byteOffset == static_cast<std::uint32_t>(probe.offsetOrValue));
  CHECK(field->flags == flags);

  using Value = typename OptionalOf<Member>::type;
  CHECK(atlantis::schema::hasFlags(field->flags, FieldFlags::Optional) == OptionalOf<Member>::value);
  if constexpr (std::is_enum_v<Value>) {
    CHECK(field->kind == TypeKind::Enum);
    CHECK(field->type == atlantis::schema::typeId(kQualifiedName<Value>));
  } else if constexpr (!kQualifiedName<Value>.empty()) {
    CHECK(field->kind == TypeKind::Struct);
    CHECK(field->type == atlantis::schema::typeId(kQualifiedName<Value>));
  } else {
    constexpr std::optional<PrimitiveKind> expected = primitiveKindOf<Value>();
    static_assert(expected.has_value(), "member type has no primitive kind mapping");
    CHECK(field->kind == TypeKind::Primitive);
    CHECK(field->primitive == *expected);
    CHECK(sizeof(Value) == atlantis::schema::primitiveSize(*expected));
    CHECK(alignof(Value) == atlantis::schema::primitiveAlignment(*expected));
  }
}

// Enumerator completeness canary: no default, and this target builds with
// /w14062, so an unhandled enumerator is a compile error. A new enumerator
// handled here but missing from the table fails expectEnum's past-the-end
// check.
[[nodiscard]] Probe<LightKind, LightKind> lightKindProbe(LightKind kind) {
  switch (kind) {
    case LightKind::Directional: return ATLANTIS_SCHEMA_PROBE(LightKind, Directional);
    case LightKind::Point: return ATLANTIS_SCHEMA_PROBE(LightKind, Point);
  }
  return {};
}

template <typename Enum, typename ProbeFn>
void expectEnum(const TypeDescriptor& type, ProbeFn probeOf) {
  REQUIRE(type.kind == TypeKind::Enum);
  for (std::size_t i = 0; i < type.constants.size(); ++i) {
    const auto& constant = type.constants[i];
    INFO(type.name << "::" << constant.name);
    CHECK(constant.value == static_cast<std::int64_t>(i));  // contiguous from 0
    const auto probe = probeOf(static_cast<Enum>(constant.value));
    CHECK(probe.name == constant.name);
    CHECK(probe.offsetOrValue == constant.value);
  }
  CHECK(probeOf(static_cast<Enum>(type.constants.size())).name.empty());
}

}  // namespace

TEST_CASE("world schema: listing order and table check", "[world][schema]") {
  constexpr std::array<std::string_view, 7> kExpected{
      "world::Transform", "world::CameraFog", "world::CameraBloom", "world::Camera",
      "world::Light",     "world::LightKind", "world::Renderable",
  };
  const auto schema = worldSchema();
  REQUIRE(schema.size() == kExpected.size());
  for (std::size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(schema[i].name == kExpected[i]);
    CHECK(schema[i].schemaVersion == 1);
  }
  CHECK(atlantis::schema::isWellFormed(schema));
}

TEST_CASE("world schema: Transform", "[world][schema]") {
  [[maybe_unused]] const auto& [localPosition, localEulerAnglesRadians, localScale] = Transform{};
  const TypeDescriptor& type = requireType("world::Transform");
  CHECK(type.fields.size() == 3);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Transform, localPosition), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Transform, localEulerAnglesRadians), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Transform, localScale), kComponent);
}

TEST_CASE("world schema: CameraFog", "[world][schema]") {
  [[maybe_unused]] const auto& [color, density, height, heightFalloff, maxOpacity] = CameraFog{};
  const TypeDescriptor& type = requireType("world::CameraFog");
  CHECK(type.fields.size() == 5);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, color), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, density), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, height), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, heightFalloff), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, maxOpacity), kComponent);
}

TEST_CASE("world schema: CameraBloom", "[world][schema]") {
  [[maybe_unused]] const auto& [strength, threshold] = CameraBloom{};
  const TypeDescriptor& type = requireType("world::CameraBloom");
  CHECK(type.fields.size() == 2);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraBloom, strength), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraBloom, threshold), kComponent);
}

TEST_CASE("world schema: Camera", "[world][schema]") {
  [[maybe_unused]] const auto& [fovYRadians, nearZ, farZ, exposureCompensationEv, fog, bloom] = Camera{};
  const TypeDescriptor& type = requireType("world::Camera");
  CHECK(type.fields.size() == 6);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, fovYRadians), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, nearZ), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, farZ), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, exposureCompensationEv), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, fog), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, bloom), kComponent);
}

TEST_CASE("world schema: Light and LightKind", "[world][schema]") {
  [[maybe_unused]] const auto& [kind, color, intensity, range] = Light{};
  const TypeDescriptor& type = requireType("world::Light");
  CHECK(type.fields.size() == 4);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, kind), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, color), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, intensity), kComponent);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, range), kComponent);

  expectEnum<LightKind>(requireType("world::LightKind"), lightKindProbe);
}

TEST_CASE("world schema: Renderable", "[world][schema]") {
  [[maybe_unused]] const auto& [meshAsset, materialAsset] = Renderable{};
  const TypeDescriptor& type = requireType("world::Renderable");
  CHECK(type.fields.size() == 2);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Renderable, meshAsset), kComponent | FieldFlags::AssetReference);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Renderable, materialAsset),
              kComponent | FieldFlags::AssetReference | FieldFlags::Optional);
}
