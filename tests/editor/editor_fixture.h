#pragma once

#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/editor/editor.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Plan 0056 M1/M4/M5: the editor over a baked fixture scene -- the Spec
// 0054/0055 CLI tests' scene -- through an InProcess connection, with a fake
// RuntimeControl that records its calls and runs "frames" the way
// RuntimeControlHost drives atlantis_runtime's: each frame applies pending
// commands unless paused (a step releases exactly its frames).
namespace atlantis::editor::test {

namespace fs = std::filesystem;
namespace access = atlantis::world::access;
namespace connection = atlantis::connection;

[[nodiscard]] inline atlantis::asset_system::ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  static const std::string tag = std::to_string(std::random_device{}());
  static std::atomic<int> counter{0};
  const fs::path dir = fs::temp_directory_path() / "atlantis_editor_tests" /
                       ("fixture_" + tag + "_" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00560056-0056-4056-8056-005600560056").value(), "scene");
  REQUIRE(atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), guid,
                                            (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// The Spec 0054/0055 CLI fixture scene: a mesh node, a directional light, a
// point light (parented), a camera, and an empty node.
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

inline constexpr std::string_view kScene = "00560056-0056-4056-8056-005600560056";

[[nodiscard]] inline atlantis::asset_system::EntityGuid guid(std::string_view text) {
  return atlantis::asset_system::parseEntityGuid(text).value();
}

// A RuntimeControl over the fixture's boundary: frame() applies pending
// commands unless paused, and every control call is recorded.
class FakeRuntime final : public connection::RuntimeControl {
 public:
  explicit FakeRuntime(access::RuntimeWorldAccess& boundary) : boundary_(boundary) {}

  void frame() {
    const bool stepping = stepFramesLeft_ > 0;
    if (!paused_ || stepping) (void)boundary_.applyPending();
    ++frame_;
    if (stepping) --stepFramesLeft_;
  }

  connection::RuntimeStatus status() override {
    return {paused_, frame_, atlantis::asset_system::parseAssetGuid(kScene).value()};
  }
  void pause() override {
    calls.emplace_back("pause");
    paused_ = true;
  }
  void resume() override {
    calls.emplace_back("resume");
    paused_ = false;
  }
  void step(connection::StepRequest request,
            std::function<void(atlantis::Result<connection::FrameReport, connection::ControlError>)> done) override {
    calls.push_back("step(" + std::to_string(request.frames) + ")");
    stepFramesLeft_ += request.frames;
    done(atlantis::Result<connection::FrameReport, connection::ControlError>::Ok(connection::FrameReport{}));
  }
  connection::DiagnosticBatch diagnostics(std::uint64_t, std::size_t) override { return {}; }

  std::vector<std::string> calls;

 private:
  access::RuntimeWorldAccess& boundary_;
  bool paused_ = false;
  std::uint64_t frame_ = 0;
  std::uint32_t stepFramesLeft_ = 0;
};

// The world, a boundary, an endpoint, the editor's connection, a fake Runtime
// and an Editor. Not movable (the parts borrow each other).
struct EditorFixture {
  EditorFixture()
      : scene(cookAndDecodeScene(kSceneSource)),
        baked(atlantis::world::bakeScene(scene)),
        boundary(baked),
        endpoint(boundary),
        connection(endpoint.open()),
        runtime(boundary),
        editor(std::make_unique<Editor>(*connection, runtime)) {}
  EditorFixture(const EditorFixture&) = delete;
  EditorFixture& operator=(const EditorFixture&) = delete;

  atlantis::asset_system::ValidatedSceneData scene;
  atlantis::world::BakedScene baked;
  access::RuntimeWorldAccess boundary;
  connection::InProcessEndpoint endpoint;
  std::unique_ptr<connection::RuntimeConnection> connection;
  FakeRuntime runtime;
  std::unique_ptr<Editor> editor;
};

}  // namespace atlantis::editor::test
