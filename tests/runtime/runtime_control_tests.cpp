#include <atlantis/runtime/runtime_control_host.h>

#include <atlantis/log.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Plan 0055 M3/M4 (Spec 0055 R3; P5, P6): RuntimeControlHost over a fake
// target -- status, idempotent pause/resume, steps (one at a time, in order,
// completing after exactly their frames), the hold decision each frame, stop,
// and the diagnostics ring (sequences, cursor, bound, dropped count) and its
// installation as the log sink (only when asked for: the host exists only
// under --listen).

namespace {

namespace connection = atlantis::connection;
using atlantis::runtime::DiagnosticsRing;
using atlantis::runtime::RuntimeControlHost;
using StepResult = atlantis::Result<connection::FrameReport, connection::ControlError>;

struct Fake {
  std::vector<bool> holds;  // setCommandsHeld() per frame
  int frameDataCalls = 0;
  std::vector<std::string> images;
  bool failFrameData = false;

  RuntimeControlHost::Target target() {
    RuntimeControlHost::Target t;
    t.setCommandsHeld = [this](bool held) { holds.push_back(held); };
    t.frameData = [this] {
      ++frameDataCalls;
      if (failFrameData) {
        return atlantis::Result<connection::FrameData, connection::ControlError>::Err(
            connection::ControlError::NotRendering);
      }
      connection::FrameData data;
      data.drawItemCount = static_cast<std::uint64_t>(frameDataCalls);
      return atlantis::Result<connection::FrameData, connection::ControlError>::Ok(data);
    };
    t.captureImage = [this](const std::string& path) {
      images.push_back(path);
      return atlantis::Result<connection::CapturedImage, connection::ControlError>::Ok(
          connection::CapturedImage{path, 8, 4});
    };
    t.scene = atlantis::asset_system::parseAssetGuid("00550055-0055-4055-8055-005500550055").value();
    return t;
  }
};

[[nodiscard]] RuntimeControlHost::Options noDiagnostics() {
  RuntimeControlHost::Options options;
  options.recordDiagnostics = false;
  return options;
}

void frame(RuntimeControlHost& host) {
  host.beforeFrame();
  host.afterFrame();
}

// A sink that remembers what it was given.
struct RecordingSink final : atlantis::LogSink {
  std::vector<std::string> messages;
  void write(atlantis::LogLevel, std::string_view message) override { messages.emplace_back(message); }
};

}  // namespace

TEST_CASE("control: status, and idempotent pause and resume", "[runtime][control]") {
  Fake fake;
  RuntimeControlHost host(fake.target(), noDiagnostics());
  CHECK(host.status() == connection::RuntimeStatus{false, 0, fake.target().scene});
  host.pause();
  host.pause();
  CHECK(host.status().paused);
  frame(host);
  frame(host);
  CHECK(host.status().frame == 2);
  host.resume();
  host.resume();
  CHECK_FALSE(host.status().paused);
}

TEST_CASE("control: a running step completes after exactly its frames, with that frame's report",
          "[runtime][control]") {
  Fake fake;
  RuntimeControlHost host(fake.target(), noDiagnostics());
  frame(host);  // frame 1
  std::optional<StepResult> result;
  host.step(connection::StepRequest{3, std::nullopt}, [&](StepResult r) { result = std::move(r); });
  frame(host);
  frame(host);
  CHECK_FALSE(result.has_value());
  frame(host);  // the third stepped frame
  REQUIRE(result.has_value());
  REQUIRE(result->isOk());
  CHECK(result->value().frame == 4);
  CHECK(result->value().applied);
  CHECK(result->value().data.drawItemCount == 1);  // frame data taken once, after the last frame
  CHECK_FALSE(result->value().image.has_value());
  CHECK(fake.frameDataCalls == 1);
  frame(host);
  CHECK(fake.frameDataCalls == 1);
}

TEST_CASE("control: steps run one at a time, in order; zero frames is refused at once", "[runtime][control]") {
  Fake fake;
  RuntimeControlHost host(fake.target(), noDiagnostics());
  std::vector<std::string> order;
  std::optional<StepResult> zero;
  host.step(connection::StepRequest{0, std::nullopt}, [&](StepResult r) { zero = std::move(r); });
  REQUIRE(zero.has_value());
  CHECK(zero->error() == connection::ControlError::InvalidRequest);
  host.step(connection::StepRequest{2, std::nullopt}, [&](StepResult r) {
    order.push_back("a@" + std::to_string(r.value().frame));
  });
  host.step(connection::StepRequest{1, std::string("shot.png")}, [&](StepResult r) {
    order.push_back("b@" + std::to_string(r.value().frame) + ":" + r.value().image->path);
  });
  for (int i = 0; i < 4; ++i) frame(host);
  CHECK(order == std::vector<std::string>{"a@2", "b@3:shot.png"});
  CHECK(fake.images == std::vector<std::string>{"shot.png"});
}

TEST_CASE("control: a failing capture fails the step; stop() fails what is still waiting", "[runtime][control]") {
  Fake fake;
  fake.failFrameData = true;
  std::optional<StepResult> failed;
  std::optional<StepResult> stopped;
  {
    RuntimeControlHost host(fake.target(), noDiagnostics());
    host.step(connection::StepRequest{1, std::nullopt}, [&](StepResult r) { failed = std::move(r); });
    frame(host);
    REQUIRE(failed.has_value());
    CHECK(failed->error() == connection::ControlError::NotRendering);
    host.step(connection::StepRequest{5, std::nullopt}, [&](StepResult r) { stopped = std::move(r); });
    frame(host);
  }  // destroyed mid-step
  REQUIRE(stopped.has_value());
  CHECK(stopped->error() == connection::ControlError::Stopped);
}

TEST_CASE("diagnostics ring: sequences from 1, a cursor, Warning and above only", "[runtime][control][diagnostics]") {
  DiagnosticsRing ring(8, nullptr);
  ring.write(atlantis::LogLevel::Info, "info is not kept");
  ring.write(atlantis::LogLevel::Warn, "w1");
  ring.write(atlantis::LogLevel::Error, "e2");
  ring.write(atlantis::LogLevel::Debug, "debug is not kept");
  ring.write(atlantis::LogLevel::Fatal, "f3");
  const connection::DiagnosticBatch all = ring.read(0, 100);
  CHECK(all.latest == 3);
  CHECK(all.dropped == 0);
  CHECK(all.entries == std::vector<connection::Diagnostic>{{1, connection::DiagnosticSeverity::Warning, "w1"},
                                                            {2, connection::DiagnosticSeverity::Error, "e2"},
                                                            {3, connection::DiagnosticSeverity::Fatal, "f3"}});
  CHECK(ring.read(2, 100).entries.size() == 1);
  CHECK(ring.read(3, 100).entries.empty());
  CHECK(ring.read(0, 2).entries.size() == 2);  // at most max, oldest first
  CHECK(ring.read(0, 2).entries[1].sequence == 2);
}

TEST_CASE("diagnostics ring: forwards every record unchanged", "[runtime][control][diagnostics]") {
  auto forward = std::make_shared<RecordingSink>();
  DiagnosticsRing ring(2, forward);
  ring.write(atlantis::LogLevel::Info, "i");
  ring.write(atlantis::LogLevel::Warn, "w");
  ring.write(atlantis::LogLevel::Error, "e");
  ring.write(atlantis::LogLevel::Error, "e2");
  CHECK(forward->messages == std::vector<std::string>{"i", "w", "e", "e2"});
  CHECK(ring.read(0, 10).dropped == 1);
}

TEST_CASE("diagnostics ring: bounded, counting what it drops", "[runtime][control][diagnostics]") {
  DiagnosticsRing ring(4096, nullptr);
  for (int i = 1; i <= 5000; ++i) ring.write(atlantis::LogLevel::Error, "e" + std::to_string(i));
  const connection::DiagnosticBatch batch = ring.read(0, 10000);
  CHECK(batch.entries.size() == 4096);
  CHECK(batch.dropped == 904);
  CHECK(batch.latest == 5000);
  CHECK(batch.entries.front().sequence == 905);
  CHECK(batch.entries.back().message == "e5000");
}

TEST_CASE("control: the ring becomes the log sink only when asked, and the default returns after",
          "[runtime][control][diagnostics]") {
  auto recording = std::make_shared<RecordingSink>();
  atlantis::log::initialize(recording);
  Fake fake;
  {
    RuntimeControlHost host(fake.target(), noDiagnostics());  // what no --listen amounts to: no host at all
    ATLANTIS_LOG_WARN("untouched");
    CHECK(host.diagnostics(0, 10).entries.empty());
  }
  CHECK(recording->messages.size() == 1);  // the host left the sink alone
  {
    RuntimeControlHost host(fake.target(), RuntimeControlHost::Options{});
    ATLANTIS_LOG_WARN("recorded");
    const auto batch = host.diagnostics(0, 10);
    REQUIRE(batch.entries.size() == 1);
    CHECK(batch.entries[0].message.find("recorded") != std::string::npos);
  }
  CHECK(recording->messages.size() == 1);  // the ring, not this sink, took it
  atlantis::log::initialize(nullptr);
}
