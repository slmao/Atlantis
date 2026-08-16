#pragma once

#include <span>

#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/texture.h>

namespace atlantis::renderer {

// Stateless orchestrator (ADR-0022): retains no GPU resource, no
// frame-to-frame state, across calls. Depends only on RHI, RenderGraph,
// Core -- never Platform, Vulkan Backend, or any Vk* type. Not internally
// thread-safe; caller-thread-only (ADR-0004).
//
// Deliberately copyable and movable, unlike RenderGraphBuilder
// (Spec 0005/ADR-0017's non-copyable/non-movable builder) -- that
// restriction exists specifically because RenderGraphBuilder vends
// PassHandle/ResourceHandle values whose provenance is tied to the
// builder's own stable address. Renderer has no handles, no vended
// identity, and no member state of any kind, so every special member is
// left at its trivial compiler-generated default.
class Renderer {
 public:
  Renderer() = default;

  // Builds, compiles, and executes one RenderGraph draw pass into
  // commandList -- never calls Device::submit()/Presentation::present()
  // itself (ADR-0022). colorTarget/depthTarget/cameraUniformBuffer are
  // borrowed references the caller already owns and (for
  // cameraUniformBuffer) has already written this frame's view/
  // projection matrices into -- Renderer never touches raw camera math,
  // only binds the Buffer it is handed. drawItems is an ordinary
  // caller-supplied span, iterated once, retained nowhere.
  //
  // finalColorState: required, backend-agnostic (Spec 0010/ADR-0022
  // Accepted Amendment) -- the state colorTarget must be left in when
  // this call returns. Passed through unmodified as the internal draw
  // pass's trailing-transition target; Renderer does not interpret,
  // validate, or branch on this value, and gains no knowledge of why the
  // caller chose it. A windowed caller supplies
  // atlantis::rhi::ResourceState::PresentSource; a headless caller
  // supplies atlantis::rhi::ResourceState::TransferSource directly.
  void drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
                 atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
                 std::span<const DrawItem> drawItems, atlantis::rhi::ResourceState finalColorState);
};

}  // namespace atlantis::renderer
