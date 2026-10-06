#include "access/property_access.h"

#include <catch2/catch_test_macros.hpp>

#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/world_schema.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>

// Plan 0052 M1 (Spec 0052 R7, ruling Q2; P3): the accessor layer over
// worldSchema()'s descriptors -- every World leaf field read and written
// through it, the enum convention, the one Optional accessor, and coverage
// checks that make a new Enum or Optional field fail until it is handled.

namespace {

namespace access = atlantis::world::access;
namespace detail = atlantis::world::access::detail;
namespace ecs = atlantis::world::ecs;
using atlantis::schema::FieldDescriptor;
using atlantis::schema::FieldFlags;
using atlantis::schema::FieldId;
using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeId;
using atlantis::schema::TypeKind;

[[nodiscard]] const TypeDescriptor& requireType(TypeId id) {
  for (const TypeDescriptor& type : atlantis::world::worldSchema()) {
    if (type.id == id) return type;
  }
  FAIL("type not in worldSchema()");
  return atlantis::world::worldSchema()[0];
}

struct Leaf {
  TypeId component;
  const FieldDescriptor* field = nullptr;
};

// The leaves of a component, walked from the descriptors independently of
// the accessor under test.
void collectLeaves(TypeId component, const TypeDescriptor& type, std::vector<Leaf>& out) {
  for (const FieldDescriptor& field : type.fields) {
    if (field.kind == TypeKind::Struct) {
      collectLeaves(component, requireType(field.type), out);
    } else {
      out.push_back({component, &field});
    }
  }
}

[[nodiscard]] std::vector<Leaf> allLeaves() {
  std::vector<Leaf> leaves;
  std::apply(
      [&](auto... tag) {
        (collectLeaves(ecs::componentTypeId<decltype(tag)>(), requireType(ecs::componentTypeId<decltype(tag)>()),
                       leaves),
         ...);
      },
      ecs::WorldComponentTypes{});
  return leaves;
}

// A value of the leaf's kind, distinct from every component default.
[[nodiscard]] access::PropertyValue distinctValue(const FieldDescriptor& field, std::size_t salt) {
  const auto f = static_cast<float>(salt);
  if (field.kind == TypeKind::Enum) return access::EnumValue{1};  // LightKind::Point; the default is 0
  switch (field.primitive) {
    case PrimitiveKind::UInt64: return std::uint64_t{0x5200 + salt};
    case PrimitiveKind::Float32: return 7.25f + f;
    case PrimitiveKind::Vec3Float32: return std::array<float, 3>{f + 0.25f, f + 0.5f, f + 0.75f};
    case PrimitiveKind::Vec4Float32: return std::array<float, 4>{f + 0.125f, f + 0.375f, f + 0.625f, f + 0.875f};
    case PrimitiveKind::AssetGuid: {
      atlantis::asset_system::AssetGuid g;
      g.bytes[0] = std::byte{0x52};
      return g;
    }
    case PrimitiveKind::EntityGuid: {
      atlantis::asset_system::EntityGuid g;
      g.bytes[0] = std::byte{0x52};
      return g;
    }
  }
  FAIL("unhandled PrimitiveKind");
  return access::Absent{};
}

template <typename T>
void roundTripEveryLeafOf() {
  const TypeId component = ecs::componentTypeId<T>();
  std::vector<Leaf> leaves;
  collectLeaves(component, requireType(component), leaves);
  for (std::size_t i = 0; i < leaves.size(); ++i) {
    const FieldDescriptor& field = *leaves[i].field;
    INFO(std::string{requireType(component).name} << " leaf " << field.name);
    T value{};
    auto* bytes = reinterpret_cast<std::byte*>(&value);
    const auto resolved = detail::resolveField(component, field.id);
    REQUIRE(resolved.isOk());

    std::vector<access::PropertyValue> before;
    for (const Leaf& leaf : leaves) {
      before.push_back(detail::readField(bytes, component, leaf.field->id,
                                         detail::resolveField(component, leaf.field->id).value()));
    }
    const access::PropertyValue written = distinctValue(field, i);
    REQUIRE(detail::checkValue(resolved.value(), written).isOk());
    detail::writeField(bytes, component, field.id, resolved.value(), written);
    CHECK(detail::readField(bytes, component, field.id, resolved.value()) == written);
    CHECK_FALSE(before[i] == written);  // a real change
    for (std::size_t j = 0; j < leaves.size(); ++j) {
      if (j == i) continue;
      INFO("other leaf " << leaves[j].field->name);
      CHECK(detail::readField(bytes, component, leaves[j].field->id,
                              detail::resolveField(component, leaves[j].field->id).value()) == before[j]);
    }
  }
}

}  // namespace

TEST_CASE("world access: the World components have exactly 24 leaf fields", "[world][access]") {
  // Transform 3, Camera 4 + fog 5 + bloom 2, Light 4, Renderable 2, WorldMatrix 4.
  CHECK(allLeaves().size() == 24);
}

TEST_CASE("world access: every leaf of every World component round-trips through the accessor, leaving the "
          "component's other leaves unchanged",
          "[world][access]") {
  roundTripEveryLeafOf<atlantis::world::Transform>();
  roundTripEveryLeafOf<atlantis::world::Camera>();
  roundTripEveryLeafOf<atlantis::world::Light>();
  roundTripEveryLeafOf<atlantis::world::Renderable>();
  roundTripEveryLeafOf<atlantis::world::WorldMatrix>();
  static_assert(std::tuple_size_v<ecs::WorldComponentTypes> == 5, "a new component joins the matrix above");
}

TEST_CASE("world access: the accessor reads the real members (offsets agree with the C++ types)", "[world][access]") {
  atlantis::world::Camera camera{};
  camera.nearZ = 0.25f;
  camera.fog.density = 0.125f;
  camera.bloom.threshold = 3.5f;
  const auto* bytes = reinterpret_cast<const std::byte*>(&camera);
  const TypeId cameraType = ecs::componentTypeId<atlantis::world::Camera>();
  const auto read = [&](std::string_view owner, std::string_view name) {
    const FieldId id = atlantis::schema::fieldId(owner, name);
    return detail::readField(bytes, cameraType, id, detail::resolveField(cameraType, id).value());
  };
  CHECK(read("world::Camera", "nearZ") == access::PropertyValue{0.25f});
  CHECK(read("world::CameraFog", "density") == access::PropertyValue{0.125f});  // nested, by leaf id (J8)
  CHECK(read("world::CameraBloom", "threshold") == access::PropertyValue{3.5f});

  atlantis::world::Light light{};
  light.kind = atlantis::world::LightKind::Point;
  const TypeId lightType = ecs::componentTypeId<atlantis::world::Light>();
  const FieldId kind = atlantis::schema::fieldId("world::Light", "kind");
  CHECK(detail::readField(reinterpret_cast<const std::byte*>(&light), lightType, kind,
                          detail::resolveField(lightType, kind).value()) == access::PropertyValue{access::EnumValue{1}});
}

TEST_CASE("world access: the Optional accessor maps Absent to nullopt and a value to the AssetId", "[world][access]") {
  atlantis::world::Renderable renderable{};
  auto* bytes = reinterpret_cast<std::byte*>(&renderable);
  const TypeId type = ecs::componentTypeId<atlantis::world::Renderable>();
  const FieldId field = atlantis::schema::fieldId("world::Renderable", "materialAsset");
  const auto resolved = detail::resolveField(type, field);
  REQUIRE(resolved.isOk());
  CHECK(detail::readField(bytes, type, field, resolved.value()) == access::PropertyValue{access::Absent{}});
  detail::writeField(bytes, type, field, resolved.value(), std::uint64_t{0xabcd});
  REQUIRE(renderable.materialAsset.has_value());
  CHECK(*renderable.materialAsset == 0xabcd);
  detail::writeField(bytes, type, field, resolved.value(), access::Absent{});
  CHECK_FALSE(renderable.materialAsset.has_value());
}

TEST_CASE("world access: resolution refuses non-components, struct-valued fields and foreign fields",
          "[world][access]") {
  const TypeId camera = ecs::componentTypeId<atlantis::world::Camera>();
  CHECK(detail::resolveField(atlantis::schema::typeId("world::CameraFog"),
                             atlantis::schema::fieldId("world::CameraFog", "density"))
            .error() == access::AccessError::UnknownComponentType);
  CHECK(detail::resolveField(camera, atlantis::schema::fieldId("world::Camera", "fog")).error() ==
        access::AccessError::UnknownField);
  CHECK(detail::resolveField(ecs::componentTypeId<atlantis::world::Light>(),
                             atlantis::schema::fieldId("world::Camera", "nearZ"))
            .error() == access::AccessError::UnknownField);
}

TEST_CASE("world access: value checks -- kind, Absent, enum range, finiteness", "[world][access]") {
  const TypeId light = ecs::componentTypeId<atlantis::world::Light>();
  const auto intensity = detail::resolveField(light, atlantis::schema::fieldId("world::Light", "intensity")).value();
  const auto kind = detail::resolveField(light, atlantis::schema::fieldId("world::Light", "kind")).value();
  const auto color = detail::resolveField(light, atlantis::schema::fieldId("world::Light", "color")).value();
  CHECK(detail::checkValue(intensity, std::uint64_t{1}).error() == access::AccessError::KindMismatch);
  CHECK(detail::checkValue(intensity, access::Absent{}).error() == access::AccessError::KindMismatch);
  CHECK(detail::checkValue(intensity, std::numeric_limits<float>::quiet_NaN()).error() ==
        access::AccessError::NonFiniteValue);
  CHECK(detail::checkValue(color, std::array<float, 3>{0.0f, std::numeric_limits<float>::infinity(), 0.0f})
            .error() == access::AccessError::NonFiniteValue);
  CHECK(detail::checkValue(kind, access::EnumValue{2}).error() == access::AccessError::EnumValueOutOfRange);
  CHECK(detail::checkValue(kind, access::EnumValue{0}).isOk());
  CHECK(detail::checkValue(intensity, 2.0f).isOk());
}

TEST_CASE("world access coverage: every Enum field's type has the int32 storage convention", "[world][access]") {
  for (const TypeDescriptor& type : atlantis::world::worldSchema()) {
    for (const FieldDescriptor& field : type.fields) {
      if (field.kind != TypeKind::Enum) continue;
      INFO(std::string{type.name} << "." << field.name);
      bool listed = false;
      for (const TypeId id : detail::int32Enums()) listed = listed || id == field.type;
      CHECK(listed);  // a new enum must be added to kInt32Enums with its static_assert
    }
  }
  for (const TypeId id : detail::int32Enums()) CHECK(requireType(id).kind == TypeKind::Enum);
}

TEST_CASE("world access coverage: every Optional field has a typed accessor whose offset is its descriptor's",
          "[world][access]") {
  std::size_t optionalLeaves = 0;
  for (const Leaf& leaf : allLeaves()) {
    if (!atlantis::schema::hasFlags(leaf.field->flags, FieldFlags::Optional)) continue;
    ++optionalLeaves;
    INFO(leaf.field->name);
    bool found = false;
    for (const auto& entry : detail::optionalFieldAccessors()) {
      if (entry.component == leaf.component && entry.field == leaf.field->id) {
        found = true;
        CHECK(entry.byteOffset == detail::resolveField(leaf.component, leaf.field->id).value().byteOffset);
      }
    }
    CHECK(found);  // a new Optional field must be added to kOptionalFields
  }
  CHECK(optionalLeaves == detail::optionalFieldAccessors().size());
}
