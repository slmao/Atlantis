// Plan 0057 M3 (Spec 0057 R3, R4, R5, R10; ADR-0111 D3, D4): the generated
// typed layer over the fixture scene, no GPU -- typed calls submit exactly
// the commands their reflective equivalents submit; a component read is one
// batch; value initialization writes zeros (and enum value 0), a bare add
// World's defaults; and the binding check is recursive, per type, and
// refuses a whole transaction that touches an incompatible type.

#include "gameplay_fixture.h"

#include <atlantis/gameplay/generated/world.h>
#include <atlantis/gameplay/transaction.h>
#include <atlantis/gameplay/world.h>

#include <atlantis/world/world_schema.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <deque>
#include <functional>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace atlantis::gameplay;
using namespace atlantis::gameplay::test;
namespace w = atlantis::gameplay::world;
namespace schema = atlantis::schema;

[[nodiscard]] float asFloat(const PropertyValue& value) {
  REQUIRE(std::holds_alternative<float>(value));
  return std::get<float>(value);
}

// A mutable copy of worldSchema() a test can change, served through
// RecordingConnection::schemaOverride. Field and constant arrays are owned
// here; names stay views of the static table's strings.
struct SchemaCopy {
  SchemaCopy() {
    for (const schema::TypeDescriptor& type : atlantis::world::worldSchema()) {
      fields.emplace_back(type.fields.begin(), type.fields.end());
      constants.emplace_back(type.constants.begin(), type.constants.end());
      types.push_back(type);
    }
    relink();
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
    FAIL("no type " << name);
    return 0;
  }
  [[nodiscard]] schema::FieldDescriptor& field(std::string_view type, std::string_view name) {
    for (schema::FieldDescriptor& f : fields[indexOf(type)]) {
      if (f.name == name) return f;
    }
    FAIL("no field " << name);
    return fields[0][0];
  }
  std::deque<std::vector<schema::FieldDescriptor>> fields;
  std::deque<std::vector<schema::EnumConstantDescriptor>> constants;
  std::vector<schema::TypeDescriptor> types;
};

}  // namespace

TEST_CASE("typed get and set: the leaf's C++ type, and the same command as the reflective set",
          "[gameplay_sdk][typed]") {
  GameplayFixture f;
  CHECK(f.world.get(kSun, w::fields::Light.intensity).value() == 1.2f);
  CHECK(f.world.get(kSun, w::fields::Light.kind).value() == w::LightKind::Directional);
  CHECK(f.world.get(kLamp, w::fields::Light.kind).value() == w::LightKind::Point);
  CHECK(f.world.get(kMesh, w::fields::Renderable.materialAsset).value().has_value());

  REQUIRE(f.world.set(kSun, w::fields::Light.intensity, 2.0f).isOk());
  REQUIRE(f.world.set(kSun, "Light.intensity", 2.0f).isOk());
  REQUIRE(f.recording.submissions.size() == 2);
  CHECK(sameCommands(f.recording.submissions[0].commands, f.recording.submissions[1].commands));  // R4 parity
  CHECK_FALSE(f.recording.submissions[0].transaction);
  f.frame();
  CHECK(f.world.get(kSun, w::fields::Light.intensity).value() == 2.0f);

  const auto missing = f.world.get(kEmpty, w::fields::Light.intensity);
  REQUIRE(missing.isErr());
  CHECK(missing.error().access == access::AccessError::ComponentMissing);
}

TEST_CASE("read<C>: every leaf in one batched getProperties, nested structs filled", "[gameplay_sdk][typed]") {
  GameplayFixture f;
  const auto sun = f.world.read<w::Light>(kSun);
  REQUIRE(sun.isOk());
  CHECK(sun.value().kind == w::LightKind::Directional);
  CHECK(sun.value().color == std::array<float, 3>{0.6f, 0.7f, 1.0f});
  CHECK(sun.value().intensity == 1.2f);
  CHECK(f.batch.getPropertiesBatches == std::vector<std::size_t>{4});

  const auto camera = f.world.read<w::Camera>(kCamera);
  REQUIRE(camera.isOk());
  CHECK(camera.value().fovYRadians == asFloat(f.world.get(kCamera, "Camera.fovYRadians").value()));
  CHECK(camera.value().fog.density == asFloat(f.world.get(kCamera, "Camera.fog.density").value()));
  CHECK(camera.value().bloom.threshold == asFloat(f.world.get(kCamera, "Camera.bloom.threshold").value()));
  CHECK(f.batch.getPropertiesBatches == std::vector<std::size_t>{4, 11});

  const auto missing = f.world.read<w::Light>(kEmpty);
  REQUIRE(missing.isErr());
  CHECK(missing.error().kind == ErrorKind::Refused);
  CHECK(missing.error().access == access::AccessError::ComponentMissing);
}

TEST_CASE("typed entitiesWith equals the reflective filter", "[gameplay_sdk][typed]") {
  GameplayFixture f;
  CHECK(f.world.entitiesWith<w::Light>().value() == f.world.entitiesWith({"Light"}).value());
  CHECK(f.world.entitiesWith<w::Light, w::WorldMatrix>().value() == std::vector<EntityGuid>{kSun, kLamp});
  CHECK(f.world.entitiesWith<w::Renderable>().value() == std::vector<EntityGuid>{kMesh});
}

TEST_CASE("typed add: AddComponent then every leaf in descriptor order -- the reflective transaction's commands",
          "[gameplay_sdk][typed]") {
  GameplayFixture f;
  const EntityGuid beacon = guid("57005700-0000-4000-8000-0000000000b1");
  Transaction typed;
  typed.create(beacon)
      .add(beacon, w::Light{w::LightKind::Point, {1.0f, 0.6f, 0.2f}, 4.0f, 4.0f})
      .add(beacon, w::WorldMatrix{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {1.0f, 2.0f, 0.0f, 1.0f}});
  Transaction reflective;
  reflective.create(beacon)
      .add(beacon, "Light",
           {{"kind", access::EnumValue{1}},
            {"color", std::array<float, 3>{1.0f, 0.6f, 0.2f}},
            {"intensity", 4.0f},
            {"range", 4.0f}})
      .add(beacon, "WorldMatrix",
           {{"column0", std::array<float, 4>{1, 0, 0, 0}},
            {"column1", std::array<float, 4>{0, 1, 0, 0}},
            {"column2", std::array<float, 4>{0, 0, 1, 0}},
            {"column3", std::array<float, 4>{1.0f, 2.0f, 0.0f, 1.0f}}});
  const auto ticket = f.world.submit(typed);
  REQUIRE(ticket.isOk());
  CHECK(ticket.value().count == 11);
  REQUIRE(f.world.submit(reflective).isOk());  // refused at apply (duplicate GUID); only its commands matter here
  REQUIRE(f.recording.submissions.size() == 2);
  CHECK(sameCommands(f.recording.submissions[0].commands, f.recording.submissions[1].commands));
  f.frame();
  FailureLog log;
  log.absorb(f.world.drainFailures());
  CHECK_FALSE(log.refusal(ticket.value()).has_value());
  const auto beaconLight = f.world.read<w::Light>(beacon);
  REQUIRE(beaconLight.isOk());
  CHECK(beaconLight.value().kind == w::LightKind::Point);
  CHECK(beaconLight.value().intensity == 4.0f);

  // Typed set and remove inside a transaction, typed decode of the events.
  Subscription events = f.world.subscribe(atlantis::connection::EventFilter{
      atlantis::connection::EventKindSet::all(), beacon, std::nullopt});
  Transaction move;
  move.set(beacon, w::fields::WorldMatrix.column3, {3.0f, 2.0f, 1.0f, 1.0f}).set(beacon, w::fields::Light.intensity, 5.0f);
  REQUIRE(f.world.submit(move).isOk());
  f.frame();
  const auto drained = events.drain().value();
  REQUIRE(drained.size() == 2);
  CHECK(f.world.decode(drained[0], w::fields::WorldMatrix.column3).value() ==
        std::optional<std::array<float, 4>>{{3.0f, 2.0f, 1.0f, 1.0f}});
  CHECK_FALSE(f.world.decode(drained[0], w::fields::Light.intensity).value().has_value());
  CHECK(f.world.decode(drained[1], w::fields::Light.intensity).value() == std::optional<float>{5.0f});
  Transaction gone;
  gone.remove<w::WorldMatrix>(beacon).remove<w::Light>(beacon).destroy(beacon);
  REQUIRE(f.world.submit(gone).isOk());
  f.frame();
  CHECK_FALSE(f.world.exists(beacon));
}

TEST_CASE("value initialization writes zeros and enum value 0; a bare add gives World's defaults (R10)",
          "[gameplay_sdk][typed]") {
  GameplayFixture f;
  const EntityGuid zeroed = guid("57005700-0000-4000-8000-0000000000c1");
  const EntityGuid defaulted = guid("57005700-0000-4000-8000-0000000000c2");
  Transaction tx;
  tx.create(zeroed).add(zeroed, w::Light{}).create(defaulted).add<w::Light>(defaulted);
  REQUIRE(f.world.submit(tx).isOk());
  f.frame();
  CHECK(f.world.drainFailures().empty());
  const w::Light zero = f.world.read<w::Light>(zeroed).value();
  CHECK(zero.kind == w::LightKind::Directional);  // enum value 0 is Directional (zeroIsDeclared)
  CHECK(zero.color == std::array<float, 3>{0.0f, 0.0f, 0.0f});
  CHECK(zero.intensity == 0.0f);
  CHECK(zero.range == 0.0f);
  const w::Light world = f.world.read<w::Light>(defaulted).value();
  CHECK(world.kind == w::LightKind::Directional);
  CHECK(world.color == std::array<float, 3>{1.0f, 1.0f, 1.0f});
  CHECK(world.intensity == 1.0f);
}

TEST_CASE("schema compatibility: per type, recursive, and a transaction touching a mismatch is not submitted (R5)",
          "[gameplay_sdk][typed][compatibility]") {
  GameplayFixture f;
  using Mutation = std::function<void(SchemaCopy&)>;
  const std::vector<std::pair<std::string_view, Mutation>> lightMismatches{
      {"a changed schemaVersion", [](SchemaCopy& c) { c.types[c.indexOf("world::Light")].schemaVersion = 2; }},
      {"a changed primitive kind",
       [](SchemaCopy& c) { c.field("world::Light", "range").primitive = schema::PrimitiveKind::UInt64; }},
      {"a changed referenced TypeId",
       [](SchemaCopy& c) { c.field("world::Light", "kind").type = schema::typeId("world::CameraFog"); }},
      {"a dropped Editable flag",
       [](SchemaCopy& c) { c.field("world::Light", "intensity").flags = schema::FieldFlags::Serializable; }},
      {"a missing field", [](SchemaCopy& c) { c.fields[c.indexOf("world::Light")].pop_back(); }},
      {"an extra field",
       [](SchemaCopy& c) {
         c.fields[c.indexOf("world::Light")].push_back(schema::FieldDescriptor{
             schema::fieldId("world::Light", "extra"), "extra", schema::TypeKind::Primitive,
             schema::PrimitiveKind::Float32, schema::TypeId{}, schema::FieldFlags::Editable, 0});
       }},
      {"a renamed constant of the referenced enum (recursive)",
       [](SchemaCopy& c) { c.constants[c.indexOf("world::LightKind")][1].name = "Spot"; }},
      {"a changed constant value of the referenced enum (recursive)",
       [](SchemaCopy& c) { c.constants[c.indexOf("world::LightKind")][1].value = 7; }},
  };
  for (const auto& [what, mutate] : lightMismatches) {
    INFO(what);
    SchemaCopy changed;
    mutate(changed);
    changed.relink();
    f.recording.schemaOverride = std::span<const schema::TypeDescriptor>(changed.types);
    World world(f.recording, &f.batch);
    const auto refused = world.get(kSun, w::fields::Light.intensity);
    REQUIRE(refused.isErr());
    CHECK(refused.error().kind == ErrorKind::SchemaMismatch);
    CHECK(refused.error().subject == "world::Light");
    CHECK(world.read<w::Light>(kSun).error().kind == ErrorKind::SchemaMismatch);
    CHECK(world.set(kSun, w::fields::Light.intensity, 2.0f).error().kind == ErrorKind::SchemaMismatch);
    // Unrelated types still work.
    CHECK(world.get(kSun, w::fields::WorldMatrix.column3).isOk());
    CHECK(world.read<w::Transform>(kSun).isOk());
  }

  SECTION("a change only to a nested struct makes its component incompatible") {
    SchemaCopy changed;
    changed.types[changed.indexOf("world::CameraFog")].schemaVersion = 2;
    changed.relink();
    f.recording.schemaOverride = std::span<const schema::TypeDescriptor>(changed.types);
    World world(f.recording, &f.batch);
    CHECK(world.read<w::Camera>(kCamera).error().kind == ErrorKind::SchemaMismatch);
    CHECK(world.get(kCamera, w::fields::Camera.nearZ).error().kind == ErrorKind::SchemaMismatch);
    CHECK(world.read<w::Light>(kSun).isOk());
  }

  SECTION("a mixed transaction with an incompatible type is not submitted: no ticket, no command") {
    SchemaCopy changed;
    changed.types[changed.indexOf("world::Light")].schemaVersion = 2;
    changed.relink();
    f.recording.schemaOverride = std::span<const schema::TypeDescriptor>(changed.types);
    World world(f.recording, &f.batch);
    Transaction mixed;
    mixed.set(kSun, w::fields::WorldMatrix.column3, {0.0f, 1.0f, 0.0f, 1.0f})  // compatible
        .set(kSun, "Transform.localScale", std::array<float, 3>{2.0f, 2.0f, 2.0f})  // reflective
        .set(kSun, w::fields::Light.intensity, 2.0f);  // incompatible
    const auto refused = world.submit(mixed);
    REQUIRE(refused.isErr());
    CHECK(refused.error().kind == ErrorKind::SchemaMismatch);
    CHECK(f.recording.submissions.empty());
    // Typed decoding of the incompatible type is refused too; the reflective
    // layer is not.
    const access::Event changedEvent = access::PropertyChanged{
        {kSun, schema::typeId("world::Light"), schema::fieldId("world::Light", "intensity")}, 2.0f};
    CHECK(world.decode(changedEvent, w::fields::Light.intensity).error().kind == ErrorKind::SchemaMismatch);
    CHECK(world.get(kSun, "Light.intensity").isOk());
  }
  f.recording.schemaOverride.reset();
}
