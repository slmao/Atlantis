#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/result.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

enum class ControlError {
  InvalidRequest,  // zero frames
  NotRendering,    // the Runtime has no drawable frame to capture (no window surface yet, or minimized)
  CaptureFailed,   // the offscreen render, readback or PNG write failed
  Stopped,         // the Runtime stopped before the step completed
};

[[nodiscard]] constexpr std::string_view toString(ControlError error) noexcept {
  switch (error) {
    case ControlError::InvalidRequest: return "InvalidRequest";
    case ControlError::NotRendering: return "NotRendering";
    case ControlError::CaptureFailed: return "CaptureFailed";
    case ControlError::Stopped: return "Stopped";
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
};

}  // namespace atlantis::connection
