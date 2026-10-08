#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

// Plan 0056 P9 (Spec 0056 R5, ruling Q5): the Transform Gizmo model. It edits
// the selected entity's WorldMatrix -- the render-authoritative placement
// (Spec 0052 ruling Q8, T-a) -- decomposed and recomposed client-side, and
// writes all four columns as one transaction: at most one per editor frame
// while dragging, carrying the latest drag state, and the final value on
// release. UI-free: no UI-library header is included by any model file.
namespace atlantis::editor {

// WorldMatrix's four Vec4 fields, column0..column3 (column-major).
using MatrixColumns = std::array<std::array<float, 4>, 4>;

// translation, then a proper rotation (column-major 3x3, orthonormal,
// determinant +1), then a per-axis scale (a reflection is carried as a
// negative x scale): M = T * R * S.
struct Trs {
  std::array<float, 3> translation{};
  std::array<float, 9> rotation{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
};

// Two normalized basis columns whose |dot| exceeds this are sheared.
inline constexpr float kShearTolerance = 1e-4f;

// Empty when the matrix has no TRS form: sheared (kShearTolerance), a
// degenerate (near-zero) axis, or a bottom row other than (0, 0, 0, 1).
[[nodiscard]] std::optional<Trs> decompose(const MatrixColumns& columns);
[[nodiscard]] MatrixColumns compose(const Trs& trs);

enum class GizmoMode : std::uint8_t { Translate, Rotate, Scale };
enum class GizmoAxis : std::uint8_t { X, Y, Z };

// Borrows the connection, which must outlive it. Frame thread only, between
// Runtime frames (ADR-0105); not thread-safe.
class GizmoModel {
 public:
  explicit GizmoModel(atlantis::connection::RuntimeConnection& connection);

  void setSubject(std::optional<atlantis::asset_system::EntityGuid> entity);
  [[nodiscard]] const std::optional<atlantis::asset_system::EntityGuid>& subject() const noexcept {
    return subject_;
  }

  // Between frames, unless dragging: re-reads the subject's WorldMatrix.
  void update();

  // The matrix the Gizmo shows -- while dragging, the drag's latest state.
  // Empty when the subject has no WorldMatrix.
  [[nodiscard]] const std::optional<MatrixColumns>& matrix() const noexcept { return matrix_; }
  // A Gizmo is shown: the subject's WorldMatrix decomposes (not sheared).
  [[nodiscard]] bool available() const;

  [[nodiscard]] GizmoMode mode() const noexcept { return mode_; }
  void setMode(GizmoMode mode);

  [[nodiscard]] bool dragging() const noexcept { return dragAxis_.has_value(); }
  [[nodiscard]] std::optional<GizmoAxis> dragAxis() const noexcept { return dragAxis_; }

  // Starts a drag along a world axis, from the current matrix. Requires
  // available() and no drag in progress (programmer error otherwise).
  void beginDrag(GizmoAxis axis);
  // The drag's amount, from its start: Translate -- world units along the
  // axis (only column3 changes, the other columns bit for bit as they were);
  // Rotate -- radians about the world axis; Scale -- a factor on the
  // entity's own axis of the same index (its local axes, so a rotated entity
  // gains no shear). A non-finite amount is ignored.
  void dragTo(float amount);
  void endDrag();

  // Once per editor frame: submits one transaction of SetProperty column0..
  // column3 when the drag state changed since the last submission, or when a
  // drag ended (its final value, always). Returns its ticket, if any.
  std::optional<atlantis::world::access::TransactionTicket> flush();

 private:
  atlantis::connection::RuntimeConnection& connection_;
  std::optional<schema::TypeId> worldMatrixType_;
  std::array<schema::FieldId, 4> columnFields_{};
  std::optional<atlantis::asset_system::EntityGuid> subject_;
  std::optional<MatrixColumns> matrix_;
  GizmoMode mode_ = GizmoMode::Translate;
  std::optional<GizmoAxis> dragAxis_;
  MatrixColumns dragStart_{};
  Trs dragStartTrs_;
  bool dirty_ = false;       // the drag state changed since the last flush()
  bool released_ = false;    // a drag ended since the last flush()
};

}  // namespace atlantis::editor
