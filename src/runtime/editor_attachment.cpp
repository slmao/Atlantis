#include "editor_attachment.h"

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace atlantis::runtime {

namespace editor = atlantis::editor;
namespace platform = atlantis::platform;

// The two key sets are the same list in the same order (input.h,
// platform_event.h): translation is by value, pinned at both ends here.
static_assert(static_cast<int>(platform::Key::Tab) == static_cast<int>(editor::Key::Tab));
static_assert(static_cast<int>(platform::Key::A) == static_cast<int>(editor::Key::A));
static_assert(static_cast<int>(platform::Key::Num0) == static_cast<int>(editor::Key::Num0));
static_assert(static_cast<int>(platform::Key::F1) == static_cast<int>(editor::Key::F1));
static_assert(static_cast<int>(platform::Key::RightAlt) == static_cast<int>(editor::Key::RightAlt));

std::optional<editor::InputEvent> toEditorInput(const platform::PlatformEvent& event) {
  return std::visit(
      [](const auto& e) -> std::optional<editor::InputEvent> {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, platform::PointerMoved>) {
          return editor::PointerMoved{e.x, e.y};
        } else if constexpr (std::is_same_v<T, platform::PointerButtonChanged>) {
          return editor::PointerButtonChanged{static_cast<editor::PointerButton>(e.button), e.down, e.x, e.y};
        } else if constexpr (std::is_same_v<T, platform::WheelScrolled>) {
          return editor::WheelScrolled{e.dx, e.dy};
        } else if constexpr (std::is_same_v<T, platform::KeyChanged>) {
          return editor::KeyChanged{static_cast<editor::Key>(e.key), e.down,
                                    editor::KeyModifiers{e.modifiers.ctrl, e.modifiers.shift, e.modifiers.alt}};
        } else if constexpr (std::is_same_v<T, platform::TextEntered>) {
          editor::TextEntered text;
          text.utf8 = e.utf8;
          text.size = e.size;
          return text;
        } else {
          return std::nullopt;
        }
      },
      event);
}

void toRendererDrawList(const editor::UiDrawList& from, atlantis::renderer::UiDrawList& to) {
  to.vertices.resize(from.vertices.size());
  for (std::size_t i = 0; i < from.vertices.size(); ++i) {
    const editor::UiVertex& v = from.vertices[i];
    to.vertices[i] = atlantis::renderer::UiVertex{v.x, v.y, v.u, v.v, v.r, v.g, v.b, v.a};
  }
  to.indices = from.indices;
  to.commands.resize(from.commands.size());
  for (std::size_t i = 0; i < from.commands.size(); ++i) {
    const editor::UiDrawCommand& c = from.commands[i];
    to.commands[i] = atlantis::renderer::UiDrawCommand{
        c.firstIndex, c.indexCount, c.vertexOffset,
        atlantis::rhi::Rect2D{c.clip.x, c.clip.y, {c.clip.width, c.clip.height}},
        c.texture == editor::UiTexture::Viewport ? atlantis::renderer::UiTexture::Viewport
                                                 : atlantis::renderer::UiTexture::FontAtlas};
  }
  to.display = atlantis::rhi::Extent2D{from.displayWidth, from.displayHeight};
}

EditorAttachment::EditorAttachment(RuntimeApplication& application, atlantis::connection::RuntimeControl& control)
    : application_(application),
      connection_(application.openConnection()),
      editor_(std::make_unique<editor::Editor>(*connection_, control)),
      atlas_(editor_->fontAtlas()) {}

// The editor goes before its connection (it borrows it).
EditorAttachment::~EditorAttachment() { editor_.reset(); }

void EditorAttachment::onPlatformEvent(const platform::PlatformEvent& event) {
  if (const auto* resize = std::get_if<platform::WindowResize>(&event)) {
    if (!resize->framebuffer.isZero()) {
      windowWidth_ = resize->framebuffer.width;
      windowHeight_ = resize->framebuffer.height;
    }
    return;
  }
  if (std::optional<editor::InputEvent> input = toEditorInput(event)) pending_.push_back(*input);
}

void EditorAttachment::update() {
  const auto now = std::chrono::steady_clock::now();
  editor::FrameContext context;
  context.windowWidth = windowWidth_;
  context.windowHeight = windowHeight_;
  if (lastUpdate_.has_value()) {
    context.deltaSeconds = std::clamp(std::chrono::duration<float>(now - *lastUpdate_).count(), 1e-4f, 1.0f);
  }
  lastUpdate_ = now;
  // Plan 0062 P3 (J4): the scene camera's pose and lens -- no draw-item walk.
  if (const std::optional<ActiveCameraInput> scene = application_.activeCameraPose(); scene.has_value()) {
    context.sceneCamera = editor::CameraPose{
        scene->worldMatrix, editor::CameraLens{scene->camera.fovYRadians, scene->camera.nearZ, scene->camera.farZ}};
  }
  context.pickAnswer = std::exchange(pickAnswer_, std::nullopt);
  context.selectionBounds = selectionBounds_;
  editor_->frame(pending_, context);
  pending_.clear();
  toRendererDrawList(editor_->drawList(), drawList_);

  // Plan 0062 P3/P8 (ADR-0123 D2, D3): this frame's requests, answered now,
  // between frames, and handed to the next editor frame.
  if (const std::optional<editor::PickRequest>& request = editor_->pickRequest(); request.has_value()) {
    const std::optional<PickHit> hit =
        application_.pickRenderable(PickRay{request->ray.origin, request->ray.direction});
    pickAnswer_ = editor::PickAnswer{request->id, hit.has_value() ? std::optional(hit->entity) : std::nullopt,
                                     hit.has_value() ? hit->distance : 0.0f};
  }
  selectionBounds_.reset();
  if (const std::optional<atlantis::asset_system::EntityGuid> subject = editor_->boundsSubject(); subject.has_value()) {
    if (const std::optional<LocalBounds> bounds = application_.renderableLocalBounds(*subject); bounds.has_value()) {
      selectionBounds_ = editor::SelectionBounds{*subject, bounds->min, bounds->max};
    }
  }
}

std::optional<ViewportCameraOverride> EditorAttachment::viewportCamera() const {
  const std::optional<editor::CameraPose> pose = editor_->viewportCamera();
  if (!pose.has_value()) return std::nullopt;
  return ViewportCameraOverride{pose->worldMatrix, pose->lens.fovYRadians, pose->lens.nearZ, pose->lens.farZ};
}

atlantis::rhi::Extent2D EditorAttachment::viewportExtent() const {
  const editor::ViewportSize size = editor_->viewportSize();
  return atlantis::rhi::Extent2D{std::max(size.width, 1u), std::max(size.height, 1u)};
}

const atlantis::renderer::UiDrawList& EditorAttachment::drawList() const { return drawList_; }

FontAtlasPixels EditorAttachment::fontAtlas() const {
  return FontAtlasPixels{atlas_.width, atlas_.height, atlas_.rgba};
}

}  // namespace atlantis::runtime
