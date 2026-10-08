#pragma once

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/editor/input.h>
#include <atlantis/editor/model/gizmo.h>
#include <atlantis/editor/model/hierarchy.h>
#include <atlantis/editor/model/inspector.h>
#include <atlantis/editor/ui_draw_list.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// Spec 0056 / ADR-0108 D2 (Plan 0056 P8): Atlantis Editor -- an ordinary
// client of a running Runtime. It reaches the World only through a
// RuntimeConnection and the Runtime's lifecycle only through a
// RuntimeControl; its host gives it plain input values and the frame's camera,
// and takes back a UI draw list and the Viewport size it wants.
namespace atlantis::editor {

// The UI's font atlas: width x height RGBA8 pixels, row-major, tightly packed.
// Built once, when the Editor is constructed; the host uploads it once.
struct FontAtlas {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgba;
};

// The Viewport panel's size in framebuffer pixels: the size the host renders
// the scene at.
struct ViewportSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  friend bool operator==(const ViewportSize&, const ViewportSize&) = default;
};

// The frame's camera, column-major, exactly as the Runtime's frame data holds
// it (Spec 0055 C1).
struct CameraMatrices {
  std::array<float, 16> view{};
  std::array<float, 16> projection{};
};

struct FrameContext {
  std::uint32_t windowWidth = 0;
  std::uint32_t windowHeight = 0;
  float deltaSeconds = 1.0f / 60.0f;
  // Present when the host has the frame's camera; the Gizmo needs it
  // (wantsCamera()).
  std::optional<CameraMatrices> camera;
};

// Owns one UI-library context of its own (no global UI state outside it) and
// borrows the connection and the control, which must outlive it. Every call
// is the host's frame thread's, between Runtime frames (ADR-0105's contract
// for the connection); not thread-safe (ADR-0004).
class Editor {
 public:
  Editor(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control);
  ~Editor();
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;
  Editor(Editor&&) = delete;
  Editor& operator=(Editor&&) = delete;

  [[nodiscard]] FontAtlas fontAtlas() const;

  // One UI frame: the input gathered since the last frame, in order. Drains
  // the connection's failures once, brings the models up to date with the
  // World (Hierarchy events, the Inspector's values and pending edits, the
  // Gizmo's matrix), runs the UI -- whose actions are the models' and the
  // control's own calls -- and submits the Gizmo's transaction for this
  // frame, if any.
  void frame(std::span<const InputEvent> input, const FrameContext& context);

  // The last frame's UI; valid until the next frame().
  [[nodiscard]] const UiDrawList& drawList() const;

  // The Viewport panel's size after the last frame (zero before the first).
  [[nodiscard]] ViewportSize viewportSize() const;

  // Whether the next frame() draws a Gizmo and so needs FrameContext::camera
  // (Plan 0056 J7: the host computes the camera only then).
  [[nodiscard]] bool wantsCamera() const;

  // The models the UI acts through -- the same calls the view makes, for a
  // host or test that drives the editor without pointer input.
  [[nodiscard]] HierarchyModel& hierarchy();
  [[nodiscard]] InspectorModel& inspector();
  [[nodiscard]] GizmoModel& gizmo();

  // Where the last frame drew a widget, in framebuffer pixels -- for scripted
  // input. Keys: "hierarchy:<guid>", "hierarchy:filter",
  // "inspector:<Type>.<field...>" (an enum's choices, while its list is open:
  // "...#<constant>"), "inspector:<Type>" (a component's header),
  // "toolbar:Play|Pause|Step|Translate|Rotate|Scale", "viewport". Empty when
  // the widget was not drawn (scrolled away, collapsed).
  [[nodiscard]] std::optional<UiRect> widgetRect(std::string_view key) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace atlantis::editor
