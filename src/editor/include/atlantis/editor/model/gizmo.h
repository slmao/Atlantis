#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/editor/model/viewport_camera.h>
#include <atlantis/editor/model/viewport_picking.h>
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
// Plan 0062 P9 (Spec 0062 R7): the axes Translate and Rotate use -- the
// world's, or the entity's own (its normalized rotation columns). Scale always
// uses the entity's own axes.
enum class GizmoSpace : std::uint8_t { World, Local };

// Plan 0062 P9: snapping steps, measured from the drag's start (Ctrl held).
inline constexpr float kTranslateSnap = 0.5f;          // world units
inline constexpr float kRotateSnapRadians = 0.2617994f;  // 15 degrees
inline constexpr float kScaleSnap = 0.1f;              // of the factor

// Plan 0068 M1 (ADR-0137 D1, D3): the Gizmo's math as pure functions -- what
// GizmoModel computes, and the screen handles the ImGui view draws, hit-tests
// and drags, moved here unchanged so that a C++ test can generate the vectors
// Atlantis Editor App's C# port is held to.

// The world-space unit direction of `axis` for `mode` and `space`: a world axis
// (Translate, Rotate in World), or the entity's own axis from `trs` (its
// rotation column, flipped for the x axis of a reflection).
[[nodiscard]] std::array<float, 3> gizmoAxisDirection(const Trs& trs, GizmoMode mode, GizmoSpace space, GizmoAxis axis);
// A drag amount rounded to its mode's step (kTranslateSnap, kRotateSnapRadians,
// kScaleSnap; a scale factor no smaller than one step).
[[nodiscard]] float snapGizmoAmount(GizmoMode mode, float amount);
// The matrix a drag of `amount` along `axis` gives from `start` (GizmoModel::
// dragTo()'s rule, snapping first when `snap`). Requires decompose(start).
[[nodiscard]] MatrixColumns gizmoDragMatrix(const MatrixColumns& start, GizmoMode mode, GizmoSpace space,
                                            GizmoAxis axis, float amount, bool snap);

// The handles on screen (Spec 0062 R5): each axis from the projected origin
// along its projected direction, kGizmoHandlePixels long; a pointer within
// kGizmoPickPixels of a handle's segment -- of its end's ring of
// kGizmoRotateRingPixels for Rotate -- grabs it.
inline constexpr float kGizmoHandlePixels = 80.0f;
inline constexpr float kGizmoPickPixels = 8.0f;
inline constexpr float kGizmoRotateRingPixels = 9.0f;

// One axis handle in pixels: `direction` is a unit screen vector; `pixelsPerUnit`
// is how many pixels one world unit along the axis covers at the origin.
struct GizmoHandle {
  std::array<float, 2> origin{};
  std::array<float, 2> direction{};
  std::array<float, 2> end{};
  float pixelsPerUnit = 0.0f;
  friend bool operator==(const GizmoHandle&, const GizmoHandle&) = default;
};
using GizmoHandles = std::array<std::optional<GizmoHandle>, 3>;

// The handles of a Gizmo at `position` with the world `directions` of its three
// axes, drawn with `camera` into `rect`. Empty when `position` is behind the
// camera; an axis whose tip is behind the camera, or projects within 1e-3 pixel
// of the origin, has no handle.
[[nodiscard]] std::optional<GizmoHandles> gizmoHandles(const CameraMatrices& camera, const PixelRect& rect,
                                                       const Vec3& position, const std::array<Vec3, 3>& directions);

// Whether a world direction points toward the camera (view space looks down -z).
[[nodiscard]] bool gizmoAxisFacesCamera(const CameraMatrices& camera, const Vec3& direction);

// A grabbed handle: the axis, the pointer where it was grabbed, the handle, the
// pointer's angle about the origin then, and whether the axis faced the camera.
struct GizmoGrab {
  GizmoAxis axis = GizmoAxis::X;
  std::array<float, 2> start{};
  GizmoHandle handle;
  float startAngle = 0.0f;
  bool towardCamera = false;
  friend bool operator==(const GizmoGrab&, const GizmoGrab&) = default;
};

// The handle a press at `pointer` grabs: the nearest within kGizmoPickPixels
// (a later axis wins a tie), or none.
[[nodiscard]] std::optional<GizmoGrab> gizmoGrab(const GizmoHandles& handles, const std::array<Vec3, 3>& directions,
                                                 const CameraMatrices& camera, GizmoMode mode,
                                                 const std::array<float, 2>& pointer);

// A grabbed drag's amount for GizmoModel::dragTo() at `pointer`: Translate --
// world units (the motion along the handle over its pixels per unit); Scale --
// a factor (1 plus that motion over kGizmoHandlePixels); Rotate -- radians (the
// pointer's turn about the origin, negated when the axis faces the camera, as
// screen y points down).
[[nodiscard]] float gizmoDragAmount(GizmoMode mode, const GizmoGrab& grab, const std::array<float, 2>& pointer);

// Plan 0063 P5 (Spec 0063 R2): a drag, begin to release, as one edit -- its
// subject, mode, the WorldMatrix columns' addresses, the matrix at the drag's
// start and at its release, and the release transaction's ticket.
struct CompletedDrag {
  atlantis::asset_system::EntityGuid subject;
  GizmoMode mode = GizmoMode::Translate;
  std::array<atlantis::world::access::PropertyAddress, 4> addresses{};
  MatrixColumns start{};
  MatrixColumns final{};
  atlantis::world::access::TransactionTicket ticket;
};

// Borrows the connection, which must outlive it. Frame thread only, between
// Runtime frames (ADR-0105); not thread-safe.
class GizmoModel {
 public:
  explicit GizmoModel(atlantis::connection::RuntimeConnection& connection);

  void setSubject(std::optional<atlantis::asset_system::EntityGuid> entity);
  [[nodiscard]] const std::optional<atlantis::asset_system::EntityGuid>& subject() const noexcept {
    return subject_;
  }

  // Between frames, unless dragging or a released drag is still to be
  // flushed: re-reads the subject's WorldMatrix.
  void update();

  // The matrix the Gizmo shows -- while dragging, the drag's latest state.
  // Empty when the subject has no WorldMatrix.
  [[nodiscard]] const std::optional<MatrixColumns>& matrix() const noexcept { return matrix_; }
  // A Gizmo is shown: the subject's WorldMatrix decomposes (not sheared).
  [[nodiscard]] bool available() const;

  [[nodiscard]] GizmoMode mode() const noexcept { return mode_; }
  void setMode(GizmoMode mode);

  // Plan 0062 P9: World or Local (not while dragging), and snapping, which
  // applies to the amounts dragTo() receives from then on.
  [[nodiscard]] GizmoSpace space() const noexcept { return space_; }
  void setSpace(GizmoSpace space);
  [[nodiscard]] bool snap() const noexcept { return snap_; }
  void setSnap(bool snap) noexcept { snap_ = snap; }

  // The world-space unit direction of `axis` for the current mode and space:
  // a world axis (Translate, Rotate in World), or the entity's own axis --
  // from the drag's start while dragging. Requires available().
  [[nodiscard]] std::array<float, 3> axisDirection(GizmoAxis axis) const;

  [[nodiscard]] bool dragging() const noexcept { return dragAxis_.has_value(); }
  [[nodiscard]] std::optional<GizmoAxis> dragAxis() const noexcept { return dragAxis_; }

  // Starts a drag along an axis (axisDirection()), from the current matrix.
  // Requires available() and no drag in progress (programmer error otherwise).
  void beginDrag(GizmoAxis axis);
  // The drag's amount, from its start: Translate -- world units along the
  // axis direction (only column3 changes, the other columns bit for bit as
  // they were); Rotate -- radians about the axis direction; Scale -- a factor
  // on the entity's own axis of the same index (its local axes, so a rotated
  // entity gains no shear). With snapping, the amount is first rounded to its
  // step (kTranslateSnap, kRotateSnapRadians, kScaleSnap; a scale factor no
  // smaller than one step). A non-finite amount is ignored.
  void dragTo(float amount);
  void endDrag();

  // Once per editor frame: submits one transaction of SetProperty column0..
  // column3 when the drag state changed since the last submission, or when a
  // drag ended (its final value, always). Returns its ticket, if any.
  std::optional<atlantis::world::access::TransactionTicket> flush();

  // Plan 0063 P5: the drag whose release flush() last submitted, once, if its
  // final matrix differs from its start; preview flushes never set it.
  [[nodiscard]] std::optional<CompletedDrag> takeCompletedDrag();

 private:
  atlantis::connection::RuntimeConnection& connection_;
  std::optional<schema::TypeId> worldMatrixType_;
  std::array<schema::FieldId, 4> columnFields_{};
  std::optional<atlantis::asset_system::EntityGuid> subject_;
  std::optional<MatrixColumns> matrix_;
  GizmoMode mode_ = GizmoMode::Translate;
  GizmoSpace space_ = GizmoSpace::World;
  bool snap_ = false;
  std::optional<GizmoAxis> dragAxis_;
  MatrixColumns dragStart_{};
  Trs dragStartTrs_;
  bool dirty_ = false;       // the drag state changed since the last flush()
  bool released_ = false;    // a drag ended since the last flush()
  std::optional<CompletedDrag> completed_;
};

}  // namespace atlantis::editor
