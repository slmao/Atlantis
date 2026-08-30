# Spec: PBR Material Foundation (Direct Lighting)

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-30
- **Related Plan(s):** none yet — this Spec's own Human Review Approval,
  once recorded, authorizes drafting a Plan only, not any Implementation,
  matching every prior Spec in this repository's own governance pattern.
- **Related ADR(s):**
  [ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Proposed` — Material asset parameter set, artifact schema, base-color
  texture color-space contract) and
  [ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (`Proposed` — exact BRDF, push-constant extension, rendering contract),
  deliberately two single-responsibility ADRs, not one, per this Spec's
  own drafting brief.

## Summary

This Spec proposes a minimal, physically-based direct-lighting Material
kind — `MaterialKind::PbrDirectLit` — implementing a metallic-roughness
Cook-Torrance BRDF (Lambertian diffuse + GGX/Smith-Schlick specular)
against the existing Directional/Point light data this codebase already
computes and uploads every frame (Spec 0022). It continues this
codebase's existing single-base-color-texture-plus-scalar-parameters
Material architecture — no second texture, no normal map, no IBL, no
shadows — specifically to avoid reopening Spec 0021's own
"at most one combined-image-sampler descriptor per Pipeline" descriptor-
pool-capacity proof. Every claim in this document is grounded in a
direct, file-and-line read of this codebase's own real, current source,
performed before any design recommendation was written — see "Pre-draft
verification against real, current source" below.

## Motivation / Problem Statement

`LitTextured` (Spec 0019) gave this engine its first real per-fragment
lighting, but its own BRDF is a pure N·L diffuse accumulation with no
specular term, no roughness/metallic concept, and no physically-motivated
Fresnel or energy-conservation behavior — confirmed by a full read of
`lit_textured.slang` (`shaders/lit_textured/lit_textured.slang:81-97`).
This is a real, disclosed gap for any content that wants to show a
metallic or glossy surface responding believably to the two light kinds
this engine already extracts, uploads, and re-extracts every frame
(Spec 0022, `Approved`, merged via
[PR #106](https://github.com/slmao/Atlantis/pull/106)). This Spec was
directed by the human maintainer as the next drafting priority, ahead of
Android Platform, Shadow Foundation, IBL, and Post-processing — see
"Cross-cutting note (governance)" below.

## Goals

1. A new, closed `MaterialKind::PbrDirectLit`, rendering a precisely
   specified metallic-roughness Cook-Torrance BRDF for Directional and
   Point lights only, against the existing, unmodified 176-byte
   `FrameLightingData`/304-byte Camera+Lighting buffer.
2. Continue the existing single-base-color-texture architecture — a
   `PbrDirectLit` Material references exactly one texture (reusing the
   existing `MaterialAssetData::textureAsset` field, unchanged in type
   and meaning) plus three new scalar parameters
   (`baseColorFactor`/`metallicFactor`/`roughnessFactor`), never a
   second texture binding — so Spec 0021's own descriptor-pool-capacity
   proof (at most one combined-image-sampler descriptor per Pipeline)
   continues to hold without re-derivation.
3. Real mesh normal (Spec 0020) and UV0 (Spec 0017) are the only
   geometric inputs this BRDF consumes beyond position — no tangent, no
   normal map.
4. `UnlitTextured`/`LitTextured` remain byte-for-byte/pixel-for-pixel
   unchanged — their own `.slang` source, `VkPipelineLayout`/
   `VkPushConstantRange` objects, and all five existing goldens are
   unmodified by this Spec.
5. Every new per-material parameter travels through the existing
   cook/artifact/metadata/load Asset System chain
   ([ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)),
   and the existing per-draw bind/push chain
   ([ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)),
   with no new Asset Catalog, Material Graph, Serialization module, or
   third-party dependency.

## Non-Goals

- **Image-Based Lighting, environment reflection, or any HDR
  environment map** — no diffuse/specular IBL, no environment probe, no
  reflection capture. Registered as a future candidate below.
- **Shadows of any kind** — no shadow map, no shadow-ray, no contact
  shadow. Registered as a future candidate below.
- **Normal maps or a tangent vertex attribute** — this codebase's own
  mesh vertex layout has no tangent field today (confirmed absent from
  `MeshSourceVertex`, the mesh artifact's 44-byte stride, and the
  runtime `Vertex` struct — see "Pre-draft verification" below); adding
  one is real, disproportionate scope for this Spec's own direct-only
  BRDF, which needs only the existing, already-asset-sourced normal.
  Registered as a future candidate below.
- **Ambient occlusion, emissive, clear-coat, transmission, or any other
  additional PBR material channel** beyond base color/metallic/roughness.
- **Tone mapping, exposure, or any HDR intermediate render target** —
  the existing `clamp(..., 0, 1)` final-color policy is reused unchanged
  (ADR-0067 D-9); every currently-bound render target in this engine is
  UNORM with no hardware or shader-side display-encode step
  (confirmed, "Pre-draft verification" below). Registered as a future
  candidate below.
- **Mipmapping, texture compression, or texture streaming** — the
  existing, single-mip-level `SampledTexture` contract (ADR-0055) is
  unmodified.
- **Bindless rendering or descriptor indexing** — this Spec's own single
  combined-image-sampler-per-Pipeline shape is deliberately preserved,
  not superseded (Goal 2).
- **A second base-color/metallic-roughness texture, of any kind** — see
  Goal 2. If real evidence during Implementation proves the
  single-texture-plus-scalars design is not viable, that is itself a new
  architectural decision requiring its own Spec/ADR/Human Review — never
  silently added (see "Risks & Open Questions").
- **Android, iOS, or Linux implementation** — Windows-verified only,
  per this codebase's current Phase 1 scope; this Spec's own D22
  states the portability reasoning for a future Android backend without
  implementing it. Android Platform's own Candidate Order 1 registration
  is entirely unaffected (see "Cross-cutting note").
- **Any Editor/Client-facing authoring UI or Material Graph/node
  system.**
- **A `Material Graph`, shader graph, or any user-composable shading
  system of any kind.**
- **Re-capturing or modifying any of the five existing goldens** — this
  Spec, once implemented, adds one new golden only; it does not touch
  `minimal_cube`, `world_scene`, `textured_quad`, `material_demo`, or
  `lighting_demo`.

## Requirements

### Functional

- A Material asset authored with `kind: pbr_direct_lit` (or the
  equivalent programmatic `MaterialKind::PbrDirectLit`) must cook, load,
  realize (GPU), and render via a distinct, real `pbr_direct_lit.slang`
  shader pair — never silently falling back to `LitTextured`'s or
  `UnlitTextured`'s own shader.
- `baseColorFactor`/`metallicFactor`/`roughnessFactor` must each survive
  a full cook→artifact→metadata→load round trip byte-for-byte (subject
  to IEEE-754 float exactness, matching every other float field this
  codebase's Asset System already round-trips) and must each be
  independently re-validated at load time, never trusted from a
  well-formed cooker output alone (ADR-0066 Decision item 5).
- A `PbrDirectLit` material's own referenced texture must be rejected at
  scene-load time if its own metadata does not declare
  `colorSpace == Srgb` (ADR-0066 Decision item 6) — a real, new, functioning
  validation, not merely documented author guidance.
- `UnlitTextured`/`LitTextured` materials must render identically before
  and after this Spec's own Implementation lands — verified by the five
  existing goldens staying byte-for-byte/pixel-for-pixel unchanged.
- A scene mixing `UnlitTextured`, `LitTextured`, and `PbrDirectLit`
  materials in the same frame must render all three correctly in one
  draw pass, through the existing, unmodified per-`DrawItem` bind/push
  loop (`Renderer::drawFrame()`).

### Non-functional

- **Performance:** the extended BRDF's added per-fragment ALU cost
  (a handful of `dot`/`pow`/division operations per active light, capped
  at 1 Directional + 4 Point per Spec 0022's own unchanged
  `FrameLightingData`) is not measured or budgeted by this Spec — no
  currently-shipped scene stresses fragment ALU cost, and no performance
  requirement is stated beyond "renders correctly."
- **Memory:** the material artifact widens by exactly 24 bytes
  (ADR-0066 Decision item 3); the push-constant range widens by exactly 24 bytes for
  `PbrDirectLit` Pipelines only (ADR-0067 D-6) — both fixed, small,
  disclosed, one-time costs.
- **Portability (within the Vulkan-only Phase 1 constraint):** every new
  Vulkan API surface this Spec's own design touches (a wider
  `VkPushConstantRange::stageFlags`, a wider `size`) is core Vulkan 1.0,
  already in use elsewhere in this codebase, sized against Vulkan's own
  guaranteed minimum (ADR-0067 D-6/D-11) — no new extension, no new
  device feature, no platform-specific branch.
- **Other:** zero new third-party dependency; zero change to
  `Atlantis::AssetSystem`'s Core-only link closure; zero change to
  `Atlantis::RHI`'s/`Atlantis::Renderer`'s public API surface beyond one
  new, additive `PipelineCreateParams` field (ADR-0067 D-7).

## Pre-draft verification against real, current source

Confirmed directly against `main` at `f7c2d18` (the commit this Spec's
own branch was created from), by reading full files — not by inferring
PBR capability from general graphics knowledge and not by trusting any
prior Spec's own conclusions as a substitute for a fresh read:

- **`MaterialKind`** (`src/asset_system/include/atlantis/asset_system/material_types.h:30-33`):
  a closed, two-value enum, `UnlitTextured`/`LitTextured`. Dispatched by
  one, single, Runtime-private `switch` with no `default:` label
  (`selectShaderPair()`, `src/runtime/src/material_realization.cpp:100-115`)
  — this repository's own `/w14062`/`/WX` build configuration makes a
  missing case a hard compile error, confirmed by that function's own
  comment (lines 92-99).
- **`MaterialAssetData`** (`material_types.h:40-45`): exactly four
  fields — `kind`, `textureAsset` (`AssetId`), `filter`, `addressMode` —
  unchanged since ADR-0059 and unwidened by ADR-0061's later
  `LitTextured` addition. **No color/tint/metallic/roughness field
  exists anywhere in the Material asset chain** — confirmed absent from
  the authoring grammar
  (`src/asset_system/include/atlantis/asset_system/material_source.h:16-21`),
  the 32-byte binary artifact
  (`src/asset_system/include/atlantis/asset_system/material_artifact.h:14-41`,
  decode-rejected on any other size), and the metadata sidecar
  (`src/asset_system/include/atlantis/asset_system/material_metadata.h:19-24`).
- **`atlantis::renderer::Material`**
  (`src/renderer/include/atlantis/renderer/material.h:34-54`): owns
  exactly one `Pipeline`, one borrowed, nullable `const SampledTexture*`,
  one borrowed, nullable `const Sampler*` — both-or-neither, checked
  once in the constructor, no rebind path. Structurally supports exactly
  one optional texture+sampler pair, never a list.
- **Texture dependency declaration**: singular at every layer — one
  `texture: <logical-path>` authoring line
  (`src/asset_system/src/material_source.cpp:12`), one `AssetId
  textureAsset` artifact field, one `TEXTURE_DEPENDENCIES`-shaped
  single-value CMake argument (`atlantis_add_material_asset()`,
  `src/asset_system/CMakeLists.txt:306-352`, `set(oneValueArgs NAME
  SOURCE TEXTURE)`). No multi-texture manifest concept exists anywhere.
- **`DrawItem`** (`src/renderer/include/atlantis/renderer/draw_item.h:17-21`):
  `{ const Mesh* mesh; const Material* material; std::array<float, 16>
  objectToWorld; }` — three fields, unchanged shape needed for this
  Spec (Requirement/D10).
- **Renderer per-draw bind/push order**
  (`src/renderer/src/renderer.cpp:27-41`): `bindPipeline` →
  `bindVertexBuffer` → `bindIndexBuffer` → `bindUniformBuffer` →
  (conditional) `bindTexture` → `pushConstant` → `drawIndexed` — one
  fixed order, unconditionally reused by every `DrawItem` regardless of
  `MaterialKind`.
- **`CommandList::pushConstant()`**
  (`src/rhi/include/atlantis/rhi/command_list.h:73`): `virtual void
  pushConstant(const void* data, std::size_t sizeBytes) = 0;` — no
  offset, no stage-flags parameter. The Vulkan implementation
  (`src/vulkan_backend/src/vulkan_command_list.cpp:320-324`) hardcodes
  `VK_SHADER_STAGE_VERTEX_BIT` and offset `0` for every call,
  unconditionally. The sole real call site
  (`src/renderer/src/renderer.cpp:39`) pushes exactly 64 bytes
  (`item.objectToWorld`) today.
- **`VulkanDevice::createPipeline()`'s push-constant range**
  (`src/vulkan_backend/src/vulkan_device.cpp:1035-1045`): exactly one
  `VkPushConstantRange`, `stageFlags = VK_SHADER_STAGE_VERTEX_BIT`,
  `offset = 0`, `size = params.pushConstantSizeBytes` — a per-call
  value, hardcoded to `sizeof(float) * 16` (64 bytes) at all three real
  call sites (`src/runtime/src/material_realization.cpp:176, 301, 324`),
  never derived from Shader System reflection at runtime.
- **Vulkan's own guaranteed minimum push-constant capacity, 128 bytes
  across all stages**, is already recorded in this codebase
  (`adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md:353-360`)
  — the only place this number appears in this repository.
- **Shader System's push-constant validation is vertex-stage-only**,
  by explicit, stated design
  (`src/tools/shader_compiler/compile_and_validate.cpp:157-178`) — no
  existing cross-check exists between vertex- and fragment-stage
  push-constant reflection.
- **`lit_textured.slang`** (full file read,
  `shaders/lit_textured/lit_textured.slang`): `CameraUniform` at
  `[[vk::binding(0,0)]]` includes the full 304-byte Camera+Lighting
  layout inline (view/projection/light-count/light-array fields);
  `Sampler2D texturedSampler` at `[[vk::binding(1,0)]]`; `PushConstants
  { float4x4 objectToWorld; }` at `[[vk::push_constant]]`; vertex input
  `position@0 (float3), uv@1 (float2), normal@2 (float3)`. Lighting math
  (lines 81-97) is pure N·L diffuse accumulation, **no ambient, no
  specular**, linear Point attenuation `clamp(1 - dist/range, 0, 1)`,
  final `clamp(texColor.rgb * accumulated, 0, 1)` with no tone-mapping
  or gamma-encode.
- **`textured_quad.slang`** (`UnlitTextured`'s own real shader, full
  file read): `CameraUniform` is only the 128-byte view+projection
  block, no light fields; same two bindings; same `PushConstants`
  shape; fragment shader does a raw, unmodified texture sample with
  zero lighting math.
- **`FrameLightingData`**
  (`src/runtime/include/atlantis/runtime/scene_extraction.h:77-100`,
  `static_assert(sizeof(FrameLightingData) == 176)` at line 120): 1
  Directional + 4 Point lights, field offsets confirmed by
  `static_assert`. Combined with the 128-byte `CameraMatrices`
  (`scene_extraction.h:61-64`) into one 304-byte buffer, created at
  `src/runtime/src/runtime_application.cpp:288-289`, bound via
  `cmd.bindUniformBuffer()` (descriptor set binding 0), never a push
  constant.
- **RenderTarget color format**: the swapchain's own preference order
  (`src/vulkan_backend/src/vulkan_presentation.cpp:107-112`) picks
  `VK_FORMAT_B8G8R8A8_UNORM` first when available — a UNORM, not sRGB,
  format. Every offscreen/golden fixture explicitly uses `Rgba8Unorm`
  (also UNORM) — confirmed across every fixture header and every golden
  sidecar. **No `Srgb`-format render target is ever actually selected or
  bound by any currently-shipped code path.** No gamma-encode/
  tone-mapping code exists anywhere under `src/` (repository-wide grep,
  zero hits) — confirmed independently by ADR-0062 Decision 5's own
  existing text.
- **Mesh vertex layout**: `MeshSourceVertex`
  (`src/asset_system/include/atlantis/asset_system/mesh_source.h:23-35`)
  and the runtime `Vertex` struct
  (`src/runtime/src/runtime_application.cpp:62-73`) both carry
  position/color/UV0/normal — 11 floats, 44-byte stride
  (`kMeshArtifactVertexStrideBytes`,
  `src/asset_system/include/atlantis/asset_system/mesh_artifact.h:34`).
  **No tangent field exists anywhere in this chain.**
- **Geometry assets**: exactly three mesh files exist
  (`assets/meshes/minimal_cube.mesh.txt`,
  `textured_quad_left.mesh.txt`, `textured_quad_right.mesh.txt`), all
  hand-authored text sources. **No sphere mesh, and no procedural
  mesh-generation code of any kind, exists anywhere in this
  repository** (repository-wide search, confirmed). A flat cube/quad
  face's `N·V` varies only weakly across its visible area — insufficient
  to visually demonstrate this BRDF's own roughness/metallic-driven
  highlight shape (Requirement/D16).
- **Spec 0021's own descriptor-pool "at most one combined-image-sampler
  per Pipeline" proof**
  (`specs/0021-descriptor-pool-capacity-foundation.md:677-697`, D4):
  "every Pipeline's own descriptor set ... consumes exactly one
  `UNIFORM_BUFFER` descriptor ... A Material-bound Pipeline
  (`hasSampledTextureBinding = true`) additionally consumes one
  `COMBINED_IMAGE_SAMPLER` descriptor, at a rate strictly no greater
  than the uniform/`maxSets` rate ... A future Pipeline kind with more
  than one sampler binding ... would invalidate this specific proof."
  Independently re-confirmed against real, current
  `VulkanDevice::createPipeline()`'s own descriptor-set-layout
  construction (`src/vulkan_backend/src/vulkan_device.cpp:984-1011`):
  binding 0 is always exactly one `UNIFORM_BUFFER`; binding 1 is at most
  one `COMBINED_IMAGE_SAMPLER` (`descriptorCount = 1`), present only
  when `hasSampledTextureBinding` is true — this Spec's own Goal 2
  (single texture, no second binding) is what keeps this proof intact
  (D21 below).
- **Scene/`World::Renderable`**
  (`src/world/include/atlantis/world/renderable.h:15-18`):
  `{ AssetId meshAsset; std::optional<AssetId> materialAsset; }` —
  unaffected by this Spec (D20).
- **Runtime `MaterialKind` dispatch, initial realization, format-change
  rebuild**: `selectShaderPair()` (above); driven per-frame by
  `realizePendingMaterials()`/`realizeOneMaterialCandidate()`
  (`src/runtime/src/material_realization.cpp:119-275`), called from
  `runtime_application.cpp:653-657`; format-change rebuild
  (`rebuildMaterialsForFormatChange()`,
  `material_realization.cpp:277-332`) triggered by comparing
  `presentation_->metadata().format` against `lastSeenFormat_`
  (`runtime_application.cpp:435-456`), an atomic, all-or-nothing
  candidate-batch swap (ADR-0060 Decision 9) — unaffected in mechanism
  by this Spec; `PbrDirectLit` simply becomes a third arm of the
  already-existing dispatch/realize/rebuild machinery (D11).
- **`World::Light`**
  (`src/world/include/atlantis/world/light.h:15-20`):
  `{ LightKind kind; Vec3 color; float intensity; float range; }` — no
  stored position/direction (derived from the owning entity's world
  matrix at extraction time). Unaffected by this Spec.
- **ADR-0062's own Point-light attenuation formula**
  (`adr/0062-...md:130-135`): `atten = clamp(1 - dist/range, 0, 1)`,
  explicitly linear/non-physical, `kPointLightDistanceEpsilon = 1e-4`
  floor on `dist`, inverse-square explicitly rejected for this codebase
  (`adr/0062-...md:199-201`) — reused unchanged by this Spec's own
  BRDF (D12 below; ADR-0067 D-5).

No unresolvable architectural conflict was found. The real,
evidence-grounded recommendation below is achievable by extending three
already-existing chains (Asset System's Material format, the RHI
push-constant path, and Runtime's `MaterialKind` dispatch) in kind, not
by inventing a new module or a new resource-binding mechanism.

## Proposed Design

### End to end, one `PbrDirectLit` draw

```
Author:  assets/materials/pbr_<name>.material.txt
         kind: pbr_direct_lit
         texture: textures/<base-color>.png        (cooked Rgba8Srgb --
                                                      ADR-0066 item 6)
         base_color_factor: r g b a                 (optional, default
                                                      1 1 1 1)
         metallic_factor: m                         (optional, default 1)
         roughness_factor: r                         (optional, default 1)
              |
              v
cookMaterial()  -- widened schema, ADR-0066 items 1-5, same cook/artifact/
                   metadata/atomic-write shape every other Asset System
                   type already uses
              |
              v
loadMaterialAsset()  -- MaterialAssetData{kind, textureAsset, filter,
                         addressMode, baseColorFactor, metallicFactor,
                         roughnessFactor}, re-validated (ADR-0066 item 5)
              |
              v
Runtime scene-load (Phase 1, CPU-only, ADR-0060 unchanged):
  resolve texture AssetId against manifest, read its metadata,
  REJECT if colorSpace != Srgb for a PbrDirectLit material (ADR-0066 item 6)
              |
              v
Runtime per-frame realization (Phase 2, ADR-0060 unchanged mechanism):
  selectShaderPair(PbrDirectLit) -> pbr_direct_lit.slang pair
  createMaterial(..., pushConstantSizeBytes = 88,
                 pushConstantFragmentVisible = true,   -- ADR-0067 D-7
                 hasSampledTextureBinding = true)      -- unchanged flag
              |
              v
Renderer::drawFrame()'s existing, unmodified per-DrawItem loop:
  bindPipeline -> bindVertexBuffer -> bindIndexBuffer ->
  bindUniformBuffer (existing 304B Camera+Lighting, unchanged) ->
  bindTexture (existing single combined-image-sampler, unchanged) ->
  pushConstant(objectToWorld[64B] + baseColorFactor[16B] +
               metallicFactor[4B] + roughnessFactor[4B] = 88B,
               now fragment-visible for THIS Pipeline only) ->
  drawIndexed
              |
              v
pbr_direct_lit.slang fragment stage: metallic-roughness Cook-Torrance
  BRDF (ADR-0067 D-1 through D-5), consuming the same per-light
  color/intensity/range data lit_textured.slang already consumes,
  unchanged
```

Every arrow above is either an existing, unmodified mechanism (Renderer
bind/push order, the descriptor set layout, `FrameLightingData`
extraction, Runtime's two-phase realization) or a narrowly-scoped
widening of one existing mechanism at exactly one new call site
(`selectShaderPair()`'s own switch, `createPipeline()`'s per-call
`pushConstantSizeBytes`/new stage-flags field) — no new subsystem, no
new resource type, no new binding.

## Architectural Impact

**Yes** — two distinct architectural decisions, deliberately recorded as
two single-responsibility ADRs, not one:

- **[ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Proposed`):** widens `MaterialAssetData`'s own field set and the
  Material artifact's own binary schema (a real Asset System data-format
  decision), and adds a new Runtime-side texture-color-space
  cross-validation (closing a gap ADR-0057 already disclosed and left
  open).
- **[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (`Proposed`):** extends the RHI's own push-constant byte layout and
  introduces a new, RHI-internal, per-Pipeline push-constant
  stage-visibility concept (a real RHI/Vulkan-Backend resource-contract
  decision) — `CommandList`'s own public API surface gains exactly one
  additive field on `PipelineCreateParams`; no existing public method
  signature changes.

These two decisions are deliberately not bundled into one ADR — the
first is a pure Asset System data-format/validation decision with no
Vulkan/GPU content; the second is a pure RHI/rendering-math decision with
no Asset System content — matching this codebase's own established
"one ADR, one architectural responsibility" convention (e.g. ADR-0061 vs.
ADR-0062 for Spec 0019's own Light-component-vs-frame-uniform split).

No RenderGraph, World, Scene, or Platform module boundary is touched by
either decision. `Atlantis::AssetSystem`'s Core-only dependency closure
is unchanged (ADR-0066 introduces no RHI type into that module).

## Alternatives Considered

See ADR-0066's and ADR-0067's own "Alternatives Considered" sections for
the detailed, per-decision alternatives (a per-`MaterialKind` artifact
sub-format; a second Asset System type; automatic texture color-space
inference; a new per-material uniform buffer instead of extended push
constants; a uniformly-widened push-constant stage visibility;
height-correlated Smith geometry; inverse-square Point attenuation;
scoped tone-mapping). At the Spec level:

- **A second base-color/metallic-roughness texture from the outset**
  (the professional-engine-standard "packed ORM" or separate
  metallic-roughness texture). Rejected for this foundation round per
  this Spec's own Goal 2 — would immediately reopen Spec 0021's own
  descriptor-pool-capacity proof (D4 there), requiring a real,
  evidence-driven re-derivation of pool sizing this Spec was directed
  to avoid unless real evidence during Implementation proves scalar
  parameters insufficient (see Risks & Open Questions). Registered as a
  future candidate below.
- **Extending `LitTextured` in place**, adding metallic/roughness
  fields to its own existing shader instead of introducing a new
  `MaterialKind`. Rejected: `LitTextured`'s own existing goldens and
  shader must remain byte-for-byte/pixel-for-pixel unchanged (Goal 4) —
  a `MaterialKind` is this codebase's own established mechanism for a
  genuinely different shading behavior (ADR-0059 Decision 3), and reuses
  the existing, proven "add one closed-enum value, one shader pair, one
  dispatch arm" pattern ADR-0061 already used for `LitTextured` itself.

## Decisions for Human Review

Twenty-five items, matching the human-directed drafting brief for this
Spec exactly, one to one. Every recommendation below is a proposal for
Human Review to accept, amend, or reject — none of these are treated as
already decided.

### D1. New `MaterialKind` name and closed semantics

**Recommendation:** `MaterialKind::PbrDirectLit` — a third, closed enum
value (`src/asset_system/include/atlantis/asset_system/material_types.h:30-33`
widens from two to three enumerators), matching `LitTextured`'s own
naming convention (`<Model><Texturing>`) and stating its own scope
precisely in the name: "PBR" (metallic-roughness, physically-based) +
"Direct" (no IBL) + "Lit" (implied by "Direct"). Reuses
`MaterialAssetData`'s own existing shape, widened (ADR-0066), never a
new DTO.

### D2. PBR parameter set

**Recommendation:** exactly three new scalar-shaped fields —
`baseColorFactor` (RGBA, 4 floats), `metallicFactor` (1 float),
`roughnessFactor` (1 float) — see
[ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
Decision item 1 for the exact struct and default values. No emissive,
no occlusion/AO factor, no normal-scale, no clear-coat — each would be a
real, additional parameter this Spec's own Non-Goals deliberately
exclude for this foundation round.

### D3. Parameter range/finiteness and cook/decode dual validation

**Recommendation:** all three parameters finite and in `[0, 1]`,
validated independently at both cook time and load time (never trusting
a well-formed cooker output) — see
[ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
Decision item 5 for the exact two new error enumerators this introduces,
matching Spec 0020's own established dual cook/decode validation
discipline for authored mesh normals.

### D4. Numerical stability strategy near `roughness = 0`

**Recommendation:** shader-internal minimum alpha clamp
(`alpha = max(roughness², kMinAlpha)`, `kMinAlpha = 1e-3`), never a
cook-time rejection of an authored `roughness = 0` — see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-4 for the full reasoning (a perfectly smooth surface is a legitimate
authored intent; only the BRDF's own internal computation is clamped,
never the authored/cooked/recorded value itself).

### D5. Single base-color texture + scalar metallic/roughness vs. multi-texture

**Recommendation:** single base-color texture (reusing the existing
`textureAsset` field unchanged) + scalar `metallicFactor`/
`roughnessFactor`, per Goal 2 — continuing this codebase's existing
one-texture-per-Material architecture and keeping Spec 0021's
descriptor-pool-capacity proof intact without re-derivation (D21
below). See "Risks & Open Questions" for the explicit, disclosed
escape hatch if real Implementation evidence proves this insufficient.

### D6. Base-color texture sRGB/linear requirement and metadata-mismatch error

**Recommendation:** the base-color texture must be cooked as
`SampledTextureFormat::Rgba8Srgb`; a `PbrDirectLit` material referencing
a texture whose metadata declares `colorSpace != Srgb` fails scene load
with a new, distinct `RuntimeInitError` sub-code — see
[ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
Decision item 6 for the exact mechanism (Runtime-side, at the existing
Phase 1 scene-dependency-resolution point, reusing an already-read
value — no new Asset System mechanism). `baseColorFactor` is authored
and consumed in linear space, matching the sampled, hardware-linearized
texture value it multiplies against (ADR-0066 Decision item 7).

### D7. Per-material parameter transmission method

**Recommendation:** extended push constants — `objectToWorld` (64
bytes, unchanged) + `baseColorFactor`/`metallicFactor`/`roughnessFactor`
(24 bytes) = 88 bytes, fitting comfortably within Vulkan's guaranteed
128-byte minimum (ADR-0025) with 40 bytes to spare. See
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-6/D-7 and "Alternatives Considered" there for why a new per-material
uniform buffer was evaluated and not recommended for this round (it
would reopen Spec 0021's own descriptor-pool-sizing proof for no benefit
given the push-constant headroom already available).

### D8. If push constants: exact layout, byte count, stage visibility, compatibility

**Recommendation:** see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-6 (exact 88-byte layout, `objectToWorld` at its existing offset 0/size
64, the three new fields at offset 64) and D-7 (a new, RHI-internal,
per-Pipeline push-constant stage-flags field on `PipelineCreateParams`,
defaulting to today's vertex-only behavior for every existing call site
— `CommandList::pushConstant()`'s own public signature unchanged). D-7
explicitly discloses and does not silently reject a simpler alternative
(uniformly widen every Pipeline, matching ADR-0062's own precedent) —
Human Review may pick either. `objectToWorld`'s own existing push range
is unchanged in offset/size for `UnlitTextured`/`LitTextured`; only
`PbrDirectLit`'s own Pipeline gets the wider range. Full compatibility
argument: ADR-0067 D-8.

### D9. `Material` ownership of PBR parameters; Renderer read path; public API

**Recommendation:** `atlantis::renderer::Material` itself gains **no**
new field — the three PBR parameters live only in `MaterialAssetData`
(Asset System, CPU-side) and are baked directly into the per-`DrawItem`
push-constant payload at the point `Renderer::drawFrame()` already
constructs that payload from `item.objectToWorld`, widened to also read
the bound `Material`'s own originating `MaterialAssetData` (or a small,
Runtime-owned side table keyed the same way `materialResourceMap_`
already is) for `PbrDirectLit` items specifically. This keeps
`atlantis::renderer::Material`'s own public shape (`pipeline()`,
`sampledTexture()`, `sampler()`) completely unchanged — a real,
disclosed, Plan-time question is whether the *cleanest* implementation
instead adds three optional fields to `Material` itself (mirroring how
`sampledTexture_`/`sampler_` already are optional, borrowed fields) —
**explicitly left open for Human Review to pick**, since both are
functionally equivalent and neither changes `DrawItem`'s own shape
(D10) or `Renderer::drawFrame()`'s own bind/push *order*.

### D10. `DrawItem` — stays unchanged

**Recommendation:** yes, unchanged — `DrawItem`'s own three fields
(`mesh`, `material`, `objectToWorld`) are sufficient; the three new PBR
parameters are reached via `item.material`'s own already-referenced
`MaterialAssetData`/side-table (D9), never a new `DrawItem` field.

### D11. Runtime `MaterialKind` dispatch and format-change rebuild

**Recommendation:** `PbrDirectLit` becomes a third arm of
`selectShaderPair()`'s own existing closed switch (D23 below) and a
third path through `realizeOneMaterialCandidate()`/
`rebuildMaterialsForFormatChange()`'s own existing, unmodified
mechanism (both already `MaterialKind`-parametric via the shader-pair
selection they already delegate) — no new realization phase, no new map,
no new format-change-detection mechanism. See "Pre-draft verification"
above for the exact real call sites this reuses unchanged.

### D12. Exact BRDF

**Recommendation:** see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-1 through D-5 for the complete, named-constant, per-division-guarded
formula (Lambertian diffuse with metallic energy split; GGX/Trowbridge-
Reitz normal distribution; Schlick-GGX geometry with Karis's
direct-lighting `k = alpha/2` remap; Schlick Fresnel with dielectric
`F0 = 0.04`/metallic `F0 = baseColor` blend; ADR-0062's existing,
unchanged linear Point attenuation; multi-light accumulation via the
identical `for`-loop-and-`+=` shape `lit_textured.slang` already uses).

### D13. Coordinate/normal/view/light vector conventions

**Recommendation:** identical conventions to `lit_textured.slang`'s own
existing, already-verified code — world-space `N` from
`mul((float3x3)objectToWorld, input.normal)` (unchanged), world-space
`L` derived per-light exactly as `lit_textured.slang` already derives it
(`-direction` for Directional, `normalize(position - worldPosition)` for
Point). The one new vector, `V` (view direction), is
`normalize(cameraWorldPosition - worldPosition)` — **`cameraWorldPosition`
is not currently exposed to any shader** (`CameraUniform` carries only
`view`/`projection` matrices, never an explicit eye-position vector) —
this Spec's own Plan must add it, either as a new, small
`CameraUniform` field (widening the existing 304-byte buffer by 12-16
bytes, a real, disclosed layout change every consumer of that buffer
must account for) or derived in-shader from the inverse of `view`'s own
translation component (`-transpose(mat3(view)) * view[3].xyz`, avoiding
any buffer-layout change at the cost of one small per-fragment matrix
extraction). **Explicitly left open for Human Review** — this is a real,
previously-unnoticed gap this Spec's own investigation surfaced, not a
silently-resolved detail; see "Risks & Open Questions."

### D14. Output color space

**Recommendation:** no change to the existing "final `clamp(..., 0, 1)`
is the only transformation" contract — see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-9 for the full, disclosed trade-off (bright specular highlights on
smooth surfaces hard-clip rather than tone-map; this engine's current
RenderTarget format is UNORM, confirmed via
`vulkan_presentation.cpp:107-112` and every golden fixture, with no
hardware or shader-side display-encode step anywhere today).

### D15. Direct-light cap and 176-byte `FrameLightingData`

**Recommendation:** unchanged — see ADR-0067 D-10. This BRDF consumes
the identical, already-extracted per-light data `lit_textured.slang`
already consumes; only the per-light *math* changes.

### D16. Validation mesh choice

**Recommendation:** a new, hand-authored, non-shared-vertex sphere
`.mesh.txt` (e.g. a UV-sphere or icosphere), generated by an offline,
throwaway script (never committed as engine/tooling code — this
codebase's own convention is 100% hand-authored `.mesh.txt` text
sources today, confirmed by "Pre-draft verification" above; no
procedural mesh-generation capability is added to the engine or its
Tools) and checked in as an ordinary text mesh source, following the
exact same authoring format every other mesh asset already uses. **Not**
the existing cube or quads — a flat face's `N·V` varies too weakly to
demonstrate this BRDF's own roughness/metallic-driven highlight shape
and falloff, confirmed by direct inspection of the only three existing
mesh assets (Pre-draft verification above).

### D17. New PBR validation scene and golden

**Recommendation:** a new scene (e.g. `pbr_material_demo.scene.txt`)
placing several instances of the new sphere mesh (D16), each with a
distinct `PbrDirectLit` material spanning the four corners of the
metallic/roughness space this BRDF's own math is sensitive to —
dielectric-rough, dielectric-smooth, metallic-rough, metallic-smooth —
lit by both one Directional and one Point light (matching
`lighting_demo`'s own existing "both light kinds independently visible"
asymmetric design precedent). Golden captured per ADR-0042's existing
"Initial baseline bootstrap" amendment (Implementation committed first,
capture against a clean commit, human-reviewed, PNG/sidecar landed in a
separate, later commit) — no relaxation of that process.

### D18. Material asset/schema/artifact version bump

**Recommendation:** yes — see
[ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
Decision items 2-3 (authoring grammar version 1→2; binary artifact
schema bump, fixed new size, no dual-version reader).

### D19. Rejection/re-cook strategy for old Material assets

**Recommendation:** every currently-checked-in Material asset
(`unlit_textured_quad`, `lit_textured_quad`, and the CMake-test-only
`cmake_material_declaration_test` fixture) is re-cooked under the new
schema (a mechanical version-line bump, no new authored content needed
per ADR-0066's own default values) — an old-schema artifact is rejected
outright at decode time (`SizeMismatch`), matching every existing Asset
System format's own "no dual-version reader" convention. See ADR-0066
Consequences for the full, disclosed cost of this one-time migration.

### D20. Scene artifact — no change

**Recommendation:** correct, no change needed. `world::Renderable`'s
own `std::optional<AssetId> materialAsset` (ADR-0060) already
references a Material by opaque `AssetId`, regardless of that
Material's own `kind` — a `PbrDirectLit` material is referenced exactly
like any other Material `AssetId` today. The Scene binary format's own
schema is untouched by this Spec.

### D21. Descriptor-pool-per-set-sampler-count proof — still holds

**Recommendation:** yes, unchanged, by direct construction — this
Spec's own Goal 2 (single texture, no second binding) is exactly what
keeps Spec 0021's own D4 proof
(`specs/0021-descriptor-pool-capacity-foundation.md:677-697`) intact.
`PbrDirectLit`'s own Pipeline sets `hasSampledTextureBinding = true`,
consuming exactly the same one `UNIFORM_BUFFER` + at most one
`COMBINED_IMAGE_SAMPLER` descriptor pair every existing textured
Pipeline already consumes — re-confirmed directly against real,
current `vulkan_device.cpp:984-1011` (Pre-draft verification above),
not merely asserted from the Spec 0021 text alone.

### D22. Windows/future-Android portability

**Recommendation:** see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-11 — every new Vulkan API surface this Spec touches is core Vulkan
1.0, sized against Vulkan's own portable guaranteed minimum, no new
extension or device feature. Android Platform (Candidate Order 1) is
entirely unaffected; a future Android Vulkan Backend would inherit this
exact design unchanged.

### D23. Error domain and C4062

**Recommendation:** see ADR-0067 D-12 — `selectShaderPair()`'s existing
closed, `default:`-free switch gains one new arm, already enforced as a
compile-time error by this repository's own `/w14062`/`/WX` build
configuration for any future missed case. New error enumerators
(ADR-0066 Decision items 5/6) are additive only; no existing enumerator's meaning
changes.

### D24. Thread/ownership/lifetime

**Recommendation:** unchanged — see ADR-0067 D-13. `PbrDirectLit`
follows ADR-0060's existing two-phase realization model and existing
`AssetId`-keyed resource maps exactly; no new resource type, map, or
synchronization primitive; Phase 1's existing single-threaded
frame-orchestration baseline (AGENTS.md) is unaffected.

### D25. Explicit Non-Goals and future-candidate splitting

**Recommendation:** restated from the Non-Goals section above, each
explicitly registered as its own future candidate rather than silently
dropped: **PBR Texture Set / Multi-Texture Binding** (a second,
metallic-roughness-packed texture — the direct future extension of D5's
own single-texture choice, if real evidence later proves it
insufficient); **Tangent Attribute + Normal Mapping**; **IBL / HDR
Environment**; **Shadow Foundation**; **Tone Mapping / Post-processing**.
None of these five is designed, scoped, or committed to by this Spec.

## Testing & Verification Plan

At minimum, matching this codebase's own established layered-testing
discipline (`docs/process/testing-strategy.md`):

- **Material source/artifact/cook/decode**: fixed-byte round-trip tests
  for the widened 56-byte (or Plan-finalized exact size) artifact;
  version-bump rejection test (an old, 32-byte artifact is rejected,
  not silently accepted); per-parameter range-error tests
  (`baseColorFactor`/`metallicFactor`/`roughnessFactor` each
  independently NaN/Inf/out-of-`[0,1]`, at both cook time and decode
  time — never sharing one shared, self-certifying validation function
  between cook and decode without an independent test proving each path
  is separately reachable); the new base-color-texture-`colorSpace`
  rejection test (a `PbrDirectLit` material referencing an `Unorm`
  texture fails scene load with the new, distinct error).
- **Independent CPU BRDF reference tests, never sharing production's own
  possible errors (no self-certification):** a hand-computed or
  independently-implemented (not calling the production Slang/HLSL
  translation unit) CPU reference for the exact formula in ADR-0067 D-1,
  covering at minimum: a pure dielectric (`metallic = 0`) at low and
  high roughness; a pure metal (`metallic = 1`) at low and high
  roughness; a Directional light at several `N·L` angles including
  grazing (`N·L → 0`) and directly overhead; a Point light at several
  distances including at and beyond `range` (confirming the reused,
  unchanged ADR-0062 attenuation); a multi-light case (1 Directional + 2
  Point) confirming linear accumulation; and at least one case exercising
  the `roughness = 0`/`kMinAlpha` clamp (D4) and one exercising the
  `NdotL ≤ 0` early-out (ADR-0067 D-3), confirming no `NaN`/`Inf`
  anywhere in the output for either.
- **Shader reflection vs. C++ parameter layout**: real Slang reflection
  JSON for `pbr_direct_lit.slang`'s own push-constant block, cross-
  checked against the C++ `PbrPushConstants` struct's own
  offsets/total size via `static_assert` — never a shared literal
  trusted from only one side (matching Plan 0022's own "independent
  byte-layout cross-check" precedent).
- **Real GPU push/uniform transmission**: a real-GPU test confirming a
  known `baseColorFactor`/`metallicFactor`/`roughnessFactor` combination
  actually reaches the fragment shader and visibly changes the rendered
  output (e.g. two otherwise-identical draws differing only in
  `metallicFactor`, confirmed to produce different captured pixels).
- **Runtime initial realization and format-change rebuild**: a
  `PbrDirectLit` material realized for the first time; a format-change
  rebuild including at least one `PbrDirectLit` material alongside an
  `UnlitTextured`/`LitTextured` material, confirming the existing
  all-or-nothing candidate-batch contract (ADR-0060 Decision 9) still
  holds unmodified.
- **Mixed-material scenes**: at least one scene drawing `UnlitTextured`,
  `LitTextured`, and `PbrDirectLit` materials together in one frame.
- **Spec 0021 descriptor-pool regression**: re-run the existing N=2/N=6
  GPU regression tests unmodified, confirming zero regression; add one
  new case mixing a `PbrDirectLit` material into an existing N-material
  format-change scenario, confirming the pool-sizing proof (D21) holds
  for a mixed-kind workload, not only a single-kind one.
- **Spec 0022 dynamic-Lighting multi-frame regression**: re-run
  unmodified, confirming a `PbrDirectLit` material correctly reflects a
  runtime `Light`/Transform change on the next successful frame exactly
  as `LitTextured` already does (both consume the identical,
  re-extracted-every-frame `FrameLightingData`).
- **New PBR golden**: per D17 and ADR-0042's "Initial baseline
  bootstrap" — Implementation committed first, capture against a clean
  commit, human-reviewed (non-black, non-garbage, dielectric/metallic
  and rough/smooth combinations each independently visible, both light
  kinds independently visible), PNG/sidecar landed in a separate, later
  commit.
- **Existing five goldens**: confirmed byte-for-byte/pixel-for-pixel
  unchanged — no golden is regenerated for `minimal_cube`, `world_scene`,
  `textured_quad`, `material_demo`, or `lighting_demo`.
- **Full suite**: fresh Debug and Release builds; `ctest -LE gpu` and
  `ctest -L gpu` both configurations on real Vulkan-capable hardware,
  Vulkan Validation Layers grepped clean (zero `VUID`/`Validation
  Error`/`Validation Warning`); a fresh `ATLANTIS_BUILD_TESTS=OFF`
  configure+build; confirmation that `Atlantis::AssetSystem`'s Core-only
  link closure and `Atlantis::Renderer`'s/`Atlantis::RHI`'s public link
  graph are unchanged beyond the one additive `PipelineCreateParams`
  field; a `git diff --check` pass on the final Implementation diff.

## Risks & Open Questions

- **D5's own single-texture-plus-scalars scope is a real, disclosed bet,
  not a proven-sufficient design** — if real Implementation evidence
  (the new PBR validation scene/golden, D17) shows the visual result is
  unsatisfying without a genuine metallic-roughness *texture* (spatially
  varying, not a single scalar per Material), that is itself a new
  architectural decision (a second descriptor binding, reopening Spec
  0021's own pool-capacity proof) requiring its own Spec/ADR/Human
  Review — **never silently added** during this Spec's own eventual
  Plan or Implementation, per this Spec's own drafting brief.
- **D13's own camera-world-position gap is a real, previously-unnoticed
  finding this Spec's own investigation surfaced**, not a
  pre-existing, disclosed limitation of any prior Spec — no shader in
  this codebase today has any notion of the camera's own world-space
  position (`CameraUniform` carries only `view`/`projection`). Human
  Review must pick between widening the shared uniform buffer (a real,
  disclosed layout change affecting `UnlitTextured`/`LitTextured`'s own
  descriptor binding, though not their own shader's *reads*) or an
  in-shader matrix-inverse extraction (no buffer change, small added
  ALU cost) before this Spec's own eventual Plan can proceed.
- **ADR-0067 D-7's own "Open for Human Review" choice** (a new,
  per-Pipeline RHI-internal field vs. uniformly widening every
  Pipeline's push-constant stage visibility, matching ADR-0062's
  precedent) is a real, disclosed fork this Spec does not resolve
  unilaterally.
- **D9's own "does `Material` gain new fields, or does Renderer read a
  side table" question** is left open for Human Review — both are
  functionally equivalent at this Spec's own level of design; the
  eventual Plan needs a single, chosen answer before Implementation.
- **The geometry-term remap (ADR-0067 D-2) and whether inverse-square
  Point attenuation should ever be revisited** are both disclosed,
  not-fully-closed judgment calls a future Spec/ADR could reopen with
  new evidence — not treated as permanently settled by this Spec alone
  beyond its own stated scope.

## Out of Scope / Future Work

Restated from Non-Goals/D25, each a real, registered future candidate:
PBR Texture Set / Multi-Texture Binding; Tangent Attribute + Normal
Mapping; IBL / HDR Environment; Shadow Foundation; Tone Mapping /
Post-processing. None is designed by this Spec.

## Cross-cutting note (governance, not scope)

This Spec was drafted at explicit human direction, as the next drafting
priority, placed ahead of Android Platform
(`specs/README.md` Section B, Candidate Order 1) and ahead of Shadow,
IBL, and Post-processing. **Android Platform's own Candidate Order 1
registration, dependencies, and scope are entirely unaffected** — see
`specs/README.md`'s own updated Section B note recording this
reprioritization, matching the identical pattern every prior
reprioritization note in that section already uses (Descriptor Pool
Capacity Foundation, 2026-08-29; Dynamic Frame Uniform Updates
Foundation, 2026-08-30).
