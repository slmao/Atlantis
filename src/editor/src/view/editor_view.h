#pragma once

#include <atlantis/connection/runtime_control.h>
#include <atlantis/editor/editor.h>
#include <atlantis/editor/input.h>
#include <atlantis/editor/model/gizmo.h>
#include <atlantis/editor/model/hierarchy.h>
#include <atlantis/editor/model/inspector.h>
#include <atlantis/editor/ui_draw_list.h>

#include <memory>
#include <optional>
#include <span>
#include <string_view>

// Plan 0056 P3/P8: the view layer's entry point, private to the editor
// library. Its implementation is the only place a UI-library header is
// included (src/editor/src/view/); this header names no UI-library type, so
// editor.cpp can hold it without seeing the library.
namespace atlantis::editor::detail {

// What one UI frame shows and acts on: the models (the view calls exactly
// their public actions) and the Runtime's control and status.
struct ViewModels {
  HierarchyModel& hierarchy;
  InspectorModel& inspector;
  GizmoModel& gizmo;
  atlantis::connection::RuntimeControl& control;
  atlantis::connection::RuntimeStatus status;
};

class EditorView {
 public:
  // Creates and owns one UI-library context and builds its font atlas once.
  EditorView();
  ~EditorView();
  EditorView(const EditorView&) = delete;
  EditorView& operator=(const EditorView&) = delete;

  [[nodiscard]] const FontAtlas& fontAtlas() const;

  // Feeds `input`, runs one UI frame of the fixed layout over `models` and
  // translates its draw data into drawList().
  void frame(std::span<const InputEvent> input, const FrameContext& context, ViewModels& models);

  [[nodiscard]] const UiDrawList& drawList() const;
  [[nodiscard]] ViewportSize viewportSize() const;
  [[nodiscard]] std::optional<UiRect> widgetRect(std::string_view key) const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace atlantis::editor::detail
