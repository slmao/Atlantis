// Plan 0058 P4 (Spec 0058 R6, ruling Q7; J4): atlantis_csharp_fixture_server
// -- a GPU-free atlantis.remote/1 server the C# tests attach to. It serves the
// Spec 0054 fixture scene (cooked and decoded in a temp directory, baked,
// behind a boundary and an InProcess endpoint) through a real RemoteServer on
// 127.0.0.1, with a RuntimeControl that runs "frames" as atlantis_runtime's
// RuntimeControlHost does: a frame applies pending commands unless paused,
// and a step releases exactly its frames and completes after the last one.
// There is no renderer: a report's frame data is empty, and an image request
// is refused (NotRendering).
//
//   atlantis_csharp_fixture_server --session-file <path> [--record <path>] [--mutate <name>]
//
// A frame runs every 2 ms, followed by poll(), so answers arrive at frame
// boundaries as from the Runtime. --record appends one JSON line per
// submission any client makes ({"transaction":bool,"commands":[...]}, the
// sdk_transcripts.jsonl shape). --mutate serves a modified schema for the
// compatibility tests. The session file is written once listening; the
// process exits 0, removing it, when its standard input closes.
//
// Test-only; host only.

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/connection/json.h>
#include <atlantis/remote/remote_server.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_schema.h>

#include "codec.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
namespace json = atlantis::connection::json;
namespace schema = atlantis::schema;
using atlantis::asset_system::AssetGuid;
using atlantis::asset_system::EntityGuid;

// The Spec 0054/0055/0056/0057 fixture scene (tests/gameplay_sdk/gameplay_fixture.h's
// kSceneSource, whose header needs Catch2): a mesh node, a Directional light,
// a Point light (parented), the active camera, and an empty node.
constexpr const char* kSceneSource =
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

[[noreturn]] void die(const std::string& message) {
  std::cerr << "atlantis_csharp_fixture_server: " << message << "\n";
  std::exit(2);
}

[[nodiscard]] AssetGuid sceneGuid() {
  return atlantis::asset_system::deriveAssetGuid(
      atlantis::asset_system::parseAssetGuid("00580058-0058-4058-8058-005800580058").value(), "scene");
}

[[nodiscard]] atlantis::asset_system::ValidatedSceneData cookAndDecodeScene() {
  const fs::path dir = fs::temp_directory_path() / "atlantis_csharp_fixture_server" /
                       ("fixture_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << kSceneSource;
  }
  if (atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), sceneGuid(),
                                        (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
          .isErr()) {
    die("the fixture scene did not cook");
  }
  auto decoded =
      atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  if (decoded.isErr()) die("the fixture scene did not decode");
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// A mutable copy of worldSchema(), for --mutate. Field and constant arrays
// are owned here; names stay views of the static table's strings.
struct SchemaCopy {
  SchemaCopy() {
    for (const schema::TypeDescriptor& type : atlantis::world::worldSchema()) {
      fields.emplace_back(type.fields.begin(), type.fields.end());
      constants.emplace_back(type.constants.begin(), type.constants.end());
      types.push_back(type);
    }
  }
  void relink() {
    for (std::size_t i = 0; i < types.size(); ++i) {
      types[i].fields = fields[i];
      types[i].constants = constants[i];
    }
  }
  [[nodiscard]] std::size_t indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < types.size(); ++i) {
      if (types[i].name == name) return i;
    }
    die("no type " + std::string(name));
  }
  [[nodiscard]] schema::FieldDescriptor& field(std::string_view type, std::string_view name) {
    for (schema::FieldDescriptor& f : fields[indexOf(type)]) {
      if (f.name == name) return f;
    }
    die("no field " + std::string(name));
  }
  std::deque<std::vector<schema::FieldDescriptor>> fields;
  std::deque<std::vector<schema::EnumConstantDescriptor>> constants;
  std::vector<schema::TypeDescriptor> types;
};

// P4's five schema mutations.
void mutate(SchemaCopy& copy, std::string_view name) {
  if (name == "light-version") {
    copy.types[copy.indexOf("world::Light")].schemaVersion = 2;
  } else if (name == "light-kind-constant") {
    copy.constants[copy.indexOf("world::LightKind")][1].name = "Spot";
  } else if (name == "camera-fog-version") {
    copy.types[copy.indexOf("world::CameraFog")].schemaVersion = 2;
  } else if (name == "light-intensity-readonly") {
    copy.field("world::Light", "intensity").flags = schema::FieldFlags::Serializable;
  } else if (name == "light-extra-field") {
    copy.fields[copy.indexOf("world::Light")].push_back(schema::FieldDescriptor{
        schema::fieldId("world::Light", "extra"), "extra", schema::TypeKind::Primitive, schema::PrimitiveKind::Float32,
        schema::TypeId{}, schema::FieldFlags::Editable, 0});
  } else {
    die("unknown --mutate " + std::string(name));
  }
  copy.relink();
}

// Appends every submission to the record file, one line each, flushed.
class Recorder {
 public:
  explicit Recorder(const std::optional<fs::path>& path) {
    if (path) {
      out_.open(*path, std::ios::binary | std::ios::trunc);
      if (!out_) die("cannot write " + path->string());
    }
  }
  void record(bool transaction, std::span<const access::Command> commands) {
    if (!out_.is_open()) return;
    json::Value list = json::Value::array();
    for (const access::Command& command : commands) list.push(atlantis::remote::codec::encode(command));
    json::Value line = json::Value::object();
    line.set("transaction", json::Value::boolean(transaction));
    line.set("commands", std::move(list));
    out_ << json::write(line) << "\n";
    out_.flush();
  }

 private:
  std::ofstream out_;
};

// One client's connection: forwards to its own InProcess connection, records
// its submissions, and serves the (possibly mutated) schema.
class FixtureConnection final : public connection::RuntimeConnection {
 public:
  FixtureConnection(std::unique_ptr<connection::RuntimeConnection> inner, Recorder& recorder,
                    std::span<const schema::TypeDescriptor> schema)
      : inner_(std::move(inner)), recorder_(recorder), schema_(schema) {}

  bool findEntity(const EntityGuid& entity) const override { return inner_->findEntity(entity); }
  std::vector<EntityGuid> listEntities() const override { return inner_->listEntities(); }
  atlantis::Result<std::vector<schema::TypeId>, access::AccessError> listComponents(
      const EntityGuid& entity) const override {
    return inner_->listComponents(entity);
  }
  atlantis::Result<access::PropertyValue, access::AccessError> getProperty(
      const access::PropertyAddress& address) const override {
    return inner_->getProperty(address);
  }
  std::span<const schema::TypeDescriptor> schema() const override { return schema_; }
  access::CommandTicket submit(access::Command command) override {
    recorder_.record(false, std::span<const access::Command>(&command, 1));
    return inner_->submit(std::move(command));
  }
  access::TransactionTicket submitTransaction(std::vector<access::Command> commands) override {
    recorder_.record(true, commands);
    return inner_->submitTransaction(std::move(commands));
  }
  connection::SubscriptionId subscribe(connection::EventFilter filter) override {
    return inner_->subscribe(std::move(filter));
  }
  atlantis::Result<std::monostate, connection::ConnectionError> unsubscribe(
      connection::SubscriptionId subscription) override {
    return inner_->unsubscribe(subscription);
  }
  atlantis::Result<std::vector<access::Event>, connection::ConnectionError> drainEvents(
      connection::SubscriptionId subscription) override {
    return inner_->drainEvents(subscription);
  }
  std::vector<access::CommandFailure> drainFailures() override { return inner_->drainFailures(); }

 private:
  std::unique_ptr<connection::RuntimeConnection> inner_;
  Recorder& recorder_;
  std::span<const schema::TypeDescriptor> schema_;
};

// RuntimeControlHost's frame and step rules over the fixture's boundary.
class FixtureControl final : public connection::RuntimeControl {
 public:
  using Done = std::function<void(atlantis::Result<connection::FrameReport, connection::ControlError>)>;

  FixtureControl(access::RuntimeWorldAccess& boundary, AssetGuid scene) : boundary_(boundary), scene_(scene) {}

  void frame() {
    if (!steps_.empty() && !stepStarted_) {
      stepStarted_ = true;
      steps_.front().remaining = steps_.front().request.frames;
    }
    const bool stepping = stepStarted_ && steps_.front().remaining > 0;
    if (!paused_ || stepping) (void)boundary_.applyPending();
    ++frame_;
    if (!stepStarted_) return;
    Pending& current = steps_.front();
    if (--current.remaining > 0) return;
    Pending finished = std::move(current);
    steps_.pop_front();
    stepStarted_ = false;
    using ResultT = atlantis::Result<connection::FrameReport, connection::ControlError>;
    if (finished.request.imagePath.has_value()) {
      finished.done(ResultT::Err(connection::ControlError::NotRendering));
      return;
    }
    connection::FrameReport report;
    report.frame = frame_;
    report.applied = true;
    finished.done(ResultT::Ok(std::move(report)));
  }

  void stop() {
    using ResultT = atlantis::Result<connection::FrameReport, connection::ControlError>;
    std::deque<Pending> waiting = std::move(steps_);
    steps_.clear();
    for (Pending& pending : waiting) pending.done(ResultT::Err(connection::ControlError::Stopped));
  }

  connection::RuntimeStatus status() override { return connection::RuntimeStatus{paused_, frame_, scene_}; }
  void pause() override { paused_ = true; }
  void resume() override { paused_ = false; }
  void step(connection::StepRequest request, Done done) override {
    if (request.frames == 0) {
      done(atlantis::Result<connection::FrameReport, connection::ControlError>::Err(
          connection::ControlError::InvalidRequest));
      return;
    }
    steps_.push_back(Pending{std::move(request), std::move(done), 0});
  }
  connection::DiagnosticBatch diagnostics(std::uint64_t, std::size_t) override { return {}; }

 private:
  struct Pending {
    connection::StepRequest request;
    Done done;
    std::uint32_t remaining = 0;
  };

  access::RuntimeWorldAccess& boundary_;
  AssetGuid scene_;
  bool paused_ = false;
  std::uint64_t frame_ = 0;
  std::deque<Pending> steps_;
  bool stepStarted_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  std::optional<fs::path> sessionFile;
  std::optional<fs::path> recordFile;
  std::optional<std::string> mutation;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (i + 1 >= argc) die("usage: --session-file <path> [--record <path>] [--mutate <name>]");
    if (arg == "--session-file") {
      sessionFile = argv[++i];
    } else if (arg == "--record") {
      recordFile = argv[++i];
    } else if (arg == "--mutate") {
      mutation = argv[++i];
    } else {
      die("unknown argument " + std::string(arg));
    }
  }
  if (!sessionFile) die("usage: --session-file <path> [--record <path>] [--mutate <name>]");

  SchemaCopy served;
  if (mutation) mutate(served, *mutation);
  served.relink();
  const std::span<const schema::TypeDescriptor> schemaSpan(served.types);

  const atlantis::asset_system::ValidatedSceneData scene = cookAndDecodeScene();
  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  access::RuntimeWorldAccess boundary(baked);
  connection::InProcessEndpoint endpoint(boundary);
  FixtureControl control(boundary, sceneGuid());
  Recorder recorder(recordFile);

  auto listening = atlantis::remote::RemoteServer::listen(
      0, atlantis::remote::generateToken(), sceneGuid(),
      [&] { return std::make_unique<FixtureConnection>(endpoint.open(), recorder, schemaSpan); }, &control);
  if (listening.isErr()) die("cannot listen: " + std::string(atlantis::remote::toString(listening.error())));
  std::unique_ptr<atlantis::remote::RemoteServer> server = std::move(listening.value());
  if (atlantis::remote::writeSessionFile(*sessionFile, server->session()).isErr()) {
    die("cannot write " + sessionFile->string());
  }

  // Standard input closing is the signal to stop (the C# test holds it).
  std::atomic<bool> stopping{false};
  std::thread watcher([&] {
    std::string ignored;
    while (std::getline(std::cin, ignored)) {
    }
    stopping = true;
  });

  while (!stopping) {
    control.frame();
    server->poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  control.stop();
  server->poll();
  watcher.join();
  server.reset();
  atlantis::remote::removeSessionFile(*sessionFile);
  return 0;
}
