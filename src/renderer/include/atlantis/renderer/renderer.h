#pragma once

#include <span>

#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/renderer/environment_lighting.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/shadow_map.h>
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
  // this call returns. Passed through unmodified as the output-
  // transform pass's own trailing-transition target; Renderer does not
  // interpret, validate, or branch on this value, and gains no
  // knowledge of why the caller chose it. A windowed caller supplies
  // atlantis::rhi::ResourceState::PresentSource; a headless caller
  // supplies atlantis::rhi::ResourceState::TransferSource directly.
  //
  // Plan 0024 Milestone 5 (ADR-0068 D-1/D-3): five new, all-borrowed
  // parameters -- Renderer stays a stateless orchestrator, owning none
  // of them. hdrColorTarget is the scene-referred linear HDR
  // intermediate the geometry pass now writes into instead of
  // colorTarget directly. fullscreenTriangleVertexBuffer/
  // ...IndexBuffer are the output-transform pass's own fixed, 3-vertex/
  // 3-index geometry (never scene content). outputTransformPipeline/
  // ...Sampler are whichever of the two closed *_Unorm/*_Srgb variants
  // the caller has already selected to match colorTarget's own real
  // format class (ADR-0068 D-6) -- Renderer performs no such
  // classification itself and holds no format-dependent state; it
  // draws whatever Pipeline it is handed, identically either way.
  // Still exactly one CommandList, one RenderGraphBuilder::compile()/
  // render_graph::execute() call pair -- Renderer still never calls
  // Device::submit()/Presentation::present() itself.
  // environmentLighting is a nullable, frame-scoped borrowed view.
  // MaterialEnvironmentBinding::None never reads it; Ibl requires it
  // and binds the cubemap/LUT at slots 2/3.
  //
  // Plan 0026 Milestone 2 (ADR-0071): skyPipeline is a nullable, caller-
  // owned borrowed Pipeline. When non-null, it must be paired with a
  // non-null environmentLighting (ATLANTIS_CHECK_MSG otherwise) -- the
  // sky reuses environmentLighting's own prefilteredEnvironment/
  // environmentSampler and the existing fullscreenTriangleVertexBuffer/
  // ...IndexBuffer above, needing no further caller-owned resource.
  // Per ADR-0071's own Proposed Correction, the sky draw is issued
  // strictly before every DrawItem, every frame -- a correctness
  // requirement (an opaque DrawItem drawn first could leave a real depth
  // value the sky's own fixed depth would incorrectly pass against, sky
  // Pipeline's own depthWriteEnabled = false notwithstanding), not an
  // implementation convenience. nullptr draws no sky at all -- every
  // existing caller/scene without an environment is unaffected.
  //
  // Plan 0027 Milestone 9 (ADR-0072 D-1/P6): five new, all-borrowed,
  // required parameters appended after skyPipeline -- shadow
  // infrastructure is unconditionally created (Milestone 8), unlike
  // skyPipeline's own nullable shape, so none of these five are
  // pointers or carry a null case. shadowCasterDrawItems is
  // independent from drawItems above -- Renderer never derives one from
  // the other and never inspects Mesh layout; Runtime passes
  // shadowCasterDrawItems = drawItems when a directional light is
  // configured, an empty span otherwise. A new RenderGraph "shadow"
  // pass, compiled first, writes shadowMap; "draw" also reads it. The
  // shadow-map binding index (2 or 4) is decided by each PbrDirectLit
  // DrawItem's own Material::environmentBinding(), never a new
  // MaterialKind.
  //
  // Discovered during Implementation: appending five required
  // (non-default) parameters after environmentLighting/skyPipeline
  // above is not expressible while those two keep their own `= nullptr`
  // default (C++ forbids a non-default parameter after a defaulted
  // one). Both defaults are therefore dropped here -- environmentLighting/
  // skyPipeline remain the exact same nullable pointer types with the
  // exact same nullptr-means-"none" meaning, now simply always passed
  // explicitly. Every call site in the repository is updated in this
  // same commit (P9(e)); this is a mechanical, disclosed, zero-behavior
  // signature correction, not a design change.
  void drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
                 atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
                 std::span<const DrawItem> drawItems, atlantis::rhi::ResourceState finalColorState,
                 atlantis::rhi::HdrColorTarget& hdrColorTarget,
                 atlantis::rhi::Buffer& fullscreenTriangleVertexBuffer,
                 atlantis::rhi::Buffer& fullscreenTriangleIndexBuffer,
                 atlantis::rhi::Pipeline& outputTransformPipeline, atlantis::rhi::Sampler& outputTransformSampler,
                 const EnvironmentLighting* environmentLighting, atlantis::rhi::Pipeline* skyPipeline,
                 atlantis::rhi::ShadowMap& shadowMap, atlantis::rhi::Sampler& shadowMapSampler,
                 atlantis::rhi::Pipeline& shadowCastPipeline, atlantis::rhi::Buffer& shadowLightSpaceBuffer,
                 std::span<const DrawItem> shadowCasterDrawItems);
};

}  // namespace atlantis::renderer
