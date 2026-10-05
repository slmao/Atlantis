#include <atlantis/world/world_schema.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include <atlantis/world/camera.h>
#include <atlantis/world/light.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/transform.h>

namespace atlantis::world {

namespace {

using schema::EnumConstantDescriptor;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeKind;

// Byte offsets are only meaningful for standard-layout types (Spec 0048
// Non-functional). Checked here so the Android build verifies them too.
static_assert(std::is_standard_layout_v<Transform>);
static_assert(std::is_standard_layout_v<CameraFog>);
static_assert(std::is_standard_layout_v<CameraBloom>);
static_assert(std::is_standard_layout_v<Camera>);
static_assert(std::is_standard_layout_v<Light>);
static_assert(std::is_standard_layout_v<Renderable>);

// Plan 0048 P6 (ruling J3): every World component field is Serializable and
// Editable.
constexpr FieldFlags kComponent = FieldFlags::Serializable | FieldFlags::Editable;

constexpr FieldDescriptor primitive(std::string_view owner, std::string_view name, PrimitiveKind kind,
                                    FieldFlags flags, std::size_t offset) {
  return {schema::fieldId(owner, name), name,  TypeKind::Primitive, kind,
          schema::TypeId{},             flags, static_cast<std::uint32_t>(offset)};
}

constexpr FieldDescriptor reference(std::string_view owner, std::string_view name, TypeKind kind,
                                    std::string_view referenced, FieldFlags flags, std::size_t offset) {
  return {schema::fieldId(owner, name),  name,  kind, PrimitiveKind{},
          schema::typeId(referenced), flags, static_cast<std::uint32_t>(offset)};
}

constexpr TypeDescriptor structType(std::string_view name, std::span<const FieldDescriptor> fields) {
  return {schema::typeId(name), name, TypeKind::Struct, 1, fields, {}};
}

constexpr TypeDescriptor enumType(std::string_view name, std::span<const EnumConstantDescriptor> constants) {
  return {schema::typeId(name), name, TypeKind::Enum, 1, {}, constants};
}

constexpr std::string_view kTransform = "world::Transform";
constexpr std::string_view kCameraFog = "world::CameraFog";
constexpr std::string_view kCameraBloom = "world::CameraBloom";
constexpr std::string_view kCamera = "world::Camera";
constexpr std::string_view kLight = "world::Light";
constexpr std::string_view kLightKind = "world::LightKind";
constexpr std::string_view kRenderable = "world::Renderable";

constexpr std::array kTransformFields{
    primitive(kTransform, "localPosition", PrimitiveKind::Vec3Float32, kComponent,
              offsetof(Transform, localPosition)),
    primitive(kTransform, "localEulerAnglesRadians", PrimitiveKind::Vec3Float32, kComponent,
              offsetof(Transform, localEulerAnglesRadians)),
    primitive(kTransform, "localScale", PrimitiveKind::Vec3Float32, kComponent, offsetof(Transform, localScale)),
};

constexpr std::array kCameraFogFields{
    primitive(kCameraFog, "color", PrimitiveKind::Vec3Float32, kComponent, offsetof(CameraFog, color)),
    primitive(kCameraFog, "density", PrimitiveKind::Float32, kComponent, offsetof(CameraFog, density)),
    primitive(kCameraFog, "height", PrimitiveKind::Float32, kComponent, offsetof(CameraFog, height)),
    primitive(kCameraFog, "heightFalloff", PrimitiveKind::Float32, kComponent, offsetof(CameraFog, heightFalloff)),
    primitive(kCameraFog, "maxOpacity", PrimitiveKind::Float32, kComponent, offsetof(CameraFog, maxOpacity)),
};

constexpr std::array kCameraBloomFields{
    primitive(kCameraBloom, "strength", PrimitiveKind::Float32, kComponent, offsetof(CameraBloom, strength)),
    primitive(kCameraBloom, "threshold", PrimitiveKind::Float32, kComponent, offsetof(CameraBloom, threshold)),
};

constexpr std::array kCameraFields{
    primitive(kCamera, "fovYRadians", PrimitiveKind::Float32, kComponent, offsetof(Camera, fovYRadians)),
    primitive(kCamera, "nearZ", PrimitiveKind::Float32, kComponent, offsetof(Camera, nearZ)),
    primitive(kCamera, "farZ", PrimitiveKind::Float32, kComponent, offsetof(Camera, farZ)),
    primitive(kCamera, "exposureCompensationEv", PrimitiveKind::Float32, kComponent,
              offsetof(Camera, exposureCompensationEv)),
    reference(kCamera, "fog", TypeKind::Struct, kCameraFog, kComponent, offsetof(Camera, fog)),
    reference(kCamera, "bloom", TypeKind::Struct, kCameraBloom, kComponent, offsetof(Camera, bloom)),
};

constexpr std::array kLightFields{
    reference(kLight, "kind", TypeKind::Enum, kLightKind, kComponent, offsetof(Light, kind)),
    primitive(kLight, "color", PrimitiveKind::Vec3Float32, kComponent, offsetof(Light, color)),
    primitive(kLight, "intensity", PrimitiveKind::Float32, kComponent, offsetof(Light, intensity)),
    primitive(kLight, "range", PrimitiveKind::Float32, kComponent, offsetof(Light, range)),
};

constexpr std::array kLightKindConstants{
    EnumConstantDescriptor{"Directional", static_cast<std::int64_t>(LightKind::Directional)},
    EnumConstantDescriptor{"Point", static_cast<std::int64_t>(LightKind::Point)},
};

// Plan 0048 P6 (ruling J2): materialAsset is the one std::optional member.
constexpr std::array kRenderableFields{
    primitive(kRenderable, "meshAsset", PrimitiveKind::UInt64, kComponent | FieldFlags::AssetReference,
              offsetof(Renderable, meshAsset)),
    primitive(kRenderable, "materialAsset", PrimitiveKind::UInt64,
              kComponent | FieldFlags::AssetReference | FieldFlags::Optional, offsetof(Renderable, materialAsset)),
};

constexpr std::array kWorldSchema{
    structType(kTransform, kTransformFields),   structType(kCameraFog, kCameraFogFields),
    structType(kCameraBloom, kCameraBloomFields), structType(kCamera, kCameraFields),
    structType(kLight, kLightFields),           enumType(kLightKind, kLightKindConstants),
    structType(kRenderable, kRenderableFields),
};

static_assert(schema::isWellFormed(kWorldSchema));

}  // namespace

std::span<const schema::TypeDescriptor> worldSchema() noexcept { return kWorldSchema; }

}  // namespace atlantis::world
