#include "editor_fixture.h"

#include <atlantis/connection/text.h>
#include <atlantis/editor/editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
using atlantis::editor::CameraMatrices;
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
  REQUIRE(fixture.editor->wantsCamera());

  // A camera at (0, 0, 10) looking down -z, 60 degree vertical field of view,
  // Vulkan clip space (y down, depth 0..1), column-major.
  const auto viewport = fixture.editor->viewportSize();
  const float aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
  const float f = 1.0f / std::tan(0.5236f);
  constexpr float n = 0.1f;
  constexpr float far = 100.0f;
  CameraMatrices camera;
  camera.view = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -10, 1};
  camera.projection = {f / aspect, 0, 0, 0, 0, -f, 0, 0, 0, 0, far / (n - far), -1, 0, 0, far * n / (n - far), 0};
  driver.context.camera = camera;
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

