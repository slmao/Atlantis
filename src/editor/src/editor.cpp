#include <atlantis/editor/editor.h>

#include <atlantis/editor/model/runtime_world_edit_target.h>

#include "view/editor_view.h"

#include <array>
#include <string>
#include <string_view>

namespace atlantis::editor {

struct Editor::Impl {
  Impl(atlantis::connection::RuntimeConnection& connection, atlantis::connection::RuntimeControl& control)
      : connection(connection),
        control(control),
        hierarchy(connection),
        inspector(connection),
        gizmo(connection),
        picking(connection),
        editTarget(connection),
        history(editTarget) {}

  // Plan 0063 P7: the completed drag, then the commits, as Editor Transactions.
  void recordCompleted();

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
  // Plan 0063 P7 (Spec 0063 R2, R4): the edit history over E2's one target,
  // and the scene it belongs to.
  RuntimeWorldEditTarget editTarget;
  EditHistory history;
  std::optional<atlantis::asset_system::AssetGuid> scene;
  detail::EditorView view;
};

namespace {

[[nodiscard]] atlantis::asset_system::scene::PropertyAddress toScene(const atlantis::world::access::PropertyAddress& a) {
  return {a.entity, a.component, a.field};
}

[[nodiscard]] std::string_view modeName(GizmoMode mode) {
  switch (mode) {
    case GizmoMode::Translate: return "Move";
    case GizmoMode::Rotate: return "Rotate";
    case GizmoMode::Scale: return "Scale";
  }
  return "Move";
}

}  // namespace

void Editor::Impl::recordCompleted() {
  if (const std::optional<CompletedDrag> drag = gizmo.takeCompletedDrag(); drag.has_value()) {
    EditTransaction edit{std::string(modeName(drag->mode)) + " " +
                             atlantis::asset_system::toString(drag->subject).substr(0, 8),
                         {}};
    for (std::size_t k = 0; k < 4; ++k) {
      edit.writes.push_back(PropertyWrite{toScene(drag->addresses[k]), drag->start[k], drag->final[k]});
    }
    history.record(std::move(edit), RuntimeWorldEditTarget::handleOf(drag->ticket));
  }
  for (CompletedEdit& commit : inspector.takeCommits()) {
    history.record(EditTransaction{"Set " + commit.path, {PropertyWrite{toScene(commit.address), commit.before, commit.after}}},
                   RuntimeWorldEditTarget::handleOf(commit.ticket));
  }
}

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
  // Plan 0063 P7: edits made through the models between frames are recorded
  // before the history settles, so none of their events is missed.
  impl.recordCompleted();
  const atlantis::connection::RuntimeStatus status = impl.control.status();
  if (impl.scene.has_value() && *impl.scene != status.scene) impl.history.clear();
  impl.scene = status.scene;
  impl.history.update(failures);
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
  detail::ViewModels models{impl.hierarchy, impl.inspector,       impl.gizmo,        impl.control,
                            status,          impl.camera,          impl.picking,      impl.selectionBounds,
                            impl.showMarkers, impl.history};
  impl.view.frame(input, context, models);
  // The Gizmo's drag: at most one transaction this frame (ruling Q5, G-a).
  (void)impl.gizmo.flush();
  impl.recordCompleted();
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

EditHistory& Editor::history() { return impl_->history; }

std::optional<UiRect> Editor::widgetRect(std::string_view key) const { return impl_->view.widgetRect(key); }

}  // namespace atlantis::editor
