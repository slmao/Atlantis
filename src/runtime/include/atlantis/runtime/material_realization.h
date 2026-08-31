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
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv, atlantis::asset_system::AssetId materialAssetId,
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
    const std::vector<atlantis::asset_system::AssetId>& pendingIds,
    const std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>&
        sampledTextureResourceMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>&
        materialDataMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData>&
        textureDataMap);

// Spec 0018 D9 steps 2-4: builds a COMPLETE candidate batch (the
// fallback Material plus one rebuilt Material per CURRENT
// materialResourceMap_ entry, each reusing that entry's own already-
// uploaded SampledTexture/Sampler unchanged, via a borrowed raw pointer
// into the CALLER's still-live sampledTextureResourceMap_/
// samplerResourceMap_ -- no new upload, no CommandList, no submit()).
// Plan 0024 Milestone 6: every rebuilt Pipeline now targets the fixed
// HdrFormat::Rgba16Float (never a real, caller-supplied Format) -- see
// this function's own trailing comment for why it is still called on
// every format-change event regardless. Never touches currentMaterials, never
// touches the caller's own fallbackMaterial_ -- purely constructs new,
// local unique_ptr<Material> objects. Returns Err on the FIRST
// sub-failure (fallback or any one entry) -- the caller (runFrame())
// discards the returned partial state via ordinary RAII and leaves the
// EXISTING fallbackMaterial_/materialResourceMap_ completely untouched;
// no partial candidate is ever returned as if it were usable.
//
// CRITICAL (Human Review Approval item 2): the caller must not swap
// this result in until its OWN subsequent submit() call (recording the
// frame's real draw graph using this candidate batch) has returned Ok
// -- this function itself has no awareness of submit()/waitIdle() at
// all, and never destroys the caller's existing fallback/map. The old
// Material/Pipeline bundle the caller still holds must remain alive and
// untouched until that submit() confirms (via its own internal
// waitAndReleaseRetainedSubmission() drain) that the previous frame's
// GPU work -- the last work that could have referenced the OLD
// Pipeline -- has finished.
struct FormatRebuildCandidates {
  std::unique_ptr<atlantis::renderer::Material> fallback;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>> materials;
  // Plan 0024 Milestone 6 (ADR-0068 D-6): the one output-transform
  // Pipeline variant this format-change event actually needs (which of
  // the two, decided by isSrgbFormat() below) -- populated by the
  // CALLER (runFrame()'s own format-change branch), not by
  // rebuildMaterialsForFormatChange() itself, which stays scoped to
  // Material construction only; a raw output-transform Pipeline is not
  // a Material (no push constants, no sampled-texture/sampler pair in
  // the Material sense) and is created directly via
  // Device::createPipeline(), the same way cameraBuffer_'s own startup
  // creation calls Device::createBuffer() directly rather than through
  // this module. Default-constructed (nullptr) whenever this candidate
  // batch's own caller never populated it -- never reachable as null
  // once the swap-in point (:878-882) actually assigns it into the
  // live outputTransform*Pipeline_ member, since that assignment is
  // only ever reached after a successful create.
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;
};

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

// Deliberately takes no separate sampledTextureResourceMap_/
// samplerResourceMap_ parameter: each rebuilt Material reuses its own
// EXISTING SampledTexture*/Sampler* by reading them straight off the
// CURRENT Material object being replaced (Material::sampledTexture()/
// sampler() return the exact same borrowed pointer passed at that
// object's own construction) -- no separate AssetId-keyed lookup is
// needed to obtain them, avoiding an unnecessary indirection.
//
// Plan 0019 Section P6: gains the litTextured* trio (inserted
// immediately after unlitTexturedFragmentSpirv, mirroring
// realizeOneMaterialCandidate()'s own insertion point) AND a new
// materialDataMap parameter -- without it, this function has no way to
// look up a given existing Material's own real MaterialKind and would
// silently rebuild every material, LitTextured included, through
// whichever shader pair happened to be named; materialDataMap is keyed
// identically to currentMaterials (both by material AssetId), so every
// id in currentMaterials is required to already have a matching entry
// here.
//
// Plan 0023 Milestone 5: gains the identical pbrDirectLit* trio,
// inserted immediately after litTextured*, mirroring
// realizeOneMaterialCandidate()'s own insertion point.
//
// Plan 0024 Milestone 6: the former newColorFormat parameter is
// removed -- every rebuilt geometry Pipeline (fallback and every
// entry) now targets the fixed HdrFormat::Rgba16Float, never the
// caller's real swapchain Format, so this function's own rebuild no
// longer depends on which format actually changed. The returned
// FormatRebuildCandidates::outputTransformPipeline field is left
// default-constructed (nullptr) by this function -- see that field's
// own comment for why populating it is the CALLER's responsibility,
// not this function's.
[[nodiscard]] atlantis::Result<FormatRebuildCandidates, MaterialRealizationError> rebuildMaterialsForFormatChange(
    atlantis::rhi::Device& device, const atlantis::rhi::VertexInputLayout& fallbackVertexInputLayout,
    const std::vector<std::uint32_t>& fallbackVertexSpirv, const std::vector<std::uint32_t>& fallbackFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& unlitTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv,
    const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& litTexturedVertexInputLayout,
    const std::vector<std::uint32_t>& litTexturedVertexSpirv,
    const std::vector<std::uint32_t>& litTexturedFragmentSpirv,
    const atlantis::rhi::VertexInputLayout& pbrDirectLitVertexInputLayout,
    const std::vector<std::uint32_t>& pbrDirectLitVertexSpirv,
    const std::vector<std::uint32_t>& pbrDirectLitFragmentSpirv,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>&
        materialDataMap,
    const std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>&
        currentMaterials);

}  // namespace atlantis::runtime
