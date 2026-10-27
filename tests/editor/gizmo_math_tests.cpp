// Plan 0068 M1 (ADR-0137 D3): gizmo.h's pure functions -- the drag math
// GizmoModel uses and the screen handles the ImGui view draws, grabs and drags
// -- pinned on their own, so that the vectors generated from them
// (tests/csharp/conformance/gizmo_vectors.jsonl) rest on tested code. E1's own
// tests (editor_model_tests.cpp, editor_view_tests.cpp, the GPU north star)
// keep holding GizmoModel and the view to their behaviour.
#include <atlantis/editor/model/gizmo.h>
#include <atlantis/editor/model/viewport_camera.h>
#include <atlantis/editor/model/viewport_picking.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <optional>

namespace {

using namespace atlantis::editor;

// A camera at the origin looking down -z (head-on), 1280 x 720.
[[nodiscard]] CameraMatrices headOn() {
  CameraPose pose;
  const std::optional<CameraMatrices> camera = cameraMatrices(pose, 1280.0f / 720.0f);
  REQUIRE(camera.has_value());
  return *camera;
}

constexpr PixelRect kRect{0.0f, 0.0f, 1280.0f, 720.0f};
constexpr std::array<Vec3, 3> kWorldAxes{Vec3{1.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f}};

[[nodiscard]] bool near(float a, float b, float tolerance = 1e-4f) { return std::fabs(a - b) <= tolerance; }

}  // namespace

TEST_CASE("gizmo handles: head-on, X right and Y up, 80 pixels each; Z toward the camera has none",
          "[editor][gizmo][plan0068]") {
  const CameraMatrices camera = headOn();
  const Vec3 position{0.0f, 0.0f, -10.0f};
  const std::optional<GizmoHandles> handles = gizmoHandles(camera, kRect, position, kWorldAxes);
  REQUIRE(handles.has_value());
  REQUIRE((*handles)[0].has_value());
  REQUIRE((*handles)[1].has_value());
  CHECK_FALSE((*handles)[2].has_value());  // projects onto the origin
  const GizmoHandle& x = *(*handles)[0];
  const GizmoHandle& y = *(*handles)[1];
  CHECK(near(x.origin[0], 640.0f));
  CHECK(near(x.origin[1], 360.0f));
  CHECK(near(x.direction[0], 1.0f));
  CHECK(near(y.direction[1], -1.0f));  // screen y points down: world up is negative
  CHECK(near(x.end[0], 640.0f + kGizmoHandlePixels));
  CHECK(near(y.end[1], 360.0f - kGizmoHandlePixels));
  CHECK(x.pixelsPerUnit > 0.0f);
  // Behind the camera: no handles at all.
  CHECK_FALSE(gizmoHandles(camera, kRect, Vec3{0.0f, 0.0f, 10.0f}, kWorldAxes).has_value());
}

TEST_CASE("gizmo grab: the nearest handle within 8 pixels, a ring for Rotate, nothing farther", "[editor][gizmo][plan0068]") {
  const CameraMatrices camera = headOn();
  const std::optional<GizmoHandles> handles = gizmoHandles(camera, kRect, Vec3{0.0f, 0.0f, -10.0f}, kWorldAxes);
  REQUIRE(handles.has_value());
  const std::optional<GizmoGrab> onX = gizmoGrab(*handles, kWorldAxes, camera, GizmoMode::Translate, {680.0f, 365.0f});
  REQUIRE(onX.has_value());
  CHECK(onX->axis == GizmoAxis::X);
  CHECK(onX->start == std::array<float, 2>{680.0f, 365.0f});
  CHECK_FALSE(gizmoGrab(*handles, kWorldAxes, camera, GizmoMode::Translate, {680.0f, 370.0f}).has_value());  // 10 px off
  // Rotate: on X's ring (9 pixels around its end), not on its segment.
  const float ringX = 640.0f + kGizmoHandlePixels + kGizmoRotateRingPixels;
  const std::optional<GizmoGrab> ring = gizmoGrab(*handles, kWorldAxes, camera, GizmoMode::Rotate, {ringX, 360.0f});
  REQUIRE(ring.has_value());
  CHECK(ring->axis == GizmoAxis::X);
  CHECK_FALSE(gizmoGrab(*handles, kWorldAxes, camera, GizmoMode::Rotate, {680.0f, 360.0f}).has_value());
}

TEST_CASE("gizmo drag amount: units along the handle, a scale factor, a screen turn", "[editor][gizmo][plan0068]") {
  const CameraMatrices camera = headOn();
  const std::optional<GizmoHandles> handles = gizmoHandles(camera, kRect, Vec3{0.0f, 0.0f, -10.0f}, kWorldAxes);
  REQUIRE(handles.has_value());
  const GizmoGrab grab = *gizmoGrab(*handles, kWorldAxes, camera, GizmoMode::Translate, {700.0f, 360.0f});
  const float perUnit = grab.handle.pixelsPerUnit;
  CHECK(near(gizmoDragAmount(GizmoMode::Translate, grab, {700.0f + perUnit, 360.0f}), 1.0f));
  CHECK(near(gizmoDragAmount(GizmoMode::Translate, grab, {700.0f, 300.0f}), 0.0f));  // across the handle: nothing
  CHECK(near(gizmoDragAmount(GizmoMode::Scale, grab, {700.0f + kGizmoHandlePixels, 360.0f}), 2.0f));
  // A quarter turn about the origin, from the +x side to screen-down (+y); X is perpendicular to the view.
  GizmoGrab turn = grab;
  turn.start = {740.0f, 360.0f};
  turn.startAngle = 0.0f;
  turn.towardCamera = false;
  CHECK(near(gizmoDragAmount(GizmoMode::Rotate, turn, {640.0f, 460.0f}), 1.5707964f));
  turn.towardCamera = true;
  CHECK(near(gizmoDragAmount(GizmoMode::Rotate, turn, {640.0f, 460.0f}), -1.5707964f));
}

TEST_CASE("gizmo drag matrix: translate, rotate and scale from a start, with snapping", "[editor][gizmo][plan0068]") {
  Trs trs;
  trs.translation = {1.0f, 2.0f, 3.0f};
  trs.scale = {2.0f, 1.0f, 1.0f};
  const MatrixColumns start = compose(trs);
  const MatrixColumns moved = gizmoDragMatrix(start, GizmoMode::Translate, GizmoSpace::World, GizmoAxis::Y, 1.3f, true);
  CHECK(moved[3][1] == 2.0f + 1.5f);  // 1.3 snapped to 1.5
  CHECK(moved[0] == start[0]);
  const MatrixColumns scaled = gizmoDragMatrix(start, GizmoMode::Scale, GizmoSpace::World, GizmoAxis::X, 0.02f, true);
  CHECK(near(scaled[0][0], 2.0f * kScaleSnap));  // never below one step
  const MatrixColumns turned =
      gizmoDragMatrix(start, GizmoMode::Rotate, GizmoSpace::World, GizmoAxis::Z, 0.2f, true);  // snaps to 15 degrees
  CHECK(near(turned[0][1], 2.0f * std::sin(kRotateSnapRadians)));
  CHECK(snapGizmoAmount(GizmoMode::Rotate, 0.2f) == kRotateSnapRadians);
  CHECK(gizmoAxisDirection(trs, GizmoMode::Translate, GizmoSpace::World, GizmoAxis::Z) == Vec3{0.0f, 0.0f, 1.0f});
  CHECK(gizmoAxisFacesCamera(headOn(), Vec3{0.0f, 0.0f, 1.0f}));
  CHECK_FALSE(gizmoAxisFacesCamera(headOn(), Vec3{0.0f, 0.0f, -1.0f}));
}
