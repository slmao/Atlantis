#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/connection/viewport_overlay.h>
#include <atlantis/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
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
  // on the wire, and ignored from a client.
  std::optional<ViewportOverlay> captureOverlay;
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

// Spec 0064 / ADR-0128 D2: a world-space ray (the direction unit length) and
// the entity it hits first, with the distance along the ray.
struct Ray {
  std::array<float, 3> origin{};
  std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
  friend bool operator==(const Ray&, const Ray&) = default;
};
struct RayHit {
  atlantis::asset_system::EntityGuid entity;
  float distance = 0.0f;
  friend bool operator==(const RayHit&, const RayHit&) = default;
};

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
  // Spec 0064 / ADR-0128 D2: the nearest Renderable the ray hits at its
  // current WorldMatrix (both triangle faces, alpha masks ignored; ADR-0123
  // D2), none on a miss; NoGeometry when the Runtime keeps no pick geometry.
  [[nodiscard]] virtual atlantis::Result<std::optional<RayHit>, ControlError> raycast(const Ray& ray) = 0;
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
