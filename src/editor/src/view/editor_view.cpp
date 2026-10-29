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

// A world point through the Viewport camera's view and projection
// (column-major) to Viewport-panel pixels; empty behind the camera.
[[nodiscard]] std::optional<ImVec2> project(const CameraMatrices& camera, const Vec3& point, ImVec2 origin,
                                           ImVec2 size) {
  const std::optional<std::array<float, 2>> pixel =
      pixelOf(camera, PixelRect{origin.x, origin.y, size.x, size.y}, point);
  if (!pixel.has_value()) return std::nullopt;
  return ImVec2((*pixel)[0], (*pixel)[1]);
}

// A world point through a column-major 4x4.
[[nodiscard]] Vec3 transformPoint(const MatrixColumns& m, const Vec3& p) {
  return {m[0][0] * p[0] + m[1][0] * p[1] + m[2][0] * p[2] + m[3][0],
          m[0][1] * p[0] + m[1][1] * p[1] + m[2][1] * p[2] + m[3][1],
          m[0][2] * p[0] + m[1][2] * p[1] + m[2][2] * p[2] + m[3][2]};
}

// Plan 0062 P8: a click is a press and release this close together (pixels).
constexpr float kClickSlopPixels = 4.0f;
constexpr ImU32 kSelectionColor = IM_COL32(250, 220, 60, 255);
constexpr ImU32 kMarkerColor = IM_COL32(235, 235, 235, 230);

[[nodiscard]] float length(ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

constexpr std::array<ImU32, 3> kAxisColors{IM_COL32(230, 60, 60, 255), IM_COL32(70, 200, 70, 255),
                                           IM_COL32(70, 120, 240, 255)};
constexpr ImU32 kActiveAxisColor = IM_COL32(250, 220, 60, 255);

// A point of gizmo.h's pixel arrays as the UI library's vector.
[[nodiscard]] ImVec2 toImVec2(const std::array<float, 2>& p) { return ImVec2(p[0], p[1]); }

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

  // The Gizmo drag in progress, in screen terms (gizmo.h's grab, Plan 0068 M1).
  std::optional<GizmoGrab> drag;

  // Plan 0062 P10: the navigation drag in progress (it holds the pointer from
  // press to release), a left press that may become a click, and the
  // Hierarchy's view of the selection (to scroll to a selection made
  // elsewhere).
  enum class Navigation : std::uint8_t { None, Look, Orbit, Pan, Dolly };
  Navigation navigation = Navigation::None;
  std::optional<ImVec2> clickStart;
  std::uint64_t seenSelectionGeneration = 0;
  bool scrollToSelection = false;
  bool clearFilter = false;  // the note's button, applied at the next Hierarchy panel

  void feed(std::span<const InputEvent> input);
  void record(const std::string& key);
  void layout(const FrameContext& context, ViewModels& models);
  void hierarchyPanel(ViewModels& models);
  void viewportPanel(const FrameContext& context, ViewModels& models);
  void navigate(const FrameContext& context, ViewModels& models, bool hovered, float viewportHeight);
  void frameSelection(ViewModels& models);
  void drawMarkers(ViewModels& models, const CameraMatrices& camera, ImVec2 origin, ImVec2 size);
  void drawBounds(ViewModels& models, const CameraMatrices& camera, ImVec2 origin, ImVec2 size);
  // Returns whether a handle was grabbed this frame.
  bool gizmo(ViewModels& models, const std::optional<CameraMatrices>& camera, ImVec2 origin, ImVec2 size,
             bool hovered, bool mayGrab);
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
  // Plan 0063 P8 (Spec 0063 R9): undo and redo anywhere in the window, but
  // never inside a text field (its own undo) or during a Gizmo drag.
  const ImGuiIO& io = ImGui::GetIO();
  if (!io.WantTextInput && !models.gizmo.dragging() && io.KeyCtrl) {
    if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      (void)(io.KeyShift ? models.history.redo() : models.history.undo());
    } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
      (void)models.history.redo();
    }
  }
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

// R2: the flat entity list, filtered, only the visible rows emitted. Plan
// 0062 P10 (Spec 0062 R6): a selection made elsewhere scrolls its row into
// view once; one the filter hides is named, with a way to clear the filter.
void EditorView::State::hierarchyPanel(ViewModels& models) {
  if (models.hierarchy.selectionGeneration() != seenSelectionGeneration) {
    seenSelectionGeneration = models.hierarchy.selectionGeneration();
    scrollToSelection = models.hierarchy.selection().has_value();
  }
  if (clearFilter) {
    // The text box may still hold its own copy of the text: let go of it first.
    if (ImGui::GetActiveID() == ImGui::GetID("##filter")) ImGui::ClearActiveID();
    filter.fill('\0');
    clearFilter = false;
  }
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##filter", "filter: GUID prefix or component", filter.data(), filter.size());
  record("hierarchy:filter");
  const std::vector<std::size_t> visible = models.hierarchy.filtered(filter.data());
  ImGui::TextDisabled("%zu of %zu entities", visible.size(), models.hierarchy.rows().size());
  std::optional<int> selectedRow;
  if (const auto& selection = models.hierarchy.selection(); selection.has_value()) {
    for (std::size_t i = 0; i < visible.size(); ++i) {
      if (models.hierarchy.rows()[visible[i]].entity == *selection) selectedRow = static_cast<int>(i);
    }
    if (!selectedRow.has_value()) {
      ImGui::TextDisabled("The selection is hidden by the filter.");
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) clearFilter = true;
      record("hierarchy:filter-hidden");
    }
  }
  if (!ImGui::BeginChild("##rows")) {
    ImGui::EndChild();
    return;
  }
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible.size()));
  const bool scrolling = scrollToSelection && selectedRow.has_value();
  if (scrolling) clipper.IncludeItemByIndex(*selectedRow);
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
      const HierarchyRow& row = models.hierarchy.rows()[visible[static_cast<std::size_t>(i)]];
      const std::string guidText = atlantis::asset_system::toString(row.entity);
      const std::string label = guidText + "  " + models.hierarchy.componentNames(row) + "##" + guidText;
      const bool selected = models.hierarchy.selection() == row.entity;
      if (ImGui::Selectable(label.c_str(), selected)) {
        models.hierarchy.select(row.entity);
        seenSelectionGeneration = models.hierarchy.selectionGeneration();  // chosen here: no scroll
      }
      if (scrolling && i == *selectedRow) ImGui::SetScrollHereY(0.5f);
      record("hierarchy:" + guidText);
    }
  }
  if (scrolling) scrollToSelection = false;
  ImGui::EndChild();
}

// R4 + R6: the toolbar (Play / Pause / Step, the Gizmo mode, the status) over
// the Runtime's rendered frame, with the Gizmo drawn on it. Plan 0062 P10
// (Spec 0062 R1, R2, R4-R7): the Gizmo space, the camera mode, Align and the
// markers toggle; navigation, clicks that pick, markers and the selection's
// bounds box.
void EditorView::State::viewportPanel(const FrameContext& context, ViewModels& models) {
  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + 4.0f));
  // Plan 0063 P8: Undo and Redo, disabled with their reason, labelled with
  // the edit they would act on.
  const auto historyButton = [&](const char* name, bool enabled, std::string_view label, bool redo) {
    if (!enabled) ImGui::BeginDisabled();
    if (ImGui::Button(name)) (void)(redo ? models.history.redo() : models.history.undo());
    if (!enabled) ImGui::EndDisabled();
    record(std::string("toolbar:") + name);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      const std::string tip = enabled                    ? std::string(name) + " " + std::string(label)
                              : models.history.waiting() ? std::string("waiting for the Runtime")
                                                         : std::string(redo ? "nothing to redo" : "nothing to undo");
      ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::SameLine();
  };
  historyButton("Undo", models.history.canUndo(), models.history.undoLabel(), false);
  historyButton("Redo", models.history.canRedo(), models.history.redoLabel(), true);
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
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
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  if (ImGui::RadioButton("World", models.gizmo.space() == GizmoSpace::World)) models.gizmo.setSpace(GizmoSpace::World);
  record("toolbar:World");
  ImGui::SameLine();
  if (ImGui::RadioButton("Local", models.gizmo.space() == GizmoSpace::Local)) models.gizmo.setSpace(GizmoSpace::Local);
  record("toolbar:Local");
  ImGui::SameLine();
  ImGui::Text("| %s  frame %llu", models.status.paused ? "paused" : "running",
              static_cast<unsigned long long>(models.status.frame));

  // The second row: the Viewport's camera and the markers.
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
  if (ImGui::RadioButton("Scene camera", models.camera.mode() == ViewportCameraMode::Scene)) {
    models.camera.setMode(ViewportCameraMode::Scene);
  }
  record("toolbar:SceneCam");
  ImGui::SameLine();
  if (ImGui::RadioButton("Editor camera", models.camera.mode() == ViewportCameraMode::Editor)) {
    models.camera.setMode(ViewportCameraMode::Editor);
  }
  record("toolbar:EditorCam");
  ImGui::SameLine();
  if (ImGui::Button("Align")) models.camera.align();
  record("toolbar:Align");
  ImGui::SameLine();
  ImGui::Checkbox("Markers", &models.showMarkers);
  record("toolbar:Markers");
  ImGui::SameLine();
  ImGui::TextDisabled("%s", models.history.notice().c_str());  // Plan 0063 P8
  record("status:history");

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  viewportSize = ViewportSize{static_cast<std::uint32_t>(std::max(available.x, 0.0f)),
                              static_cast<std::uint32_t>(std::max(available.y, 0.0f))};
  if (viewportSize.width == 0 || viewportSize.height == 0) return;
  const ImVec2 size(static_cast<float>(viewportSize.width), static_cast<float>(viewportSize.height));
  const float aspect = size.x / size.y;
  ImGui::Image(ImTextureRef(kViewportTextureId), size);
  record("viewport");
  const bool hovered = ImGui::IsItemHovered();
  ImGuiIO& io = ImGui::GetIO();
  // The camera that drew the image the pointer is on, before this frame's
  // navigation moves it: a pick's ray comes from it.
  const std::optional<CameraMatrices> shown = models.camera.matrices(aspect);

  if (!io.WantTextInput) {
    if (hovered && navigation != Navigation::Look) {
      if (ImGui::IsKeyPressed(ImGuiKey_W, false)) models.gizmo.setMode(GizmoMode::Translate);
      if (ImGui::IsKeyPressed(ImGuiKey_E, false)) models.gizmo.setMode(GizmoMode::Rotate);
      if (ImGui::IsKeyPressed(ImGuiKey_R, false)) models.gizmo.setMode(GizmoMode::Scale);
      if (ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        models.gizmo.setSpace(models.gizmo.space() == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World);
      }
      if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameSelection(models);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) models.hierarchy.select(std::nullopt);
  }
  navigate(context, models, hovered, size.y);
  models.gizmo.setSnap(io.KeyCtrl);

  const std::optional<CameraMatrices> camera = models.camera.matrices(aspect);
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  draw.PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  if (camera.has_value()) {
    drawBounds(models, *camera, origin, size);
    if (models.showMarkers) drawMarkers(models, *camera, origin, size);
  }
  draw.PopClipRect();
  const bool grabbed = gizmo(models, camera, origin, size, hovered, navigation == Navigation::None && !io.KeyAlt);

  // A left press on the image that is not a handle, a navigation or an Alt
  // drag may become a click; its release within the slop picks (R4).
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !grabbed && !io.KeyAlt &&
      navigation == Navigation::None) {
    clickStart = io.MousePos;
  }
  if (clickStart.has_value() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    const ImVec2 at = io.MousePos;
    const bool still = length(ImVec2(at.x - clickStart->x, at.y - clickStart->y)) < kClickSlopPixels;
    if (still && shown.has_value()) {
      models.picking.click(models.hierarchy, *shown, PixelRect{origin.x, origin.y, size.x, size.y}, at.x, at.y,
                           models.showMarkers);
    }
    clickStart.reset();
  }
  if (clickStart.has_value() && !io.MouseDown[ImGuiMouseButton_Left]) clickStart.reset();
}

// Plan 0062 P10 (Spec 0062 R2): a drag that starts on the image holds the
// pointer until its button is released; motion is frame-rate independent.
void EditorView::State::navigate(const FrameContext& context, ViewModels& models, bool hovered,
                                 float viewportHeight) {
  ImGuiIO& io = ImGui::GetIO();
  if (navigation == Navigation::None && hovered && !drag.has_value()) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      navigation = io.KeyAlt ? Navigation::Dolly : Navigation::Look;
    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
      navigation = Navigation::Pan;
    } else if (io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      navigation = Navigation::Orbit;
    }
  }
  switch (navigation) {
    case Navigation::Look:
    case Navigation::Dolly:
      if (!io.MouseDown[ImGuiMouseButton_Right]) navigation = Navigation::None;
      break;
    case Navigation::Pan:
      if (!io.MouseDown[ImGuiMouseButton_Middle]) navigation = Navigation::None;
      break;
    case Navigation::Orbit:
      if (!io.MouseDown[ImGuiMouseButton_Left]) navigation = Navigation::None;
      break;
    case Navigation::None: break;
  }

  ViewportCameraState& camera = models.camera;
  const ImVec2 delta = io.MouseDelta;
  const bool moved = delta.x != 0.0f || delta.y != 0.0f;
  switch (navigation) {
    case Navigation::Look: {
      if (moved) {
        camera.beginNavigation();
        camera.editor().look(delta.x, delta.y);
      }
      if (!io.WantTextInput) {
        const auto axis = [](ImGuiKey positive, ImGuiKey negative) {
          return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
        };
        const Vec3 direction{axis(ImGuiKey_D, ImGuiKey_A), axis(ImGuiKey_E, ImGuiKey_Q), axis(ImGuiKey_W, ImGuiKey_S)};
        if (direction[0] != 0.0f || direction[1] != 0.0f || direction[2] != 0.0f) {
          camera.beginNavigation();
          camera.editor().fly(direction, context.deltaSeconds, io.KeyShift);
        }
      }
      if (io.MouseWheel != 0.0f) camera.editor().scaleFlySpeed(io.MouseWheel);
      break;
    }
    case Navigation::Orbit:
      if (moved) {
        camera.beginNavigation();
        camera.editor().orbit(delta.x, delta.y);
      }
      break;
    case Navigation::Pan:
      if (moved) {
        camera.beginNavigation();
        camera.editor().pan(delta.x, delta.y, viewportHeight);
      }
      break;
    case Navigation::Dolly:
      if (delta.y != 0.0f) {
        camera.beginNavigation();
        camera.editor().dollyDrag(delta.y);
      }
      break;
    case Navigation::None:
      if (hovered && io.MouseWheel != 0.0f) {
        camera.beginNavigation();
        camera.editor().dolly(io.MouseWheel);
      }
      break;
  }
}

// Plan 0062 P10 (Spec 0062 R6): F -- the pivot to the selection's bounds
// centre (or its origin) at the distance that fits them.
void EditorView::State::frameSelection(ViewModels& models) {
  const std::optional<MatrixColumns>& matrix = models.gizmo.matrix();
  if (!matrix.has_value()) return;
  Vec3 center{(*matrix)[3][0], (*matrix)[3][1], (*matrix)[3][2]};
  float radius = 0.0f;
  if (const auto& bounds = models.selectionBounds; bounds.has_value() && bounds->entity == models.gizmo.subject()) {
    const Vec3 localCenter{(bounds->localMin[0] + bounds->localMax[0]) * 0.5f,
                           (bounds->localMin[1] + bounds->localMax[1]) * 0.5f,
                           (bounds->localMin[2] + bounds->localMax[2]) * 0.5f};
    center = transformPoint(*matrix, localCenter);
    for (int corner = 0; corner < 8; ++corner) {
      const Vec3 world = transformPoint(*matrix, Vec3{(corner & 1) ? bounds->localMax[0] : bounds->localMin[0],
                                                      (corner & 2) ? bounds->localMax[1] : bounds->localMin[1],
                                                      (corner & 4) ? bounds->localMax[2] : bounds->localMin[2]});
      const float dx = world[0] - center[0];
      const float dy = world[1] - center[1];
      const float dz = world[2] - center[2];
      radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
  }
  models.camera.beginNavigation();
  models.camera.editor().frame(center, radius);
}

// Plan 0062 P10 (Spec 0062 R5): a glyph per marker -- a sun for a light, a
// small frustum for a camera -- the selected one highlighted.
void EditorView::State::drawMarkers(ViewModels& models, const CameraMatrices& camera, ImVec2 origin, ImVec2 size) {
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  for (const Marker& marker : models.picking.markers()) {
    if (!(viewDepth(camera, marker.position) > 0.0f)) continue;
    const std::optional<ImVec2> at = project(camera, marker.position, origin, size);
    if (!at.has_value()) continue;
    const ImU32 color = models.hierarchy.selection() == marker.entity ? kSelectionColor : kMarkerColor;
    if (marker.kind == MarkerKind::Light) {
      draw.AddCircleFilled(*at, 4.0f, color);
      for (int ray = 0; ray < 8; ++ray) {
        const float angle = static_cast<float>(ray) * 0.7853982f;
        const ImVec2 direction(std::cos(angle), std::sin(angle));
        draw.AddLine(ImVec2(at->x + direction.x * 6.0f, at->y + direction.y * 6.0f),
                     ImVec2(at->x + direction.x * 9.0f, at->y + direction.y * 9.0f), color, 1.5f);
      }
    } else {
      draw.AddRect(ImVec2(at->x - 6.0f, at->y - 4.0f), ImVec2(at->x + 2.0f, at->y + 4.0f), color);
      draw.AddTriangleFilled(ImVec2(at->x + 2.0f, at->y), ImVec2(at->x + 8.0f, at->y - 4.0f),
                             ImVec2(at->x + 8.0f, at->y + 4.0f), color);
    }
  }
}

// Plan 0062 P10 (Spec 0062 R6, ruling Q5 B-a): the selection's mesh bounds
// through its WorldMatrix, as twelve edges; an edge with an end behind the
// camera is left out.
void EditorView::State::drawBounds(ViewModels& models, const CameraMatrices& camera, ImVec2 origin, ImVec2 size) {
  const auto& bounds = models.selectionBounds;
  const std::optional<MatrixColumns>& matrix = models.gizmo.matrix();
  if (!bounds.has_value() || !matrix.has_value() || bounds->entity != models.gizmo.subject()) return;
  std::array<std::optional<ImVec2>, 8> corners;
  for (int corner = 0; corner < 8; ++corner) {
    const Vec3 world = transformPoint(*matrix, Vec3{(corner & 1) ? bounds->localMax[0] : bounds->localMin[0],
                                                    (corner & 2) ? bounds->localMax[1] : bounds->localMin[1],
                                                    (corner & 4) ? bounds->localMax[2] : bounds->localMin[2]});
    if (viewDepth(camera, world) > 0.0f) corners[static_cast<std::size_t>(corner)] = project(camera, world, origin, size);
  }
  constexpr std::array<std::pair<int, int>, 12> kEdges{
      std::pair{0, 1}, std::pair{2, 3}, std::pair{4, 5}, std::pair{6, 7}, std::pair{0, 2}, std::pair{1, 3},
      std::pair{4, 6}, std::pair{5, 7}, std::pair{0, 4}, std::pair{1, 5}, std::pair{2, 6}, std::pair{3, 7}};
  ImDrawList& draw = *ImGui::GetWindowDrawList();
  for (const auto& [a, b] : kEdges) {
    const auto& p = corners[static_cast<std::size_t>(a)];
    const auto& q = corners[static_cast<std::size_t>(b)];
    if (p.has_value() && q.has_value()) draw.AddLine(*p, *q, kSelectionColor, 1.0f);
  }
}

// R5: axis handles on the selected entity -- along the world's axes or, in
// Local space and for Scale, the entity's own (Plan 0062 P9) -- projected with
// the Viewport camera; a drag turns pointer motion into the model's drag
// amount.
bool EditorView::State::gizmo(ViewModels& models, const std::optional<CameraMatrices>& cameraMatrices, ImVec2 origin,
                              ImVec2 size, bool hovered, bool mayGrab) {
  GizmoModel& model = models.gizmo;
  if (!cameraMatrices.has_value() || !model.available()) {
    if (drag.has_value()) {
      model.endDrag();
      drag.reset();
    }
    return false;
  }
  const CameraMatrices& camera = *cameraMatrices;
  const MatrixColumns& matrix = *model.matrix();
  const Vec3 position{matrix[3][0], matrix[3][1], matrix[3][2]};
  // gizmo.h's handle math (ADR-0137 D3): the layout, the grab and the drag amount.
  std::array<Vec3, 3> directions{};
  for (std::size_t axis = 0; axis < 3; ++axis) directions[axis] = model.axisDirection(static_cast<GizmoAxis>(axis));
  const std::optional<GizmoHandles> handles =
      gizmoHandles(camera, PixelRect{origin.x, origin.y, size.x, size.y}, position, directions);
  if (!handles.has_value()) return false;

  ImDrawList& draw = *ImGui::GetWindowDrawList();
  draw.PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
  const ImVec2 pointer = ImGui::GetIO().MousePos;
  for (std::size_t axis = 0; axis < 3; ++axis) {
    if (!(*handles)[axis].has_value()) continue;
    const GizmoHandle& handle = *(*handles)[axis];
    const ImVec2 handleOrigin = toImVec2(handle.origin);
    const ImVec2 handleEnd = toImVec2(handle.end);
    const ImVec2 direction = toImVec2(handle.direction);
    const bool active = drag.has_value() && static_cast<std::size_t>(drag->axis) == axis;
    const ImU32 color = active ? kActiveAxisColor : kAxisColors[axis];
    switch (model.mode()) {
      case GizmoMode::Translate:
        draw.AddLine(handleOrigin, handleEnd, color, 3.0f);
        draw.AddTriangleFilled(ImVec2(handleEnd.x + direction.x * 10.0f, handleEnd.y + direction.y * 10.0f),
                               ImVec2(handleEnd.x - direction.y * 5.0f, handleEnd.y + direction.x * 5.0f),
                               ImVec2(handleEnd.x + direction.y * 5.0f, handleEnd.y - direction.x * 5.0f),
                               color);
        break;
      case GizmoMode::Scale:
        draw.AddLine(handleOrigin, handleEnd, color, 3.0f);
        draw.AddRectFilled(ImVec2(handleEnd.x - 5.0f, handleEnd.y - 5.0f), ImVec2(handleEnd.x + 5.0f, handleEnd.y + 5.0f),
                           color);
        break;
      case GizmoMode::Rotate:
        draw.AddCircle(handleEnd, kGizmoRotateRingPixels, color, 0, 3.0f);
        draw.AddLine(handleOrigin, handleEnd, color, 1.0f);
        break;
    }
  }
  draw.PopClipRect();

  ImGuiIO& io = ImGui::GetIO();
  bool grabbed = false;
  if (!drag.has_value() && hovered && mayGrab && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    drag = gizmoGrab(*handles, directions, camera, model.mode(), {pointer.x, pointer.y});
    if (drag.has_value()) {
      model.beginDrag(drag->axis);
      grabbed = true;
    }
  }
  if (!drag.has_value()) return grabbed;
  if (!io.MouseDown[ImGuiMouseButton_Left]) {
    model.endDrag();
    drag.reset();
    return grabbed;
  }
  model.dragTo(gizmoDragAmount(model.mode(), *drag, {pointer.x, pointer.y}));
  return grabbed;
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
