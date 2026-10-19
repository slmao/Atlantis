#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/editor/model/hierarchy.h>
#include <atlantis/editor/model/viewport_camera.h>
#include <atlantis/schema.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// Plan 0062 P8 (Spec 0062 R4-R6; ADR-0123 D2, D3): Viewport picking on the
// editor's side -- pixels to rays, the markers of entities without geometry
// (picked here, client-only), and the pick request the host answers one
// editor frame later. UI-free: no UI-library header is included by any model
// file.
namespace atlantis::editor {

// The Viewport image's rectangle, in framebuffer pixels.
struct PixelRect {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

// A world point's pixel inside `rect`; empty behind the camera.
[[nodiscard]] std::optional<std::array<float, 2>> pixelOf(const CameraMatrices& camera, const PixelRect& rect,
                                                          const Vec3& point);
// The world-space ray through pixel (px, py) of `rect`.
[[nodiscard]] Ray rayThroughPixel(const CameraMatrices& camera, const PixelRect& rect, float px, float py);

enum class MarkerKind : std::uint8_t { Light, Camera };

// An entity with a Light or a Camera, a WorldMatrix and no Renderable, drawn
// at its world translation (R5).
struct Marker {
  atlantis::asset_system::EntityGuid entity;
  MarkerKind kind = MarkerKind::Light;
  Vec3 position{};
  friend bool operator==(const Marker&, const Marker&) = default;
};

// How close the pointer must be to a marker's pixel to pick it.
inline constexpr float kMarkerPickPixels = 10.0f;

// The marker within kMarkerPickPixels of (px, py), the nearest to the camera
// (by view depth) when several are; a marker behind the camera is never hit.
[[nodiscard]] std::optional<atlantis::asset_system::EntityGuid> pickMarker(std::span<const Marker> markers,
                                                                          const CameraMatrices& camera,
                                                                          const PixelRect& rect, float px, float py);

// A geometry pick for the host: an id and a world-space ray (unit direction).
struct PickRequest {
  std::uint64_t id = 0;
  Ray ray;
  friend bool operator==(const PickRequest&, const PickRequest&) = default;
};

// The host's answer to request `id`: the nearest hit entity, or none.
struct PickAnswer {
  std::uint64_t id = 0;
  std::optional<atlantis::asset_system::EntityGuid> entity;
  float distance = 0.0f;
  friend bool operator==(const PickAnswer&, const PickAnswer&) = default;
};

// The host's answer to boundsSubject(): the entity's mesh local bounds.
struct SelectionBounds {
  atlantis::asset_system::EntityGuid entity;
  Vec3 localMin{};
  Vec3 localMax{};
  friend bool operator==(const SelectionBounds&, const SelectionBounds&) = default;
};

// Borrows the connection, which must outlive it. Frame thread only, between
// Runtime frames (ADR-0105); not thread-safe.
class ViewportPicking {
 public:
  explicit ViewportPicking(atlantis::connection::RuntimeConnection& connection);

  // Re-reads the markers: for every Hierarchy row with a Light or a Camera,
  // a WorldMatrix and no Renderable, its WorldMatrix translation.
  void updateMarkers(const HierarchyModel& hierarchy);
  [[nodiscard]] const std::vector<Marker>& markers() const noexcept { return markers_; }

  // A click at (px, py) on the image drawn with `camera`: a marker hit (when
  // `withMarkers`, i.e. they are shown) selects its entity now; otherwise a
  // geometry pick is requested (request()), superseding any request still
  // unanswered.
  void click(HierarchyModel& hierarchy, const CameraMatrices& camera, const PixelRect& rect, float px, float py,
             bool withMarkers = true);

  // The request made since clearRequest(), for the host to answer.
  [[nodiscard]] const std::optional<PickRequest>& request() const noexcept { return request_; }
  void clearRequest() noexcept { request_.reset(); }

  // The host's answer: applied -- the hit selected, a miss clearing the
  // selection -- only if it answers the latest request and the selection has
  // not changed since that request was made. Returns whether it applied.
  bool answer(HierarchyModel& hierarchy, const PickAnswer& answer);

 private:
  atlantis::connection::RuntimeConnection& connection_;
  std::optional<schema::TypeId> lightType_;
  std::optional<schema::TypeId> cameraType_;
  std::optional<schema::TypeId> renderableType_;
  std::optional<schema::TypeId> worldMatrixType_;
  schema::FieldId column3Field_{};
  std::vector<Marker> markers_;
  std::uint64_t nextId_ = 1;
  std::optional<PickRequest> request_;
  // The latest request and the selection generation it was made at.
  std::optional<std::uint64_t> outstandingId_;
  std::uint64_t outstandingGeneration_ = 0;
};

}  // namespace atlantis::editor
