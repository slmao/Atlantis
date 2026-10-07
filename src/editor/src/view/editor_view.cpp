#include "editor_view.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <variant>

namespace atlantis::editor::detail {

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

}  // namespace

struct EditorView::State {
  ImGuiContext* context = nullptr;
  FontAtlas fontAtlas;
  UiDrawList drawList;
  ViewportSize viewportSize;

  void feed(std::span<const InputEvent> input);
  void layout();
  void translate(const ImDrawData& data);
};

EditorView::EditorView() : state_(std::make_unique<State>()) {
  ImGuiContext* previous = ImGui::GetCurrentContext();
  state_->context = ImGui::CreateContext();
  ImGui::SetCurrentContext(state_->context);
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
  if (state_ && state_->context != nullptr) ImGui::DestroyContext(state_->context);
}

const FontAtlas& EditorView::fontAtlas() const { return state_->fontAtlas; }

void EditorView::frame(std::span<const InputEvent> input, const FrameContext& context) {
  const ContextScope scope(state_->context);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(static_cast<float>(context.windowWidth), static_cast<float>(context.windowHeight));
  io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
  io.DeltaTime = context.deltaSeconds > 0.0f ? context.deltaSeconds : 1.0f / 60.0f;
  state_->feed(input);

  ImGui::NewFrame();
  state_->layout();
  ImGui::Render();
  state_->translate(*ImGui::GetDrawData());
}

const UiDrawList& EditorView::drawList() const { return state_->drawList; }

ViewportSize EditorView::viewportSize() const { return state_->viewportSize; }

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
            const std::string text(e.utf8.data(), e.size);
            io.AddInputCharactersUTF8(text.c_str());
          }
        },
        event);
  }
}

// The fixed layout (ruling Q6 keeps the Content quadrant): Hierarchy | Viewport
// over Content | Inspector, filling the window. No docking, nothing movable.
void EditorView::State::layout() {
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
  ImGui::Begin("Hierarchy", nullptr, kFixed);
  ImGui::End();

  place(leftWidth, 0.0f, display.x - leftWidth, topHeight);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("Viewport", nullptr, kFixed | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  viewportSize = ViewportSize{static_cast<std::uint32_t>(std::max(available.x, 0.0f)),
                              static_cast<std::uint32_t>(std::max(available.y, 0.0f))};
  if (viewportSize.width > 0 && viewportSize.height > 0) ImGui::Image(ImTextureRef(kViewportTextureId), available);
  ImGui::End();

  place(0.0f, topHeight, leftWidth, display.y - topHeight);
  ImGui::Begin("Content", nullptr, kFixed);
  ImGui::End();

  place(leftWidth, topHeight, display.x - leftWidth, display.y - topHeight);
  ImGui::Begin("Inspector", nullptr, kFixed);
  ImGui::End();
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
