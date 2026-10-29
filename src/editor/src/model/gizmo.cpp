#include <atlantis/editor/model/gizmo.h>

#include <atlantis/assert.h>
#include <atlantis/connection/text.h>
#include <atlantis/editor/model/viewport_camera.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace atlantis::editor {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
using atlantis::asset_system::EntityGuid;

namespace {

[[nodiscard]] float dot(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
[[nodiscard]] Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}

// The rotation by `angle` radians about world axis `axis`, column-major.
[[nodiscard]] std::array<float, 9> axisRotation(GizmoAxis axis, float angle) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  switch (axis) {
    case GizmoAxis::X: return {1.0f, 0.0f, 0.0f, 0.0f, c, s, 0.0f, -s, c};
    case GizmoAxis::Y: return {c, 0.0f, -s, 0.0f, 1.0f, 0.0f, s, 0.0f, c};
    case GizmoAxis::Z: return {c, s, 0.0f, -s, c, 0.0f, 0.0f, 0.0f, 1.0f};
  }
  return {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
}

// a * b, both column-major 3x3.
[[nodiscard]] std::array<float, 9> multiply(const std::array<float, 9>& a, const std::array<float, 9>& b) {
  std::array<float, 9> out{};
  for (int column = 0; column < 3; ++column) {
    for (int row = 0; row < 3; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 3; ++k) sum += a[k * 3 + row] * b[column * 3 + k];
      out[column * 3 + row] = sum;
    }
  }
  return out;
}

}  // namespace

std::optional<Trs> decompose(const MatrixColumns& columns) {
  if (columns[0][3] != 0.0f || columns[1][3] != 0.0f || columns[2][3] != 0.0f || columns[3][3] != 1.0f) {
    return std::nullopt;
  }
  Trs trs;
  trs.translation = {columns[3][0], columns[3][1], columns[3][2]};
  std::array<Vec3, 3> axes{};
  for (int k = 0; k < 3; ++k) {
    const Vec3 column{columns[k][0], columns[k][1], columns[k][2]};
    const float length = std::sqrt(dot(column, column));
    if (!(length > 1e-8f) || !std::isfinite(length)) return std::nullopt;
    trs.scale[k] = length;
    axes[k] = {column[0] / length, column[1] / length, column[2] / length};
  }
  if (std::fabs(dot(axes[0], axes[1])) > kShearTolerance || std::fabs(dot(axes[0], axes[2])) > kShearTolerance ||
      std::fabs(dot(axes[1], axes[2])) > kShearTolerance) {
    return std::nullopt;
  }
  if (dot(axes[0], cross(axes[1], axes[2])) < 0.0f) {  // a reflection: carried by the x scale
    trs.scale[0] = -trs.scale[0];
    axes[0] = {-axes[0][0], -axes[0][1], -axes[0][2]};
  }
  for (int k = 0; k < 3; ++k) {
    for (int i = 0; i < 3; ++i) trs.rotation[k * 3 + i] = axes[k][i];
  }
  return trs;
}

MatrixColumns compose(const Trs& trs) {
  MatrixColumns columns{};
  for (int k = 0; k < 3; ++k) {
    for (int i = 0; i < 3; ++i) columns[k][i] = trs.rotation[k * 3 + i] * trs.scale[k];
    columns[k][3] = 0.0f;
  }
  columns[3] = {trs.translation[0], trs.translation[1], trs.translation[2], 1.0f};
  return columns;
}

// The WorldMatrix component and its column fields, found in the schema by
// name -- the model names no World type.
GizmoModel::GizmoModel(connection::RuntimeConnection& connection) : connection_(connection) {
  for (const schema::TypeDescriptor& type : connection_.schema()) {
    if (connection::text::shortName(type.name) != "WorldMatrix") continue;
    int found = 0;
    for (const schema::FieldDescriptor& field : type.fields) {
      for (int k = 0; k < 4; ++k) {
        if (field.name == "column" + std::to_string(k)) {
          columnFields_[static_cast<std::size_t>(k)] = field.id;
          ++found;
        }
      }
    }
    if (found == 4) worldMatrixType_ = type.id;
  }
}

void GizmoModel::setSubject(std::optional<EntityGuid> entity) {
  if (entity == subject_) return;
  subject_ = entity;
  dragAxis_.reset();
  dirty_ = false;
  released_ = false;
  matrix_.reset();
  update();
}

void GizmoModel::update() {
  // A released drag keeps its final matrix until flush() submits it (Plan
  // 0063 P5): an endDrag() made between frames must not be overwritten by
  // the World's last preview.
  if (dragging() || released_) return;
  matrix_.reset();
  if (!subject_.has_value() || !worldMatrixType_.has_value()) return;
  MatrixColumns columns{};
  for (std::size_t k = 0; k < 4; ++k) {
    auto value = connection_.getProperty(access::PropertyAddress{*subject_, *worldMatrixType_, columnFields_[k]});
    if (value.isErr()) return;
    const auto* column = std::get_if<std::array<float, 4>>(&value.value());
    if (column == nullptr) return;
    columns[k] = *column;
  }
  matrix_ = columns;
}

bool GizmoModel::available() const { return matrix_.has_value() && decompose(*matrix_).has_value(); }

void GizmoModel::setMode(GizmoMode mode) {
  if (!dragging()) mode_ = mode;
}

void GizmoModel::setSpace(GizmoSpace space) {
  if (!dragging()) space_ = space;
}

std::array<float, 3> gizmoAxisDirection(const Trs& trs, GizmoMode mode, GizmoSpace space, GizmoAxis axis) {
  const auto k = static_cast<std::size_t>(axis);
  const bool local = mode == GizmoMode::Scale || space == GizmoSpace::Local;
  if (!local) {
    Vec3 unit{0.0f, 0.0f, 0.0f};
    unit[k] = 1.0f;
    return unit;
  }
  // The rotation column; a reflection (a negative x scale) flips the x axis
  // the entity's own frame points along.
  const float sign = k == 0 && trs.scale[0] < 0.0f ? -1.0f : 1.0f;
  return {trs.rotation[k * 3 + 0] * sign, trs.rotation[k * 3 + 1] * sign, trs.rotation[k * 3 + 2] * sign};
}

float snapGizmoAmount(GizmoMode mode, float amount) {
  switch (mode) {
    case GizmoMode::Translate: return std::round(amount / kTranslateSnap) * kTranslateSnap;
    case GizmoMode::Rotate: return std::round(amount / kRotateSnapRadians) * kRotateSnapRadians;
    case GizmoMode::Scale: return std::fmax(std::round(amount / kScaleSnap) * kScaleSnap, kScaleSnap);
  }
  return amount;
}

MatrixColumns gizmoDragMatrix(const MatrixColumns& start, GizmoMode mode, GizmoSpace space, GizmoAxis axis,
                              float amount, bool snap) {
  const std::optional<Trs> startTrs = decompose(start);
  ATLANTIS_CHECK_MSG(startTrs.has_value(), "gizmoDragMatrix(): the start matrix has no TRS form");
  if (!startTrs.has_value()) return start;
  const auto k = static_cast<std::size_t>(axis);
  if (snap) amount = snapGizmoAmount(mode, amount);
  switch (mode) {
    case GizmoMode::Translate: {
      MatrixColumns columns = start;
      if (space == GizmoSpace::World) {
        columns[3][k] = start[3][k] + amount;
      } else {
        const Vec3 direction = gizmoAxisDirection(*startTrs, mode, space, axis);
        for (std::size_t i = 0; i < 3; ++i) columns[3][i] = start[3][i] + direction[i] * amount;
      }
      return columns;
    }
    case GizmoMode::Rotate: {
      Trs trs = *startTrs;
      // World: about the world axis (R_axis * R). Local: about the entity's own
      // axis (R * R_axis), which leaves that axis where it was.
      trs.rotation = space == GizmoSpace::World ? multiply(axisRotation(axis, amount), startTrs->rotation)
                                                : multiply(startTrs->rotation, axisRotation(axis, amount));
      return compose(trs);
    }
    case GizmoMode::Scale: {
      Trs trs = *startTrs;
      trs.scale[k] = startTrs->scale[k] * std::fmax(amount, 1e-4f);
      return compose(trs);
    }
  }
  return start;
}

std::optional<GizmoHandles> gizmoHandles(const CameraMatrices& camera, const PixelRect& rect, const Vec3& position,
                                         const std::array<Vec3, 3>& directions) {
  const std::optional<std::array<float, 2>> center = pixelOf(camera, rect, position);
  if (!center.has_value()) return std::nullopt;
  GizmoHandles handles;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    const Vec3 unit{position[0] + directions[axis][0], position[1] + directions[axis][1],
                    position[2] + directions[axis][2]};
    const std::optional<std::array<float, 2>> tip = pixelOf(camera, rect, unit);
    if (!tip.has_value()) continue;
    const std::array<float, 2> delta{(*tip)[0] - (*center)[0], (*tip)[1] - (*center)[1]};
    const float pixels = std::sqrt(delta[0] * delta[0] + delta[1] * delta[1]);
    if (pixels < 1e-3f) continue;
    const std::array<float, 2> direction{delta[0] / pixels, delta[1] / pixels};
    handles[axis] = GizmoHandle{*center, direction,
                                {(*center)[0] + direction[0] * kGizmoHandlePixels,
                                 (*center)[1] + direction[1] * kGizmoHandlePixels},
                                pixels};
  }
  return handles;
}

bool gizmoAxisFacesCamera(const CameraMatrices& camera, const Vec3& direction) {
  return camera.view[2] * direction[0] + camera.view[6] * direction[1] + camera.view[10] * direction[2] > 0.0f;
}

namespace {

[[nodiscard]] float pixelLength(const std::array<float, 2>& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1]); }

[[nodiscard]] float distanceToSegment(const std::array<float, 2>& p, const std::array<float, 2>& a,
                                      const std::array<float, 2>& b) {
  const std::array<float, 2> ab{b[0] - a[0], b[1] - a[1]};
  const float lengthSquared = ab[0] * ab[0] + ab[1] * ab[1];
  float t = lengthSquared > 0.0f ? ((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / lengthSquared : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  return pixelLength({p[0] - (a[0] + ab[0] * t), p[1] - (a[1] + ab[1] * t)});
}

}  // namespace

std::optional<GizmoGrab> gizmoGrab(const GizmoHandles& handles, const std::array<Vec3, 3>& directions,
                                   const CameraMatrices& camera, GizmoMode mode, const std::array<float, 2>& pointer) {
  std::optional<GizmoGrab> grab;
  float best = kGizmoPickPixels;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!handles[axis].has_value()) continue;
    const GizmoHandle& handle = *handles[axis];
    const float distance =
        mode == GizmoMode::Rotate
            ? std::fabs(pixelLength({pointer[0] - handle.end[0], pointer[1] - handle.end[1]}) - kGizmoRotateRingPixels)
            : distanceToSegment(pointer, handle.origin, handle.end);
    if (distance <= best) {
      best = distance;
      grab = GizmoGrab{static_cast<GizmoAxis>(axis), pointer, handle,
                       std::atan2(pointer[1] - handle.origin[1], pointer[0] - handle.origin[0]),
                       gizmoAxisFacesCamera(camera, directions[axis])};
    }
  }
  return grab;
}

float gizmoDragAmount(GizmoMode mode, const GizmoGrab& grab, const std::array<float, 2>& pointer) {
  const std::array<float, 2> moved{pointer[0] - grab.start[0], pointer[1] - grab.start[1]};
  const float along = moved[0] * grab.handle.direction[0] + moved[1] * grab.handle.direction[1];
  switch (mode) {
    case GizmoMode::Translate: return along / grab.handle.pixelsPerUnit;
    case GizmoMode::Scale: return 1.0f + along / kGizmoHandlePixels;
    case GizmoMode::Rotate: {
      // Screen y points down: a positive (right-handed) rotation about an
      // axis facing the camera turns counter-clockwise on screen, i.e. by a
      // decreasing screen angle.
      const float angle = std::atan2(pointer[1] - grab.handle.origin[1], pointer[0] - grab.handle.origin[0]) -
                          grab.startAngle;
      return grab.towardCamera ? -angle : angle;
    }
  }
  return 0.0f;
}

std::array<float, 3> GizmoModel::axisDirection(GizmoAxis axis) const {
  if (mode_ != GizmoMode::Scale && space_ == GizmoSpace::World) return gizmoAxisDirection(Trs{}, mode_, space_, axis);
  const std::optional<Trs> trs = dragging() ? std::optional<Trs>(dragStartTrs_)
                                            : (matrix_.has_value() ? decompose(*matrix_) : std::nullopt);
  ATLANTIS_CHECK_MSG(trs.has_value(), "GizmoModel::axisDirection(): no Gizmo");
  if (!trs.has_value()) return {0.0f, 0.0f, 0.0f};
  return gizmoAxisDirection(*trs, mode_, space_, axis);
}

void GizmoModel::beginDrag(GizmoAxis axis) {
  ATLANTIS_CHECK_MSG(available() && !dragging(), "GizmoModel::beginDrag(): no Gizmo, or a drag in progress");
  if (!available() || dragging()) return;
  dragAxis_ = axis;
  dragStart_ = *matrix_;
  dragStartTrs_ = *decompose(dragStart_);
}

void GizmoModel::dragTo(float amount) {
  if (!dragging() || !std::isfinite(amount)) return;
  matrix_ = gizmoDragMatrix(dragStart_, mode_, space_, *dragAxis_, amount, snap_);
  dirty_ = true;
}

void GizmoModel::endDrag() {
  if (!dragging()) return;
  dragAxis_.reset();
  released_ = true;
}

std::optional<access::TransactionTicket> GizmoModel::flush() {
  if ((!dirty_ && !released_) || !subject_.has_value() || !matrix_.has_value() || !worldMatrixType_.has_value()) {
    return std::nullopt;
  }
  std::vector<access::Command> commands;
  std::array<access::PropertyAddress, 4> addresses{};
  for (std::size_t k = 0; k < 4; ++k) {
    addresses[k] = access::PropertyAddress{*subject_, *worldMatrixType_, columnFields_[k]};
    commands.emplace_back(access::SetProperty{addresses[k], access::PropertyValue{(*matrix_)[k]}});
  }
  const bool release = released_;
  dirty_ = false;
  released_ = false;
  const access::TransactionTicket ticket = connection_.submitTransaction(std::move(commands));
  // Plan 0063 P5: the release is the drag's one edit; previews are not.
  if (release && *matrix_ != dragStart_) completed_ = CompletedDrag{*subject_, mode_, addresses, dragStart_, *matrix_, ticket};
  return ticket;
}

std::optional<CompletedDrag> GizmoModel::takeCompletedDrag() { return std::exchange(completed_, std::nullopt); }

}  // namespace atlantis::editor
