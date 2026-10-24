#include <atlantis/runtime/runtime_control_host.h>

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/log.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

// Plan 0055 M4 (P7, ADR-0106 D4): the hold decision each frame. Paused,
// every frame is held except the ones a step releases -- exactly its count;
// running, nothing is held; resume returns to per-frame application.
TEST_CASE("control: paused frames are held, a step releases exactly its frames, resume releases all",
          "[runtime][control][hold]") {
  Fake fake;
  RuntimeControlHost host(fake.target(), noDiagnostics());
  frame(host);  // running: applied
  host.pause();
  frame(host);
  frame(host);  // paused: held
  int completed = 0;
  host.step(connection::StepRequest{2, std::nullopt}, [&](StepResult) { ++completed; });
  frame(host);
  frame(host);  // the step's two frames: applied
  CHECK(completed == 1);
  frame(host);  // paused again: held
  host.step(connection::StepRequest{1, std::nullopt}, [&](StepResult) { ++completed; });
  frame(host);  // released once
  frame(host);  // held
  host.resume();
  frame(host);
  frame(host);  // running: applied
  CHECK(completed == 2);
  CHECK(fake.holds == std::vector<bool>{false, true, true, false, false, true, false, true, false, false});
}

// Plan 0055 P7 proof 2: setCommandsHeld() is commandsHeld_'s only writer, and
// RuntimeControlHost is setCommandsHeld()'s only caller -- so without the
// host (no --listen) nothing ever holds a frame.
TEST_CASE("control: commandsHeld_ has one writer, reached only through RuntimeControlHost",
          "[runtime][control][hold]") {
  namespace fs = std::filesystem;
  std::size_t writes = 0;
  std::vector<std::string> callers;
  for (const auto& entry : fs::recursive_directory_iterator(ATLANTIS_RUNTIME_SOURCE_DIR)) {
    const auto extension = entry.path().extension();
    if (!entry.is_regular_file() || (extension != ".h" && extension != ".cpp")) continue;
    std::ifstream in(entry.path(), std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (std::size_t at = 0; (at = source.find("commandsHeld_ =", at)) != std::string::npos; ++at) {
      if (at < 5 || source.compare(at - 5, 5, "bool ") != 0) ++writes;  // not the member's initializer
    }
    for (std::size_t at = 0; (at = source.find("setCommandsHeld(", at)) != std::string::npos; ++at) {
      callers.push_back(entry.path().filename().string());
    }
  }
  CHECK(writes == 1);  // `commandsHeld_ = held;` in setCommandsHeld(), besides `bool commandsHeld_ = false;`
  std::sort(callers.begin(), callers.end());
  callers.erase(std::unique(callers.begin(), callers.end()), callers.end());
  // The declaration (runtime_application.h) and the host (its Target hook).
  CHECK(callers == std::vector<std::string>{"runtime_application.h", "runtime_control_host.cpp"});
}

// Plan 0065 M4 (Spec 0065 third Correction; ADR-0131 Accepted Amendment
// 2026-10-23): RuntimeControlHost's hold decision over a real World access and
// the InProcess endpoint, each frame starting as runFrame() does -- pending
// commands applied unless held, then the reload swap point under the same
// condition. A command held by a pause survives a requested reload, and the
// Step that releases it applies it to the old World, settles it (its event
// reaches its subscription) and only then swaps.
TEST_CASE("control: a held command survives a requested reload and settles on the Step that performs it",
          "[runtime][control][hold][reload]") {
  namespace access = atlantis::world::access;
  auto catalog = atlantis::asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());
  const auto guid = atlantis::asset_system::parseAssetGuid(ATLANTIS_WHITELIST_INTEGRATED_SHOWCASE_SCENE_GUID);
  REQUIRE(guid.isOk());
  const auto* record = catalog.value().find(guid.value());
  REQUIRE(record != nullptr);
  const auto scene = atlantis::asset_system::decodeScene(record->artifact, record->metadata);
  REQUIRE(scene.isOk());

  std::optional<atlantis::world::BakedScene> baked;
  baked.emplace(atlantis::world::bakeScene(scene.value()));
  std::optional<access::RuntimeWorldAccess> boundary;
  boundary.emplace(*baked);
  atlantis::connection::InProcessEndpoint endpoint(*boundary);
  const auto client = endpoint.open();
  const auto events = client->subscribe(connection::EventFilter{});

  std::optional<atlantis::asset_system::EntityGuid> sun;
  for (const auto& [entityGuid, entity] : baked->entities.entries()) {
    if (baked->world.has<atlantis::world::Light>(entity).value()) sun = entityGuid;
  }
  REQUIRE(sun.has_value());
  const access::PropertyAddress intensity{*sun, atlantis::world::ecs::componentTypeId<atlantis::world::Light>(),
                                          atlantis::schema::fieldId("world::Light", "intensity")};

  bool held = false;
  Fake fake;
  RuntimeControlHost::Target target = fake.target();
  target.setCommandsHeld = [&held](bool h) { held = h; };
  RuntimeControlHost host(std::move(target), noDiagnostics());
  std::optional<atlantis::world::BakedScene> reloadRequested;
  int swaps = 0;
  std::optional<access::PropertyValue> seenBySwap;  // the old World's value when the swap happens
  const auto runFrame = [&] {
    host.beforeFrame();
    if (!held) (void)boundary->applyPending();
    if (reloadRequested.has_value() && !held) {
      seenBySwap = client->getProperty(intensity).value();
      endpoint.detach();
      boundary.reset();
      baked.reset();
      baked.emplace(std::move(*reloadRequested));
      reloadRequested.reset();
      boundary.emplace(*baked);
      endpoint.attach(*boundary);
      ++swaps;
    }
    host.afterFrame();
  };

  runFrame();
  host.pause();
  const access::CommandTicket edit = client->submit(access::SetProperty{intensity, 6.5f});
  reloadRequested.emplace(atlantis::world::bakeScene(scene.value()));
  runFrame();
  runFrame();  // paused: the command held, the reload waiting with it
  CHECK(swaps == 0);
  CHECK(client->getProperty(intensity).value() != access::PropertyValue{6.5f});
  CHECK(client->drainEvents(events).value().empty());
  CHECK(client->drainFailures().empty());

  int completed = 0;
  host.step(connection::StepRequest{1, std::nullopt}, [&](StepResult result) {
    CHECK(result.isOk());
    ++completed;
  });
  runFrame();
  CHECK(completed == 1);
  CHECK(swaps == 1);
  CHECK(seenBySwap == access::PropertyValue{6.5f});  // applied to the old World, before the swap
  CHECK(client->drainEvents(events).value() ==
        std::vector<access::Event>{access::PropertyChanged{intensity, 6.5f}});
  CHECK(client->drainFailures().empty());
  CHECK(client->getProperty(intensity).value() != access::PropertyValue{6.5f});  // the new World's own value
  CHECK(edit.value > 0);
}

// Plan 0066 P1 (Spec 0066 R2; ADR-0132 D2): the host keeps the overlay it is handed (the transport's union, which
// may exceed one client's limits) and passes it to its target; one not well formed is refused, nothing changed.
TEST_CASE("control: the Viewport overlay is kept and passed on; one not well formed is refused",
          "[runtime][control][overlay]") {
  Fake fake;
  std::vector<std::optional<connection::ViewportOverlay>> handed;
  RuntimeControlHost::Target target = fake.target();
  target.setViewportOverlay = [&handed](const std::optional<connection::ViewportOverlay>& o) { handed.push_back(o); };
  RuntimeControlHost host(std::move(target), noDiagnostics());
  CHECK_FALSE(host.viewportOverlay().has_value());

  connection::ViewportOverlay overlay;
  overlay.selection = {atlantis::asset_system::parseEntityGuid("52052052-0001-4052-8052-000000000001").value()};
  overlay.groups.resize(connection::kMaxOverlayGroups + 1);  // a union: over one client's limit, still accepted
  REQUIRE(host.setViewportOverlay(overlay).isOk());
  CHECK(host.viewportOverlay() == std::optional<connection::ViewportOverlay>{overlay});

  connection::ViewportOverlay bad = overlay;
  bad.groups[0].points.push_back({{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.5f}, connection::OverlayDepth::Tested, 4.0f});
  const auto refused = host.setViewportOverlay(bad);
  REQUIRE(refused.isErr());
  CHECK(refused.error() == connection::ControlError::InvalidRequest);
  CHECK(host.viewportOverlay() == std::optional<connection::ViewportOverlay>{overlay});

  REQUIRE(host.setViewportOverlay(std::nullopt).isOk());
  CHECK_FALSE(host.viewportOverlay().has_value());
  REQUIRE(handed.size() == 2);
  CHECK(handed[0] == std::optional<connection::ViewportOverlay>{overlay});
  CHECK_FALSE(handed[1].has_value());
}
