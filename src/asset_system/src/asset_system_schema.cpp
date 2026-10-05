#include <atlantis/asset_system/asset_system_schema.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include <atlantis/asset_system/entity_ref.h>
#include <atlantis/asset_system/material_types.h>

namespace atlantis::asset_system {

namespace {

using schema::EnumConstantDescriptor;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeKind;

// Byte offsets are only meaningful for standard-layout types (Spec 0048
// Non-functional). Checked here so the Android build verifies them too.
static_assert(std::is_standard_layout_v<MaterialAssetData>);
static_assert(std::is_standard_layout_v<EntityRef>);

// Plan 0048 P6 (rulings J2, J3): every field is Serializable; none is
// Editable (a cooked load result and a reference value); the material
// texture references use the 0 = none sentinel and are not Optional.
constexpr FieldFlags kData = FieldFlags::Serializable;
constexpr FieldFlags kAssetRef = FieldFlags::Serializable | FieldFlags::AssetReference;

constexpr FieldDescriptor primitive(std::string_view owner, std::string_view name, PrimitiveKind kind,
                                    FieldFlags flags, std::size_t offset) {
  return {schema::fieldId(owner, name), name,  TypeKind::Primitive, kind,
          schema::TypeId{},             flags, static_cast<std::uint32_t>(offset)};
}

constexpr FieldDescriptor enumField(std::string_view owner, std::string_view name, std::string_view enumType,
                                    std::size_t offset) {
  return {schema::fieldId(owner, name), name,  TypeKind::Enum, PrimitiveKind{},
          schema::typeId(enumType),     kData, static_cast<std::uint32_t>(offset)};
}

constexpr TypeDescriptor structType(std::string_view name, std::span<const FieldDescriptor> fields) {
  return {schema::typeId(name), name, TypeKind::Struct, 1, fields, {}};
}

constexpr TypeDescriptor enumType(std::string_view name, std::span<const EnumConstantDescriptor> constants) {
  return {schema::typeId(name), name, TypeKind::Enum, 1, {}, constants};
}

template <typename Enum>
constexpr EnumConstantDescriptor constant(std::string_view name, Enum value) {
  return {name, static_cast<std::int64_t>(value)};
}

constexpr std::string_view kMaterial = "asset_system::MaterialAssetData";
constexpr std::string_view kMaterialKind = "asset_system::MaterialKind";
constexpr std::string_view kAlphaMode = "asset_system::MaterialAlphaMode";
constexpr std::string_view kSamplerFilter = "asset_system::MaterialSamplerFilter";
constexpr std::string_view kAddressMode = "asset_system::MaterialSamplerAddressMode";
constexpr std::string_view kEntityRef = "asset_system::EntityRef";

using M = MaterialAssetData;
constexpr std::array kMaterialFields{
    enumField(kMaterial, "kind", kMaterialKind, offsetof(M, kind)),
    primitive(kMaterial, "textureAsset", PrimitiveKind::UInt64, kAssetRef, offsetof(M, textureAsset)),
    enumField(kMaterial, "filter", kSamplerFilter, offsetof(M, filter)),
    enumField(kMaterial, "addressMode", kAddressMode, offsetof(M, addressMode)),
    primitive(kMaterial, "baseColorFactor", PrimitiveKind::Vec4Float32, kData, offsetof(M, baseColorFactor)),
    primitive(kMaterial, "metallicFactor", PrimitiveKind::Float32, kData, offsetof(M, metallicFactor)),
    primitive(kMaterial, "roughnessFactor", PrimitiveKind::Float32, kData, offsetof(M, roughnessFactor)),
    primitive(kMaterial, "normalMapTexture", PrimitiveKind::UInt64, kAssetRef, offsetof(M, normalMapTexture)),
    primitive(kMaterial, "clearcoatFactor", PrimitiveKind::Float32, kData, offsetof(M, clearcoatFactor)),
    primitive(kMaterial, "clearcoatRoughness", PrimitiveKind::Float32, kData, offsetof(M, clearcoatRoughness)),
    primitive(kMaterial, "sheenColor", PrimitiveKind::Vec3Float32, kData, offsetof(M, sheenColor)),
    primitive(kMaterial, "sheenRoughness", PrimitiveKind::Float32, kData, offsetof(M, sheenRoughness)),
    primitive(kMaterial, "anisotropyFactor", PrimitiveKind::Float32, kData, offsetof(M, anisotropyFactor)),
    primitive(kMaterial, "anisotropyRotation", PrimitiveKind::Float32, kData, offsetof(M, anisotropyRotation)),
    primitive(kMaterial, "emissiveFactor", PrimitiveKind::Vec3Float32, kData, offsetof(M, emissiveFactor)),
    enumField(kMaterial, "alphaMode", kAlphaMode, offsetof(M, alphaMode)),
    primitive(kMaterial, "alphaCutoff", PrimitiveKind::Float32, kData, offsetof(M, alphaCutoff)),
    primitive(kMaterial, "emissiveTexture", PrimitiveKind::UInt64, kAssetRef, offsetof(M, emissiveTexture)),
};

constexpr std::array kMaterialKindConstants{
    constant("UnlitTextured", MaterialKind::UnlitTextured), constant("LitTextured", MaterialKind::LitTextured),
    constant("PbrDirectLit", MaterialKind::PbrDirectLit),   constant("PbrClearcoat", MaterialKind::PbrClearcoat),
    constant("PbrSheen", MaterialKind::PbrSheen),           constant("PbrAnisotropic", MaterialKind::PbrAnisotropic),
};

constexpr std::array kAlphaModeConstants{
    constant("Opaque", MaterialAlphaMode::Opaque),
    constant("Mask", MaterialAlphaMode::Mask),
    constant("Blend", MaterialAlphaMode::Blend),
};

constexpr std::array kSamplerFilterConstants{
    constant("Nearest", MaterialSamplerFilter::Nearest),
    constant("Linear", MaterialSamplerFilter::Linear),
};

constexpr std::array kAddressModeConstants{
    constant("Repeat", MaterialSamplerAddressMode::Repeat),
    constant("ClampToEdge", MaterialSamplerAddressMode::ClampToEdge),
};

constexpr std::array kEntityRefFields{
    primitive(kEntityRef, "scene", PrimitiveKind::AssetGuid, kAssetRef, offsetof(EntityRef, scene)),
    primitive(kEntityRef, "entity", PrimitiveKind::EntityGuid, FieldFlags::Serializable | FieldFlags::EntityReference,
              offsetof(EntityRef, entity)),
};

constexpr std::array kAssetSystemSchema{
    structType(kMaterial, kMaterialFields),
    enumType(kMaterialKind, kMaterialKindConstants),
    enumType(kAlphaMode, kAlphaModeConstants),
    enumType(kSamplerFilter, kSamplerFilterConstants),
    enumType(kAddressMode, kAddressModeConstants),
    structType(kEntityRef, kEntityRefFields),
};

static_assert(schema::isWellFormed(kAssetSystemSchema));

}  // namespace

std::span<const schema::TypeDescriptor> assetSystemSchema() noexcept { return kAssetSystemSchema; }

}  // namespace atlantis::asset_system
