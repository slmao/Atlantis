// atlantis_gameplay_demo (Spec 0057 R8, ruling Q8; ADR-0110 D3, D4, D6;
// Plan 0057 P6): a real Gameplay SDK client. It attaches to a running
// `atlantis_runtime --listen` through Atlantis Remote's client half, holds no
// Runtime C++ object, and drives a "beacon" point light through the full
// operation loop with the typed layer -- find, spawn as one transaction,
// move and pulse it once per logic step, a refused transaction, a capture,
// destroy -- checking every stepped frame's data exactly. One line per step;
// exit 0 only if every check holds.
//
// The game logic is a plain loop in this process (ruling Q6). Its time is
// its own logic step k (ruling Q7): the Runtime is paused and stepped one
// frame per k, so the output is the same on every run whatever frame numbers
// the Runtime reports -- provided no other client resumes or steps the
// Runtime meanwhile (Spec 0057 R10).
//
//   atlantis_gameplay_demo [--session <path>] [--steps <K>] [--capture-dir <dir>]
//
// Exit codes: 0 every check passed; 1 a check failed (or a beacon from an
// aborted run is still there); 2 usage; 3 session, connection or a schema
// mismatch.

#include "remote_batch.h"

#include <atlantis/gameplay/generated/world.h>
#include <atlantis/gameplay/transaction.h>
#include <atlantis/gameplay/world.h>

#include <atlantis/connection/runtime_control.h>
#include <atlantis/remote/remote_client.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace gp = atlantis::gameplay;
namespace w = atlantis::gameplay::world;
namespace access = atlantis::world::access;
namespace connection = atlantis::connection;

// Plan 0057 J12: fixed, so the output is identical run to run.
constexpr std::string_view kBeaconGuid = "57005700-0000-4000-8000-0000000000b1";

constexpr float kPi = 3.14159265358979f;

// The beacon's orbit: radius 1.5 at height 2 around the scene's centre, in
// the default camera's view.
[[nodiscard]] std::array<float, 4> positionAt(int k, int steps) {
  const float angle = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(steps);
  return {1.5f * std::cos(angle), 2.0f, 1.5f * std::sin(angle), 1.0f};
}

[[nodiscard]] float intensityAt(int k, int steps) {
  return 4.0f + 2.0f * std::sin(2.0f * kPi * static_cast<float>(k) / static_cast<float>(steps));
}

[[nodiscard]] std::string number(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
  return buffer;
}

[[nodiscard]] std::string readBytes(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Arguments {
  std::optional<std::string> session;
  int steps = 8;
  fs::path captureDir = fs::temp_directory_path() / "atlantis_gameplay_demo";
};

[[nodiscard]] std::optional<Arguments> parse(int argc, char** argv) {
  Arguments out;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (i + 1 >= argc) return std::nullopt;
    if (arg == "--session") {
      out.session = argv[++i];
    } else if (arg == "--steps") {
      out.steps = std::atoi(argv[++i]);
      if (out.steps < 1) return std::nullopt;
    } else if (arg == "--capture-dir") {
      out.captureDir = argv[++i];
    } else {
      return std::nullopt;
    }
  }
  return out;
}

class Demo {
 public:
  Demo(atlantis::remote::RemoteSession& session, gp::QueryBatch& batch, const Arguments& arguments)
      : session_(session),
        world_(session.connection(), &batch),
        control_(session.control()),
        steps_(arguments.steps),
        captureDir_(arguments.captureDir),
        beacon_(atlantis::asset_system::parseEntityGuid(kBeaconGuid).value()) {}

  // Returns the exit code. Each step returns false once it has printed its
  // failure.
  int run() {
    bool ok = connect() && find() && baseline() && spawn();
    for (int k = 1; ok && k <= steps_; ++k) ok = move(k);
    ok = ok && refuse() && capture() && destroy();
    control_.resume();
    if (!ok) return exitCode_;
    line("9 resume", "ok");
    return 0;
  }

 private:
  static void line(std::string_view step, std::string_view text) { std::cout << step << ": " << text << std::endl; }

  bool fail(std::string_view step, const std::string& why, int code = 1) {
    std::cout << step << ": FAILED " << why << std::endl;
    exitCode_ = code;
    return false;
  }

  // One stepped frame (paused: exactly one frame of command application),
  // its report, optionally with a captured image. Remote's step blocks until
  // the Runtime completes it and has called `done`.
  std::optional<connection::FrameReport> stepOnce(std::string_view step, std::optional<fs::path> image = std::nullopt) {
    connection::StepRequest request;
    request.frames = 1;
    if (image.has_value()) request.imagePath = image->string();
    std::optional<atlantis::Result<connection::FrameReport, connection::ControlError>> result;
    control_.step(request, [&](atlantis::Result<connection::FrameReport, connection::ControlError> done) {
      result = std::move(done);
    });
    if (session_.failure().has_value()) {
      fail(step, std::string(atlantis::remote::toString(*session_.failure())), 3);
      return std::nullopt;
    }
    if (!result.has_value() || result->isErr()) {
      fail(step, result.has_value() ? "step refused: " + std::string(connection::toString(result->error()))
                                    : std::string("the step did not complete"));
      return std::nullopt;
    }
    return result->value();
  }

  [[nodiscard]] static const connection::FramePointLight* beaconIn(const connection::FrameReport& report,
                                                                   const std::array<float, 4>& position) {
    for (const auto& light : report.data.pointLights) {
      if (light.position == std::array<float, 3>{position[0], position[1], position[2]}) return &light;
    }
    return nullptr;
  }

  bool submit(std::string_view step, const gp::Transaction& tx, access::TransactionTicket& ticket) {
    auto submitted = world_.submit(tx);
    if (submitted.isErr()) return fail(step, submitted.error().describe(), 3);
    ticket = submitted.value();
    return true;
  }

  // 1. The bindings this client uses match the Runtime's schema (R5).
  bool connect() {
    for (const auto& check : {world_.checkCompatible<w::Light>(), world_.checkCompatible<w::WorldMatrix>()}) {
      if (check.isErr()) return fail("1 connect", check.error().describe(), 3);
    }
    line("1 connect", "ok (bindings Light, LightKind, WorldMatrix match the Runtime's schema)");
    return true;
  }

  // 2. Find the Directional light through a typed query.
  bool find() {
    if (world_.exists(beacon_)) {
      return fail("2 find", "a beacon from an earlier run is still there: " + std::string(kBeaconGuid));
    }
    auto lights = world_.entitiesWith<w::Light, w::WorldMatrix>();
    if (lights.isErr()) return fail("2 find", lights.error().describe());
    int directional = 0;
    float sunIntensity = 0.0f;
    for (const auto& entity : lights.value()) {
      auto light = world_.read<w::Light>(entity);
      if (light.isErr()) return fail("2 find", light.error().describe());
      if (light.value().kind == w::LightKind::Directional) {
        ++directional;
        sunIntensity = light.value().intensity;
      }
    }
    if (directional != 1) {
      return fail("2 find", "expected exactly one Directional light, found " + std::to_string(directional));
    }
    line("2 find", "ok (" + std::to_string(lights.value().size()) +
                       " light(s); the Directional light's intensity is " + number(sunIntensity) + ")");
    return true;
  }

  // 3. Pause; the baseline frame and image.
  bool baseline() {
    control_.pause();
    baselineImage_ = captureDir_ / "atlantis_gameplay_demo_baseline.png";
    auto report = stepOnce("3 baseline", baselineImage_);
    if (!report.has_value()) return false;
    if (!report->image.has_value() || !fs::exists(baselineImage_)) return fail("3 baseline", "no baseline image");
    baseline_ = *report;
    line("3 baseline", "ok (paused; " + std::to_string(baseline_.data.pointLights.size()) + " point light(s))");
    return true;
  }

  // 4. Spawn the beacon: one typed transaction, the Light (Point) before the
  //    WorldMatrix (Plan 0052 J1).
  bool spawn() {
    events_ = std::make_unique<gp::Subscription>(
        world_.subscribe(connection::EventFilter{connection::EventKindSet::all(), beacon_, std::nullopt}));
    const std::array<float, 4> at = positionAt(0, steps_);
    gp::Transaction tx;
    tx.create(beacon_)
        .add(beacon_, w::Light{w::LightKind::Point, {1.0f, 0.6f, 0.2f}, intensityAt(0, steps_), 4.0f})
        .add(beacon_, w::WorldMatrix{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, at});
    access::TransactionTicket ticket;
    if (!submit("4 spawn", tx, ticket)) return false;
    const auto report = stepOnce("4 spawn");
    if (!report.has_value()) return false;
    failures_.absorb(world_.drainFailures());
    if (const auto refusal = failures_.refusal(ticket)) {
      return fail("4 spawn", "refused: " + std::string(access::toString(refusal->error)));
    }
    if (report->data.pointLights.size() != baseline_.data.pointLights.size() + 1) {
      return fail("4 spawn", "expected one more point light in the frame");
    }
    const auto* light = beaconIn(*report, at);
    if (light == nullptr || light->color != std::array<float, 3>{1.0f, 0.6f, 0.2f} ||
        light->intensity != intensityAt(0, steps_) || light->range != 4.0f) {
      return fail("4 spawn", "the frame's beacon is not exactly what was written");
    }
    auto events = events_->drain();
    if (events.isErr() || events.value().size() != ticket.count ||
        events.value().front() != access::Event{access::EntityCreated{beacon_}}) {
      return fail("4 spawn", "expected one event per command, EntityCreated first");
    }
    line("4 spawn", "ok (" + std::to_string(ticket.count) +
                        " commands in one transaction; the next frame has the beacon at " + number(at[0]) + " " +
                        number(at[1]) + " " + number(at[2]) + ")");
    return true;
  }

  // 5. One logic step: move and pulse, one transaction, one frame, exact.
  bool move(int k) {
    const std::string step = "5." + std::to_string(k) + " move";
    const std::array<float, 4> at = positionAt(k, steps_);
    const float intensity = intensityAt(k, steps_);
    gp::Transaction tx;
    tx.set(beacon_, w::fields::WorldMatrix.column3, at).set(beacon_, w::fields::Light.intensity, intensity);
    access::TransactionTicket ticket;
    if (!submit(step, tx, ticket)) return false;
    const auto report = stepOnce(step);
    if (!report.has_value()) return false;
    const auto* light = beaconIn(*report, at);
    if (light == nullptr || light->intensity != intensity) {
      return fail(step, "the stepped frame does not show the values written for this step");
    }
    auto events = events_->drain();
    if (events.isErr() || events.value().size() != 2) return fail(step, "expected two PropertyChanged events");
    const auto decoded = world_.decode(events.value()[1], w::fields::Light.intensity);
    if (decoded.isErr() || decoded.value() != std::optional<float>{intensity}) {
      return fail(step, "the typed event decode does not match");
    }
    line(step, "ok (position " + number(at[0]) + " " + number(at[1]) + " " + number(at[2]) + ", intensity " +
                   number(intensity) + ")");
    return true;
  }

  // 6. A refused transaction changes nothing: one failure by ticket, and its
  //    valid half did not apply either.
  bool refuse() {
    gp::Transaction tx;
    tx.set(beacon_, w::fields::Light.intensity, 9.0f)
        .set(beacon_, w::fields::Light.color, {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f});
    access::TransactionTicket ticket;
    if (!submit("6 refuse", tx, ticket)) return false;
    const auto report = stepOnce("6 refuse");
    if (!report.has_value()) return false;
    failures_.absorb(world_.drainFailures());
    const auto refusal = failures_.refusal(ticket);
    if (!refusal.has_value() || refusal->error != access::AccessError::NonFiniteValue) {
      return fail("6 refuse", "expected one NonFiniteValue failure inside the transaction's tickets");
    }
    const auto* light = beaconIn(*report, positionAt(steps_, steps_));
    if (light == nullptr || light->intensity != intensityAt(steps_, steps_)) {
      return fail("6 refuse", "the frame changed: the transaction was not all or nothing");
    }
    auto events = events_->drain();
    if (events.isErr() || !events.value().empty()) return fail("6 refuse", "an aborted transaction emitted events");
    line("6 refuse", "ok (" + std::string(access::toString(refusal->error)) + " at command " +
                         std::to_string(refusal->ticket.value - ticket.first.value + 1) + " of " +
                         std::to_string(ticket.count) + "; nothing applied)");
    return true;
  }

  // 7. Capture: the image differs from the baseline (J7: PNG bytes).
  bool capture() {
    const fs::path image = captureDir_ / "atlantis_gameplay_demo_beacon.png";
    const auto report = stepOnce("7 capture", image);
    if (!report.has_value()) return false;
    if (!report->image.has_value() || !fs::exists(image)) return fail("7 capture", "no image");
    if (readBytes(image) == readBytes(baselineImage_)) return fail("7 capture", "the image equals the baseline");
    line("7 capture", "ok (" + std::to_string(report->image->width) + "x" + std::to_string(report->image->height) +
                          "; differs from the baseline)");
    return true;
  }

  // 8. Destroy: the frame returns to the original lights.
  bool destroy() {
    gp::Transaction tx;
    tx.destroy(beacon_);
    access::TransactionTicket ticket;
    if (!submit("8 destroy", tx, ticket)) return false;
    const auto report = stepOnce("8 destroy");
    if (!report.has_value()) return false;
    if (report->data.pointLights != baseline_.data.pointLights ||
        report->data.directionalLights != baseline_.data.directionalLights) {
      return fail("8 destroy", "the frame's lights are not the original ones");
    }
    auto events = events_->drain();
    if (events.isErr() || events.value().empty() ||
        events.value().back() != access::Event{access::EntityDestroyed{beacon_}}) {
      return fail("8 destroy", "expected EntityDestroyed");
    }
    if (world_.exists(beacon_)) return fail("8 destroy", "the beacon still exists");
    events_.reset();
    line("8 destroy", "ok (the frame's lights equal the baseline's)");
    return true;
  }

  atlantis::remote::RemoteSession& session_;
  gp::World world_;
  connection::RuntimeControl& control_;
  int steps_;
  fs::path captureDir_;
  atlantis::asset_system::EntityGuid beacon_;
  std::unique_ptr<gp::Subscription> events_;
  gp::FailureLog failures_;
  connection::FrameReport baseline_;
  fs::path baselineImage_;
  int exitCode_ = 1;
};

}  // namespace

int main(int argc, char** argv) {
  const auto arguments = parse(argc, argv);
  if (!arguments.has_value()) {
    std::cerr << "usage: atlantis_gameplay_demo [--session <path>] [--steps <K>] [--capture-dir <dir>]\n"
                 "  Attaches to a running `atlantis_runtime --listen` and drives a beacon light through the\n"
                 "  Gameplay SDK's operation loop. Assumes no other client resumes or steps the Runtime meanwhile.\n";
    return 2;
  }
  char* environment = nullptr;
  std::size_t length = 0;
  std::optional<std::string> fromEnvironment;
  if (_dupenv_s(&environment, &length, "ATLANTIS_SESSION") == 0 && environment != nullptr) {
    fromEnvironment = environment;
    std::free(environment);
  }
  const fs::path sessionPath = atlantis::remote::resolveSessionPath(
      arguments->session, fromEnvironment.has_value() ? fromEnvironment->c_str() : nullptr);
  const auto info = atlantis::remote::readSessionFile(sessionPath);
  if (info.isErr()) {
    std::cout << "1 connect: FAILED no attachable Runtime (" << sessionPath.string() << ")" << std::endl;
    return 3;
  }
  auto session = atlantis::remote::connectRemote(info.value());
  if (session.isErr()) {
    std::cout << "1 connect: FAILED " << atlantis::remote::toString(session.error()) << std::endl;
    return 3;
  }
  std::error_code ec;
  fs::create_directories(arguments->captureDir, ec);
  atlantis::examples::gameplay_demo::RemoteBatch batch(*session.value());
  Demo demo(*session.value(), batch, *arguments);
  return demo.run();
}
