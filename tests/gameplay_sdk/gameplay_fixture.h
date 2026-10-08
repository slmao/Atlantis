#pragma once

#include <atlantis/gameplay/query_batch.h>
#include <atlantis/gameplay/world.h>

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/connection/in_process_endpoint.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

// Plan 0057 M2/M3: the Gameplay SDK over the Spec 0054/0055/0056 fixture
// scene through an InProcess connection -- no GPU. RecordingConnection
// forwards to the real connection and records every submission (the R4
// parity checks) and can serve a modified schema (the R5 compatibility
// checks); RecordingBatch counts batch calls (Q9).
namespace atlantis::gameplay::test {

namespace fs = std::filesystem;
namespace access = ::atlantis::world::access;
namespace connection = ::atlantis::connection;

[[nodiscard]] inline ::atlantis::asset_system::ValidatedSceneData cookAndDecodeScene(const std::string& sourceText) {
  static const std::string tag = std::to_string(std::random_device{}());
  static std::atomic<int> counter{0};
  const fs::path dir = fs::temp_directory_path() / "atlantis_gameplay_sdk_tests" /
                       ("fixture_" + tag + "_" + std::to_string(counter.fetch_add(1)));
  fs::create_directories(dir);
  {
    std::ofstream out(dir / "scene.scene.txt", std::ios::binary | std::ios::trunc);
    out << sourceText;
  }
  const auto guid = ::atlantis::asset_system::deriveAssetGuid(
      ::atlantis::asset_system::parseAssetGuid("00570057-0057-4057-8057-005700570057").value(), "scene");
  REQUIRE(::atlantis::asset_system::cookScene((dir / "scene.scene.txt").string(), guid,
                                              (dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string())
              .isOk());
  auto decoded =
      ::atlantis::asset_system::decodeScene((dir / "scene.ascene").string(), (dir / "scene.ascene.meta.txt").string());
  REQUIRE(decoded.isOk());
  std::error_code ec;
  fs::remove_all(dir, ec);
  return decoded.value();
}

// The Spec 0054/0055/0056 fixture scene: a mesh node, a Directional light, a
// Point light (parented), the active camera, and an empty node.
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

[[nodiscard]] inline EntityGuid guid(std::string_view text) {
  return ::atlantis::asset_system::parseEntityGuid(text).value();
}

inline const EntityGuid kMesh = guid("52052052-0001-4052-8052-000000000001");
inline const EntityGuid kSun = guid("52052052-0002-4052-8052-000000000002");
inline const EntityGuid kLamp = guid("52052052-0003-4052-8052-000000000003");
inline const EntityGuid kCamera = guid("52052052-0004-4052-8052-000000000004");
inline const EntityGuid kEmpty = guid("52052052-0005-4052-8052-000000000005");

// Command equality (World's command structs declare no operator==).
[[nodiscard]] inline bool sameCommand(const access::Command& a, const access::Command& b) {
  if (a.index() != b.index()) return false;
  return std::visit(
      [&](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        const T& y = std::get<T>(b);
        if constexpr (std::is_same_v<T, access::CreateEntity> || std::is_same_v<T, access::DestroyEntity>) {
          return x.entity == y.entity;
        } else if constexpr (std::is_same_v<T, access::AddComponent> || std::is_same_v<T, access::RemoveComponent>) {
          return x.entity == y.entity && x.component == y.component;
        } else {
          return x.address == y.address && x.value == y.value;
        }
      },
      a);
}

[[nodiscard]] inline bool sameCommands(const std::vector<access::Command>& a, const std::vector<access::Command>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (!sameCommand(a[i], b[i])) return false;
  }
  return true;
}

// One submission as RuntimeConnection received it.
struct Submission {
  bool transaction = false;
  std::vector<access::Command> commands;
};

// Forwards every call to `inner`; records submissions; serves `schemaOverride`
// as its schema when set.
class RecordingConnection final : public connection::RuntimeConnection {
 public:
  explicit RecordingConnection(connection::RuntimeConnection& inner) : inner_(inner) {}

  bool findEntity(const EntityGuid& entity) const override { return inner_.findEntity(entity); }
  std::vector<EntityGuid> listEntities() const override { return inner_.listEntities(); }
  atlantis::Result<std::vector<schema::TypeId>, access::AccessError> listComponents(
      const EntityGuid& entity) const override {
    ++listComponentsCalls;
    return inner_.listComponents(entity);
  }
  atlantis::Result<PropertyValue, access::AccessError> getProperty(const PropertyAddress& address) const override {
    ++getPropertyCalls;
    return inner_.getProperty(address);
  }
  std::span<const schema::TypeDescriptor> schema() const override {
    return schemaOverride.has_value() ? *schemaOverride : inner_.schema();
  }
  access::CommandTicket submit(access::Command command) override {
    submissions.push_back(Submission{false, {command}});
    return inner_.submit(std::move(command));
  }
  access::TransactionTicket submitTransaction(std::vector<access::Command> commands) override {
    submissions.push_back(Submission{true, commands});
    return inner_.submitTransaction(std::move(commands));
  }
  connection::SubscriptionId subscribe(connection::EventFilter filter) override {
    return inner_.subscribe(std::move(filter));
  }
  atlantis::Result<std::monostate, connection::ConnectionError> unsubscribe(
      connection::SubscriptionId subscription) override {
    ++unsubscribeCalls;
    return inner_.unsubscribe(subscription);
  }
  atlantis::Result<std::vector<access::Event>, connection::ConnectionError> drainEvents(
      connection::SubscriptionId subscription) override {
    return inner_.drainEvents(subscription);
  }
  std::vector<access::CommandFailure> drainFailures() override { return inner_.drainFailures(); }

  std::vector<Submission> submissions;
  std::optional<std::span<const schema::TypeDescriptor>> schemaOverride;
  mutable int listComponentsCalls = 0;
  mutable int getPropertyCalls = 0;
  int unsubscribeCalls = 0;

 private:
  connection::RuntimeConnection& inner_;
};

// Counts batch calls and how many queries each carried; answers one at a
// time through the connection.
class RecordingBatch final : public QueryBatch {
 public:
  explicit RecordingBatch(connection::RuntimeConnection& connection) : sequential_(connection) {}
  std::vector<atlantis::Result<std::vector<schema::TypeId>, access::AccessError>> listComponents(
      std::span<const EntityGuid> entities) override {
    listComponentsBatches.push_back(entities.size());
    return sequential_.listComponents(entities);
  }
  std::vector<atlantis::Result<PropertyValue, access::AccessError>> getProperties(
      std::span<const PropertyAddress> addresses) override {
    getPropertiesBatches.push_back(addresses.size());
    return sequential_.getProperties(addresses);
  }
  std::vector<std::size_t> listComponentsBatches;
  std::vector<std::size_t> getPropertiesBatches;

 private:
  SequentialQueryBatch sequential_;
};

// The world, its boundary, an InProcess endpoint, the SDK's connection
// (recorded) and a World over it. Not movable (the parts borrow each other).
// frame() is what the owner's runFrame() does to the World: apply pending
// commands.
struct GameplayFixture {
  GameplayFixture()
      : scene(cookAndDecodeScene(kSceneSource)),
        baked(::atlantis::world::bakeScene(scene)),
        boundary(baked),
        endpoint(boundary),
        inner(endpoint.open()),
        recording(*inner),
        batch(recording),
        world(recording, &batch) {}
  GameplayFixture(const GameplayFixture&) = delete;
  GameplayFixture& operator=(const GameplayFixture&) = delete;

  void frame() { (void)boundary.applyPending(); }

  ::atlantis::asset_system::ValidatedSceneData scene;
  ::atlantis::world::BakedScene baked;
  access::RuntimeWorldAccess boundary;
  connection::InProcessEndpoint endpoint;
  std::unique_ptr<connection::RuntimeConnection> inner;
  RecordingConnection recording;
  RecordingBatch batch;
  World world;
};

}  // namespace atlantis::gameplay::test
