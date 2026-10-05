#include <atlantis/asset_system/asset_system_schema.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include <atlantis/asset_system/entity_ref.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/scene_semantic_types.h>

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
static_assert(std::is_standard_layout_v<scene::Transform>);
static_assert(std::is_standard_layout_v<scene::CameraFog>);
static_assert(std::is_standard_layout_v<scene::CameraBloom>);
static_assert(std::is_standard_layout_v<scene::Camera>);
static_assert(std::is_standard_layout_v<scene::Light>);
static_assert(std::is_standard_layout_v<scene::Renderable>);

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

// Plan 0049 P2 (Spec 0049 R1, rulings Q6, J6, J7): the authoring scene's
// semantic component types. Every field is Serializable only; Renderable's
// references are AssetGuids. Vectors are std::array<float, 3>.
constexpr FieldDescriptor structField(std::string_view owner, std::string_view name, std::string_view structType,
                                      std::size_t offset) {
  return {schema::fieldId(owner, name),   name,  TypeKind::Struct, PrimitiveKind{},
          schema::typeId(structType),     kData, static_cast<std::uint32_t>(offset)};
}

constexpr std::string_view kSceneTransform = "asset_system::scene::Transform";
constexpr std::string_view kSceneCamera = "asset_system::scene::Camera";
constexpr std::string_view kSceneCameraFog = "asset_system::scene::CameraFog";
constexpr std::string_view kSceneCameraBloom = "asset_system::scene::CameraBloom";
constexpr std::string_view kSceneRenderable = "asset_system::scene::Renderable";
constexpr std::string_view kSceneLight = "asset_system::scene::Light";
constexpr std::string_view kSceneLightKind = "asset_system::scene::LightKind";

constexpr std::array kSceneTransformFields{
    primitive(kSceneTransform, "localPosition", PrimitiveKind::Vec3Float32, kData,
              offsetof(scene::Transform, localPosition)),
    primitive(kSceneTransform, "localEulerAnglesRadians", PrimitiveKind::Vec3Float32, kData,
              offsetof(scene::Transform, localEulerAnglesRadians)),
    primitive(kSceneTransform, "localScale", PrimitiveKind::Vec3Float32, kData,
              offsetof(scene::Transform, localScale)),
};

constexpr std::array kSceneCameraFields{
    primitive(kSceneCamera, "fovYRadians", PrimitiveKind::Float32, kData, offsetof(scene::Camera, fovYRadians)),
    primitive(kSceneCamera, "nearZ", PrimitiveKind::Float32, kData, offsetof(scene::Camera, nearZ)),
    primitive(kSceneCamera, "farZ", PrimitiveKind::Float32, kData, offsetof(scene::Camera, farZ)),
    primitive(kSceneCamera, "exposureCompensationEv", PrimitiveKind::Float32, kData,
              offsetof(scene::Camera, exposureCompensationEv)),
    structField(kSceneCamera, "fog", kSceneCameraFog, offsetof(scene::Camera, fog)),
    structField(kSceneCamera, "bloom", kSceneCameraBloom, offsetof(scene::Camera, bloom)),
};

constexpr std::array kSceneCameraFogFields{
    primitive(kSceneCameraFog, "color", PrimitiveKind::Vec3Float32, kData, offsetof(scene::CameraFog, color)),
    primitive(kSceneCameraFog, "density", PrimitiveKind::Float32, kData, offsetof(scene::CameraFog, density)),
    primitive(kSceneCameraFog, "height", PrimitiveKind::Float32, kData, offsetof(scene::CameraFog, height)),
    primitive(kSceneCameraFog, "heightFalloff", PrimitiveKind::Float32, kData,
              offsetof(scene::CameraFog, heightFalloff)),
    primitive(kSceneCameraFog, "maxOpacity", PrimitiveKind::Float32, kData, offsetof(scene::CameraFog, maxOpacity)),
};

constexpr std::array kSceneCameraBloomFields{
    primitive(kSceneCameraBloom, "strength", PrimitiveKind::Float32, kData, offsetof(scene::CameraBloom, strength)),
    primitive(kSceneCameraBloom, "threshold", PrimitiveKind::Float32, kData,
              offsetof(scene::CameraBloom, threshold)),
};

constexpr std::array kSceneRenderableFields{
    primitive(kSceneRenderable, "meshAsset", PrimitiveKind::AssetGuid, kAssetRef,
              offsetof(scene::Renderable, meshAsset)),
    primitive(kSceneRenderable, "materialAsset", PrimitiveKind::AssetGuid, kAssetRef | FieldFlags::Optional,
              offsetof(scene::Renderable, materialAsset)),
};

constexpr std::array kSceneLightFields{
    enumField(kSceneLight, "kind", kSceneLightKind, offsetof(scene::Light, kind)),
    primitive(kSceneLight, "color", PrimitiveKind::Vec3Float32, kData, offsetof(scene::Light, color)),
    primitive(kSceneLight, "intensity", PrimitiveKind::Float32, kData, offsetof(scene::Light, intensity)),
    primitive(kSceneLight, "range", PrimitiveKind::Float32, kData, offsetof(scene::Light, range)),
};

constexpr std::array kSceneLightKindConstants{
    constant("Directional", scene::LightKind::Directional),
    constant("Point", scene::LightKind::Point),
};

constexpr std::array kAssetSystemSchema{
    structType(kMaterial, kMaterialFields),
    enumType(kMaterialKind, kMaterialKindConstants),
    enumType(kAlphaMode, kAlphaModeConstants),
    enumType(kSamplerFilter, kSamplerFilterConstants),
    enumType(kAddressMode, kAddressModeConstants),
    structType(kEntityRef, kEntityRefFields),
    structType(kSceneTransform, kSceneTransformFields),
    structType(kSceneCamera, kSceneCameraFields),
    structType(kSceneCameraFog, kSceneCameraFogFields),
    structType(kSceneCameraBloom, kSceneCameraBloomFields),
    structType(kSceneRenderable, kSceneRenderableFields),
    structType(kSceneLight, kSceneLightFields),
    enumType(kSceneLightKind, kSceneLightKindConstants),
};

static_assert(schema::isWellFormed(kAssetSystemSchema));

}  // namespace

std::span<const schema::TypeDescriptor> assetSystemSchema() noexcept { return kAssetSystemSchema; }

}  // namespace atlantis::asset_system
