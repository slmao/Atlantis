#pragma once

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/editor/input.h>
#include <atlantis/editor/model/edit_history.h>
#include <atlantis/editor/model/gizmo.h>
#include <atlantis/editor/model/hierarchy.h>
#include <atlantis/editor/model/inspector.h>
#include <atlantis/editor/model/viewport_camera.h>
#include <atlantis/editor/model/viewport_picking.h>
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
// RuntimeControl; its host gives it plain input values and the scene camera,
// and takes back a UI draw list and the Viewport size it wants. Since Spec 0062
// (ADR-0123 D3) the host also takes the Viewport camera, a pick request and a
// bounds subject after each frame, and hands the answers to the next frame.
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

struct FrameContext {
  std::uint32_t windowWidth = 0;
  std::uint32_t windowHeight = 0;
  float deltaSeconds = 1.0f / 60.0f;
  // Plan 0062 P1 (ADR-0123 D1, D3): the scene's active camera, every frame
  // the host has one -- the Viewport's camera in Scene mode, and where the
  // editor camera starts.
  std::optional<CameraPose> sceneCamera;
  // The host's answer to the pickRequest() of an earlier frame.
  std::optional<PickAnswer> pickAnswer;
  // The mesh local bounds of the entity boundsSubject() named, when it has
  // a Renderable.
  std::optional<SelectionBounds> selectionBounds;
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

  // Plan 0062 P1 (ADR-0123 D3): the host's work after each frame() --
  // viewportCamera(): the camera to draw the Viewport from, none for the
  // scene's (Scene mode); pickRequest(): a geometry pick to answer in the next
  // FrameContext (none when this frame made none); boundsSubject(): the
  // entity whose mesh bounds the next FrameContext should carry.
  [[nodiscard]] std::optional<CameraPose> viewportCamera() const;
  [[nodiscard]] const std::optional<PickRequest>& pickRequest() const;
  [[nodiscard]] std::optional<atlantis::asset_system::EntityGuid> boundsSubject() const;

  // The models the UI acts through -- the same calls the view makes, for a
  // host or test that drives the editor without pointer input.
  [[nodiscard]] HierarchyModel& hierarchy();
  [[nodiscard]] InspectorModel& inspector();
  [[nodiscard]] GizmoModel& gizmo();
  // Plan 0062: the Viewport's camera (mode, editor camera) and its picking.
  [[nodiscard]] ViewportCameraState& camera();
  [[nodiscard]] ViewportPicking& picking();
  // Plan 0063 P7 (Spec 0063 R4): this session's edit history. Every frame()
  // records the Inspector's commits and the Gizmo's completed drags as Editor
  // Transactions (those made through the models between frames too) and
  // settles the history; the history is cleared when the Runtime's scene
  // changes.
  [[nodiscard]] EditHistory& history();

  // Where the last frame drew a widget, in framebuffer pixels -- for scripted
  // input. Keys: "hierarchy:<guid>", "hierarchy:filter",
  // "inspector:<Type>.<field...>" (an enum's choices, while its list is open:
  // "...#<constant>"), "inspector:<Type>" (a component's header),
  // "toolbar:Play|Pause|Step|Translate|Rotate|Scale", "viewport"; since Plan
  // 0062 also "toolbar:SceneCam|EditorCam|Align|World|Local|Markers" and
  // "hierarchy:filter-hidden" (the note shown when the filter hides the
  // selection; its button clears the filter). Empty when
  // the widget was not drawn (scrolled away, collapsed).
  [[nodiscard]] std::optional<UiRect> widgetRect(std::string_view key) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace atlantis::editor
