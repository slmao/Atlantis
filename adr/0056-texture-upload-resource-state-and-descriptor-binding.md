# ADR 0056: Texture Upload, Resource State, and Descriptor Binding

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review (as part of Spec 0016)
- **Related Spec:** [specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md)

## Context

This repository's RHI has no CPU→GPU copy-command path today of any
kind. `Buffer` (`src/rhi/include/atlantis/rhi/buffer.h:9-16`) states
plainly: *"This round, every Buffer is host-visible and host-coherent
regardless of purpose ... (no staging/upload path this round)."* The
existing upload path for mesh data (`renderer::createMesh()`,
`src/renderer/src/mesh.cpp:11-41`) is a direct `std::memcpy` into a
permanently host-mapped buffer — no GPU-side copy command exists at all.
`ResourceState` (`types.h:56-63`) has exactly six values (`Undefined`,
`ColorAttachmentWrite`, `PresentSource`, `ColorAttachmentOutput`,
`DepthAttachmentReadWrite`, `TransferSource`); Vulkan Backend's own
`(before, after)` barrier-plan table
(`src/vulkan_backend/src/resource_state_mapping.cpp:104-131`) is
exhaustive by design — any pair not explicitly listed hits an
`ATLANTIS_CHECK_MSG(false, ...)` hard failure, never a silent
best-effort transition.

`CommandList::copyRenderTargetToBuffer()`
(`src/rhi/include/atlantis/rhi/command_list.h:77-84`, Spec 0010/
ADR-0040) is the only copy command that exists, and it moves data in the
opposite direction this Spec needs (GPU image → CPU-visible buffer, for
readback) — nothing copies a CPU-populated buffer into a GPU image.

RenderGraph's own execution model
(`src/render_graph/include/atlantis/render_graph/execution.h:41-49`)
binds exactly two resource kinds per `ResourceBinding`: `RenderTarget*
target` and `Texture* depthTexture` (depth-only). `execute()`
(`src/render_graph/src/execution.cpp:94-127`) dereferences one of these
two per binding to insert transitions — there is no third slot. AGENTS.md
states, without qualification: *"Render Graph is the mandatory path for
GPU work. No subsystem submits ad hoc, hand-scheduled GPU work outside
it"* (`AGENTS.md:142-143`), and the RenderGraph module boundary's own
Responsibilities line states the same constraint (*"no ad hoc
direct-submission rendering path is allowed to bypass it"*). This is not
advisory language — it is the same governing principle that already
shapes every existing GPU-work path in this codebase (clear, draw,
readback all run through RenderGraph passes today).

`Material` (`src/renderer/include/atlantis/renderer/material.h:18-32`)
today owns only a `Pipeline`; the entire descriptor-binding surface lives
on `CommandList`/`VulkanDevice`, hardcoded to exactly one binding — a
uniform buffer at set 0/binding 0, vertex stage
(`vulkan_device.cpp:808-861`), against a device-level descriptor pool
sized for exactly that one binding type
(`vulkan_device.cpp:1245-1254`, `maxSets = 4`, one
`VkDescriptorPoolSize` entry).

**A review round's own deeper verification of the submission/completion
API this ADR's Decision 4 depends on** found: `Device::submit()` has
exactly one overload
(`src/rhi/include/atlantis/rhi/device.h:51-52`:
`submit(std::unique_ptr<CommandList>, const RenderTarget&)`), and
`VulkanDevice::submit()` (`vulkan_device.cpp:508-564`) unconditionally
`dynamic_cast`s the target and `ATLANTIS_CHECK_MSG`-asserts it is real
and module-produced (`vulkan_device.cpp:530-531`) — there is no
target-optional or target-free submit path anywhere in this codebase
today. `SubmissionSignal`
(`src/rhi/include/atlantis/rhi/submission_signal.h:21-24`) is opaque —
no public method beyond its own destructor, never inspected or waited on
by a caller. The real, already-existing, already-used blocking
completion mechanism is `Device::waitIdle()` (`device.h:59`), used
today at exactly the "submit once, then block until GPU-complete" shape
this Spec's upload needs: `tests/vulkan_backend/headless_rendering_gpu_tests.cpp:405`
(`REQUIRE(device->waitIdle().isOk());` immediately after `submit()`),
`tests/image_regression/fixture/minimal_cube_fixture.cpp`'s own
`renderOneFrame()` (`:279`), and Runtime's own shutdown path
(`src/runtime/src/runtime_application.cpp:455`). That same headless test
(`headless_rendering_gpu_tests.cpp:360-408`) is also this codebase's
closest existing precedent for the upload's own shape: it acquires one
real `OffscreenTarget`-vended `RenderTarget`, builds and `execute()`s a
*second*, caller-built `RenderGraphBuilder` pass (a copy pass, distinct
from the drawing pass already recorded into the same `CommandList`) into
that same `CommandList`, then `submit()`s once and `waitIdle()`s once
for both passes together.

**A second review round's own further verification corrected this
ADR's own first-drafted Decision 5–7 below**, which had the upload run
as its own, separate `submit()`/`waitIdle()` cycle *before* a later,
separate per-frame draw graph. That design works, but does not match
`headless_rendering_gpu_tests.cpp`'s own precedent as closely as
possible (which submits its draw pass and its copy pass *together*, one
`submit()`), and unnecessarily doubles GPU submissions and CPU stalls
for a fixture whose own base verification need is exactly one combined
submission covering upload, draw, and readback together. The corrected
Decisions below reflect the combined design.

## Decision

1. **New `BufferPurpose::Staging`**, extending the existing four-value
   `BufferPurpose` enum exactly as `Readback` (Spec 0010) already did —
   host-visible, host-coherent, matching `Buffer`'s own existing,
   unmodified contract. A `Staging`-purpose buffer holds decoded pixel
   bytes transiently, between CPU load and the GPU copy below; it is not
   retained past the one-time upload that consumes it.
2. **New `ResourceState::TransferDestination` and
   `ResourceState::ShaderRead`**, extending the existing 6-value
   `ResourceState` enum. Vulkan Backend's exhaustive barrier-plan table
   (`resource_state_mapping.cpp`) gains exactly two new, explicit
   entries: `Undefined → TransferDestination` and
   `TransferDestination → ShaderRead` — no wildcard/catch-all transition,
   preserving the table's own existing exhaustiveness guarantee.
3. **New `CommandList::copyBufferToTexture(Buffer&, SampledTexture&)`**
   and a third `CommandList::transitionResource(SampledTexture&,
   ResourceState before, ResourceState after)` overload, alongside the
   two existing `RenderTarget`/depth-`Texture` overloads
   (`command_list.h:29,38`).
4. **The one-time texture upload runs as a genuine, minimal RenderGraph
   execution — never a raw `CommandList` sequence outside it.**
   `ResourceBinding` gains a third resource-carrying field, a generic
   `SampledTexture*`, alongside the existing `target`/`depthTexture`
   fields — tracking only the destination `SampledTexture`; the source
   staging `Buffer` is not itself RenderGraph-tracked, matching
   `copyRenderTargetToBuffer()`'s own existing untracked-destination-
   buffer precedent. `execute()`'s transition-insertion logic is
   extended to drive `Undefined → TransferDestination` (before the copy)
   and `TransferDestination → ShaderRead` (after it) for this binding
   kind, reusing `ResourceBinding`'s existing `incomingState`/`finalState`
   fields (`execution.h:23-40`) — the same mechanism Spec 0010's own
   readback `finalState` already uses, not a new mechanism.
5. **The upload, the real draw, and the readback all share exactly one
   `Device::submit()` call — `Device::submit()` itself is unchanged;
   this ADR introduces no target-optional or target-free submit path,
   and does not use a dummy target to work around needing one.** The
   caller (this Spec's own fixture) creates one real `OffscreenTarget`,
   `acquireTarget()`s once for one real `RenderTarget`, and creates
   **one** `CommandList`. It records, in order, into that one
   `CommandList`: (a) the upload graph(s) from Decision 4 above; (b)
   `Renderer::drawFrame()`'s own draw graph (unmodified shape),
   rendering into the same `RenderTarget`, its `DrawItem`s' `Material`s
   sampling the now-`ShaderRead` `SampledTexture`(s) via
   `CommandList::bindTexture()` (Decision 8 below), leaving the target in
   `ResourceState::TransferSource` via its own existing `finalColorState`
   parameter; (c) a third, caller-built `RenderGraphBuilder` readback
   graph, `writes(target, ResourceState::TransferSource)` matching what
   (b) just left it in, calling the existing
   `copyRenderTargetToBuffer(*target, *readbackBuffer)` — this
   three-graphs-in-one-`CommandList` shape directly extends
   `headless_rendering_gpu_tests.cpp`'s own already-established
   two-graphs-in-one-`CommandList` precedent (draw pass + copy pass) by
   one more graph at the front. Then **exactly one**
   `submit(commandList, *target)` — `target` genuinely participates
   (drawn into by (b), read from by (c)), never reused merely to satisfy
   the parameter's own signature.
6. **`ShaderRead` correctness is guaranteed by the upload graph's own
   barrier and the recorded execution order within that one submission —
   `Device::waitIdle()` plays no role in it.** The upload graph's own
   `execute()` call records a real `vkCmdPipelineBarrier` transitioning
   `SampledTexture` to `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL` before
   the draw graph's own recorded sampling commands, both inside the same
   `CommandList` — Vulkan's own single-command-buffer, single-queue
   execution-order guarantee, together with the barrier's own
   pipeline-stage synchronization, is what makes the draw pass's own
   sampling valid, and this is already true the instant the GPU begins
   processing this one submission, independent of `waitIdle()`.
   `Device::waitIdle()`'s own role is narrower and purely CPU-side: it is
   the signal that it is safe (a) for the CPU to read the readback
   `Buffer`'s `mappedData()`, and (b) to destroy the staging `Buffer`(s)
   (their own upload already consumed) and the readback `Buffer` (its
   data already read) — submission is asynchronous, so only `waitIdle()`'s
   own return, not `submit()`'s, is real evidence of this. `SampledTexture`/
   `Sampler` themselves are owned by the same composition root that
   creates them; `Material` (Decision 8 below) only borrows them.
7. **Upload/submit/device-loss error semantics reuse existing `Result`
   channels — no new unified error enum.** Resource creation
   (`SampledTexture`, `Sampler`, the staging `Buffer`, the readback
   `Buffer`) reports through existing `*CreateError`-style enums; every
   `RenderGraphBuilder::compile()`/`execute()` call (upload, draw,
   readback) reports through RenderGraph's own existing compile/execute
   error channel, unchanged; the one `submit()`/`waitIdle()` pair reports
   through the existing `SubmitError` enum, whose already-`Accepted`
   `DeviceLost` enumerator covers a device loss during this combined
   submission exactly as it already covers one during any other
   `submit()`/`waitIdle()` call.
8. **`Material` gains an optional, construction-time, borrowed — never
   owned — `SampledTexture`/`Sampler` pair**, explicitly extending
   `DrawItem`'s own existing "mesh/material are borrowed (must outlive
   the `Renderer::drawFrame()` call they are passed to)" contract
   (`draw_item.h:10-12`) to `Material`'s own new fields: the caller that
   constructs a `Material` with a `SampledTexture`/`Sampler` pair must
   keep both alive for at least as long as that `Material` is used in
   any `drawFrame()` call; destroying either while a live `Material`
   still references it is a caller precondition violation, not a checked
   error, matching this codebase's existing borrowed-reference discipline
   exactly — not implicit shared ownership. A new
   `CommandList::bindTexture(SampledTexture&, Sampler&)`, called inside
   `Renderer`'s existing per-`DrawItem` pass-callback loop
   (`src/renderer/src/renderer.cpp:26-31`), immediately alongside the
   existing `bindUniformBuffer()` call. Vulkan Backend's per-`Pipeline`
   descriptor-set-layout creation and the device-level descriptor pool
   each gain one new, fixed, second entry: binding 1,
   `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, fragment stage.
9. **Combined image sampler, not separate texture/sampler descriptor
   types**, for the one new Vulkan binding this ADR adds — one binding
   slot, one pool-size entry, one `VkDescriptorImageInfo` pairing an
   image view and a sampler at bind time. This is a decision about the
   Vulkan-level binding mechanism only; `Sampler`'s own independence at
   the RHI level (ADR-0055) is unaffected.
10. **Single mip level, fully synchronous, combined-submission upload —
    no asynchronous or deferred readiness state.** Every `SampledTexture`
    this round has exactly one mip level; the whole combined submission
    (upload, draw, readback per Decision 5) completes as a unit —
    `submit()` once, then `waitIdle()` once, returning `Ok` (real,
    blocking, confirmed, per Decisions 5–6 above) — before the fixture
    reads back or reasons about any result. No texture is ever sampled
    "maybe still uploading"; `ShaderRead` readiness is a compile-time
    consequence of graph ordering (Decision 4/6), not a runtime race.
11. **Target-independent submission — an upload with no real
    `RenderTarget` at all — is explicitly named future work, not solved
    or worked around with a dummy target here.** `Device::submit()` has
    exactly one overload, requiring a real, module-produced
    `const RenderTarget&`; this ADR's own base verification path
    (Decision 5) always has a real one and reuses it genuinely. A future
    Runtime need for an upload genuinely decoupled from any per-frame
    `RenderTarget` (e.g. loading a texture with no frame currently in
    flight) would need its own, separately-designed RHI change — not
    scaffolded for or anticipated here.
12. **The shader-compiler tool's `expectedContract` field, already
    plumbed from CMake but never read, is wired into real use.**
    `CompileAndValidateRequest::expectedContract`
    (`compile_and_validate.h:21`) is parsed but never consulted by
    `validateDescriptorContractForStage()`
    (`compile_and_validate.cpp:129-142`), which unconditionally calls
    `minimalRendererExpectedDescriptorContract()` for every shader
    compiled through `atlantis_add_slang_shader_pair()`. Without this
    fix, this Spec's own new shader (declaring the second, sampler
    binding from Decision 8) would fail build-time validation against
    the *wrong*, fixed, one-binding contract, independent of any UV
    decision. `compileAndValidate()`'s own call site is changed to
    consult the caller-supplied `expectedContract` instead — a small,
    mechanical fix using an already-declared field, not a new mechanism
    or a new CMake parameter.

## Consequences

### Positive

- Fully respects AGENTS.md's own already-established, non-negotiable
  Golden Rule constraint — introduces no new "GPU work outside
  RenderGraph" precedent for any future spec to point back to.
- **No RHI public-API change to `Device::submit()`, `Device::waitIdle()`,
  or `SubmissionSignal`** — two review rounds' own deeper verification
  confirmed the existing, already-used submit/wait-idle shape (already
  proven at `headless_rendering_gpu_tests.cpp`/`minimal_cube_fixture.cpp`)
  covers this Spec's own upload need without modification, once the
  upload, real draw, and readback share one combined submission against
  a real, genuinely-used `RenderTarget`.
- **Fewer GPU submissions and CPU stalls than the immediately-prior
  draft** (which submitted the upload separately, before a later,
  separate draw submission) — one `submit()`/`waitIdle()` pair covers
  the fixture's entire base verification path, matching this
  repository's own closest existing multi-graph-per-submission
  precedent as closely as possible rather than diverging from it.
- The new barrier-plan entries are explicit and additive; the existing
  table's own hard-failure-on-unlisted-pair behavior is preserved
  unchanged for every other transition.
- `Material`'s new binding is additive (a second, fixed slot) — the
  existing one-uniform-buffer contract, and every shader that declares
  only it, continues to validate and bind exactly as before, once the
  `expectedContract` wiring fix (Decision 11) is in place.
- Combined image sampler keeps the Vulkan descriptor-pool/layout change
  to the minimum needed for this Spec's own one-texture, one-sampler
  scope.
- Every new error condition reuses an existing `Result`/error channel
  (Decision 7) — no new error taxonomy to maintain or keep in sync with
  this repository's existing ones.

### Negative / Trade-offs

- RenderGraph's `ResourceBinding` union grows a third case, and
  `execute()` grows new branch logic for it — real, disclosed complexity
  this Spec's own suggested core scope did not originally anticipate
  (found only by checking AGENTS.md's own constraint against the actual
  proposed design, not assumed away).
- A one-time, single-pass RenderGraph execution per texture is not
  designed to batch multiple textures' uploads into one pass/graph —
  acceptable for this Spec's own one-texture scope; a future spec with
  many textures to upload at once will need to revisit this.
- The device-level descriptor pool's fixed `maxSets`/pool-size-entry
  model (already a known, narrow, hardcoded shape before this ADR) now
  carries two hardcoded entry kinds instead of one — still not a general
  descriptor-pool sizing strategy, deferred exactly as before.
- The `expectedContract` wiring fix (Decision 11) touches a shared Tools
  file (`compile_and_validate.cpp`) every existing shader's own
  build-time validation also runs through — a Plan implementing this
  ADR must re-verify the existing `minimal_mesh.slang` shader still
  validates correctly, since today's code path never varies by shader
  and a regression here would otherwise be silent.

## Alternatives Considered

- **A raw, one-time `CommandList` sequence issued directly against
  `Device`, bypassing RenderGraph.** Rejected outright — this was this
  Spec's own first-drafted design and was corrected during self-review
  specifically because AGENTS.md forbids it without qualification; not a
  style preference.
- **A new, target-optional/target-free `Device::submit()` overload**,
  so the upload would not need to reuse the caller's own `RenderTarget`
  at all. Considered during a later review round and rejected: a real,
  disclosed RHI public-API change (a second overload plus a
  `VulkanDevice::submit()` branch skipping the `dynamic_cast`/semaphore-
  read block) for a need this Spec's own single-texture, single-fixture
  scope does not actually have, since the caller already creates and can
  reuse a real `OffscreenTarget`-vended `RenderTarget`. Deferred to a
  future spec if a genuine target-free-submission consumer (e.g.
  compute-only work) ever needs one.
- **Destroy the staging `Buffer` immediately after `submit()` returns**,
  rather than after `waitIdle()` returns. Rejected outright — `submit()`
  is asynchronous; the GPU may still be reading the staging buffer's
  memory when `submit()` itself returns, making this a real
  use-after-free risk, not merely an overly conservative choice.
- **The upload as its own, separate `submit()`/`waitIdle()` cycle,
  before a later, separate per-frame draw graph's own submission.**
  This was this ADR's own first-drafted design (Decisions 5–7, prior
  revision) and is withdrawn, not merely revised: it is correct but
  needlessly doubles GPU submissions and CPU stalls for a fixture whose
  own base verification need is exactly one combined submission, and a
  second review round found it diverges from
  `headless_rendering_gpu_tests.cpp`'s own closest existing precedent
  (draw pass + copy pass sharing one `submit()`) more than necessary.
  Nothing about the separate-submission design was functionally
  incorrect; the combined design is preferred as the closer match to
  established precedent and the smaller footprint.
- **A throwaway/dummy `RenderTarget`, constructed solely to satisfy
  `submit()`'s own required parameter, reused across an otherwise
  target-free upload.** Rejected — indistinguishable in effect from a
  target-free submit path this codebase does not have, and would let a
  future reader mistake `submit()`'s target parameter for something
  optional/decorative when it is not; the real, already-acquired
  `RenderTarget` a headless fixture already has is always the correct
  choice when one exists.
- **Teach RenderGraph to manage the texture's full lifecycle (creation,
  upload, and every subsequent per-frame binding) as a tracked,
  persistent resource.** Rejected as more machinery than this Spec's own
  one-time-upload-then-read-only-forever shape needs; RenderGraph tracks
  the upload transition only, not the texture's own creation or its
  read-only use in later, unrelated per-frame graphs.
- **`Material` taking shared/owning references to `SampledTexture`/
  `Sampler`** (e.g. a `shared_ptr`-style handle), rather than borrowing.
  Rejected — introduces implicit shared ownership this codebase's own
  "explicit ownership, no hidden caching" principle does not use
  anywhere else, for no need this Spec's own single-fixture scope has.
- **Separate, non-combined texture/sampler descriptor types.** Rejected
  — more descriptor-pool/layout complexity for no benefit until a real
  consumer needs to reuse one sampler across many images independently
  bound.
- **A general, variable-length descriptor-binding list on `Material`.**
  Rejected as premature — matches this codebase's own "one fixed,
  hardcoded contract, extended only when a real need appears" discipline
  already established by `minimalRendererExpectedDescriptorContract()`.
- **Leave `expectedContract` unread, giving this Spec's own new shader a
  different build path that skips `validateDescriptorContractForStage()`
  entirely.** Rejected — would mean this Spec's own new shader is
  compiled without the same build-time descriptor-contract safety net
  every other shader in this repository already gets, a real regression
  in verification rigor for no stated benefit.
