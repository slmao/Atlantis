#include "document_access.h"

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_system_schema.h>
#include <atlantis/asset_system/scene_semantic_schema.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <type_traits>
#include <vector>

namespace atlantis::authoring::detail {

namespace {

namespace scene = atlantis::asset_system::scene;
using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;
using schema::FieldDescriptor;
using schema::FieldFlags;
using schema::PrimitiveKind;
using schema::TypeDescriptor;
using schema::TypeKind;

// The enum convention World's access holds (Plan 0052 P3): a described enum
// is stored as std::int32_t, on MSVC and the Android NDK's Clang alike.
static_assert(std::is_same_v<std::underlying_type_t<scene::LightKind>, std::int32_t>,
              "Plan 0065 P2: scene::LightKind is stored as std::int32_t");
static_assert(sizeof(std::array<float, 3>) == schema::primitiveSize(PrimitiveKind::Vec3Float32));
static_assert(sizeof(AssetGuid) == schema::primitiveSize(PrimitiveKind::AssetGuid));
static_assert(sizeof(EntityGuid) == schema::primitiveSize(PrimitiveKind::EntityGuid));

const schema::TypeId kTransform = schema::typeId("asset_system::scene::Transform");
const schema::TypeId kCamera = schema::typeId("asset_system::scene::Camera");
const schema::TypeId kRenderable = schema::typeId("asset_system::scene::Renderable");
const schema::TypeId kLight = schema::typeId("asset_system::scene::Light");
const schema::FieldId kMaterialAsset = schema::fieldId("asset_system::scene::Renderable", "materialAsset");

[[nodiscard]] std::vector<schema::TypeId> collectNodeComponents() {
  std::vector<schema::TypeId> out;
  for (const scene::ComponentRule& rule : scene::sceneSchema().components) out.push_back(rule.component);
  return out;
}

const std::vector<schema::TypeId>& nodeComponents() {
  static const std::vector<schema::TypeId> components = collectNodeComponents();
  return components;
}

void findLeaf(const TypeDescriptor& type, schema::FieldId field, std::uint32_t base, int& matches,
              ResolvedField& out) {
  for (const FieldDescriptor& candidate : type.fields) {
    if (candidate.kind == TypeKind::Struct) {
      const TypeDescriptor* nested = findSceneType(candidate.type);
      ATLANTIS_CHECK_MSG(nested != nullptr, "resolveField(): a Struct field's type is not in assetSystemSchema()");
      findLeaf(*nested, field, base + candidate.byteOffset, matches, out);
    } else if (candidate.id == field) {
      ++matches;
      out = ResolvedField{candidate.kind, candidate.primitive, candidate.type, candidate.flags,
                          base + candidate.byteOffset};
    }
  }
}

[[nodiscard]] std::size_t expectedAlternative(const ResolvedField& field) {
  if (field.kind == TypeKind::Enum) return 6;  // EnumValue
  switch (field.primitive) {
    case PrimitiveKind::UInt64: return 0;
    case PrimitiveKind::Float32: return 1;
    case PrimitiveKind::Vec3Float32: return 2;
    case PrimitiveKind::Vec4Float32: return 3;
    case PrimitiveKind::AssetGuid: return 4;
    case PrimitiveKind::EntityGuid: return 5;
  }
  ATLANTIS_CHECK_MSG(false, "expectedAlternative(): unhandled PrimitiveKind");
  return 0;
}

}  // namespace

std::span<const schema::TypeId> nodeComponentTypes() noexcept { return nodeComponents(); }

bool isNodeComponent(schema::TypeId type) noexcept {
  const auto& components = nodeComponents();
  return std::find(components.begin(), components.end(), type) != components.end();
}

bool isRequiredComponent(schema::TypeId type) noexcept {
  for (const scene::ComponentRule& rule : scene::sceneSchema().components) {
    if (rule.component == type) return rule.cardinality == scene::Cardinality::Required;
  }
  return false;
}

std::span<const schema::TypeDescriptor> authoringTypes() {
  static const std::vector<TypeDescriptor> types = [] {
    std::set<std::uint64_t> reached;
    std::vector<schema::TypeId> pending = nodeComponents();
    while (!pending.empty()) {
      const schema::TypeId id = pending.back();
      pending.pop_back();
      if (!reached.insert(id.value).second) continue;
      const TypeDescriptor* type = findSceneType(id);
      ATLANTIS_CHECK_MSG(type != nullptr, "authoringTypes(): a scene type is not in assetSystemSchema()");
      for (const FieldDescriptor& field : type->fields) {
        if (field.kind != TypeKind::Primitive) pending.push_back(field.type);
      }
    }
    std::vector<TypeDescriptor> out;
    for (const TypeDescriptor& type : asset_system::assetSystemSchema()) {
      if (reached.contains(type.id.value)) out.push_back(type);
    }
    return out;
  }();
  return types;
}

const schema::TypeDescriptor* findSceneType(schema::TypeId id) noexcept {
  for (const TypeDescriptor& type : asset_system::assetSystemSchema()) {
    if (type.id == id) return &type;
  }
  return nullptr;
}

atlantis::Result<ResolvedField, ac::Error> resolveField(schema::TypeId component, schema::FieldId field) {
  using ResultT = atlantis::Result<ResolvedField, ac::Error>;
  if (!isNodeComponent(component)) return ResultT::Err(ac::Error::UnknownComponentType);
  const TypeDescriptor* type = findSceneType(component);
  ATLANTIS_CHECK_MSG(type != nullptr, "resolveField(): a node component is not described in assetSystemSchema()");
  int matches = 0;
  ResolvedField resolved;
  findLeaf(*type, field, 0, matches, resolved);
  if (matches != 1) return ResultT::Err(ac::Error::UnknownField);
  return ResultT::Ok(resolved);
}

const std::byte* componentBytes(const scene::AuthoringNode& node, schema::TypeId component) noexcept {
  if (component == kTransform) return reinterpret_cast<const std::byte*>(&node.transform);
  if (component == kCamera) return node.camera ? reinterpret_cast<const std::byte*>(&*node.camera) : nullptr;
  if (component == kRenderable) {
    return node.renderable ? reinterpret_cast<const std::byte*>(&*node.renderable) : nullptr;
  }
  if (component == kLight) return node.light ? reinterpret_cast<const std::byte*>(&*node.light) : nullptr;
  return nullptr;
}

std::byte* componentBytes(scene::AuthoringNode& node, schema::TypeId component) noexcept {
  return const_cast<std::byte*>(componentBytes(static_cast<const scene::AuthoringNode&>(node), component));
}

bool hasComponent(const scene::AuthoringNode& node, schema::TypeId component) noexcept {
  return componentBytes(node, component) != nullptr;
}

bool addComponent(scene::AuthoringNode& node, schema::TypeId component) {
  if (hasComponent(node, component)) return false;
  if (component == kCamera) {
    node.camera = scene::Camera{};
  } else if (component == kRenderable) {
    node.renderable = scene::Renderable{};
  } else if (component == kLight) {
    node.light = scene::Light{};
  } else {
    ATLANTIS_CHECK_MSG(false, "addComponent(): not an optional node component");
  }
  return true;
}

bool removeComponent(scene::AuthoringNode& node, schema::TypeId component) {
  if (!hasComponent(node, component)) return false;
  if (component == kCamera) {
    node.camera.reset();
  } else if (component == kRenderable) {
    node.renderable.reset();
  } else if (component == kLight) {
    node.light.reset();
  } else {
    ATLANTIS_CHECK_MSG(false, "removeComponent(): not an optional node component");
  }
  return true;
}

std::vector<schema::TypeId> componentsOf(const scene::AuthoringNode& node) {
  std::vector<schema::TypeId> out;
  for (const schema::TypeId type : nodeComponents()) {
    if (hasComponent(node, type)) out.push_back(type);
  }
  std::sort(out.begin(), out.end());
  return out;
}

atlantis::Result<std::monostate, ac::Error> checkKind(const ResolvedField& field, const ac::PropertyValue& value) {
  using ResultT = atlantis::Result<std::monostate, ac::Error>;
  if (std::holds_alternative<atlantis::world::access::Absent>(value)) {
    return schema::hasFlags(field.flags, FieldFlags::Optional) ? ResultT::Ok({}) : ResultT::Err(ac::Error::KindMismatch);
  }
  if (value.index() != expectedAlternative(field)) return ResultT::Err(ac::Error::KindMismatch);
  if (const auto* enumValue = std::get_if<atlantis::world::access::EnumValue>(&value)) {
    const TypeDescriptor* enumType = findSceneType(field.enumType);
    ATLANTIS_CHECK_MSG(enumType != nullptr, "checkKind(): an Enum field's type is not in assetSystemSchema()");
    const bool declared = std::any_of(enumType->constants.begin(), enumType->constants.end(),
                                      [&](const auto& constant) { return constant.value == enumValue->value; });
    if (!declared) return ResultT::Err(ac::Error::EnumValueOutOfRange);
  }
  return ResultT::Ok({});
}

ac::PropertyValue readField(const std::byte* component, schema::TypeId type, schema::FieldId field,
                            const ResolvedField& resolved) {
  if (type == kRenderable && field == kMaterialAsset) {  // the one Optional field
    const auto& renderable = *reinterpret_cast<const scene::Renderable*>(component);
    if (!renderable.materialAsset.has_value()) return atlantis::world::access::Absent{};
    return *renderable.materialAsset;
  }
  ATLANTIS_CHECK_MSG(!schema::hasFlags(resolved.flags, FieldFlags::Optional),
                      "readField(): an Optional field has no typed accessor (Plan 0065 P2)");
  const std::byte* at = component + resolved.byteOffset;
  if (resolved.kind == TypeKind::Enum) {
    std::int32_t raw = 0;
    std::memcpy(&raw, at, sizeof(raw));
    return atlantis::world::access::EnumValue{raw};
  }
  switch (resolved.primitive) {
    case PrimitiveKind::UInt64: {
      std::uint64_t v = 0;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
    case PrimitiveKind::Float32: {
      float v = 0.0f;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
    case PrimitiveKind::Vec3Float32: {
      std::array<float, 3> v{};
      std::memcpy(v.data(), at, sizeof(v));
      return v;
    }
    case PrimitiveKind::Vec4Float32: {
      std::array<float, 4> v{};
      std::memcpy(v.data(), at, sizeof(v));
      return v;
    }
    case PrimitiveKind::AssetGuid: {
      AssetGuid v;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
    case PrimitiveKind::EntityGuid: {
      EntityGuid v;
      std::memcpy(&v, at, sizeof(v));
      return v;
    }
  }
  ATLANTIS_CHECK_MSG(false, "readField(): unhandled PrimitiveKind");
  return atlantis::world::access::Absent{};
}

void writeField(std::byte* component, schema::TypeId type, schema::FieldId field, const ResolvedField& resolved,
                const ac::PropertyValue& value) {
  if (type == kRenderable && field == kMaterialAsset) {
    auto& renderable = *reinterpret_cast<scene::Renderable*>(component);
    if (std::holds_alternative<atlantis::world::access::Absent>(value)) {
      renderable.materialAsset.reset();
    } else {
      renderable.materialAsset = std::get<AssetGuid>(value);
    }
    return;
  }
  ATLANTIS_CHECK_MSG(!schema::hasFlags(resolved.flags, FieldFlags::Optional),
                      "writeField(): an Optional field has no typed accessor (Plan 0065 P2)");
  std::byte* at = component + resolved.byteOffset;
  std::visit(
      [&](const auto& v) {
        using V = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<V, atlantis::world::access::EnumValue>) {
          const auto raw = static_cast<std::int32_t>(v.value);
          std::memcpy(at, &raw, sizeof(raw));
        } else if constexpr (std::is_same_v<V, atlantis::world::access::Absent>) {
          ATLANTIS_CHECK_MSG(false, "writeField(): Absent reached a non-Optional field");
        } else {
          std::memcpy(at, &v, sizeof(V));
        }
      },
      value);
}

}  // namespace atlantis::authoring::detail
