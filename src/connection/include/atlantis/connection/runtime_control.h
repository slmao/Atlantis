#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/connection/viewport_overlay.h>
#include <atlantis/result.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Spec 0055 R3 / ADR-0106 D3 (ruling Q2, K-a + V-a): the Runtime's lifecycle
// control, beside RuntimeConnection -- not a World operation, so the World's
// four concepts (ADR-0103, ADR-0105) are untouched. Runtime implements it; a
// transport (Atlantis Remote) carries it. No Runtime type appears here.
namespace atlantis::connection {

struct RuntimeStatus {
  bool paused = false;
  std::uint64_t frame = 0;  // frames run since the control began
  atlantis::asset_system::AssetGuid scene;
  // Plan 0065 P4 (ADR-0131 D5): the World's generation -- incremented by every
  // scene reload that swapped; 0 for the World the Runtime started with.
  std::uint64_t sceneGeneration = 0;
  bool supportsRenderLookCapture = false;
  bool supportsRenderLookCaptureExtent = false;
  bool supportsMaterialDiagnosticCapture = false;
  bool supportsDirectionalShadowDiagnosticCapture = false;
  friend bool operator==(const RuntimeStatus&, const RuntimeStatus&) = default;
};

// A frame's extraction output (ruling Q2, C1): exactly what the frame handed
// the renderer -- its lights as the GPU lighting block holds them, its
// camera matrices (column-major, as written to the camera uniform), and how
// many draw items it drew.
struct FrameDirectionalLight {
  std::array<float, 3> direction{};
  std::array<float, 3> color{};
  float intensity = 0.0f;
  friend bool operator==(const FrameDirectionalLight&, const FrameDirectionalLight&) = default;
};
struct FramePointLight {
  std::array<float, 3> position{};
  std::array<float, 3> color{};
  float intensity = 0.0f;
  float range = 0.0f;
  friend bool operator==(const FramePointLight&, const FramePointLight&) = default;
};
struct FrameData {
  std::vector<FrameDirectionalLight> directionalLights;
  std::vector<FramePointLight> pointLights;
  std::array<float, 16> view{};
  std::array<float, 16> projection{};
  std::uint64_t drawItemCount = 0;
  // Effective camera compensation used for this frame. Absent on legacy reports.
  std::optional<float> exposureCompensationEv{};
  friend bool operator==(const FrameData&, const FrameData&) = default;
};

// An image of a stepped frame (ruling Q2, C2), written as PNG.
struct CapturedImage {
  std::string path;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  friend bool operator==(const CapturedImage&, const CapturedImage&) = default;
};

struct StepRequest {
  std::uint32_t frames = 1;              // at least 1
  std::optional<std::string> imagePath;  // write the last frame's image here (PNG)
  // Spec 0066 R5 / ADR-0132 D2: a capture draws no Viewport overlay unless the
  // request asks; it then draws only the requesting client's own overlay.
  bool withOverlay = false;
  // The requester's own overlay, filled by the transport that holds each
  // client's overlay (Remote's server) when withOverlay is set; never carried
  // on the wire, and ignored from a client. (Its initializer keeps
  // StepRequest{frames, image} free of -Wmissing-field-initializers.)
  std::optional<ViewportOverlay> captureOverlay{};
};

// The last stepped frame. `applied`: command application ran in the stepped
// frames (Spec 0055 ruling Q3's `runtime step` field) -- true for every
// completed step, held or not, since a step releases application.
struct FrameReport {
  std::uint64_t frame = 0;
  bool applied = false;
  FrameData data;  // always (C1)
  std::optional<CapturedImage> image;  // when the request named a path (C2)
};

// Spec 0064 / ADR-0128 D1: a camera to draw the Viewport from instead of the
// scene's active camera -- the eye is column 3 of `worldMatrix` (column-major)
// and the forward direction -column 2, as the Runtime reads a camera; the lens
// is the vertical field of view and the depth range. Exposure, fog and bloom
// stay the active camera's (ADR-0123 D1).
struct ViewportCamera {
  std::array<float, 16> worldMatrix{};
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
  friend bool operator==(const ViewportCamera&, const ViewportCamera&) = default;
};

enum class RenderLookCaptureError { Unsupported, Busy, InvalidRequest, StaleProjection, TargetChanged,
                                    Unavailable, BudgetExceeded, CaptureFailed };
[[nodiscard]] constexpr std::string_view toString(RenderLookCaptureError value) noexcept {
  switch (value) {
    case RenderLookCaptureError::Unsupported: return "Unsupported";
    case RenderLookCaptureError::Busy: return "Busy";
    case RenderLookCaptureError::InvalidRequest: return "InvalidRequest";
    case RenderLookCaptureError::StaleProjection: return "StaleProjection";
    case RenderLookCaptureError::TargetChanged: return "TargetChanged";
    case RenderLookCaptureError::Unavailable: return "Unavailable";
    case RenderLookCaptureError::BudgetExceeded: return "BudgetExceeded";
    case RenderLookCaptureError::CaptureFailed: return "CaptureFailed";
  }
  return "CaptureFailed";
}
inline constexpr std::uint32_t kRenderLookCaptureWidth = 960;
inline constexpr std::uint32_t kRenderLookCaptureHeight = 540;

struct RenderLookExtent {
  std::uint32_t width = kRenderLookCaptureWidth;
  std::uint32_t height = kRenderLookCaptureHeight;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return width >= 128 && height >= 128 && width <= 1920 && height <= 1920 &&
           static_cast<std::uint64_t>(width) * height <= 2073600;
  }

  friend constexpr bool operator==(const RenderLookExtent&, const RenderLookExtent&) = default;
};

struct RenderLookCaptureRequest {
  authoring::DocumentToken document;
  authoring::ApplyId apply;
  std::uint64_t generation = 0;
  authoring::RenderLookBasis basis;
  ViewportCamera camera;
  std::array<std::vector<authoring::SetProperty>, 2> looks;
  std::array<std::string, 2> paths;
  RenderLookExtent extent{};
};
struct RenderLookCapturedPair {
  authoring::DocumentToken document;
  authoring::ApplyId apply;
  std::uint64_t generation = 0;
  authoring::EntityGuid activeCamera;
  std::array<FrameData, 2> frames;
  std::array<CapturedImage, 2> images;
  std::array<std::vector<authoring::SetProperty>, 2> looks;
};
using RenderLookCaptureResult = Result<RenderLookCapturedPair, RenderLookCaptureError>;

inline constexpr std::size_t kMaxViewportNavigationTargets = 64;
[[nodiscard]] inline bool validViewportNavigationTargets(
    std::span<const atlantis::asset_system::EntityGuid> targets) {
  if (targets.size() > kMaxViewportNavigationTargets) return false;
  for (auto it = targets.begin(); it != targets.end(); ++it) {
    if (*it == atlantis::asset_system::EntityGuid{} || std::find(targets.begin(), it, *it) != it) return false;
  }
  return true;
}
enum class ViewportBoundsKind { Mesh, Origin };

struct ViewportBounds {
  atlantis::asset_system::EntityGuid entity;
  ViewportBoundsKind kind = ViewportBoundsKind::Origin;
  std::array<float, 3> min{};
  std::array<float, 3> max{};
  friend bool operator==(const ViewportBounds&, const ViewportBounds&) = default;
};

struct ViewportNavigation {
  std::uint64_t sceneGeneration = 0;
  ViewportCamera sceneCamera;
  std::vector<ViewportBounds> bounds;
  friend bool operator==(const ViewportNavigation&, const ViewportNavigation&) = default;
};

enum class ViewportNavigationError {
  InvalidRequest, SceneCameraUnavailable, TargetUnavailable, GeometryUnavailable, InvalidBounds,
  Unsupported, TransportFailure, RpcFailure, ProtocolFailure,
};

[[nodiscard]] constexpr std::string_view toString(ViewportNavigationError error) noexcept {
  switch (error) {
    case ViewportNavigationError::InvalidRequest: return "InvalidRequest";
    case ViewportNavigationError::SceneCameraUnavailable: return "SceneCameraUnavailable";
    case ViewportNavigationError::TargetUnavailable: return "TargetUnavailable";
    case ViewportNavigationError::GeometryUnavailable: return "GeometryUnavailable";
    case ViewportNavigationError::InvalidBounds: return "InvalidBounds";
    case ViewportNavigationError::Unsupported: return "Unsupported";
    case ViewportNavigationError::TransportFailure: return "TransportFailure";
    case ViewportNavigationError::RpcFailure: return "RpcFailure";
    case ViewportNavigationError::ProtocolFailure: return "ProtocolFailure";
  }
  return "(unrecognized ViewportNavigationError)";
}

struct ViewportNavigationFailure {
  ViewportNavigationError code;
  std::optional<atlantis::asset_system::EntityGuid> target = std::nullopt;
  friend bool operator==(const ViewportNavigationFailure&, const ViewportNavigationFailure&) = default;
};
using ViewportNavigationResult = atlantis::Result<ViewportNavigation, ViewportNavigationFailure>;

// Spec 0064 / ADR-0128 D2: a world-space ray (the direction unit length) and
// the entity it hits first, with the distance along the ray.
struct Ray {
  std::array<float, 3> origin{};
  std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
  friend bool operator==(const Ray&, const Ray&) = default;
};
inline constexpr std::uint32_t kAlignmentTriangleLimit = 65536;
inline constexpr std::size_t kAlignmentRayLimit = 2;

struct AlignmentFace {
  atlantis::asset_system::EntityGuid entity;
  atlantis::asset_system::AssetGuid mesh;
  std::array<std::uint32_t, 2> triangles{};
  float distance = 0;
  std::array<std::array<float, 3>, 4> corners{};
  std::array<float, 3> normal{};
  std::array<float, 16> worldMatrix{};
  friend bool operator==(const AlignmentFace&, const AlignmentFace&) = default;
};
struct AlignmentFaces {
  std::uint64_t sceneGeneration = 0;
  std::vector<AlignmentFace> faces;
  friend bool operator==(const AlignmentFaces&, const AlignmentFaces&) = default;
};
enum class AlignmentFaceError {
  InvalidRequest, NoGeometry, Miss, UnsupportedFace, Ambiguous, TooLarge, Unsupported,
  TransportFailure, RpcFailure, ProtocolFailure,
};
[[nodiscard]] constexpr std::string_view toString(AlignmentFaceError error) noexcept {
  constexpr std::array<std::string_view, 10> names{
      "InvalidRequest", "NoGeometry", "Miss", "UnsupportedFace", "Ambiguous", "TooLarge", "Unsupported",
      "TransportFailure", "RpcFailure", "ProtocolFailure"};
  return names[static_cast<std::size_t>(error)];
}
struct AlignmentFaceFailure {
  AlignmentFaceError code;
  std::optional<std::uint32_t> rayIndex = {};
  friend bool operator==(const AlignmentFaceFailure&, const AlignmentFaceFailure&) = default;
};
using AlignmentFacesResult = atlantis::Result<AlignmentFaces, AlignmentFaceFailure>;
[[nodiscard]] inline bool validAlignmentRay(const Ray& ray) {
  if (!std::ranges::all_of(ray.origin, [](float value) { return std::isfinite(value); })) return false;
  double length = 0;
  for (const float value : ray.direction) length += static_cast<double>(value) * value;
  return std::abs(length - 1.0) <= 1e-4;
}

struct RayHit {
  atlantis::asset_system::EntityGuid entity;
  float distance = 0.0f;
  friend bool operator==(const RayHit&, const RayHit&) = default;
};

inline constexpr std::size_t kViewportVisibilityEntries = 8192;
using ViewportMembers = std::vector<atlantis::asset_system::EntityGuid>;

struct ViewportStamp {
  std::uint64_t sceneGeneration = 0;
  std::uint64_t revision = 0;
  friend bool operator==(const ViewportStamp&, const ViewportStamp&) = default;
};

struct MaterialDiagnosticTarget {
  authoring::EntityGuid entity;
  asset_system::AssetGuid mesh;
  asset_system::AssetGuid material;
  friend bool operator==(const MaterialDiagnosticTarget&, const MaterialDiagnosticTarget&) = default;
};

struct MaterialDiagnosticRequest {
  authoring::DocumentToken document;
  authoring::ApplyId apply;
  std::uint64_t generation;
  ViewportCamera camera;
  RenderLookExtent extent;
  MaterialDiagnosticTarget target;
  std::array<std::string, 2> paths;
  std::optional<ViewportStamp> viewport;
};

struct MaterialDiagnosticPair {
  authoring::DocumentToken document;
  authoring::ApplyId apply;
  std::uint64_t generation;
  MaterialDiagnosticTarget target;
  asset_system::AssetGuid texture;
  std::array<float, 3> factor;
  std::array<FrameData, 2> frames;
  std::array<CapturedImage, 2> images;
};

using MaterialDiagnosticResult = Result<MaterialDiagnosticPair, RenderLookCaptureError>;

struct DirectionalShadowDiagnosticRequest {
  authoring::DocumentToken document;
  authoring::ApplyId apply;
  std::uint64_t generation;
  ViewportCamera camera;
  RenderLookExtent extent;
  std::array<std::string, 2> paths;
  std::optional<ViewportStamp> viewport;
};

struct DirectionalShadowDiagnosticPair {
  authoring::DocumentToken document;
  authoring::ApplyId apply;
  std::uint64_t generation;
  authoring::EntityGuid activeCamera;
  FrameData frame;
  std::array<CapturedImage, 2> images;
};

using DirectionalShadowDiagnosticResult = Result<DirectionalShadowDiagnosticPair, RenderLookCaptureError>;

inline constexpr std::size_t kMaterialBindingInstances = 8192;
inline constexpr std::size_t kMaterialBindingTriangles = 262144;
inline constexpr std::string_view kMaterialBindingSlot = "Renderable.materialAsset";

struct MaterialBindingPickRequest {
  Ray ray;
  float minDistance = 0.0f;
  float maxDistance = 0.0f;
  std::uint64_t sceneGeneration = 0;
  std::optional<ViewportStamp> viewport;
};

[[nodiscard]] inline bool validMaterialBindingPick(const MaterialBindingPickRequest& request) {
  return validAlignmentRay(request.ray) && std::isfinite(request.minDistance) &&
      std::isfinite(request.maxDistance) && request.minDistance >= 0.0f &&
      request.maxDistance > request.minDistance &&
      (!request.viewport || request.viewport->sceneGeneration == request.sceneGeneration);
}

enum class MaterialBindingPickError {
  InvalidRequest, Unavailable, Unsupported, StaleGeneration, StaleViewport, NotPresented,
  ActivePolicy, NoGeometry, Miss, Ambiguous, UnsupportedBinding, TooLarge,
  TransportFailure, RpcFailure, ProtocolFailure,
};

[[nodiscard]] constexpr std::string_view toString(MaterialBindingPickError error) {
  constexpr std::array<std::string_view, 15> names{
      "InvalidRequest", "Unavailable", "Unsupported", "StaleGeneration", "StaleViewport", "NotPresented",
      "ActivePolicy", "NoGeometry", "Miss", "Ambiguous", "UnsupportedBinding", "TooLarge",
      "TransportFailure", "RpcFailure", "ProtocolFailure"};
  return names[static_cast<std::size_t>(error)];
}

struct MaterialBindingPick {
  std::uint64_t sceneGeneration;
  std::optional<ViewportStamp> viewport;
  atlantis::asset_system::EntityGuid entity;
  atlantis::asset_system::AssetGuid mesh;
  atlantis::asset_system::AssetGuid material;
  float distance;
};
using MaterialBindingPickResult = atlantis::Result<MaterialBindingPick, MaterialBindingPickError>;

struct ViewportPolicy {
  ViewportMembers hidden;
  ViewportMembers locked;
  std::optional<ViewportMembers> isolated;
  friend bool operator==(const ViewportPolicy&, const ViewportPolicy&) = default;
};

enum class ViewportOwner { None, Caller, Other };
struct ViewportVisibility {
  ViewportStamp stamp;
  bool presented = false;
  ViewportOwner owner = ViewportOwner::None;
  std::optional<ViewportPolicy> policy;
};

enum class ViewportVisibilityError {
  Unsupported, InvalidRequest, Unavailable, TooLarge, NotOwner, StaleGeneration, StaleRevision,
  UnknownMember, NotPresented, NoGeometry, FaceRefused, TransportFailure, RpcFailure, ProtocolFailure,
};
[[nodiscard]] constexpr std::string_view toString(ViewportVisibilityError error) noexcept {
  constexpr std::array<std::string_view, 14> names{
      "Unsupported", "InvalidRequest", "Unavailable", "TooLarge", "NotOwner", "StaleGeneration",
      "StaleRevision", "UnknownMember", "NotPresented", "NoGeometry", "FaceRefused",
      "TransportFailure", "RpcFailure", "ProtocolFailure"};
  return names[static_cast<std::size_t>(error)];
}

struct ViewportVisibilityFailure {
  ViewportVisibilityError code;
  std::optional<AlignmentFaceFailure> face = {};
};

using ViewportVisibilityResult = atlantis::Result<ViewportVisibility, ViewportVisibilityFailure>;
enum class ViewportPickKind { Selection, Alignment };
struct ViewportPick {
  ViewportStamp stamp;
  std::variant<std::optional<RayHit>, AlignmentFaces> value;
};
using ViewportPickResult = atlantis::Result<ViewportPick, ViewportVisibilityFailure>;

[[nodiscard]] inline std::optional<ViewportVisibilityError> validateViewportPolicy(const ViewportPolicy& policy) {
  const std::size_t total = policy.hidden.size() + policy.locked.size() +
                            (policy.isolated ? policy.isolated->size() : 0);
  if (total > kViewportVisibilityEntries) return ViewportVisibilityError::TooLarge;
  const auto validList = [](const ViewportMembers& list) {
    return (list.empty() || list.front() != atlantis::asset_system::EntityGuid{}) &&
           std::is_sorted(list.begin(), list.end()) && std::adjacent_find(list.begin(), list.end()) == list.end();
  };
  if (total == 0 || !validList(policy.hidden) || !validList(policy.locked) ||
      (policy.isolated && (policy.isolated->empty() || !validList(*policy.isolated))))
    return ViewportVisibilityError::InvalidRequest;
  return std::nullopt;
}

enum class ControlError {
  InvalidRequest,  // zero frames; a Viewport overlay not well formed or over its limits
  NotRendering,    // the Runtime has no drawable frame to capture (no window surface yet, or minimized)
  CaptureFailed,   // the offscreen render, readback or PNG write failed
  Stopped,         // the Runtime stopped before the step completed
  NoGeometry,      // Spec 0064 / ADR-0128 D2: the Runtime keeps no pick geometry (--no-pick-geometry)
};

[[nodiscard]] constexpr std::string_view toString(ControlError error) noexcept {
  switch (error) {
    case ControlError::InvalidRequest: return "InvalidRequest";
    case ControlError::NotRendering: return "NotRendering";
    case ControlError::CaptureFailed: return "CaptureFailed";
    case ControlError::Stopped: return "Stopped";
    case ControlError::NoGeometry: return "NoGeometry";
  }
  return "(unrecognized ControlError)";
}

enum class DiagnosticSeverity : std::uint8_t { Warning, Error, Fatal };

[[nodiscard]] constexpr std::string_view toString(DiagnosticSeverity severity) noexcept {
  switch (severity) {
    case DiagnosticSeverity::Warning: return "warning";
    case DiagnosticSeverity::Error: return "error";
    case DiagnosticSeverity::Fatal: return "fatal";
  }
  return "(unrecognized DiagnosticSeverity)";
}

// One Runtime log record of Warning or above (Plan 0055 P6), with a
// sequence number that increases by one per record from 1.
struct Diagnostic {
  std::uint64_t sequence = 0;
  DiagnosticSeverity severity = DiagnosticSeverity::Warning;
  std::string message;
  friend bool operator==(const Diagnostic&, const Diagnostic&) = default;
};

struct DiagnosticBatch {
  std::vector<Diagnostic> entries;  // after the cursor, oldest first
  std::uint64_t latest = 0;         // the newest sequence recorded so far (0: none)
  std::uint64_t dropped = 0;        // records the bounded ring has discarded so far
};

// Not thread-safe (ADR-0004): every call on the owner's frame thread, between
// frames -- like RuntimeConnection.
class RuntimeControl {
 public:
  virtual ~RuntimeControl() = default;

  [[nodiscard]] virtual ViewportVisibilityResult viewportVisibility() {
    return ViewportVisibilityResult::Err({ViewportVisibilityError::Unsupported});
  }
  [[nodiscard]] virtual ViewportVisibilityResult setViewportVisibility(
      ViewportStamp, std::optional<ViewportPolicy>) {
    return ViewportVisibilityResult::Err({ViewportVisibilityError::Unsupported});
  }
  [[nodiscard]] virtual ViewportPickResult pickViewport(ViewportStamp, ViewportPickKind, std::span<const Ray>) {
    return ViewportPickResult::Err({ViewportVisibilityError::Unsupported});
  }

  [[nodiscard]] virtual RuntimeStatus status() = 0;
  // Pause holds command application at the start of each frame; frames go on
  // being pumped and presented (ruling Q2, P-a). Both are idempotent.
  virtual void pause() = 0;
  virtual void resume() = 0;
  // Runs `request.frames` frames -- while paused, releasing command
  // application for exactly that many -- and calls `done` once, after the
  // last of them, with its report (or why there is none). `done` runs on the
  // frame thread, between frames.
  virtual void step(StepRequest request, std::function<void(atlantis::Result<FrameReport, ControlError>)> done) = 0;
  // Records with a sequence after `afterSequence`, at most `max` of them.
  [[nodiscard]] virtual DiagnosticBatch diagnostics(std::uint64_t afterSequence, std::size_t max) = 0;

  // Spec 0064 / ADR-0128 D1: the camera the Runtime draws the Viewport from
  // from the next frame on, or none for the scene's active camera. One per
  // Runtime, last writer wins; an in-process overlay's camera takes
  // precedence; a camera the frame cannot use is ignored (ADR-0123 D1). Not
  // World state: no event reports it.
  virtual void setViewportCamera(std::optional<ViewportCamera> camera) = 0;
  [[nodiscard]] virtual MaterialDiagnosticResult captureMaterialDiagnostic(const MaterialDiagnosticRequest&) {
    return MaterialDiagnosticResult::Err(RenderLookCaptureError::Unsupported);
  }
  [[nodiscard]] virtual DirectionalShadowDiagnosticResult captureDirectionalShadowDiagnostic(
      const DirectionalShadowDiagnosticRequest&) {
    return DirectionalShadowDiagnosticResult::Err(RenderLookCaptureError::Unsupported);
  }
  [[nodiscard]] virtual RenderLookCaptureResult captureRenderLook(const RenderLookCaptureRequest&) {
    return RenderLookCaptureResult::Err(RenderLookCaptureError::Unsupported);
  }
  // Spec 0064 / ADR-0128 D2: the nearest Renderable the ray hits at its
  // current WorldMatrix (both triangle faces, alpha masks ignored; ADR-0123
  // D2), none on a miss; NoGeometry when the Runtime keeps no pick geometry.
  [[nodiscard]] virtual atlantis::Result<std::optional<RayHit>, ControlError> raycast(const Ray& ray) = 0;
  // ADR-0184: one or two first-hit cap observations from a single live scene.
  [[nodiscard]] virtual MaterialBindingPickResult pickMaterialBinding(const MaterialBindingPickRequest&) {
    return MaterialBindingPickResult::Err(MaterialBindingPickError::Unsupported);
  }
  [[nodiscard]] virtual AlignmentFacesResult pickAlignmentFaces(std::span<const Ray>) {
    return AlignmentFacesResult::Err({AlignmentFaceError::Unsupported});
  }
  // ADR-0165: owned values from a single live scene sample, in target order.
  // Targets are borrowed for this call only. An empty span asks for the active
  // scene camera without an override or any geometry read.
  [[nodiscard]] virtual ViewportNavigationResult viewportNavigation(
      std::span<const atlantis::asset_system::EntityGuid>) {
    return ViewportNavigationResult::Err({ViewportNavigationError::Unsupported});
  }
  // Spec 0066 R2 / ADR-0132 D2: the calling client's Viewport overlay, drawn
  // from the next frame on, replaced whole; none clears it. Each client has
  // its own, the frame draws the union of all clients', and a client's
  // overlay goes when its connection closes (a transport keeps them apart and
  // hands its implementation the union). InvalidRequest when the overlay is
  // not well formed or over one client's limits; nothing changes then. Not
  // World state: no event reports it.
  [[nodiscard]] virtual atlantis::Result<std::monostate, ControlError> setViewportOverlay(
      std::optional<ViewportOverlay> overlay) = 0;
};

}  // namespace atlantis::connection
