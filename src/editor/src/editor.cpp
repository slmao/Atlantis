#include <atlantis/editor/editor.h>

#include "view/editor_view.h"

namespace atlantis::editor {

struct Editor::Impl {
  Impl(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control)
      : connection(connection), control(control) {}

  atlantis::connection::RuntimeConnection& connection;
  atlantis::connection::RuntimeControl& control;
  detail::EditorView view;
};

Editor::Editor(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control)
    : impl_(std::make_unique<Impl>(connection, control)) {}

Editor::~Editor() = default;

FontAtlas Editor::fontAtlas() const { return impl_->view.fontAtlas(); }

void Editor::frame(std::span<const InputEvent> input, const FrameContext& context) {
  impl_->view.frame(input, context);
}

const UiDrawList& Editor::drawList() const { return impl_->view.drawList(); }

ViewportSize Editor::viewportSize() const { return impl_->view.viewportSize(); }

}  // namespace atlantis::editor
