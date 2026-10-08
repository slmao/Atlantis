#include <atlantis/editor/editor.h>

#include "view/editor_view.h"

namespace atlantis::editor {

struct Editor::Impl {
  Impl(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control)
      : connection(connection), control(control), hierarchy(connection), inspector(connection), gizmo(connection) {}

  atlantis::connection::RuntimeConnection& connection;
  atlantis::connection::RuntimeControl& control;
  HierarchyModel hierarchy;
  InspectorModel inspector;
  GizmoModel gizmo;
  detail::EditorView view;
};

Editor::Editor(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control)
    : impl_(std::make_unique<Impl>(connection, control)) {}

Editor::~Editor() = default;

FontAtlas Editor::fontAtlas() const { return impl_->view.fontAtlas(); }

void Editor::frame(std::span<const InputEvent> input, const FrameContext& context) {
  Impl& impl = *impl_;
  // Failures belong to the connection, not to a model: drained once here and
  // handed to the Inspector, whose edits are the ones that report them.
  const std::vector<atlantis::world::access::CommandFailure> failures = impl.connection.drainFailures();
  impl.hierarchy.update();
  impl.inspector.setSubject(impl.hierarchy.selection());
  impl.inspector.update(failures);
  impl.gizmo.setSubject(impl.hierarchy.selection());
  impl.gizmo.update();
  detail::ViewModels models{impl.hierarchy, impl.inspector, impl.gizmo, impl.control, impl.control.status()};
  impl.view.frame(input, context, models);
  // The Gizmo's drag: at most one transaction this frame (ruling Q5, G-a).
  (void)impl.gizmo.flush();
}

const UiDrawList& Editor::drawList() const { return impl_->view.drawList(); }

ViewportSize Editor::viewportSize() const { return impl_->view.viewportSize(); }

bool Editor::wantsCamera() const { return impl_->gizmo.available(); }

HierarchyModel& Editor::hierarchy() { return impl_->hierarchy; }

InspectorModel& Editor::inspector() { return impl_->inspector; }

GizmoModel& Editor::gizmo() { return impl_->gizmo; }

std::optional<UiRect> Editor::widgetRect(std::string_view key) const { return impl_->view.widgetRect(key); }

}  // namespace atlantis::editor
