#include "editor_fixture.h"

#include <atlantis/connection/text.h>
#include <atlantis/editor/model/edit_history.h>
#include <atlantis/editor/model/gizmo.h>
#include <atlantis/editor/model/hierarchy.h>
#include <atlantis/editor/model/inspector.h>
#include <atlantis/editor/model/runtime_world_edit_target.h>
#include <atlantis/editor/model/viewport_camera.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <span>
#include <utility>
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

// --- The editor camera (Plan 0062 M1; Spec 0062 R1, R2, R6) ----------------------

namespace {

using atlantis::editor::CameraPose;
using atlantis::editor::ViewportCameraModel;
using Vec3 = std::array<float, 3>;

[[nodiscard]] float distance(const Vec3& a, const Vec3& b) {
  return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

// A camera at `eye` looking down -z.
[[nodiscard]] ViewportCameraModel cameraAt(const Vec3& eye) {
  CameraPose pose;
  pose.worldMatrix[12] = eye[0];
  pose.worldMatrix[13] = eye[1];
  pose.worldMatrix[14] = eye[2];
  pose.lens = atlantis::editor::CameraLens{1.0472f, 0.1f, 100.0f};
  ViewportCameraModel camera;
  camera.reset(pose);
  return camera;
}

// Pixels of the Viewport (width x height) where `point` projects.
[[nodiscard]] std::array<float, 2> pixelOf(const ViewportCameraModel& camera, const Vec3& point, float width,
                                           float height) {
  const auto matrices = camera.matrices(width / height);
  REQUIRE(matrices.has_value());
  const auto ndc = atlantis::editor::projectToNdc(*matrices, point);
  REQUIRE(ndc.has_value());
  return {((*ndc)[0] + 1.0f) * 0.5f * width, ((*ndc)[1] + 1.0f) * 0.5f * height};
}

}  // namespace

TEST_CASE("an editor camera reset from a pose round-trips its eye, direction and lens", "[editor][model][camera]") {
  ViewportCameraModel source = cameraAt({1.0f, 2.0f, 3.0f});
  source.look(-120.0f, 60.0f);
  ViewportCameraModel copy;
  copy.reset(source.pose());
  CHECK(distance(copy.eye(), source.eye()) < 1e-6f);
  CHECK(std::fabs(copy.yaw() - source.yaw()) < 1e-5f);
  CHECK(std::fabs(copy.pitch() - source.pitch()) < 1e-5f);
  CHECK(copy.lens() == source.lens());
}

TEST_CASE("looking turns about the eye, right and down for a drag right and down, pitch clamped at 89 degrees",
          "[editor][model][camera]") {
  ViewportCameraModel camera = cameraAt({0.0f, 0.0f, 5.0f});
  const Vec3 eye = camera.eye();
  camera.look(100.0f, 0.0f);
  CHECK(camera.forward()[0] > 0.0f);  // looks right (+x)
  CHECK(camera.eye() == eye);
  camera.look(0.0f, 100.0f);
  CHECK(camera.forward()[1] < 0.0f);  // looks down
  camera.look(0.0f, -100000.0f);
  CHECK(camera.pitch() == ViewportCameraModel::kMaxPitchRadians);
  CHECK(camera.matrices(1.0f).has_value());  // never the host's degenerate basis
  camera.look(0.0f, 100000.0f);
  CHECK(camera.pitch() == -ViewportCameraModel::kMaxPitchRadians);
}

TEST_CASE("orbiting keeps the pivot and its distance", "[editor][model][camera]") {
  ViewportCameraModel camera = cameraAt({0.0f, 1.0f, 10.0f});
  const Vec3 pivot = camera.pivot();
  const float d = camera.pivotDistance();
  camera.orbit(250.0f, -80.0f);
  CHECK(distance(camera.pivot(), pivot) < 1e-4f);
  CHECK(std::fabs(distance(camera.eye(), pivot) - d) < 1e-4f);
  CHECK(camera.eye()[0] < 0.0f);  // a drag right carries the eye left around the pivot
}

TEST_CASE("panning keeps the point under the pointer at the pivot's depth under it", "[editor][model][camera]") {
  constexpr float kWidth = 800.0f;
  constexpr float kHeight = 450.0f;
  ViewportCameraModel camera = cameraAt({0.0f, 0.0f, 10.0f});
  camera.look(30.0f, 20.0f);
  const Vec3 pivot = camera.pivot();
  const auto before = pixelOf(camera, pivot, kWidth, kHeight);
  camera.pan(37.0f, -12.0f, kHeight);
  const auto after = pixelOf(camera, pivot, kWidth, kHeight);
  CHECK(std::fabs(after[0] - (before[0] + 37.0f)) < 1e-2f);
  CHECK(std::fabs(after[1] - (before[1] - 12.0f)) < 1e-2f);
}

TEST_CASE("dollying moves toward the pivot and stops at its floor", "[editor][model][camera]") {
  ViewportCameraModel camera = cameraAt({0.0f, 0.0f, 10.0f});
  const Vec3 pivot = camera.pivot();
  camera.dolly(1.0f);
  CHECK(std::fabs(camera.pivotDistance() - 10.0f * ViewportCameraModel::kWheelDollyFactor) < 1e-5f);
  CHECK(distance(camera.pivot(), pivot) < 1e-5f);
  camera.dollyDrag(50.0f);
  CHECK(camera.pivotDistance() < 10.0f * ViewportCameraModel::kWheelDollyFactor);
  camera.dolly(1000.0f);
  CHECK(camera.pivotDistance() == ViewportCameraModel::kMinPivotDistance);
  CHECK(distance(camera.pivot(), pivot) < 1e-4f);
}

TEST_CASE("flying moves speed x time along the camera's axes, x4 when fast", "[editor][model][camera]") {
  ViewportCameraModel camera = cameraAt({0.0f, 0.0f, 0.0f});
  camera.fly({0.0f, 0.0f, 1.0f}, 0.5f, false);
  CHECK(std::fabs(camera.eye()[2] + ViewportCameraModel::kDefaultFlySpeed * 0.5f) < 1e-5f);
  camera.fly({1.0f, 0.0f, 0.0f}, 0.25f, true);
  CHECK(std::fabs(camera.eye()[0] - ViewportCameraModel::kDefaultFlySpeed * 0.25f * 4.0f) < 1e-5f);
  camera.fly({0.0f, 1.0f, 0.0f}, 0.1f, false);
  CHECK(std::fabs(camera.eye()[1] - ViewportCameraModel::kDefaultFlySpeed * 0.1f) < 1e-5f);
  camera.scaleFlySpeed(1.0f);
  CHECK(std::fabs(camera.flySpeed() - ViewportCameraModel::kDefaultFlySpeed * 1.25f) < 1e-5f);
  camera.scaleFlySpeed(-1000.0f);
  CHECK(camera.flySpeed() == ViewportCameraModel::kMinFlySpeed);
}

TEST_CASE("framing puts the center at the pivot and fits the sphere to the vertical field of view",
          "[editor][model][camera]") {
  ViewportCameraModel camera = cameraAt({0.0f, 0.0f, 10.0f});
  camera.look(40.0f, 25.0f);
  const Vec3 center{3.0f, -1.0f, 2.0f};
  camera.frame(center, 2.0f);
  CHECK(distance(camera.pivot(), center) < 1e-4f);
  CHECK(std::fabs(camera.pivotDistance() * std::sin(camera.lens().fovYRadians * 0.5f) - 2.0f) < 1e-4f);
  const auto matrices = camera.matrices(1.0f);
  REQUIRE(matrices.has_value());
  const auto ndc = atlantis::editor::projectToNdc(*matrices, center);
  REQUIRE(ndc.has_value());
  CHECK(std::fabs((*ndc)[0]) < 1e-4f);
  CHECK(std::fabs((*ndc)[1]) < 1e-4f);
  const float kept = camera.pivotDistance();
  camera.frame({0.0f, 0.0f, 0.0f}, 0.0f);  // no bounds: the distance is kept
  CHECK(camera.pivotDistance() == kept);
}

TEST_CASE("the ray through a pixel passes through the point that projected there", "[editor][model][camera]") {
  ViewportCameraModel camera = cameraAt({1.0f, 2.0f, 9.0f});
  camera.look(-50.0f, 30.0f);
  const auto matrices = camera.matrices(1.6f);
  REQUIRE(matrices.has_value());
  const Vec3 point{0.5f, 0.3f, -2.0f};
  const auto ndc = atlantis::editor::projectToNdc(*matrices, point);
  REQUIRE(ndc.has_value());
  const auto ray = atlantis::editor::rayThroughNdc(*matrices, (*ndc)[0], (*ndc)[1]);
  CHECK(distance(ray.origin, camera.eye()) < 1e-4f);
  const float t = distance(ray.origin, point);
  const Vec3 along{ray.origin[0] + ray.direction[0] * t, ray.origin[1] + ray.direction[1] * t,
                   ray.origin[2] + ray.direction[2] * t};
  CHECK(distance(along, point) < 1e-3f);
  const Vec3 f = camera.forward();
  const Vec3& e = camera.eye();
  const float alongForward = (point[0] - e[0]) * f[0] + (point[1] - e[1]) * f[1] + (point[2] - e[2]) * f[2];
  CHECK(std::fabs(atlantis::editor::viewDepth(*matrices, point) - alongForward) < 1e-4f);
}

// --- Viewport picking, markers, selection sync, the camera state and the Gizmo's
// space and snapping (Plan 0062 M4; Spec 0062 R1, R4-R7) -----------------------------

namespace {

using atlantis::editor::CameraLens;
using atlantis::editor::Editor;
using atlantis::editor::FrameContext;
using atlantis::editor::GizmoSpace;
using atlantis::editor::Marker;
using atlantis::editor::MarkerKind;
using atlantis::editor::PickAnswer;
using atlantis::editor::PixelRect;
using atlantis::editor::SelectionBounds;
using atlantis::editor::ViewportCameraMode;
using atlantis::editor::ViewportCameraState;
using atlantis::editor::ViewportPicking;

const EntityGuid kPoint = guid("52052052-0003-4052-8052-000000000003");

// A scene camera at (0, 0, 10) looking down -z.
[[nodiscard]] CameraPose sceneCameraAt10() {
  CameraPose pose;
  pose.worldMatrix[14] = 10.0f;
  pose.lens = CameraLens{1.0472f, 0.1f, 100.0f};
  return pose;
}

[[nodiscard]] atlantis::editor::CameraMatrices matricesOf(const CameraPose& pose, float aspect) {
  const auto matrices = atlantis::editor::cameraMatrices(pose, aspect);
  REQUIRE(matrices.has_value());
  return *matrices;
}

}  // namespace

TEST_CASE("a marker is picked within its radius, the nearest to the camera first", "[editor][model][picking]") {
  const PixelRect rect{100.0f, 50.0f, 800.0f, 400.0f};
  const auto camera = matricesOf(sceneCameraAt10(), 2.0f);
  const std::vector<Marker> markers{Marker{kSun, MarkerKind::Light, {0.0f, 0.0f, 0.0f}},
                                    Marker{kCamera, MarkerKind::Camera, {0.0f, 0.0f, 5.0f}},  // nearer, same pixel
                                    Marker{kPoint, MarkerKind::Light, {0.0f, 0.0f, 20.0f}}};  // behind the camera
  const auto at = atlantis::editor::pixelOf(camera, rect, {0.0f, 0.0f, 0.0f});
  REQUIRE(at.has_value());
  CHECK(atlantis::editor::pickMarker(markers, camera, rect, (*at)[0] + 6.0f, (*at)[1] + 6.0f) == kCamera);
  CHECK_FALSE(atlantis::editor::pickMarker(markers, camera, rect, (*at)[0] + 8.0f, (*at)[1] + 8.0f).has_value());
  CHECK(atlantis::editor::pickMarker(std::span<const Marker>(markers).first(1), camera, rect, (*at)[0], (*at)[1]) ==
        kSun);
}

TEST_CASE("markers are the Light and Camera entities without a Renderable, at their WorldMatrix translation",
          "[editor][model][picking]") {
  EditorFixture fixture;
  HierarchyModel hierarchy(*fixture.connection);
  hierarchy.update();
  ViewportPicking picking(*fixture.connection);
  picking.updateMarkers(hierarchy);
  std::set<std::string> found;
  for (const Marker& marker : picking.markers()) {
    found.insert(atlantis::asset_system::toString(marker.entity));
    const MatrixColumns m = readMatrix(*fixture.connection, marker.entity);
    CHECK(marker.position == Vec3{m[3][0], m[3][1], m[3][2]});
    CHECK(marker.kind == (marker.entity == kCamera ? MarkerKind::Camera : MarkerKind::Light));
  }
  CHECK(found == std::set<std::string>{atlantis::asset_system::toString(kSun), atlantis::asset_system::toString(kPoint),
                                       atlantis::asset_system::toString(kCamera)});
}

TEST_CASE("a click requests a geometry pick; its answer selects, a miss clears, a stale answer is dropped",
          "[editor][model][picking]") {
  EditorFixture fixture;
  HierarchyModel hierarchy(*fixture.connection);
  hierarchy.update();
  ViewportPicking picking(*fixture.connection);
  picking.updateMarkers(hierarchy);
  const PixelRect rect{0.0f, 0.0f, 640.0f, 480.0f};
  const auto camera = matricesOf(sceneCameraAt10(), 640.0f / 480.0f);

  // Away from every marker: a request whose ray goes through the clicked pixel.
  picking.click(hierarchy, camera, rect, 600.0f, 40.0f);
  REQUIRE(picking.request().has_value());
  const auto first = *picking.request();
  CHECK(first.ray == atlantis::editor::rayThroughPixel(camera, rect, 600.0f, 40.0f));
  CHECK(picking.answer(hierarchy, PickAnswer{first.id, kMesh, 4.0f}));
  CHECK(hierarchy.selection() == kMesh);

  // A miss clears the selection.
  picking.click(hierarchy, camera, rect, 600.0f, 40.0f);
  CHECK(picking.answer(hierarchy, PickAnswer{picking.request()->id, std::nullopt, 0.0f}));
  CHECK_FALSE(hierarchy.selection().has_value());

  // A newer request supersedes an older one; the older answer is dropped.
  picking.click(hierarchy, camera, rect, 600.0f, 40.0f);
  const std::uint64_t older = picking.request()->id;
  picking.click(hierarchy, camera, rect, 610.0f, 40.0f);
  CHECK_FALSE(picking.answer(hierarchy, PickAnswer{older, kMesh, 1.0f}));
  CHECK_FALSE(hierarchy.selection().has_value());

  // A selection made after the request wins over its answer.
  const std::uint64_t latest = picking.request()->id;
  hierarchy.select(kSun);
  CHECK_FALSE(picking.answer(hierarchy, PickAnswer{latest, kMesh, 1.0f}));
  CHECK(hierarchy.selection() == kSun);

  // A click on a marker selects at once, with no request.
  picking.clearRequest();
  const MatrixColumns cameraMatrix = readMatrix(*fixture.connection, kCamera);
  const auto cameraMarker =
      atlantis::editor::pixelOf(camera, rect, Vec3{cameraMatrix[3][0], cameraMatrix[3][1], cameraMatrix[3][2]});
  REQUIRE(cameraMarker.has_value());
  picking.click(hierarchy, camera, rect, (*cameraMarker)[0], (*cameraMarker)[1]);
  CHECK(hierarchy.selection() == kCamera);
  CHECK_FALSE(picking.request().has_value());
  // Hidden markers are not picked: the click becomes a request.
  picking.click(hierarchy, camera, rect, (*cameraMarker)[0], (*cameraMarker)[1], /*withMarkers=*/false);
  CHECK(picking.request().has_value());
}

TEST_CASE("the editor takes up a pick answer and the selection's bounds, and asks the host for the next frame",
          "[editor][model][picking]") {
  EditorFixture fixture;
  Editor& editor = *fixture.editor;
  FrameContext context{1280, 720};
  context.sceneCamera = sceneCameraAt10();
  editor.frame({}, context);
  // Ruling Q6 (D-a): the editor camera, started from the scene camera exactly.
  CHECK(editor.camera().mode() == ViewportCameraMode::Editor);
  CHECK(editor.viewportCamera() == context.sceneCamera);
  CHECK_FALSE(editor.pickRequest().has_value());
  CHECK_FALSE(editor.boundsSubject().has_value());

  const PixelRect rect{0.0f, 0.0f, 640.0f, 480.0f};
  editor.picking().click(editor.hierarchy(), matricesOf(*context.sceneCamera, 640.0f / 480.0f), rect, 600.0f, 40.0f);
  REQUIRE(editor.pickRequest().has_value());
  context.pickAnswer = PickAnswer{editor.pickRequest()->id, kMesh, 3.0f};
  editor.frame({}, context);
  CHECK(editor.hierarchy().selection() == kMesh);
  CHECK(editor.inspector().subject() == kMesh);
  CHECK(editor.gizmo().subject() == kMesh);
  CHECK_FALSE(editor.pickRequest().has_value());  // taken by the host, cleared by the next frame
  CHECK(editor.boundsSubject() == kMesh);

  // Destroying the selection clears it, and the subjects follow.
  context.pickAnswer.reset();
  (void)fixture.connection->submit(access::DestroyEntity{kMesh});
  fixture.runtime.frame();
  editor.frame({}, context);
  CHECK_FALSE(editor.hierarchy().selection().has_value());
  CHECK_FALSE(editor.inspector().subject().has_value());
  CHECK_FALSE(editor.gizmo().subject().has_value());
  CHECK_FALSE(editor.boundsSubject().has_value());
}

TEST_CASE("the Viewport camera: Scene mode hands the host nothing; navigating from it switches to the editor camera",
          "[editor][model][camera]") {
  ViewportCameraState state;
  CHECK_FALSE(state.pose().has_value());
  CHECK_FALSE(state.override().has_value());  // nothing to start from yet
  state.setScenePose(sceneCameraAt10());
  CHECK(state.editorInitialized());
  CHECK(state.override() == sceneCameraAt10());
  state.setMode(ViewportCameraMode::Scene);
  CHECK_FALSE(state.override().has_value());
  CHECK(state.pose() == sceneCameraAt10());
  state.beginNavigation();
  CHECK(state.mode() == ViewportCameraMode::Editor);
  state.editor().orbit(40.0f, 0.0f);
  CHECK_FALSE(state.override() == sceneCameraAt10());
  // A moved scene camera does not move the editor camera; Align does.
  CameraPose moved = sceneCameraAt10();
  moved.worldMatrix[12] = 3.0f;
  state.setScenePose(moved);
  CHECK_FALSE(state.override() == moved);
  state.align();
  CHECK(state.override() == moved);
}

TEST_CASE("Local translate and rotate use the entity's own axes; World is unchanged", "[editor][model][gizmo]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  GizmoModel gizmo(c);
  gizmo.setSubject(kMesh);  // rotated (0.1, 0.2, 0.3), scaled (1, 2, 1)
  REQUIRE(gizmo.available());
  const MatrixColumns start = *gizmo.matrix();
  const auto trs = decompose(start);
  REQUIRE(trs.has_value());

  gizmo.setSpace(GizmoSpace::Local);
  const auto axisY = gizmo.axisDirection(GizmoAxis::Y);
  for (std::size_t i = 0; i < 3; ++i) CHECK(std::fabs(axisY[i] - trs->rotation[3 + i]) < 1e-6f);
  gizmo.beginDrag(GizmoAxis::Y);
  gizmo.dragTo(2.0f);
  for (std::size_t i = 0; i < 3; ++i) {
    CHECK(std::fabs((*gizmo.matrix())[3][i] - (start[3][i] + 2.0f * axisY[i])) < 1e-5f);
  }
  for (std::size_t k = 0; k < 3; ++k) CHECK((*gizmo.matrix())[k] == start[k]);
  gizmo.endDrag();
  (void)gizmo.flush();
  fixture.runtime.frame();
  gizmo.update();

  // Local rotate about the entity's z: its z axis stays put, its x moves.
  gizmo.setMode(GizmoMode::Rotate);
  const auto axisZ = gizmo.axisDirection(GizmoAxis::Z);
  gizmo.beginDrag(GizmoAxis::Z);
  gizmo.dragTo(0.7f);
  const auto rotated = decompose(*gizmo.matrix());
  REQUIRE(rotated.has_value());
  for (std::size_t i = 0; i < 3; ++i) CHECK(std::fabs(rotated->rotation[6 + i] - axisZ[i]) < 1e-5f);
  CHECK(std::fabs(rotated->rotation[0] - trs->rotation[0]) > 1e-3f);
  gizmo.endDrag();

  // World space keeps world axes for Translate and Rotate; Scale is always local.
  gizmo.setSpace(GizmoSpace::World);
  CHECK(gizmo.axisDirection(GizmoAxis::X) == std::array<float, 3>{1.0f, 0.0f, 0.0f});
  gizmo.setMode(GizmoMode::Scale);
  CHECK(std::fabs(gizmo.axisDirection(GizmoAxis::X)[0] - 1.0f) > 1e-4f);
}

TEST_CASE("snapping rounds the drag amount to its step, still one transaction per frame", "[editor][model][gizmo]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  GizmoModel gizmo(c);
  gizmo.setSubject(kMesh);
  REQUIRE(gizmo.available());
  const MatrixColumns start = *gizmo.matrix();
  Changes changes(c, kMesh);
  gizmo.setSnap(true);
  gizmo.beginDrag(GizmoAxis::X);
  gizmo.dragTo(0.74f);
  CHECK((*gizmo.matrix())[3][0] == start[3][0] + 0.5f);
  gizmo.dragTo(0.76f);
  CHECK((*gizmo.matrix())[3][0] == start[3][0] + 1.0f);
  REQUIRE(gizmo.flush().has_value());
  gizmo.endDrag();
  REQUIRE(gizmo.flush().has_value());  // the release's final value
  CHECK_FALSE(gizmo.flush().has_value());
  fixture.runtime.frame();
  CHECK(changes.drain().size() == 8);  // two four-column transactions
  gizmo.update();

  // Rotate about world y by one 15 degree step: the x axis turns by exactly that.
  gizmo.setMode(GizmoMode::Rotate);
  const auto before = decompose(*gizmo.matrix());
  REQUIRE(before.has_value());
  gizmo.beginDrag(GizmoAxis::Y);
  gizmo.dragTo(0.30f);  // 17.2 degrees -> 15
  const auto after = decompose(*gizmo.matrix());
  gizmo.endDrag();
  REQUIRE(after.has_value());
  // The x axis turns about world y; its angle in the xz plane changes by the step.
  const float beforeAngle = std::atan2(before->rotation[2], before->rotation[0]);
  const float afterAngle = std::atan2(after->rotation[2], after->rotation[0]);
  CHECK(std::fabs(std::fabs(afterAngle - beforeAngle) - atlantis::editor::kRotateSnapRadians) < 1e-4f);
  CHECK(std::fabs(after->rotation[1] - before->rotation[1]) < 1e-5f);  // its height is unchanged

  gizmo.setMode(GizmoMode::Scale);
  const auto scaleBefore = decompose(*gizmo.matrix());
  REQUIRE(scaleBefore.has_value());
  gizmo.beginDrag(GizmoAxis::Z);
  gizmo.dragTo(1.27f);
  CHECK(std::fabs(decompose(*gizmo.matrix())->scale[2] - scaleBefore->scale[2] * 1.3f) < 1e-4f);
  gizmo.dragTo(0.01f);  // never below one step
  CHECK(std::fabs(decompose(*gizmo.matrix())->scale[2] - scaleBefore->scale[2] * 0.1f) < 1e-4f);
  gizmo.endDrag();
}

// --- The edit history over a scripted edit target (Plan 0063 M1; Spec 0063 R1,
// R3-R6, R8): no connection -- the history needs only the EditTarget interface.

namespace {

using atlantis::editor::AddressedValue;
using atlantis::editor::EditHandle;
using atlantis::editor::EditHistory;
using atlantis::editor::EditTarget;
using atlantis::editor::EditTransaction;
using atlantis::editor::HistoryResult;
using atlantis::editor::Invalidation;
using atlantis::editor::PropertyWrite;
using atlantis::editor::Settlement;
using SceneAddress = atlantis::asset_system::scene::PropertyAddress;

// A world of addressed values whose transactions settle when the test says.
class ScriptedTarget final : public EditTarget {
 public:
  std::optional<access::PropertyValue> read(const SceneAddress& address) override {
    const auto it = values.find(address);
    if (it == values.end()) return std::nullopt;
    return it->second;
  }
  EditHandle submit(std::span<const AddressedValue> writes) override {
    const EditHandle handle{next++, 1};
    submitted.emplace_back(writes.begin(), writes.end());
    track(handle, writes);
    return handle;
  }
  void track(EditHandle handle, std::span<const AddressedValue> writes) override {
    tracked.push_back({handle, std::vector<AddressedValue>(writes.begin(), writes.end()), Settlement::Pending});
  }
  Settlement settlement(EditHandle handle) const override {
    for (const auto& t : tracked) {
      if (t.handle == handle) return t.settlement;
    }
    return Settlement::Pending;
  }
  std::string refusalText(EditHandle) const override { return "scripted refusal"; }
  void update(std::span<const access::CommandFailure>) override {}
  std::vector<Invalidation> drainInvalidations() override { return std::exchange(invalidations, {}); }

  // The world applies (writing its values) or refuses the tracked handle.
  void apply(EditHandle handle) {
    for (auto& t : tracked) {
      if (t.handle != handle) continue;
      for (const auto& [address, value] : t.writes) values[address] = value;
      t.settlement = Settlement::Applied;
    }
  }
  void refuse(EditHandle handle) {
    for (auto& t : tracked) {
      if (t.handle == handle) t.settlement = Settlement::Refused;
    }
  }
  [[nodiscard]] EditHandle last() const { return tracked.back().handle; }

  struct Tracked {
    EditHandle handle;
    std::vector<AddressedValue> writes;
    Settlement settlement;
  };
  std::map<SceneAddress, access::PropertyValue> values;
  std::vector<Tracked> tracked;
  std::vector<std::vector<AddressedValue>> submitted;
  std::vector<Invalidation> invalidations;
  std::uint64_t next = 1000;
};

[[nodiscard]] SceneAddress addressOf(const EntityGuid& entity, std::uint64_t component, std::uint64_t field) {
  return SceneAddress{entity, atlantis::schema::TypeId{component}, atlantis::schema::FieldId{field}};
}

// An edit of `count` float writes on `entity`, from `from` to `to`, made the
// way the Inspector or the Gizmo makes one: written to the scripted world
// directly, then recorded with its handle.
EditHandle makeEdit(ScriptedTarget& target, EditHistory& history, const EntityGuid& entity, float from, float to,
                    std::size_t count = 1, std::string label = "edit") {
  EditTransaction edit{std::move(label), {}};
  for (std::size_t i = 0; i < count; ++i) {
    const SceneAddress address = addressOf(entity, 7, 100 + i);
    target.values[address] = from;
    edit.writes.push_back(PropertyWrite{address, from, to});
  }
  const EditHandle handle{target.next++, count};
  history.record(edit, handle);
  return handle;
}

}  // namespace

TEST_CASE("an edit is undoable once applied; undo and redo write before and after as one submission each",
          "[editor][model][history]") {
  ScriptedTarget target;
  EditHistory history(target);
  const EditHandle handle = makeEdit(target, history, kMesh, 1.0f, 2.0f, 4, "Move 52052052");
  CHECK(history.waiting());
  CHECK_FALSE(history.canUndo());
  CHECK(history.undo() == HistoryResult::Waiting);
  target.apply(handle);
  history.update({});
  REQUIRE(history.canUndo());
  CHECK(history.undoLabel() == "Move 52052052");

  REQUIRE(history.undo() == HistoryResult::Submitted);
  REQUIRE(target.submitted.size() == 1);
  REQUIRE(target.submitted[0].size() == 4);
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(target.submitted[0][i].first == addressOf(kMesh, 7, 100 + i));  // in order
    CHECK(target.submitted[0][i].second == access::PropertyValue{1.0f});  // before
  }
  CHECK(history.redo() == HistoryResult::Waiting);  // the undo is pending
  target.apply(target.last());
  history.update({});
  CHECK(history.undoDepth() == 0);
  REQUIRE(history.canRedo());

  REQUIRE(history.redo() == HistoryResult::Submitted);
  for (const auto& [address, value] : target.submitted[1]) CHECK(value == access::PropertyValue{2.0f});  // after
  target.apply(target.last());
  history.update({});
  CHECK(history.undoDepth() == 1);
  CHECK(history.redoDepth() == 0);
  CHECK(history.undo() == HistoryResult::Submitted);
}

TEST_CASE("a refused edit is not kept; nothing to undo is reported", "[editor][model][history]") {
  ScriptedTarget target;
  EditHistory history(target);
  CHECK(history.undo() == HistoryResult::NothingTo);
  const EditHandle handle = makeEdit(target, history, kMesh, 1.0f, 2.0f);
  target.refuse(handle);
  history.update({});
  CHECK_FALSE(history.waiting());
  CHECK(history.undoDepth() == 0);
  CHECK(history.notice().find("refused") != std::string::npos);
}

TEST_CASE("a new edit clears redo; the history is at most 128 deep; clear() forgets everything",
          "[editor][model][history]") {
  ScriptedTarget target;
  EditHistory history(target);
  target.apply(makeEdit(target, history, kMesh, 1.0f, 2.0f));
  history.update({});
  REQUIRE(history.undo() == HistoryResult::Submitted);
  target.apply(target.last());
  history.update({});
  REQUIRE(history.redoDepth() == 1);
  target.apply(makeEdit(target, history, kSun, 3.0f, 4.0f));
  CHECK(history.redoDepth() == 0);
  history.update({});

  for (int i = 0; i < 200; ++i) {
    target.apply(makeEdit(target, history, kMesh, static_cast<float>(i), static_cast<float>(i + 1)));
    history.update({});
  }
  CHECK(history.undoDepth() == EditHistory::kMaxDepth);
  history.clear();
  CHECK(history.undoDepth() == 0);
  CHECK_FALSE(history.waiting());
}

TEST_CASE("an undo overtaken by a new edit is not redoable once it applies", "[editor][model][history]") {
  ScriptedTarget target;
  EditHistory history(target);
  target.apply(makeEdit(target, history, kMesh, 1.0f, 2.0f));
  history.update({});
  REQUIRE(history.undo() == HistoryResult::Submitted);
  const EditHandle undo = target.last();
  target.apply(makeEdit(target, history, kSun, 3.0f, 4.0f));  // a new edit while the undo is pending
  target.apply(undo);
  history.update({});
  CHECK(history.redoDepth() == 0);
  CHECK(history.undoDepth() == 1);  // the new edit
}

TEST_CASE("a value changed elsewhere refuses the step, submits nothing and drops the entry",
          "[editor][model][history]") {
  ScriptedTarget target;
  EditHistory history(target);
  target.apply(makeEdit(target, history, kMesh, 1.0f, 2.0f, 2, "first"));
  history.update({});
  target.apply(makeEdit(target, history, kSun, 3.0f, 4.0f, 1, "second"));
  history.update({});
  target.values[addressOf(kSun, 7, 100)] = 5.0f;  // another writer
  CHECK(history.undo() == HistoryResult::ChangedElsewhere);
  CHECK(target.submitted.empty());
  CHECK(history.notice().find("changed elsewhere") != std::string::npos);
  CHECK(history.undoLabel() == "first");  // the next one is still undoable
  REQUIRE(history.undo() == HistoryResult::Submitted);

  // Redo checks `before` the same way.
  target.apply(target.last());
  history.update({});
  target.values[addressOf(kMesh, 7, 101)] = 9.0f;
  CHECK(history.redo() == HistoryResult::ChangedElsewhere);
  CHECK(history.redoDepth() == 0);
}

TEST_CASE("a destroyed entity or removed component drops the entries naming it, even if the GUID returns",
          "[editor][model][history]") {
  ScriptedTarget target;
  EditHistory history(target);
  target.apply(makeEdit(target, history, kMesh, 1.0f, 2.0f, 1, "mesh"));
  history.update({});
  target.apply(makeEdit(target, history, kSun, 3.0f, 4.0f, 1, "sun"));
  history.update({});
  target.apply(makeEdit(target, history, kCamera, 5.0f, 6.0f, 1, "camera"));
  history.update({});

  // The mesh is destroyed and its GUID re-created: its entry stays dropped.
  target.invalidations.push_back(Invalidation{kMesh, std::nullopt});
  history.update({});
  CHECK(history.undoDepth() == 2);
  // A different component of the sun is removed: its entry stays; the edited one goes.
  target.invalidations.push_back(Invalidation{kSun, atlantis::schema::TypeId{8}});
  history.update({});
  CHECK(history.undoDepth() == 2);
  target.invalidations.push_back(Invalidation{kSun, atlantis::schema::TypeId{7}});
  history.update({});
  CHECK(history.undoDepth() == 1);
  CHECK(history.undoLabel() == "camera");

  // An unreadable address at undo time drops the entry too.
  target.values.erase(addressOf(kCamera, 7, 100));
  CHECK(history.undo() == HistoryResult::Invalidated);
  CHECK(history.undoDepth() == 0);

  // An invalidation that arrives while an edit is pending drops it when it settles.
  const EditHandle pending = makeEdit(target, history, kPoint, 1.0f, 2.0f, 1, "pending");
  target.invalidations.push_back(Invalidation{kPoint, std::nullopt});
  target.apply(pending);
  history.update({});
  CHECK(history.undoDepth() == 0);
}

// --- The Runtime World edit target (Plan 0063 M2; Spec 0063 R3, R6, R8) over the
// InProcess fixture, with the fake Runtime's frames. ------------------------------

namespace {

using atlantis::editor::RuntimeWorldEditTarget;

// The Spec 0049 address of `path` ("Light.intensity") on `entity`.
[[nodiscard]] SceneAddress sceneAddress(connection::RuntimeConnection& c, const EntityGuid& entity,
                                        std::string_view path) {
  auto resolved = text::parsePath(c.schema(), path);
  REQUIRE(resolved.isOk());
  return SceneAddress{entity, resolved.value().component, resolved.value().leaf->id};
}

// One editor frame of the target: this frame's failures, then its events.
void settle(RuntimeWorldEditTarget& target, connection::RuntimeConnection& c) {
  const auto failures = c.drainFailures();
  target.update(failures);
}

}  // namespace

TEST_CASE("the Runtime World target settles a transaction applied once all its writes' events arrive in order",
          "[editor][model][history][target]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  RuntimeWorldEditTarget target(c);
  const SceneAddress intensity = sceneAddress(c, kSun, "Light.intensity");
  const SceneAddress color = sceneAddress(c, kSun, "Light.color");
  CHECK(target.read(intensity) == std::optional<access::PropertyValue>{1.2f});

  const std::vector<AddressedValue> writes{{intensity, 6.0f}, {color, std::array<float, 3>{1.0f, 0.5f, 0.25f}}};
  const EditHandle handle = target.submit(writes);
  CHECK(handle.count == 2);
  settle(target, c);
  CHECK(target.settlement(handle) == Settlement::Pending);  // not applied before the Runtime's frame
  fixture.runtime.frame();
  settle(target, c);
  CHECK(target.settlement(handle) == Settlement::Applied);
  CHECK(target.read(intensity) == std::optional<access::PropertyValue>{6.0f});
  settle(target, c);
  CHECK(target.settlement(handle) == Settlement::Pending);  // forgotten once reported

  // A one-command edit made elsewhere, tracked as a transaction of one (J2).
  const auto ticket = c.submit(access::SetProperty{{kSun, intensity.component, intensity.field}, 3.0f});
  const EditHandle one = RuntimeWorldEditTarget::handleOf(ticket);
  const std::vector<AddressedValue> oneWrite{{intensity, 3.0f}};
  target.track(one, oneWrite);
  fixture.runtime.frame();
  settle(target, c);
  CHECK(target.settlement(one) == Settlement::Applied);
}

TEST_CASE("the Runtime World target reports a refusal, and stays pending while the Runtime is paused",
          "[editor][model][history][target]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  RuntimeWorldEditTarget target(c);
  const SceneAddress intensity = sceneAddress(c, kSun, "Light.intensity");
  const SceneAddress missing{guid("99999999-0000-4099-8099-000000000099"), intensity.component, intensity.field};
  const std::vector<AddressedValue> bad{{intensity, 2.0f}, {missing, 2.0f}};
  const EditHandle refused = target.submit(bad);
  fixture.runtime.frame();
  settle(target, c);
  CHECK(target.settlement(refused) == Settlement::Refused);
  CHECK_FALSE(target.refusalText(refused).empty());
  CHECK(target.read(intensity) == std::optional<access::PropertyValue>{1.2f});  // all or nothing

  fixture.runtime.pause();
  const std::vector<AddressedValue> good{{intensity, 2.0f}};
  const EditHandle held = target.submit(good);
  for (int i = 0; i < 3; ++i) {
    fixture.runtime.frame();
    settle(target, c);
    CHECK(target.settlement(held) == Settlement::Pending);
  }
  fixture.runtime.step(connection::StepRequest{1, std::nullopt}, [](auto) {});
  fixture.runtime.frame();
  settle(target, c);
  CHECK(target.settlement(held) == Settlement::Applied);
}

TEST_CASE("another writer's matching write does not settle a transaction out of order",
          "[editor][model][history][target]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  auto other = fixture.endpoint.open();
  RuntimeWorldEditTarget target(c);
  const SceneAddress intensity = sceneAddress(c, kSun, "Light.intensity");
  const SceneAddress range = sceneAddress(c, kPoint, "Light.range");
  // Expecting intensity = 7 then range = 4: another client's range = 4 alone settles nothing.
  const EditHandle expected{424242, 2};
  const std::vector<AddressedValue> writes{{intensity, 7.0f}, {range, 4.0f}};
  target.track(expected, writes);
  (void)other->submit(access::SetProperty{{kPoint, range.component, range.field}, 4.0f});
  fixture.runtime.frame();
  settle(target, c);
  CHECK(target.settlement(expected) == Settlement::Pending);
}

TEST_CASE("the Runtime World target reports destroyed entities and removed components; an unreadable address is empty",
          "[editor][model][history][target]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  RuntimeWorldEditTarget target(c);
  const auto light = typeNamed(c, "Light");
  (void)c.submit(access::RemoveComponent{kPoint, light});
  (void)c.submit(access::DestroyEntity{kSun});
  fixture.runtime.frame();
  settle(target, c);
  const auto invalidations = target.drainInvalidations();
  REQUIRE(invalidations.size() == 2);
  CHECK(invalidations[0] == Invalidation{kPoint, light});
  CHECK(invalidations[1] == Invalidation{kSun, std::nullopt});
  CHECK(target.drainInvalidations().empty());
  CHECK_FALSE(target.read(sceneAddress(c, kPoint, "Light.range")).has_value());
}

// --- Recording through the Editor (Plan 0063 M3; Spec 0063 R2, R4, R5): the
// Gizmo's drag and the Inspector's commit as Editor Transactions, undone and redone
// through the editor's own frames. -----------------------------------------------

namespace {

// One round of the host's loop: the Runtime's frame, then the editor's.
void round(EditorFixture& fixture, int count = 1) {
  for (int i = 0; i < count; ++i) {
    fixture.runtime.frame();
    fixture.editor->frame({}, FrameContext{1280, 720});
  }
}

void selectEntity(EditorFixture& fixture, const EntityGuid& entity) {
  round(fixture);  // the Hierarchy's first listing
  fixture.editor->hierarchy().select(entity);
  round(fixture);
  REQUIRE(fixture.editor->gizmo().subject() == entity);
}

}  // namespace

TEST_CASE("a 30-update Gizmo drag is one Editor Transaction of four writes; its previews are not recorded",
          "[editor][model][history][recording]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  selectEntity(fixture, kMesh);
  auto& gizmo = fixture.editor->gizmo();
  auto& history = fixture.editor->history();
  const MatrixColumns start = readMatrix(c, kMesh);
  Changes changes(c, kMesh);
  gizmo.beginDrag(GizmoAxis::X);
  for (int i = 1; i <= 30; ++i) {
    gizmo.dragTo(0.1f * static_cast<float>(i));
    round(fixture);  // the editor's frame flushes the preview
    CHECK_FALSE(history.waiting());
    CHECK(history.undoDepth() == 0);
  }
  gizmo.endDrag();
  round(fixture, 3);  // the release flush, its application, its settlement
  const MatrixColumns dropped = readMatrix(c, kMesh);
  CHECK(dropped[3][0] == start[3][0] + 3.0f);
  CHECK(changes.drain().size() == 31 * 4);  // 30 previews and the release
  REQUIRE(history.undoDepth() == 1);
  CHECK(history.undoLabel() == "Move 52052052");

  // Undo: one transaction back to the drag's start, bit for bit; redo: the drop.
  REQUIRE(history.undo() == HistoryResult::Submitted);
  round(fixture, 2);
  CHECK(readMatrix(c, kMesh) == start);
  CHECK(changes.drain().size() == 4);
  CHECK(history.redoDepth() == 1);
  REQUIRE(history.redo() == HistoryResult::Submitted);
  round(fixture, 2);
  CHECK(readMatrix(c, kMesh) == dropped);
}

TEST_CASE("an unchanged drag, or one refused at release, records nothing", "[editor][model][history][recording]") {
  EditorFixture fixture;
  selectEntity(fixture, kMesh);
  auto& gizmo = fixture.editor->gizmo();
  auto& history = fixture.editor->history();
  gizmo.beginDrag(GizmoAxis::Y);
  gizmo.dragTo(0.5f);
  round(fixture);
  gizmo.dragTo(0.0f);  // back where it began
  gizmo.endDrag();
  round(fixture, 3);
  CHECK(history.undoDepth() == 0);
  CHECK_FALSE(history.waiting());

  // The entity is destroyed by another client mid-drag: the release is refused.
  auto other = fixture.endpoint.open();
  gizmo.beginDrag(GizmoAxis::Y);
  gizmo.dragTo(0.5f);
  round(fixture);
  (void)other->submit(access::DestroyEntity{kMesh});
  fixture.runtime.frame();
  gizmo.endDrag();
  (void)gizmo.flush();
  round(fixture, 3);
  CHECK(history.undoDepth() == 0);
  CHECK_FALSE(history.waiting());
}

TEST_CASE("an Inspector commit is one Editor Transaction with the shown value as before; undo shows it again",
          "[editor][model][history][recording]") {
  EditorFixture fixture;
  selectEntity(fixture, kSun);
  auto& inspector = fixture.editor->inspector();
  auto& history = fixture.editor->history();
  const std::size_t intensity = fieldIndex(inspector, "Light.intensity");
  REQUIRE(inspector.fields()[intensity].value == access::PropertyValue{1.2f});
  (void)inspector.commit(intensity, 6.0f);
  round(fixture, 2);
  REQUIRE(history.undoDepth() == 1);
  CHECK(history.undoLabel() == "Set Light.intensity");
  CHECK(inspector.fields()[intensity].value == access::PropertyValue{6.0f});

  REQUIRE(history.undo() == HistoryResult::Submitted);
  round(fixture, 2);
  CHECK(inspector.fields()[intensity].value == access::PropertyValue{1.2f});

  // A new edit clears redo.
  REQUIRE(history.redoDepth() == 1);
  (void)inspector.commit(intensity, 4.0f);
  round(fixture, 2);
  CHECK(history.redoDepth() == 0);
  CHECK(history.undoDepth() == 1);
}

TEST_CASE("clearing an Optional field records Absent as after; undo restores the value", "[editor][model][history][recording]") {
  EditorFixture fixture;
  selectEntity(fixture, kMesh);
  auto& inspector = fixture.editor->inspector();
  auto& history = fixture.editor->history();
  const std::size_t material = fieldIndex(inspector, "Renderable.materialAsset");
  REQUIRE(inspector.fields()[material].optional);
  const auto before = inspector.fields()[material].value;
  REQUIRE(before.has_value());
  (void)inspector.commit(material, access::Absent{});
  round(fixture, 2);
  REQUIRE(history.undoDepth() == 1);
  CHECK(inspector.fields()[material].value == access::PropertyValue{access::Absent{}});
  REQUIRE(history.undo() == HistoryResult::Submitted);
  round(fixture, 2);
  CHECK(inspector.fields()[material].value == before);
}

TEST_CASE("another client's later write refuses the undo; a scene change clears the history",
          "[editor][model][history][recording]") {
  EditorFixture fixture;
  selectEntity(fixture, kSun);
  auto& inspector = fixture.editor->inspector();
  auto& history = fixture.editor->history();
  const std::size_t intensity = fieldIndex(inspector, "Light.intensity");
  (void)inspector.commit(intensity, 6.0f);
  round(fixture, 2);
  REQUIRE(history.canUndo());

  auto other = fixture.endpoint.open();
  (void)other->submit(access::SetProperty{{kSun, inspector.fields()[intensity].component, inspector.fields()[intensity].field}, 5.0f});
  round(fixture);
  CHECK(history.undo() == HistoryResult::ChangedElsewhere);
  round(fixture);
  CHECK(inspector.fields()[intensity].value == access::PropertyValue{5.0f});
  CHECK(history.undoDepth() == 0);

  (void)inspector.commit(intensity, 7.0f);
  round(fixture, 2);
  REQUIRE(history.undoDepth() == 1);
  fixture.runtime.scene = atlantis::asset_system::parseAssetGuid("00630063-0063-4063-8063-006300630063").value();
  round(fixture);
  CHECK(history.undoDepth() == 0);
}

// Plan 0065 M4 (Spec 0065 R10; ADR-0131 D5): a reload of the same scene
// changes its generation, and the history clears as on a scene change.
TEST_CASE("a scene reload (a new generation of the same scene) clears the history", "[editor][model][history]") {
  EditorFixture fixture;
  selectEntity(fixture, kSun);
  auto& inspector = fixture.editor->inspector();
  auto& history = fixture.editor->history();
  (void)inspector.commit(fieldIndex(inspector, "Light.intensity"), 6.0f);
  round(fixture, 2);
  REQUIRE(history.undoDepth() == 1);
  round(fixture);
  CHECK(history.undoDepth() == 1);  // the same generation: kept
  fixture.runtime.sceneGeneration = 1;
  round(fixture);
  CHECK(history.undoDepth() == 0);
}
