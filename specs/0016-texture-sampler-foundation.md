# Spec: Texture & Sampler Foundation

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Not yet reviewed/approved — see Human Review
  Decision Table below; this document is submitted for that review.
- **Created:** 2026-08-23
- **Related Plan(s):** None yet — this round drafts only the Spec and its
  ADRs, per explicit human direction; no Plan is authorized until this
  Spec itself reaches `Approved`.
- **Related ADR(s):**
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)
  (sampled Texture/Sampler RHI boundary and ownership),
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)
  (upload, resource state, RenderGraph responsibility, and descriptor
  binding), [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  (texture asset format, decoder dependency, and color space contract)
  — all `Proposed`.

## Summary

Give Atlantis its first general, sampled 2D color texture: a build-time
cooker turning an authored PNG into a versioned, little-endian runtime
artifact (reusing `Atlantis::AssetSystem`'s existing cook/artifact/load
conventions), a Runtime-side, RenderGraph-driven CPU→GPU upload path
(staging buffer → `copyBufferToTexture()` → a `TransferDestination`→
`ShaderRead` transition), a new, independent, immutable RHI `Sampler`
object, and a `Material`/Shader binding path that lets one fragment
shader sample one bound texture — proved end to end by a new, independent
headless fixture and its own first image-regression golden. Every layer
this closed loop touches — RHI, Vulkan Backend, RenderGraph, Renderer's
`Material`, Shader System's reflection/contract surface, and Asset
System's cooker/loader — currently has an explicit, load-bearing gap
this Spec's own pre-draft code verification confirmed (cited throughout
Requirements and the Human Review Decision Table below); none of it is
invented speculatively. Mesh UV (texcoord) attributes remain out of this
Spec's own scope by explicit, disclosed Human Review decision (item 11)
— this Spec's own proof fixture uses hand-authored, non-Asset-System UV
data, matching `minimal_cube_fixture`'s own already-established
hand-authored-construction-path precedent, and does **not** claim a real
asset-sourced textured mesh exists yet.

## Motivation / Problem Statement

Every rendering-capable Spec approved so far explicitly deferred this
exact gap rather than silently ignoring it:

- [ADR-0023](../adr/0023-minimal-renderer-texture-and-depth-buffer-strategy.md)
  (Spec 0007), on today's depth-only `Texture`: *"A general, sampled
  `Texture` (material color maps, etc.) is explicitly future work — this
  spec's acceptance introduces no `Buffer`/`Texture`/`Sampler` type"*
  beyond that narrow depth-attachment shape (`adr/0023-...md:74-77`,
  `:18`).
- `src/rhi/include/atlantis/rhi/texture.h:7-8`'s own class comment states
  verbatim: *"A GPU image used, this round, exclusively as a depth
  attachment (ADR-0023) -- no sampled/shader-read usage, no mipmaps."*
- `src/rhi/include/atlantis/rhi/types.h:22-25`'s own `Format` enum
  comment: *"A future Buffer/Texture spec is expected to introduce its
  own general format concept, quite possibly superseding this enum's
  role rather than extending it in place."*
- Spec 0012 (Asset System Foundation) and Spec 0015 (Scene Asset &
  Serialization Foundation) both explicitly named "Texture/Sampler...
  of any kind" as a Non-Goal, not a silent omission.
- `docs/architecture/module_boundaries.md`'s own "Extension points" for
  Asset System names a texture asset type as future, unscoped work.
- `specs/README.md`'s Candidate Backlog (Candidate Order 8) already
  tracks this gap; the human maintainer directly selected it,
  2026-08-24, to be specced next, ahead of Android Platform (whose own
  Candidate Order 1 position is explicitly unchanged by that selection).

This Spec is that gap, narrowed to the smallest slice that proves the
whole pipeline end to end — author → cook → decode → CPU load → GPU
upload → sample — using a small, purpose-built fixture, not yet a real
PBR material, lighting model, or asset-sourced textured mesh.

## Goals

- A general, sampled 2D color `SampledTexture` RHI type, independent of
  today's depth-only `Texture` (Human Review item 1), with a minimal,
  explicit linear-vs-sRGB color-space contract (item 3).
- An independent, immutable RHI `Sampler` object with a minimal
  filter/address configuration (item 4).
- A real CPU→GPU upload path: a staging `Buffer`, a
  `copyBufferToTexture()` command, and an explicit
  `TransferDestination`→`ShaderRead` resource-state transition — run, per
  AGENTS.md's own Golden Rule ("Render Graph is the mandatory path for
  GPU work. No subsystem submits ad hoc, hand-scheduled GPU work outside
  it.", `AGENTS.md:142-143`), as a genuine one-time RenderGraph
  execution, never a raw `CommandList` sequence issued outside it (item
  6 — see Requirements for why this is a real, previously-unaddressed
  RenderGraph scope gap, not a stylistic choice).
- A `Material`/Shader binding path letting one fragment shader declare
  and sample one bound `SampledTexture` + `Sampler` pair (items 7, 8).
- A build-time texture cooker producing a versioned, little-endian,
  deterministic runtime artifact, following `Atlantis::AssetSystem`'s
  existing mesh/scene cook/artifact/metadata/loader conventions exactly
  (item 9); a Runtime loader that reads only cooked pixel bytes, never
  parsing PNG/JPEG itself (item 10).
- Proof: a new, independent headless fixture rendering a texture-sampled
  quad, matched against its own new, first image-regression golden with
  zero channel difference on repeat runs — the existing `minimal_cube`
  and `world_scene` goldens re-run unmodified as regression evidence
  (item 14).
- An explicit, disclosed decision on mesh UV data (item 11) — not a
  silent substitution of position-derived coordinates for real UV.

## Non-Goals

Explicitly excluded from this Spec's design:

- **PBR, a material graph, or multiple material slots.** One fixed
  fragment shader samples exactly one texture through exactly one fixed
  descriptor binding — the same "one fixed, hardcoded contract" shape
  Shader System's `minimalRendererExpectedDescriptorContract()` already
  uses for its one uniform-buffer binding today.
- **Normal, metallic, or roughness texture semantics.** The one texture
  this Spec introduces is a plain sampled color image; nothing in this
  Spec's design assigns it, or any future texture, a lighting-specific
  meaning.
- **Mipmap generation, texture streaming, or virtual/sparse textures.**
  Every `SampledTexture` this Spec creates has exactly one mip level
  (item 12) — no mip chain, no partial residency.
- **Compressed texture formats (KTX2/Basis, ASTC, BC/DXT).** The
  artifact stores uncompressed RGBA8 pixel bytes only.
- **Texture arrays, cubemaps, or 3D textures.** Exactly one 2D image per
  `SampledTexture`.
- **A bindless descriptor system.** The one new binding this Spec adds
  is a second, fixed slot on the same small, hardcoded per-`Pipeline`
  descriptor-set-layout Vulkan Backend already uses for the uniform
  buffer — not a growable or indexed binding table.
- **Render-to-texture, or sampling a depth attachment as a shader
  resource.** `SampledTexture` is populated only by this Spec's own
  CPU→GPU upload path, never by rendering into it; today's depth
  `Texture` gains no sampled/shader-read usage of any kind — see
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md).
- **Lighting, shadowing, HDR, or post-processing of any kind.** This
  Spec's own fixture applies no lighting model — the sampled color is
  written to the color attachment unmodified, matching
  `minimal_mesh.slang`'s own existing "no lighting" scope.
- **Android, iOS, or Linux.** Windows remains this Spec's own verified
  target, matching every prior Spec.
- **A second graphics backend.** Vulkan-only, matching every prior Spec.
- **Any change to the Scene Asset format ([Spec 0015](0015-scene-asset-serialization-foundation.md)).**
  A scene's `Renderable` continues to name exactly one mesh `AssetId`;
  no texture or material reference is added to the scene schema this
  round (item 15).
- **Real mesh UV (texcoord) data in `StaticMeshAssetData`, the mesh
  authoring grammar, or the mesh runtime artifact.** This Spec's own
  proof fixture uses hand-authored, non-Asset-System-sourced UV
  coordinates (item 11) — extending the actual mesh asset pipeline with
  a real `float2 UV0` attribute is named, explicitly, as an immediate
  follow-up candidate this Spec does **not** implement; see Out of
  Scope / Future Work.

## Requirements

### Functional

- **`SampledTexture` (new RHI type, independent of `Texture`)**: a
  move-only, single-owner GPU image usable as a shader-read resource —
  extent, `SampledTextureFormat`, and a fixed mip-level count of 1 (item
  12). Created via a new `Device::createSampledTexture(SampledTextureCreateParams)`
  factory (exact method/param names a Plan-level detail), matching
  `createTexture()`/`createBuffer()`'s own existing factory-method shape.
  Today's `Texture` (`src/rhi/include/atlantis/rhi/texture.h`) is **not**
  generalized or modified in any way — its own depth-only contract, its
  `DepthFormat` enum (still one value, `D32Sfloat`), and every existing
  caller are untouched (item 1;
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)).
- **`SampledTextureFormat` (new, independent enum)**: decoupled from the
  existing swapchain/offscreen-shaped `Format` enum
  (`types.h:26-32`, whose own doc comment already anticipates exactly
  this kind of extension, `types.h:22-25`, but whose four values —
  including two BGRA variants meaningful only for a platform swapchain —
  are not a good fit for an authored-texture color-space contract). First
  two supported values: `Rgba8Unorm` (linear) and `Rgba8Srgb` (item 3;
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)).
- **`Sampler` (new, independent, immutable RHI type)**: created once via
  a new `Device::createSampler(SamplerCreateParams)` factory, move-only,
  single-owner, RAII-torn-down, matching `resource_lifetime.md`'s own
  "explicit ownership, borrowed use, no hidden caching" principle
  exactly. Minimal configuration: a single `Filter` value (`Nearest` |
  `Linear`, applied to both minification and magnification — no separate
  mip filter, since every `SampledTexture` has exactly one mip level) and
  a single `AddressMode` value (`Repeat` | `ClampToEdge`) applied to both
  U and V. No LOD bias, no anisotropy, no compare-op, this round (item 4;
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)).
- **New `BufferPurpose::Staging`**: a transient, host-visible buffer
  purpose holding decoded pixel bytes between CPU load and GPU copy,
  extending the existing `BufferPurpose` enum (`types.h:85-90`) exactly
  the way `Readback` already did for Spec 0010 — no change to `Buffer`'s
  own existing "always host-visible/host-coherent" contract
  (`buffer.h:9-16`), since a staging buffer needs exactly that (item 5).
- **New `ResourceState::TransferDestination` and `ResourceState::ShaderRead`**:
  extending the existing 6-value `ResourceState` enum (`types.h:56-63`).
  The Vulkan Backend's exhaustive `(before, after)` barrier-plan table
  (`src/vulkan_backend/src/resource_state_mapping.cpp:104-131`, which
  today hard-fails via `ATLANTIS_CHECK_MSG` on any pair not explicitly
  listed) gains two new, explicit entries:
  `Undefined → TransferDestination` and `TransferDestination → ShaderRead`
  — no wildcard/catch-all transition is added (item 5;
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
- **New `CommandList::copyBufferToTexture(Buffer&, SampledTexture&)`**
  and a third `CommandList::transitionResource(SampledTexture&,
  ResourceState before, ResourceState after)` overload (alongside the
  two existing `RenderTarget`/depth-`Texture` overloads,
  `command_list.h:29,38`) — the CPU→GPU direction `copyRenderTargetToBuffer()`
  (Spec 0010's GPU→CPU readback direction, `command_list.h:84`) does not
  cover today (item 5).
- **RenderGraph gains a real, new resource-binding kind — this is a
  genuine architectural gap this Spec's pre-draft verification found,
  not an assumption.** `execution.h`'s `ResourceBinding` today carries
  exactly two resource-carrying fields, `RenderTarget* target` and
  `Texture* depthTexture` (the latter depth-only), with `execute()`'s own
  transition-insertion logic (`execution.cpp:94-127`) dereferencing
  exactly one of the two per binding — there is no third slot for a
  generic sampled resource. Per AGENTS.md's own explicit, non-negotiable
  constraint (*"Render Graph is the mandatory path for GPU work. No
  subsystem submits ad hoc, hand-scheduled GPU work outside it."*,
  `AGENTS.md:142-143`; RenderGraph module boundary's own Responsibilities
  line: *"no ad hoc direct-submission rendering path is allowed to
  bypass it"*), this Spec's own one-time upload **cannot** be a raw
  `CommandList` sequence recorded outside `render_graph::execute()` — it
  must run as a genuine, if minimal, RenderGraph pass. `ResourceBinding`
  therefore gains a third resource-carrying field for a generic sampled
  `SampledTexture*`, and `execute()`'s transition-insertion logic is
  extended to drive `Undefined → TransferDestination` (before the copy)
  and `TransferDestination → ShaderRead` (after it, before any draw pass
  samples the texture) for it. The upload itself runs as its own
  small, single-pass `RenderGraphBuilder`/`compile()`/`execute()`
  invocation, built and run once, synchronously, before Runtime's first
  per-frame graph — never folded into, or reusing state from, any later
  per-frame graph (item 6;
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
- **`Material` gains a second, fixed descriptor binding.** Today,
  `Material` (`src/renderer/include/atlantis/renderer/material.h:18-32`)
  is a thin wrapper owning only a `Pipeline`; the entire binding surface
  lives on `CommandList`/`VulkanDevice`, hardcoded to exactly one
  binding — `{set 0, binding 0, UniformBuffer, Vertex stage}`
  (`vulkan_device.cpp:808-861`, one `VkDescriptorPoolSize` entry,
  `vulkan_device.cpp:1245-1254`). `Material` gains an optional,
  construction-time `SampledTexture`/`Sampler` pair (borrowed, matching
  `Mesh`/`Material`'s own existing non-owning-reference conventions in
  `DrawItem`); a new `CommandList::bindTexture(SampledTexture&, Sampler&)`
  call, used inside `Renderer`'s existing per-`DrawItem` pass-callback
  loop (`src/renderer/src/renderer.cpp:26-31`) immediately alongside the
  existing `bindUniformBuffer()` call. Vulkan Backend's per-`Pipeline`
  descriptor-set-layout creation and the device-level descriptor pool
  both gain one new, second, fixed entry: `binding 1,
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fragment stage` (item 7;
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
- **Combined image sampler, not separate texture/sampler descriptor
  types.** The one new Vulkan descriptor binding this Spec adds uses
  `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` — one binding slot, one
  `VkDescriptorImageInfo` pairing an image view and a sampler at bind
  time — rather than two separate descriptor types (`SampledImage` +
  `Sampler`) requiring two binding slots and two pool-size entries. The
  RHI-level `Sampler` object itself remains independent (own creation,
  own lifetime, reusable across multiple `SampledTexture`s) — this
  decision is about the underlying Vulkan descriptor-binding mechanism,
  not the RHI's own C++ object model (item 8;
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
- **Shader System gains one new reflected descriptor kind.**
  `DescriptorType` (`reflection_metadata.h:25-27`) today has exactly one
  value, `UniformBuffer`; `slang_json_transform.cpp:245-248` explicitly,
  by its own comment, silently skips any other Slang-reported binding
  kind as "outside this round's modeled scope." This Spec adds a new
  `DescriptorType::Sampler` (matching `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`'s
  own combined shape) value, reflected from a real Slang
  `[[vk::binding(1,0)]]` combined-sampler declaration, and a second,
  new expected-descriptor-contract shape alongside
  `minimalRendererExpectedDescriptorContract()`'s existing one-binding
  contract — this Spec does not remove or narrow the existing contract,
  it adds a sibling for shaders that declare both bindings.
- **Texture cooker (`Atlantis::AssetSystem`)**: `cookTexture(sourceImagePath,
  logicalPathInput, colorSpace, artifactOutputPath, metadataOutputPath) ->
  Result<monostate, TextureCookError>` (exact name a Plan-level detail),
  following `cookStaticMesh()`/`cookScene()`'s own established pattern
  exactly (`cook.cpp:82-125`): normalize logical path → decode the
  authoring image via `stb_image` (promoted from test-only to Tools use,
  see below) → validate (non-zero dimensions, within a defensive maximum
  — Plan-level detail, e.g. 8192×8192 — decoded channel count matches the
  declared `SampledTextureFormat`) → `computeAssetId()` → encode a
  versioned, unconditionally little-endian artifact (magic + fixed
  header: schema version, width, height, format, mip count [= 1],
  pixel-data offset/size, followed by tightly-packed row-major RGBA8
  bytes, explicit shift/mask serialization matching `mesh_artifact.h`'s
  own discipline — never a struct memcpy) plus a text metadata sidecar
  → atomic dual-file write (`writeBytesAtomically()`/`writeTextAtomically()`,
  the same temp-file-then-rename pattern every existing cooker uses).
  Exposed via a new mode of the existing `atlantis_asset_cooker` Tools
  executable, dispatched by `AssetKind`, matching
  `cook_command.cpp:208-217`'s own existing dispatch shape (item 9;
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)).
- **Texture loader (`Atlantis::AssetSystem`)**: `loadTextureAsset(artifactPath,
  metadataPath) -> Result<TextureAssetData, TextureLoadError>` (exact
  name a Plan-level detail), mirroring `loadStaticMeshAsset()`'s own
  cross-validation discipline (`load.cpp:40-83`): independently
  re-validates every cook-time condition against the artifact's actual
  bytes (magic, schema version, declared byte counts self-consistent,
  pixel-data size exactly `width * height * 4`), cross-checks the
  metadata sidecar against the artifact, and returns a pure CPU-side
  `TextureAssetData` (width, height, `SampledTextureFormat`, owned pixel
  bytes) naming no RHI type — a composition root elsewhere passes the
  result into `Device::createSampledTexture()` plus this Spec's own
  upload path, exactly the way `loadStaticMeshAsset()`'s own result today
  passes into `renderer::createMesh()` (item 9, 10).
- **Runtime never decodes PNG/JPEG.** `loadTextureAsset()` reads only the
  cooked, already-decoded pixel-byte artifact; `stb_image`'s own linkage
  never reaches `Atlantis::AssetSystem`'s runtime-loading code path or
  any runtime-linked target — confirmed by the same module-boundary
  include-scanning discipline every prior Spec's own Requirements already
  establish (item 10;
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)).
- **`stb_image` promoted from test-only to Tools use.** Today, `Stb::Stb`
  is linked `PRIVATE` into exactly two `tests/image_regression/` targets
  only (`tests/image_regression/support/CMakeLists.txt:12-18`,
  `tests/image_regression/CMakeLists.txt:18-29`); `src/tools/asset_cooker`
  links no image-decoding library at all
  (`src/tools/asset_cooker/CMakeLists.txt:21-27`). ADR-0041 states this
  boundary explicitly: *"the `STB_IMAGE_IMPLEMENTATION`/
  `STB_IMAGE_WRITE_IMPLEMENTATION` translation unit and every call site
  above exist only inside `tests/image_regression/`'s own test-support
  targets ... never in `src/`, never linked into any shipping example or
  the engine's own libraries"* (`adr/0041-...md:156-160`). This Spec
  widens that boundary, disclosed explicitly, not silently: `Stb::Stb`
  is additionally linked `PRIVATE` into `atlantis_asset_cooker`
  (Tools) only — never into `Atlantis::AssetSystem`'s own runtime
  library, never into `src/renderer`, `src/runtime`, or any target a
  shipped executable links. ADR-0041's own license/offline-build
  disclosures (`adr/0041-...md:161-176`) are re-confirmed, unchanged, as
  applying equally to this new linkage. This requires ADR-0041's own
  future Human Review Amendment — **not made by this Spec** (ADR-0041's
  `Accepted` body is not modified here); see
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  and this Spec's own Human Review Decision Table item 10 (item 10).
- **New, independent headless textured fixture and its own first
  golden.** A new fixture (e.g. `textured_quad_fixture`, exact name a
  Plan-level detail) renders one texture-sampled quad (two triangles,
  hand-authored position + UV data, matching `minimal_cube_fixture`'s
  own already-established hand-authored, non-Asset-System-sourced
  construction path — see Human Review item 11) with a small, cooked,
  distinctively-patterned test texture, through this Spec's real
  cook → decode → upload → bind → sample path. A new golden,
  `tests/image_regression/goldens/textured_quad/`, is captured following
  ADR-0042's own "Initial baseline bootstrap" category — the same
  category Spec 0011's own first-ever golden used, since no prior golden
  for this scene exists. **The existing `minimal_cube` and `world_scene`
  goldens are not modified, and their own existing tests are re-run
  unmodified** as regression proof this Spec's changes did not disturb
  either existing rendering path (item 14).

### Non-functional

- **Performance:** cooking and loading a single small test texture (a
  few kilobytes to low hundreds of kilobytes of pixel data) is not a hot
  path — no performance budget beyond "does not noticeably delay the new
  fixture's own startup," matching every prior Spec's own similarly
  unbudgeted one-time-load cost.
- **Memory:** the staging `Buffer` is destroyed immediately after its
  one-time upload command completes (RAII scope exit) — never retained.
  `SampledTexture`/`Sampler` are explicitly, single-owner-held by
  whichever composition root creates them (this Spec's own fixture),
  matching `resource_lifetime.md`'s existing "explicit ownership, no
  hidden caching" principle exactly — no resource pool, no texture
  cache, this round.
- **Portability (within the Vulkan-only Phase 1 constraint):** the
  artifact's unconditional little-endian encoding (matching
  `mesh_artifact.h`'s own established discipline) makes the format
  itself host-endianness-independent; raw, uncompressed RGBA8 pixel data
  carries no platform-specific compression concern to resolve now. This
  Spec's own verified target remains Windows, matching every prior Spec
  — no Android-specific texture-compression format (ASTC/ETC) is
  addressed here (item 16).
- **Other:** every new error condition (`TextureCookError`,
  `TextureArtifactDecodeError`/`TextureLoadError`) is a distinct
  enumerator, never collapsed into a generic "load failed," matching
  this repository's own consistent error-taxonomy discipline
  (`src/asset_system/include/atlantis/asset_system/errors.h`'s own
  existing shape).

## Proposed Design

Build time (once per texture, CMake-triggered) and load time (once, at
this Spec's own fixture's startup), per
[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md):

```
Build time:
  authoring image (.png) ─▶ cookTexture() [AssetSystem, stb_image-backed, Tools-only] ─▶ texture artifact (.atex) + metadata sidecar

Load time (fixture startup, before the first per-frame RenderGraph runs):
  texture artifact + sidecar ─▶ loadTextureAsset() [AssetSystem] ─▶ TextureAssetData (CPU pixel bytes, no RHI type)
                                                                          │
                                              Device::createSampledTexture() + createSampler()
                                                                          │
                                    staging Buffer (BufferPurpose::Staging) populated with pixel bytes
                                                                          │
                        one-time, single-pass RenderGraph execution [RenderGraph -- AGENTS.md's own
                        "Render Graph is the mandatory path for GPU work" constraint, never a raw
                        CommandList sequence outside it]:
                          transitionResource(SampledTexture, Undefined -> TransferDestination)
                          copyBufferToTexture(staging Buffer, SampledTexture)
                          transitionResource(SampledTexture, TransferDestination -> ShaderRead)
                                                                          │
                                                    Material(pipeline, SampledTexture, Sampler)
                                                                          │
                        per-frame RenderGraph draw pass (existing, unmodified shape):
                          bindPipeline / bindVertexBuffer / bindUniformBuffer / bindTexture(SampledTexture, Sampler)
                                                                          │
                                                              fragment shader samples, writes color
```

Module ownership: RHI (`SampledTexture`, `Sampler`, new `ResourceState`/
`BufferPurpose` values, new `CommandList` overloads) and Vulkan Backend
(implementation: manual per-resource `vkAllocateMemory` — no VMA,
matching the existing, `Accepted`-deferred (ADR-0015) pattern every other
RHI resource already uses; barrier-plan table entries; descriptor pool/
layout extension) per
[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md);
RenderGraph (new `ResourceBinding` kind, transition-insertion logic) and
Renderer's `Material`/`CommandList::bindTexture()` per
[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md);
Asset System (`cookTexture()`/`loadTextureAsset()`, `Stb::Stb`'s widened
Tools-only linkage) and Shader System (`DescriptorType::Sampler`) per
[ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).

## Architectural Impact

This Spec introduces or changes, in every case because this Spec's own
pre-draft verification against real, current source confirmed the
existing public API cannot form a reasonable closed loop without it —
none of the following is hidden or silently worked around:

- A **new RHI subsystem boundary**: `SampledTexture`/`Sampler` as
  independent types, new `ResourceState`/`BufferPurpose` values, two new
  `CommandList` capabilities (buffer→texture copy, a third
  `transitionResource()` overload) —
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md).
- A **new RenderGraph resource-binding kind and transition
  responsibility** — `ResourceBinding` gains a third field, `execute()`
  gains new transition logic, and a genuine one-time, single-pass
  RenderGraph execution is introduced ahead of Runtime's first per-frame
  graph. This is the one finding this Spec's own suggested core scope
  (as directed) did not explicitly anticipate — the CPU→GPU upload
  cannot be a raw `CommandList` sequence outside RenderGraph, per
  AGENTS.md's own Golden Rule; it must be real, if minimal, RenderGraph
  scope —
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).
- A **new `Material`/descriptor-binding public API**: a second, fixed
  descriptor slot (combined image sampler, fragment stage), extending
  both RHI's `CommandList` surface and Vulkan Backend's previously
  hardcoded single-binding descriptor-set-layout/pool —
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).
- A **new Shader System reflected descriptor kind**
  (`DescriptorType::Sampler`) and a second expected-descriptor-contract
  shape, alongside (not replacing) the existing one —
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).
- A **new Asset System asset kind** (texture: cooker, artifact format,
  loader), following the established mesh/scene pattern exactly —
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).
- A **new third-party dependency boundary widening**: `stb_image`,
  already an `Accepted` (ADR-0041) test-only dependency, gains a second,
  disclosed linkage point (Tools' `atlantis_asset_cooker`) — never a new
  library, but a real, named boundary change ADR-0041 itself did not
  previously permit, requiring ADR-0041's own future Human Review
  Amendment (not made by this Spec) —
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).

**Explicitly not changed**: `Atlantis::World`'s `Renderable` component
shape (still names exactly one mesh `AssetId`, Spec 0014/0015
unmodified); the Scene Asset artifact format (Spec 0015 unmodified);
`StaticMeshAssetData`/the mesh authoring grammar/the mesh runtime
artifact (no UV added this round, item 11); today's depth-only `Texture`
and its single-value `DepthFormat` enum (completely untouched, not
generalized).

## Human Review Decision Table

| # | Question | Recommendation | Rejected Alternative(s) | Where |
|---|---|---|---|---|
| 1 | Generalize the existing `Texture` (today, depth-only) to also cover sampled color textures, or introduce a new, independent `SampledTexture` type? | New, independent `SampledTexture` type. Depth and sampled-color images differ in Vulkan usage bits, image aspect, and consumption pattern (attachment write vs. shader read) enough that forcing one C++ type to cover both would touch every existing depth-`Texture` call site for no benefit, and contradicts `texture.h`'s own explicit "depth attachment... no sampled/shader-read usage" scoping (`texture.h:7-8`). Matches the existing precedent that `RenderTarget` and depth `Texture` are already two separate types for two different roles. | Generalize `Texture` with a `Kind`/`Usage` tag spanning Depth and Sampled — rejected: forces `DepthFormat` and a new color-format concept into one type and risks every existing depth-`Texture` caller. | [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md) |
| 2 | What is the type boundary between depth and sampled-color formats? | `DepthFormat` (still one value, `D32Sfloat`) is completely untouched. A new, independent `SampledTextureFormat` enum is introduced for sampled color textures — not a shared enum with `DepthFormat`, and not a reuse of the existing swapchain-shaped `Format` enum (see item 3). | A single, unified `Format` enum spanning depth and color — rejected outright, `DepthFormat`'s own existing single-variant-enum precedent (`types.h:65-68`) exists specifically so a future depth format extends it independently of any color concept. | [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md) |
| 3 | What is the first supported set of linear/sRGB sampled-texture formats, and is the existing swapchain `Format` enum reused? | A new, independent `SampledTextureFormat` enum, not the existing `Format` (whose own doc comment already anticipates a future format concept "quite possibly superseding" it, `types.h:22-25`, but whose two BGRA variants are swapchain-specific and meaningless for an authored texture). First two values: `Rgba8Unorm` (linear) and `Rgba8Srgb`, giving an explicit, minimal linear-vs-sRGB contract from the start. | Reuse `Format` directly — rejected, couples an authored-texture color-space contract to swapchain-surface-format concerns that have nothing to do with it. Ship only `Rgba8Srgb` (no linear option) — rejected, the Goals explicitly require "at least" naming the linear/sRGB boundary, not just the color-authoring case. | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 4 | What is the minimal `Sampler` filter/address/LOD API? | A single `Filter` (`Nearest`\|`Linear`, applied to min/mag together — no separate mip filter, since every texture has exactly one mip level this round) and a single `AddressMode` (`Repeat`\|`ClampToEdge`, applied to U/V together). No LOD bias, anisotropy, or compare-op. `Sampler` is independent, immutable, RAII-owned — matching `resource_lifetime.md`'s existing "explicit ownership, no hidden caching" principle. | Separate min/mag/mip filter fields — rejected, meaningless with a fixed single mip level (item 12); would be dead API surface. Exposing anisotropy/LOD bias now — rejected, no consumer or measured need exists yet; matches AGENTS.md's own "do not add abstraction knobs for a capability that isn't being built" discipline. | [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md) |
| 5 | What staging buffer, copy command, and `ResourceState` values does the CPU→GPU upload path need? | A new `BufferPurpose::Staging` (host-visible, extending the existing `BufferPurpose` enum exactly as `Readback` already did); new `ResourceState::TransferDestination`/`ShaderRead` values, with two new, explicit `(before,after)` entries added to Vulkan Backend's existing exhaustive barrier-plan table (`resource_state_mapping.cpp`) — no wildcard transition; a new `CommandList::copyBufferToTexture()` and a third `transitionResource()` overload. | A single combined "upload" `ResourceState` collapsing `TransferDestination` and `ShaderRead` — rejected, these are two genuinely distinct Vulkan image layouts (`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` vs. `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) and collapsing them would make the barrier-plan table's own exhaustiveness check meaningless for this path. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 6 | **(Real API gap, resolved.)** Can the one-time texture upload be a raw `CommandList` sequence issued outside RenderGraph, given it runs once, synchronously, before any per-frame graph? | No. AGENTS.md states plainly: *"Render Graph is the mandatory path for GPU work. No subsystem submits ad hoc, hand-scheduled GPU work outside it"* (`AGENTS.md:142-143`); the RenderGraph module boundary's own Responsibilities line states *"no ad hoc direct-submission rendering path is allowed to bypass it."* `ResourceBinding` (`execution.h`) therefore gains a third, generic-`SampledTexture`-carrying field, and `execute()`'s transition-insertion logic is extended for `TransferDestination`/`ShaderRead`. The upload runs as its own small, single-pass RenderGraph execution (build → compile → execute, once), never folded into or reusing state from any later per-frame graph. | A raw, one-time `CommandList` sequence recorded directly against `Device`, bypassing RenderGraph — this was this Spec's own first-drafted design and was corrected during self-review specifically because it violates AGENTS.md's own already-established, non-negotiable constraint; not a stylistic preference, an actual governance violation. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 7 | What is `Material`'s new binding API for a sampled texture + sampler? | `Material` gains an optional, construction-time, borrowed `SampledTexture`/`Sampler` pair (matching `Mesh`/`Material`'s existing non-owning-reference shape in `DrawItem`). A new `CommandList::bindTexture(SampledTexture&, Sampler&)`, called inside `Renderer`'s existing per-`DrawItem` pass callback alongside `bindUniformBuffer()`. Vulkan Backend's per-`Pipeline` descriptor-set-layout and the device-level descriptor pool each gain one new, fixed entry (binding 1, combined image sampler, fragment stage). | A general, per-`Material` variable-length binding list — rejected as premature, ahead of any real second consumer needing more than one texture; matches this codebase's own "one fixed, hardcoded contract, extended only when a real need appears" discipline (`minimalRendererExpectedDescriptorContract()`'s own existing precedent). | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 8 | Combined image sampler, or separate texture/sampler descriptor types? | Combined image sampler (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`) — one binding slot, one pool-size entry, one `VkDescriptorImageInfo` pairing image view + sampler at bind time. The RHI-level `Sampler` object itself remains fully independent (own creation, own lifetime, reusable across multiple textures) — this decision is purely about the underlying Vulkan descriptor-binding mechanism, not the RHI's own C++ object model. | Separate `SampledImage`/`Sampler` descriptor types (two binding slots, two pool-size entries) — rejected as more descriptor-pool/layout complexity for no benefit this round, ahead of any real need to bind one sampler against multiple images independently. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 9 | What is the texture artifact's format, byte order, row order, size limit, and validation? | Magic + fixed header (schema version, width, height, `SampledTextureFormat`, mip count [= 1], pixel-data offset/size), unconditionally little-endian, explicit shift/mask serialization — matching `mesh_artifact.h`'s own discipline exactly, never a struct memcpy. Row order: the first row in the artifact is the authoring image's own first-decoded row (`stb_image`'s default top-to-bottom origin) — a fixed, documented convention, not left ambiguous. A defensive maximum dimension (Plan-level detail, e.g. 8192×8192) is enforced as an explicit decode-time error, not a crash/OOM risk. Decode independently re-validates: magic, schema version, non-zero dimensions within the maximum, a known format value, and pixel-data byte count exactly `width × height × 4`. | Storing pixel data bottom-to-top (matching some legacy image conventions) — rejected, `stb_image`'s own natural decode order is top-to-bottom and there is no existing convention in this codebase to match instead. No explicit size cap (trust the authoring pipeline) — rejected, an unbounded value read from a corrupted/malformed header is exactly the kind of decode-time risk this repository's existing artifact formats already guard against. | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 10 | **(Real dependency-boundary gap, resolved.)** What authoring-image decoder does the cooker use, and does promoting `stb_image` from test-only to Tools require re-evaluating ADR-0041? | Reuse `stb_image` (already an `Accepted`, license-reviewed, pinned-commit dependency, `Stb::Stb`) rather than writing a redundant decoder — but this genuinely widens ADR-0041's own explicit boundary statement ("never in `src/`, never linked into any shipping example or engine library," `adr/0041-...md:156-160`). `Stb::Stb` is linked `PRIVATE` into `atlantis_asset_cooker` (Tools) only — never into `Atlantis::AssetSystem`'s own runtime library or any runtime-linked target. This requires ADR-0041's own future Human Review Amendment, not made by this Spec/ADR-0057 (ADR-0041's `Accepted` body is untouched here) — approving Spec 0016 without also amending ADR-0041 leaves this dependency boundary inconsistent, and Human Review must explicitly authorize both together. | Write a hand-rolled PNG decoder instead — rejected, duplicates a small, permissively-licensed, already-vetted library for no real benefit, and this repository's own established restraint is "no new general parser library," not "no reuse of an already-accepted one." Ship raw, undecoded PNG bytes as the "artifact" and decode at Runtime load time — rejected outright, directly violates this Spec's own explicit "Runtime never parses PNG/JPEG" requirement and this repository's authoring/runtime separation principle (ADR-0035). | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 11 | **(Explicit, disclosed decision — not silently resolved.)** `StaticMeshAssetData`/`MeshSourceVertex`/the mesh runtime artifact are all confirmed position+color only (24-byte stride) at three independent layers, with no UV field anywhere, and RHI's `VertexAttributeFormat` has only `Float3` (no `Float2`). Does this Spec add real `float2 UV0` to the mesh pipeline, or use procedural/hand-authored coordinates? | **Hand-authored, non-Asset-System UV coordinates in this Spec's own new fixture only** — matching `minimal_cube_fixture`'s own already-established, disclosed precedent of a hand-authored construction path existing alongside (not replacing) an Asset-System-sourced one. This is an explicit, small, fixed UV array written directly in the fixture's own C++ source — **not** derived from vertex position by any formula, satisfying the letter and spirit of "must not silently substitute position-derived coordinates." This Spec's own closed-loop claim is scoped to texture infrastructure (cook/load/upload/sample), not to an asset-sourced textured mesh — that claim is **explicitly not made here**. "Mesh UV Attribute Foundation" (real `float2 UV0` across the authoring grammar, `MeshSourceVertex`, the mesh artifact — requiring a schema version bump per this repository's own established "new schema version, not an in-place mutation" precedent — and `VertexAttributeFormat::Float2`) is registered as an immediate, named, blocking follow-up candidate; see Out of Scope / Future Work. | Add real `float2 UV0` in this Spec — rejected as this Spec's own recommendation (Human Review may override): a genuine three-layer schema change (authoring grammar, artifact schema-version bump, `StaticMeshAssetData`/RHI/Shader-System attribute additions) plus retrofitting or dual-schema-version-supporting the existing, checked-in `minimal_cube.mesh.txt` asset — a substantially different, mesh-schema-focused scope from "Texture & Sampler Foundation," better served by its own, tightly-scoped Spec once a real asset-sourced textured-mesh consumer exists to design against. | This Spec's own Non-Goals; Out of Scope / Future Work |
| 12 | What is this round's mip-level contract? | Exactly 1 mip level, always — no mip-count creation parameter exposed, matching `DepthFormat`'s own "don't expose a knob for a dimension not yet supported" precedent. No mip generation, no mip selection in the shader (a fragment shader sampling a 1-mip texture has no LOD to select). | Expose a mip-count parameter now, defaulted to 1 — rejected, `types.h`'s own existing comments consistently prefer a single-value enum/fixed contract over an unused knob until a real second value is needed. | This Spec's own Non-Goals |
| 13 | What is the upload's synchronization model and the resulting resources' lifetime? | Fully synchronous: the one-time upload RenderGraph execution completes (submit + an existing wait-for-completion equivalent) before the fixture's first per-frame graph runs — matching this Spec's own explicit "single frame-in-flight, synchronous upload" requirement. The staging `Buffer` is destroyed immediately after (RAII scope exit, never retained); `SampledTexture`/`Sampler` are held, explicitly owned, by the same composition root that created them (this Spec's own fixture), matching `resource_lifetime.md`'s existing ownership principle — no implicit sharing, no cache. | An asynchronous/deferred upload (submit without waiting, sample only once a fence signals) — rejected, adds real synchronization complexity this Spec's own minimal-loop scope does not need; Non-Goals already exclude streaming, which is the scenario that would motivate it. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 14 | What proves the texture-sampling path actually works, and how is the new golden categorized? | A new, independent headless fixture (hand-authored quad + UV, a small cooked test texture) rendered through the real cook→load→upload→bind→sample path, matched against its own new, first golden — captured under ADR-0042's existing "Initial baseline bootstrap" category (the same category Spec 0011's own first golden used). The existing `minimal_cube`/`world_scene` goldens and their own tests are re-run unmodified, as regression proof. | Reuse `minimal_cube`'s existing golden for the new fixture — rejected outright, `minimal_cube` has no texture binding and was never rendered with one; a texture-sampling claim needs its own scene and its own golden to mean anything. | This Spec's own Goals/Requirements |
| 15 | Does the Scene Asset format ([Spec 0015](0015-scene-asset-serialization-foundation.md)) gain a texture/material reference this round? | No — confirmed Non-Goal. A scene's `Renderable` continues to name exactly one mesh `AssetId`, entirely unchanged; this Spec's own textured fixture is not scene-driven at all (matching `minimal_cube_fixture`'s own non-scene, hand-authored construction shape). | Add an optional texture `AssetId` to `Renderable` now, unused by anything yet — rejected as speculative scope-widening of an already-`Approved`/merged Spec 0015 format for no consumer this Spec itself provides. | This Spec's own Non-Goals |
| 16 | What is the artifact's portability contract given Windows-now/Android-later? | Unconditional little-endian encoding (matching every prior artifact format) makes the byte format itself host-endianness-independent; raw, uncompressed RGBA8 carries no format-specific mobile-GPU concern to resolve now. Windows remains this Spec's own only verified target, matching every prior Spec — no ASTC/ETC mobile-compression format is chosen or implied here. | Choose a mobile-compressed format now "for" Android — rejected per AGENTS.md's own "do not add abstraction knobs for a capability that isn't being built" discipline; Android Platform itself remains an un-specced Candidate. | This Spec's own Non-Goals |

## Alternatives Considered

- **Defer this Spec entirely; wait for a real PBR/lighting Spec to design
  texture sampling as part of a larger materials system.** Rejected:
  every rendering-capable Spec so far has explicitly deferred exactly
  this gap to "a future spec" rather than designing it speculatively
  ahead of time; waiting further leaves no working example of the
  smallest real texture pipeline for a future PBR Spec to build on, and
  the RenderGraph/descriptor-binding gaps this Spec found (items 6, 7)
  are prerequisites for *any* future texture-consuming Spec, not
  something a materials-focused Spec would want to re-derive from
  scratch.
- **Add real mesh UV0 in this Spec, bundling "Texture & Sampler
  Foundation" with "Mesh UV Attribute Foundation."** Considered and
  rejected as this Spec's own recommendation (Human Review Decision
  item 11) — a genuine three-layer mesh-schema change is a substantially
  different scope from RHI/RenderGraph/Material texture infrastructure,
  and bundling them risks neither landing cleanly; either can proceed
  independently once approved.
- **Bypass RenderGraph for the one-time upload, treating it as a
  narrow, disclosed exception to the "no ad hoc GPU work" rule.**
  Rejected outright, not merely disfavored — AGENTS.md states this
  constraint as non-negotiable, with no disclosed-exception mechanism;
  see Human Review Decision item 6.
- **Separate, non-combined texture/sampler descriptor types.** Rejected
  as more Vulkan descriptor-pool/layout complexity than this Spec's own
  one-texture, one-sampler scope needs; see Human Review Decision item
  8. Remains a legitimate future option once a real multi-sampler
  consumer exists.
- **Reuse `Format` (the existing swapchain/offscreen-shaped enum) for
  sampled-texture color space, rather than introducing a new
  `SampledTextureFormat`.** Rejected — see Human Review Decision item 3.

## Testing & Verification Plan

GPU-independent (unit-level, matching every prior module's own
three-layer verification model):

- Texture cooker: valid-image round-trip (cook then decode reproduces
  the exact same pixel bytes); every named `TextureCookError` condition
  (unreadable/corrupt source image, zero-dimension image, a dimension
  exceeding the defensive maximum) individually triggered and correctly
  reported; deterministic output confirmed by cooking the same source
  twice and comparing artifact bytes exactly.
- Texture artifact decode: every named decode-time error (bad magic,
  unknown schema version, a pixel-data size inconsistent with declared
  width/height, a dimension exceeding the defensive maximum) individually
  triggered and correctly reported, matching `mesh_artifact.h`'s own
  already-shipped test discipline.
- `SampledTexture`/`Sampler` creation: valid parameters succeed; the
  fixed single-mip-level contract confirmed (no mip-count parameter
  exists to misuse); `Sampler`'s own immutability confirmed by
  `static_assert`s on its public surface (no setter, no mutable
  accessor), matching `ValidatedSceneData`'s own V27-style unforgeability
  precedent.
- `ResourceState` barrier-plan table: the two new `(before, after)`
  entries (`Undefined → TransferDestination`,
  `TransferDestination → ShaderRead`) produce the expected Vulkan barrier
  parameters; an unlisted pair still triggers the existing
  `ATLANTIS_CHECK_MSG` failure (regression-confirmed, not merely assumed
  unchanged).
- Shader System: a real captured Slang reflection JSON declaring both
  the existing uniform-buffer binding and a new combined-sampler binding
  produces the expected `ReflectionMetadata` (`DescriptorType::Sampler`
  present, `VertexAttributeType` unchanged); the new descriptor-contract
  shape accepts exactly this two-binding case and rejects a mismatched
  one (wrong count, wrong stage), matching
  `minimalRendererExpectedDescriptorContract()`'s own existing test
  discipline.
- Module-boundary include scan: `Atlantis::AssetSystem`'s own runtime
  loader translation units (`load.h`/`load.cpp` and this Spec's own
  `loadTextureAsset()`) never include `stb_image.h`/`stb_image_write.h`;
  only `atlantis_asset_cooker`'s own translation unit(s) do — confirmed
  by grep, not inspection alone, matching every prior Spec's own
  module-boundary verification discipline.

GPU-required (real hardware, matching every prior Spec's own headless
verification tier):

- **Headless, new golden (this Spec's own central verification claim):**
  cook the test texture, load it, upload it through the new one-time
  RenderGraph execution, bind it via `Material`, render the new textured
  fixture's one frame, and capture/compare against its own new golden —
  zero channel difference on a second, independent run of the same
  fixture (confirming the upload/sample path is deterministic, not
  merely "looked right once"). Vulkan Validation Layers grepped clean
  throughout (zero `VUID`/Validation Error/Warning matches), confirming
  the new barrier-plan entries and descriptor-binding changes are
  themselves layout- and binding-correct, not merely visually
  plausible.
- **Existing-golden regression check:** the existing `minimal_cube` and
  `world_scene` headless tests are re-run unmodified, against their own
  existing, unmodified goldens, with zero channel difference — proving
  this Spec's RenderGraph/`Material`/Vulkan Backend changes did not
  disturb either existing rendering path.
- **Manual visual sanity check** of the new textured fixture's own
  output (not a substitute for the golden comparison above, matching
  every prior Spec's own "golden comparison is the real evidence, manual
  viewing is a sanity cross-check" discipline) — confirms the checkerboard
  or other distinctive test-texture pattern is visibly, correctly
  sampled, not merely bit-identical to some unintended constant output.

## Risks & Open Questions

- **Exact artifact header field layout, exact enumerator names, the
  exact defensive maximum-dimension value, and the exact new
  Slang/reflection JSON binding-kind mapping** are Plan-level details
  this Spec does not fix beyond the *conditions* and *shapes* named in
  Requirements — matching every prior Spec's own precedent of leaving
  concrete C++/file-format shapes to the Plan where no architectural
  content is at stake.
- **Whether a second, separate (non-combined) texture/sampler descriptor
  model will eventually be needed** once a real consumer wants one
  sampler reused across many textures, or vice versa, is a named,
  disclosed open question (Human Review Decision item 8) — either
  answer today is compatible with this Spec's own narrow scope; nothing
  here forecloses it.
- **Whether the one-time-upload RenderGraph shape (one small, single-pass
  graph per texture) generalizes cleanly to a future scene with many
  textures uploaded together** is not addressed here — this Spec's own
  scope is exactly one texture, one upload, and does not attempt to
  design a batched-upload mechanism ahead of a real multi-texture
  consumer.
- **ADR-0041's own scope-widening amendment is a real, disclosed
  prerequisite for this Spec's own Implementation, not yet made.**
  Approving this Spec's own Human Review without also amending ADR-0041
  would leave a genuine inconsistency between an `Accepted` ADR's stated
  boundary and this Spec's own design; Human Review Decision item 10
  names this explicitly rather than assuming it resolves itself.
- **This Spec's own hand-authored UV data (item 11) is not, and must not
  be read as, evidence that a real asset-sourced textured mesh exists.**
  A future Spec claiming that requires "Mesh UV Attribute Foundation"
  (or equivalent) first — this Spec does not, and cannot, substitute for
  it.

## Out of Scope / Future Work

**Mesh UV Attribute Foundation** — real `float2 UV0` across the mesh
authoring grammar, `MeshSourceVertex`, the mesh runtime artifact (a new
schema version, not an in-place mutation, matching this repository's own
established artifact-versioning precedent), `StaticMeshAssetData`, and
RHI's `VertexAttributeFormat::Float2` — is registered as an **immediate,
named, blocking follow-up candidate**, required before any future Spec
may claim a real, asset-sourced textured mesh exists; see Human Review
Decision item 11. This Spec's own hand-authored fixture proves texture
infrastructure only, explicitly not this.

Also remaining out of scope, unaffected by this Spec: PBR/material-graph
support, normal/metallic/roughness texture semantics, mipmap generation/
streaming/virtual/sparse textures, compressed texture formats (KTX2/
Basis/ASTC/BC), texture arrays/cubemaps/3D textures, a bindless
descriptor system, render-to-texture/depth-sampling, lighting/shadow/HDR/
post-processing, an Editor or Tool/Editor Connection Protocol, a Gameplay
SDK, Android/iOS/Linux, and a second graphics backend — all remain later,
separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md). The broader
Cross-Session Stable Identity and Asset Catalog candidate
(`specs/README.md` Candidate Order 7) is unaffected and unextended by
this Spec — a texture asset's own `AssetId`/location resolution follows
whatever mechanism a future scene-referencing-textures Spec designs,
not solved here.
