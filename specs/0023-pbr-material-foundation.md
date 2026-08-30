# Spec: PBR Material Foundation (Direct Lighting)

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-08-30
- **Related Plan(s):** none yet — this Spec's own Human Review Approval
  authorizes drafting a Plan only, not any Implementation, matching
  every prior Spec in this repository's own governance pattern.
- **Related ADR(s):**
  [ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Accepted` — Material asset parameter set, artifact schema, base-color
  texture color-space contract) and
  [ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (`Accepted` — exact BRDF, push-constant extension, rendering contract),
  deliberately two single-responsibility ADRs, not one, per this Spec's
  own drafting brief. A third, real architectural piece — extending the
  shared Camera/Lighting uniform buffer to carry a camera world-space
  position — is recorded as an **Accepted Amendment to
  [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)**
  (the ADR that already owns that buffer's own layout), not as a third
  new ADR and not folded into ADR-0067 — see ADR-0067 D-15 for the
  motivating BRDF need and ADR-0062's own "Proposed Amendment —
  2026-08-30" section (now `Accepted`) for the complete, self-contained
  amendment text.
- **Human Review Approval (2026-08-30):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-30, accepting this document's
  own "Decisions for Human Review" section in full, per the corrections
  produced during one centralized, final review round (below) — see
  that section for the complete, itemized record. Accepting
  [ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Proposed` → `Accepted`),
  [ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (`Proposed` → `Accepted`), and
  [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
  own "Accepted Amendment — 2026-08-30" (its own top-level ADR Status
  unchanged at `Accepted`) in the same pass.
  **This approval authorizes drafting Plan 0023 only — not any
  Implementation.**

## Final Review Round (2026-08-30) — closed findings, recorded before approval

A single, targeted centralized final review examined this Spec and its
two ADRs against real Slang reflection JSON (a live `slangc` compile of
the candidate push-constant block, both stages), a real MSVC layout
probe (`sizeof`/`alignof`/`offsetof` on two candidate C++ structs), a
real `vulkaninfo` query of this session's own reference GPU, and a
direct reading of `extractCameraMatrices()`/`bindUniformBuffer()`'s own
real code — not by re-deriving conclusions from the original drafting
round's own reasoning alone. Every item below was closed with a real
design correction or an explicit, evidence-backed lock — recorded here
so each change is visible, not silently folded in:

1. **The push-constant total size was a real, confirmed error, not a
   disclosed estimate — corrected from 88 to 96 bytes.** The original
   draft summed each field's own byte width (64+16+4+4=88) and stopped,
   conflating "the last field's own end offset" with "the real
   push-constant range size." A real `slangc` compile of the candidate
   block (both stages) and a real MSVC `alignas(16)` layout probe both
   independently confirm the true total is **96 bytes** — Slang rounds
   the push-constant block's own container size up to a 16-byte
   boundary. `PbrPushConstants` is corrected to `alignas(16)`
   (`sizeof == 96`), matching this codebase's own existing
   `FrameLightingData`/`DirectionalLightGpu` convention exactly.
   Headroom against Vulkan's own guaranteed 128-byte minimum corrected
   from 40 to 32 bytes — every occurrence in the Spec and ADR-0067
   updated (D7/D8, ADR-0067 D-6/D-8/D-11 and its own Consequences/
   Alternatives Considered).
2. **The BRDF's own geometry-term remap carried a real citation error —
   `k = alpha/2` is Karis's IBL remap, not his direct-lighting one.**
   Corrected to the correctly-cited direct-lighting remap,
   `k = (roughness+1)²/8`, using the clamped perceptual `roughness`
   value directly, never `alpha` — a real, load-bearing distinction the
   original draft's single shared variable name obscured. Fixed in
   ADR-0067 D-1/D-2 and this Spec's own D12, with `kMinAlpha`'s own
   scope (guards `alpha` for `D` only, never `k`) stated explicitly to
   prevent the same conflation recurring (ADR-0067 D-4).
3. **Push-constant fragment-stage visibility, left as two undecided
   alternatives, is now locked to uniformly widening every Pipeline's
   `stageFlags` to `VERTEX | FRAGMENT`** — matching ADR-0062's own
   already-shipped precedent exactly, closing the simpler,
   precedent-matching choice with real evidence (this codebase's own
   already-documented "stray, harmless, unread `pushConstantBuffer`
   reflection entry" fact, `slang_json_transform.cpp:244-255`, plus
   standard Vulkan pipeline-layout compatibility rules) rather than
   deferring the choice to Human Review. The rejected per-Pipeline
   RHI-internal-field alternative is recorded in ADR-0067's own
   Alternatives Considered, not left as an open implementation
   question (ADR-0067 D-7, this Spec's own D7/D8).
4. **Camera world-space position, a real gap the original drafting round
   surfaced but did not close, is now locked** to extending the shared
   Camera/Lighting uniform buffer (304 → 320 bytes), the new field
   appended *after* the existing region, never inserted — confirmed
   safe for `UnlitTextured`/`LitTextured` by `VulkanCommandList::
   bindUniformBuffer()`'s own real `VK_WHOLE_SIZE` binding
   (`vulkan_command_list.cpp:250`), and confirmed cheap by
   `extractCameraMatrices()`'s own already-computed, currently-discarded
   `eye` value (`scene_extraction.cpp:107`). All three other real
   options (shader-side inverse-view extraction; a per-draw push
   constant; reusing existing padding) were compared and rejected with
   stated reasons (ADR-0067 D-15). Recorded as an **Accepted Amendment
   to ADR-0062** — the ADR that already owns this buffer's own layout —
   rather than folded into ADR-0067's own, unrelated BRDF/push-constant
   scope, per this review's own explicit governance instruction.
5. **The output-color-space contract (no tone-mapping, hard clamp) now
   carries an explicit, binding condition on the new golden's own
   human-review step**, not merely a disclosed trade-off: the four
   corner cases (dielectric/metallic × rough/smooth) must be confirmed
   *visually distinguishable* under the existing contract at
   golden-capture time, or Implementation must stop and a dedicated
   Output Transfer Function Spec is the correct next step — never a
   local, `PbrDirectLit`-only gamma/tone-map patch, which would
   reintroduce cross-`MaterialKind` output inconsistency (ADR-0067 D-9,
   this Spec's own D14/D17).
6. **The Material artifact's own new 56-byte size, previously stated as
   "32 + 24," now carries a complete, field-by-field byte table**
   confirming no hidden padding exists anywhere in this format (a
   hand-serialized, explicit shift/mask byte stream, never a C++ struct
   memcpy) — a provable sum, not an estimate (ADR-0066 item 3).
7. **Parameter semantics — `baseColorFactor.a`'s real (in)effect,
   `-0.0f` handling, and whether `PbrDirectLit` requires a texture —
   were previously implicit; all three are now closed explicitly**,
   including the real, confirmed finding that this engine has zero
   alpha-blending capability anywhere (`blendEnable = VK_FALSE`, every
   Pipeline, `vulkan_device.cpp:1125`) — `baseColorFactor.a` is stored
   and validated but currently inert, matching the texture's own
   already-inert alpha channel exactly (ADR-0066 item 8).
8. **The base-color-texture sRGB validation's own scope was implicit —
   now stated explicitly as `PbrDirectLit`-only**, with an explicit
   confirmation that Spec 0016's own dual-format GPU test fixture and
   every currently-shipped `UnlitTextured`/`LitTextured` Material are
   unaffected (this Spec's own D6).
9. **The validation sphere mesh's own exact requirements (topology,
   attributes, radius, winding, pole handling, determinism, one-mesh-
   many-transforms) were left as "a sphere" — now fully specified**,
   closed against this codebase's own real, existing mesh-authoring and
   validation conventions (this Spec's own D16).
10. **The new PBR golden's own discriminating power was asserted, not
    demonstrated as verifiable** — an asymmetric four-sphere layout and
    a concrete set of reversible negative/mutation tests (metallic
    forced to 0; roughness forced constant; camera position sign
    flipped; Fresnel blend disabled; Point attenuation bypassed) are now
    required, each confirmed to make the corresponding test/golden
    comparison actually fail before being accepted as real coverage
    (this Spec's own D17, Testing & Verification Plan).

No unresolvable architectural conflict was found. Every item above was
closed within this Spec's own existing, evidence-grounded scope — no
finding required a second base-color texture, a new descriptor binding,
or reopening Spec 0021's own descriptor-pool-capacity proof.

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
  `Atlantis::RHI`'s public API surface at all (ADR-0067 D-7's own locked
  recommendation — uniform push-constant stage-flags widening — adds no
  new RHI type, field, or method); `Atlantis::Renderer`'s public API
  gains exactly three new, additive accessors on `Material` (D9) and no
  other change.

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

**Yes** — three distinct architectural decisions, deliberately kept as
two new, single-responsibility ADRs plus one scoped amendment to an
already-`Accepted` ADR, never bundled together:

- **[ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Accepted`):** widens `MaterialAssetData`'s own field set and the
  Material artifact's own binary schema (a real Asset System data-format
  decision), and adds a new Runtime-side texture-color-space
  cross-validation (closing a gap ADR-0057 already disclosed and left
  open).
- **[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (`Accepted`):** extends the RHI's own push-constant byte layout
  (locked at 96 bytes, real Slang/MSVC evidence) and uniformly widens
  every Pipeline's own push-constant stage visibility to `VERTEX |
  FRAGMENT` (matching ADR-0062's own already-shipped precedent) — no
  new RHI type, field, or method; `CommandList`'s own public API surface
  is entirely unchanged.
- **[ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
  own Accepted Amendment — 2026-08-30** (not a new ADR): extends the
  already-`Accepted` shared Camera/Lighting uniform buffer with one new,
  trailing, frame-scoped field (camera world-space position), growing
  it from 304 to 320 bytes, under two structural conditions (existing
  304-byte region's own layout never altered; no existing shader ever
  required to change source) — recorded on ADR-0062 because that ADR
  already owns this buffer's own layout decision, not folded into
  ADR-0067's own, unrelated BRDF/push-constant scope.

These three are deliberately kept apart — the first is a pure Asset
System data-format/validation decision with no Vulkan/GPU content; the
second is a pure RHI/rendering-math decision with no Asset System
content; the third is a scoped extension of an existing, unrelated
frame-data decision that happens to be motivated by the second —
matching this codebase's own established "one ADR, one architectural
responsibility" convention (e.g. ADR-0061 vs. ADR-0062 for Spec 0019's
own Light-component-vs-frame-uniform split) and its own established
amendment-over-new-ADR convention for a scoped extension of an already-
`Accepted` decision (ADR-0041's/ADR-0042's own prior Accepted
Amendments).

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
**Validation scope confirmed precise, not "all Materials":** this check
runs only when `kind == PbrDirectLit` — every existing `UnlitTextured`/
`LitTextured` Material asset (both currently-shipped ones, using
`Rgba8Unorm` textures) is completely unaffected, and Spec 0016's own
dedicated dual-format GPU test fixture (which cooks and samples the
identical source image as both `Rgba8Unorm` and `Rgba8Srgb`, neither
via a `PbrDirectLit` material) is equally unaffected — this new
validation adds a constraint on one new `MaterialKind` only, never
narrows what `Rgba8Unorm`/`Rgba8Srgb` textures may be used for
elsewhere.

### D7. Per-material parameter transmission method

**Recommendation:** extended push constants — `objectToWorld` (64
bytes, unchanged) + `baseColorFactor`/`metallicFactor`/`roughnessFactor`
(24 bytes, ending at byte offset 88), whose real, Slang-reflection- and
MSVC-layout-confirmed total block size is **96 bytes** (`alignas(16)`,
ADR-0067 D-6 — corrected during final review from an earlier
hand-computed 88), fitting comfortably within Vulkan's guaranteed
128-byte minimum (ADR-0025) with **32 bytes to spare**. See
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-6/D-7 and "Alternatives Considered" there for why a new per-material
uniform buffer was evaluated and not recommended for this round (it
would reopen Spec 0021's own descriptor-pool-sizing proof for no benefit
given the push-constant headroom already available).

### D8. If push constants: exact layout, byte count, stage visibility, compatibility

**Recommendation, locked (no longer left open for Human Review):** see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-6 (exact 96-byte layout — real Slang reflection and a real MSVC
`alignas(16)` layout probe both confirm this, not the field-sum-only
88 an earlier draft assumed — `objectToWorld` at its existing offset
0/size 64, the three new fields ending at offset 88, 8 bytes of
Slang's own trailing block-rounding padding) and D-7 (every Pipeline's
own push-constant `stageFlags` uniformly widened to `VERTEX |
FRAGMENT`, matching ADR-0062's own already-shipped precedent exactly —
no new RHI-internal field, no `PipelineCreateParams` change,
`CommandList::pushConstant()`'s own public signature unchanged). D-7's
own real evidence (the Shader System's already-documented "stray,
harmless, unread `pushConstantBuffer` reflection entry" fact, plus
standard Vulkan pipeline-layout compatibility rules) closes the
per-Pipeline-field alternative as unnecessary — it is recorded only in
ADR-0067's own Alternatives Considered now, not left as an open
implementation choice. `objectToWorld`'s own existing push range is
unchanged in offset/size for `UnlitTextured`/`LitTextured`; only
`PbrDirectLit`'s own Pipeline gets the wider (96-byte) range. Full
compatibility argument: ADR-0067 D-8.

### D9. `Material` ownership of PBR parameters; Renderer read path; public API

**Recommendation, locked (no longer left open for Human Review):**
`atlantis::renderer::Material` gains three new, optional, borrowed-by-
value fields — `baseColorFactor` (4 floats), `metallicFactor`,
`roughnessFactor` — set once in its constructor exactly like
`sampledTexture_`/`sampler_` already are (present-or-default, no
setter, fixed for the object's lifetime). This is the locked choice
over a Runtime-owned side table: `Material` already exists specifically
to hold "whatever a Pipeline needs to draw with" (it already owns
`pipeline_` and borrows `sampledTexture_`/`sampler_`) — three more POD
`float` values is a direct, minimal extension of an existing,
established shape, while a side table would introduce a **new**
parallel state structure whose keys must be kept in lockstep with
`materialResourceMap_`'s own keys, a new invariant this codebase does
not otherwise need. `realizeOneMaterialCandidate()`
(`material_realization.cpp:119-183`) already has `materialData` (the
loaded `MaterialAssetData`) in scope at the exact point it calls
`createMaterial()` — passing `materialData.baseColorFactor`/
`metallicFactor`/`roughnessFactor` through to `Material`'s own
constructor is a direct, local change, not a new data-flow path.
`Renderer::drawFrame()`'s existing per-`DrawItem` loop
(`renderer.cpp:27-41`) reads `item.material->baseColorFactor()`/etc.
(new, `const`, `noexcept` accessors matching `pipeline()`/
`sampledTexture()`/`sampler()`'s own existing shape) to build the
push-constant payload passed to `pushConstant()` — `AssetSystem`'s own
`MaterialAssetData` DTO is never passed into `Renderer` directly,
preserving the existing module boundary (Runtime is still the sole
translator from Asset System DTOs to Renderer/RHI types, matching
ADR-0059/ADR-0060's own established boundary). `UnlitTextured`/
`LitTextured` Materials simply never read these three fields when
constructing their own (unchanged, 64-byte) push-constant payload —
matching how they already ignore `sampledTexture_`/`sampler_` when
`nullptr`. Survives a format-change rebuild unchanged: `Material`'s own
three new fields are copied from the same, already-resolved
`materialData` every rebuild candidate is already built from
(`rebuildMaterialsForFormatChange()`, `material_realization.cpp:277-332`),
never re-defaulted or dropped.

### D10. `DrawItem` — stays unchanged

**Recommendation:** yes, unchanged — `DrawItem`'s own three fields
(`mesh`, `material`, `objectToWorld`) are sufficient; the three new PBR
parameters are reached via `item.material`'s own new fields (D9), never
a new `DrawItem` field.

### D11. Runtime `MaterialKind` dispatch and format-change rebuild

**Recommendation:** `PbrDirectLit` becomes a third arm of
`selectShaderPair()`'s own existing closed switch (D23 below) and a
third path through `realizeOneMaterialCandidate()`/
`rebuildMaterialsForFormatChange()`'s own existing, unmodified
mechanism (both already `MaterialKind`-parametric via the shader-pair
selection they already delegate) — no new realization phase, no new map,
no new format-change-detection mechanism. See "Pre-draft verification"
above for the exact real call sites this reuses unchanged. **PBR
parameters do not regress or reset across a format-change rebuild**
(D9 above): every rebuild candidate is built from the same, already-
resolved `materialData` (`MaterialAssetData`, unchanged by a format
change) each material's own first realization already used — `Material`'s
own three new fields (D9) are copied fresh from that same source on
every rebuild, never re-defaulted, never carried over stale from the
old batch.

### D12. Exact BRDF

**Recommendation:** see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-1 through D-5 for the complete, named-constant, per-division-guarded
formula (Lambertian diffuse with metallic energy split; GGX/Trowbridge-
Reitz normal distribution; Schlick-GGX geometry with Karis's own
**direct-lighting** `k = (roughness+1)²/8` remap — corrected during
final review from an earlier draft's mislabeled `k = alpha/2`, which is
actually Karis's *IBL* remap, not his direct-lighting one; Schlick
Fresnel with dielectric `F0 = 0.04`/metallic `F0 = baseColor` blend;
ADR-0062's existing, unchanged linear Point attenuation; multi-light
accumulation via the identical `for`-loop-and-`+=` shape
`lit_textured.slang` already uses).

### D13. Coordinate/normal/view/light vector conventions

**Recommendation, locked (no longer left open for Human Review):**
identical conventions to `lit_textured.slang`'s own existing,
already-verified code — world-space `N` from
`mul((float3x3)objectToWorld, input.normal)` (unchanged), world-space
`L` derived per-light exactly as `lit_textured.slang` already derives it
(`-direction` for Directional, `normalize(position - worldPosition)` for
Point). The one new vector, `V` (view direction), is
`normalize(cameraWorldPosition - worldPosition)` — `cameraWorldPosition`
is supplied by extending the shared Camera/Lighting uniform buffer, per
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-15 (a real, previously-unnoticed gap this Spec's own investigation
surfaced, now closed with real evidence — `extractCameraMatrices()`
already computes this exact value, `scene_extraction.cpp:107`, and
currently discards it): the shared buffer grows from 304 to 320 bytes,
the new field appended *after* the existing region (never inserted
between `CameraMatrices` and `FrameLightingData`), confirmed to require
zero source change to either `textured_quad.slang` or `lit_textured.slang`
— both keep reading exactly their own existing byte ranges (0-127 and
0-303 respectively), unaffected by the buffer's growth, per
`VulkanCommandList::bindUniformBuffer()`'s own existing `VK_WHOLE_SIZE`
binding (`vulkan_command_list.cpp:250`). This closes what the original
drafting round left as an open question — see ADR-0067 D-15 for the
complete comparison against the three rejected alternatives (shader-side
inverse-view extraction; a per-draw push constant; reusing existing
padding) and the accompanying Accepted Amendment to ADR-0062.

### D14. Output color space

**Recommendation:** no change to the existing "final `clamp(..., 0, 1)`
is the only transformation" contract — see
[ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-9 for the full, disclosed trade-off (bright specular highlights on
smooth surfaces hard-clip rather than tone-map; this engine's current
RenderTarget format is UNORM, confirmed via
`vulkan_presentation.cpp:107-112` and every golden fixture, with no
hardware or shader-side display-encode step anywhere today) and its own
explicit, binding condition on this Spec's new golden's human-review
step (D17): the four corner cases must be confirmed *visually
distinguishable*, not merely non-black — a real, blocking finding at
golden-capture time if they are not, requiring a dedicated Output
Transfer Function Spec rather than a local, `PbrDirectLit`-only patch.
All three `MaterialKind`s this engine ships follow this identical
output contract — consistency across `MaterialKind`s in one scene was a
real concern this review raised, and is exactly what not giving
`PbrDirectLit` its own output transform achieves.

### D15. Direct-light cap and 176-byte `FrameLightingData`

**Recommendation:** unchanged — see ADR-0067 D-10. This BRDF consumes
the identical, already-extracted per-light data `lit_textured.slang`
already consumes; only the per-light *math* changes.

### D16. Validation mesh choice — exact requirements, closed

**Recommendation:** a new, hand-authored, non-shared-vertex UV-sphere
`.mesh.txt`, generated by an offline, throwaway script (never committed
as engine/tooling code — this codebase's own convention is 100%
hand-authored `.mesh.txt` text sources today, confirmed by "Pre-draft
verification" above; no procedural mesh-generation capability is added
to the engine or its Tools) and checked in as an ordinary text mesh
source, following the exact same authoring format every other mesh
asset already uses. **Not** the existing cube or quads — a flat face's
`N·V` varies too weakly to demonstrate this BRDF's own roughness/
metallic-driven highlight shape and falloff, confirmed by direct
inspection of the only three existing mesh assets (Pre-draft
verification above). **Exact requirements, locked for the Plan to
implement against, not left as generic "a sphere":**

- **Topology:** UV-sphere (latitude/longitude), not icosphere —
  simplest to hand-verify row/column count and pole handling
  deterministically; a Plan-time-fixed segment count (e.g. 16 latitude
  × 24 longitude bands — exact numbers a Plan-time detail, chosen for a
  visually smooth silhouette at this Spec's own golden resolution,
  512×512, matching every other golden's own extent).
- **Non-shared-vertex, matching the existing `textured_quad_left/right`
  precedent exactly:** every triangle gets its own duplicated vertices
  (never an indexed shared-vertex topology like `minimal_cube`'s own),
  so each vertex carries a hard, unambiguous per-face or smoothly-
  interpolated-per-vertex normal without any shared-vertex averaging
  ambiguity.
- **Attributes, matching the existing, unchanged 44-byte vertex layout
  exactly (position/color/UV0/normal, no tangent — Non-Goals):**
  position on the unit sphere scaled by a fixed, Plan-time-named radius;
  color a fixed, inert placeholder (e.g. white — `PbrDirectLit` never
  reads vertex color); UV0 standard equirectangular (`u = longitude /
  2π`, `v = latitude / π`, with the two pole rows collapsing to
  degenerate but still well-defined UV coordinates); normal the exact,
  analytically-known unit-length outward radial direction at each
  vertex (`normalize(position - center)`), never approximated.
- **Radius and center:** a fixed, Plan-time-named radius (e.g. `1.0`)
  centered at the mesh's own local origin — object-to-world placement
  (position/scale) is the scene's own job (D17), not baked into the
  mesh.
- **Winding order:** counter-clockwise front-facing, matching every
  other existing mesh asset's own established convention (confirmed
  consistent across `minimal_cube`/`textured_quad_left/right` — a
  Plan-time direct check, not re-derived here).
- **Normal length:** exactly unit length at every vertex (an analytic
  sphere normal is exact, not approximated/averaged) — must independently
  pass the identical length-squared tolerance check Spec 0020's own
  `MeshSourceVertex` validation already applies to every authored normal,
  with zero special-casing for this mesh.
- **Pole handling:** the two polar rows (a UV-sphere's own natural
  degenerate case, where multiple vertices at one pole share the same
  position but different UV `u`) are duplicated per-longitude-segment,
  never collapsed to a single shared vertex — consistent with this
  mesh's own non-shared-vertex topology overall.
- **Determinism:** vertex and index emission order is fixed and
  reproducible (row-major, latitude-then-longitude) — re-running the
  offline generation script must produce byte-identical `.mesh.txt`
  output, though the **checked-in `.mesh.txt` file itself, not the
  script, is this Spec's own authoritative source** (matching D16's own
  "never committed as engine/tooling code" framing) — the repository
  does not claim this asset can be regenerated by any in-repo tool.
- **Verification, not regeneration, is what this Spec's own test suite
  owns:** GPU-independent tests parse the checked-in `.mesh.txt` and
  confirm vertex count, non-shared-vertex topology (no duplicate
  position+normal pairs collapsed), every normal's own unit length, UV0
  range `[0, 1]`, and consistent counter-clockwise winding — proving the
  checked-in asset's own real properties, never assuming the generation
  method was correct.
- **One mesh asset, reused via multiple scene-node transforms — not
  four separate mesh files.** D17's own four material corner cases
  (dielectric/metallic × rough/smooth) are four distinct `PbrDirectLit`
  *materials* applied to four scene nodes that all reference the
  *same* one sphere `AssetId`, exactly matching how `world_scene.scene.txt`
  already instances `minimal_cube` five times from one mesh asset —
  duplicating the mesh itself four times would be pure, unjustified
  asset duplication with no benefit, since geometry is identical across
  all four corners.

### D17. New PBR validation scene and golden — discriminating power made explicit

**Recommendation:** a new scene (e.g. `pbr_material_demo.scene.txt`)
placing **four instances of the one sphere mesh** (D16) at distinct,
non-mirror-symmetric positions (an asymmetric layout, matching
`lighting_demo`'s own existing "both light kinds independently visible"
asymmetric design precedent — never a simple row/grid a mirrored render
could accidentally still pass), each with its own distinct
`PbrDirectLit` material spanning all four corners of the metallic ×
roughness space this BRDF's own math is sensitive to — dielectric-rough,
dielectric-smooth, metallic-rough, metallic-smooth — lit by both one
Directional and one Point light, positioned so **each light's own
contribution is independently visible on at least one sphere from a
angle the other light does not equally illuminate** (matching
`lighting_demo`'s own already-verified "Directional/Point contributions
each independently visible" human-review bar).

**Explicit discriminating-power requirement, not left implicit:** the
scene/camera/light layout must let a human reviewer (and this Spec's
own real-GPU parameter-transmission test) visually and numerically
confirm all of: dielectric vs. metallic (a real Fresnel/`F0` difference
at grazing angles); rough vs. smooth (highlight size/sharpness); that
`baseColorFactor` reaches the fragment shader (a visible tint
difference between two otherwise-identical spheres); and that both
light kinds contribute (per above). See the Testing & Verification
Plan's own new "Negative/mutation tests" item for the concrete,
reversible failure-injection tests that prove this discriminating power
is real, not asserted.

Golden captured per ADR-0042's existing "Initial baseline bootstrap"
amendment (Implementation committed first, capture against a clean
commit, human-reviewed, PNG/sidecar landed in a separate, later commit)
— no relaxation of that process. **Human-review bar for this specific
golden, stated precisely (not "non-black, non-garbage" alone):** the
four corner cases must be confirmed visually distinguishable from one
another under this Spec's own existing, unmodified clamp-only output
contract (ADR-0067 D-9) — a real, binding condition on this golden's
own acceptance, not a soft aspiration (D14 above).

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
not merely asserted from the Spec 0021 text alone. **Confirmed the new
PBR parameters cannot silently introduce a third descriptor binding by
construction, not merely by design intent:** `baseColorFactor`/
`metallicFactor`/`roughnessFactor` travel exclusively as push-constant
bytes (ADR-0067 D-6/D-7) — Slang's own reflection separates
`descriptorTableSlot` bindings from `pushConstantBuffer` bindings as two
structurally distinct binding kinds (confirmed directly in this
review's own real `pbr_direct_lit.slang` probe reflection JSON: `camera`
and `texturedSampler` each reflect as `descriptorTableSlot`,
`pushConstants` reflects as `pushConstantBuffer`, never conflated) — a
push-constant field can never be reflected as, or accidentally become, a
third descriptor binding regardless of how many scalar fields it grows
to contain, up to Vulkan's own guaranteed push-constant capacity
(ADR-0067 D-6/D-11).

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
  for the widened, exact 56-byte artifact (ADR-0066's own byte table,
  confirmed a provable sum, not a Plan-time estimate); version-bump
  rejection test (an old, 32-byte artifact is rejected,
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
  link closure is unchanged and that `Atlantis::Renderer`'s/
  `Atlantis::RHI`'s public API gains only `Material`'s own three new
  accessors (D9) and no other signature change (D7's own locked
  recommendation adds no new `PipelineCreateParams`/RHI-internal field
  at all — ADR-0067 D-7); a `git diff --check` pass on the final
  Implementation diff.
- **Negative/mutation tests, confirming the verification suite itself
  can actually catch a real regression, not only pass on correct code**
  (added during centralized final review): at minimum, one test each
  confirming a real, deliberate failure is caught — `metallicFactor`
  fixed at `0` regardless of authored value (dielectric/metallic
  visually indistinguishable, must be caught); `roughnessFactor` fixed
  at one constant value regardless of authored value (rough/smooth
  indistinguishable); `cameraWorldPosition`'s own sign flipped (specular
  highlight lands on the wrong side of the surface); `F0` computed
  without the metallic/dielectric blend (metals look dielectric or vice
  versa); Point-light attenuation (D5) disabled/bypassed (Point
  contribution never falls off with distance). Each must be confirmed,
  by deliberately introducing it and reverting, to make the
  corresponding CPU reference test or golden comparison actually fail —
  not merely asserted to be "obviously" caught.

## Risks & Open Questions

**Status after centralized final review (2026-08-30):** every item this
Spec's own original drafting round left open for Human Review is now
closed with real evidence — see each numbered Decision above and
ADR-0066/ADR-0067's own corresponding Decision items for the exact
resolution. What remains below are genuine, permanent risk disclosures
— real, evidence-grounded bets this Spec makes and discloses, not
unresolved design forks blocking approval.

- **D5's own single-texture-plus-scalars scope is a real, disclosed bet,
  not a proven-sufficient design** — if real Implementation evidence
  (the new PBR validation scene/golden, D17) shows the visual result is
  unsatisfying without a genuine metallic-roughness *texture* (spatially
  varying, not a single scalar per Material), that is itself a new
  architectural decision (a second descriptor binding, reopening Spec
  0021's own pool-capacity proof) requiring its own Spec/ADR/Human
  Review — **never silently added** during this Spec's own eventual
  Plan or Implementation, per this Spec's own drafting brief. This risk
  is permanent by design (D5's own recommendation is deliberately
  scoped this way), not an artifact of incomplete review.
- **D9 (`Material` ownership of PBR parameters — locked to new fields,
  not a side table), D13 (camera world-space position — locked to
  extending the shared uniform buffer, ADR-0067 D-15), and ADR-0067 D-7
  (push-constant fragment-stage visibility — locked to uniformly
  widening every Pipeline, matching ADR-0062's precedent) were all real,
  open forks in this Spec's own original drafting round.** Each is now
  closed with real evidence (a real Slang reflection probe, a real MSVC
  layout probe, a real `vulkaninfo` query, and a direct reading of
  `extractCameraMatrices()`'s and `bindUniformBuffer()`'s own real code)
  during this Spec's own centralized final review — see D9/D13 above and
  ADR-0067 D-6/D-7/D-15 for the complete, itemized resolution of each.
  None of these three remains an open implementation choice.
- **The push-constant total size correction (88 → 96 bytes, D7/D8;
  ADR-0067 D-6) and the geometry-term citation correction (`k =
  alpha/2` → `k = (roughness+1)²/8`, D12; ADR-0067 D-2) were both real
  errors in this Spec's own first draft, not disclosed trade-offs** —
  both are now fixed, with the real evidence that caught each recorded
  in place, per this codebase's own governance discipline of showing a
  correction rather than silently rewriting the original mistake away.
- **D9's own decision to give `Material` three new POD fields is a
  small, permanent, disclosed API-surface cost** (three new accessors,
  `atlantis::renderer::Material`) — accepted as the minimal, most
  directly precedented extension of an already-established shape, not
  re-opened as a risk.
- **The choice of Schlick-GGX (with a now-correctly-cited direct-
  lighting `k`) over a height-correlated Smith visibility term (D12;
  ADR-0067 D-2's own Alternatives Considered) remains a legitimate,
  disclosed judgment call**, not a citation error — a future Spec/ADR
  could revisit it with new evidence (e.g. a measured visual accuracy
  gap), but this Spec's own recommendation is not blocked on that
  question.
- **D14's own output-color-space contract now carries a real, binding
  condition on the new golden's own human-review step** (ADR-0067 D-9):
  if the four corner cases are found visually indistinguishable under
  the existing hard-clip contract at golden-capture time, that is a
  real, blocking finding requiring a dedicated Output Transfer Function
  Spec before this Spec's own Implementation may land that golden — not
  a risk this Spec's own approval defers indefinitely, but one its own
  Implementation phase must actively check for.

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
