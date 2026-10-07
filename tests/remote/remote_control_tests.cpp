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
    steps_.push_back({std::move(request), std::move(done), 0});
  }
  connection::DiagnosticBatch diagnostics(std::uint64_t after, std::size_t max) override {
    connection::DiagnosticBatch batch{{}, 3, 7};
    for (std::uint64_t s = after + 1; s <= 3 && batch.entries.size() < max; ++s) {
      batch.entries.push_back({s, connection::DiagnosticSeverity::Error, "message " + std::to_string(s)});
    }
    return batch;
  }

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
