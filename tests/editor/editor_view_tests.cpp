#include "editor_fixture.h"

#include <atlantis/connection/text.h>
#include <atlantis/editor/editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// Plan 0056 M5 (P8; Spec 0056 R2-R6): the view layer run headless -- the UI
// library with no backend, a fixed 1280x720 display -- driven by scripted
// pointer input, one input event per UI frame (the UI library applies at most
// one button change per frame). Widgets are found through
// Editor::widgetRect(); the effects are checked on the World (through a
// connection of the test's own) and on the fake RuntimeControl.

namespace {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
namespace text = atlantis::connection::text;
using atlantis::asset_system::EntityGuid;
using atlantis::editor::FrameContext;
using atlantis::editor::InputEvent;
using atlantis::editor::PointerButton;
using atlantis::editor::PointerButtonChanged;
using atlantis::editor::PointerMoved;
using atlantis::editor::UiDrawList;
using atlantis::editor::UiTexture;
using atlantis::editor::test::EditorFixture;
using atlantis::editor::test::guid;

const EntityGuid kMesh = guid("52052052-0001-4052-8052-000000000001");
const EntityGuid kSun = guid("52052052-0002-4052-8052-000000000002");

struct Point {
  float x;
  float y;
};

// Drives an editor one UI frame at a time.
struct Driver {
  explicit Driver(EditorFixture& fixture) : fixture(fixture) {}

  void frame(std::vector<InputEvent> input = {}) { fixture.editor->frame(input, context); }
  void idle(int frames) {
    for (int i = 0; i < frames; ++i) frame();
  }
  [[nodiscard]] Point center(const std::string& key) {
    const auto rect = fixture.editor->widgetRect(key);
    INFO(key);
    REQUIRE(rect.has_value());
    return Point{static_cast<float>(rect->x) + static_cast<float>(rect->width) / 2.0f,
                 static_cast<float>(rect->y) + static_cast<float>(rect->height) / 2.0f};
  }
  void moveTo(Point p) { frame({PointerMoved{p.x, p.y}}); }
  void press(Point p) { frame({PointerButtonChanged{PointerButton::Left, true, p.x, p.y}}); }
  void release(Point p) { frame({PointerButtonChanged{PointerButton::Left, false, p.x, p.y}}); }
  // Scrolls the panel under `key`'s widget with the wheel until the widget
  // lies inside the display (the Inspector holds more than its panel shows).
  void reveal(const std::string& key) {
    for (int i = 0; i < 40; ++i) {
      const auto rect = fixture.editor->widgetRect(key);
      INFO(key);
      REQUIRE(rect.has_value());
      if (rect->y >= 0 && rect->y + static_cast<std::int32_t>(rect->height) <= static_cast<std::int32_t>(context.windowHeight) - 4) return;
      const Point inside{static_cast<float>(rect->x) + 4.0f, static_cast<float>(context.windowHeight) - 20.0f};
      moveTo(inside);
      frame({atlantis::editor::WheelScrolled{0.0f, rect->y < 0 ? 1.0f : -1.0f}});
      idle(1);
    }
    FAIL("could not scroll " << key << " into view");
  }
  void click(const std::string& key) {
    reveal(key);
    const Point p = center(key);
    moveTo(p);
    press(p);
    release(p);
  }

  EditorFixture& fixture;
  FrameContext context{1280, 720};
};

// The World's PropertyChanged events for one entity, through a connection of
// the test's own.
struct Changes {
  Changes(EditorFixture& fixture, const EntityGuid& entity) : observer(fixture.endpoint.open()) {
    connection::EventFilter filter;
    filter.kinds = connection::EventKindSet::only(connection::EventKind::PropertyChanged);
    filter.entity = entity;
    subscription = observer->subscribe(filter);
  }
  [[nodiscard]] std::vector<access::PropertyChanged> drain() {
    std::vector<access::PropertyChanged> out;
    auto events = observer->drainEvents(subscription);  // held: value() refers into it
    for (const access::Event& event : events.value()) {
      out.push_back(std::get<access::PropertyChanged>(event));
    }
    return out;
  }
  std::unique_ptr<connection::RuntimeConnection> observer;
  connection::SubscriptionId subscription;
};

[[nodiscard]] atlantis::schema::FieldId fieldOf(connection::RuntimeConnection& c, std::string_view path) {
  auto resolved = text::parsePath(c.schema(), path);
  REQUIRE(resolved.isOk());
  return resolved.value().leaf->id;
}

void selectInHierarchy(Driver& driver, const EntityGuid& entity) {
  driver.idle(2);
  driver.click("hierarchy:" + atlantis::asset_system::toString(entity));
  driver.idle(2);
  REQUIRE(driver.fixture.editor->hierarchy().selection() == entity);
}

}  // namespace

TEST_CASE("clicking a Hierarchy row selects its entity for the Inspector", "[editor][view]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kSun);
  CHECK(fixture.editor->inspector().subject() == kSun);
  CHECK(fixture.editor->widgetRect("inspector:Light.intensity").has_value());
  CHECK(fixture.editor->widgetRect("inspector:Light").has_value());
  CHECK_FALSE(fixture.editor->widgetRect("inspector:Camera.nearZ").has_value());
}

TEST_CASE("dragging a float field writes nothing until release, then exactly one SetProperty", "[editor][view]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kSun);
  Changes changes(fixture, kSun);
  driver.reveal("inspector:Light.intensity");
  const Point start = driver.center("inspector:Light.intensity");
  driver.moveTo(start);
  driver.press(start);
  for (int dx = 10; dx <= 60; dx += 10) {
    driver.moveTo(Point{start.x + static_cast<float>(dx), start.y});
    fixture.runtime.frame();  // Runtime frames run while the field is dragged
  }
  CHECK(changes.drain().empty());  // nothing written while dragging
  driver.release(Point{start.x + 60.0f, start.y});
  fixture.runtime.frame();
  const auto events = changes.drain();
  REQUIRE(events.size() == 1);
  CHECK(events[0].address.field == fieldOf(*fixture.connection, "Light.intensity"));
  REQUIRE(std::holds_alternative<float>(events[0].value));
  CHECK(std::get<float>(events[0].value) > 1.2f);
  driver.idle(1);
  const auto& inspector = fixture.editor->inspector();
  const auto it = std::find_if(inspector.fields().begin(), inspector.fields().end(),
                               [](const auto& field) { return field.path == "Light.intensity"; });
  REQUIRE(it != inspector.fields().end());
  const auto index = static_cast<std::size_t>(it - inspector.fields().begin());
  REQUIRE(inspector.outcome(index).has_value());
  CHECK(inspector.outcome(index)->status == atlantis::editor::EditStatus::Applied);
}

TEST_CASE("picking an enum constant commits it as one SetProperty", "[editor][view]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kSun);
  Changes changes(fixture, kSun);
  driver.click("inspector:Light.kind");
  driver.idle(1);
  driver.click("inspector:Light.kind#Point");
  fixture.runtime.frame();
  const auto events = changes.drain();
  REQUIRE(events.size() == 1);
  CHECK(events[0].address.field == fieldOf(*fixture.connection, "Light.kind"));
  CHECK(events[0].value == access::PropertyValue{access::EnumValue{1}});
}

TEST_CASE("Play, Pause and Step call the RuntimeControl", "[editor][view]") {
  EditorFixture fixture;
  Driver driver(fixture);
  driver.idle(2);
  driver.click("toolbar:Pause");
  driver.click("toolbar:Step");
  driver.click("toolbar:Play");
  CHECK(fixture.runtime.calls == std::vector<std::string>{"pause", "step(1)", "resume"});
  driver.idle(1);
  // The toolbar's Gizmo modes are the model's.
  driver.click("toolbar:Rotate");
  CHECK(fixture.editor->gizmo().mode() == atlantis::editor::GizmoMode::Rotate);
}

TEST_CASE("the UI draw list is well formed, uses both textures, and is deterministic", "[editor][view]") {
  EditorFixture fixture;
  Driver driver(fixture);
  driver.idle(4);
  const UiDrawList first = fixture.editor->drawList();
  driver.idle(1);
  const UiDrawList& second = fixture.editor->drawList();
  CHECK(first == second);  // two identical frames, identical lists

  REQUIRE_FALSE(second.commands.empty());
  CHECK(second.displayWidth == 1280);
  CHECK(second.displayHeight == 720);
  bool font = false;
  bool viewport = false;
  for (const auto& command : second.commands) {
    REQUIRE(static_cast<std::size_t>(command.firstIndex) + command.indexCount <= second.indices.size());
    for (std::uint32_t i = 0; i < command.indexCount; ++i) {
      const auto vertex = static_cast<std::int64_t>(second.indices[command.firstIndex + i]) + command.vertexOffset;
      REQUIRE(vertex >= 0);
      REQUIRE(static_cast<std::size_t>(vertex) < second.vertices.size());
    }
    CHECK(command.clip.x >= 0);
    CHECK(command.clip.y >= 0);
    CHECK(static_cast<std::uint32_t>(command.clip.x) + command.clip.width <= second.displayWidth);
    CHECK(static_cast<std::uint32_t>(command.clip.y) + command.clip.height <= second.displayHeight);
    font = font || command.texture == UiTexture::FontAtlas;
    viewport = viewport || command.texture == UiTexture::Viewport;
  }
  CHECK(font);
  CHECK(viewport);
}

TEST_CASE("a Gizmo handle dragged in the Viewport moves the entity along the axis", "[editor][view][gizmo]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kMesh);
  REQUIRE(fixture.editor->gizmo().available());

  // A camera at (0, 0, 10) looking down -z, 60 degree vertical field of view,
  // Vulkan clip space (y down, depth 0..1), column-major.
  const auto viewport = fixture.editor->viewportSize();
  const float aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
  const float f = 1.0f / std::tan(0.5236f);
  constexpr float n = 0.1f;
  constexpr float far = 100.0f;
  // Plan 0062 J4: the host gives the scene camera's pose and lens; the editor
  // camera starts from it exactly (ruling Q6, D-a).
  atlantis::editor::CameraPose pose;
  pose.worldMatrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 10, 1};
  pose.lens = atlantis::editor::CameraLens{2.0f * 0.5236f, n, far};
  driver.context.sceneCamera = pose;
  driver.idle(1);

  // The X handle: from the entity's projected origin toward +x on screen.
  const auto image = fixture.editor->widgetRect("viewport");
  REQUIRE(image.has_value());
  const auto projectX = [&](float worldX) {
    const float viewZ = 3.0f - 10.0f;  // the mesh is at (1, 2, 3)
    const float ndcX = (f / aspect) * worldX / -viewZ;
    return static_cast<float>(image->x) + (ndcX + 1.0f) * 0.5f * static_cast<float>(image->width);
  };
  const float ndcY = (-f * 2.0f) / -(3.0f - 10.0f);
  const float screenY = static_cast<float>(image->y) + (ndcY + 1.0f) * 0.5f * static_cast<float>(image->height);
  const float originX = projectX(1.0f);
  const Point grab{originX + 40.0f, screenY};  // the middle of the 80-pixel handle

  Changes changes(fixture, kMesh);
  const auto before = fixture.connection->getProperty(
      {kMesh, text::findType(fixture.connection->schema(), "WorldMatrix")->id, fieldOf(*fixture.connection, "WorldMatrix.column3")});
  driver.moveTo(grab);
  driver.press(grab);
  REQUIRE(fixture.editor->gizmo().dragging());
  for (int dx = 10; dx <= 30; dx += 10) driver.moveTo(Point{grab.x + static_cast<float>(dx), grab.y});
  driver.release(Point{grab.x + 30.0f, grab.y});
  CHECK_FALSE(fixture.editor->gizmo().dragging());
  fixture.runtime.frame();
  const auto events = changes.drain();
  CHECK(events.size() % 4 == 0);  // whole four-column transactions only
  CHECK(events.size() >= 8);      // at least one while dragging, and the release
  const auto after = fixture.connection->getProperty(
      {kMesh, text::findType(fixture.connection->schema(), "WorldMatrix")->id, fieldOf(*fixture.connection, "WorldMatrix.column3")});
  const auto& b = std::get<std::array<float, 4>>(before.value());
  const auto& a = std::get<std::array<float, 4>>(after.value());
  // 30 pixels at the handle's pixels-per-unit: about 30 / (projectX(2) - projectX(1)) world units.
  const float expected = 30.0f / (projectX(2.0f) - projectX(1.0f));
  CHECK(std::fabs((a[0] - b[0]) - expected) < 1e-3f);
  CHECK(a[1] == b[1]);
  CHECK(a[2] == b[2]);
}

// --- Plan 0062 M5 (P10; Spec 0062 R1, R2, R4, R6, R7): navigation, clicks that
// pick, Esc, the Hierarchy following a selection made elsewhere, and the Gizmo's
// space and snapping keys -- scripted input, headless. -------------------------------

namespace {

using atlantis::editor::CameraLens;
using atlantis::editor::CameraPose;
using atlantis::editor::GizmoSpace;
using atlantis::editor::Key;
using atlantis::editor::KeyChanged;
using atlantis::editor::KeyModifiers;
using atlantis::editor::ViewportCameraMode;
using atlantis::editor::WheelScrolled;

// The scene camera the host reports: at (0, 0, 10) looking down -z.
[[nodiscard]] CameraPose sceneCamera() {
  CameraPose pose;
  pose.worldMatrix[14] = 10.0f;
  pose.lens = CameraLens{1.0472f, 0.1f, 100.0f};
  return pose;
}

// A Driver with the scene camera set and a few frames run.
void start(Driver& driver) {
  driver.context.sceneCamera = sceneCamera();
  driver.idle(2);
}

// A point on the Viewport image far from every marker and from the Gizmo: near
// its bottom-left corner.
[[nodiscard]] Point emptyViewportPoint(Driver& driver) {
  const auto image = driver.fixture.editor->widgetRect("viewport");
  REQUIRE(image.has_value());
  return Point{static_cast<float>(image->x) + 20.0f,
               static_cast<float>(image->y) + static_cast<float>(image->height) - 20.0f};
}

void button(Driver& driver, PointerButton which, bool down, Point p) {
  driver.frame({PointerButtonChanged{which, down, p.x, p.y}});
}

void key(Driver& driver, Key k, bool down, KeyModifiers modifiers = {}) { driver.frame({KeyChanged{k, down, modifiers}}); }

}  // namespace

TEST_CASE("a right drag on the Viewport looks around; the wheel dollies; neither picks", "[editor][view][camera]") {
  EditorFixture fixture;
  Driver driver(fixture);
  start(driver);
  auto& camera = fixture.editor->camera();
  REQUIRE(fixture.editor->viewportCamera() == sceneCamera());  // ruling Q6, D-a
  const Point p = emptyViewportPoint(driver);
  driver.moveTo(p);
  button(driver, PointerButton::Right, true, p);
  driver.moveTo(Point{p.x + 100.0f, p.y});
  button(driver, PointerButton::Right, false, Point{p.x + 100.0f, p.y});
  CHECK(camera.editor().forward()[0] > 0.0f);  // looks right
  CHECK_FALSE(fixture.editor->viewportCamera() == sceneCamera());
  CHECK_FALSE(fixture.editor->pickRequest().has_value());

  const float distance = camera.editor().pivotDistance();
  driver.frame({WheelScrolled{0.0f, 1.0f}});
  CHECK(std::fabs(camera.editor().pivotDistance() - distance * 0.85f) < 1e-4f);
  CHECK_FALSE(fixture.editor->pickRequest().has_value());
}

TEST_CASE("an Alt + left drag orbits about the pivot and does not pick", "[editor][view][camera]") {
  EditorFixture fixture;
  Driver driver(fixture);
  start(driver);
  auto& camera = fixture.editor->camera();
  const auto pivot = camera.editor().pivot();
  const Point p = emptyViewportPoint(driver);
  driver.moveTo(p);
  key(driver, Key::LeftAlt, true, KeyModifiers{false, false, true});
  driver.press(p);
  driver.moveTo(Point{p.x + 60.0f, p.y - 20.0f});
  driver.release(Point{p.x + 60.0f, p.y - 20.0f});
  key(driver, Key::LeftAlt, false);
  CHECK_FALSE(fixture.editor->pickRequest().has_value());
  const auto moved = camera.editor().pivot();
  for (std::size_t i = 0; i < 3; ++i) CHECK(std::fabs(moved[i] - pivot[i]) < 1e-3f);
  CHECK(camera.editor().eye()[0] < 0.0f);
}

TEST_CASE("in Scene mode a navigation switches the Viewport to the editor camera, from the scene camera",
          "[editor][view][camera]") {
  EditorFixture fixture;
  Driver driver(fixture);
  start(driver);
  driver.click("toolbar:SceneCam");
  REQUIRE(fixture.editor->camera().mode() == ViewportCameraMode::Scene);
  CHECK_FALSE(fixture.editor->viewportCamera().has_value());
  const Point p = emptyViewportPoint(driver);
  driver.moveTo(p);
  button(driver, PointerButton::Middle, true, p);
  driver.moveTo(Point{p.x + 15.0f, p.y});
  button(driver, PointerButton::Middle, false, Point{p.x + 15.0f, p.y});
  CHECK(fixture.editor->camera().mode() == ViewportCameraMode::Editor);
  REQUIRE(fixture.editor->viewportCamera().has_value());
  CHECK(fixture.editor->viewportCamera()->worldMatrix[12] < 0.0f);  // panned: the scene follows the pointer right
  driver.click("toolbar:Align");
  CHECK(fixture.editor->viewportCamera() == sceneCamera());
}

TEST_CASE("a click on the Viewport requests one pick; a drag of four pixels or more requests none",
          "[editor][view][picking]") {
  EditorFixture fixture;
  Driver driver(fixture);
  start(driver);
  const Point p = emptyViewportPoint(driver);
  driver.moveTo(p);
  driver.press(p);
  driver.release(Point{p.x + 2.0f, p.y + 1.0f});
  REQUIRE(fixture.editor->pickRequest().has_value());
  const auto request = *fixture.editor->pickRequest();
  driver.idle(1);
  CHECK_FALSE(fixture.editor->pickRequest().has_value());  // one request, taken by the host

  // The host's answer selects; the Hierarchy, Inspector and Gizmo follow.
  driver.context.pickAnswer = atlantis::editor::PickAnswer{request.id, kMesh, 5.0f};
  driver.idle(1);
  driver.context.pickAnswer.reset();
  CHECK(fixture.editor->hierarchy().selection() == kMesh);
  CHECK(fixture.editor->inspector().subject() == kMesh);
  CHECK(fixture.editor->gizmo().subject() == kMesh);

  driver.moveTo(p);
  driver.press(p);
  driver.moveTo(Point{p.x + 4.0f, p.y});
  driver.release(Point{p.x + 4.0f, p.y});
  CHECK_FALSE(fixture.editor->pickRequest().has_value());

  // Esc clears the selection.
  key(driver, Key::Escape, true);
  key(driver, Key::Escape, false);
  CHECK_FALSE(fixture.editor->hierarchy().selection().has_value());
}

TEST_CASE("a selection made outside the Hierarchy scrolls its row into view; one the filter hides is named",
          "[editor][view][hierarchy]") {
  EditorFixture fixture;
  auto& c = *fixture.connection;
  // Enough entities that most rows lie below the panel.
  EntityGuid last;
  for (int i = 0; i < 120; ++i) {
    char text[40];
    std::snprintf(text, sizeof(text), "77777777-0000-4077-8077-%012d", i);
    last = guid(text);
    (void)c.submit(access::CreateEntity{last});
  }
  fixture.runtime.frame();
  Driver driver(fixture);
  start(driver);
  const std::string row = "hierarchy:" + atlantis::asset_system::toString(last);
  CHECK_FALSE(fixture.editor->widgetRect(row).has_value());  // below the panel, not drawn
  fixture.editor->hierarchy().select(last);                   // as a pick answer selects
  driver.idle(2);
  const auto rect = fixture.editor->widgetRect(row);
  REQUIRE(rect.has_value());
  CHECK(rect->y >= 0);
  CHECK(rect->y + static_cast<std::int32_t>(rect->height) <= static_cast<std::int32_t>(driver.context.windowHeight));

  // Filter to the lights; the selection is hidden, and the note's button clears the filter.
  driver.click("hierarchy:filter");
  for (const char ch : std::string("Light")) {
    atlantis::editor::TextEntered typed;
    typed.utf8[0] = ch;
    typed.size = 1;
    driver.frame({typed});
  }
  driver.idle(1);
  REQUIRE(fixture.editor->widgetRect("hierarchy:filter-hidden").has_value());
  driver.click("hierarchy:filter-hidden");
  driver.idle(1);
  CHECK_FALSE(fixture.editor->widgetRect("hierarchy:filter-hidden").has_value());
}

TEST_CASE("X toggles the Gizmo's space and Ctrl snaps while the Viewport is hovered", "[editor][view][gizmo]") {
  EditorFixture fixture;
  Driver driver(fixture);
  start(driver);
  selectInHierarchy(driver, kMesh);
  const Point p = emptyViewportPoint(driver);
  driver.moveTo(p);
  key(driver, Key::X, true);
  key(driver, Key::X, false);
  CHECK(fixture.editor->gizmo().space() == GizmoSpace::Local);
  driver.click("toolbar:World");
  CHECK(fixture.editor->gizmo().space() == GizmoSpace::World);
  driver.moveTo(p);
  key(driver, Key::LeftCtrl, true, KeyModifiers{true, false, false});
  CHECK(fixture.editor->gizmo().snap());
  key(driver, Key::LeftCtrl, false);
  CHECK_FALSE(fixture.editor->gizmo().snap());
}

// --- Plan 0063 M4 (P8; Spec 0063 R9): Undo/Redo in the toolbar and on Ctrl+Z,
// Ctrl+Y and Ctrl+Shift+Z -- not inside a text field, not during a drag -- and
// the history's status line; scripted input, headless. -----------------------------

namespace {

using atlantis::editor::HistoryResult;

// The Runtime applies what was submitted, then the editor runs one frame.
void settleRound(Driver& driver, int count = 1) {
  for (int i = 0; i < count; ++i) {
    driver.fixture.runtime.frame();
    driver.idle(1);
  }
}

void chord(Driver& driver, Key k, bool shift = false) {
  const KeyModifiers held{true, shift, false};
  driver.frame({KeyChanged{Key::LeftCtrl, true, held}});
  if (shift) driver.frame({KeyChanged{Key::LeftShift, true, held}});
  driver.frame({KeyChanged{k, true, held}});
  driver.frame({KeyChanged{k, false, held}});
  if (shift) driver.frame({KeyChanged{Key::LeftShift, false, KeyModifiers{true, false, false}}});
  driver.frame({KeyChanged{Key::LeftCtrl, false, KeyModifiers{}}});
}

// Light.intensity of the fixture's directional light, through the test's connection.
[[nodiscard]] float sunIntensity(EditorFixture& fixture) {
  auto& c = *fixture.connection;
  const auto* light = text::findType(c.schema(), "Light");
  REQUIRE(light != nullptr);
  return std::get<float>(c.getProperty({kSun, light->id, fieldOf(c, "Light.intensity")}).value());
}

// Selects the sun and commits Light.intensity = value through the Inspector, settled.
void editSun(Driver& driver, float value) {
  auto& inspector = driver.fixture.editor->inspector();
  std::size_t index = inspector.fields().size();
  for (std::size_t i = 0; i < inspector.fields().size(); ++i) {
    if (inspector.fields()[i].path == "Light.intensity") index = i;
  }
  REQUIRE(index < inspector.fields().size());
  (void)inspector.commit(index, value);
  settleRound(driver, 2);
}

}  // namespace

TEST_CASE("Ctrl+Z undoes, Ctrl+Y and Ctrl+Shift+Z redo", "[editor][view][history]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kSun);
  editSun(driver, 6.0f);
  REQUIRE(fixture.editor->history().canUndo());
  chord(driver, Key::Z);
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 1.2f);
  chord(driver, Key::Y);
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 6.0f);
  chord(driver, Key::Z);
  settleRound(driver, 2);
  chord(driver, Key::Z, /*shift=*/true);
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 6.0f);
}

TEST_CASE("Ctrl+Z in a text field is the field's; during a Gizmo drag it is ignored", "[editor][view][history]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kSun);
  editSun(driver, 6.0f);
  driver.click("hierarchy:filter");
  chord(driver, Key::Z);
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 6.0f);
  CHECK(fixture.editor->history().undoDepth() == 1);

  // Leave the text field, then hold a drag on the mesh: Ctrl+Z does nothing.
  driver.frame({KeyChanged{Key::Escape, true, KeyModifiers{}}});
  driver.frame({KeyChanged{Key::Escape, false, KeyModifiers{}}});
  selectInHierarchy(driver, kMesh);
  auto& gizmo = fixture.editor->gizmo();
  REQUIRE(gizmo.available());
  gizmo.beginDrag(atlantis::editor::GizmoAxis::X);
  chord(driver, Key::Z);
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 6.0f);
  gizmo.endDrag();
}

TEST_CASE("the Undo and Redo buttons act only when enabled", "[editor][view][history]") {
  EditorFixture fixture;
  Driver driver(fixture);
  driver.idle(2);
  REQUIRE(fixture.editor->widgetRect("toolbar:Undo").has_value());
  driver.click("toolbar:Undo");  // nothing to undo
  CHECK(fixture.editor->history().notice().empty());
  selectInHierarchy(driver, kSun);
  editSun(driver, 6.0f);
  driver.click("toolbar:Undo");
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 1.2f);
  CHECK(fixture.editor->history().canRedo());
  driver.click("toolbar:Redo");
  settleRound(driver, 2);
  CHECK(sunIntensity(fixture) == 6.0f);
}

TEST_CASE("the status line tells why an undo was refused", "[editor][view][history]") {
  EditorFixture fixture;
  Driver driver(fixture);
  selectInHierarchy(driver, kSun);
  editSun(driver, 6.0f);
  auto other = fixture.endpoint.open();
  const auto* light = text::findType(other->schema(), "Light");
  (void)other->submit(access::SetProperty{{kSun, light->id, fieldOf(*other, "Light.intensity")}, 5.0f});
  settleRound(driver);
  chord(driver, Key::Z);
  settleRound(driver);
  CHECK(sunIntensity(fixture) == 5.0f);
  CHECK(fixture.editor->history().notice().find("changed elsewhere") != std::string::npos);
  CHECK(fixture.editor->widgetRect("status:history").has_value());
}
