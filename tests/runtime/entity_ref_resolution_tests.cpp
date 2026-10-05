#include <atlantis/runtime/entity_ref_resolution.h>

#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace atlantis::runtime;
namespace as = atlantis::asset_system;
namespace fs = std::filesystem;

// Plan 0047 M7 (P18, ADR-0097 D6): resolving an EntityRef through a loaded
// scene -- its SceneEntityMap and World liveness. A real cooked-and-decoded
// scene, instantiated through the real instantiateScene().

namespace {

constexpr const char* kEntityTexts[2] = {"e68122c6-1bb2-8f1f-b185-358f58780b05",
                                         "e68122c6-18b2-8f1f-b185-358f58780754"};

const std::string gProcessTag = std::to_string(std::random_device{}());
std::atomic<int> gScratchCounter{0};

[[nodiscard]] as::AssetGuid sceneGuid() { return as::parseAssetGuid("0047aaaa-0000-4000-8000-000000000001").value(); }

[[nodiscard]] as::ValidatedSceneData cookAndDecodeTwoNodeScene() {
  const fs::path dir = fs::temp_directory_path() / "atlantis_entity_ref_resolution_tests" /
                       (gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary);
    out << "atlantis_scene_source_version: 7\n"
           "node_count: 2\n"
           "active_camera: none\n"
           "node: node_id=1 guid=" << kEntityTexts[0]
        << " parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n"
           "node: node_id=2 guid=" << kEntityTexts[1]
        << " parent=1 position=1.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";
  }
  REQUIRE(as::cookScene((dir / "scene.scene.txt").string(), sceneGuid(), (dir / "scene.ascene").string(),
                        (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded = as::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

[[nodiscard]] as::EntityRef refTo(const char* entityText) {
  return as::EntityRef{sceneGuid(), as::parseEntityGuid(entityText).value()};
}

}  // namespace

TEST_CASE("resolveEntityRef: a reference to a live entity of the loaded scene resolves to that entity",
          "[runtime][entity_ref]") {
  const as::ValidatedSceneData scene = cookAndDecodeTwoNodeScene();
  atlantis::world::SceneInstance instance = atlantis::world::instantiateScene(scene);
  const LoadedSceneView loaded{sceneGuid(), instance.entities, instance.world};

  for (std::size_t i = 0; i < 2; ++i) {
    INFO("node " << i);
    const auto resolved = resolveEntityRef(loaded, refTo(kEntityTexts[i]));
    REQUIRE(resolved.isOk());
    CHECK(resolved.value() == *instance.entities.find(scene.entityGuid(i)));
  }
}

TEST_CASE("resolveEntityRef: a reference naming another scene is UnknownScene", "[runtime][entity_ref]") {
  const as::ValidatedSceneData scene = cookAndDecodeTwoNodeScene();
  atlantis::world::SceneInstance instance = atlantis::world::instantiateScene(scene);
  const LoadedSceneView loaded{sceneGuid(), instance.entities, instance.world};

  as::EntityRef ref = refTo(kEntityTexts[0]);  // an entity the loaded scene does contain...
  ref.scene = as::parseAssetGuid("0047aaaa-0000-4000-8000-000000000002").value();  // ...under another scene
  const auto resolved = resolveEntityRef(loaded, ref);
  REQUIRE(resolved.isErr());
  CHECK(resolved.error() == EntityRefError::UnknownScene);
}

TEST_CASE("resolveEntityRef: a GUID the loaded scene lacks is UnknownEntity", "[runtime][entity_ref]") {
  const as::ValidatedSceneData scene = cookAndDecodeTwoNodeScene();
  atlantis::world::SceneInstance instance = atlantis::world::instantiateScene(scene);
  const LoadedSceneView loaded{sceneGuid(), instance.entities, instance.world};

  const auto resolved = resolveEntityRef(loaded, refTo("e68122c6-1bb2-8f1f-b185-358f5878ffff"));
  REQUIRE(resolved.isErr());
  CHECK(resolved.error() == EntityRefError::UnknownEntity);
}

TEST_CASE("resolveEntityRef: an entity destroyed in the World is DeadEntity, never a stale id",
          "[runtime][entity_ref]") {
  const as::ValidatedSceneData scene = cookAndDecodeTwoNodeScene();
  atlantis::world::SceneInstance instance = atlantis::world::instantiateScene(scene);
  const LoadedSceneView loaded{sceneGuid(), instance.entities, instance.world};

  const atlantis::world::EntityId victim = *instance.entities.find(scene.entityGuid(1));
  REQUIRE(instance.world.destroyEntity(victim).isOk());

  const auto dead = resolveEntityRef(loaded, refTo(kEntityTexts[1]));
  REQUIRE(dead.isErr());
  CHECK(dead.error() == EntityRefError::DeadEntity);

  // Its sibling is unaffected; the map still names the dead entity.
  CHECK(resolveEntityRef(loaded, refTo(kEntityTexts[0])).isOk());
  CHECK(instance.entities.find(scene.entityGuid(1)).has_value());
}

TEST_CASE("toString(EntityRefError) names every enumerator", "[runtime][entity_ref]") {
  CHECK(std::string(toString(EntityRefError::UnknownScene)) == "UnknownScene");
  CHECK(std::string(toString(EntityRefError::UnknownEntity)) == "UnknownEntity");
  CHECK(std::string(toString(EntityRefError::DeadEntity)) == "DeadEntity");
}
