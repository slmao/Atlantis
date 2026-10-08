#include "editor_fixture.h"

#include <atlantis/connection/text.h>
#include <atlantis/editor/model/gizmo.h>
#include <atlantis/editor/model/hierarchy.h>
#include <atlantis/editor/model/inspector.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <variant>
#include <vector>

// Plan 0056 M4 (P9; Spec 0056 R2, R3, R5): the editor's models over an
// InProcess connection on the baked fixture scene -- no GPU, no UI. Frames are
// the fake Runtime's: each applies pending commands unless paused.

namespace {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
namespace text = atlantis::connection::text;
using atlantis::asset_system::EntityGuid;
using atlantis::editor::decompose;
using atlantis::editor::compose;
using atlantis::editor::EditStatus;
using atlantis::editor::FieldControl;
using atlantis::editor::GizmoAxis;
using atlantis::editor::GizmoMode;
using atlantis::editor::GizmoModel;
using atlantis::editor::HierarchyModel;
using atlantis::editor::HierarchyRow;
using atlantis::editor::InspectorModel;
using atlantis::editor::MatrixColumns;
using atlantis::editor::Trs;
using atlantis::editor::test::EditorFixture;
using atlantis::editor::test::guid;

const EntityGuid kMesh = guid("52052052-0001-4052-8052-000000000001");
const EntityGuid kSun = guid("52052052-0002-4052-8052-000000000002");
const EntityGuid kCamera = guid("52052052-0004-4052-8052-000000000004");
const EntityGuid kNew1 = guid("56056056-0001-4056-8056-000000000001");
const EntityGuid kNew2 = guid("56056056-0002-4056-8056-000000000002");
const EntityGuid kNew3 = guid("56056056-0003-4056-8056-000000000003");
const EntityGuid kNew4 = guid("56056056-0004-4056-8056-000000000004");

[[nodiscard]] atlantis::schema::TypeId typeNamed(connection::RuntimeConnection& c, std::string_view name) {
  const auto* type = text::findType(c.schema(), name);
  REQUIRE(type != nullptr);
  return type->id;
}

// What a fresh listing shows now.
[[nodiscard]] std::vector<HierarchyRow> freshListing(connection::RuntimeConnection& c) {
  std::vector<HierarchyRow> rows;
  for (const EntityGuid& entity : c.listEntities()) rows.push_back(HierarchyRow{entity, c.listComponents(entity).value()});
  return rows;
}

[[nodiscard]] std::size_t fieldIndex(const InspectorModel& inspector, std::string_view path) {
  for (std::size_t i = 0; i < inspector.fields().size(); ++i) {
    if (inspector.fields()[i].path == path) return i;
  }
  FAIL("no field " << path);
  return 0;
}

// The PropertyChanged events a frame produced for `entity`, through a
// subscription of the test's own.
struct Changes {
  Changes(connection::RuntimeConnection& c, const EntityGuid& entity) : source(c) {
    connection::EventFilter filter;
    filter.kinds = connection::EventKindSet::only(connection::EventKind::PropertyChanged);
    filter.entity = entity;
    subscription = c.subscribe(filter);
  }
  [[nodiscard]] std::vector<access::PropertyChanged> drain() {
    std::vector<access::PropertyChanged> out;
    auto events = source.drainEvents(subscription);  // held: value() refers into it
    for (const access::Event& event : events.value()) {
      out.push_back(std::get<access::PropertyChanged>(event));
    }
    return out;
  }
  connection::RuntimeConnection& source;
  connection::SubscriptionId subscription;
};

[[nodiscard]] MatrixColumns readMatrix(connection::RuntimeConnection& c, const EntityGuid& entity) {
  const auto* type = text::findType(c.schema(), "WorldMatrix");
  REQUIRE(type != nullptr);
  MatrixColumns columns{};
  for (std::size_t k = 0; k < 4; ++k) {
    columns[k] = std::get<std::array<float, 4>>(c.getProperty({entity, type->id, type->fields[k].id}).value());
  }
  return columns;
}

}  // namespace

// --- Hierarchy (R2, ruling Q4) ------------------------------------------------

TEST_CASE("the Hierarchy's first listing is every entity with its components, in GUID order", "[editor][model][hierarchy]") {
  EditorFixture fixture;
  HierarchyModel hierarchy(*fixture.connection);
  CHECK(hierarchy.rows().empty());
  hierarchy.update();
  CHECK(hierarchy.rows() == freshListing(*fixture.connection));
  CHECK(hierarchy.rows().size() == 5);
  CHECK(hierarchy.componentNames(hierarchy.rows()[1]).find("Light") != std::string::npos);
}

TEST_CASE("the Hierarchy's incremental state equals a fresh listing after creates, destroys, adds and removes",
          "[editor][model][hierarchy]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  HierarchyModel hierarchy(c);
  hierarchy.update();
  const auto transform = typeNamed(c, "Transform");
  const auto worldMatrix = typeNamed(c, "WorldMatrix");
  const auto renderable = typeNamed(c, "Renderable");
  const auto step = [&] {
    fixture.runtime.frame();
    hierarchy.update();
    REQUIRE(hierarchy.rows() == freshListing(c));
  };

  // Single commands: creates, then adds.
  (void)c.submit(access::CreateEntity{kNew1});
  (void)c.submit(access::CreateEntity{kNew2});
  step();
  CHECK(hierarchy.rows().size() == 7);
  (void)c.submit(access::AddComponent{kNew1, transform});
  (void)c.submit(access::AddComponent{kNew1, worldMatrix});
  (void)c.submit(access::RemoveComponent{kMesh, renderable});
  step();
  // A transaction: a create with a component, and a destroy.
  (void)c.submitTransaction({access::CreateEntity{kNew3}, access::AddComponent{kNew3, transform},
                             access::DestroyEntity{kNew2}});
  step();
  CHECK(hierarchy.rows().size() == 7);
  // A refused transaction changes nothing.
  (void)c.submitTransaction({access::CreateEntity{kNew4}, access::CreateEntity{kNew1}});
  step();
  // Created and destroyed in the same frame: never listed.
  (void)c.submit(access::CreateEntity{kNew4});
  (void)c.submit(access::DestroyEntity{kNew4});
  (void)c.submit(access::RemoveComponent{kNew1, worldMatrix});
  step();
  (void)c.drainFailures();
}

TEST_CASE("the Hierarchy clears a selection whose entity is destroyed", "[editor][model][hierarchy]") {
  EditorFixture fixture;
  HierarchyModel hierarchy(*fixture.connection);
  hierarchy.update();
  (void)fixture.connection->submit(access::CreateEntity{kNew1});
  fixture.runtime.frame();
  hierarchy.update();
  hierarchy.select(kNew1);
  CHECK(hierarchy.selection() == kNew1);
  (void)fixture.connection->submit(access::DestroyEntity{kNew1});
  fixture.runtime.frame();
  hierarchy.update();
  CHECK_FALSE(hierarchy.selection().has_value());
  hierarchy.select(kNew1);  // not listed: no selection
  CHECK_FALSE(hierarchy.selection().has_value());
}

TEST_CASE("the Hierarchy filter matches a GUID prefix or a component name", "[editor][model][hierarchy]") {
  EditorFixture fixture;
  HierarchyModel hierarchy(*fixture.connection);
  hierarchy.update();
  CHECK(hierarchy.filtered("").size() == 5);
  const auto lights = hierarchy.filtered("light");
  REQUIRE(lights.size() == 2);  // the directional and the point light
  const auto byGuid = hierarchy.filtered("52052052-0002");
  REQUIRE(byGuid.size() == 1);
  CHECK(hierarchy.rows()[byGuid[0]].entity == kSun);
  CHECK(hierarchy.filtered("0002-4052").empty());  // a prefix, not a substring, of the GUID
  CHECK(hierarchy.filtered("camera").size() == 1);
}

// --- Inspector (R3) --------------------------------------------------------------

TEST_CASE("the Inspector's generated field table equals the schema's leaves for every World component",
          "[editor][model][inspector]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  InspectorModel inspector(c);
  std::set<std::string> covered;
  for (const EntityGuid& entity : c.listEntities()) {
    inspector.setSubject(entity);
    std::size_t at = 0;
    const auto components = c.listComponents(entity);  // held: value() refers into it
    for (const auto component : components.value()) {
      const auto* type = text::findType(c.schema(), component);
      REQUIRE(type != nullptr);
      covered.insert(std::string(text::shortName(type->name)));
      for (const text::LeafPath& leaf : text::leavesOf(c.schema(), component)) {
        REQUIRE(at < inspector.fields().size());
        const auto& field = inspector.fields()[at++];
        INFO(leaf.path);
        CHECK(field.component == component);
        CHECK(field.path == leaf.path);
        CHECK(field.field == leaf.leaf->id);
        CHECK(field.kind == leaf.leaf->kind);
        CHECK(field.editable == atlantis::schema::hasFlags(leaf.leaf->flags, atlantis::schema::FieldFlags::Editable));
        CHECK(field.optional == atlantis::schema::hasFlags(leaf.leaf->flags, atlantis::schema::FieldFlags::Optional));
        CHECK(field.label == std::string(leaf.leaf->name));
        if (leaf.leaf->kind == atlantis::schema::TypeKind::Enum) {
          const auto* enumType = text::findType(c.schema(), leaf.leaf->type);
          REQUIRE(enumType != nullptr);
          REQUIRE(field.choices.size() == enumType->constants.size());
          for (std::size_t k = 0; k < field.choices.size(); ++k) {
            CHECK(field.choices[k].name == std::string(enumType->constants[k].name));
            CHECK(field.choices[k].value == enumType->constants[k].value);
          }
          CHECK(field.control == FieldControl::Enum);
        } else {
          CHECK(field.primitive == leaf.leaf->primitive);
          CHECK(field.choices.empty());
        }
        CHECK(field.value.has_value());
      }
    }
    CHECK(at == inspector.fields().size());
  }
  CHECK(covered == std::set<std::string>{"Camera", "Light", "Renderable", "Transform", "WorldMatrix"});

  // Nesting: Camera's fog and bloom leaves are grouped under their struct.
  inspector.setSubject(kCamera);
  const auto& density = inspector.fields()[fieldIndex(inspector, "Camera.fog.density")];
  CHECK(density.groups == std::vector<std::string>{"fog"});
  CHECK(density.label == "density");
  CHECK(density.control == FieldControl::Float);
  CHECK(inspector.fields()[fieldIndex(inspector, "Camera.nearZ")].groups.empty());
  // Optional: Renderable.materialAsset.
  inspector.setSubject(kMesh);
  CHECK(inspector.fields()[fieldIndex(inspector, "Renderable.materialAsset")].optional);
  CHECK(inspector.fields()[fieldIndex(inspector, "Renderable.meshAsset")].control == FieldControl::Integer);
  CHECK(inspector.fields()[fieldIndex(inspector, "WorldMatrix.column2")].control == FieldControl::Float4);
  CHECK(inspector.fields()[fieldIndex(inspector, "Transform.localScale")].control == FieldControl::Float3);
  // The Transform note (ruling Q5), and nothing on other components.
  CHECK(inspector.componentNote(typeNamed(c, "Transform")) == atlantis::editor::kTransformNote);
  CHECK(inspector.componentNote(typeNamed(c, "WorldMatrix")).empty());
}

TEST_CASE("an Inspector edit of each kind is exactly one SetProperty of the field's value alternative",
          "[editor][model][inspector]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  InspectorModel inspector(c);

  struct Edit {
    EntityGuid entity;
    std::string_view path;
    access::PropertyValue value;
  };
  const std::vector<Edit> edits{
      {kSun, "Light.intensity", 6.0f},
      {kSun, "Light.color", std::array<float, 3>{0.1f, 0.2f, 0.3f}},
      {kSun, "Light.kind", access::EnumValue{1}},  // Point
      {kMesh, "WorldMatrix.column0", std::array<float, 4>{2.0f, 0.0f, 0.0f, 0.0f}},
      {kMesh, "Renderable.materialAsset", access::Absent{}},
      {kCamera, "Camera.fog.density", 0.25f},
  };
  for (const Edit& edit : edits) {
    INFO(edit.path);
    inspector.setSubject(edit.entity);
    Changes changes(c, edit.entity);
    const std::size_t index = fieldIndex(inspector, edit.path);
    const access::CommandTicket ticket = inspector.commit(index, edit.value);
    REQUIRE(inspector.outcome(index).has_value());
    CHECK(inspector.outcome(index)->status == EditStatus::Pending);
    CHECK(inspector.outcome(index)->ticket == ticket);
    // Exactly one command: the next submission (a probe the World refuses,
    // harmlessly) gets the very next ticket.
    const access::CommandTicket probe = c.submit(access::DestroyEntity{kNew4});
    CHECK(probe.value == ticket.value + 1);
    fixture.runtime.frame();
    const auto events = changes.drain();
    REQUIRE(events.size() == 1);
    CHECK(events[0].address.field == inspector.fields()[index].field);
    CHECK(events[0].value == edit.value);
    inspector.update(c.drainFailures());
    CHECK(inspector.outcome(index)->status == EditStatus::Applied);
    CHECK(inspector.fields()[index].value == edit.value);
  }
}

TEST_CASE("a refused Inspector edit reports the refusal beside the field", "[editor][model][inspector]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  InspectorModel inspector(c);
  inspector.setSubject(kSun);
  const std::size_t intensity = fieldIndex(inspector, "Light.intensity");
  const std::size_t kind = fieldIndex(inspector, "Light.kind");
  (void)inspector.commit(intensity, std::numeric_limits<float>::quiet_NaN());
  (void)inspector.commit(kind, access::EnumValue{7});
  fixture.runtime.frame();
  inspector.update(c.drainFailures());
  REQUIRE(inspector.outcome(intensity).has_value());
  CHECK(inspector.outcome(intensity)->status == EditStatus::Refused);
  CHECK(inspector.outcome(intensity)->detail == std::string(access::toString(access::AccessError::NonFiniteValue)));
  CHECK(inspector.outcome(kind)->status == EditStatus::Refused);
  CHECK(inspector.outcome(kind)->detail == std::string(access::toString(access::AccessError::EnumValueOutOfRange)));
  CHECK(inspector.fields()[intensity].value == access::PropertyValue{1.2f});  // unchanged
}

TEST_CASE("the Inspector holds a pending edit while the Runtime is paused", "[editor][model][inspector]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  InspectorModel inspector(c);
  inspector.setSubject(kSun);
  const std::size_t intensity = fieldIndex(inspector, "Light.intensity");
  fixture.runtime.pause();
  (void)inspector.commit(intensity, 2.0f);
  fixture.runtime.frame();
  inspector.update(c.drainFailures());
  CHECK(inspector.outcome(intensity)->status == EditStatus::Pending);
  fixture.runtime.resume();
  fixture.runtime.frame();
  inspector.update(c.drainFailures());
  CHECK(inspector.outcome(intensity)->status == EditStatus::Applied);
}

// --- Gizmo (R5, ruling Q5) --------------------------------------------------------

TEST_CASE("decompose(compose(T, R, S)) recovers T, R and S", "[editor][model][gizmo]") {
  std::mt19937 random(0x0056);
  std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
  std::uniform_real_distribution<float> scale(0.1f, 5.0f);
  for (int i = 0; i < 500; ++i) {
    // A random proper rotation from a random unit quaternion.
    float qx = unit(random), qy = unit(random), qz = unit(random), qw = unit(random);
    const float n = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    qx /= n, qy /= n, qz /= n, qw /= n;
    Trs trs;
    trs.translation = {100.0f * unit(random), 100.0f * unit(random), 100.0f * unit(random)};
    trs.rotation = {1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw),     2 * (qx * qz - qy * qw),
                    2 * (qx * qy - qz * qw),     1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw),
                    2 * (qx * qz + qy * qw),     2 * (qy * qz - qx * qw),     1 - 2 * (qx * qx + qy * qy)};
    trs.scale = {scale(random) * (i % 7 == 0 ? -1.0f : 1.0f), scale(random), scale(random)};
    const auto back = decompose(compose(trs));
    REQUIRE(back.has_value());
    for (int k = 0; k < 3; ++k) CHECK(back->translation[k] == trs.translation[k]);  // exact
    for (int k = 0; k < 3; ++k) CHECK(std::fabs(back->scale[k] - trs.scale[k]) <= 1e-5f * std::fabs(trs.scale[k]) + 1e-6f);
    for (int k = 0; k < 9; ++k) CHECK(std::fabs(back->rotation[k] - trs.rotation[k]) <= 1e-5f);
  }
}

TEST_CASE("a sheared, degenerate or projective matrix has no TRS form", "[editor][model][gizmo]") {
  const MatrixColumns identity{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
  CHECK(decompose(identity).has_value());
  MatrixColumns sheared = identity;
  sheared[1] = {0.3f, 1.0f, 0.0f, 0.0f};
  CHECK_FALSE(decompose(sheared).has_value());
  MatrixColumns degenerate = identity;
  degenerate[2] = {0.0f, 0.0f, 0.0f, 0.0f};
  CHECK_FALSE(decompose(degenerate).has_value());
  MatrixColumns projective = identity;
  projective[0][3] = 0.5f;
  CHECK_FALSE(decompose(projective).has_value());
}

TEST_CASE("a translate drag changes only column3, and writes one four-column transaction per frame plus the release",
          "[editor][model][gizmo]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  GizmoModel gizmo(c);
  gizmo.setSubject(kMesh);
  REQUIRE(gizmo.available());
  const MatrixColumns start = *gizmo.matrix();
  Changes changes(c, kMesh);

  gizmo.beginDrag(GizmoAxis::X);
  for (int i = 1; i <= 5; ++i) gizmo.dragTo(0.25f * static_cast<float>(i));  // five input events, one frame
  const auto first = gizmo.flush();
  REQUIRE(first.has_value());
  CHECK(first->count == 4);
  CHECK_FALSE(gizmo.flush().has_value());  // nothing new this frame
  fixture.runtime.frame();
  gizmo.update();  // ignored while dragging: the drag state stands
  auto events = changes.drain();
  CHECK(events.size() == 4);

  gizmo.dragTo(2.0f);
  gizmo.endDrag();
  const auto last = gizmo.flush();  // the final value, once
  REQUIRE(last.has_value());
  CHECK(last->count == 4);
  CHECK_FALSE(gizmo.flush().has_value());
  fixture.runtime.frame();
  events = changes.drain();
  CHECK(events.size() == 4);

  const MatrixColumns now = readMatrix(c, kMesh);
  for (std::size_t k = 0; k < 3; ++k) CHECK(now[k] == start[k]);  // bit for bit
  CHECK(now[3][0] == start[3][0] + 2.0f);
  CHECK(now[3][1] == start[3][1]);
  CHECK(now[3][2] == start[3][2]);
  CHECK(now[3][3] == start[3][3]);
  gizmo.update();
  CHECK(*gizmo.matrix() == now);
}

TEST_CASE("rotate and scale drags recompose the matrix about a world axis and along the entity's own axis",
          "[editor][model][gizmo]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  GizmoModel gizmo(c);
  gizmo.setSubject(kMesh);
  const Trs start = *decompose(*gizmo.matrix());

  gizmo.setMode(GizmoMode::Rotate);
  gizmo.beginDrag(GizmoAxis::Z);
  gizmo.dragTo(0.5f);
  gizmo.endDrag();
  REQUIRE(gizmo.flush().has_value());
  fixture.runtime.frame();
  gizmo.update();
  const Trs rotated = *decompose(*gizmo.matrix());
  const float co = std::cos(0.5f);
  const float si = std::sin(0.5f);
  for (int column = 0; column < 3; ++column) {
    const float x = start.rotation[column * 3 + 0];
    const float y = start.rotation[column * 3 + 1];
    CHECK(std::fabs(rotated.rotation[column * 3 + 0] - (co * x - si * y)) <= 1e-5f);
    CHECK(std::fabs(rotated.rotation[column * 3 + 1] - (si * x + co * y)) <= 1e-5f);
    CHECK(std::fabs(rotated.rotation[column * 3 + 2] - start.rotation[column * 3 + 2]) <= 1e-5f);
  }
  CHECK(rotated.translation == start.translation);
  for (int k = 0; k < 3; ++k) CHECK(std::fabs(rotated.scale[k] - start.scale[k]) <= 1e-5f);

  gizmo.setMode(GizmoMode::Scale);
  gizmo.beginDrag(GizmoAxis::Y);
  gizmo.dragTo(2.0f);
  gizmo.endDrag();
  REQUIRE(gizmo.flush().has_value());
  fixture.runtime.frame();
  gizmo.update();
  const Trs scaled = *decompose(*gizmo.matrix());
  CHECK(std::fabs(scaled.scale[1] - 2.0f * rotated.scale[1]) <= 1e-5f);
  CHECK(std::fabs(scaled.scale[0] - rotated.scale[0]) <= 1e-5f);
  CHECK(std::fabs(scaled.scale[2] - rotated.scale[2]) <= 1e-5f);
}

TEST_CASE("no Gizmo without a WorldMatrix, or on a sheared one", "[editor][model][gizmo]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  GizmoModel gizmo(c);
  (void)c.submitTransaction({access::CreateEntity{kNew1}, access::AddComponent{kNew1, typeNamed(c, "Transform")}});
  fixture.runtime.frame();
  gizmo.setSubject(kNew1);
  CHECK_FALSE(gizmo.matrix().has_value());
  CHECK_FALSE(gizmo.available());

  gizmo.setSubject(kMesh);
  REQUIRE(gizmo.available());
  const auto* worldMatrix = text::findType(c.schema(), "WorldMatrix");
  (void)c.submit(access::SetProperty{{kMesh, worldMatrix->id, worldMatrix->fields[1].id},
                                     std::array<float, 4>{0.5f, 1.0f, 0.0f, 0.0f}});
  fixture.runtime.frame();
  gizmo.update();
  CHECK(gizmo.matrix().has_value());
  CHECK_FALSE(gizmo.available());
  gizmo.setSubject(std::nullopt);
  CHECK_FALSE(gizmo.available());
  CHECK_FALSE(gizmo.flush().has_value());
}
