#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include <atlantis/asset_system/asset_system_schema.h>
#include <atlantis/schema.h>
#include <atlantis/world/world_schema.h>

// Plan 0049 M1 (Spec 0049 R1): each authoring-scene semantic type carries
// World's field names in World's order, with World's kinds -- so a later
// World <-> Scene mapping is name-for-name. The one declared difference is
// Renderable's references: AssetGuid (authoring identity) vs UInt64 (the
// runtime AssetId key). Lives in tests/world, which links both modules.

namespace {

using atlantis::schema::PrimitiveKind;
using atlantis::schema::TypeDescriptor;
using atlantis::schema::TypeKind;

[[nodiscard]] const TypeDescriptor& find(std::span<const TypeDescriptor> table, std::string_view name) {
  const TypeDescriptor* found = nullptr;
  for (const TypeDescriptor& type : table) {
    if (type.name == name) found = &type;
  }
  INFO(name);
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] const TypeDescriptor& findById(std::span<const TypeDescriptor> table, atlantis::schema::TypeId id) {
  const TypeDescriptor* found = nullptr;
  for (const TypeDescriptor& type : table) {
    if (type.id == id) found = &type;
  }
  REQUIRE(found != nullptr);
  return *found;
}

[[nodiscard]] std::string unqualified(std::string_view name) {
  return std::string{name.substr(name.rfind("::") + 2)};
}

void expectAligned(std::string_view worldName, std::string_view sceneName) {
  const TypeDescriptor& world = find(atlantis::world::worldSchema(), worldName);
  const TypeDescriptor& scene = find(atlantis::asset_system::assetSystemSchema(), sceneName);
  INFO(worldName << " vs " << sceneName);
  REQUIRE(world.kind == scene.kind);
  REQUIRE(world.fields.size() == scene.fields.size());
  for (std::size_t i = 0; i < world.fields.size(); ++i) {
    const auto& w = world.fields[i];
    const auto& s = scene.fields[i];
    INFO(w.name);
    CHECK(w.name == s.name);
    CHECK(w.kind == s.kind);
    if (w.kind == TypeKind::Primitive) {
      if (sceneName == "asset_system::scene::Renderable") {
        CHECK(w.primitive == PrimitiveKind::UInt64);
        CHECK(s.primitive == PrimitiveKind::AssetGuid);
      } else {
        CHECK(w.primitive == s.primitive);
      }
    } else {
      // Struct/enum fields reference the same-named type in their own module.
      CHECK(unqualified(findById(atlantis::world::worldSchema(), w.type).name) ==
            unqualified(findById(atlantis::asset_system::assetSystemSchema(), s.type).name));
    }
  }
  REQUIRE(world.constants.size() == scene.constants.size());
  for (std::size_t i = 0; i < world.constants.size(); ++i) {
    CHECK(world.constants[i].name == scene.constants[i].name);
    CHECK(world.constants[i].value == scene.constants[i].value);
  }
}

}  // namespace

TEST_CASE("scene semantic types align with World's by name, order and kind", "[world][schema][scene]") {
  expectAligned("world::Transform", "asset_system::scene::Transform");
  expectAligned("world::CameraFog", "asset_system::scene::CameraFog");
  expectAligned("world::CameraBloom", "asset_system::scene::CameraBloom");
  expectAligned("world::Camera", "asset_system::scene::Camera");
  expectAligned("world::Light", "asset_system::scene::Light");
  expectAligned("world::LightKind", "asset_system::scene::LightKind");
  expectAligned("world::Renderable", "asset_system::scene::Renderable");
}
