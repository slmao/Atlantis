#include <atlantis/asset_system/asset_system_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/entity_ref.h>
#include <atlantis/asset_system/material_types.h>

// Plan 0048 M3 / P9 (Spec 0048 Testing plan): Asset System's descriptor
// tables against the C++ types they describe. A member added, removed or
// renamed without a table change fails this test's build or its checks.

namespace {

using atlantis::schema::FieldDescriptor;
using atlantis::schema::FieldFlags;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeKind;
using namespace atlantis::asset_system;

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
constexpr std::string_view kQualifiedName<MaterialKind> = "asset_system::MaterialKind";
template <>
constexpr std::string_view kQualifiedName<MaterialAlphaMode> = "asset_system::MaterialAlphaMode";
template <>
constexpr std::string_view kQualifiedName<MaterialSamplerFilter> = "asset_system::MaterialSamplerFilter";
template <>
constexpr std::string_view kQualifiedName<MaterialSamplerAddressMode> = "asset_system::MaterialSamplerAddressMode";

template <typename T>
constexpr std::optional<PrimitiveKind> primitiveKindOf() {
  if constexpr (std::is_same_v<T, std::uint64_t>) {
    return PrimitiveKind::UInt64;
  } else if constexpr (std::is_same_v<T, float>) {
    return PrimitiveKind::Float32;
  } else if constexpr (std::is_same_v<T, float[3]>) {
    return PrimitiveKind::Vec3Float32;
  } else if constexpr (std::is_same_v<T, float[4]>) {
    return PrimitiveKind::Vec4Float32;
  } else if constexpr (std::is_same_v<T, AssetGuid>) {
    return PrimitiveKind::AssetGuid;
  } else if constexpr (std::is_same_v<T, EntityGuid>) {
    return PrimitiveKind::EntityGuid;
  } else {
    return std::nullopt;
  }
}

[[nodiscard]] const TypeDescriptor& requireType(std::string_view name) {
  const TypeDescriptor* found = nullptr;
  for (const TypeDescriptor& type : assetSystemSchema()) {
    if (type.name == name) found = &type;
  }
  INFO(name);
  REQUIRE(found != nullptr);
  return *found;
}

// No v1 Asset System field is std::optional (ruling J2), so Optional is
// expected nowhere here.
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
  if constexpr (std::is_enum_v<Member>) {
    CHECK(field->kind == TypeKind::Enum);
    CHECK(field->type == atlantis::schema::typeId(kQualifiedName<Member>));
  } else {
    constexpr std::optional<PrimitiveKind> expected = primitiveKindOf<Member>();
    static_assert(expected.has_value(), "member type has no primitive kind mapping");
    CHECK(field->kind == TypeKind::Primitive);
    CHECK(field->primitive == *expected);
    CHECK(sizeof(Member) == atlantis::schema::primitiveSize(*expected));
    CHECK(alignof(Member) == atlantis::schema::primitiveAlignment(*expected));
  }
}

// Enumerator completeness canaries: no default, and this target builds with
// /w14062, so an unhandled enumerator is a compile error. A new enumerator
// handled here but missing from the table fails expectEnum's past-the-end
// check.
[[nodiscard]] Probe<MaterialKind, MaterialKind> materialKindProbe(MaterialKind kind) {
  switch (kind) {
    case MaterialKind::UnlitTextured: return ATLANTIS_SCHEMA_PROBE(MaterialKind, UnlitTextured);
    case MaterialKind::LitTextured: return ATLANTIS_SCHEMA_PROBE(MaterialKind, LitTextured);
    case MaterialKind::PbrDirectLit: return ATLANTIS_SCHEMA_PROBE(MaterialKind, PbrDirectLit);
    case MaterialKind::PbrClearcoat: return ATLANTIS_SCHEMA_PROBE(MaterialKind, PbrClearcoat);
    case MaterialKind::PbrSheen: return ATLANTIS_SCHEMA_PROBE(MaterialKind, PbrSheen);
    case MaterialKind::PbrAnisotropic: return ATLANTIS_SCHEMA_PROBE(MaterialKind, PbrAnisotropic);
  }
  return {};
}

[[nodiscard]] Probe<MaterialAlphaMode, MaterialAlphaMode> alphaModeProbe(MaterialAlphaMode mode) {
  switch (mode) {
    case MaterialAlphaMode::Opaque: return ATLANTIS_SCHEMA_PROBE(MaterialAlphaMode, Opaque);
    case MaterialAlphaMode::Mask: return ATLANTIS_SCHEMA_PROBE(MaterialAlphaMode, Mask);
    case MaterialAlphaMode::Blend: return ATLANTIS_SCHEMA_PROBE(MaterialAlphaMode, Blend);
  }
  return {};
}

[[nodiscard]] Probe<MaterialSamplerFilter, MaterialSamplerFilter> samplerFilterProbe(MaterialSamplerFilter filter) {
  switch (filter) {
    case MaterialSamplerFilter::Nearest: return ATLANTIS_SCHEMA_PROBE(MaterialSamplerFilter, Nearest);
    case MaterialSamplerFilter::Linear: return ATLANTIS_SCHEMA_PROBE(MaterialSamplerFilter, Linear);
  }
  return {};
}

[[nodiscard]] Probe<MaterialSamplerAddressMode, MaterialSamplerAddressMode> addressModeProbe(
    MaterialSamplerAddressMode mode) {
  switch (mode) {
    case MaterialSamplerAddressMode::Repeat: return ATLANTIS_SCHEMA_PROBE(MaterialSamplerAddressMode, Repeat);
    case MaterialSamplerAddressMode::ClampToEdge:
      return ATLANTIS_SCHEMA_PROBE(MaterialSamplerAddressMode, ClampToEdge);
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

constexpr FieldFlags kData = FieldFlags::Serializable;
constexpr FieldFlags kAssetRef = FieldFlags::Serializable | FieldFlags::AssetReference;

}  // namespace

TEST_CASE("asset system schema: listing order and table check", "[asset_system][schema]") {
  constexpr std::array<std::string_view, 6> kExpected{
      "asset_system::MaterialAssetData",     "asset_system::MaterialKind",
      "asset_system::MaterialAlphaMode",     "asset_system::MaterialSamplerFilter",
      "asset_system::MaterialSamplerAddressMode", "asset_system::EntityRef",
  };
  const auto schema = assetSystemSchema();
  REQUIRE(schema.size() == kExpected.size());
  for (std::size_t i = 0; i < kExpected.size(); ++i) {
    CHECK(schema[i].name == kExpected[i]);
    CHECK(schema[i].schemaVersion == 1);
  }
  CHECK(atlantis::schema::isWellFormed(schema));
}

TEST_CASE("asset system schema: MaterialAssetData", "[asset_system][schema]") {
  [[maybe_unused]] const auto& [kind, textureAsset, filter, addressMode, baseColorFactor, metallicFactor,
                                roughnessFactor, normalMapTexture, clearcoatFactor, clearcoatRoughness, sheenColor,
                                sheenRoughness, anisotropyFactor, anisotropyRotation, emissiveFactor, alphaMode,
                                alphaCutoff, emissiveTexture] = MaterialAssetData{};
  const TypeDescriptor& type = requireType("asset_system::MaterialAssetData");
  CHECK(type.fields.size() == 18);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, kind), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, textureAsset), kAssetRef);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, filter), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, addressMode), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, baseColorFactor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, metallicFactor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, roughnessFactor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, normalMapTexture), kAssetRef);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, clearcoatFactor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, clearcoatRoughness), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, sheenColor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, sheenRoughness), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, anisotropyFactor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, anisotropyRotation), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, emissiveFactor), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, alphaMode), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, alphaCutoff), kData);
  expectField(type, ATLANTIS_SCHEMA_PROBE(MaterialAssetData, emissiveTexture), kAssetRef);
}

TEST_CASE("asset system schema: material enums", "[asset_system][schema]") {
  expectEnum<MaterialKind>(requireType("asset_system::MaterialKind"), materialKindProbe);
  expectEnum<MaterialAlphaMode>(requireType("asset_system::MaterialAlphaMode"), alphaModeProbe);
  expectEnum<MaterialSamplerFilter>(requireType("asset_system::MaterialSamplerFilter"), samplerFilterProbe);
  expectEnum<MaterialSamplerAddressMode>(requireType("asset_system::MaterialSamplerAddressMode"), addressModeProbe);
}

TEST_CASE("asset system schema: EntityRef", "[asset_system][schema]") {
  [[maybe_unused]] const auto& [scene, entity] = EntityRef{};
  const TypeDescriptor& type = requireType("asset_system::EntityRef");
  CHECK(type.fields.size() == 2);
  expectField(type, ATLANTIS_SCHEMA_PROBE(EntityRef, scene), kAssetRef);
  expectField(type, ATLANTIS_SCHEMA_PROBE(EntityRef, entity), FieldFlags::Serializable | FieldFlags::EntityReference);
}
