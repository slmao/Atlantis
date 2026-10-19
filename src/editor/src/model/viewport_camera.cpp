#include <atlantis/editor/model/viewport_camera.h>

#include <algorithm>
#include <cmath>

namespace atlantis::editor {

namespace {

// The host's degeneracy threshold (scene_extraction.cpp, kDegenerateLengthEpsilon).
constexpr float kDegenerateLengthEpsilon = 1e-6f;

[[nodiscard]] float length(const Vec3& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
[[nodiscard]] Vec3 add(const Vec3& a, const Vec3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
[[nodiscard]] Vec3 scale(const Vec3& v, float s) { return {v[0] * s, v[1] * s, v[2] * s}; }

// The host's lookAtMatrix(), operation for operation (scene_extraction.cpp):
// the convention test holds the two equal.
[[nodiscard]] Mat4 lookAt(float eyeX, float eyeY, float eyeZ, float centerX, float centerY, float centerZ) {
  float fx = centerX - eyeX, fy = centerY - eyeY, fz = centerZ - eyeZ;
  const float fLen = std::sqrt(fx * fx + fy * fy + fz * fz);
  fx /= fLen;
  fy /= fLen;
  fz /= fLen;
  const float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
  float sx = fy * upZ - fz * upY;
  float sy = fz * upX - fx * upZ;
  float sz = fx * upY - fy * upX;
  const float sLen = std::sqrt(sx * sx + sy * sy + sz * sz);
  sx /= sLen;
  sy /= sLen;
  sz /= sLen;
  const float ux = sy * fz - sz * fy;
  const float uy = sz * fx - sx * fz;
  const float uz = sx * fy - sy * fx;
  Mat4 result{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  result[0] = sx;
  result[4] = sy;
  result[8] = sz;
  result[1] = ux;
  result[5] = uy;
  result[9] = uz;
  result[2] = -fx;
  result[6] = -fy;
  result[10] = -fz;
  result[12] = -(sx * eyeX + sy * eyeY + sz * eyeZ);
  result[13] = -(ux * eyeX + uy * eyeY + uz * eyeZ);
  result[14] = fx * eyeX + fy * eyeY + fz * eyeZ;
  return result;
}

// The host's perspectiveMatrix(): right-handed, depth [0, 1], y flipped.
[[nodiscard]] Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float f = 1.0f / std::tan(fovYRadians * 0.5f);
  Mat4 result{};
  result[0] = f / aspect;
  result[5] = -f;
  result[10] = farZ / (nearZ - farZ);
  result[11] = -1.0f;
  result[14] = (nearZ * farZ) / (nearZ - farZ);
  return result;
}

}  // namespace

std::optional<CameraMatrices> cameraMatrices(const CameraPose& pose, float aspect) {
  const Mat4& m = pose.worldMatrix;
  const Vec3 negColumn2{-m[8], -m[9], -m[10]};
  const float forwardLen = length(negColumn2);
  if (forwardLen < kDegenerateLengthEpsilon) return std::nullopt;
  const Vec3 forward{negColumn2[0] / forwardLen, negColumn2[1] / forwardLen, negColumn2[2] / forwardLen};
  const Vec3 eye{m[12], m[13], m[14]};
  if (length(cross(forward, Vec3{0.0f, 1.0f, 0.0f})) < kDegenerateLengthEpsilon) return std::nullopt;
  CameraMatrices result;
  result.view = lookAt(eye[0], eye[1], eye[2], eye[0] + forward[0], eye[1] + forward[1], eye[2] + forward[2]);
  result.projection = perspective(pose.lens.fovYRadians, aspect, pose.lens.nearZ, pose.lens.farZ);
  return result;
}

std::optional<std::array<float, 2>> projectToNdc(const CameraMatrices& camera, const Vec3& point) {
  std::array<float, 4> view{};
  for (int row = 0; row < 4; ++row) {
    view[row] = camera.view[0 * 4 + row] * point[0] + camera.view[1 * 4 + row] * point[1] +
                camera.view[2 * 4 + row] * point[2] + camera.view[3 * 4 + row];
  }
  std::array<float, 4> clip{};
  for (int row = 0; row < 4; ++row) {
    clip[row] = camera.projection[0 * 4 + row] * view[0] + camera.projection[1 * 4 + row] * view[1] +
                camera.projection[2 * 4 + row] * view[2] + camera.projection[3 * 4 + row] * view[3];
  }
  if (!(clip[3] > 1e-6f)) return std::nullopt;
  return std::array<float, 2>{clip[0] / clip[3], clip[1] / clip[3]};
}

float viewDepth(const CameraMatrices& camera, const Vec3& point) {
  return -(camera.view[2] * point[0] + camera.view[6] * point[1] + camera.view[10] * point[2] + camera.view[14]);
}

Ray rayThroughNdc(const CameraMatrices& camera, float ndcX, float ndcY) {
  // x_ndc = p0 * x / -z and y_ndc = p5 * y / -z: the view-space direction at
  // z = -1 is (ndcX / p0, ndcY / p5, -1). The view is rigid, so its inverse
  // rotation is its transpose: world_i = sum_r view[i*4 + r] * v_r.
  const Vec3 v{ndcX / camera.projection[0], ndcY / camera.projection[5], -1.0f};
  const Mat4& m = camera.view;
  Vec3 direction{};
  Vec3 origin{};
  for (int i = 0; i < 3; ++i) {
    direction[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2];
    origin[i] = -(m[i * 4 + 0] * m[12] + m[i * 4 + 1] * m[13] + m[i * 4 + 2] * m[14]);
  }
  const float len = length(direction);
  return Ray{origin, scale(direction, 1.0f / len)};
}

void ViewportCameraModel::reset(const CameraPose& pose) {
  const Mat4& m = pose.worldMatrix;
  resetWorldMatrix_.reset();
  eye_ = {m[12], m[13], m[14]};
  lens_ = pose.lens;
  const Vec3 negColumn2{-m[8], -m[9], -m[10]};
  const float len = length(negColumn2);
  if (!(len > kDegenerateLengthEpsilon) || !std::isfinite(len)) {
    setDirection(0.0f, 0.0f);
    return;
  }
  const Vec3 f = scale(negColumn2, 1.0f / len);
  const float pitch = std::asin(std::clamp(f[1], -1.0f, 1.0f));
  setDirection(std::atan2(-f[0], -f[2]), pitch);
  if (pitch == pitch_) resetWorldMatrix_ = m;
}

void ViewportCameraModel::setDirection(float yaw, float pitch) {
  constexpr float kTwoPi = 6.2831853f;
  yaw_ = std::remainder(yaw, kTwoPi);
  pitch_ = std::clamp(pitch, -kMaxPitchRadians, kMaxPitchRadians);
}

Vec3 ViewportCameraModel::forward() const {
  const float c = std::cos(pitch_);
  return {-std::sin(yaw_) * c, std::sin(pitch_), -std::cos(yaw_) * c};
}

Vec3 ViewportCameraModel::right() const { return {std::cos(yaw_), 0.0f, -std::sin(yaw_)}; }

Vec3 ViewportCameraModel::up() const { return cross(right(), forward()); }

Vec3 ViewportCameraModel::pivot() const { return add(eye_, scale(forward(), pivotDistance_)); }

CameraPose ViewportCameraModel::pose() const {
  if (resetWorldMatrix_.has_value()) return CameraPose{*resetWorldMatrix_, lens_};
  const Vec3 f = forward();
  const Vec3 r = right();
  const Vec3 u = cross(r, f);
  CameraPose pose;
  pose.worldMatrix = {r[0], r[1], r[2], 0.0f, u[0], u[1], u[2], 0.0f,
                      -f[0], -f[1], -f[2], 0.0f, eye_[0], eye_[1], eye_[2], 1.0f};
  pose.lens = lens_;
  return pose;
}

void ViewportCameraModel::look(float dxPixels, float dyPixels) {
  resetWorldMatrix_.reset();
  setDirection(yaw_ - dxPixels * kLookRadiansPerPixel, pitch_ - dyPixels * kLookRadiansPerPixel);
}

void ViewportCameraModel::orbit(float dxPixels, float dyPixels) {
  resetWorldMatrix_.reset();
  const Vec3 center = pivot();
  setDirection(yaw_ - dxPixels * kOrbitRadiansPerPixel, pitch_ - dyPixels * kOrbitRadiansPerPixel);
  eye_ = add(center, scale(forward(), -pivotDistance_));
}

void ViewportCameraModel::pan(float dxPixels, float dyPixels, float viewportHeightPixels) {
  resetWorldMatrix_.reset();
  if (!(viewportHeightPixels > 0.0f)) return;
  // World units per pixel at the pivot's depth.
  const float unitsPerPixel = 2.0f * pivotDistance_ * std::tan(lens_.fovYRadians * 0.5f) / viewportHeightPixels;
  eye_ = add(eye_, add(scale(right(), -dxPixels * unitsPerPixel), scale(up(), dyPixels * unitsPerPixel)));
}

void ViewportCameraModel::dolly(float notches) {
  resetWorldMatrix_.reset();
  const Vec3 center = pivot();
  pivotDistance_ = std::max(kMinPivotDistance, pivotDistance_ * std::pow(kWheelDollyFactor, notches));
  eye_ = add(center, scale(forward(), -pivotDistance_));
}

void ViewportCameraModel::dollyDrag(float dyPixels) {
  resetWorldMatrix_.reset();
  const Vec3 center = pivot();
  pivotDistance_ = std::max(kMinPivotDistance, pivotDistance_ * std::exp(-kDragDollyPerPixel * dyPixels));
  eye_ = add(center, scale(forward(), -pivotDistance_));
}

void ViewportCameraModel::fly(const Vec3& direction, float deltaSeconds, bool fast) {
  resetWorldMatrix_.reset();
  if (!(deltaSeconds > 0.0f)) return;
  const float distance = flySpeed_ * (fast ? kFastFlyFactor : 1.0f) * deltaSeconds;
  const Vec3 move = add(add(scale(right(), direction[0]), scale(Vec3{0.0f, 1.0f, 0.0f}, direction[1])),
                        scale(forward(), direction[2]));
  eye_ = add(eye_, scale(move, distance));
}

void ViewportCameraModel::scaleFlySpeed(float notches) {
  flySpeed_ = std::clamp(flySpeed_ * std::pow(kFlySpeedStep, notches), kMinFlySpeed, kMaxFlySpeed);
}

void ViewportCameraModel::frame(const Vec3& center, float radius) {
  resetWorldMatrix_.reset();
  if (radius > 0.0f && std::isfinite(radius)) {
    pivotDistance_ = std::max(kMinPivotDistance, radius / std::sin(lens_.fovYRadians * 0.5f));
  }
  eye_ = add(center, scale(forward(), -pivotDistance_));
}

void ViewportCameraState::setScenePose(const std::optional<CameraPose>& pose) {
  scenePose_ = pose;
  if (!initialized_ && scenePose_.has_value()) {
    editor_.reset(*scenePose_);
    initialized_ = true;
  }
}

void ViewportCameraState::align() {
  if (!scenePose_.has_value()) return;
  editor_.reset(*scenePose_);
  initialized_ = true;
}

void ViewportCameraState::beginNavigation() {
  if (mode_ == ViewportCameraMode::Editor) return;
  align();
  mode_ = ViewportCameraMode::Editor;
}

std::optional<CameraPose> ViewportCameraState::pose() const {
  if (const std::optional<CameraPose> editor = override(); editor.has_value()) return editor;
  return scenePose_;
}

std::optional<CameraPose> ViewportCameraState::override() const {
  if (mode_ != ViewportCameraMode::Editor || !initialized_) return std::nullopt;
  return editor_.pose();
}

std::optional<CameraMatrices> ViewportCameraState::matrices(float aspect) const {
  const std::optional<CameraPose> drawn = pose();
  if (!drawn.has_value()) return std::nullopt;
  return cameraMatrices(*drawn, aspect);
}

}  // namespace atlantis::editor
