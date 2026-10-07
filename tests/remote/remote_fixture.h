#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/remote/remote_client.h>
#include <atlantis/remote/remote_server.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

// Plan 0055 M2: a baked fixture scene served by a real RemoteServer on a real
// loopback socket, driven single-threaded (J4): a client's whileWaiting polls
// the server, and the test applies pending commands itself, as the Spec 0054
// connection tests do.
namespace atlantis::remote::test {

namespace fs = std::filesystem;

inline const std::string& processTag() {
  static const std::string tag = std::to_string(std::random_device{}());
  return tag;
}

inline atlantis::asset_system::AssetGuid fixtureSceneGuid() {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00550055-0055-4055-8055-005500550055").value(), "scene");
}

[[nodiscard]] inline atlantis::asset_system::ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  static std::atomic<int> counter{0};
  const fs::path dir = fs::temp_directory_path() / "atlantis_remote_tests" /
                       ("fixture_" + processTag() + "_" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  REQUIRE(atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), fixtureSceneGuid(),
                                            (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// The Spec 0054 connection tests' scene: a material renderable, a
// Directional and a Point light, the active camera, and a bare node.
inline constexpr const char* kSceneSource =
    "atlantis_scene_source_version: 7\n"
    "node_count: 5\n"
    "active_camera: 4\n"
    "node: node_id=1 guid=52052052-0001-4052-8052-000000000001 parent=none position=1.0 2.0 3.0 "
    "rotation=0.1 0.2 0.3 scale=1.0 2.0 1.0 mesh=52052052-00aa-4052-8052-0000000000aa "
    "material=52052052-00bb-4052-8052-0000000000bb\n"
    "node: node_id=2 guid=52052052-0002-4052-8052-000000000002 parent=none position=0.0 5.0 0.0 "
    "rotation=0.5 -0.6 0.0 scale=1.0 1.0 1.0 light=directional color=0.6 0.7 1.0 intensity=1.2\n"
    "node: node_id=3 guid=52052052-0003-4052-8052-000000000003 parent=1 position=0.8 0.3 0.5 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 light=point color=1.0 0.6 0.3 intensity=3.0 range=2.5\n"
    "node: node_id=4 guid=52052052-0004-4052-8052-000000000004 parent=none position=0.0 2.2 7.0 "
    "rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0\n"
    "node: node_id=5 guid=52052052-0005-4052-8052-000000000005 parent=none position=0.0 0.0 0.0 "
    "rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0\n";

inline constexpr const char* kToken = "00112233445566778899aabbccddeeff";

// The world, its boundary and InProcess endpoint, and a server over them.
// Not movable: the boundary borrows the baked scene, the endpoint the
// boundary, the server the endpoint.
struct ServedWorld {
  explicit ServedWorld(const atlantis::asset_system::ValidatedSceneData& scene, RemoteServer::Limits limits = {},
                       atlantis::connection::RuntimeControl* control = nullptr)
      : baked(atlantis::world::bakeScene(scene)), boundary(baked), endpoint(boundary) {
    auto listening = RemoteServer::listen(0, kToken, fixtureSceneGuid(), [this] { return endpoint.open(); }, control,
                                          limits);
    REQUIRE(listening.isOk());
    server = std::move(listening.value());
  }
  ServedWorld(const ServedWorld&) = delete;
  ServedWorld& operator=(const ServedWorld&) = delete;

  [[nodiscard]] std::unique_ptr<RemoteSession> attach() {
    RemoteOptions options;
    options.whileWaiting = [this] { server->poll(); };
    options.responseTimeoutMilliseconds = 10000;
    auto session = connectRemote(server->session(), std::move(options));
    REQUIRE(session.isOk());
    return std::move(session.value());
  }

  atlantis::world::BakedScene baked;
  atlantis::world::access::RuntimeWorldAccess boundary;
  atlantis::connection::InProcessEndpoint endpoint;
  std::unique_ptr<RemoteServer> server;  // after the endpoint: destroyed first
};

}  // namespace atlantis::remote::test
