#include "editor_view.h"

#include <atlantis/connection/text.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace atlantis::editor::detail {

namespace access = atlantis::world::access;
namespace text = atlantis::connection::text;
using atlantis::asset_system::EntityGuid;

namespace {

// The two texture identifiers the UI's draw commands carry; the translation
// below maps them to UiTexture. Zero is ImGui's "invalid" identifier.
constexpr ImTextureID kFontAtlasTextureId = 1;
constexpr ImTextureID kViewportTextureId = 2;

// The Gizmo's handles: their on-screen length, and how close the pointer must
// be to pick one.
constexpr float kHandlePixels = 80.0f;
constexpr float kPickPixels = 8.0f;

// Restores the previously current UI-library context when it leaves scope, so
// an Editor never leaves its context installed as process-global state.
class ContextScope {
 public:
  explicit ContextScope(ImGuiContext* context) : previous_(ImGui::GetCurrentContext()) {
    ImGui::SetCurrentContext(context);
  }
  ~ContextScope() { ImGui::SetCurrentContext(previous_); }
  ContextScope(const ContextScope&) = delete;
  ContextScope& operator=(const ContextScope&) = delete;

 private:
  ImGuiContext* previous_;
};

[[nodiscard]] ImGuiKey toImGuiKey(Key key) {
  switch (key) {
    case Key::Tab: return ImGuiKey_Tab;
    case Key::LeftArrow: return ImGuiKey_LeftArrow;
    case Key::RightArrow: return ImGuiKey_RightArrow;
    case Key::UpArrow: return ImGuiKey_UpArrow;
    case Key::DownArrow: return ImGuiKey_DownArrow;
    case Key::Home: return ImGuiKey_Home;
    case Key::End: return ImGuiKey_End;
    case Key::PageUp: return ImGuiKey_PageUp;
    case Key::PageDown: return ImGuiKey_PageDown;
    case Key::Insert: return ImGuiKey_Insert;
    case Key::Delete: return ImGuiKey_Delete;
    case Key::Backspace: return ImGuiKey_Backspace;
    case Key::Space: return ImGuiKey_Space;
    case Key::Enter: return ImGuiKey_Enter;
    case Key::Escape: return ImGuiKey_Escape;
    case Key::LeftCtrl: return ImGuiKey_LeftCtrl;
    case Key::LeftShift: return ImGuiKey_LeftShift;
    case Key::LeftAlt: return ImGuiKey_LeftAlt;
    case Key::RightCtrl: return ImGuiKey_RightCtrl;
    case Key::RightShift: return ImGuiKey_RightShift;
    case Key::RightAlt: return ImGuiKey_RightAlt;
    default: break;
  }
  const auto value = static_cast<int>(key);
  if (value >= static_cast<int>(Key::A) && value <= static_cast<int>(Key::Z)) {
    return static_cast<ImGuiKey>(ImGuiKey_A + (value - static_cast<int>(Key::A)));
  }
  if (value >= static_cast<int>(Key::Num0) && value <= static_cast<int>(Key::Num9)) {
    return static_cast<ImGuiKey>(ImGuiKey_0 + (value - static_cast<int>(Key::Num0)));
  }
  if (value >= static_cast<int>(Key::F1) && value <= static_cast<int>(Key::F12)) {
    return static_cast<ImGuiKey>(ImGuiKey_F1 + (value - static_cast<int>(Key::F1)));
  }
  return ImGuiKey_None;
}

[[nodiscard]] float channel(ImU32 color, int shift) {
  return static_cast<float>((color >> shift) & 0xFFu) / 255.0f;
}

// --- The Gizmo's screen geometry ------------------------------------------------

using Vec3 = std::array<float, 3>;

// A world point through the frame's view and projection (column-major) to
// Viewport-panel pixels; empty behind the camera.
[[nodiscard]] std::optional<ImVec2> project(const CameraMatrices& camera, const Vec3& point, ImVec2 origin,
                                           ImVec2 size) {
  std::array<float, 4> view{};
  for (int row = 0; row < 4; ++row) {
    view[row] = camera.view[0 * 4 + row] * point[0] + camera.view[1 * 4 + row] * point[1] +
                camera.view[2 * 4 + row] * point[2] + camera.view[3 * 4 + row];
  }
  std::array<float, 4> clip{};
  for (int row = 0; row < 4; ++row) {
    clip[row] = camera.projection[0 * 4 + row] * view[0] + camera.projection[1 * 4 + row] * view[1] +
                camera.projection[2 * 4 + row] * view[2] + camera.projection[3 * 4 + row] * view[3];
  }
  if (!(clip[3] > 1e-6f)) return std::nullopt;
  const float ndcX = clip[0] / clip[3];
  const float ndcY = clip[1] / clip[3];
  return ImVec2(origin.x + (ndcX + 1.0f) * 0.5f * size.x, origin.y + (ndcY + 1.0f) * 0.5f * size.y);
}

// Whether world axis `axis` points toward the camera (view space looks down -z).
[[nodiscard]] bool facesCamera(const CameraMatrices& camera, std::size_t axis) {
  return camera.view[axis * 4 + 2] > 0.0f;
}

[[nodiscard]] float length(ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

[[nodiscard]] float distanceToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
  const ImVec2 ab(b.x - a.x, b.y - a.y);
  const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
  float t = lengthSquared > 0.0f ? ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSquared : 0.0f;
  t = std::clamp(t, 0.0f, 1.0f);
  return length(ImVec2(p.x - (a.x + ab.x * t), p.y - (a.y + ab.y * t)));
}

constexpr std::array<ImU32, 3> kAxisColors{IM_COL32(230, 60, 60, 255), IM_COL32(70, 200, 70, 255),
                                           IM_COL32(70, 120, 240, 255)};
constexpr ImU32 kActiveAxisColor = IM_COL32(250, 220, 60, 255);

// One axis handle on screen: from the origin along the axis's projected
// direction, kHandlePixels long; `pixelsPerUnit` is how many pixels one world
// unit along the axis covers at the origin.
struct AxisHandle {
  ImVec2 origin;
  ImVec2 direction;  // unit, screen space
  ImVec2 end;
  float pixelsPerUnit = 0.0f;
};

}  // namespace

struct EditorView::State {
  ImGuiContext* imgui = nullptr;
  FontAtlas fontAtlas;
  UiDrawList drawList;
  ViewportSize viewportSize;
  std::unordered_map<std::string, UiRect> rects;

  // Per-widget edit state: the value being edited in a field until its edit
  // completes (Plan 0056 J5: one SetProperty per completed edit, none while
  // dragging), and the text of GUID fields being typed.
  std::map<std::string, access::PropertyValue> scratch;
  std::map<std::string, std::array<char, 64>> guidBuffers;
  std::array<char, 64> filter{};

  // The Gizmo drag in progress, in screen terms.
  struct Drag {
    GizmoAxis axis = GizmoAxis::X;
    ImVec2 start;
    AxisHandle handle;
    float startAngle = 0.0f;
  };
  std::optional<Drag> drag;

  void feed(std::span<const InputEvent> input);
  void record(const std::string& key);
  void layout(const FrameContext& context, ViewModels& models);
  void hierarchyPanel(ViewModels& models);
  void viewportPanel(const FrameContext& context, ViewModels& models);
  void gizmo(const FrameContext& context, ViewModels& models, ImVec2 origin, ImVec2 size, bool hovered);
  void inspectorPanel(ViewModels& models);
  void fieldControl(ViewModels& models, std::size_t index);
  void translate(const ImDrawData& data);
};

EditorView::EditorView() : state_(std::make_unique<State>()) {
  ImGuiContext* previous = ImGui::GetCurrentContext();
  state_->imgui = ImGui::CreateContext();
  ImGui::SetCurrentContext(state_->imgui);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // no settings file: the layout is fixed
  io.LogFilename = nullptr;
  // Vertex offsets are honoured (UiDrawCommand::vertexOffset). Dynamic font
  // textures (RendererHasTextures) are not used (Plan 0056 J8): the atlas is
  // built once below and uploaded once by the host.
  io.BackendFlags = ImGuiBackendFlags_RendererHasVtxOffset;

  // ImGui 1.92 compiles the legacy ImFontAtlas::Build()/GetTexDataAsRGBA32()
  // out under IMGUI_DISABLE_OBSOLETE_FUNCTIONS (J8). ImFontAtlasBuildMain() is
  // that Build()'s entire body: with RendererHasTextures unset it preloads
  // every glyph of the default font into one RGBA32 texture.
  ImFontAtlas& atlas = *io.Fonts;
  atlas.TexDesiredFormat = ImTextureFormat_RGBA32;
  ImFontAtlasBuildMain(&atlas);
  ImTextureData& texture = *atlas.TexData;
  texture.SetTexID(kFontAtlasTextureId);
  state_->fontAtlas.width = static_cast<std::uint32_t>(texture.Width);
  state_->fontAtlas.height = static_cast<std::uint32_t>(texture.Height);
  const auto* pixels = static_cast<const std::uint8_t*>(texture.GetPixels());
  state_->fontAtlas.rgba.assign(pixels, pixels + static_cast<std::size_t>(texture.GetSizeInBytes()));
  ImGui::SetCurrentContext(previous);
}

EditorView::~EditorView() {
  if (state_ && state_->imgui != nullptr) ImGui::DestroyContext(state_->imgui);
}

const FontAtlas& EditorView::fontAtlas() const { return state_->fontAtlas; }

void EditorView::frame(std::span<const InputEvent> input, const FrameContext& context, ViewModels& models) {
  const ContextScope scope(state_->imgui);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(context.windowWidth), static_cast<float>(context.windowHeight));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  io.DeltaTime = context.deltaSeconds > 0.0f ? context.deltaSeconds : 1.0f / 60.0f;
  state_->feed(input);
  state_->rects.clear();

  ImGui::NewFrame();
  state_->layout(context, models);
  ImGui::Render();
  state_->translate(*ImGui::GetDrawData());
}

const UiDrawList& EditorView::drawList() const { return state_->drawList; }

ViewportSize EditorView::viewportSize() const { return state_->viewportSize; }

std::optional<UiRect> EditorView::widgetRect(std::string_view key) const {
  const auto it = state_->rects.find(std::string(key));
  if (it == state_->rects.end()) return std::nullopt;
  return it->second;
}

void EditorView::State::feed(std::span<const InputEvent> input) {
  ImGuiIO& io = ImGui::GetIO();
  for (const InputEvent& event : input) {
    std::visit(
        [&io](const auto& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<T, PointerMoved>) {
            io.AddMousePosEvent(e.x, e.y);
          } else if constexpr (std::is_same_v<T, PointerButtonChanged>) {
            io.AddMousePosEvent(e.x, e.y);
            io.AddMouseButtonEvent(static_cast<int>(e.button), e.down);
          } else if constexpr (std::is_same_v<T, WheelScrolled>) {
            io.AddMouseWheelEvent(e.dx, e.dy);
          } else if constexpr (std::is_same_v<T, KeyChanged>) {
            io.AddKeyEvent(ImGuiMod_Ctrl, e.modifiers.ctrl);
            io.AddKeyEvent(ImGuiMod_Shift, e.modifiers.shift);
            io.AddKeyEvent(ImGuiMod_Alt, e.modifiers.alt);
            const ImGuiKey key = toImGuiKey(e.key);
            if (key != ImGuiKey_None) io.AddKeyEvent(key, e.down);
          } else {
            const std::string typed(e.utf8.data(), e.size);
            io.AddInputCharactersUTF8(typed.c_str());
          }
        },
        event);
  }
}

// The last item's screen rectangle, under `key`, for scripted input.
void EditorView::State::record(const std::string& key) {
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  rects[key] = UiRect{static_cast<std::int32_t>(std::floor(min.x)), static_cast<std::int32_t>(std::floor(min.y)),
                      static_cast<std::uint32_t>(std::max(0.0f, std::ceil(max.x) - std::floor(min.x))),
                      static_cast<std::uint32_t>(std::max(0.0f, std::ceil(max.y) - std::floor(min.y)))};
}

// The fixed layout (ruling Q6 keeps the Content quadrant): Hierarchy | Viewport
// over Content | Inspector, filling the window. No docking, nothing movable.
void EditorView::State::layout(const FrameContext& context, ViewModels& models) {
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float leftWidth = std::floor(display.x * 0.3f);
  const float topHeight = std::floor(display.y * 0.6f);
  constexpr ImGuiWindowFlags kFixed = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                      ImGuiWindowFlags_NoBringToFrontOnFocus;
  const auto place = [](float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(std::max(w, 1.0f), std::max(h, 1.0f)));
  };

  place(0.0f, 0.0f, leftWidth, topHeight);
  if (ImGui::Begin("Hierarchy", nullptr, kFixed)) hierarchyPanel(models);
  ImGui::End();

  place(leftWidth, 0.0f, display.x - leftWidth, topHeight);
  if (ImGui::Begin("Viewport", nullptr, kFixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    viewportPanel(context, models);
  }
  ImGui::End();

  place(0.0f, topHeight, leftWidth, display.y - topHeight);
  if (ImGui::Begin("Content", nullptr, kFixed)) ImGui::TextDisabled("Reserved (Spec 0056 ruling Q6)");
  ImGui::End();

  place(leftWidth, topHeight, display.x - leftWidth, display.y - topHeight);
  if (ImGui::Begin("Inspector", nullptr, kFixed)) inspectorPanel(models);
  ImGui::End();
}

// R2: the flat entity list, filtered, only the visible rows emitted.
void EditorView::State::hierarchyPanel(ViewModels& models) {
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##filter", "filter: GUID prefix or component", filter.data(), filter.size());
  record("hierarchy:filter");
  const std::vector<std::size_t> visible = models.hierarchy.filtered(filter.data());
  ImGui::TextDisabled("%zu of %zu entities", visible.size(), models.hierarchy.rows().size());
  if (!ImGui::BeginChild("##rows")) {
    ImGui::EndChild();
    return;
  }
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible.size()));
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const HierarchyRow& row = models.hierarchy.rows()[visible[static_cast<std::size_t>(i)]];
      const std::string guidText = atlantis::asset_system::toString(row.entity);
      const std::string label = guidText + "  " + models.hierarchy.componentNames(row) + "##" + guidText;
      const bool selected = models.hierarchy.selection() == row.entity;
      if (ImGui::Selectable(label.c_str(), selected)) models.hierarchy.select(row.entity);
      record("hierarchy:" + guidText);
    }
  }
  ImGui::EndChild();
}

// R4 + R6: the toolbar (Play / Pause / Step, the Gizmo mode, the status) over
// the Runtime's rendered frame, with the Gizmo drawn on it.
void EditorView::State::viewportPanel(const FrameContext& context, ViewModels& models) {
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + 4.0f));
  if (ImGui::Button("Play")) models.control.resume();
  record("toolbar:Play");
  ImGui::SameLine();
  if (ImGui::Button("Pause")) models.control.pause();
  record("toolbar:Pause");
  ImGui::SameLine();
  if (ImGui::Button("Step")) {
    models.control.step(atlantis::connection::StepRequest{1, std::nullopt}, [](auto) {});
  }
  record("toolbar:Step");
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  constexpr std::array<std::pair<GizmoMode, const char*>, 3> kModes{
      std::pair{GizmoMode::Translate, "Translate"}, std::pair{GizmoMode::Rotate, "Rotate"},
      std::pair{GizmoMode::Scale, "Scale"}};
  for (const auto& [mode, name] : kModes) {
    ImGui::SameLine();
    if (ImGui::RadioButton(name, models.gizmo.mode() == mode)) models.gizmo.setMode(mode);
    record(std::string("toolbar:") + name);
  }
  ImGui::SameLine();
  ImGui::Text("| %s  frame %llu", models.status.paused ? "paused" : "running",
              static_cast<unsigned long long>(models.status.frame));

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  viewportSize = ViewportSize{static_cast<std::uint32_t>(std::max(available.x, 0.0f)),
                              static_cast<std::uint32_t>(std::max(available.y, 0.0f))};
  if (viewportSize.width == 0 || viewportSize.height == 0) return;
  const ImVec2 size(static_cast<float>(viewportSize.width), static_cast<float>(viewportSize.height));
  ImGui::Image(ImTextureRef(kViewportTextureId), size);
  record("viewport");
  const bool hovered = ImGui::IsItemHovered();
  if (hovered && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) models.gizmo.setMode(GizmoMode::Translate);
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) models.gizmo.setMode(GizmoMode::Rotate);
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) models.gizmo.setMode(GizmoMode::Scale);
  }
  gizmo(context, models, origin, size, hovered);
}

// R5: world-axis handles on the selected entity, projected with the frame's
// camera; a drag turns pointer motion into the model's drag amount.
void EditorView::State::gizmo(const FrameContext& context, ViewModels& models, ImVec2 origin, ImVec2 size,
                              bool hovered) {
  GizmoModel& model = models.gizmo;
  if (!context.camera.has_value() || !model.available()) {
    if (drag.has_value()) {
      model.endDrag();
      drag.reset();
    }
    return;
  }
  const CameraMatrices& camera = *context.camera;
  const MatrixColumns& matrix = *model.matrix();
  const Vec3 position{matrix[3][0], matrix[3][1], matrix[3][2]};
  const std::optional<ImVec2> center = project(camera, position, origin, size);
  if (!center.has_value()) return;

  std::array<std::optional<AxisHandle>, 3> handles;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    Vec3 unit = position;
    unit[axis] += 1.0f;
    const std::optional<ImVec2> tip = project(camera, unit, origin, size);
    if (!tip.has_value()) continue;
    const ImVec2 delta(tip->x - center->x, tip->y - center->y);
    const float pixels = length(delta);
    if (pixels < 1e-3f) continue;
    const ImVec2 direction(delta.x / pixels, delta.y / pixels);
    handles[axis] = AxisHandle{*center, direction,
                               ImVec2(center->x + direction.x * kHandlePixels, center->y + direction.y * kHandlePixels),
                               pixels};
  }

  ImDrawList& draw = *ImGui::GetWindowDrawList();
  draw.PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  const ImVec2 pointer = ImGui::GetIO().MousePos;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!handles[axis].has_value()) continue;
    const AxisHandle& handle = *handles[axis];
    const bool active = drag.has_value() && static_cast<std::size_t>(drag->axis) == axis;
    const ImU32 color = active ? kActiveAxisColor : kAxisColors[axis];
    switch (model.mode()) {
      case GizmoMode::Translate:
        draw.AddLine(handle.origin, handle.end, color, 3.0f);
        draw.AddTriangleFilled(ImVec2(handle.end.x + handle.direction.x * 10.0f, handle.end.y + handle.direction.y * 10.0f),
                               ImVec2(handle.end.x - handle.direction.y * 5.0f, handle.end.y + handle.direction.x * 5.0f),
                               ImVec2(handle.end.x + handle.direction.y * 5.0f, handle.end.y - handle.direction.x * 5.0f),
                               color);
        break;
      case GizmoMode::Scale:
        draw.AddLine(handle.origin, handle.end, color, 3.0f);
        draw.AddRectFilled(ImVec2(handle.end.x - 5.0f, handle.end.y - 5.0f), ImVec2(handle.end.x + 5.0f, handle.end.y + 5.0f),
                           color);
        break;
      case GizmoMode::Rotate:
        draw.AddCircle(handle.end, 9.0f, color, 0, 3.0f);
        draw.AddLine(handle.origin, handle.end, color, 1.0f);
        break;
    }
  }
  draw.PopClipRect();

  ImGuiIO& io = ImGui::GetIO();
  if (!drag.has_value() && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    float best = kPickPixels;
    for (std::size_t axis = 0; axis < 3; ++axis) {
      if (!handles[axis].has_value()) continue;
      const AxisHandle& handle = *handles[axis];
      const float distance = model.mode() == GizmoMode::Rotate
                                 ? std::fabs(length(ImVec2(pointer.x - handle.end.x, pointer.y - handle.end.y)) - 9.0f)
                                 : distanceToSegment(pointer, handle.origin, handle.end);
      if (distance <= best) {
        best = distance;
        drag = Drag{static_cast<GizmoAxis>(axis), pointer, handle,
                    std::atan2(pointer.y - handle.origin.y, pointer.x - handle.origin.x)};
      }
    }
    if (drag.has_value()) model.beginDrag(drag->axis);
  }
  if (!drag.has_value()) return;
  if (!io.MouseDown[ImGuiMouseButton_Left]) {
    model.endDrag();
    drag.reset();
    return;
  }
  const ImVec2 moved(pointer.x - drag->start.x, pointer.y - drag->start.y);
  const float along = moved.x * drag->handle.direction.x + moved.y * drag->handle.direction.y;
  switch (model.mode()) {
    case GizmoMode::Translate: model.dragTo(along / drag->handle.pixelsPerUnit); break;
    case GizmoMode::Scale: model.dragTo(1.0f + along / kHandlePixels); break;
    case GizmoMode::Rotate: {
      // Screen y points down: a positive (right-handed) rotation about an
      // axis facing the camera turns counter-clockwise on screen, i.e. by a
      // decreasing screen angle.
      float angle = std::atan2(pointer.y - drag->handle.origin.y, pointer.x - drag->handle.origin.x) - drag->startAngle;
      if (facesCamera(camera, static_cast<std::size_t>(drag->axis))) angle = -angle;
      model.dragTo(angle);
      break;
    }
  }
}

// R3: every field of the subject's components, generated from the schema --
// one control per PrimitiveKind, nested structs as tree nodes, Optional with
// a "set" box, read-only fields disabled; an edit commits when it completes.
void EditorView::State::inspectorPanel(ViewModels& models) {
  InspectorModel& inspector = models.inspector;
  if (!inspector.subject().has_value()) {
    ImGui::TextDisabled("Select an entity in the Hierarchy.");
    return;
  }
  ImGui::Text("Entity %s", atlantis::asset_system::toString(*inspector.subject()).c_str());
  const std::vector<InspectorField>& fields = inspector.fields();
  std::size_t index = 0;
  while (index < fields.size()) {
    const schema::TypeId component = fields[index].component;
    const std::string componentName(inspector.componentName(component));
    const bool open = ImGui::CollapsingHeader(componentName.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
    record("inspector:" + componentName);
    std::vector<std::string> openGroups;  // the tree nodes pushed for nested structs
    std::size_t depthShown = 0;           // how many of them are open
    for (; index < fields.size() && fields[index].component == component; ++index) {
      if (!open) continue;
      const InspectorField& field = fields[index];
      // Pop the groups this field is not in, then push the ones it enters.
      std::size_t common = 0;
      while (common < openGroups.size() && common < field.groups.size() && openGroups[common] == field.groups[common]) {
        ++common;
      }
      while (openGroups.size() > common) {
        if (depthShown == openGroups.size()) {
          ImGui::TreePop();
          --depthShown;
        }
        openGroups.pop_back();
      }
      while (openGroups.size() < field.groups.size()) {
        const std::string& group = field.groups[openGroups.size()];
        const bool parentsOpen = depthShown == openGroups.size();
        openGroups.push_back(group);
        if (parentsOpen && ImGui::TreeNodeEx((group + "##" + field.path).c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
          ++depthShown;
        }
      }
      if (depthShown == openGroups.size()) fieldControl(models, index);
    }
    while (depthShown > 0) {
      ImGui::TreePop();
      --depthShown;
    }
    if (open) {
      if (const std::string_view note = inspector.componentNote(component); !note.empty()) {
        ImGui::TextDisabled("%.*s", static_cast<int>(note.size()), note.data());
      }
    }
  }
}

void EditorView::State::fieldControl(ViewModels& models, std::size_t index) {
  InspectorModel& inspector = models.inspector;
  const InspectorField& field = inspector.fields()[index];
  ImGui::PushID(field.path.c_str());
  const bool absent = field.value.has_value() && std::holds_alternative<access::Absent>(*field.value);
  if (field.optional) {
    bool set = !absent;
    if (!field.editable) ImGui::BeginDisabled();
    if (ImGui::Checkbox("##set", &set)) {
      if (!set) {
        (void)inspector.commit(index, access::Absent{});
      } else {
        // Present again: the kind's zero, edited from there.
        switch (field.control) {
          case FieldControl::Integer: (void)inspector.commit(index, std::uint64_t{0}); break;
          case FieldControl::Float: (void)inspector.commit(index, 0.0f); break;
          case FieldControl::Float3: (void)inspector.commit(index, std::array<float, 3>{}); break;
          case FieldControl::Float4: (void)inspector.commit(index, std::array<float, 4>{}); break;
          case FieldControl::Enum:
            if (!field.choices.empty()) (void)inspector.commit(index, access::EnumValue{field.choices[0].value});
            break;
          case FieldControl::Guid: break;  // typed into the text box instead
        }
      }
    }
    if (!field.editable) ImGui::EndDisabled();
    ImGui::SameLine();
  }
  const bool disabled = !field.editable || absent || !field.value.has_value();
  if (disabled) ImGui::BeginDisabled();
  ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.6f));
  const std::string& key = field.path;
  // The value shown: the edit in progress, else the World's.
  const access::PropertyValue shown = scratch.contains(key)                ? scratch.at(key)
                                      : field.value.has_value() && !absent ? *field.value
                                                                           : access::PropertyValue{};
  const auto editable = [&](auto edited, bool changed) {
    if (changed) scratch[key] = edited;
    if (ImGui::IsItemDeactivatedAfterEdit() && scratch.contains(key)) {
      (void)inspector.commit(index, scratch.at(key));
      scratch.erase(key);
    } else if (!ImGui::IsItemActive()) {
      scratch.erase(key);
    }
  };
  switch (field.control) {
    case FieldControl::Integer: {
      std::uint64_t value = std::holds_alternative<std::uint64_t>(shown) ? std::get<std::uint64_t>(shown) : 0;
      const bool changed = ImGui::InputScalar(field.label.c_str(), ImGuiDataType_U64, &value);
      editable(value, changed);
      break;
    }
    case FieldControl::Float: {
      float value = std::holds_alternative<float>(shown) ? std::get<float>(shown) : 0.0f;
      const bool changed = ImGui::DragFloat(field.label.c_str(), &value, 0.01f, 0.0f, 0.0f, "%.4g");
      editable(value, changed);
      break;
    }
    case FieldControl::Float3: {
      auto value = std::holds_alternative<std::array<float, 3>>(shown) ? std::get<std::array<float, 3>>(shown)
                                                                        : std::array<float, 3>{};
      const bool changed = ImGui::DragFloat3(field.label.c_str(), value.data(), 0.01f, 0.0f, 0.0f, "%.4g");
      editable(value, changed);
      break;
    }
    case FieldControl::Float4: {
      auto value = std::holds_alternative<std::array<float, 4>>(shown) ? std::get<std::array<float, 4>>(shown)
                                                                        : std::array<float, 4>{};
      const bool changed = ImGui::DragFloat4(field.label.c_str(), value.data(), 0.01f, 0.0f, 0.0f, "%.4g");
      editable(value, changed);
      break;
    }
    case FieldControl::Enum: {
      const std::int64_t current =
          std::holds_alternative<access::EnumValue>(shown) ? std::get<access::EnumValue>(shown).value : -1;
      std::string currentName = "?";
      for (const EnumChoice& choice : field.choices) {
        if (choice.value == current) currentName = choice.name;
      }
      if (ImGui::BeginCombo(field.label.c_str(), currentName.c_str())) {
        for (const EnumChoice& choice : field.choices) {
          if (ImGui::Selectable(choice.name.c_str(), choice.value == current)) {
            (void)inspector.commit(index, access::EnumValue{choice.value});  // a pick is a completed edit
          }
          record("inspector:" + field.path + "#" + choice.name);
        }
        ImGui::EndCombo();
      }
      break;
    }
    case FieldControl::Guid: {
      auto& buffer = guidBuffers[key];
      // Refreshed from the World unless the text box is being typed in.
      if (ImGui::GetActiveID() != ImGui::GetID(field.label.c_str())) {
        std::string current;
        if (const auto* entity = std::get_if<EntityGuid>(&shown)) current = text::formatEntity(*entity);
        if (const auto* asset = std::get_if<atlantis::asset_system::AssetGuid>(&shown)) {
          current = atlantis::asset_system::toString(*asset);
        }
        std::snprintf(buffer.data(), buffer.size(), "%s", current.c_str());
      }
      ImGui::InputText(field.label.c_str(), buffer.data(), buffer.size());
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        // Invalid text is not committed (P9).
        if (field.primitive == schema::PrimitiveKind::EntityGuid) {
          if (auto parsed = text::parseEntity(buffer.data()); parsed.isOk()) (void)inspector.commit(index, parsed.value());
        } else if (auto parsed = atlantis::asset_system::parseAssetGuid(buffer.data()); parsed.isOk()) {
          (void)inspector.commit(index, parsed.value());
        }
      }
      break;
    }
  }
  record("inspector:" + field.path);
  if (disabled) ImGui::EndDisabled();
  if (const std::optional<EditOutcome>& outcome = inspector.outcome(index); outcome.has_value()) {
    ImGui::SameLine();
    switch (outcome->status) {
      case EditStatus::Pending: ImGui::TextDisabled("(pending)"); break;
      case EditStatus::Applied: ImGui::TextDisabled("(applied)"); break;
      case EditStatus::Refused: ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(refused: %s)", outcome->detail.c_str()); break;
    }
  }
  ImGui::PopID();
}

void EditorView::State::translate(const ImDrawData& data) {
  drawList.vertices.clear();
  drawList.indices.clear();
  drawList.commands.clear();
  drawList.displayWidth = static_cast<std::uint32_t>(std::max(data.DisplaySize.x, 0.0f));
  drawList.displayHeight = static_cast<std::uint32_t>(std::max(data.DisplaySize.y, 0.0f));
  const float displayWidth = static_cast<float>(drawList.displayWidth);
  const float displayHeight = static_cast<float>(drawList.displayHeight);
  for (const ImDrawList* list : data.CmdLists) {
    const auto baseVertex = static_cast<std::int32_t>(drawList.vertices.size());
    const auto baseIndex = static_cast<std::uint32_t>(drawList.indices.size());
    for (const ImDrawVert& vertex : list->VtxBuffer) {
      drawList.vertices.push_back(UiVertex{vertex.pos.x - data.DisplayPos.x, vertex.pos.y - data.DisplayPos.y,
                                           vertex.uv.x, vertex.uv.y, channel(vertex.col, IM_COL32_R_SHIFT),
                                           channel(vertex.col, IM_COL32_G_SHIFT),
                                           channel(vertex.col, IM_COL32_B_SHIFT),
                                           channel(vertex.col, IM_COL32_A_SHIFT)});
    }
    for (const ImDrawIdx index : list->IdxBuffer) drawList.indices.push_back(index);
    for (const ImDrawCmd& command : list->CmdBuffer) {
      if (command.UserCallback != nullptr || command.ElemCount == 0) continue;
      const float x0 = std::clamp(command.ClipRect.x - data.DisplayPos.x, 0.0f, displayWidth);
      const float y0 = std::clamp(command.ClipRect.y - data.DisplayPos.y, 0.0f, displayHeight);
      const float x1 = std::clamp(command.ClipRect.z - data.DisplayPos.x, 0.0f, displayWidth);
      const float y1 = std::clamp(command.ClipRect.w - data.DisplayPos.y, 0.0f, displayHeight);
      if (x1 <= x0 || y1 <= y0) continue;
      UiRect clip;
      clip.x = static_cast<std::int32_t>(std::floor(x0));
      clip.y = static_cast<std::int32_t>(std::floor(y0));
      clip.width = static_cast<std::uint32_t>(std::ceil(x1)) - static_cast<std::uint32_t>(clip.x);
      clip.height = static_cast<std::uint32_t>(std::ceil(y1)) - static_cast<std::uint32_t>(clip.y);
      drawList.commands.push_back(UiDrawCommand{
          baseIndex + command.IdxOffset, command.ElemCount, baseVertex + static_cast<std::int32_t>(command.VtxOffset),
          clip, command.GetTexID() == kViewportTextureId ? UiTexture::Viewport : UiTexture::FontAtlas});
    }
  }
}

}  // namespace atlantis::editor::detail
