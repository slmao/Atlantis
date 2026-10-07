#pragma once

#include <atlantis/editor/editor.h>
#include <atlantis/editor/input.h>
#include <atlantis/editor/ui_draw_list.h>

#include <memory>
#include <span>

// Plan 0056 P3/P8: the view layer's entry point, private to the editor
// library. Its implementation is the only place a UI-library header is
// included (src/editor/src/view/); this header names no UI-library type, so
// editor.cpp can hold it without seeing the library.
namespace atlantis::editor::detail {

class EditorView {
 public:
  // Creates and owns one UI-library context and builds its font atlas once.
  EditorView();
  ~EditorView();
  EditorView(const EditorView&) = delete;
  EditorView& operator=(const EditorView&) = delete;

  [[nodiscard]] const FontAtlas& fontAtlas() const;

  // Feeds `input`, runs one UI frame of the fixed layout and translates its
  // draw data into drawList().
  void frame(std::span<const InputEvent> input, const FrameContext& context);

  [[nodiscard]] const UiDrawList& drawList() const;
  [[nodiscard]] ViewportSize viewportSize() const;

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace atlantis::editor::detail
