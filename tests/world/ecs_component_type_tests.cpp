#include <atlantis/world/ecs/world_components.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <tuple>
#include <type_traits>

#include <atlantis/schema.h>
#include <atlantis/world/ecs/ecs_error.h>
#include <atlantis/world/camera.h>
#include <atlantis/world/world_schema.h>

// Plan 0050 M1 (Spec 0050 R3, rulings Q3, Q7; J4): every mapped World
// component's ComponentTypeId is its schema TypeId and names a Struct
// descriptor in worldSchema() with the C++ type's size and alignment.

namespace {

namespace ecs = atlantis::world::ecs;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeKind;

template <typename T>
void expectInWorldSchema() {
  const auto id = ecs::componentTypeId<T>();
  INFO(std::string{ecs::ComponentType<T>::kName});
  CHECK(id == atlantis::schema::typeId(ecs::ComponentType<T>::kName));
  const TypeDescriptor* found = nullptr;
  for (const TypeDescriptor& type : atlantis::world::worldSchema()) {
    if (type.id == id) found = &type;
  }
  REQUIRE(found != nullptr);
  CHECK(found->kind == TypeKind::Struct);
  CHECK(found->name == ecs::ComponentType<T>::kName);
  const ecs::ComponentInfo info = ecs::componentInfo<T>();
  CHECK(info.id == id);
  CHECK(info.size == sizeof(T));
  CHECK(info.alignment == alignof(T));
}

template <typename Tuple, std::size_t... I>
void expectAll(std::index_sequence<I...>) {
  (expectInWorldSchema<std::tuple_element_t<I, Tuple>>(), ...);
}

struct Unmapped {
  float value = 0.0f;
};

}  // namespace

static_assert(ecs::Component<atlantis::world::Transform>);
static_assert(ecs::Component<atlantis::world::Camera>);
static_assert(ecs::Component<atlantis::world::Light>);
static_assert(ecs::Component<atlantis::world::Renderable>);
static_assert(!ecs::Component<atlantis::world::CameraFog>);  // a field of Camera, not a component
static_assert(!ecs::Component<Unmapped>);                    // no ComponentType specialization
static_assert(std::tuple_size_v<ecs::WorldComponentTypes> == 4);

TEST_CASE("ecs component types: every mapped World component is in worldSchema()", "[world][ecs]") {
  expectAll<ecs::WorldComponentTypes>(std::make_index_sequence<std::tuple_size_v<ecs::WorldComponentTypes>>{});
}

TEST_CASE("ecs component types: ComponentTypeIds are distinct", "[world][ecs]") {
  CHECK(ecs::componentTypeId<atlantis::world::Transform>() != ecs::componentTypeId<atlantis::world::Camera>());
  CHECK(ecs::componentTypeId<atlantis::world::Light>() != ecs::componentTypeId<atlantis::world::Renderable>());
}

TEST_CASE("ecs errors: every EcsError has a name", "[world][ecs]") {
  using atlantis::world::ecs::EcsError;
  for (const EcsError e : {EcsError::InvalidEntity, EcsError::ComponentMissing, EcsError::ComponentAlreadyPresent,
                           EcsError::StructuralChangeDuringQuery, EcsError::NilGuid,
                           EcsError::DuplicateGuid}) {
    CHECK(atlantis::world::ecs::toString(e) != "Unknown");
  }
}
