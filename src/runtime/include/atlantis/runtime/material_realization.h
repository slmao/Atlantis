#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/renderer/material.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/types.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace atlantis::runtime {

// Plan 0018 Section P12/P13 (Spec 0018 D8/D9): Runtime-private,
// GPU-dependent realization logic for MaterialKind::UnlitTextured --
// factored out of runtime_application.cpp's own anonymous namespace so
// this logic is directly reusable by Milestone 16's own image-regression
// fixture, matching scene_extraction.cpp/scene_load.cpp's own already-
// established "factored out for testability" precedent exactly. Every
// function below takes its dependencies as explicit parameters -- never
// reads RuntimeApplication's own private state directly -- and is
// called identically by runFrame() and by the fixture. No new *public*
// API is introduced outside Atlantis::RuntimeHost: this header lives
// under atlantis::runtime, the same Runtime-private namespace
// scene_extraction.h already uses.

enum class MaterialRealizationError {
  SamplerCreateFailed,
  SampledTextureCreateFailed,
  StagingBufferCreateFailed,
  MaterialCreateFailed,
};

// A single successfully-realized material's own new GPU resources --
// entirely function-local/RAII-owned until the caller decides to keep
// them (Spec 0018 D8 step 2's own "candidate bundle"). Each owning
// member is a unique_ptr, so its pointee's own address never changes
// once allocated -- moving this whole struct (e.g. inside the
// std::unordered_map realizePendingMaterials() below returns) relocates
// only the unique_ptr's own bookkeeping, never the pointee a borrowed
// raw pointer might already reference (Human Review Approval items 1/3).
//
// newSampledTexture is std::nullopt precisely when this candidate's own
// textureAssetId is ALREADY a key in the caller's current
// sampledTextureResourceMap_ (a second material naming an already-
// realized texture, D10 dedup) -- realizeOneMaterialCandidate() itself
// never re-uploads a texture it can already see is present; the caller
// is responsible for actually reusing the existing entry when this
// field is empty. stagingBuffer is deliberately NOT part of the
// persisted resource shape (SampledTexture/Sampler/Material) -- it is
// populated only alongside newSampledTexture, and its own lifetime is
// the CALLER's responsibility (runFrame() keeps a frame-local
// std::vector<Buffer> of every staging buffer created this frame,
// destroyed via RAII only after that frame's own waitIdle() succeeds) --
// it is never moved into any persistent RuntimeApplication-owned map.
struct RealizedMaterialCandidate {
  atlantis::asset_system::AssetId materialAssetId = 0;
  atlantis::asset_system::AssetId textureAssetId = 0;
  std::unique_ptr<atlantis::rhi::SampledTexture> newSampledTexture;  // nullptr if textureAssetId is already realized
  std::optional<std::unique_ptr<atlantis::rhi::Buffer>> stagingBuffer;  // present iff newSampledTexture is non-null
  std::unique_ptr<atlantis::rhi::Sampler> sampler;                       // always new -- keyed per material
  std::unique_ptr<atlantis::renderer::Material> material;                // always new -- keyed per material
};

// Step 2 of Spec 0018 D8: attempts one material's own full realization
// as one local, all-or-nothing sequence. Never touches any
// RuntimeApplication-owned map -- purely functional given its inputs.
// effectiveSampledTextures is a non-owning, borrowed-pointer view --
// seeded by the caller from sampledTextureResourceMap_'s own current
// entries, and (Human Review Approval item 3's own same-frame dedup
// fix) ALSO extended by realizePendingMaterials() below with any
// texture a PRIOR candidate in this same frame's own pendingIds list
// already created, so two materials newly realized in the same frame
// that name the same texture still upload it only once. Consulted to
// decide whether this candidate needs its own new SampledTexture/
// staging upload at all (D10 dedup) AND, when it does not, to borrow
// the already-realized SampledTexture's own real pointer for the new
// Material's own construction. Never mutated here; a nullopt
// newSampledTexture in the return value means "the caller already has
// this one" (either persistently or from earlier this same frame), not
// "this candidate does not need it." On success, the caller is
// responsible for recording the returned candidate's own texture-upload
// RenderGraph pass (when newSampledTexture is non-null) into the shared
// CommandList (step 3) before this candidate is ever moved into a
// persistent map.
// Plan 0019 Section P6: litTexturedVertexInputLayout/litTexturedVertexSpirv/
// litTexturedFragmentSpirv, inserted immediately after the existing
// unlitTexturedFragmentSpirv parameter -- the unlitTextured* trio's own
// signature position is unchanged. This function itself selects between
// the two shader pairs via the shared, file-local selectShaderPair()
// helper (material_realization.cpp), keyed off materialData.kind --
// never a second, separately-written switch.
// Plan 0024 Milestone 6 (ADR-0068 D-1/D-3): the former colorFormat
// parameter (a real atlantis::rhi::Format, once threaded straight into
// this call's own createMaterial() invocation) is removed -- every
// geometry Pipeline now renders into the fixed HDR intermediate
// (HdrFormat::Rgba16Float), never the caller's final swapchain/
// offscreen Format, so no per-call format ever needs to reach this
// function at all.
// Plan 0023 Milestone 5: pbrDirectLitVertexInputLayout/
// pbrDirectLitVertexSpirv/pbrDirectLitFragmentSpirv, inserted
// immediately after the litTextured* trio, mirroring its own insertion
// point exactly. The PbrDirectLit-only base-color-texture Rgba8Srgb
// requirement (ADR-0066 item 6) is NOT checked here -- it runs earlier,
// at Runtime's own Phase 1 scene-dependency-resolution point
// (scene_load.cpp, ADR-0060 Decision 6), the first point in the
// pipeline with both a material's own kind and its resolved texture's
// own real colorSpace, and the only point whose own Result error type
// (RuntimeInitError) can actually carry the ADR's own required
// RuntimeInitError::PbrBaseColorTextureNotSrgb sub-code -- this
// function's own Result error type (MaterialRealizationError) is a
// different, steady-state-retry-shaped domain (a failure here simply
// leaves a material pending, retried next frame, never a fatal scene-
// load failure), so it cannot mechanically carry that sub-code.
[[nodiscard]] atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError> realizeOneMaterialCandidate(
    atlantis::rhi::Device& device, const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblFragmentSpirv, bool environmentEnabled,
    atlantis::asset_system::AssetId materialAssetId,
    const atlantis::asset_system::MaterialAssetData& materialData,
    const atlantis::asset_system::TextureAssetData& textureData,
    const std::unordered_map<atlantis::asset_system::AssetId, const atlantis::rhi::SampledTexture*>&
        effectiveSampledTextures);

// Step 1 of Spec 0018 D8: the pending set is a pure function of current
// state, recomputed every frame -- never a persisted queue. Returns a
// std::vector, not a std::unordered_set -- its own ORDER is what
// realizePendingMaterials() below iterates to record upload passes, so
// this order must itself be deterministic; the caller derives
// referencedIds from World::renderableEntities()'s own already-
// deterministic iteration (never an unordered_map), matching how
// knownMeshAssetIds is already collected in runFrame() today.
// alreadyRealizedIds is materialResourceMap_'s own current key set.
[[nodiscard]] std::vector<atlantis::asset_system::AssetId> computePendingMaterialIds(
    const std::vector<atlantis::asset_system::AssetId>& referencedIds,
    const std::vector<atlantis::asset_system::AssetId>& alreadyRealizedIds);

// Steps 2-3 combined, for every pending id this frame, IN pendingIds'
// OWN ORDER (never re-sorted, never bucketed by AssetId hash -- Human
// Review Approval item 3's own determinism requirement): builds every
// candidate that succeeds, recording each one's upload pass into
// commandList (before the caller's own subsequent draw-graph recording)
// -- never partially recording a candidate that itself failed at any
// sub-step. Returns the set of successfully-realized candidates, keyed
// by material AssetId, for the caller to (a) build this frame's own
// DrawItems from directly (Spec 0018 D8 step 3's own same-frame-visible
// guarantee), (b) collect every returned candidate's own stagingBuffer
// (when present) into its own frame-local list, destroyed only after
// this frame's own waitIdle() succeeds, and (c), only after this
// frame's submit()+conditional waitIdle() both succeed, move each
// candidate's own newSampledTexture/sampler/material into the
// persistent resource maps -- this function itself never touches those
// maps.
// Plan 0019 Section P6: gains the identical litTextured* trio, threaded
// straight through to realizeOneMaterialCandidate() unchanged in kind
// from how unlitTextured* is already threaded -- this function never
// calls selectShaderPair() itself.
// Plan 0023 Milestone 5: gains the identical pbrDirectLit* trio, same
// insertion point and threading as litTextured*.
// Plan 0024 Milestone 6: the former colorFormat parameter is removed,
// mirroring realizeOneMaterialCandidate()'s own identical removal --
// this function only ever forwarded it unchanged.
[[nodiscard]] std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizePendingMaterials(
    atlantis::rhi::Device& device, atlantis::rhi::CommandList& commandList,
    const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrIblVertexInputLayout,
    const std::vector<std::uint32_t>& pbrIblVertexSpirv,
    const std::vector<std::uint32_t>& pbrIblFragmentSpirv, bool environmentEnabled,
    const std::vector<atlantis::asset_system::AssetId>& pendingIds,
    const std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>&
        sampledTextureResourceMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>&
        materialDataMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData>&
        textureDataMap);

// Compatibility overload for every no-environment composition root. It keeps
// the pre-Spec-0025 call shape and selects pbr_direct_lit exactly.
[[nodiscard]] inline std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate>
realizePendingMaterials(
    atlantis::rhi::Device& device, atlantis::rhi::CommandList& commandList,
    const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const std::vector<atlantis::asset_system::AssetId>& pendingIds,
    const std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>&
        sampledTextureResourceMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>&
        materialDataMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData>&
        textureDataMap) {
  return realizePendingMaterials(
      device, commandList, unlitTexturedVertexInputLayout, unlitTexturedVertexSpirv,
      unlitTexturedFragmentSpirv, litTexturedVertexInputLayout, litTexturedVertexSpirv,
      litTexturedFragmentSpirv, pbrDirectLitVertexInputLayout, pbrDirectLitVertexSpirv,
      pbrDirectLitFragmentSpirv, pbrDirectLitVertexInputLayout, pbrDirectLitVertexSpirv,
      pbrDirectLitFragmentSpirv, false, pendingIds, sampledTextureResourceMap, materialDataMap,
      textureDataMap);
}

// Plan 0024 Milestone 6 (correction, ADR-0068 D-4, discovered during
// Implementation -- Human Review direction, chat, 2026-09-01): every
// geometry Pipeline (fallback and every MaterialKind) now targets the
// fixed HdrFormat::Rgba16Float, never the caller's real, negotiated
// Format -- D-4's own text is explicit that this means NONE of them
// participate in format-change rebuild any longer ("this is a real
// simplification, not merely a substitution"). The former
// rebuildMaterialsForFormatChange()/FormatRebuildCandidates (Spec 0018
// Section P13/D9) existed ONLY to rebuild format-dependent geometry
// Pipelines on a format-change event -- with nothing left for it to
// do, it is retired here rather than kept as dead production code.
// fallbackMaterial_ is now created once at startup
// (runtime_application.cpp's own initializeSteps()), exactly like
// realizeOneMaterialCandidate()'s own materials already were --
// neither needs any format-triggered rebuild path any more. Only the
// output-transform Pipeline (D-6) still varies with the final target's
// negotiated format; runFrame()'s own format-change branch builds and
// swaps that one Pipeline directly (a raw Device::createPipeline()
// call, not a Material -- see runtime_application.cpp), with no
// intermediate candidate-bundle type needed for a single resource.

// Plan 0024 Milestone 6 (ADR-0068 D-6): the one place that decides
// which of the two output-transform shader contracts (unorm/srgb) a
// given final Format needs -- mirrors selectShaderPair()'s own closed-
// switch/no-default shape exactly (material_realization.cpp). Called
// both by runFrame()'s own format-change branch (to decide which of
// outputTransformUnormPipeline_/...SrgbPipeline_ needs rebuilding, and
// which one drawFrame() should be handed this frame) and by every
// image-regression fixture's own construction-time Pipeline selection
// (Milestone 7, no runtime format-change path exists there -- each
// fixture calls this once).
[[nodiscard]] bool isSrgbFormat(atlantis::rhi::Format format);

// Plan 0027 Milestone 9 (ADR-0072 D-7): the one place that decides a
// geometry Pipeline's own sampledTextureBindingCount for a given
// MaterialKind -- mirrors isSrgbFormat()'s own "closed switch, exposed
// for direct GPU-independent testing" shape exactly. UnlitTextured/
// LitTextured are always 1 (their own single material-texture binding;
// unlit_textured.slang/lit_textured.slang were never modified to declare
// a shadow-map binding). PbrDirectLit is 2 without an environment
// (base-color@1, shadow-map@2) or 4 with one (base-color@1, environment
// cubemap@2, DFG LUT@3, shadow-map@4) -- pbr_direct_lit.slang/pbr_ibl.slang
// both declare exactly that many bindings (Milestone 5).
[[nodiscard]] std::uint32_t sampledTextureBindingCountFor(atlantis::asset_system::MaterialKind kind,
                                                           bool environmentEnabled);

}  // namespace atlantis::runtime
