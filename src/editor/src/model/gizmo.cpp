#include <atlantis/editor/model/gizmo.h>

#include <atlantis/assert.h>
#include <atlantis/connection/text.h>

#include <cmath>
#include <string>
#include <variant>
#include <vector>

namespace atlantis::editor {

namespace access = atlantis::world::access;
namespace connection = atlantis::connection;
using atlantis::asset_system::EntityGuid;

namespace {

using Vec3 = std::array<float, 3>;

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
  if (dragging()) return;
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

void GizmoModel::beginDrag(GizmoAxis axis) {
  ATLANTIS_CHECK_MSG(available() && !dragging(), "GizmoModel::beginDrag(): no Gizmo, or a drag in progress");
  if (!available() || dragging()) return;
  dragAxis_ = axis;
  dragStart_ = *matrix_;
  dragStartTrs_ = *decompose(dragStart_);
}

void GizmoModel::dragTo(float amount) {
  if (!dragging() || !std::isfinite(amount)) return;
  const auto axis = static_cast<std::size_t>(*dragAxis_);
  switch (mode_) {
    case GizmoMode::Translate: {
      MatrixColumns columns = dragStart_;
      columns[3][axis] = dragStart_[3][axis] + amount;
      matrix_ = columns;
      break;
    }
    case GizmoMode::Rotate: {
      Trs trs = dragStartTrs_;
      trs.rotation = multiply(axisRotation(*dragAxis_, amount), dragStartTrs_.rotation);
      matrix_ = compose(trs);
      break;
    }
    case GizmoMode::Scale: {
      Trs trs = dragStartTrs_;
      trs.scale[axis] = dragStartTrs_.scale[axis] * std::fmax(amount, 1e-4f);
      matrix_ = compose(trs);
      break;
    }
  }
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
  for (std::size_t k = 0; k < 4; ++k) {
    commands.emplace_back(access::SetProperty{access::PropertyAddress{*subject_, *worldMatrixType_, columnFields_[k]},
                                              access::PropertyValue{(*matrix_)[k]}});
  }
  dirty_ = false;
  released_ = false;
  return connection_.submitTransaction(std::move(commands));
}

}  // namespace atlantis::editor
