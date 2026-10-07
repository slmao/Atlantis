#pragma once

#include <atlantis/cli/invocation.h>
#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/connection/runtime_control.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Plan 0055 M6: the `atlantis` command layer and invocation driver on a
// baked fixture scene -- the Spec 0054 CLI tests' scene -- through an
// InProcess connection, with a fake RuntimeControl that runs "frames" the way
// RuntimeControlHost drives atlantis_runtime's: each frame applies pending
// commands unless paused (a step releases exactly its frames).
namespace atlantis::cli::test {

namespace fs = std::filesystem;
namespace access = atlantis::world::access;
namespace connection = atlantis::connection;

[[nodiscard]] inline atlantis::asset_system::ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  static const std::string tag = std::to_string(std::random_device{}());
  static std::atomic<int> counter{0};
  const fs::path dir = fs::temp_directory_path() / "atlantis_cli_tree_tests" /
                       ("fixture_" + tag + "_" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00520052-0052-4052-8052-005200520052").value(), "scene");
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

inline constexpr std::string_view kScene = "00550055-0055-4055-8055-005500550055";

// Runs "frames" over the boundary as RuntimeControlHost + runFrame() do.
class FakeRuntime final : public connection::RuntimeControl {
 public:
  explicit FakeRuntime(access::RuntimeWorldAccess& boundary) : boundary_(boundary) {}

  void frame() {
    const bool stepping = !steps_.empty();
    if (!paused_ || stepping) (void)boundary_.applyPending();
    ++frame_;
    if (!stepping) return;
    if (++steps_.front().ran < steps_.front().request.frames) return;
    Pending finished = std::move(steps_.front());
    steps_.pop_front();
    connection::FrameReport report;
    report.frame = frame_;
    report.applied = true;
    report.data.directionalLights.push_back({{0.0f, -1.0f, 0.0f}, {0.6f, 0.7f, 1.0f}, 1.2f});
    report.data.pointLights.push_back({{1.5f, 2.5f, 3.5f}, {1.0f, 0.6f, 0.3f}, 3.0f, 2.5f});
    report.data.view[0] = 1.0f;
    report.data.projection[5] = 2.0f;
    report.data.drawItemCount = 1;
    if (finished.request.imagePath) report.image = connection::CapturedImage{*finished.request.imagePath, 64, 32};
    finished.done(atlantis::Result<connection::FrameReport, connection::ControlError>::Ok(std::move(report)));
  }

  connection::RuntimeStatus status() override {
    return {paused_, frame_, atlantis::asset_system::parseAssetGuid(kScene).value()};
  }
  void pause() override { paused_ = true; }
  void resume() override { paused_ = false; }
  void step(connection::StepRequest request,
            std::function<void(atlantis::Result<connection::FrameReport, connection::ControlError>)> done) override {
    if (request.frames == 0) {
      done(atlantis::Result<connection::FrameReport, connection::ControlError>::Err(
          connection::ControlError::InvalidRequest));
      return;
    }
    steps_.push_back({std::move(request), std::move(done), 0});
  }
  connection::DiagnosticBatch diagnostics(std::uint64_t after, std::size_t max) override {
    connection::DiagnosticBatch batch;
    batch.latest = diagnostics_.empty() ? 0 : diagnostics_.back().sequence;
    for (const auto& d : diagnostics_) {
      if (d.sequence > after && batch.entries.size() < max) batch.entries.push_back(d);
    }
    return batch;
  }
  void warn(std::string message) {
    diagnostics_.push_back({diagnostics_.size() + 1, connection::DiagnosticSeverity::Warning, std::move(message)});
  }

 private:
  struct Pending {
    connection::StepRequest request;
    std::function<void(atlantis::Result<connection::FrameReport, connection::ControlError>)> done;
    std::uint32_t ran = 0;
  };
  access::RuntimeWorldAccess& boundary_;
  bool paused_ = false;
  std::uint64_t frame_ = 0;
  std::deque<Pending> steps_;
  std::vector<connection::Diagnostic> diagnostics_;
};

// The world, a connection to it, a fake Runtime, and an `atlantis` driver
// writing to two string streams. Not movable (the parts borrow each other).
struct Cli {
  explicit Cli(bool json = false, bool diagnosticsJson = false)
      : scene(cookAndDecodeScene(kSceneSource)),
        baked(atlantis::world::bakeScene(scene)),
        boundary(baked),
        endpoint(boundary),
        connection(endpoint.open()),
        runtime(boundary) {
    InvocationOptions options;
    options.json = json;
    options.diagnosticsJson = diagnosticsJson;
    options.awaitFrame = [this] { runtime.frame(); };
    invocation = std::make_unique<Invocation>(*connection, &runtime, nullptr, out, err, options);
  }
  Cli(const Cli&) = delete;
  Cli& operator=(const Cli&) = delete;

  // Runs one command line (whitespace-separated), returning its exit code.
  int run(std::string_view line) {
    out.str({});
    err.str({});
    std::vector<std::string> args;
    std::istringstream words{std::string(line)};
    for (std::string word; words >> word;) args.push_back(word);
    return invocation->run(args);
  }

  atlantis::asset_system::ValidatedSceneData scene;
  atlantis::world::BakedScene baked;
  access::RuntimeWorldAccess boundary;
  connection::InProcessEndpoint endpoint;
  std::unique_ptr<connection::RuntimeConnection> connection;
  FakeRuntime runtime;
  std::ostringstream out;
  std::ostringstream err;
  std::unique_ptr<Invocation> invocation;
};

}  // namespace atlantis::cli::test
