#include "remote_fixture.h"

#include <atlantis/connection/runtime_control.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Plan 0055 M3 (Spec 0055 R3; ADR-0106 D3): RuntimeControl over the wire --
// status, pause/resume, diagnostics, and a step that the server parks until
// its frame completes it. A fake control stands in for the Runtime: each of
// the client's waits runs one "frame" (the fake's) and one server poll.

namespace {

namespace connection = atlantis::connection;
using atlantis::remote::test::cookAndDecodeScene;
using atlantis::remote::test::kSceneSource;
using atlantis::remote::test::ServedWorld;
using StepResult = atlantis::Result<connection::FrameReport, connection::ControlError>;

class FakeControl final : public connection::RuntimeControl {
 public:
  connection::RuntimeStatus status() override { return {paused_, frame_, {}}; }
  void pause() override { paused_ = true; }
  void resume() override { paused_ = false; }
  void step(connection::StepRequest request, std::function<void(StepResult)> done) override {
    if (request.frames == 0) {
      done(StepResult::Err(connection::ControlError::InvalidRequest));
      return;
    }
    stepRequests.push_back(request);
    steps_.push_back({std::move(request), std::move(done), 0});
  }
  std::vector<connection::StepRequest> stepRequests;  // Plan 0066: as the server handed them over
  connection::DiagnosticBatch diagnostics(std::uint64_t after, std::size_t max) override {
    connection::DiagnosticBatch batch{{}, 3, 7};
    for (std::uint64_t s = after + 1; s <= 3 && batch.entries.size() < max; ++s) {
      batch.entries.push_back({s, connection::DiagnosticSeverity::Error, "message " + std::to_string(s)});
    }
    return batch;
  }

  // Spec 0064 / ADR-0128 (Plan 0064 M2): the camera last set, and the answer
  // raycast() gives (NoGeometry unless a test scripts a hit or a miss).
  void setViewportCamera(std::optional<connection::ViewportCamera> camera) override {
    viewportCamera = std::move(camera);
    ++viewportCameraCalls;
  }
  // Plan 0066 (Spec 0066 R2): the Viewport overlay last set (for a transport's fake, the union), and how often.
  atlantis::Result<std::monostate, connection::ControlError> setViewportOverlay(
      std::optional<connection::ViewportOverlay> overlay) override {
    viewportOverlay = std::move(overlay);
    ++viewportOverlayCalls;
    return atlantis::Result<std::monostate, connection::ControlError>::Ok(std::monostate{});
  }
  atlantis::Result<std::optional<connection::RayHit>, connection::ControlError> raycast(
      const connection::Ray& ray) override {
    lastRay = ray;
    if (!rayAnswer.has_value()) {
      return atlantis::Result<std::optional<connection::RayHit>, connection::ControlError>::Err(
          connection::ControlError::NoGeometry);
    }
    return atlantis::Result<std::optional<connection::RayHit>, connection::ControlError>::Ok(*rayAnswer);
  }
  std::optional<connection::ViewportCamera> viewportCamera;
  int viewportCameraCalls = 0;
  std::optional<connection::ViewportOverlay> viewportOverlay;
  int viewportOverlayCalls = 0;
  std::optional<connection::Ray> lastRay;
  std::optional<std::optional<connection::RayHit>> rayAnswer;

  void runFrame() {
    ++frame_;
    if (steps_.empty()) return;
    Pending& current = steps_.front();
    if (++current.ran < current.request.frames) return;
    connection::FrameReport report;
    report.frame = frame_;
    report.applied = true;
    report.data.pointLights.push_back({{-39.615f, 3.255f, -5.032f}, {1.0f, 0.8f, 0.55f}, 24.0f, 6.0f});
    report.data.view[0] = 1.0f;
    report.data.drawItemCount = 42;
    if (current.request.imagePath) report.image = connection::CapturedImage{*current.request.imagePath, 640, 480};
    auto done = std::move(current.done);
    steps_.pop_front();
    done(StepResult::Ok(std::move(report)));
  }

 private:
  struct Pending {
    connection::StepRequest request;
    std::function<void(StepResult)> done;
    std::uint32_t ran = 0;
  };
  bool paused_ = false;
  std::uint64_t frame_ = 0;
  std::deque<Pending> steps_;
};

}  // namespace

TEST_CASE("remote control: status, pause, resume and diagnostics cross the wire", "[remote][control]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  FakeControl control;
  ServedWorld world(scene, {}, &control);
  const auto session = world.attach();
  connection::RuntimeControl& remote = session->control();
  CHECK_FALSE(remote.status().paused);
  remote.pause();
  CHECK(remote.status().paused);
  remote.resume();
  CHECK_FALSE(remote.status().paused);

  const connection::DiagnosticBatch batch = remote.diagnostics(1, 10);
  CHECK(batch.latest == 3);
  CHECK(batch.dropped == 7);
  CHECK(batch.entries == std::vector<connection::Diagnostic>{{2, connection::DiagnosticSeverity::Error, "message 2"},
                                                              {3, connection::DiagnosticSeverity::Error, "message 3"}});
  CHECK_FALSE(session->failure().has_value());
}

TEST_CASE("remote control: a step is parked until its frames complete, and its report arrives whole",
          "[remote][control]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  FakeControl control;
  ServedWorld world(scene, {}, &control);
  atlantis::remote::RemoteOptions options;
  options.whileWaiting = [&] {
    control.runFrame();
    world.server->poll();
  };
  auto connected = atlantis::remote::connectRemote(world.server->session(), options);
  REQUIRE(connected.isOk());
  const auto& session = connected.value();
  const std::uint64_t before = session->control().status().frame;

  std::optional<StepResult> result;
  session->control().step(connection::StepRequest{3, std::string("out.png")}, [&](StepResult r) { result = std::move(r); });
  REQUIRE(result.has_value());  // the remote step returns once done
  REQUIRE(result->isOk());
  const connection::FrameReport& report = result->value();
  CHECK(report.frame >= before + 3);
  CHECK(report.applied);
  REQUIRE(report.data.pointLights.size() == 1);
  CHECK(report.data.pointLights[0] ==
        connection::FramePointLight{{-39.615f, 3.255f, -5.032f}, {1.0f, 0.8f, 0.55f}, 24.0f, 6.0f});
  CHECK(report.data.view[0] == 1.0f);
  CHECK(report.data.drawItemCount == 42);
  CHECK(report.image == connection::CapturedImage{"out.png", 640, 480});

  std::optional<StepResult> refused;
  session->control().step(connection::StepRequest{0, std::nullopt}, [&](StepResult r) { refused = std::move(r); });
  REQUIRE(refused.has_value());
  CHECK(refused->error() == connection::ControlError::InvalidRequest);
}

TEST_CASE("remote control: without a control, control.* is refused", "[remote][control]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  ServedWorld world(scene);
  const auto session = world.attach();
  (void)session->control().status();
  CHECK(session->failure() == atlantis::remote::RemoteError::ProtocolError);
}

// Spec 0064 / ADR-0128 D1-D3 (Plan 0064 M3; J2): the camera and the ray-cast
// cross the wire; the camera is cleared when the client that set it goes.
TEST_CASE("remote control: the viewport camera and the ray-cast cross the wire", "[remote][control][camera]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  FakeControl control;
  ServedWorld world(scene, {}, &control);
  auto session = world.attach();
  connection::RuntimeControl& remote = session->control();

  connection::ViewportCamera camera;
  for (std::size_t i = 0; i < 16; ++i) camera.worldMatrix[i] = i % 5 == 0 ? 1.0f : 0.0f;
  camera.worldMatrix[14] = 10.0f;
  camera.fovYRadians = 1.0472f;
  camera.nearZ = 0.1f;
  camera.farZ = 100.0f;
  remote.setViewportCamera(camera);
  (void)remote.status();  // a round trip: the server has handled the set
  REQUIRE(control.viewportCamera.has_value());
  CHECK(*control.viewportCamera == camera);
  remote.setViewportCamera(std::nullopt);
  (void)remote.status();
  CHECK_FALSE(control.viewportCamera.has_value());

  const connection::Ray ray{{0.0f, 1.0f, 5.0f}, {0.0f, 0.0f, -1.0f}};
  auto refused = remote.raycast(ray);  // the fake keeps no geometry until told
  REQUIRE(refused.isErr());
  CHECK(refused.error() == connection::ControlError::NoGeometry);
  CHECK(control.lastRay == ray);
  const auto entity = atlantis::asset_system::parseEntityGuid("52052052-0001-4052-8052-000000000001").value();
  control.rayAnswer = std::optional<connection::RayHit>{connection::RayHit{entity, 4.5f}};
  auto hit = remote.raycast(ray);
  REQUIRE(hit.isOk());
  REQUIRE(hit.value().has_value());
  CHECK(*hit.value() == connection::RayHit{entity, 4.5f});
  control.rayAnswer = std::optional<connection::RayHit>{};
  auto miss = remote.raycast(ray);
  REQUIRE(miss.isOk());
  CHECK_FALSE(miss.value().has_value());

  // The setter goes away: its camera goes with it.
  remote.setViewportCamera(camera);
  (void)remote.status();
  REQUIRE(control.viewportCamera.has_value());
  session.reset();
  for (int i = 0; i < 20 && control.viewportCamera.has_value(); ++i) world.server->poll();
  CHECK_FALSE(control.viewportCamera.has_value());
  CHECK(world.server->clientCount() == 0);
}

// Plan 0066 P2 (Spec 0066 R2, R5; ADR-0132 D2; J2, J3): one overlay per client, replaced by its next; the host is
// handed their union on every change and on a client's disconnect; null clears; a refused overlay changes nothing;
// a capture is handed no overlay unless withOverlay, and then only the requester's own.
TEST_CASE("remote control: per-client Viewport overlays, their union, and a capture's own overlay",
          "[remote][control][overlay]") {
  const auto scene = cookAndDecodeScene(kSceneSource);
  FakeControl control;
  ServedWorld world(scene, {}, &control);
  atlantis::remote::RemoteOptions options;
  options.whileWaiting = [&] {
    control.runFrame();
    world.server->poll();
  };
  auto a = atlantis::remote::connectRemote(world.server->session(), options);
  auto b = atlantis::remote::connectRemote(world.server->session(), options);
  REQUIRE(a.isOk());
  REQUIRE(b.isOk());
  connection::RuntimeControl& ca = a.value()->control();
  connection::RuntimeControl& cb = b.value()->control();
  const auto guid = [](std::string_view text) { return atlantis::asset_system::parseEntityGuid(text).value(); };
  const auto e1 = guid("52052052-0001-4052-8052-000000000001");
  const auto e2 = guid("52052052-0002-4052-8052-000000000002");
  const auto e3 = guid("52052052-0003-4052-8052-000000000003");

  connection::ViewportOverlay own;
  own.selection = {e1};
  connection::OverlayGroup group;
  group.segments.push_back({{0, 0, 0}, {1, 0, 0}, {1, 0.5f, 0, 1}, connection::OverlayDepth::OnTop, 2.0f});
  own.groups.push_back(group);
  REQUIRE(ca.setViewportOverlay(own).isOk());
  REQUIRE(control.viewportOverlay.has_value());
  CHECK(*control.viewportOverlay == own);

  connection::ViewportOverlay theirs;
  theirs.selection = {e2};
  REQUIRE(cb.setViewportOverlay(theirs).isOk());
  CHECK(control.viewportOverlay->selection == std::vector{e1, e2});
  CHECK(control.viewportOverlay->groups == own.groups);
  theirs.selection = {e3};
  REQUIRE(cb.setViewportOverlay(theirs).isOk());  // replaced, not added to
  CHECK(control.viewportOverlay->selection == std::vector{e1, e3});
  const int calls = control.viewportOverlayCalls;

  // Refused: over a limit, or not well formed -- nothing changes.
  connection::ViewportOverlay tooMany;
  tooMany.groups.resize(connection::kMaxOverlayGroups + 1);
  auto refused = cb.setViewportOverlay(tooMany);
  REQUIRE(refused.isErr());
  CHECK(refused.error() == connection::ControlError::InvalidRequest);
  connection::ViewportOverlay zeroWidth = own;
  zeroWidth.groups[0].segments[0].widthPixels = 0.0f;
  CHECK(cb.setViewportOverlay(zeroWidth).isErr());
  CHECK(control.viewportOverlayCalls == calls);
  CHECK(control.viewportOverlay->selection == std::vector{e1, e3});
  CHECK_FALSE(b.value()->failure().has_value());

  // Captures: none without withOverlay (even one the client fills in: it never crosses the wire), else its own.
  const auto step = [&](connection::RuntimeControl& c, connection::StepRequest request) {
    std::optional<StepResult> result;
    c.step(std::move(request), [&](StepResult r) { result = std::move(r); });
    REQUIRE(result.has_value());
    CHECK(result->isOk());
    REQUIRE_FALSE(control.stepRequests.empty());
    return control.stepRequests.back();
  };
  connection::StepRequest plain{1, std::string("b.png")};
  plain.captureOverlay = own;
  CHECK_FALSE(step(cb, plain).captureOverlay.has_value());
  connection::StepRequest withOverlay{1, std::string("b.png")};
  withOverlay.withOverlay = true;
  const connection::StepRequest handed = step(cb, withOverlay);
  CHECK(handed.withOverlay);
  CHECK(handed.captureOverlay == std::optional<connection::ViewportOverlay>{theirs});
  CHECK(step(ca, withOverlay).captureOverlay == std::optional<connection::ViewportOverlay>{own});

  // null clears this client's; the union is the other's alone. A client with none gets none for its capture.
  REQUIRE(ca.setViewportOverlay(std::nullopt).isOk());
  CHECK(control.viewportOverlay == std::optional<connection::ViewportOverlay>{theirs});
  CHECK_FALSE(step(ca, withOverlay).captureOverlay.has_value());
  const int afterClear = control.viewportOverlayCalls;
  REQUIRE(ca.setViewportOverlay(std::nullopt).isOk());  // nothing to clear: the host is not called
  CHECK(control.viewportOverlayCalls == afterClear);

  // The other client goes: its overlay goes with it.
  b.value().reset();
  for (int i = 0; i < 20 && control.viewportOverlay.has_value(); ++i) world.server->poll();
  CHECK_FALSE(control.viewportOverlay.has_value());
  CHECK(world.server->clientCount() == 1);
}
