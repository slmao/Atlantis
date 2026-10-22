#pragma once

// Plan 0065 M2: shared fixtures for Atlantis Authoring's tests -- the Spec 0049
// conformance base scene (scene_conformance_tests.cpp's baseScene()), written
// to a temporary source file and opened by a session.

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/authoring/authoring_session.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/schema.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace authoring_test {

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;
namespace ac = atlantis::connection::authoring;
namespace wa = atlantis::world::access;
using atlantis::authoring::AuthoringSession;
using atlantis::authoring::DocumentSource;
using atlantis::authoring::HandAuthored;
using atlantis::authoring::Imported;
using atlantis::connection::AuthoringConnection;
using atlantis::connection::SubscriptionId;
using atlantis::schema::FieldId;
using atlantis::schema::TypeId;

inline const TypeId kTransform = atlantis::schema::typeId("asset_system::scene::Transform");
inline const TypeId kCamera = atlantis::schema::typeId("asset_system::scene::Camera");
inline const TypeId kRenderable = atlantis::schema::typeId("asset_system::scene::Renderable");
inline const TypeId kLight = atlantis::schema::typeId("asset_system::scene::Light");
inline const TypeId kCameraFog = atlantis::schema::typeId("asset_system::scene::CameraFog");

[[nodiscard]] inline FieldId field(std::string_view owner, std::string_view name) {
  return atlantis::schema::fieldId(std::string("asset_system::scene::") + std::string(owner), name);
}

[[nodiscard]] inline as::EntityGuid entity(std::uint32_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xe0};
  bytes[14] = static_cast<std::byte>((n >> 8) & 0xffu);
  bytes[15] = static_cast<std::byte>(n & 0xffu);
  return as::entityGuidFromBytes(bytes).value();
}

[[nodiscard]] inline as::AssetGuid asset(std::uint8_t n) {
  as::GuidBytes bytes{};
  bytes[0] = std::byte{0xa0};
  bytes[15] = std::byte{n};
  return as::assetGuidFromBytes(bytes).value();
}

[[nodiscard]] inline as::AssetGuid sceneGuid() {
  return as::parseAssetGuid("00650065-0065-4065-8065-0065006500a2").value();
}

// The conformance base scene: node 1, the active camera with fog and bloom on;
// node 2, a renderable with a material, parented to node 1; node 3, a point
// light.
inline constexpr std::size_t kCameraNode = 0;
inline constexpr std::size_t kMeshNode = 1;
inline constexpr std::size_t kLightNode = 2;

[[nodiscard]] inline scene::AuthoringScene baseScene() {
  scene::AuthoringScene s;
  scene::AuthoringNode camera;
  camera.guid = entity(1);
  camera.transform.localPosition = {0.0f, 1.0f, 5.0f};
  scene::Camera c;
  c.fovYRadians = 1.0f;
  c.nearZ = 0.5f;
  c.farZ = 64.0f;
  c.exposureCompensationEv = 1.5f;
  c.fog = {{0.5f, 0.25f, 0.75f}, 0.125f, 2.0f, 0.25f, 0.75f};
  c.bloom = {0.5f, 1.5f};
  camera.camera = c;

  scene::AuthoringNode mesh;
  mesh.guid = entity(2);
  mesh.parent = entity(1);
  mesh.transform.localPosition = {1.0f, 0.0f, 0.0f};
  mesh.renderable = scene::Renderable{asset(1), asset(2)};

  scene::AuthoringNode light;
  light.guid = entity(3);
  light.light = scene::Light{scene::LightKind::Point, {0.5f, 0.25f, 1.0f}, 2.0f, 4.0f};

  s.nodes = {camera, mesh, light};
  s.activeCamera = entity(1);
  return s;
}

[[nodiscard]] inline std::string textOf(const scene::AuthoringScene& document) {
  const auto parsed = scene::toParsedSceneSource(document);
  REQUIRE(parsed.isOk());
  return as::serializeSceneSource(parsed.value());
}

[[nodiscard]] inline std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

inline void writeText(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

[[nodiscard]] inline scene::AuthoringScene documentOf(const std::string& text) {
  const auto parsed = as::parseSceneSource(text);
  REQUIRE(parsed.isOk());
  const auto document = scene::toAuthoringScene(parsed.value());
  REQUIRE(document.isOk());
  return document.value();
}

// Per process: catch_discover_tests runs each TEST_CASE in its own process.
inline const std::string gProcessTag = std::to_string(std::random_device{}());
inline std::atomic<int> gScratchCounter{0};

struct TempDir {
  fs::path path = fs::temp_directory_path() / "atlantis_authoring_tests" /
                  (gProcessTag + "_" + std::to_string(gScratchCounter.fetch_add(1)));
  TempDir() { fs::create_directories(path); }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
};

// A session over `document` written to a temporary hand-authored source, and
// one connection; open() unless `open` is false. Members are destroyed in
// reverse order, so the connection closes before the session.
struct Fixture {
  explicit Fixture(const scene::AuthoringScene& document = baseScene(), bool open = true)
      : source((writeText(dir.path / "scene.scene.txt", textOf(document)), dir.path / "scene.scene.txt")),
        session(DocumentSource{sceneGuid(), HandAuthored{source}}),
        connection(session.connect()) {
    if (open) REQUIRE(connection->open().isOk());
  }

  [[nodiscard]] const scene::AuthoringScene& document() const {
    REQUIRE(session.document() != nullptr);
    return *session.document();
  }

  TempDir dir;
  fs::path source;
  AuthoringSession session;
  std::unique_ptr<AuthoringConnection> connection;
};

[[nodiscard]] inline ac::PropertyAddress address(std::size_t node, TypeId component, FieldId leaf) {
  return ac::PropertyAddress{entity(static_cast<std::uint32_t>(node + 1)), component, leaf};
}

// Submits one transaction and returns its refusal, if any.
[[nodiscard]] inline std::optional<ac::Error> refusalOf(AuthoringConnection& connection,
                                                        std::vector<ac::Command> commands) {
  (void)connection.drainFailures();
  (void)connection.submitTransaction(std::move(commands));
  const auto failures = connection.drainFailures();
  if (failures.empty()) return std::nullopt;
  REQUIRE(failures.size() == 1);
  return failures.front().error;
}

}  // namespace authoring_test
