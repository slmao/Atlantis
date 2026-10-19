#pragma once

#include <array>
#include <cstdint>
#include <optional>

// Plan 0062 M1 (Spec 0062 R1-R3; ADR-0123 D1): the editor camera -- client
// state, never written to the World. The host renders the Viewport from the
// pose and lens it hands over (ADR-0123 D3); the matrices below are the
// editor's own copy of the host's camera convention, for its overlay and pick
// rays, pinned to the host's by tests/runtime/editor_camera_convention_tests.cpp.
// UI-free: no UI-library header is included by any model file.
namespace atlantis::editor {

using Vec3 = std::array<float, 3>;
// Column-major 4x4.
using Mat4 = std::array<float, 16>;

// A camera's view and projection, column-major, in the host's convention:
// look-at with world-up +Y, Vulkan clip space (y down, depth 0..1).
struct CameraMatrices {
  Mat4 view{};
  Mat4 projection{};
  friend bool operator==(const CameraMatrices&, const CameraMatrices&) = default;
};

struct CameraLens {
  float fovYRadians = 1.0472f;
  float nearZ = 0.1f;
  float farZ = 1000.0f;
  friend bool operator==(const CameraLens&, const CameraLens&) = default;
};

// A camera as the host reads one: the eye is column 3 of `worldMatrix`, the
// forward direction is -column 2 (normalized); the other columns and any roll
// are ignored (the look-at uses world-up +Y).
struct CameraPose {
  Mat4 worldMatrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  CameraLens lens;
  friend bool operator==(const CameraPose&, const CameraPose&) = default;
};

// The host's extraction, step for step (forward = normalize(-column 2); eye =
// column 3; right = cross(forward, +Y)), then its look-at and its perspective
// projection at `aspect`. Empty exactly where the host's would fail: a
// near-zero forward, or a forward (near-)parallel to +Y.
[[nodiscard]] std::optional<CameraMatrices> cameraMatrices(const CameraPose& pose, float aspect);

// A world point to normalized device coordinates (x right, y down, both in
// [-1, 1] inside the view); empty behind the camera.
[[nodiscard]] std::optional<std::array<float, 2>> projectToNdc(const CameraMatrices& camera, const Vec3& point);

// A world point's distance in front of the camera (its -z in view space).
[[nodiscard]] float viewDepth(const CameraMatrices& camera, const Vec3& point);

struct Ray {
  Vec3 origin{};
  Vec3 direction{0.0f, 0.0f, -1.0f};  // unit length
  friend bool operator==(const Ray&, const Ray&) = default;
};

// The world-space ray through normalized device coordinates (ndcX, ndcY),
// from the eye. Requires a rigid view and a perspective projection (the
// convention above).
[[nodiscard]] Ray rayThroughNdc(const CameraMatrices& camera, float ndcX, float ndcY);

// The editor camera: an eye, a yaw and a pitch (no roll), a pivot in front of
// the eye, a fly speed and a lens. yaw = pitch = 0 looks down -z; positive yaw
// turns left (toward -x), positive pitch looks up. Navigation follows Spec
// 0062 R2: angles per pointer pixel, distances scaled by the pivot distance,
// motion in units per second. Not thread-safe.
class ViewportCameraModel {
 public:
  static constexpr float kMaxPitchRadians = 1.5533430f;    // 89 degrees
  static constexpr float kLookRadiansPerPixel = 0.0043633f;   // 0.25 degrees
  static constexpr float kOrbitRadiansPerPixel = 0.0052360f;  // 0.3 degrees
  static constexpr float kMinPivotDistance = 0.05f;
  static constexpr float kDefaultPivotDistance = 10.0f;
  static constexpr float kWheelDollyFactor = 0.85f;  // per notch toward the pivot
  static constexpr float kDragDollyPerPixel = 0.01f; // distance *= exp(-k * pixels)
  static constexpr float kMinFlySpeed = 0.05f;
  static constexpr float kMaxFlySpeed = 500.0f;
  static constexpr float kDefaultFlySpeed = 5.0f;    // units per second
  static constexpr float kFlySpeedStep = 1.25f;      // per wheel notch
  static constexpr float kFastFlyFactor = 4.0f;      // Shift

  // Takes the pose's eye, direction (pitch clamped) and lens; the pivot
  // distance and fly speed are kept.
  void reset(const CameraPose& pose);

  // The pose the host renders: column 0 right, column 1 up, column 2 -forward,
  // column 3 the eye. Until the first navigation after a reset() whose pitch
  // needed no clamp, the reset pose's own world matrix, bit for bit -- so a
  // camera taken from the scene draws exactly the scene camera's image.
  [[nodiscard]] CameraPose pose() const;
  [[nodiscard]] std::optional<CameraMatrices> matrices(float aspect) const { return cameraMatrices(pose(), aspect); }

  [[nodiscard]] const Vec3& eye() const noexcept { return eye_; }
  [[nodiscard]] float yaw() const noexcept { return yaw_; }
  [[nodiscard]] float pitch() const noexcept { return pitch_; }
  [[nodiscard]] float pivotDistance() const noexcept { return pivotDistance_; }
  [[nodiscard]] float flySpeed() const noexcept { return flySpeed_; }
  [[nodiscard]] const CameraLens& lens() const noexcept { return lens_; }
  [[nodiscard]] Vec3 forward() const;
  [[nodiscard]] Vec3 right() const;  // horizontal, unit
  [[nodiscard]] Vec3 up() const;     // the camera's up, unit
  [[nodiscard]] Vec3 pivot() const;

  // Right-button drag: turns about the eye. A drag right looks right, a drag
  // down (positive dy) looks down.
  void look(float dxPixels, float dyPixels);
  // Alt + left drag: the same turn, about the pivot (the eye moves).
  void orbit(float dxPixels, float dyPixels);
  // Middle drag: eye and pivot move together so the point under the pointer at
  // the pivot's depth follows the pointer.
  void pan(float dxPixels, float dyPixels, float viewportHeightPixels);
  // The wheel: positive notches move toward the pivot.
  void dolly(float notches);
  // Alt + right drag: a drag down (positive dy) moves toward the pivot.
  void dollyDrag(float dyPixels);
  // Right button held + W/A/S/D/Q/E: `direction` is (right, world-up, forward)
  // in [-1, 1] each; moves fly speed (x kFastFlyFactor when `fast`) per second.
  void fly(const Vec3& direction, float deltaSeconds, bool fast);
  // Right button held + wheel.
  void scaleFlySpeed(float notches);
  // F: the pivot moves to `center` and, when radius > 0, the distance becomes
  // the one at which a sphere of that radius fits the vertical field of view.
  void frame(const Vec3& center, float radius);

 private:
  void setDirection(float yaw, float pitch);

  std::optional<Mat4> resetWorldMatrix_;  // cleared by every navigation
  Vec3 eye_{0.0f, 0.0f, 0.0f};
  float yaw_ = 0.0f;
  float pitch_ = 0.0f;
  float pivotDistance_ = kDefaultPivotDistance;
  float flySpeed_ = kDefaultFlySpeed;
  CameraLens lens_;
};

// Plan 0062 P1/P7 (Spec 0062 R1, R3; rulings Q6 D-a): which camera the
// Viewport shows -- the scene's active camera, or the editor camera.
enum class ViewportCameraMode : std::uint8_t { Scene, Editor };

// The Viewport's camera: the mode, the editor camera, and the scene camera's
// pose as the host last gave it. The editor camera starts in Editor mode and
// is initialized from the first scene pose it receives, so the first image is
// the scene camera's; until then the Viewport shows the scene camera. Not
// thread-safe.
class ViewportCameraState {
 public:
  // Every frame: the scene camera's pose, or none if the host has none.
  void setScenePose(const std::optional<CameraPose>& pose);
  [[nodiscard]] const std::optional<CameraPose>& scenePose() const noexcept { return scenePose_; }

  [[nodiscard]] ViewportCameraMode mode() const noexcept { return mode_; }
  void setMode(ViewportCameraMode mode) noexcept { mode_ = mode; }
  // Re-initializes the editor camera from the scene camera (no-op without one).
  void align();
  // Before a navigation input acts: in Scene mode, switches to Editor mode
  // starting from the scene camera's pose (R2).
  void beginNavigation();

  [[nodiscard]] ViewportCameraModel& editor() noexcept { return editor_; }
  [[nodiscard]] const ViewportCameraModel& editor() const noexcept { return editor_; }
  [[nodiscard]] bool editorInitialized() const noexcept { return initialized_; }

  // The pose the Viewport is drawn from: the editor camera's in Editor mode
  // once initialized, otherwise the scene camera's (none without one).
  [[nodiscard]] std::optional<CameraPose> pose() const;
  // What the host is handed: the editor camera's pose in Editor mode once
  // initialized; none (the scene camera) otherwise.
  [[nodiscard]] std::optional<CameraPose> override() const;
  [[nodiscard]] std::optional<CameraMatrices> matrices(float aspect) const;

 private:
  ViewportCameraMode mode_ = ViewportCameraMode::Editor;
  ViewportCameraModel editor_;
  bool initialized_ = false;
  std::optional<CameraPose> scenePose_;
};

}  // namespace atlantis::editor
