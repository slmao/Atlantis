#include <atlantis/editor/editor.h>

#include "view/editor_view.h"

namespace atlantis::editor {

struct Editor::Impl {
  Impl(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control)
      : connection(connection),
        control(control),
        hierarchy(connection),
        inspector(connection),
        gizmo(connection),
        picking(connection) {}

  atlantis::connection::RuntimeConnection& connection;
  atlantis::connection::RuntimeControl& control;
  HierarchyModel hierarchy;
  InspectorModel inspector;
  GizmoModel gizmo;
  // Plan 0062 P1/P8: the Viewport's camera and picking, and the last bounds
  // the host answered (kept while they are the selection's).
  ViewportCameraState camera;
  ViewportPicking picking;
  std::optional<SelectionBounds> selectionBounds;
  bool showMarkers = true;
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
  // Plan 0062 P8: the host's answers to the last frame's requests, before the
  // models take up the selection.
  impl.picking.clearRequest();
  if (context.pickAnswer.has_value()) (void)impl.picking.answer(impl.hierarchy, *context.pickAnswer);
  if (context.selectionBounds.has_value()) impl.selectionBounds = context.selectionBounds;
  if (!impl.selectionBounds.has_value() || impl.selectionBounds->entity != impl.hierarchy.selection()) {
    impl.selectionBounds.reset();
  }
  impl.camera.setScenePose(context.sceneCamera);
  impl.picking.updateMarkers(impl.hierarchy);
  impl.inspector.setSubject(impl.hierarchy.selection());
  impl.inspector.update(failures);
  impl.gizmo.setSubject(impl.hierarchy.selection());
  impl.gizmo.update();
  detail::ViewModels models{impl.hierarchy,     impl.inspector, impl.gizmo,       impl.control,
                            impl.control.status(), impl.camera, impl.picking,     impl.selectionBounds,
                            impl.showMarkers};
  impl.view.frame(input, context, models);
  // The Gizmo's drag: at most one transaction this frame (ruling Q5, G-a).
  (void)impl.gizmo.flush();
}

const UiDrawList& Editor::drawList() const { return impl_->view.drawList(); }

ViewportSize Editor::viewportSize() const { return impl_->view.viewportSize(); }

std::optional<CameraPose> Editor::viewportCamera() const { return impl_->camera.override(); }

const std::optional<PickRequest>& Editor::pickRequest() const { return impl_->picking.request(); }

std::optional<atlantis::asset_system::EntityGuid> Editor::boundsSubject() const { return impl_->hierarchy.selection(); }

HierarchyModel& Editor::hierarchy() { return impl_->hierarchy; }

InspectorModel& Editor::inspector() { return impl_->inspector; }

GizmoModel& Editor::gizmo() { return impl_->gizmo; }

ViewportCameraState& Editor::camera() { return impl_->camera; }

ViewportPicking& Editor::picking() { return impl_->picking; }

std::optional<UiRect> Editor::widgetRect(std::string_view key) const { return impl_->view.widgetRect(key); }

}  // namespace atlantis::editor
