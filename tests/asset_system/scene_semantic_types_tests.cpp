#include <atlantis/asset_system/scene_semantic_types.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_system_schema.h>

// Plan 0049 M1 / P2 (Spec 0049 R1): the seven semantic component types'
// descriptors against their C++ types, in Spec 0048's sync-test pattern.

namespace {

using atlantis::schema::FieldDescriptor;
using atlantis::schema::FieldFlags;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeKind;
using namespace atlantis::asset_system::scene;
using atlantis::asset_system::AssetGuid;

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
constexpr std::string_view kQualifiedName<CameraFog> = "asset_system::scene::CameraFog";
template <>
constexpr std::string_view kQualifiedName<CameraBloom> = "asset_system::scene::CameraBloom";
template <>
constexpr std::string_view kQualifiedName<LightKind> = "asset_system::scene::LightKind";

template <typename T>
constexpr std::optional<PrimitiveKind> primitiveKindOf() {
  if constexpr (std::is_same_v<T, float>) {
    return PrimitiveKind::Float32;
  } else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
    return PrimitiveKind::Vec3Float32;
  } else if constexpr (std::is_same_v<T, AssetGuid>) {
    return PrimitiveKind::AssetGuid;
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

// Plan 0049 P2 (ruling J6): every semantic field is Serializable only.
constexpr FieldFlags kData = FieldFlags::Serializable;
constexpr FieldFlags kAssetRef = FieldFlags::Serializable | FieldFlags::AssetReference;

[[nodiscard]] const TypeDescriptor& requireType(std::string_view name) {
  const TypeDescriptor* found = nullptr;
  for (const TypeDescriptor& type : atlantis::asset_system::assetSystemSchema()) {
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
// handled here but missing from the table fails the past-the-end check.
[[nodiscard]] Probe<LightKind, LightKind> lightKindProbe(LightKind kind) {
  switch (kind) {
    case LightKind::Directional: return ATLANTIS_SCHEMA_PROBE(LightKind, Directional);
    case LightKind::Point: return ATLANTIS_SCHEMA_PROBE(LightKind, Point);
  }
  return {};
}

}  // namespace

TEST_CASE("scene semantic types: Transform", "[asset_system][schema][scene]") {
  [[maybe_unused]] const auto& [localPosition, localEulerAnglesRadians, localScale] = Transform{};
  const TypeDescriptor& type = requireType("asset_system::scene::Transform");
  CHECK(type.fields.size() == 3);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Transform, localPosition), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Transform, localEulerAnglesRadians), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Transform, localScale), kData);
}

TEST_CASE("scene semantic types: CameraFog", "[asset_system][schema][scene]") {
  [[maybe_unused]] const auto& [color, density, height, heightFalloff, maxOpacity] = CameraFog{};
  const TypeDescriptor& type = requireType("asset_system::scene::CameraFog");
  CHECK(type.fields.size() == 5);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, color), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, density), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, height), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, heightFalloff), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraFog, maxOpacity), kData);
}

TEST_CASE("scene semantic types: CameraBloom", "[asset_system][schema][scene]") {
  [[maybe_unused]] const auto& [strength, threshold] = CameraBloom{};
  const TypeDescriptor& type = requireType("asset_system::scene::CameraBloom");
  CHECK(type.fields.size() == 2);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraBloom, strength), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(CameraBloom, threshold), kData);
}

TEST_CASE("scene semantic types: Camera", "[asset_system][schema][scene]") {
  [[maybe_unused]] const auto& [fovYRadians, nearZ, farZ, exposureCompensationEv, fog, bloom] = Camera{};
  const TypeDescriptor& type = requireType("asset_system::scene::Camera");
  CHECK(type.fields.size() == 6);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, fovYRadians), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, nearZ), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, farZ), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, exposureCompensationEv), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, fog), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Camera, bloom), kData);
}

TEST_CASE("scene semantic types: Light and LightKind", "[asset_system][schema][scene]") {
  [[maybe_unused]] const auto& [kind, color, intensity, range] = Light{};
  const TypeDescriptor& type = requireType("asset_system::scene::Light");
  CHECK(type.fields.size() == 4);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, kind), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, color), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, intensity), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Light, range), kData);

  const TypeDescriptor& lightKind = requireType("asset_system::scene::LightKind");
  REQUIRE(lightKind.kind == TypeKind::Enum);
  for (std::size_t i = 0; i < lightKind.constants.size(); ++i) {
    const auto& constant = lightKind.constants[i];
    INFO(constant.name);
    CHECK(constant.value == static_cast<std::int64_t>(i));  // contiguous from 0
    const auto probe = lightKindProbe(static_cast<LightKind>(constant.value));
    CHECK(probe.name == constant.name);
    CHECK(probe.offsetOrValue == constant.value);
  }
  CHECK(lightKindProbe(static_cast<LightKind>(lightKind.constants.size())).name.empty());
}

TEST_CASE("scene semantic types: Renderable", "[asset_system][schema][scene]") {
  [[maybe_unused]] const auto& [meshAsset, materialAsset] = Renderable{};
  const TypeDescriptor& type = requireType("asset_system::scene::Renderable");
  CHECK(type.fields.size() == 2);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Renderable, meshAsset), kAssetRef);
  expectField(type, ATLANTIS_SCHEMA_PROBE(Renderable, materialAsset), kAssetRef | FieldFlags::Optional);
}

TEST_CASE("scene semantic types: every descriptor at SchemaVersion 1", "[asset_system][schema][scene]") {
  for (const std::string_view name :
       {"asset_system::scene::Transform", "asset_system::scene::Camera", "asset_system::scene::CameraFog",
        "asset_system::scene::CameraBloom", "asset_system::scene::Renderable", "asset_system::scene::Light",
        "asset_system::scene::LightKind"}) {
    INFO(name);
    CHECK(requireType(name).schemaVersion == 1);
  }
}
