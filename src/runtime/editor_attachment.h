#pragma once

// Plan 0056 P6 (ADR-0108 D1, D2; J4): the adapter between Atlantis Editor and
// RuntimeApplication's frame overlay. Private to the atlantis_runtime
// executable's startup layer, like cli.h: NOT under
// src/runtime/include/atlantis/runtime/, NOT compiled into
// atlantis_runtime_host -- the host library never links the editor. The same
// editor_attachment.cpp is compiled into atlantis_runtime and the Runtime's GPU
// test executable.

#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/editor/editor.h>
#include <atlantis/editor/input.h>
#include <atlantis/platform/platform_event.h>
#include <atlantis/renderer/ui_overlay.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/frame_overlay.h>
#include <atlantis/runtime/runtime_application.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace atlantis::runtime {

// An editor attached to a running application: it opens the editor's
// connection to the application, translates the frame's Platform input
// events into the editor's input values, runs the editor's UI frame between
// Runtime frames (update(), J3), and hands the frame the editor's draw list
// in the Renderer's shape. Borrows the application and the control, which
// must outlive it; the application must be detached (detachOverlay()) before
// this is destroyed. Frame thread only (ADR-0004).
class EditorAttachment final : public FrameOverlay {
 public:
  EditorAttachment(RuntimeApplication& application, atlantis::connection::RuntimeControl& control);
  ~EditorAttachment() override;
  EditorAttachment(const EditorAttachment&) = delete;
  EditorAttachment& operator=(const EditorAttachment&) = delete;

  // The editor's frame, between Runtime frames: the input the last frame's
  // pump delivered, the window size, and -- only while the editor shows a
  // Gizmo (J7) -- the frame's camera from captureFrameData(). Every
  // connection call happens here, between frames (ADR-0105).
  void update();

  [[nodiscard]] atlantis::editor::Editor& editor() noexcept { return *editor_; }

  // FrameOverlay.
  void onPlatformEvent(const atlantis::platform::PlatformEvent& event) override;
  [[nodiscard]] atlantis::rhi::Extent2D viewportExtent() const override;
  [[nodiscard]] const atlantis::renderer::UiDrawList& drawList() const override;
  [[nodiscard]] FontAtlasPixels fontAtlas() const override;

 private:
  RuntimeApplication& application_;
  std::unique_ptr<atlantis::connection::RuntimeConnection> connection_;
  std::unique_ptr<atlantis::editor::Editor> editor_;
  atlantis::editor::FontAtlas atlas_;
  std::vector<atlantis::editor::InputEvent> pending_;
  atlantis::renderer::UiDrawList drawList_;
  std::uint32_t windowWidth_ = 1280;
  std::uint32_t windowHeight_ = 720;
  std::optional<std::chrono::steady_clock::time_point> lastUpdate_;
};

// The editor's input value for a Platform input event; empty for every other
// event.
[[nodiscard]] std::optional<atlantis::editor::InputEvent> toEditorInput(const atlantis::platform::PlatformEvent& event);

// The editor's draw list in the Renderer's shape (the same fields, two owners).
void toRendererDrawList(const atlantis::editor::UiDrawList& from, atlantis::renderer::UiDrawList& to);

}  // namespace atlantis::runtime
