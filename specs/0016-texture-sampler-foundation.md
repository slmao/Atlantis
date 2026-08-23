# Spec: Texture & Sampler Foundation

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Not yet reviewed/approved — see Human Review
  Decision Table below; this document is submitted for that review.
- **Created:** 2026-08-23
- **Revised 2026-08-23** — a centralized, evidence-driven final review
  (still on this Spec's own first PR, no new PR) found and fixed two
  real, previously-glossed-over gaps before Human Review: (1) the
  original draft's "submit and wait" language for the one-time texture
  upload never checked whether `Device::submit()` actually requires a
  real `RenderTarget` — it does, unconditionally, and there is no
  target-independent submit path; the corrected design reuses the
  fixture's own already-acquired `OffscreenTarget`-vended `RenderTarget`
  for the upload's own `submit()` call (no new `Device`/`Renderer`
  public API), with `Device::waitIdle()` (an already-existing, already-
  used blocking completion API) as the real synchronization mechanism,
  not a guess. (2) "Hand-authored UV in the fixture" is not actually
  achievable with zero shared/public type changes, as first drafted —
  `VertexAttributeFormat` (RHI) has no `Float2` variant anywhere in the
  pipeline; a real UV0 vertex attribute requires adding one, a small,
  disclosed, mechanical RHI/Shader-System change fully decoupled from
  Asset System's own mesh cook/artifact/load pipeline (which remains
  untouched). See the Human Review Decision Table (items 6, 11, and new
  items 17–19) and Proposed Design below for the corrected, code-proven
  designs.
- **Revised again, 2026-08-24** — a final, targeted revision (still the
  same PR) resolved two further points before this Spec proceeds to
  formal Human Review, without broadening the review further: (1) the
  immediately-prior revision's own upload design ran as its own,
  separate `submit()`/`waitIdle()` cycle before a later, separate draw
  submission — corrected so the upload, the real draw, and a readback
  all share **exactly one** combined `Device::submit()` call against a
  `RenderTarget` that genuinely participates (never a dummy token used
  only to satisfy the parameter), matching
  `headless_rendering_gpu_tests.cpp`'s own closest existing precedent as
  closely as possible; `ShaderRead` correctness is now stated
  precisely as coming from the upload's own barrier and recorded
  execution order *within* that one submission, never from
  `waitIdle()`, whose own role is narrowed to its real, purely CPU-side
  purpose. Target-independent submission is named, explicit future
  work, not solved with a workaround. (2) `Rgba8Srgb`'s own hardware
  linearization-on-sample is real GPU behavior, not tonemapping, and
  cannot be proven by a CPU-only artifact round-trip test as the
  immediately-prior revision had it — the fixture now cooks the same
  source image twice (`Rgba8Unorm` and `Rgba8Srgb`) and samples both as
  two quads in one golden, proving both real GPU sampling paths. See
  Human Review Decision items 3, 6, 13, and 14 for the corrected
  designs; this Spec is submitted for formal Human Review as of this
  revision.
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
object, and a `Material`/Shader binding path that lets a fragment shader
sample bound textures — proved end to end by a new, independent
headless fixture and its own first image-regression golden. **The
upload, the fixture's real draw, and its own readback all share exactly
one `Device::submit()` call** against a `RenderTarget` that genuinely
participates (never a dummy token) — `ShaderRead` correctness comes
from the upload's own barrier and recorded execution order within that
one submission, never from `Device::waitIdle()`, whose own role is
narrowly CPU-side (safe to read back, safe to destroy staging/readback
buffers); no new `Device`/`Renderer` public API results. **The fixture
samples both `Rgba8Unorm` and `Rgba8Srgb` textures, cooked from the same
source bytes, in the same golden** — proving Vulkan's own real hardware
sRGB linearization-on-sample, not merely a CPU-side artifact round-trip.
Every layer this closed loop touches — RHI, Vulkan Backend, RenderGraph,
Renderer's `Material`, Shader System's reflection/contract surface and
its Tools compiler, and Asset System's cooker/loader — currently has an
explicit, load-bearing gap this Spec's own pre-draft code verification,
and two subsequent review rounds' own deeper verification, confirmed
(cited throughout Requirements and the Human Review Decision Table
below); none of it is invented speculatively or worked around silently.
The fixture's own UV0 vertex attribute is real — carried through
`Mesh`/`VertexInputLayout` exactly like `minimal_cube_fixture`'s own
hand-authored position/color data — enabled by one small, disclosed
RHI/Shader-System addition (`VertexAttributeFormat::Float2`, item 11)
that is fully decoupled from, and does not touch, Asset System's own
mesh cook/artifact/load pipeline (`StaticMeshAssetData`, the mesh
authoring grammar, the mesh runtime artifact all remain untouched) — so
this Spec still does **not** claim a real asset-sourced textured mesh
exists; "Mesh UV Attribute Foundation" (real UV0 inside Asset System's
own mesh pipeline) remains a named, immediate follow-up blocker for that
claim.

## Motivation / Problem Statement

Every rendering-capable Spec approved so far explicitly deferred this
exact gap rather than silently ignoring it:

- [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
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
  it.", `AGENTS.md:142-143`), as a genuine RenderGraph execution, never
  a raw `CommandList` sequence issued outside it — sharing exactly one
  combined `Device::submit()` call with the real draw and its own
  readback, against a `RenderTarget` that genuinely participates, never
  a dummy token (item 6 — see Requirements for why this is a real,
  previously-unaddressed RenderGraph scope gap, not a stylistic choice).
- A `Material`/Shader binding path letting a fragment shader declare and
  sample bound `SampledTexture` + `Sampler` pairs (items 7, 8).
- A build-time texture cooker producing a versioned, little-endian,
  deterministic runtime artifact, following `Atlantis::AssetSystem`'s
  existing mesh/scene cook/artifact/metadata/loader conventions exactly
  (item 9); a Runtime loader that reads only cooked pixel bytes, never
  parsing PNG/JPEG itself (item 10).
- Proof: a new, independent headless fixture rendering two
  texture-sampled quads — one `Rgba8Unorm`, one `Rgba8Srgb`, cooked from
  the same source bytes — proving both real GPU sampling paths, not
  merely asserting `Rgba8Srgb` support (item 3), matched against its own
  new, first image-regression golden with zero channel difference on
  repeat runs — the existing `minimal_cube` and `world_scene` goldens
  re-run unmodified as regression evidence (item 14).
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
  samples the texture) for it. The upload's own graph — one pass,
  declaring only the destination `SampledTexture` as a RenderGraph
  resource (`incomingState = Undefined`, `finalState = ShaderRead`,
  reusing `ResourceBinding`'s existing `incomingState`/`finalState`
  fields, `execution.h:23-40`, exactly the way Spec 0010's readback
  `finalState` already works); the source staging `Buffer` is **not**
  itself a RenderGraph-tracked resource, matching the existing
  `copyRenderTargetToBuffer()` readback pass's own precedent (its own
  destination `Buffer` is likewise untracked — only the source
  `RenderTarget` is declared) — is built via a `RenderGraphBuilder`,
  compiled, and `execute()`'d into a `CommandList`.
- **The upload, the real draw, and the readback all share exactly one
  `Device::submit()` call — a review round's own further verification
  corrected an earlier draft that ran the upload as its own, separate
  submission.** The fixture's own real base verification path,
  concretely: (1) create one real `OffscreenTarget`, `acquireTarget()`
  once for one real `RenderTarget`; (2) create **one** `CommandList`;
  (3) record the upload graph above into it — `Undefined →
  TransferDestination`, `copyBufferToTexture()`, `TransferDestination →
  ShaderRead`; (4) record `Renderer::drawFrame()`'s own draw graph into
  the **same** `CommandList` — its pass callback samples the
  `SampledTexture` (now `ShaderRead`, via the new `Material`/
  `bindTexture()` binding) while rendering into the **same**
  `RenderTarget` acquired in step 1, leaving it in `ResourceState::TransferSource`
  (its own existing `finalColorState` parameter); (5) record a third,
  caller-built `RenderGraphBuilder` readback graph into the **same**
  `CommandList`, `writes(target, ResourceState::TransferSource)` matching
  what the draw graph just left it in, its own pass callback calling the
  existing `copyRenderTargetToBuffer(*target, *readbackBuffer)` — this
  three-graphs-in-one-`CommandList` shape directly matches
  `headless_rendering_gpu_tests.cpp`'s own already-established precedent
  (a draw pass and a copy pass, two independent `RenderGraphBuilder`
  graphs, recorded into one `CommandList`), extended by one more graph
  at the front; (6) call **`Device::submit(std::move(commandList),
  *target)` exactly once** for all three graphs together — `target` is
  the **real** `RenderTarget` the draw graph actually rendered into and
  the readback graph actually reads from, genuinely participating in
  this one submission, never a dummy token reused only to satisfy
  `submit()`'s own required parameter; (7) call `Device::waitIdle()`
  once.
- **`ShaderRead` correctness comes from the barrier and the recorded
  execution order within that one submission — never from
  `waitIdle()`.** The upload graph's own `execute()` call records a real
  `vkCmdPipelineBarrier` transitioning `SampledTexture` to
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` **before** the draw graph's
  own recorded sampling commands, both inside the same `CommandList`;
  Vulkan's own single-command-buffer, single-queue execution-order
  guarantee (together with the barrier's own pipeline-stage
  synchronization) is what makes the draw pass's sampling valid — this
  is true, and already fully determined, the instant the GPU begins
  executing this one submission, independent of when (or whether from
  the CPU's perspective) `waitIdle()` is later called. `Device::waitIdle()`'s
  own role is narrower and purely CPU-side: it is the signal that it is
  now safe (a) for the CPU to read the readback `Buffer`'s
  `mappedData()`, and (b) to destroy the staging `Buffer` (its upload
  already consumed) and the readback `Buffer` (its data already read) —
  it plays no role in making the GPU-side sampling itself correct (item
  6, 13; [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
  **`SubmissionSignal`** (`submission_signal.h:21-24`) remains opaque —
  no public method beyond its destructor, never inspected or waited on
  directly by a caller; `Device::waitIdle()` (`device.h:59`,
  `Result<std::monostate, SubmitError>`) remains the real,
  already-existing, already-used blocking mechanism for the CPU-side
  purposes above, matching
  `tests/vulkan_backend/headless_rendering_gpu_tests.cpp:405`,
  `minimal_cube_fixture.cpp`'s own `renderOneFrame()` (`:279`), and
  Runtime's own shutdown path (`runtime_application.cpp:455`).
- **Target-independent submission (a `Device::submit()` overload needing
  no real `RenderTarget` at all) is explicitly named future work, not
  solved or worked around here.** `Device::submit()` has exactly one
  overload (`device.h:51-52`), requiring a real, module-produced
  `const RenderTarget&` (`vulkan_device.cpp:530-531`); this Spec's own
  base verification path above always has one (the fixture's own real
  draw target) and reuses it genuinely, never via a throwaway/dummy
  target constructed only to satisfy the parameter. A future Runtime
  need for an upload genuinely decoupled from any per-frame `RenderTarget`
  (e.g. loading a texture with no frame currently in flight) would need
  a real, disclosed RHI change this Spec does not attempt to anticipate
  or scaffold for (item 6).
- **Staging and readback `Buffer` lifetime, and every other resource's
  destruction ordering, are tied to real, observed GPU completion of the
  one combined submission — never a guess, and never `submit()`
  returning.** Both the staging `Buffer` (upload source) and the
  readback `Buffer` (draw/readback destination) are destroyed only
  **after** the one `waitIdle()` call above returns `Ok` — submission is
  asynchronous; only `waitIdle()`'s own return is evidence the GPU has
  actually finished all recorded work, including reading the staging
  buffer and writing the readback buffer. `SampledTexture`/`Sampler` are
  owned by the same composition root that creates them (this Spec's own
  fixture); `Material` only **borrows** them (see next bullet) — the
  caller-owning composition root must not destroy `SampledTexture`/
  `Sampler` before every `Material` binding them is done being used in a
  `Renderer::drawFrame()` call, matching `DrawItem`'s own existing
  "mesh/material are borrowed, must outlive the `drawFrame()` call"
  contract (`draw_item.h:10-12`) extended, not re-invented (item 13;
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
- **Upload/submit/device-loss error semantics reuse existing `Result`
  channels — no new unified error enum.** Resource creation
  (`SampledTexture`, `Sampler`, the staging `Buffer`, the readback
  `Buffer`) reports through their own existing `*CreateError`-style
  enums (matching `TextureCreateError`/`BufferCreateError`'s own
  established shape); every `RenderGraphBuilder::compile()`/`execute()`
  call (upload, draw, readback) reports through RenderGraph's own
  existing compile/execute error channel, unchanged; the one
  `submit()`/`waitIdle()` pair reports through the existing
  `SubmitError` enum, whose already-`Accepted` `DeviceLost` enumerator
  covers a device loss during this combined submission exactly as it
  already covers one during any other `submit()`/`waitIdle()` call —
  this Spec adds no new error type for the upload/draw/readback path;
  the composition root propagates each stage's own `Result::Err` in
  sequence, matching `RuntimeApplication::initializeSteps()`'s own
  established multi-step composition-root error-propagation shape (item
  13; [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)).
- **`Material` gains a second, fixed descriptor binding.** Today,
  `Material` (`src/renderer/include/atlantis/renderer/material.h:18-32`)
  is a thin wrapper owning only a `Pipeline`; the entire binding surface
  lives on `CommandList`/`VulkanDevice`, hardcoded to exactly one
  binding — `{set 0, binding 0, UniformBuffer, Vertex stage}`
  (`vulkan_device.cpp:808-861`, one `VkDescriptorPoolSize` entry,
  `vulkan_device.cpp:1245-1254`) — confirmed generic enough elsewhere
  (`createMesh()`, `toVertexInputLayout()`, `createPipeline()`'s own
  attribute loop) that only this one hardcoded descriptor-set-layout/
  pool site is the actual blocker, not `Material`/`Mesh`/`Pipeline`
  construction generally. `Material` gains an optional, construction-time
  `SampledTexture`/`Sampler` pair, **borrowed, not owned** — matching
  `DrawItem`'s own existing non-owning-reference contract for
  `Mesh`/`Material` themselves (`draw_item.h:10-12`: *"mesh/material are
  borrowed (must outlive the `Renderer::drawFrame()` call they are
  passed to)"*). This Spec extends that identical contract to
  `Material`'s own new fields: the caller that constructs a `Material`
  with a `SampledTexture`/`Sampler` pair must keep both alive for at
  least as long as that `Material` is used in any `drawFrame()` call —
  `Material` neither takes ownership nor extends either object's
  lifetime, and destroying `SampledTexture`/`Sampler` while a `Material`
  still referencing them is later used is a caller precondition
  violation, not a checked error, matching this codebase's own existing
  borrowed-reference discipline exactly. A new
  `CommandList::bindTexture(SampledTexture&, Sampler&)` call, used inside
  `Renderer`'s existing per-`DrawItem` pass-callback loop
  (`src/renderer/src/renderer.cpp:26-31`) immediately alongside the
  existing `bindUniformBuffer()` call. Vulkan Backend's per-`Pipeline`
  descriptor-set-layout creation and the device-level descriptor pool
  both gain one new, second, fixed entry: `binding 1,
  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, fragment stage` (item 7, 18;
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
  own combined shape) value, plus a matching case in
  `reflection_loader.cpp`'s `parseDescriptorType()` (today recognizes
  only the string `"uniformBuffer"`), reflected from a real Slang
  `[[vk::binding(1,0)]]` combined-sampler declaration, and a second,
  new expected-descriptor-contract shape alongside
  `minimalRendererExpectedDescriptorContract()`'s existing one-binding
  contract — this Spec does not remove or narrow the existing contract,
  it adds a sibling for shaders that declare both bindings.
- **The shader-compiler Tool's contract-selection wiring is dead code
  today and must be made real — a genuine gap this Spec's own new
  shader needs closed, independent of the UV decision.** A review
  round's own deeper verification found
  `CompileAndValidateRequest::expectedContract`
  (`compile_and_validate.h:21`, populated from CMake's
  `EXPECTED_CONTRACT`/`--expected-contract=` argument,
  `shader_system/CMakeLists.txt:78`, `tools/shader_compiler/main.cpp:51-52`)
  is parsed but **never read** anywhere in `compile_and_validate.cpp` —
  `validateDescriptorContractForStage()`
  (`compile_and_validate.cpp:129-142`) unconditionally calls
  `minimalRendererExpectedDescriptorContract()` regardless of what
  `expectedContract` was given, for every shader compiled through
  `atlantis_add_slang_shader_pair()`, by name or path. Without a fix,
  this Spec's own new fragment shader (declaring a second, sampler
  binding) would fail build-time validation as
  `ContractMismatchError::UnexpectedExtraBinding` against the *wrong*,
  fixed, one-binding contract — regardless of the UV decision, since
  this failure is triggered by the sampler binding alone.
  `compileAndValidate()`'s call to `validateDescriptorContractForStage()`
  is changed to consult the caller-supplied `expectedContract` (already
  fully plumbed from CMake through to this point, simply never
  consulted) instead of unconditionally calling
  `minimalRendererExpectedDescriptorContract()` — a small, mechanical
  fix (using an already-declared field) rather than a new mechanism
  (item 17).
- **RHI gains one new vertex-attribute format, `VertexAttributeFormat::Float2`
  — small, disclosed, and fully decoupled from Asset System's own mesh
  pipeline.** A review round's own deeper verification found a
  hand-authored UV coordinate is **not** freely addable with zero
  shared-type changes, as first drafted: `VertexAttributeFormat`
  (`rhi/types.h:81-83`) has exactly one value, `Float3`, and every
  bridge that consumes it — `vertexAttributeFormatToVkFormat()`
  (`vulkan_device.cpp:624-631`, Vulkan Backend), Shader System's own
  mirror enum `VertexAttributeType` (`reflection_metadata.h:16-18`), and
  `toRhiFormat()` (`vertex_input_mapping.cpp:10-16`) — is an exhaustive
  switch over `Float3` alone, ending in a hard `ATLANTIS_CHECK_MSG`
  failure for anything else. This is genuinely different from, and far
  smaller than, adding UV to Asset System's own mesh pipeline (item 11
  below): `Mesh`/`createMesh()` (`mesh.h:47-52`) is already fully
  generic — raw bytes + stride, no hardcoded field layout
  (`mesh.cpp:11-41`) — and `toVertexInputLayout()`/`createPipeline()`'s
  own attribute loop (`vulkan_device.cpp:877-898`) already accept an
  arbitrary attribute count/layout from any caller; the *only* real
  blocker to a genuine 2-float UV attribute, for any fixture, is this one
  missing enum value and its Vulkan-format mapping. This Spec adds
  `VertexAttributeFormat::Float2` (RHI) and `VertexAttributeType::Float2`
  (Shader System reflection, plus a matching `toRhiFormat()` case) — a
  small, mechanical extension `types.h`'s own existing comment already
  anticipates verbatim ("a future spec adding a second attribute type
  (e.g. `Float2` for UVs) extends this enum") — without touching
  `StaticMeshAssetData`, the mesh authoring grammar, or the mesh runtime
  artifact in any way (item 11, 18).
- **Texture cooker (`Atlantis::AssetSystem`)**: `cookTexture(sourceImagePath,
  logicalPathInput, colorSpace, artifactOutputPath, metadataOutputPath) ->
  Result<monostate, TextureCookError>` (exact name a Plan-level detail),
  following `cookStaticMesh()`/`cookScene()`'s own established pattern
  exactly (`cook.cpp:82-125`): normalize logical path → decode the
  authoring image via `stb_image` (promoted from test-only to Tools use,
  see below) → validate → `computeAssetId()` → encode a versioned,
  unconditionally little-endian artifact plus a text metadata sidecar →
  atomic dual-file write (`writeBytesAtomically()`/`writeTextAtomically()`,
  the same temp-file-then-rename pattern every existing cooker uses).
  Exposed via a new mode of the existing `atlantis_asset_cooker` Tools
  executable, dispatched by `AssetKind`, matching
  `cook_command.cpp:208-217`'s own existing dispatch shape (item 9;
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)).
- **`colorSpace` is a mandatory, explicit cooker parameter — PNG bytes
  never decide it themselves.** `stb_image` decodes to raw RGBA8 bytes
  only; it applies no gamma/color-space interpretation of its own — the
  cooker does not read, and explicitly ignores, any source PNG's
  `gAMA`/`sRGB`/`iCCP`/`cHRM` chunk (matching ADR-0041's own existing
  "No PNG color-profile metadata is written or interpreted... comparison
  operates on raw pixel bytes only, with no color-profile interpretation
  on either side" discipline, extended here from goldens to authored
  textures). `cookTexture()`'s caller-supplied `colorSpace` parameter
  (`Rgba8Unorm` or `Rgba8Srgb`) is written into the artifact's own
  `SampledTextureFormat` header field verbatim — the cooker performs no
  pixel-value transformation of its own between the two; the *meaning*
  of the stored bytes is entirely determined by which `VkFormat`
  (`VK_FORMAT_R8G8B8A8_UNORM` vs. `VK_FORMAT_R8G8B8A8_SRGB`) `Device::createSampledTexture()`
  later creates the GPU image with. **Sampling behavior, stated
  explicitly, not left implicit**: for an `Rgba8Srgb` `SampledTexture`,
  Vulkan's own fixed-function texture unit linearizes each sampled texel
  (applies the sRGB EOTF) before the fragment shader ever sees it — the
  shader receives already-linear float values, not the raw stored bytes;
  for `Rgba8Unorm`, the shader receives the raw stored bytes reinterpreted
  as `[0,1]` floats, unmodified. **This is real GPU hardware behavior, not
  a form of tonemapping — a review round's own further verification
  corrected an earlier draft that tried to exercise `Rgba8Srgb` with only
  a CPU-side cook/decode round-trip unit test, which proves the artifact
  correctly stores/retrieves a format tag and byte, never that Vulkan's
  own hardware actually linearizes on sample.** This Spec's own one
  GPU-required fixture therefore samples **both** formats from the same
  source bytes in the same golden (see the fixture bullet below,
  item 14) — proving both real GPU paths, not merely asserting one is
  supported while only testing the other. **Golden comparison happens on
  the final, rendered RGBA8 color-attachment bytes** (the fragment
  shader's own output, after whatever hardware linearization the bound
  `SampledTexture`'s own format applied on sampling) — never a direct
  byte comparison against the source PNG or the cooked artifact's own
  stored texel values, which is the same "compare the real output, not
  an intermediate" discipline every existing golden in this repository
  already follows (item 3).
- **Texture artifact: explicit overflow, size-limit, row-pitch, mip, and
  channel contract.** Header fields (schema version, width, height,
  format, mip count, pixel-data offset/size) are unconditionally
  little-endian, explicit shift/mask serialized, matching
  `mesh_artifact.h`'s own discipline exactly — never a struct memcpy. A
  defensive maximum dimension (Plan-level detail, e.g. 8192×8192,
  chosen specifically so `maxDimension × maxDimension × 4` stays
  comfortably within a `uint32_t` pixel-data-size field — 8192×8192×4 =
  268,435,456 bytes, well under `UINT32_MAX`) is enforced as an explicit
  decode-time error, checked **before** any allocation sized from
  header-supplied values. `width × height × 4` is computed in 64-bit
  arithmetic first (matching `mesh_artifact.h`'s own "every offset/size
  recomputed independently in `uint64_t`" defense-in-depth pattern), so
  a corrupted or adversarial header cannot wrap a 32-bit multiplication
  into a small, falsely-valid size before the maximum-dimension check
  runs. **Row order**: the artifact's first row is the authoring image's
  own first-decoded row (`stb_image`'s un-flipped, top-to-bottom
  default) — matching ADR-0041's own existing "no vertical flip, ever"
  convention exactly, extended from goldens to texture artifacts. **Row
  pitch**: tightly packed, `width × 4` bytes per row, no padding —
  matching `VkBufferImageCopy::bufferRowLength = 0`'s own existing
  convention (ADR-0040, already used by `copyRenderTargetToBuffer()`),
  reused unchanged by this Spec's own `copyBufferToTexture()`. **Mip**:
  the header's own mip-count field is validated as exactly `1` at decode
  time — a foreign or future artifact claiming any other value is a
  distinct, named decode error, never silently truncated to one mip.
  **Channel**: the cooker always decodes with `desired_channels = 4`
  (matching ADR-0041's own established `stb_image` convention exactly)
  — unlike a golden (which must be a literal RGBA capture of an internal
  buffer, so ADR-0041 hard-requires `channels_in_file == 4`), an authored
  texture may legitimately be a real-world RGB-only (no alpha) or
  grayscale source image; the cooker force-expands via `stb_image`'s own
  `desired_channels = 4` (opaque alpha filled in for a source with none)
  and records the source's own real `channels_in_file` in the metadata
  sidecar for provenance/debugging — this is **not** a validation
  failure for texture cooking, a deliberate, disclosed difference from
  the golden-validation model. **Corrupted input**: an unreadable/
  malformed source image at cook time is a distinct `TextureCookError`
  enumerator (e.g. `SourceImageDecodeFailed`); a corrupted/truncated
  artifact at decode time (bad magic, unknown schema version, a
  pixel-data size inconsistent with declared width/height, a dimension
  exceeding the defensive maximum, truncated pixel data) is each a
  distinct `TextureArtifactDecodeError`/`TextureLoadError` enumerator,
  mirroring `mesh_artifact.h`'s own already-shipped defense-in-depth
  decode discipline exactly (item 9;
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)).
- **Texture loader (`Atlantis::AssetSystem`)**: `loadTextureAsset(artifactPath,
  metadataPath) -> Result<TextureAssetData, TextureLoadError>` (exact
  name a Plan-level detail), mirroring `loadStaticMeshAsset()`'s own
  cross-validation discipline (`load.cpp:40-83`): independently
  re-validates every cook-time condition against the artifact's actual
  bytes (magic, schema version, declared byte counts self-consistent,
  pixel-data size exactly `width * height * 4`, per the overflow/size-limit
  contract above), cross-checks the metadata sidecar against the
  artifact, and returns a pure CPU-side `TextureAssetData` (width,
  height, `SampledTextureFormat`, owned pixel bytes) naming no RHI type —
  a composition root elsewhere passes the result into
  `Device::createSampledTexture()` plus this Spec's own upload path,
  exactly the way `loadStaticMeshAsset()`'s own result today passes into
  `renderer::createMesh()` (item 9, 10).
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
  is additionally linked `PRIVATE` into `atlantis_asset_cooker_lib`
  (Tools) only — never into `Atlantis::AssetSystem`'s own runtime
  library, never into `src/renderer`, `src/runtime`, or any target a
  shipped executable links. ADR-0041's own license/offline-build
  disclosures (`adr/0041-...md:161-176`) are re-confirmed, unchanged, as
  applying equally to this new linkage. **ADR-0041 now carries its own
  "Proposed Amendment — 2026-08-23" section recording this widening**
  (ADR-0041's own original `Accepted` Decision/Consequences/Alternatives
  Considered are left completely unmodified above it) — pending the same
  Human Review pass as this Spec itself, not yet accepted; see
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  and this Spec's own Human Review Decision Table item 10 (item 10).
- **New, independent headless textured fixture and its own first
  golden — two textured quads, one `Rgba8Unorm`, one `Rgba8Srgb`,
  proving both real GPU sampling paths in one golden.** A review round's
  own further verification found that `Rgba8Srgb`'s own hardware
  linearization-on-sample is a real Vulkan/GPU behavior a CPU-only
  artifact round-trip test cannot prove, and is not a form of
  tonemapping this Spec's own Non-Goals exclude — it is exactly the
  sampling behavior this Spec's own color-space contract (item 3)
  claims to support, so it needs the same class of evidence every other
  GPU-facing claim in this Spec gets. A new fixture (e.g.
  `textured_quad_fixture`, exact name a Plan-level detail) hand-authors
  its own `struct Vertex { float position[3]; float uv[2]; };` array
  (two quads, side by side, matching `minimal_cube_fixture.cpp`'s own
  already-established hand-authored, non-Asset-System-sourced
  construction shape exactly) — its own new fragment shader (a new
  `.slang` file, its own `MeshVertexAttributeSchema`, its own reflection
  JSON, compiled via the existing, unmodified
  `atlantis_add_slang_shader_pair()`) declares a real
  `[[vk::location(2)]] float2 uv` vertex input, carried through
  `Mesh`/`createMesh()`/`toVertexInputLayout()` exactly like the
  existing shader-reflection-driven path every current fixture already
  uses (item 11). **Two `Material`s, two `SampledTexture`s, one
  `Sampler`** (reused across both, per its own independence, item 8):
  one quad's `Material` binds a `SampledTexture` cooked with
  `colorSpace = Rgba8Unorm`, the other's binds the same source image
  cooked a second time with `colorSpace = Rgba8Srgb` — both textures'
  own stored bytes identical, so any visible difference between the two
  quads in the captured golden is exactly, and only, Vulkan's own real
  hardware sRGB decode, not a difference in what was uploaded. Both
  `DrawItem`s render in the same draw graph, part of the same combined
  submission (item 6). A new golden,
  `tests/image_regression/goldens/textured_quad/`, is captured following
  ADR-0042's own "Initial baseline bootstrap" category, satisfying all
  six of its own numbered constraints explicitly, not merely in spirit:
  (1) applicability — no prior golden exists at this path; (2) captured
  against a clean, already-committed working tree; (3) full provenance
  recorded in the sidecar, same fields as every other golden; (4) the
  golden PNG/sidecar added via their own separate, subsequent commit;
  (5) evidence substituting for the inapplicable old-vs-new diff — a
  human's direct visual confirmation of a correctly-rendered,
  non-degenerate frame showing both quads, with the sRGB-sampled quad
  visibly, correctly different from the linear one (not merely
  "non-black"), the capture-compare cycle reproducing zero channel
  difference against itself immediately after capture, a real run on
  real Vulkan-capable hardware with Validation Layers clean, and
  citation of this repository's own already-recorded empirical
  calibration evidence for the channel-tolerance-0 rule; (6) no
  relaxation of the golden validity check, comparison algorithm, or
  sidecar encoding contract for being a bootstrap golden. **The existing
  `minimal_cube` and `world_scene` goldens are not modified, and their
  own existing tests are re-run unmodified** as regression proof this
  Spec's changes did not disturb either existing rendering path (item
  14).

### Non-functional

- **Performance:** cooking and loading a single small test texture (a
  few kilobytes to low hundreds of kilobytes of pixel data) is not a hot
  path — no performance budget beyond "does not noticeably delay the new
  fixture's own startup," matching every prior Spec's own similarly
  unbudgeted one-time-load cost.
- **Memory:** the staging `Buffer` is destroyed only after the upload's
  own `Device::waitIdle()` call returns `Ok` (RAII scope exit) — real,
  observed GPU completion, never "immediately after `submit()` returns"
  or any other guess; never retained past that point. `SampledTexture`/
  `Sampler` are explicitly, single-owner-held by whichever composition
  root creates them (this Spec's own fixture), matching
  `resource_lifetime.md`'s existing "explicit ownership, no hidden
  caching" principle exactly — no resource pool, no texture cache, this
  round.
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
  authoring image (.png) ─▶ cookTexture() [AssetSystem, stb_image-backed, Tools-only], called TWICE
                             (colorSpace=Rgba8Unorm, colorSpace=Rgba8Srgb, same source bytes)
                             ─▶ two texture artifacts (.atex) + metadata sidecars

Load time (fixture startup):
  each artifact + sidecar ─▶ loadTextureAsset() [AssetSystem] ─▶ TextureAssetData (CPU pixel bytes, no RHI type)
                                                                          │
                              Device::createSampledTexture() x2 + createSampler() x1 (shared)
                                                                          │
                    two staging Buffers (BufferPurpose::Staging), each populated with its own pixel bytes
                                                                          │
                  fixture's own OffscreenTarget::acquireTarget() ─▶ RenderTarget (genuinely drawn into
                                                                     and read back from below -- never a
                                                                     dummy token)
                                                                          │
                          fixture creates ONE CommandList; records, IN ORDER, into it:
  ┌───────────────────────────────────────────────────────────────────────────────────────────────────┐
  │ 1. upload RenderGraph execution x2 (one per SampledTexture) [RenderGraph -- AGENTS.md's own         │
  │    "Render Graph is the mandatory path for GPU work" constraint, never a raw CommandList sequence   │
  │    outside it -- each: only its own SampledTexture tracked (incomingState=Undefined,                │
  │    finalState=ShaderRead); its own staging Buffer untracked, matching                               │
  │    copyRenderTargetToBuffer()'s own untracked-destination precedent]:                                │
  │      pass body: copyBufferToTexture(staging Buffer, SampledTexture)                                  │
  │      RenderGraph-driven barrier: transitionResource(SampledTexture, Undefined -> TransferDestination │
  │                                                       -> ShaderRead)                                  │
  │                                                                                                       │
  │ 2. Renderer::drawFrame()'s own draw RenderGraph execution (existing, unmodified shape), rendering    │
  │    into the SAME RenderTarget acquired above -- two DrawItems, each Material(pipeline,               │
  │    SampledTexture&, Sampler&) [borrowed, not owned]:                                                 │
  │      bindPipeline / bindVertexBuffer / bindUniformBuffer / bindTexture(SampledTexture, Sampler)      │
  │      fragment shader samples -- hardware linearizes for the Rgba8Srgb quad, not for Rgba8Unorm --    │
  │      writes color; leaves RenderTarget in ResourceState::TransferSource (finalColorState)            │
  │                                                                                                       │
  │ 3. readback RenderGraph execution (caller-built, matching headless_rendering_gpu_tests.cpp's own     │
  │    established two-graphs-in-one-CommandList precedent), writes(target, incomingState=TransferSource)│
  │    matching what step 2 just left it in:                                                             │
  │      pass body: copyRenderTargetToBuffer(*target, *readbackBuffer)                                   │
  └───────────────────────────────────────────────────────────────────────────────────────────────────┘
                                                                          │
                    Device::submit(commandList, *target)  -- ONE call, for all three graphs above;
                    ShaderRead correctness already guaranteed by step 1's own barrier + recorded
                    execution order within THIS submission, not by anything below
                                                                          │
                    Device::waitIdle()  -- purely CPU-side: only now safe to read readbackBuffer's
                    mappedData(), and to destroy both staging Buffers + the readback Buffer
                                                                          │
                     golden comparison against the FINAL rendered RGBA8 bytes (both quads visible)
```

Module ownership: RHI (`SampledTexture`, `Sampler`, new `ResourceState`/
`BufferPurpose` values, new `CommandList` overloads — `Device::submit()`/
`waitIdle()`/`SubmissionSignal` themselves unchanged) and Vulkan Backend
(implementation: manual per-resource `vkAllocateMemory` — no VMA,
matching the existing, `Accepted`-deferred (ADR-0015) pattern every other
RHI resource already uses; barrier-plan table entries; descriptor pool/
layout extension) per
[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md);
RenderGraph (new `ResourceBinding` kind, transition-insertion logic) and
Renderer's `Material`/`CommandList::bindTexture()` per
[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md);
Asset System (`cookTexture()`/`loadTextureAsset()`, `Stb::Stb`'s widened
Tools-only linkage), Shader System (`DescriptorType::Sampler`,
`VertexAttributeType::Float2`), and Tools' shader compiler (wiring
`expectedContract`, today dead code, into real use) per
[ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).
RHI additionally gains `VertexAttributeFormat::Float2` — a small,
disclosed extension covered in this Spec's own Requirements/Human Review
Decision Table (item 18) rather than a fourth ADR, since it is a
single, mechanical enum-value addition `types.h`'s own existing comment
already anticipated, not a new module boundary, ownership model, or
public-API shape of the weight this repository's ADRs otherwise record.

## Architectural Impact

This Spec introduces or changes, in every case because this Spec's own
pre-draft verification against real, current source confirmed the
existing public API cannot form a reasonable closed loop without it —
none of the following is hidden or silently worked around:

- A **new RHI subsystem boundary**: `SampledTexture`/`Sampler` as
  independent types, new `ResourceState`/`BufferPurpose` values, two new
  `CommandList` capabilities (buffer→texture copy, a third
  `transitionResource()` overload), and a new
  `VertexAttributeFormat::Float2` value (item 11, 18) —
  [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md).
  **`Device::submit()`, `Device::waitIdle()`, and `SubmissionSignal`
  are all unchanged** — a second review round's own further
  verification confirmed the upload, the real draw, and the readback all
  share **one** `submit()` call against the fixture's own already-
  acquired, genuinely-drawn-into `OffscreenTarget`-vended `RenderTarget`
  (never a dummy token), with `waitIdle()` playing a narrowly CPU-side
  role — safe-to-read/safe-to-destroy, not what makes `ShaderRead`
  sampling correct, which the upload's own barrier and recorded
  execution order already guarantee within that one submission (item 6,
  13); this Spec does not introduce a target-optional or target-free
  submit path, registering that as named future work instead.
- A **new RenderGraph resource-binding kind and transition
  responsibility** — `ResourceBinding` gains a third field (tracking
  only the destination `SampledTexture`; the source staging `Buffer`
  remains untracked, matching `copyRenderTargetToBuffer()`'s own
  untracked-destination precedent), `execute()` gains new transition
  logic, and the upload's own graph is recorded, together with the real
  draw graph and a readback graph, into one `CommandList` covered by one
  combined submission — not a separate, earlier submission of its own,
  a design this Spec's own second review round withdrew in favor of the
  combined shape. This is the one finding this Spec's own suggested core
  scope (as directed) did not explicitly anticipate — the CPU→GPU upload
  cannot be a raw `CommandList` sequence outside RenderGraph, per
  AGENTS.md's own Golden Rule; it must be real, if minimal, RenderGraph
  scope —
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).
- A **new `Material`/descriptor-binding public API**: a second, fixed,
  **borrowed** (not owned — extending `DrawItem`'s own existing
  `Mesh`/`Material` borrowing contract) descriptor slot (combined image
  sampler, fragment stage), extending both RHI's `CommandList` surface
  and Vulkan Backend's previously hardcoded single-binding
  descriptor-set-layout/pool —
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).
- A **new Shader System reflected descriptor kind**
  (`DescriptorType::Sampler`, plus its own `reflection_loader.cpp`
  parser case) and a second expected-descriptor-contract shape,
  alongside (not replacing) the existing one —
  [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md).
- A **real, previously-dead-code Tools fix, not merely a new
  capability**: the shader-compiler tool's `expectedContract` field is
  parsed from CMake but never consulted — `compileAndValidate()` is
  changed to actually use it, a prerequisite for this Spec's own new
  shader (sampler binding) to pass build-time contract validation at
  all, independent of the UV decision —
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).
- A **new Asset System asset kind** (texture: cooker, artifact format,
  loader), following the established mesh/scene pattern exactly, with an
  explicit, mandatory `colorSpace` cook-time parameter (never inferred
  from PNG color-profile chunks) —
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).
- A **new third-party dependency boundary widening**: `stb_image`,
  already an `Accepted` (ADR-0041) test-only dependency, gains a second,
  disclosed linkage point (Tools' `atlantis_asset_cooker`) — never a new
  library, but a real, named boundary change ADR-0041 itself did not
  previously permit. **ADR-0041 now carries its own Proposed Amendment**
  (not merely a forward reference) recording this boundary widening and
  a real, previously-undiscovered CMake configure-ordering defect this
  review round found: `cmake/AtlantisDependencies.cmake` (where `stb`'s
  own `FetchContent` declaration lives) is included only inside the
  `if(ATLANTIS_BUILD_TESTS)` block, **after** `add_subdirectory(src/tools/asset_cooker)`
  already runs — `Stb::Stb` would not exist as a target at the point the
  cooker needs it, regardless of `ATLANTIS_BUILD_TESTS`'s value, without
  also relocating that declaration — see ADR-0041's own Proposed
  Amendment and this Spec's own Human Review Decision item 10 —
  [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md).

**Explicitly not changed**: `Atlantis::World`'s `Renderable` component
shape (still names exactly one mesh `AssetId`, Spec 0014/0015
unmodified); the Scene Asset artifact format (Spec 0015 unmodified);
`StaticMeshAssetData`, the mesh authoring grammar, and the mesh runtime
artifact (no UV added to Asset System's own mesh pipeline this round —
only a small, decoupled RHI/Shader-System vertex-attribute-format
addition, item 11); today's depth-only `Texture` and its single-value
`DepthFormat` enum (completely untouched, not generalized);
`Device::submit()`, `Device::waitIdle()`, and `SubmissionSignal`
(unchanged, reused as-is); `Renderer::drawFrame()`'s own public API
(the upload never goes through it — the upload is a separate,
fixture-level `RenderGraphBuilder`/`execute()` sequence, matching the
existing headless-readback test's own "second, caller-built
`RenderGraphBuilder`" precedent).

## Human Review Decision Table

| # | Question | Recommendation | Rejected Alternative(s) | Where |
|---|---|---|---|---|
| 1 | Generalize the existing `Texture` (today, depth-only) to also cover sampled color textures, or introduce a new, independent `SampledTexture` type? | New, independent `SampledTexture` type. Depth and sampled-color images differ in Vulkan usage bits, image aspect, and consumption pattern (attachment write vs. shader read) enough that forcing one C++ type to cover both would touch every existing depth-`Texture` call site for no benefit, and contradicts `texture.h`'s own explicit "depth attachment... no sampled/shader-read usage" scoping (`texture.h:7-8`). Matches the existing precedent that `RenderTarget` and depth `Texture` are already two separate types for two different roles. | Generalize `Texture` with a `Kind`/`Usage` tag spanning Depth and Sampled — rejected: forces `DepthFormat` and a new color-format concept into one type and risks every existing depth-`Texture` caller. | [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md) |
| 2 | What is the type boundary between depth and sampled-color formats? | `DepthFormat` (still one value, `D32Sfloat`) is completely untouched. A new, independent `SampledTextureFormat` enum is introduced for sampled color textures — not a shared enum with `DepthFormat`, and not a reuse of the existing swapchain-shaped `Format` enum (see item 3). | A single, unified `Format` enum spanning depth and color — rejected outright, `DepthFormat`'s own existing single-variant-enum precedent (`types.h:65-68`) exists specifically so a future depth format extends it independently of any color concept. | [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md) |
| 3 | **(Revised, second review round.)** What is the first supported set of linear/sRGB sampled-texture formats, what actually decides linear vs. sRGB, and — since `Rgba8Srgb`'s own hardware linearization is a real GPU behavior, not tonemapping — how is it actually proven to work, not merely asserted supported? | A new, independent `SampledTextureFormat` enum, not the existing `Format` (whose own doc comment already anticipates a future format concept "quite possibly superseding" it, `types.h:22-25`, but whose two BGRA variants are swapchain-specific and meaningless for an authored texture). First two values: `Rgba8Unorm` (linear) and `Rgba8Srgb`. `stb_image` applies no color-profile interpretation of its own; `cookTexture()`'s `colorSpace` parameter is a mandatory, explicit, caller-supplied choice, written into the artifact verbatim — any source PNG `gAMA`/`sRGB`/`iCCP`/`cHRM` chunk is read by neither `stb_image` nor this cooker, matching ADR-0041's own already-established "no color-profile interpretation on either side" discipline. Sampling behavior is explicit, not implicit: an `Rgba8Srgb` `SampledTexture` is hardware-linearized (sRGB EOTF) by Vulkan's own texture unit before the shader sees it; `Rgba8Unorm` is not. **Proof, real and GPU-based, not CPU-only:** this Spec's own one GPU-required fixture cooks the *same* source image twice — once `Rgba8Unorm`, once `Rgba8Srgb` — and renders both as two separate textured quads, sampled by two separate `Material`s, in the *same* golden (item 14); since the underlying stored bytes are identical, any visible or measured difference between the two quads in the captured, human-confirmed golden is exactly Vulkan's own real hardware sRGB decode, not an artifact of cooking, uploading, or comparison methodology. Golden comparison itself happens on the final rendered RGBA8 color-attachment bytes, never the source PNG or artifact texel values directly. | Reuse `Format` directly — rejected, couples an authored-texture color-space contract to swapchain-surface-format concerns that have nothing to do with it. Ship only `Rgba8Srgb` (no linear option) — rejected, the Goals explicitly require "at least" naming the linear/sRGB boundary, not just the color-authoring case. Infer color space from PNG metadata (`gAMA`/`sRGB` chunk) — rejected, `stb_image` does not read these chunks in this codebase's existing usage and adding that interpretation would be new, undisclosed decoder behavior beyond ADR-0041's own established "raw bytes only" contract. **(Withdrawn, this review round.)** Exercise `Rgba8Srgb` with only a GPU-independent cook/decode round-trip unit test, using `Rgba8Unorm` for the one GPU-required fixture — this was the immediately-prior draft's own design and is withdrawn: it proves the artifact format tag round-trips correctly, never that Vulkan's own hardware sampling actually linearizes, which is the entire point of claiming `Rgba8Srgb` support. Two separate fixtures/goldens (one per format) — considered as an alternative to one dual-format golden; rejected as this Spec's own recommendation only because one golden with two quads proves the same thing with less new-golden maintenance surface; not a correctness objection, Human Review may prefer two if a shared golden is judged to obscure per-format regressions. Ship no `Rgba8Srgb` support at all this round, narrowing to `Rgba8Unorm` only — considered; rejected as this Spec's own recommendation since the dual-quad, one-golden design proves both paths for a small, comparable cost to proving just one; remains available as a fallback if Human Review judges the combined golden too large/complex. | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 4 | What is the minimal `Sampler` filter/address/LOD API? | A single `Filter` (`Nearest`\|`Linear`, applied to min/mag together — no separate mip filter, since every texture has exactly one mip level this round) and a single `AddressMode` (`Repeat`\|`ClampToEdge`, applied to U/V together). No LOD bias, anisotropy, or compare-op. `Sampler` is independent, immutable, RAII-owned — matching `resource_lifetime.md`'s existing "explicit ownership, no hidden caching" principle. | Separate min/mag/mip filter fields — rejected, meaningless with a fixed single mip level (item 12); would be dead API surface. Exposing anisotropy/LOD bias now — rejected, no consumer or measured need exists yet; matches AGENTS.md's own "do not add abstraction knobs for a capability that isn't being built" discipline. | [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md) |
| 5 | What staging buffer, copy command, and `ResourceState` values does the CPU→GPU upload path need? | A new `BufferPurpose::Staging` (host-visible, extending the existing `BufferPurpose` enum exactly as `Readback` already did); new `ResourceState::TransferDestination`/`ShaderRead` values, with two new, explicit `(before,after)` entries added to Vulkan Backend's existing exhaustive barrier-plan table (`resource_state_mapping.cpp`) — no wildcard transition; a new `CommandList::copyBufferToTexture()` and a third `transitionResource()` overload. | A single combined "upload" `ResourceState` collapsing `TransferDestination` and `ShaderRead` — rejected, these are two genuinely distinct Vulkan image layouts (`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` vs. `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`) and collapsing them would make the barrier-plan table's own exhaustiveness check meaningless for this path. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 6 | **(Real API gap, resolved — three times over two review rounds.)** Can the one-time texture upload be a raw `CommandList` sequence issued outside RenderGraph? Does `Device::submit()` admit a target-free/target-optional `CommandList`? And — a second review round's own further verification — should the upload run as its **own**, separate submission before the real draw, or must it share one submission with the real draw/readback it feeds? | No to bypassing RenderGraph — AGENTS.md states plainly: *"Render Graph is the mandatory path for GPU work. No subsystem submits ad hoc, hand-scheduled GPU work outside it"* (`AGENTS.md:142-143`). No target-free submit path exists or is added: `Device::submit()` has exactly one overload (`device.h:51-52`, `const RenderTarget&`), and `VulkanDevice::submit()` (`vulkan_device.cpp:530-531`) unconditionally `dynamic_cast`s/`ATLANTIS_CHECK_MSG`-asserts it is real and module-produced. `ResourceBinding` (`execution.h`) gains a third, generic-`SampledTexture`-carrying field (tracking only the destination texture — the source staging `Buffer` stays untracked, matching `copyRenderTargetToBuffer()`'s own untracked-destination-buffer precedent), and `execute()`'s transition-insertion logic is extended for `TransferDestination`/`ShaderRead` via the existing `incomingState`/`finalState` mechanism. **The upload, the real draw, and the readback all share exactly one `Device::submit()` call, not three (or even two) separate ones**: one `CommandList` records, in order, the upload graph, `Renderer::drawFrame()`'s own draw graph (sampling the now-`ShaderRead` `SampledTexture` while rendering into a real, `OffscreenTarget`-vended `RenderTarget`), and a caller-built readback graph (`copyRenderTargetToBuffer()`, matching `headless_rendering_gpu_tests.cpp`'s own established two-graphs-in-one-`CommandList` shape, extended by one more graph) — then **one** `submit(commandList, *target)`, where `target` genuinely participates (drawn into, read back from), never a dummy reused only to satisfy the parameter. `ShaderRead` correctness comes from the upload graph's own real `vkCmdPipelineBarrier`, recorded before the draw graph's own sampling commands in the same `CommandList` — Vulkan's own single-command-buffer execution-order guarantee plus the barrier's own pipeline-stage synchronization, not any CPU-side wait. `Device::waitIdle()` (`device.h:59`) — not `SubmissionSignal`, opaque, `submission_signal.h:21-24` — is called once, after that one `submit()`, and its own role is narrowly CPU-side: safe to read the readback buffer, safe to destroy the staging/readback buffers. Target-independent submission (an upload with no real `RenderTarget` at all) is named, explicit future work, not solved or worked around with a dummy target here. | A raw, one-time `CommandList` sequence recorded directly against `Device`, bypassing RenderGraph — rejected, violates AGENTS.md's own non-negotiable constraint. A **new**, target-optional/target-free `Device::submit()` overload — rejected for this round: a real, disclosed RHI public-API change for a need this Spec's own scope does not have, since a real target is always available; registered as future work instead of solved with a workaround. **(Withdrawn, this review round.)** The upload as its own, separate `submit()`/`waitIdle()` cycle **before** a later, separate per-frame draw graph — this was the immediately-prior draft's own design and is withdrawn: it works, but unnecessarily doubles GPU submissions/CPU stalls for a fixture whose own base verification need is exactly one combined submission, and a second reviewer round found it doesn't match this repository's own closest existing precedent (`headless_rendering_gpu_tests.cpp`'s draw+copy-in-one-`CommandList`/one-`submit()` shape) as closely as the combined design does. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 7 | What is `Material`'s new binding API for a sampled texture + sampler, and its own ownership/lifetime semantics? | `Material` gains an optional, construction-time, **borrowed, not owned** `SampledTexture`/`Sampler` pair — explicitly extending `DrawItem`'s own existing "`mesh`/`material` are borrowed (must outlive the `drawFrame()` call)" contract (`draw_item.h:10-12`) to `Material`'s own new fields: the caller-owning composition root must keep `SampledTexture`/`Sampler` alive at least as long as any `Material` referencing them is used in a `drawFrame()` call; `Material` neither owns nor extends their lifetime, and destroying either while a live `Material` still references it is a caller precondition violation (matching this codebase's existing borrowed-reference discipline), not a checked error. A new `CommandList::bindTexture(SampledTexture&, Sampler&)`, called inside `Renderer`'s existing per-`DrawItem` pass callback alongside `bindUniformBuffer()`. Vulkan Backend's per-`Pipeline` descriptor-set-layout and the device-level descriptor pool each gain one new, fixed entry (binding 1, combined image sampler, fragment stage). | A general, per-`Material` variable-length binding list — rejected as premature, ahead of any real second consumer needing more than one texture; matches this codebase's own "one fixed, hardcoded contract, extended only when a real need appears" discipline (`minimalRendererExpectedDescriptorContract()`'s own existing precedent). `Material` taking shared/owning references (e.g. a `shared_ptr`-style handle) to `SampledTexture`/`Sampler` — rejected, introduces implicit shared ownership this codebase's own "explicit ownership, no hidden caching" principle (`resource_lifetime.md`) does not use anywhere else, for no need this Spec's own single-fixture scope has. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 8 | Combined image sampler, or separate texture/sampler descriptor types? | Combined image sampler (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`) — one binding slot, one pool-size entry, one `VkDescriptorImageInfo` pairing image view + sampler at bind time. The RHI-level `Sampler` object itself remains fully independent (own creation, own lifetime, reusable across multiple textures) — this decision is purely about the underlying Vulkan descriptor-binding mechanism, not the RHI's own C++ object model. | Separate `SampledImage`/`Sampler` descriptor types (two binding slots, two pool-size entries) — rejected as more descriptor-pool/layout complexity for no benefit this round, ahead of any real need to bind one sampler against multiple images independently. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 9 | What is the texture artifact's format, byte order, row order, row pitch, mip contract, channel-forcing behavior, overflow safety, size limit, and validation? | Magic + fixed header (schema version, width, height, `SampledTextureFormat`, mip count [= 1], pixel-data offset/size), unconditionally little-endian, explicit shift/mask serialization — matching `mesh_artifact.h`'s own discipline exactly, never a struct memcpy. Row order: the artifact's first row is the authoring image's own first-decoded row (`stb_image`'s un-flipped, top-to-bottom default), matching ADR-0041's own "no vertical flip, ever" convention exactly. Row pitch: tightly packed, `width × 4` bytes, no padding — matching `VkBufferImageCopy::bufferRowLength = 0`'s own existing convention (ADR-0040), reused unchanged by `copyBufferToTexture()`. Mip: the header's own mip-count field is checked equal to `1` at decode time, a distinct, named error otherwise. Channel: the cooker always forces `desired_channels = 4` via `stb_image` (matching ADR-0041's own established convention), recording the source's own real `channels_in_file` in the metadata sidecar for provenance — **not** a hard validation failure the way a golden's own `channels_in_file == 4` requirement is, since an authored texture may legitimately be a real RGB/grayscale source. Overflow: `width × height × 4` is computed in 64-bit arithmetic before any allocation, and a defensive maximum dimension (Plan-level detail, e.g. 8192×8192, chosen so the maximum stays comfortably within a `uint32_t` pixel-data-size field) is checked **before** that computation is trusted for sizing. Decode independently re-validates: magic, schema version, dimensions non-zero and within the maximum, a known format value, mip count exactly 1, and pixel-data byte count exactly `width × height × 4`; a malformed source image at cook time and a corrupted/truncated artifact at decode time are each distinct, named error enumerators. | Storing pixel data bottom-to-top (matching some legacy image conventions) — rejected, `stb_image`'s own natural decode order is top-to-bottom and there is no existing convention in this codebase to match instead. No explicit size cap (trust the authoring pipeline) — rejected, an unbounded value read from a corrupted/malformed header is exactly the kind of decode-time risk this repository's existing artifact formats already guard against. Computing `width × height × 4` in 32-bit arithmetic before the maximum-dimension check — rejected, a crafted header could wrap the multiplication into a small, falsely-valid size, defeating the check it's meant to gate. Requiring `channels_in_file == 4` for authored textures, matching the golden-validation model exactly — rejected, would reject legitimate real-world RGB/grayscale authoring images for no correctness benefit, conflating two different validation purposes (golden = must be a literal internal-buffer capture; texture = may be any real authored image). | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 10 | **(Real dependency-boundary gap, resolved — including a real CMake-ordering defect a later review round found.)** What authoring-image decoder does the cooker use, does promoting `stb_image` from test-only to Tools require amending ADR-0041, and does the resulting linkage actually work given today's `CMakeLists.txt` ordering? | Reuse `stb_image` (already an `Accepted`, license-reviewed, pinned-commit dependency, `Stb::Stb`) rather than writing a redundant decoder — but this genuinely widens ADR-0041's own explicit boundary statement ("never in `src/`, never linked into any shipping example or engine library," `adr/0041-...md:156-160`). `Stb::Stb` is linked `PRIVATE` into `atlantis_asset_cooker_lib` (Tools) only — never into `Atlantis::AssetSystem`'s own runtime library or any runtime-linked target. **ADR-0041 now carries its own "Proposed Amendment — 2026-08-23" section** recording this widening and its own real, previously-undiscovered fix: `cmake/AtlantisDependencies.cmake` (where `stb`'s `FetchContent` declaration lives) is included only inside `if(ATLANTIS_BUILD_TESTS)`, **after** `add_subdirectory(src/tools/asset_cooker)` already runs (`CMakeLists.txt:60,95-97`) — `Stb::Stb` would not exist as a target at the point the cooker needs it, regardless of `ATLANTIS_BUILD_TESTS`'s value, without relocating that declaration into a new, unconditionally-included module (the amendment's own Decision). Approving this Spec without also accepting that amendment leaves both a real dependency-boundary inconsistency and a non-functional build — Human Review must decide both together, in the same pass. | Write a hand-rolled PNG decoder instead — rejected, duplicates a small, permissively-licensed, already-vetted library for no real benefit, and this repository's own established restraint is "no new general parser library," not "no reuse of an already-accepted one." Ship raw, undecoded PNG bytes as the "artifact" and decode at Runtime load time — rejected outright, directly violates this Spec's own explicit "Runtime never parses PNG/JPEG" requirement and this repository's authoring/runtime separation principle (ADR-0035). Leave `stb`'s `FetchContent` declaration where it is and simply reorder `add_subdirectory()` calls in root `CMakeLists.txt` instead of relocating the declaration — rejected as this Spec's own recommendation: `AtlantisDependencies.cmake` also declares Catch2 (test-only), so moving the *whole file* earlier/unconditional would pull a test-only dependency into every configure; splitting `stb`'s own declaration out is the narrower fix. | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 11 | **(Explicit, disclosed decision, revised after a review round's own deeper verification.)** Is "hand-authored UV in the fixture, zero shared-type changes" actually achievable, and if not, what's the narrowest real fix? | **No, not with zero shared-type changes — a review round's own verification found `VertexAttributeFormat` (`rhi/types.h:81-83`) has exactly one value, `Float3`, and every bridge that consumes it (`vertexAttributeFormatToVkFormat()`, `VertexAttributeType`, `toRhiFormat()`) is an exhaustive switch over `Float3` alone; a real 2-float UV cannot be expressed by *any* fixture without this specific addition.** The good news the same verification found: this is genuinely decoupled from, and far smaller than, adding UV to Asset System's own mesh pipeline — `Mesh`/`createMesh()` is already fully generic (raw bytes + stride), and `toVertexInputLayout()`/`createPipeline()`'s own attribute loop already accept an arbitrary attribute layout from any caller. **Recommendation: add `VertexAttributeFormat::Float2` (RHI) and `VertexAttributeType::Float2` (Shader System reflection, plus its `toRhiFormat()` case) — a small, mechanical, already-anticipated extension (`types.h`'s own comment names exactly this) — and nothing else.** The new fixture hand-authors its own `struct Vertex { float position[3]; float uv[2]; };`, its own new shader (own `[[vk::location(2)]] float2 uv` input, own reflection), carried through the same shader-reflection-driven `toVertexInputLayout()` path every existing fixture already uses — **not** a position-derived or otherwise fabricated coordinate, a real vertex-buffer-bound UV attribute. `StaticMeshAssetData`, the mesh authoring grammar, and the mesh runtime artifact remain completely untouched — this Spec's own closed-loop claim proves a real `Mesh`-driven UV attribute, but still explicitly does **not** claim a real *asset-sourced* textured mesh exists. "Mesh UV Attribute Foundation" (real UV0 inside Asset System's own mesh cook/artifact/load pipeline, a genuinely separate, three-layer, schema-version-bumping change) remains a named, immediate, blocking follow-up candidate for that claim; see Out of Scope / Future Work. | **(Original first-draft recommendation, now superseded.)** "Zero shared-type changes, hand-authored UV only" — this was this Spec's own first-drafted recommendation and is withdrawn: it is not actually achievable, since `VertexAttributeFormat` itself blocks it regardless of which fixture attempts it. Add full real UV0 to Asset System's own mesh pipeline in this Spec (authoring grammar, artifact schema-version bump, `StaticMeshAssetData`) — considered and still rejected: a genuinely separate, larger, mesh-schema-focused scope from "Texture & Sampler Foundation," requiring retrofitting or dual-schema-version-supporting the existing, checked-in `minimal_cube.mesh.txt` asset, better served by its own Spec once a real asset-sourced textured-mesh consumer exists to design against. A fullscreen-triangle/`SV_VertexID`-generated quad needing no vertex buffer or UV attribute at all — considered and rejected: proves texture *sampling* but nothing about a real, vertex-buffer-driven UV *attribute* flowing through `Mesh`/`VertexInputLayout`, a meaningfully weaker claim than the recommended design achieves for a comparably small cost. | This Spec's own Non-Goals; Out of Scope / Future Work |
| 12 | What is this round's mip-level contract? | Exactly 1 mip level, always — no mip-count creation parameter exposed, matching `DepthFormat`'s own "don't expose a knob for a dimension not yet supported" precedent. No mip generation, no mip selection in the shader (a fragment shader sampling a 1-mip texture has no LOD to select). | Expose a mip-count parameter now, defaulted to 1 — rejected, `types.h`'s own existing comments consistently prefer a single-value enum/fixed contract over an unused knob until a real second value is needed. | This Spec's own Non-Goals |
| 13 | What is the combined submission's own synchronization model, and exactly when is each resource safe to destroy? | Fully synchronous, via existing, real APIs, not a guess: one `CommandList` records the upload graph, `Renderer::drawFrame()`'s own draw graph, and a readback graph, in that order; **one** `Device::submit(commandList, *target)` (`target` real, genuinely drawn-into and read-from, item 6), then **one** `Device::waitIdle()` — this specific "submit once, block until GPU-complete" shape already used at `headless_rendering_gpu_tests.cpp:405`/`minimal_cube_fixture.cpp:279`, extended here to cover three graphs instead of two. `ShaderRead` correctness for the draw graph's own sampling is guaranteed by the upload graph's own barrier and the recorded execution order **within that one submission** — already true the instant the GPU begins processing it, not something `waitIdle()` establishes. `waitIdle()`'s own role is narrower: only after it returns `Ok` are the staging `Buffer` (upload source) and the readback `Buffer` (draw/readback destination) safe to destroy, and only then is it safe for the CPU to read the readback buffer's `mappedData()` — real, observed GPU completion, never "immediately after `submit()`". `SampledTexture`/`Sampler` are owned by the fixture; `Material` only borrows them (item 7) — the fixture must not destroy either before every `Material` referencing them is done being used. Device loss during this combined submission is reported via the existing `SubmitError::DeviceLost` enumerator, same as any other `submit()`/`waitIdle()` call; every other stage (resource creation, each graph's own `RenderGraphBuilder::compile()`/`execute()`) reports through its own existing `Result`/error channel — no new unified error type. | An asynchronous/deferred upload (submit without waiting, sample only once a fence signals) — rejected, adds real synchronization complexity this Spec's own minimal-loop scope does not need; Non-Goals already exclude streaming, which is the scenario that would motivate it. The upload as its own, separate, earlier `submit()`/`waitIdle()` cycle — withdrawn, this review round; see item 6. A new, dedicated "upload complete" error/status type — rejected, every stage already has its own established `Result`/error channel; inventing a new unified one would duplicate, not clarify. | [ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md) |
| 14 | What proves the texture-sampling path actually works — both linear and sRGB — and how is the new golden categorized, against ADR-0042's own exact bootstrap criteria? | A new, independent headless fixture (two hand-authored quads, each with a real, `Mesh`-bound UV0 attribute, item 11; two `SampledTexture`s cooked from the same source image, one `Rgba8Unorm` one `Rgba8Srgb`, item 3; one shared `Sampler`, item 8) rendered through the real cook→load→combined-submission-upload→bind→sample path (item 6), matched against its own new, first golden — captured under ADR-0042's "Initial baseline bootstrap" category, satisfying all six of its own numbered constraints explicitly: no prior golden at this path; captured against a clean, committed tree; full provenance recorded; the golden PNG/sidecar added via their own separate commit; the four-part substitute evidence (human visual confirmation that both quads render correctly and visibly, correctly differently, zero-diff self-reproduction, real-hardware run with Validation Layers clean, cited empirical-calibration evidence); and no relaxation of any other Decision-section rule for being a bootstrap golden. The existing `minimal_cube`/`world_scene` goldens and their own tests are re-run unmodified, as regression proof. | Reuse `minimal_cube`'s existing golden for the new fixture — rejected outright, `minimal_cube` has no texture binding and was never rendered with one; a texture-sampling claim needs its own scene and its own golden to mean anything. A single-quad, single-format golden (this Spec's own first-drafted design) — withdrawn, this review round; see item 3, does not prove `Rgba8Srgb`'s own real GPU sampling behavior. Describing this golden's own PR using ordinary "rendering change"/"reference-environment change" language instead of citing "Initial baseline bootstrap" explicitly — rejected, ADR-0042 itself forbids exactly this confusion. | This Spec's own Goals/Requirements |
| 15 | Does the Scene Asset format ([Spec 0015](0015-scene-asset-serialization-foundation.md)) gain a texture/material reference this round? | No — confirmed Non-Goal. A scene's `Renderable` continues to name exactly one mesh `AssetId`, entirely unchanged; this Spec's own textured fixture is not scene-driven at all (matching `minimal_cube_fixture`'s own non-scene, hand-authored construction shape). | Add an optional texture `AssetId` to `Renderable` now, unused by anything yet — rejected as speculative scope-widening of an already-`Approved`/merged Spec 0015 format for no consumer this Spec itself provides. | This Spec's own Non-Goals |
| 16 | What is the artifact's portability contract given Windows-now/Android-later? | Unconditional little-endian encoding (matching every prior artifact format) makes the byte format itself host-endianness-independent; raw, uncompressed RGBA8 carries no format-specific mobile-GPU concern to resolve now. Windows remains this Spec's own only verified target, matching every prior Spec — no ASTC/ETC mobile-compression format is chosen or implied here. | Choose a mobile-compressed format now "for" Android — rejected per AGENTS.md's own "do not add abstraction knobs for a capability that isn't being built" discipline; Android Platform itself remains an un-specced Candidate. | This Spec's own Non-Goals |
| 17 | **(Real, previously-dead-code gap, resolved — a review round's own deeper verification found this, and it blocks even the non-UV core scope.)** The shader-compiler tool's `CompileAndValidateRequest::expectedContract` is parsed from CMake but never consulted by `validateDescriptorContractForStage()`, which unconditionally validates every shader against `minimalRendererExpectedDescriptorContract()`. Does this Spec's own new shader (a sampler binding, regardless of the UV decision) need this fixed? | Yes — required, not optional. `compileAndValidate()`'s call to `validateDescriptorContractForStage()` is changed to consult the caller-supplied `expectedContract` instead of always calling `minimalRendererExpectedDescriptorContract()`. This is a small, mechanical fix (reading a field that is already fully plumbed from CMake through `main.cpp` to this exact call site) — not a new mechanism, not a new CMake parameter, not a change to the existing minimal-renderer shader's own contract or validation. | Leave `expectedContract` unread and give this Spec's own new shader a *different* build path that skips `validateDescriptorContractForStage()` entirely — rejected, would mean this Spec's own new shader is compiled without the same build-time descriptor-contract safety net every other shader in this repository already gets, a real regression in verification rigor for no stated benefit. | [ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md) |
| 18 | Does the small, disclosed `VertexAttributeFormat::Float2` addition (item 11) need its own, fourth Proposed ADR — matching the instruction that any UV0/mesh-schema decision be separately recorded — or is documenting it within this Spec's own Requirements/Human Review Decision Table sufficient? | Document it here, within this Spec — no fourth ADR. It is a single, mechanical enum-value addition (plus a matching Vulkan-format-mapping case) that `types.h`'s own existing comment already explicitly anticipates verbatim, introduces no new module boundary, no new ownership model, and no new public-API *shape* (the existing `VertexAttributeFormat`/`VertexInputLayout`/`toVertexInputLayout()` surface is unchanged in shape, only in which enumerator values are legal) — unlike `BufferPurpose::Staging`/`ResourceState::TransferDestination`/`ShaderRead` (folded into ADR-0056, since they gate a genuinely new capability, upload) or `SampledTextureFormat`/`DescriptorType::Sampler` (folded into ADR-0055/0056/0057, each gating a real new type/capability), `Float2` gates nothing new by itself — it only makes an already-generic, already-caller-supplied field able to hold one more legal value. Critically, and this is the actual UV0/mesh-schema decision the instruction is protecting against silently skipping: **this addition does not touch Asset System's own mesh schema/artifact version at all** (item 11) — the real, weightier "Mesh UV Attribute Foundation" decision (a genuine schema-version-bumping change) remains explicitly un-made by this Spec and is registered as its own, separate, future-Spec-level candidate, not quietly folded into this small RHI enum addition. | A fourth ADR dedicated to `VertexAttributeFormat::Float2` alone — considered and rejected as disproportionate to the decision's own actual weight, and risks implying (incorrectly) that this Spec has made the real Asset-System mesh-schema/UV0 decision, when it has deliberately not. Silently adding `Float2` as an implementation detail with no Human Review visibility at all — rejected outright, this is exactly the kind of change the instruction requires be surfaced explicitly, whether or not it rises to full-ADR weight. | This Spec's own Requirements; Out of Scope / Future Work |

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
- **"Hand-authored UV, zero shared-type changes" — this Spec's own
  original recommendation for item 11.** Withdrawn, not merely revised:
  a review round's own deeper verification found this is not actually
  achievable (`VertexAttributeFormat` has no `Float2` variant anywhere in
  the pipeline, blocking it regardless of which fixture attempts it) —
  see Human Review Decision item 11 for the corrected, code-proven
  design and item 18 for why the resulting small RHI addition still does
  not need its own ADR.
- **A new, target-optional/target-free `Device::submit()` overload**,
  considered so the one-time upload would not need to reuse the
  fixture's own `RenderTarget` at all. Rejected for this Spec's own
  scope — see Human Review Decision item 6; would be a real, disclosed
  RHI public-API change for a need this Spec does not actually have;
  registered as named future work instead (Out of Scope / Future Work).
- **The upload as its own, separate `submit()`/`waitIdle()` cycle,
  before a later, separate per-frame draw graph's own submission — this
  Spec's own first-drafted design for the base verification path.**
  Withdrawn, not merely revised, by a second review round: functionally
  correct, but it needlessly doubles GPU submissions/CPU stalls and
  diverges from `headless_rendering_gpu_tests.cpp`'s own closest
  existing precedent (draw pass + copy pass sharing one `submit()`) more
  than the combined design does; see Human Review Decision item 6 for
  the corrected, combined-submission design.
- **Exercise `Rgba8Srgb` with only a GPU-independent cook/decode
  round-trip unit test, using `Rgba8Unorm` for the one GPU-required
  fixture — this Spec's own first-drafted recommendation for item 3.**
  Withdrawn, not merely revised: `Rgba8Srgb`'s own hardware
  linearization-on-sample is real GPU behavior, not tonemapping, and a
  CPU-only test cannot prove it happens correctly; see Human Review
  Decision item 3 for the corrected, dual-format-in-one-golden design.

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
  present); a real captured Slang reflection JSON declaring a `float2`
  vertex input produces `VertexAttributeType::Float2`, and
  `toRhiFormat()` maps it to `VertexAttributeFormat::Float2` correctly;
  the new descriptor-contract shape accepts exactly the two-binding case
  and rejects a mismatched one (wrong count, wrong stage), matching
  `minimalRendererExpectedDescriptorContract()`'s own existing test
  discipline.
- Shader-compiler Tool: `compileAndValidate()` with a real
  `expectedContract` set to the existing one-binding minimal-renderer
  contract still validates the existing `minimal_mesh.slang`'s own
  reflection correctly (regression check for the `expectedContract`
  wiring fix, item 17); set to this Spec's own new two-binding contract,
  validates this Spec's own new shader's reflection correctly; a
  mismatched `expectedContract` against either shader's real reflection
  is rejected with the correct, existing `ContractMismatchError`
  enumerator.
- `Device::submit()` call-count and target-identity check: a unit-level
  (or GPU-independent-mocked, Plan-level detail) check that the
  fixture's own composition code calls `Device::submit()` exactly
  **once** for the upload+draw+readback sequence, passing the same
  `RenderTarget` reference the draw and readback graphs actually use —
  confirming no second, separate submission and no dummy target are
  silently introduced.
- `Material` borrowing contract: a `static_assert`/compile-time check (or
  equivalent) confirming `Material`'s new fields are non-owning reference/
  pointer types, not an owning smart pointer — matching this codebase's
  existing compile-time-provable-contract precedent (`EntityId`'s own
  V27-style unforgeability tests) rather than a runtime-only convention.
- Module-boundary include scan: `Atlantis::AssetSystem`'s own runtime
  loader translation units (`load.h`/`load.cpp` and this Spec's own
  `loadTextureAsset()`) never include `stb_image.h`/`stb_image_write.h`;
  only `atlantis_asset_cooker`'s own translation unit(s) do, and
  `Atlantis::AssetSystem`'s own runtime library target does not link
  `Stb::Stb` — confirmed by grep and by inspecting the actual CMake
  target link list, not inspection alone, matching every prior Spec's
  own module-boundary verification discipline. A CMake configure-only
  check (no build required) confirms `Stb::Stb` exists as a target with
  `ATLANTIS_BUILD_TESTS=OFF` (regression check for ADR-0041's own
  Proposed Amendment's CMake-ordering fix).

GPU-required (real hardware, matching every prior Spec's own headless
verification tier):

- **Headless, new golden (this Spec's own central verification claim,
  now covering both formats' real GPU sampling behavior in one
  combined submission):** cook the same source test image twice
  (`Rgba8Unorm` and `Rgba8Srgb`, item 3), load both, record one
  `CommandList` containing both upload graphs, the draw graph (two
  `DrawItem`s, one `Material` per format, both borrowed-reference
  `SampledTexture&`/`Sampler&`, item 7), and the readback graph, in that
  order (item 6) — submit **once**, `waitIdle()` **once**, and
  capture/compare against the new dual-quad golden, satisfying
  ADR-0042's own "Initial baseline bootstrap" category in full (item
  14) — zero channel difference on a second, independent run of the
  same fixture (confirming the whole combined-submission path is
  deterministic, not merely "looked right once"), and the two quads
  visibly, correctly different from each other (confirming `Rgba8Srgb`'s
  own hardware linearization genuinely happened, not merely that both
  textures uploaded without crashing). Vulkan Validation Layers grepped
  clean throughout (zero `VUID`/Validation Error/Warning matches),
  confirming the new barrier-plan entries and descriptor-binding
  changes are themselves layout- and binding-correct, not merely
  visually plausible — this is the real evidence that the
  `TransferDestination`/`ShaderRead` transitions, recorded and ordered
  correctly within the one combined submission, are genuinely correct,
  not merely that a plausible-looking image happened to result.
- **`Rgba8Unorm`/`Rgba8Srgb` artifact round-trip, GPU-independent
  (necessary, but explicitly not sufficient on its own):** a
  GPU-independent cook/decode unit test confirms both
  `SampledTextureFormat` values round-trip correctly through the
  artifact — proving the *format tag and bytes* are stored/retrieved
  correctly, which the GPU-required golden above does not re-prove at
  the byte level. This unit test is **not**, on its own, evidence that
  Vulkan's own hardware sampling behaves correctly for either format —
  that claim rests entirely on the golden above (item 3).
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
- **Whether the one-pass-per-texture upload graph shape generalizes
  cleanly to a future scene with many textures uploaded together** is
  not addressed here — this Spec's own scope is exactly two textures
  (one per color-space, for the fixture's own dual-format proof, item
  3), each with its own upload graph recorded into the same combined
  submission, and does not attempt to design a batched-upload mechanism
  for an arbitrary number of textures ahead of a real multi-texture
  consumer.
- **Target-independent submission remains genuinely unsolved, not
  merely deferred with a workaround.** This Spec's own combined-submission
  design (item 6) depends on a real `RenderTarget` always being
  available, which is true for every consumer this Spec itself
  anticipates (a headless fixture, and by extension Runtime's own
  per-frame loop); a future consumer needing to upload a texture with
  literally no frame in flight would need its own, separately-designed
  RHI change this Spec does not attempt to anticipate the shape of.
- **ADR-0041's own Proposed Amendment is a real, disclosed prerequisite
  for this Spec's own Implementation — drafted, but not yet accepted.**
  ADR-0041 now carries a full "Proposed Amendment — 2026-08-23" section
  (its own original `Accepted` Decision, Consequences, and Alternatives
  Considered left completely unmodified above it), covering the
  boundary widening itself, the real CMake configure-ordering defect
  this review round found (`cmake/AtlantisDependencies.cmake` included
  only inside `if(ATLANTIS_BUILD_TESTS)`, after
  `add_subdirectory(src/tools/asset_cooker)` already runs), the
  per-target single-implementation-TU rule, and license/offline-build/
  maintenance re-confirmation. Approving this Spec's own Human Review
  without also accepting that amendment would leave a genuine
  inconsistency between an `Accepted` ADR's stated boundary and this
  Spec's own design; Human Review Decision item 10 names this
  explicitly, and both this Spec and ADR-0041's own amendment are
  intended to be decided by the same Human Review pass.
- **This Spec's own new, real UV vertex attribute (item 11) is not, and
  must not be read as, evidence that a real *asset-sourced* textured
  mesh exists.** It proves a genuine `Mesh`/`VertexInputLayout`-bound UV
  attribute, carried through the same reflection-driven path every
  fixture uses — a real step forward from "hand-authored, disconnected
  from any shared type" — but `StaticMeshAssetData` and the mesh
  authoring/cook/artifact pipeline remain untouched. A future Spec
  claiming a real asset-sourced textured mesh still requires "Mesh UV
  Attribute Foundation" first — this Spec does not, and cannot,
  substitute for it.
- **The shader-compiler tool's `expectedContract` wiring fix (item 17)
  is a small, mechanical, low-risk change, but it is still a real code
  change to a shared Tools file (`compile_and_validate.cpp`) that every
  other existing shader's own build-time validation also runs through.**
  A Plan implementing this Spec must re-verify the existing
  `minimal_mesh.slang` shader still validates correctly (against
  `minimalRendererExpectedDescriptorContract()`, its own existing,
  unchanged contract, now reached via the newly-read `expectedContract`
  field rather than an unconditional call) — a regression here would be
  silent otherwise, since today's code path never varies by shader.

## Out of Scope / Future Work

**Mesh UV Attribute Foundation** — real `float2 UV0` inside Asset
System's own mesh pipeline: the authoring grammar, `MeshSourceVertex`,
and the mesh runtime artifact (a new schema version, not an in-place
mutation, matching this repository's own established
artifact-versioning precedent), plus `StaticMeshAssetData` — is
registered as an **immediate, named, blocking follow-up candidate**,
required before any future Spec may claim a real, *asset-sourced*
textured mesh exists; see Human Review Decision item 11. **This is
distinct from, and does not include, `VertexAttributeFormat::Float2`
itself** — that small, RHI/Shader-System-only addition is already part
of *this* Spec's own scope (items 11, 18), fully decoupled from Asset
System's own mesh pipeline, which remains completely untouched. This
Spec's own hand-authored fixture proves a real, `Mesh`-bound UV vertex
attribute — texture and vertex-layout infrastructure — explicitly not
an asset-sourced textured mesh.

**Target-independent submission** — a `Device::submit()` path admitting
a `CommandList` with no real `RenderTarget` at all, for a texture upload
genuinely decoupled from any per-frame draw (e.g. a future Runtime
loading a texture with no frame currently in flight) — is registered as
a named, disclosed future decision, not solved or worked around with a
dummy target here (item 6). This Spec's own base verification path
always has a real `RenderTarget` (the fixture's own draw target) and
uses it genuinely; nothing here scaffolds for, or anticipates the shape
of, a future target-free design.

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
