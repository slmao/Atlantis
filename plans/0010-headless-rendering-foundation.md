# Plan: Headless Rendering Foundation

- **Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human direction.

## Objective

Implement Spec 0010's approved design: an offscreen `RenderTarget` source
(`OffscreenTarget`, no `Presentation`), a minimal GPU-to-CPU readback
capability (`ResourceState::TransferSource`, a readback `Buffer`
purpose, `CommandList::copyRenderTargetToBuffer()`), the RenderGraph
`execute()` binding generalization
([ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md))
and `Renderer::drawFrame()`'s new `finalColorState` parameter
([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Accepted Amendment) that together let the exact same
`Renderer` → RenderGraph → RHI → Vulkan Backend stack draw and read back
a frame with no window, `Presentation`, or `VkSwapchainKHR` anywhere in
the composition.

## Authoritative Sources

Read in full before implementing any step below:

- [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)
  (`Approved`) — the governing spec; every step below cites the exact
  Requirements bullet(s) it implements.
- [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Accepted Amendment (2026-08-15/2026-08-16) — `finalColorState`.
- [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  (`Accepted`) — `OffscreenTarget` construction, ownership, and the
  two-part (borrow wrapper / backing resource) lifetime contract.
- [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  (`Accepted`) — `ResourceBinding`'s `incomingState`/`finalState`.
- [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)
  (`Accepted`) — `TransferSource`, readback `Buffer`, `copyRenderTargetToBuffer()`.
- [docs/process/testing-strategy.md](../docs/process/testing-strategy.md),
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- Existing implementation, read in full or in the specific parts cited
  per step below: `src/rhi/include/atlantis/rhi/{types,device,command_list,render_target,presentation,buffer,texture}.h`,
  `src/render_graph/include/atlantis/render_graph/execution.h`,
  `src/render_graph/src/execution.cpp`,
  `src/renderer/include/atlantis/renderer/renderer.h`,
  `src/renderer/src/renderer.cpp`,
  `src/vulkan_backend/src/{vulkan_device,vulkan_presentation,vulkan_render_target,vulkan_command_list,vulkan_texture,vulkan_buffer,vulkan_memory,resource_state_mapping}.{h,cpp}`,
  `examples/frame_execution_demo/main.cpp`,
  `examples/minimal_renderer_demo/main.cpp`,
  `tests/render_graph/fake_command_list.h`,
  `tests/renderer/renderer_ownership_tests.cpp`.

## Critical Architectural Boundaries (preserved, not re-decided here)

Restated for Plan Review's benefit — none of the following is open for
reinterpretation during this Plan or its Implementation; a step below
that appears to require reopening one of these must stop and escalate
(see Human Review / Plan Review Blockers):

- `Renderer`'s dependency set stays exactly `Atlantis::RHI`/
  `Atlantis::RenderGraph`/`Atlantis::Core`; it gains one new parameter,
  nothing else, and never inspects, branches on, or logs the value it is
  given.
- `RenderTarget`'s public interface (`extent()`/`format()` only) does
  not change. `OffscreenTarget` is a second *vendor* of the existing
  type, never a change to the type itself.
- A vended borrow ends via RAII; no public `release()`/`consume()` API
  is added anywhere.
- The borrow wrapper's lifetime and `OffscreenTarget`'s own (backing-
  resource) lifetime are two independent contracts. The latter's
  destruction precondition (GPU work referencing `OffscreenTarget`'s
  resources must have completed, established by the caller via
  `Device::waitIdle()`) is **deliberately not guaranteed-detectable** —
  no new assertion, `Result` error, implicit destructor wait, or
  per-resource/per-submission tracking system may be introduced to make
  it so. Only the double-acquire and destroy-while-borrow-outstanding
  cases are guaranteed-detectable, via the existing outstanding-borrow
  boolean.
- `ColorAttachmentOutput → TransferSource` is the *only* new
  `planTransition()` entry this plan may add. No other new
  `(before, after)` pair is in scope.
- `Texture`/`DepthFormat` are not widened or touched. `OffscreenTarget`'s
  color resource is never represented as a `Texture`.
- `atlantis::rhi::Format`'s doc comment is re-scoped (per ADR-0038) but
  its four values and their meaning are unchanged.
- No general GPU memory allocator is introduced; every new allocation
  ( `OffscreenTarget`'s color image, the readback `Buffer`) uses exactly
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  existing direct, individual `vkAllocateMemory`/`vkFreeMemory` policy.
- No third-party dependency is added.

## Non-Goals (confirmed matching Spec 0010)

Not implemented by this plan, in any form: golden-image storage,
comparison, tolerance methodology, or CI image-regression gating;
Android, iOS, or Linux support/build configuration of any kind;
asynchronous/non-blocking readback; multiple frames in flight;
multi-threaded command recording/resource creation/graph execution;
depth-buffer readback; a general sampled `Texture`/`Sampler`;
`OffscreenTarget` pooling or multiple simultaneously-live instances.

## Candidate-API Status

Every concrete type/method/field name, file split, and struct layout
below is a **candidate** — a reasonable, precedent-consistent proposal
for Plan Review to confirm or redirect, not a re-opening of anything
Spec 0010/ADR-0022/0038/0039/0040 already fixed at the conceptual level.
Where this plan found more than one genuinely different, defensible
implementation shape while researching the existing code, it says so
explicitly and defers the choice to Plan Review (see "Design choices
flagged for Plan Review" under Human Review / Plan Review Blockers) —
it does not pick silently.

---

## 1. RHI Type and Interface Additions

**Spec Requirements:** "Offscreen `RenderTarget` construction and
ownership," "GPU-to-CPU readback capability" (Requirements). **ADRs:**
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md),
[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md).

### 1.1 `src/rhi/include/atlantis/rhi/types.h` (modify)

- **Input:** existing `ResourceState`, `BufferPurpose`, `Format` enums;
  existing `TextureCreateParams`/`BufferCreateParams` shape as the
  sibling pattern to follow.
- **Output:**
  - `ResourceState` gains `TransferSource` (append after
    `DepthAttachmentReadWrite`, preserving existing enumerator order).
  - `BufferPurpose` gains `Readback` (append after `Uniform`).
  - New `OffscreenTargetCreateParams` struct: `Extent2D extent;
    Format format = Format::Rgba8Unorm;` plus a matching `operator==`
    declaration, in the same position/style as `TextureCreateParams`.
    **Corrected during Plan Review:** an earlier draft of this plan
    defaulted `format` to `Format::Unknown`, matching `Format`'s
    original (pre-ADR-0038) role as a read-only metadata query result —
    but `toVkFormat(Format)` (`vulkan_device.cpp`)
    `ATLANTIS_CHECK_MSG(false, ...)`-asserts on `Format::Unknown`, so a
    caller who omitted `.format` would hit an assertion failure at
    `createOffscreenTarget()` time. Every sibling `*CreateParams` struct
    defaults its enum field to a real, usable value instead
    (`TextureCreateParams::format = DepthFormat::D32Sfloat`,
    `BufferCreateParams::purpose = BufferPurpose::Vertex`) — `Rgba8Unorm`
    is chosen here for the same reason, matching this codebase's
    established precedent rather than introducing a construction
    parameter that silently defaults to an unusable sentinel.
  - New `OffscreenTargetCreateError` enum, candidate:
    `{ AllocationFailed, ImageCreationFailed, ImageViewCreationFailed }`
    — mirrors `TextureCreateError` exactly, since offscreen color image
    creation is structurally identical (image + memory + view).
  - New `OffscreenAcquireError` enum for `OffscreenTarget::acquireTarget()`'s
    `Err` channel, candidate: `{ DeviceLost, Unknown }` — a narrow
    subset of `PresentationError`'s own non-precondition variants
    ([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md):
    "`Err(...)` for a genuine unrecoverable, environmental failure...
    mirroring `PresentationError`'s own non-precondition variants").
    **Flagged for Plan Review** — see Human Review / Plan Review
    Blockers: this enum has no reachable production path in this
    round's concrete Vulkan Backend implementation (see Section 5); an
    alternative is a bare `std::unique_ptr<RenderTarget>` return with no
    `Result` wrapper at all, which would be a smaller, more honest
    surface but a literal deviation from ADR-0038's own stated
    `Result<std::unique_ptr<RenderTarget>, Err>` shape.
  - `Format`'s doc comment updated per ADR-0038: replace "Describes
    only the currently-selected swapchain surface format for this
    spec's read-only metadata query — not a general resource-format
    system" with wording that also names `OffscreenTargetCreateParams`
    as a legitimate, still-narrow use, per ADR-0038's own "Format reuse"
    subsection (quote its exact re-scoping language, do not
    freehand new wording).
- **Dependency order:** first — every other RHI/RenderGraph/Renderer/
  Vulkan Backend step depends on these enum values and this struct
  existing.
- **Tests after this step:** `tests/rhi/types_tests.cpp` — extend the
  existing `"ResourceState enumerators are all distinct and usable"`
  case to include `TransferSource`; add a `BufferPurpose` distinctness
  case if none exists yet (verify against current file content at
  implementation time — Section 1's own research found no existing
  `BufferPurpose`-specific `TEST_CASE`, so add one, `[rhi][buffer_purpose]`);
  add `"OffscreenTargetCreateParams equality and inequality"`,
  `[rhi][offscreen_target_create_params]`, mirroring the existing
  `ClearColorValue`/`Extent2D` equality test shape.
- **Stop condition / rollback:** revert this file alone; nothing else in
  the codebase references these new names yet, so this step is
  independently revertible with no cascading edit.

### 1.2 `src/rhi/include/atlantis/rhi/offscreen_target.h` (new)

- **Input:** [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)'s
  Decision; `src/rhi/include/atlantis/rhi/presentation.h` as the
  structural sibling to follow (abstract interface, `Result`-returning
  pure-virtual methods, thread-safety/lifetime doc comment in the same
  style).
- **Output:** new abstract class `atlantis::rhi::OffscreenTarget`:
  ```
  class OffscreenTarget {
   public:
    virtual ~OffscreenTarget() = default;

    // Two-outcome (ADR-0038) -- Err reserved for genuine environmental
    // failure; calling this while a previously-vended borrow is still
    // outstanding is a guaranteed-detectable programmer error, not part
    // of this Result::Err channel.
    [[nodiscard]] virtual atlantis::Result<std::unique_ptr<RenderTarget>, OffscreenAcquireError>
    acquireTarget() = 0;
  };
  ```
  Doc comment states, in this codebase's established style: non-owning
  vended `RenderTarget`, RAII-based borrow return (no `release()`), the
  minimum-borrow-lifetime claim, the separate/independent backing-
  resource destruction precondition (cross-referencing ADR-0038, not
  restating its full argument), and "not thread-safe; caller-thread-only"
  (ADR-0004).
- **Candidate method name:** `acquireTarget()` — Spec 0010's own
  Proposed Design flow diagram already uses this name consistently;
  adopted here for continuity, confirmable at Plan Review.
- **Dependency order:** after 1.1 (`OffscreenAcquireError` must exist).
- **Tests after this step:** none directly (pure interface, no logic);
  covered indirectly once the Vulkan Backend implementation and its GPU
  tests land (Section 5/7).
- **Stop condition / rollback:** revert this file; no other step depends
  on it compiling until Section 1.3/5.

### 1.3 `src/rhi/include/atlantis/rhi/device.h` (modify)

- **Input:** existing `createBuffer()`/`createTexture()` pure-virtual
  shape as the pattern to extend.
- **Output:** add
  ```
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<OffscreenTarget>, OffscreenTargetCreateError>
  createOffscreenTarget(const OffscreenTargetCreateParams& params) = 0;
  ```
  plus `#include <atlantis/rhi/offscreen_target.h>`. Doc comment: one
  line, matching `createBuffer()`/`createTexture()`'s own terseness —
  "stateless factory call; `Device` does not retain a reference to any
  `OffscreenTarget` it creates (ADR-0003)."
  **Also corrects a now-stale existing comment, found during the final
  Plan Review round:** `submit()`'s own doc comment
  (`device.h`, current text: "a caller must call `present()` for a
  successful `submit()` before calling `submit()` again — `submit()`
  followed directly by application exit remains legal (`waitIdle()`
  drains it)") states a precondition that is accurate for a windowed
  caller but was written before any caller could legitimately avoid
  `present()` altogether. A headless caller never constructs a
  `Presentation` and therefore never calls `present()` at all — yet
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  explicitly documents repeated `acquireTarget()` → `submit()` cycles
  against the same live `OffscreenTarget`, with no intervening
  `present()` ever, as a safe, intended pattern (its own "GPU-in-flight
  safety for repeated acquire cycles... comes entirely from
  `Device::submit()`'s existing single-frame-in-flight fence-wait"
  argument — verified directly against `waitAndReleaseRetainedSubmission()`
  in `vulkan_device.cpp`, which waits only on `submissionFence_`, a
  `Device`-owned fence entirely independent of which concrete
  `RenderTarget` type was submitted, never on anything `present()`
  itself would wait on or signal). Leaving the old comment as-is would
  violate [AGENTS.md](../AGENTS.md)'s "update or remove a comment in the
  same change that makes it stale" rule (Documentation and code
  comments) — the same rule ADR-0038 already invoked once for `Format`'s
  own doc comment (Section 1.1 above). This plan therefore amends
  `submit()`'s doc comment, in the same change that adds
  `createOffscreenTarget()` to this file, to state: the
  present()-before-next-`submit()` precondition applies only to a
  caller that constructs and drives a `Presentation`; a headless caller
  that never constructs one is exempt from it, and its repeated-`submit()`
  safety instead comes entirely from `submit()`'s own internal
  single-frame-in-flight fence-wait (cite ADR-0038's Lifetime contract,
  Part 2's "repeated acquire cycles" argument, not merely asserted here).
  This is a **comment-only** change — `submit()`'s parameters, return
  type, and runtime behavior are entirely unchanged; no new
  precondition, check, or `Result::Err` variant is introduced.
- **Dependency order:** after 1.1, 1.2.
- **Tests after this step:** none directly; this is an interface-only
  addition, exercised once Section 5 implements it.
- **Stop condition / rollback:** revert this file; `Device`'s existing
  concrete implementation (`VulkanDevice`) will fail to compile until
  Section 5 adds the override — this step and Section 5's
  `VulkanDevice::createOffscreenTarget()` implementation must land in the
  same compiling changeset (see Sequencing & Dependencies). The
  `submit()` doc-comment amendment has no compilation dependency and may
  be reverted independently without affecting anything else in this
  file.

### 1.4 `src/rhi/include/atlantis/rhi/command_list.h` (modify)

- **Input:** existing `transitionResource()`/`clearColor()`/
  `beginRendering()` shape as the pattern to extend; ADR-0040's exact
  chosen name (`copyRenderTargetToBuffer`, corrected during Spec 0010's
  own Human Review from an earlier `copyTextureToBuffer` misnomer —
  do not reintroduce the old name anywhere).
- **Output:** add
  ```
  virtual void copyRenderTargetToBuffer(RenderTarget& source, Buffer& destination) = 0;
  ```
  Doc comment: "copies `source`'s full, tightly-packed color image into
  `destination`; `destination` must have been created with
  `BufferPurpose::Readback` and be sized to match — a mismatch is a
  caller precondition violation, not checked here (ADR-0040). Recording
  remains legal only from inside a RenderGraph pass execution callback
  (ADR-0020), the same, unenforced-by-the-type-system convention every
  other `CommandList` method already follows."
- **Dependency order:** after 1.1.
- **Tests after this step:** none directly; covered once Section 2's
  `FakeCommandList` and Section 6's `VulkanCommandList` implement it.
- **Stop condition / rollback:** revert this file; both concrete
  `CommandList` implementations (`VulkanCommandList`,
  `tests/render_graph/fake_command_list.h`'s `FakeCommandList`) fail to
  compile until Sections 2.3/6.5 add the override — must land in the
  same compiling changeset as those.

### 1.5 `src/rhi/src/types.cpp` (modify)

- **Input:** existing `operator==` implementations for
  `TextureCreateParams`/`BufferCreateParams`/`ClearColorValue`/`Extent2D`
  as the pattern.
- **Output:** add `operator==(const OffscreenTargetCreateParams&, const
  OffscreenTargetCreateParams&)`, field-wise comparison, matching the
  existing style exactly.
- **Dependency order:** after 1.1; must land in the same changeset (the
  header declares it, this file defines it).
- **Tests after this step:** exercised by 1.1's new
  `types_tests.cpp` case.
- **Stop condition / rollback:** revert alongside 1.1.

### 1.6 `src/rhi/include/atlantis/rhi/submission_signal.h` (modify) — stale comment only

- **Input:** `SubmissionSignal`'s existing class-level doc comment,
  which states the same "a caller must call `present()` ... before
  calling `submit()` again" precondition 1.3's own amendment corrects on
  `Device::submit()`'s side — found by reading this file directly during
  the final Plan Review round, not assumed from 1.3's own finding alone.
- **Output:** amend this comment to match 1.3's corrected wording exactly
  (a windowed-only precondition; a headless caller that never constructs
  a `Presentation` is exempt, safety instead coming from `submit()`'s own
  fence-wait, per ADR-0038) — both comments must state the same rule
  consistently, since both describe the same underlying contract from
  two different vantage points (`Device::submit()`'s caller-facing view;
  `SubmissionSignal`'s own token-lifetime view). **Comment-only**: no
  method, field, or runtime behavior of `SubmissionSignal` changes —
  it remains the same opaque, no-public-method token type; a headless
  `submit()` call still receives a `VulkanSubmissionSignal` wrapping
  `VK_NULL_HANDLE` exactly as Section 6.2 describes, and this token is
  simply never passed to `present()` in that case (headless never calls
  it), same as today.
- **Dependency order:** none — independent of every other step; may land
  in any order relative to 1.1–1.5.
- **Tests after this step:** none; comment-only, no observable behavior
  to test.
- **Stop condition / rollback:** revert this file's comment alone; no
  cascading effect on any other file.

---

## 2. RenderGraph — `ResourceBinding` Extension and `execute()` Algorithm

**Spec Requirement:** "RenderGraph execution generalization for
non-presentable and chained bindings." **ADR:**
[ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).

### 2.1 `src/render_graph/include/atlantis/render_graph/execution.h` (modify)

- **Input:** current `ResourceBinding` struct (5 fields: `resource`,
  `target`, `colorClear`, `depthTexture`, `depthClear`).
- **Output:** add two fields, meaningful only when `target != nullptr`
  (same conditional-applicability pattern `colorClear`/`depthClear`
  already establish for `target`-vs-`depthTexture`):
  ```
  struct ResourceBinding {
    CompiledResourceId resource;
    atlantis::rhi::RenderTarget* target = nullptr;
    atlantis::rhi::ClearColorValue colorClear{};
    atlantis::rhi::Texture* depthTexture = nullptr;
    float depthClear = 1.0f;
    atlantis::rhi::ResourceState incomingState = atlantis::rhi::ResourceState::Undefined;
    std::optional<atlantis::rhi::ResourceState> finalState;
  };
  ```
  Doc comment states, verbatim to ADR-0039's own Decision: `incomingState`'s
  default is safe only for a resource's first use within its
  `CommandList`; `finalState`'s `std::nullopt` means no trailing
  transition beyond the last pass's own declared state; both fields are
  ignored for a `depthTexture`-shaped entry. **`finalState` has no
  default member initializer that a `target`-shaped call site can rely
  on implicitly being "correct"** — `std::optional`'s own default
  (`std::nullopt`) is the type-level default, but every `target`-shaped
  binding construction site in this codebase (Sections 3.2, 4.1, 4.2)
  must supply an explicit value as a matter of this plan's own required
  call-site updates, not because the language forces it syntactically.
- **Dependency order:** after Section 1.1 (`ResourceState::TransferSource`
  exists, referenced by later steps' usage, though this header change
  itself only needs the pre-existing `ResourceState` type).
- **Tests after this step:** none directly; exercised by 2.4.
- **Stop condition / rollback:** revert this file; `execution.cpp` (2.2),
  every existing `ResourceBinding{...}` construction site (`renderer.cpp`,
  `frame_execution_demo/main.cpp`) will fail to compile once `finalState`
  is treated as required by convention — see Sequencing & Dependencies
  for why 2.1/2.2/3.2/4.1 land as one atomic, compiling changeset.

### 2.2 `src/render_graph/src/execution.cpp` (modify)

- **Input:** current `execute()` body (`src/render_graph/src/execution.cpp`),
  specifically: the per-usage loop's `const ResourceState previous =
  currentState.count(key) ? currentState[key] : ResourceState::Undefined;`
  line, and the trailing loop's hardcoded
  `commandList.transitionResource(*binding.target, it->second,
  ResourceState::PresentSource);`.
- **Output:**
  - Per-usage loop: replace the hardcoded `ResourceState::Undefined`
    fallback with `binding->incomingState` (the already-resolved
    `binding` pointer from the existing `findBinding()` call earlier in
    the same loop body — no new lookup needed).
  - Trailing loop: replace the unconditional `PresentSource` target with
    `binding.finalState`; the existing `if (it->second != <target>)`
    guard becomes `if (binding.finalState.has_value() && it->second !=
    *binding.finalState)`; when `finalState` is `std::nullopt`, skip
    entirely (no trailing call for this binding).
  - No other line in `execute()` changes — Guard 0/1/2, the draw-pass
    recognition rule (`isDrawPass()`), and the per-pass
    `transitionResource()`/`beginRendering()` logic are untouched.
- **Dependency order:** after 2.1, same changeset.
- **Tests after this step:** see 2.4.
- **Stop condition / rollback:** revert alongside 2.1; `execute()`'s
  observable behavior for every existing (`target`-only, no new fields
  populated) call site must be bit-for-bit unchanged once callers supply
  `incomingState = Undefined` (the default) and `finalState =
  PresentSource` explicitly — verified by 2.4/9's regression tests, not
  merely asserted.

### 2.3 `tests/render_graph/fake_command_list.h` (modify)

- **Input:** existing `FakeCommandList`/`RecordedClear`-shaped pattern.
- **Output:** add
  ```
  struct RecordedCopyToBuffer {
    const atlantis::rhi::RenderTarget* source;
    const atlantis::rhi::Buffer* destination;
  };
  ```
  and a `copyRenderTargetToBuffer(RenderTarget&, Buffer&)` override
  recording into a new `std::vector<RecordedCopyToBuffer>
  copiesToBuffer;` plus `EventKind::CopyToBuffer` in the existing
  interleaved `events` vector — exact mirror of `clearColor()`'s own
  recording shape.
- **Dependency order:** after Section 1.4 (the pure-virtual method must
  exist for `FakeCommandList` to override it) — must land in the same
  compiling changeset as 1.4.
- **Tests after this step:** exercised by 2.4 and Section 3's Renderer
  tests.
- **Stop condition / rollback:** revert alongside 1.4.

### 2.4 New: `tests/render_graph/headless_binding_tests.cpp`

- **Input:** `FakeCommandList` (2.3), `RenderGraphBuilder`/`execute()`
  (2.1/2.2) — GPU-independent, per
  [testing-strategy.md](../docs/process/testing-strategy.md) layer 1.
- **Output:** new Catch2 test file, tag `[render_graph][headless]`,
  covering — precisely, per Spec 0010's own Testing & Verification Plan
  bullets — every case listed there for this layer:
  1. A binding with no `incomingState` supplied seeds tracking from
     `Undefined` — zero behavior change (regression-style assertion
     against the existing, already-covered windowed shape).
  2. A binding with an explicit `incomingState` seeds tracking from that
     value instead.
  3. `execute()` inserts a trailing transition to a supplied `finalState`
     when it differs from the resource's ending state.
  4. `execute()` inserts no trailing transition when `finalState` is
     `std::nullopt`.
  5. `execute()` inserts **no** transition at all (not even the
     per-usage one) when a resource's `incomingState` already equals its
     one usage's declared state — the specific case the headless copy
     pass relies on.
  6. A pass declaring a single `writes()` usage tagged `TransferSource`
     does not trigger `isDrawPass()`/attachment-scoping.
  7. Guard 1/Guard 2 continue to hold, exercised against bindings that do
     and do not populate the new fields.
  8. A resource never used by any pass still produces no transition
     regardless of `incomingState`/`finalState` (existing behavior,
     confirmed unaffected).
  9. **Added once 2.3 lands (see below), not before:** a pass declaring
     a `writes()` usage tagged `TransferSource`, whose execution callback
     calls `commandList.copyRenderTargetToBuffer(...)`, records exactly
     that call in `FakeCommandList.copiesToBuffer` — confirming the
     copy pass's callback shape this spec's design relies on, not just
     the transition/recognition mechanics items 1–8 already cover.
- **Dependency order:** items 1–8 need only 2.1, 2.2 — every pass
  callback they exercise is a trivial/no-op lambda, so none of them
  requires `FakeCommandList::copyRenderTargetToBuffer()` (2.3) to exist;
  **this corrects an earlier draft of this plan, which claimed a false
  dependency on 2.3 for the whole file.** Item 9 alone depends on 2.3,
  and therefore transitively on Section 1.4 — see the revised
  "Sequencing & Dependencies" section for exactly where this file's two
  logical parts (items 1–8 vs. item 9) land.
- **Tests after this step:** items 1–8 run via `ctest -LE gpu` once
  wired into the build, immediately after 2.1/2.2 land; item 9 runs once
  2.3 additionally lands.
- **Stop condition / rollback:** revert this file alone; does not affect
  production code.

---

## 3. Renderer — `finalColorState` Parameter

**Spec Requirement:** "`Renderer::drawFrame()`'s new `finalColorState`
parameter." **ADR:**
[ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Accepted Amendment.

### 3.1 `src/renderer/include/atlantis/renderer/renderer.h` (modify)

- **Input:** current `drawFrame()` signature (`CommandList&,
  RenderTarget&, Texture&, Buffer&, std::span<const DrawItem>`).
- **Output:** append one required parameter (last position, matching
  the Amendment's own candidate signature):
  ```
  void drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
                 atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
                 std::span<const DrawItem> drawItems, atlantis::rhi::ResourceState finalColorState);
  ```
  Doc comment addition, adjacent to the existing per-parameter
  description: "`finalColorState`: required, backend-agnostic — the
  state `colorTarget` must be left in when this call returns. Passed
  through unmodified as the internal draw pass's trailing-transition
  target; `Renderer` does not interpret, validate, or branch on this
  value, and gains no knowledge of why the caller chose it (ADR-0022
  Accepted Amendment). A windowed caller supplies
  `ResourceState::PresentSource`; a headless caller supplies
  `ResourceState::TransferSource` directly."
- **Dependency order:** after Section 1.1 (`ResourceState` already
  exists; no new dependency beyond what `renderer.h` already includes).
- **Tests after this step:** see 3.3.
- **Stop condition / rollback:** revert this file; `renderer.cpp` (3.2)
  and every existing `drawFrame()` call site (Section 4) fail to compile
  — same atomic-changeset note as Section 2.

### 3.2 `src/renderer/src/renderer.cpp` (modify)

- **Input:** current internal `bindings` construction
  (`renderer.cpp`, the `std::vector<ResourceBinding> bindings{...}`
  positional-aggregate-init block).
- **Output:** thread `finalColorState` through as the color entry's
  `finalState`; switch to designated initializers for clarity now that
  the struct has seven fields (candidate, confirmable at Plan Review —
  positional init remains legal but is markedly less readable at this
  field count):
  ```
  const std::vector<atlantis::render_graph::ResourceBinding> bindings{
      {.resource = compileResult.value().resourceAt(0),
       .target = &colorTarget,
       .colorClear = kBackgroundClearColor,
       .finalState = finalColorState},
      {.resource = compileResult.value().resourceAt(1),
       .depthTexture = &depthTarget,
       .depthClear = 1.0f},
  };
  ```
  (`incomingState` omitted on both entries — its default, `Undefined`,
  is correct: this is always the first, and only, `execute()` call
  touching these two resources within `Renderer`'s own internal graph.)
  Update `drawFrame()`'s own parameter list to match 3.1.
- **Dependency order:** after 3.1, 2.1 (both structs it constructs from
  must already have the new fields) — same atomic changeset.
- **Tests after this step:** see 3.3.
- **Stop condition / rollback:** revert alongside 3.1.

### 3.3 `tests/renderer/renderer_ownership_tests.cpp` (modify)

- **Input:** existing single `drawFrame()` call site
  (`"Renderer::drawFrame() records a full bind/draw sequence..."` case).
- **Output:**
  - Update the existing call to supply
    `atlantis::rhi::ResourceState::PresentSource` as the new argument —
    the case's existing assertions (bind/draw sequence, push-constant
    data) are otherwise unaffected and must still pass unmodified.
  - Add a new case, `[renderer][final_color_state]`: call `drawFrame()`
    twice against a fresh `FakeCommandList` each time — once with
    `finalColorState = PresentSource`, once with `TransferSource` — and
    assert (a) the two calls' recorded event sequences are identical in
    every respect *except* the final `transitions` entry's `after`
    field, confirming `Renderer` does not branch on the value, and (b)
    that final entry's `after` equals the value each call supplied.
- **Dependency order:** after 3.1, 3.2 only. **Corrected during the
  final Plan Review round:** an earlier draft of this plan claimed this
  step also needed 2.3 (`FakeCommandList::copyRenderTargetToBuffer()`)
  "for full assertion coverage" — this was the same class of false
  dependency Section 2.4 already found and corrected for its own items
  1–8, just not yet applied here. On inspection, the new
  `[renderer][final_color_state]` case calls `drawFrame()` and asserts
  only against `FakeCommandList`'s recorded `transitions` vector;
  `Renderer::drawFrame()`'s own internal graph (`renderer.cpp`) never
  calls `copyRenderTargetToBuffer()` — that call is only ever made by a
  caller-composed copy pass *outside* `Renderer`, per Spec 0010's own
  flow (Section 7.1) — so nothing in this test case's code path touches
  2.3. This step therefore lands entirely within Step 3 (Sequencing &
  Dependencies), not Step 4, with no dependency on any `Device`/
  `CommandList` interface addition or the `FakeCommandList` override
  those additions require.
- **Tests after this step:** this *is* the test step.
- **Stop condition / rollback:** revert this file alone.

---

## 4. Mechanical Call-Site Updates (existing windowed examples)

**Spec Requirement:** "Every existing call site that constructs a
`target`-shaped `ResourceBinding` must be mechanically updated..."
**ADR:**
[ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
(enumerates these three sites; the third is Section 3.2 above).

### 4.1 `examples/frame_execution_demo/main.cpp` (modify)

- **Input:** `recordOneClearPass()`'s existing
  `const std::vector<ResourceBinding> bindings{{compileResult.value().resourceAt(0),
  &target}};` (line 107 as of this plan's research).
- **Output:** `{.resource = compileResult.value().resourceAt(0),
  .target = &target, .finalState = atlantis::rhi::ResourceState::PresentSource}`
  — the exact value `execute()`'s old hardcoded behavior already
  produced; zero intended behavior change.
- **Dependency order:** after 2.1, 2.2.
- **Tests after this step:** none automated (this demo has no test
  suite of its own); covered by Section 9's manual windowed-regression
  verification.
- **Stop condition / rollback:** revert this file alone; does not affect
  any other target's compilation.

### 4.2 `examples/minimal_renderer_demo/main.cpp` (modify)

- **Input:** the existing `renderer.drawFrame(*commandList, *target,
  *depthTexture, *cameraBuffer, drawItems);` call (line 504 as of this
  plan's research).
- **Output:** `renderer.drawFrame(*commandList, *target, *depthTexture,
  *cameraBuffer, drawItems, atlantis::rhi::ResourceState::PresentSource);`
  — same zero-intended-behavior-change rationale as 4.1.
- **Dependency order:** after 3.1, 3.2.
- **Tests after this step:** none automated; covered by Section 9.
- **Stop condition / rollback:** revert this file alone.

---

## 5. Vulkan Backend — Shared Private `RenderTarget` Access, `OffscreenTarget` Owner, and Borrowed `RenderTarget`

**Spec Requirement:** "Offscreen `RenderTarget` construction and
ownership" (Vulkan Backend implementation shape). **ADR:**
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).

**Must Fix, resolved in this revision:** an earlier draft of this plan
(Section 6.1, then) found that `VulkanDevice::submit()`'s shipped code
unconditionally `static_cast`s its `RenderTarget&` argument to
`VulkanRenderTarget&`, and proposed fixing that one call site. A Plan
Review round checked the rest of `src/vulkan_backend/src/` and found
**four**, not one, unconditional casts of this exact shape:

```
vulkan_command_list.cpp:50   transitionResource(RenderTarget&, ...)
vulkan_command_list.cpp:91   clearColor(RenderTarget&, ...)
vulkan_command_list.cpp:106  beginRendering(RenderTarget&, ...)
vulkan_device.cpp:521        submit()
```

A fifth (`vulkan_presentation.cpp:607`, inside
`Presentation::present()`) is confirmed **not** in scope: a headless
composition never constructs a `Presentation` at all (Section 7.1), so
`present()` is never called with an offscreen-vended `RenderTarget` —
per [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md),
`OffscreenTarget` has no `present()` counterpart, and this plan does not
add one. `Presentation::present()`'s implementation is **not modified**
by this plan.

`transitionResource()` and `beginRendering()` are, in practice, hit
*before* `submit()` on every single headless draw: `Renderer::drawFrame()`'s
own internal `execute()` call invokes `beginRendering()` for its draw
pass and (once `finalColorState = TransferSource` is supplied, per
Section 3) `transitionResource()` for its trailing transition,
regardless of origin. Fixing `submit()` alone, as an earlier draft of
this plan did, would still leave the headless demo (Section 7.1)
crashing/exhibiting undefined behavior on its very first
`Renderer::drawFrame()` call — this is a corrected, not merely
clarified, scope.

### 5.1 New: `src/vulkan_backend/src/vulkan_render_target_access.h`

- **Input:** the four call sites above; `vulkan_render_target.h`'s
  existing `image()`/`imageView()`/`acquireCompleteSemaphore()`/
  `renderFinishedSemaphore()` accessors as the exact shape to
  generalize (all already exist on `VulkanRenderTarget`, documented
  there as "Accessors below exist solely for `VulkanCommandList`...
  and `VulkanDevice::submit()`/`VulkanPresentation::present()`").
- **Output:** a private, Vulkan-Backend-only, pure-abstract interface —
  candidate name `VulkanRenderTargetAccess` — implemented by **both**
  concrete `atlantis::rhi::RenderTarget` implementations
  (`VulkanRenderTarget`, `VulkanOffscreenRenderTarget`), alongside (not
  instead of) each one's own public `atlantis::rhi::RenderTarget`
  inheritance:
  ```
  class VulkanRenderTargetAccess {
   public:
    virtual ~VulkanRenderTargetAccess() = default;

    [[nodiscard]] virtual VkImage image() const noexcept = 0;
    [[nodiscard]] virtual VkImageView imageView() const noexcept = 0;

    // VK_NULL_HANDLE is a legal, explicit value of this contract --
    // "nothing to wait on" / "nothing meaningful to signal" -- not an
    // error state and not something a caller may substitute a
    // fabricated valid handle for. VulkanRenderTarget never returns
    // VK_NULL_HANDLE here (a real swapchain image always has both
    // semaphores); VulkanOffscreenRenderTarget always does (ADR-0038:
    // "a signal that is always-already-satisfied").
    [[nodiscard]] virtual VkSemaphore acquireCompleteSemaphore() const noexcept = 0;
    [[nodiscard]] virtual VkSemaphore renderFinishedSemaphore() const noexcept = 0;
  };
  ```
  This is the **one, shared** boundary every one of the four call sites
  in Section 5/6 below casts through — not a per-call-site or
  per-accessor-pair mechanism. `image()`/`imageView()` cover
  `transitionResource()`/`clearColor()`/`beginRendering()`/
  `copyRenderTargetToBuffer()`; the two semaphore accessors cover
  `submit()` alone.
- **RTTI dependency, disclosed, no new build option:** every call site
  below uses `dynamic_cast` (checked), not `static_cast`, against this
  interface — safe and correct only because `atlantis::rhi::RenderTarget`
  already has a virtual destructor (confirmed:
  `src/rhi/include/atlantis/rhi/render_target.h`) and is therefore
  already polymorphic, and because this project's Windows/MSVC
  toolchain enables RTTI by default (`/GR`, not currently overridden
  anywhere in this repository's CMake configuration — confirmed by
  inspection at Plan-Review time; this plan does not add, remove, or
  otherwise touch any compiler/RTTI-related CMake option). No new
  `CMakeLists.txt` flag is introduced for this.
- **`dynamic_cast` form, corrected during the final Plan Review round:
  pointer-form, never reference-form.** A mismatched concrete type at
  any of these five call sites is a **programmer error** (a
  `RenderTarget`/`Buffer` argument this module itself did not produce —
  ADR-0014), not a recoverable runtime condition — so it must fail via
  this codebase's existing `ATLANTIS_CHECK_MSG` assertion mechanism, the
  same tier as every other precondition check in this file, **never**
  via a thrown C++ exception. Reference-form `dynamic_cast<T&>` throws
  `std::bad_cast` on a failed cast; this repository does not disable
  exceptions anywhere (`git grep` for `/EHs-`/`/GR-` across every
  `CMakeLists.txt` returns nothing, so MSVC's default `/EHsc` applies and
  such a throw would compile and actually propagate) — letting that
  exception unwind through `VulkanCommandList`/`VulkanDevice` methods
  that are reached from `Renderer::drawFrame()`'s own call chain would
  violate [AGENTS.md](../AGENTS.md)'s explicit "keeps the render path
  exception-free" / "programmer errors are assertions, not error
  returns" rules (Error handling section). Every call site below
  therefore uses the **pointer**-form:
  `dynamic_cast<VulkanRenderTargetAccess*>(&target)` (or the
  `const VulkanRenderTargetAccess*` form where the surrounding method
  parameter is itself `const`), immediately followed by
  `ATLANTIS_CHECK_MSG(access != nullptr, "...")` before any
  `access->...` member access — never a bare reference-form cast. This
  is not a new pattern this plan invents: it is a direct, exact mirror
  of this codebase's own existing precedent at
  `src/vulkan_backend/src/vulkan_presentation.cpp:651`
  (`auto* vulkanDevice = dynamic_cast<detail::VulkanDevice*>(&device);
  ATLANTIS_CHECK_MSG(vulkanDevice != nullptr, "createPresentation()
  received a Device not produced by this module's own createDevice()");`)
  — confirmed by reading that file, not assumed. RTTI itself is
  unchanged by this correction (still relies on the same existing
  `/GR` default, still no new build option); only the cast's *failure
  semantics* change, from an uncaught-exception risk to this codebase's
  ordinary, already-established assertion-based failure mode.
- **Dependency order:** first within Section 5/6 — every other step in
  both sections depends on this interface existing.
- **Tests after this step:** none directly (pure interface); exercised
  by every later step's own tests, cumulatively, once both concrete
  implementations exist (5.3) and all four call sites adopt it (5.4,
  6.1, 6.2).
- **Stop condition / rollback:** revert this file; nothing compiles
  against it until 5.3–6.2 reference it, so it is safe to revert alone
  at this point in the sequence, before those steps land.

### 5.2 `src/vulkan_backend/src/vulkan_render_target.h` / `.cpp` (modify)

- **Input:** current `VulkanRenderTarget final : public
  atlantis::rhi::RenderTarget` declaration and its existing four
  accessors.
- **Output:** `class VulkanRenderTarget final : public
  atlantis::rhi::RenderTarget, public VulkanRenderTargetAccess` — an
  inheritance-list-only change. `image()`/`imageView()`/
  `acquireCompleteSemaphore()`/`renderFinishedSemaphore()`'s existing
  bodies are **unchanged**; only their role (now also satisfying
  `VulkanRenderTargetAccess`, in addition to being this class's own
  plain public methods as today) changes. No behavior change for any
  existing windowed caller.
- **Dependency order:** after 5.1.
- **Tests after this step:** none directly; a compile-time check (this
  class must still satisfy both interfaces) exercised the moment 5.4/6.2
  are also in place and the windowed GPU test suite (Section 9) re-runs.
- **Stop condition / rollback:** revert this file alone; every existing
  caller of `VulkanRenderTarget`'s four accessors keeps compiling and
  behaving identically either way, since the methods themselves are
  untouched.

### 5.3 New: `src/vulkan_backend/src/vulkan_offscreen_target.h` / `.cpp` and `src/vulkan_backend/src/vulkan_offscreen_render_target.h` / `.cpp`

Two classes, presented together because their constructors/members must
be mutually consistent (a Should-Fix from Plan Review corrected the
mismatch an earlier draft had between them):

**`VulkanOffscreenTarget final : public atlantis::rhi::OffscreenTarget`**
(owning; direct structural precedent: `vulkan_texture.h`/`.cpp`):

- Owns `VkDevice device_` (borrowed, not owned — outlives this object,
  caller-enforced, same tier as every other Vulkan Backend type),
  `VkImage image_`, `VkDeviceMemory memory_`, `VkImageView imageView_`,
  `Extent2D extent_`, `Format format_`, `bool outstandingBorrow_ =
  false;`.
- Public (Vulkan-Backend-internal-only, same documentation convention as
  `VulkanRenderTarget`'s own "Accessors below exist solely for...")
  accessors: `image()`, `imageView()`, `extent()`, `format()` — these
  back `VulkanOffscreenRenderTarget`'s own `VulkanRenderTargetAccess`/
  `RenderTarget` implementations below; `VulkanOffscreenTarget` itself
  does **not** implement `atlantis::rhi::RenderTarget` or
  `VulkanRenderTargetAccess` — it is the owner, never itself vended as a
  borrow.
  ```
  VulkanOffscreenTarget(VkDevice device, VkImage image, VkDeviceMemory memory, VkImageView imageView,
                         atlantis::rhi::Extent2D extent, atlantis::rhi::Format format);
  ```
- `acquireTarget()`: `ATLANTIS_CHECK_MSG(!outstandingBorrow_, "OffscreenTarget::acquireTarget() called while a previously-vended borrow is still outstanding");`
  then `outstandingBorrow_ = true;` then
  `return Ok(std::make_unique<VulkanOffscreenRenderTarget>(this));` — a
  **single-argument** constructor call, consistent with the borrowed
  type's own member list below. No `Err` path is actually reachable in
  this round's implementation (unchanged finding, still flagged in
  Section 1.1 and Human Review / Plan Review Blockers).
- Destructor: `ATLANTIS_CHECK_MSG(!outstandingBorrow_, "VulkanOffscreenTarget destroyed while a vended borrow is still outstanding");`
  then releases `imageView_`/`memory_`/`image_` in that order (mirrors
  `VulkanTexture::~VulkanTexture()` exactly). **Does not** call
  `vkDeviceWaitIdle()` or wait on any fence — per ADR-0038, deliberately
  the same "destructor does not itself wait" tier as
  `VulkanPresentation::~VulkanPresentation()`.
- `void clearOutstandingBorrow() noexcept { outstandingBorrow_ = false; }`
  — called only by `VulkanOffscreenRenderTarget`'s destructor (a
  `friend` relationship, or a narrow accessor — candidate, confirmable
  at Plan Review, unchanged open point).

**`VulkanOffscreenRenderTarget final : public atlantis::rhi::RenderTarget, public VulkanRenderTargetAccess`**
(non-owning; direct structural precedent: `vulkan_render_target.h`/`.cpp`)
— **corrected to hold no duplicate handle members**, per Plan Review:
delegates every accessor to its owner rather than copying `image_`/
`imageView_`/`extent_`/`format_` into itself:

```
class VulkanOffscreenRenderTarget final : public atlantis::rhi::RenderTarget,
                                            public VulkanRenderTargetAccess {
 public:
  explicit VulkanOffscreenRenderTarget(VulkanOffscreenTarget* owner) : owner_(owner) {}
  ~VulkanOffscreenRenderTarget() override { owner_->clearOutstandingBorrow(); }

  VulkanOffscreenRenderTarget(const VulkanOffscreenRenderTarget&) = delete;
  VulkanOffscreenRenderTarget& operator=(const VulkanOffscreenRenderTarget&) = delete;
  VulkanOffscreenRenderTarget(VulkanOffscreenRenderTarget&&) = delete;
  VulkanOffscreenRenderTarget& operator=(VulkanOffscreenRenderTarget&&) = delete;

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return owner_->extent(); }
  [[nodiscard]] atlantis::rhi::Format format() const override { return owner_->format(); }
  [[nodiscard]] VkImage image() const noexcept override { return owner_->image(); }
  [[nodiscard]] VkImageView imageView() const noexcept override { return owner_->imageView(); }
  [[nodiscard]] VkSemaphore acquireCompleteSemaphore() const noexcept override { return VK_NULL_HANDLE; }
  [[nodiscard]] VkSemaphore renderFinishedSemaphore() const noexcept override { return VK_NULL_HANDLE; }

 private:
  VulkanOffscreenTarget* owner_;  // non-owning; must outlive this borrow --
                                   // enforced by the outstanding-borrow
                                   // contract (ADR-0038), not the type system
};
```

The constructor, member list, and every accessor's body above are shown
in full and are mutually consistent — no field is declared that isn't
either used in the constructor or trivially derived from `owner_`.
Destroying this object has **no Vulkan side effect** (non-owning) beyond
the single `clearOutstandingBorrow()` call — the entire RAII-return
contract from
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).

- **Dependency order:** after 5.1 (needs `VulkanRenderTargetAccess`) and
  Section 1.2 (needs the `atlantis::rhi::OffscreenTarget` interface).
  Both classes land together — `VulkanOffscreenTarget::acquireTarget()`
  constructs a `VulkanOffscreenRenderTarget`; that type's destructor
  calls back into `VulkanOffscreenTarget` — via a forward declaration in
  whichever header is compiled first.
- **Tests after this step:** GPU-required, see Section 7.2 (no
  GPU-independent test is possible — both types' entire purpose is
  owning/borrowing real Vulkan objects).
- **Stop condition / rollback:** revert this file pair; nothing else
  compiles against it until `VulkanDevice::createOffscreenTarget()`
  (5.4) references it.

### 5.4 `src/vulkan_backend/src/vulkan_device.h` / `.cpp` (modify) — `createOffscreenTarget()`

- **Input:** `VulkanDevice::createTexture()`'s exact implementation
  pattern (`vulkan_device.cpp`: `vkCreateImage` →
  `vkGetImageMemoryRequirements` → `selectMemoryTypeIndexForDevice`
  (`VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`) → `vkAllocateMemory` →
  `vkBindImageMemory` → `vkCreateImageView`), adapted for a color,
  transfer-source-capable image instead of a depth-attachment one.
- **Output:** `VulkanDevice::createOffscreenTarget(const
  OffscreenTargetCreateParams& params)`:
  - `VkImageCreateInfo`: `format = toVkFormat(params.format)` (existing
    `Format → VkFormat` helper, already used for swapchain format
    selection — reused verbatim, not reimplemented); `usage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT`
    (color-attachment for the draw pass, transfer-source for the
    readback copy — both required, per Spec 0010 Requirements' "GPU
    memory allocation" and ADR-0040's copy contract); other fields
    identical in shape to `createTexture()`'s (`VK_IMAGE_TYPE_2D`, 1 mip,
    1 array layer, `VK_SAMPLE_COUNT_1_BIT`, `VK_IMAGE_TILING_OPTIMAL`,
    `VK_SHARING_MODE_EXCLUSIVE`, `VK_IMAGE_LAYOUT_UNDEFINED` initial).
  - Memory: `VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` (matching
    `createTexture()`'s depth-image choice — an offscreen color target
    is GPU-written, GPU-read (by the copy), never CPU-mapped directly;
    only the separate readback `Buffer`, Section 6.3, is host-visible).
  - `VkImageViewCreateInfo`: `VK_IMAGE_VIEW_TYPE_2D`,
    `VK_IMAGE_ASPECT_COLOR_BIT` subresource range (mirrors
    `createTexture()`'s view creation, substituting the color aspect for
    depth).
  - Every `VkResult` checked; every failure path releases whatever was
    already created before returning
    `Err(OffscreenTargetCreateError::...)`, exact mirror of
    `createTexture()`'s own cleanup-on-failure ordering.
  - On success: `Ok(std::make_unique<VulkanOffscreenTarget>(device_,
    image, memory, imageView, params.extent, params.format))`.
- **Dependency order:** after 1.1, 1.3, 5.3.
- **Tests after this step:** see 7.2.
- **Stop condition / rollback:** revert this method's body alone (the
  declaration from 1.3 stays; `VulkanDevice` remains abstract/
  uninstantiable until re-added) — this is the natural rollback
  boundary since 1.3's interface addition and this override cannot be
  separated in a compiling repository regardless (see the revised
  "Sequencing & Dependencies" section).

---

## 6. Vulkan Backend — Adopting the Shared Access Boundary at All Four Call Sites, and Readback Capability

**Spec Requirement:** "GPU-to-CPU readback capability,"
"RenderGraph execution generalization... Vulkan Backend impact."
**ADR:**
[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md),
[ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).

### 6.1 `src/vulkan_backend/src/vulkan_command_list.cpp` (modify) — `transitionResource()`, `clearColor()`, `beginRendering()`

**Must Fix — the core of this revision.** All three methods currently do
`auto& vulkanTarget = static_cast<VulkanRenderTarget&>(target);` (or
`color`, for `beginRendering()`) at their very first line
(`vulkan_command_list.cpp:50,91,106`). Each becomes, **pointer-form,
exception-free** (corrected during the final Plan Review round — see
5.1's own "`dynamic_cast` form" note for the full rationale and the
`vulkan_presentation.cpp:651` precedent this mirrors):

```
auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&target);  // or &color
ATLANTIS_CHECK_MSG(access != nullptr,
                    "transitionResource() received a RenderTarget not produced by this module");
```

and every subsequent use of `vulkanTarget.image()`/`vulkanTarget.imageView()`
in that method's body becomes `access->image()`/`access->imageView()` —
no other line in any of the three methods changes. Each method's
`ATLANTIS_CHECK_MSG` message names that specific method (`clearColor()`,
`beginRendering()`), matching the existing per-call-site wording
convention `vulkan_presentation.cpp:651`/`vulkan_device.cpp` already use
elsewhere in this codebase. A failed cast here is a **programmer
error** — a `RenderTarget` reference this module itself did not
construct (ADR-0014) — caught by this assertion, never by a thrown
`std::bad_cast` propagating out of the render path. `beginRendering()`'s
own `vulkanTarget.extent()` call becomes `color.extent()` (called
directly on the public `RenderTarget&` parameter — `extent()` is part of
the public interface, already polymorphic, needs no cast).

- **`clearColor()` is fixed even though this spec's own headless path
  never calls it** (Spec 0010's own worked example uses
  `Renderer::drawFrame()`'s `beginRendering()`/draw-call path and the
  copy pass's `copyRenderTargetToBuffer()`, never `clearColor()`).
  Leaving `clearColor()`'s unconditional cast unfixed would leave a
  latent, identical-shape undefined-behavior trap for any future caller
  (e.g. a future Image Regression Testing spec, or a hypothetical future
  headless `clearColor()`-only test) that legitimately calls it against
  an offscreen target — this plan fixes all three `VulkanCommandList`
  methods uniformly, not just the two this spec's own scope currently
  exercises, for internal consistency of one already-shared,
  already-affected class.
- **Dependency order:** after 5.1, 5.2, 5.3 (needs `VulkanRenderTargetAccess`
  and both concrete implementations to exist for the `dynamic_cast` to
  be meaningful).
- **Tests after this step:** GPU-required — Section 7.2's headless cycle
  test exercises `transitionResource()`/`beginRendering()` against the
  offscreen path (both are invoked by `Renderer::drawFrame()`'s own
  internal graph); the existing windowed GPU test suite
  (`frame_execution_gpu_tests.cpp`, which exercises `clearColor()`
  directly, and `minimal_renderer_gpu_tests.cpp`, which exercises
  `transitionResource()`/`beginRendering()`) re-run unmodified (Section
  9), confirming no regression for the `VulkanRenderTarget` case.
- **Stop condition / rollback:** revert this file's three method bodies
  to their original unconditional `static_cast`; this reverts headless
  drawing entirely (the offscreen path becomes immediately unusable —
  `Renderer::drawFrame()` against an offscreen target would again be
  undefined behavior) while leaving the windowed path fully functional,
  since `VulkanRenderTarget` still satisfies `VulkanRenderTargetAccess`
  regardless of which cast form is used against it.

### 6.2 `src/vulkan_backend/src/vulkan_device.cpp` (modify) — `submit()`

- **Input:** current `submit()` body (`vulkan_device.cpp:506-550`),
  specifically line 521's `static_cast<const VulkanRenderTarget&>(target)`
  and the unconditional `waitSemaphoreCount = 1`/`signalSemaphoreCount = 1`
  `VkSubmitInfo` fields.
- **Output:**
  - **Pointer-form, exception-free** (same correction as 6.1, applied
    here for `submit()`'s own `const` parameter):
    ```
    const auto* access = dynamic_cast<const VulkanRenderTargetAccess*>(&target);
    ATLANTIS_CHECK_MSG(access != nullptr,
                        "submit() received a RenderTarget not produced by this module");
    ```
    replaces the `static_cast`; `waitSemaphore`/`signalSemaphore` are
    read from `access->acquireCompleteSemaphore()`/
    `access->renderFinishedSemaphore()` exactly as today (unchanged
    variable names/usage below that line). As in 6.1, a failed cast is a
    programmer error caught by `ATLANTIS_CHECK_MSG`, never a thrown
    exception.
  - `VkSubmitInfo` construction becomes conditional:
    `submitInfo.waitSemaphoreCount = waitSemaphore != VK_NULL_HANDLE ? 1 : 0;`,
    `submitInfo.pWaitSemaphores = waitSemaphore != VK_NULL_HANDLE ? &waitSemaphore : nullptr;`,
    `submitInfo.pWaitDstStageMask = waitSemaphore != VK_NULL_HANDLE ? &waitStage : nullptr;`
    — and symmetrically for `signalSemaphoreCount`/`pSignalSemaphores`
    against `signalSemaphore`. For a `VulkanRenderTarget` argument, both
    are always non-null (unchanged accessor bodies, per 5.2), so this
    branch is always taken exactly as the old unconditional path was —
    **byte-identical `VkSubmitInfo` contents for the windowed case,
    confirmed by Section 9's regression check, not merely asserted.**
  - `std::make_unique<VulkanSubmissionSignal>(signalSemaphore)` is
    constructed with a `VK_NULL_HANDLE` value for the headless case —
    confirmed safe by inspection of `vulkan_submission_signal.cpp`
    (`VulkanSubmissionSignal`'s constructor is a trivial, unconditional
    member-initializer store; it performs no validation and has no
    behavior contingent on the handle's validity) and by construction
    (this signal is never passed to `Presentation::present()` for a
    headless submission, since headless never constructs a
    `Presentation` — see Section 5's own framing of why
    `vulkan_presentation.cpp` needs no change).
- **Dependency order:** after 5.1, 5.2, 5.3; independent of 6.1 (a
  different file, no shared code beyond the interface both adopt) but
  conventionally landed in the same step for review coherence.
- **Tests after this step:** GPU-required — Section 7.2's headless cycle
  test exercises `submit()` against the offscreen path; existing
  windowed GPU tests re-run unmodified (Section 9).
- **Stop condition / rollback:** revert `submit()`'s body to its
  original unconditional `static_cast`/`VkSubmitInfo` construction —
  reverts headless `submit()` support specifically; `transitionResource()`/
  `beginRendering()` (6.1) would remain fixed but the offscreen path
  would still fail at `submit()` — the safest single-file rollback
  point if a GPU-verification problem is isolated specifically to
  submission.

### 6.3 `src/vulkan_backend/src/vulkan_device.cpp` — `createBuffer()` readback purpose mapping

- **Input:** existing `switch (params.purpose)` in `createBuffer()`
  mapping `BufferPurpose` to `VkBufferUsageFlags`.
- **Output:** add
  `case atlantis::rhi::BufferPurpose::Readback: usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; break;`
  — no other change to `createBuffer()`'s body; the existing
  host-visible/host-coherent memory-property selection
  (`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`)
  and always-mapped-at-creation behavior already apply uniformly to
  every purpose, per ADR-0023 — readback needs no special case there.
- **Dependency order:** after Section 1.1; independent of 5.x/6.1/6.2.
- **Tests after this step:** GPU-required (buffer creation always
  requires a real device) — Section 7.2.
- **Stop condition / rollback:** revert this one `switch` arm; does not
  affect the other three purposes.

### 6.4 `src/vulkan_backend/src/resource_state_mapping.cpp` (modify) — new `planTransition()` entry

- **Input:** existing `colorAttachmentOutputToPresentSource()` as the
  direct structural sibling (same source layout/access/stage, different
  destination).
- **Output:** add
  ```
  [[nodiscard]] ImageBarrierPlan colorAttachmentOutputToTransferSource() {
    return ImageBarrierPlan{
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
    };
  }
  ```
  and one new branch in `planTransition()`:
  `if (before == ResourceState::ColorAttachmentOutput && after == ResourceState::TransferSource) { return colorAttachmentOutputToTransferSource(); }`
  — placed alongside the existing `ColorAttachmentOutput`-involving
  branches. **This is the one and only new table entry Section
  "Critical Architectural Boundaries" above permits.**
- **Dependency order:** after Section 1.1 (`TransferSource` must exist);
  independent of 5.x/6.1/6.2/6.3.
- **Tests after this step:** `tests/vulkan_backend/resource_state_mapping_tests.cpp`
  (GPU-independent — `planTransition()` takes/returns plain values, no
  `VkDevice`) — extend with: (a) `ColorAttachmentOutput → TransferSource`
  produces the exact `ImageBarrierPlan` above without asserting; (b) a
  still-unlisted pair (e.g. `ColorAttachmentOutput → DepthAttachmentReadWrite`)
  continues to fire the existing `ATLANTIS_CHECK_MSG(false, ...)` under a
  non-terminating test handler, confirming this plan adds exactly one
  entry and no more.
- **Stop condition / rollback:** revert this one function + branch;
  fully independent of every other Section 6 step.

### 6.5 `src/vulkan_backend/src/vulkan_command_list.h` / `.cpp` (modify) — `copyRenderTargetToBuffer()`

- **Input:** `VulkanCommandList::clearColor()`'s existing implementation
  shape (one `vkCmd*` call after resolving the concrete types) as the
  pattern; ADR-0040's exact copy-region contract (full extent, tightly
  packed); `VulkanRenderTargetAccess` (5.1) for `source`'s `image()`.
- **Output:** declare the override (1.4's interface addition) and
  implement:
  ```
  void VulkanCommandList::copyRenderTargetToBuffer(atlantis::rhi::RenderTarget& source, atlantis::rhi::Buffer& destination) {
    auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&source);
    ATLANTIS_CHECK_MSG(access != nullptr,
                        "copyRenderTargetToBuffer() received a RenderTarget not produced by this module");
    auto& vulkanBuffer = static_cast<VulkanBuffer&>(destination);
    const atlantis::rhi::Extent2D sourceExtent = source.extent();

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;    // 0 = tightly packed (ADR-0040)
    region.bufferImageHeight = 0;  // 0 = tightly packed (ADR-0040)
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {sourceExtent.width, sourceExtent.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer_, access->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            vulkanBuffer.vkBuffer(), 1, &region);
  }
  ```
  **Pointer-form, exception-free** — same correction and rationale as
  6.1/6.2 (see 5.1's own "`dynamic_cast` form" note): a failed cast here
  is a programmer error caught by `ATLANTIS_CHECK_MSG`, never a thrown
  `std::bad_cast`. (`static_cast<VulkanBuffer&>` for `destination` is unchanged from
  every other `Buffer`-consuming method in this file — only one
  concrete `Buffer` implementation exists in Phase 1, ADR-0001 — this
  plan does not add a second one, so no `dynamic_cast` is needed there.)
  **No additional `vkCmdPipelineBarrier` for host-visibility is added
  here** — `destination`'s memory is host-coherent (ADR-0023), and
  `Device::waitIdle()`'s existing `vkDeviceWaitIdle()` call already
  establishes the device-to-host visibility guarantee the Vulkan
  specification's host-write-ordering rules provide for a fully-drained
  device, matching this codebase's existing precedent (the camera
  uniform `Buffer`'s own write-timing argument, ADR-0023, relies on the
  same class of guarantee without an explicit host-visibility barrier).
  **This must be confirmed empirically by Section 7.2's GPU test
  reading back real, correct pixel data — not assumed from this
  reasoning alone.**
- **Dependency order:** after 1.4, 5.1, 5.3, 6.4 (needs
  `VulkanRenderTargetAccess`, `VulkanOffscreenRenderTarget` to exist as
  a real second implementer, and the transition this copy runs after to
  already be well-defined).
- **Tests after this step:** see 7.2.
- **Stop condition / rollback:** revert this override's body alone
  (declaration stays, matching 1.4's own interface — see the revised
  "Sequencing & Dependencies" section for why 1.4's declaration and this
  override cannot be separated in a compiling repository).

---

## 7. Headless Verification Composition and GPU Tests

**Spec Requirement:** "Reuse of Renderer, RenderGraph, RHI, and Vulkan
Backend — no fork," full "Testing & Verification Plan." **ADRs:** all
four.

### 7.1 New: `examples/headless_rendering_demo/main.cpp` + `CMakeLists.txt`

- **Input:** `examples/minimal_renderer_demo/main.cpp` as the direct
  structural precedent for `Mesh`/`Material`/camera-`Buffer` setup and
  the shader-loading boilerplate; Spec 0010's own Proposed Design flow
  diagram as the authoritative sequence.
- **Output:** a new, non-shipping example executable that:
  1. Constructs a `Device` (`vulkan_backend::createDevice()`,
     `enableValidationLayers = true`) — **no `atlantis::platform::initialize()`
     call anywhere in this file** (structurally verifiable by
     `grep`/inspection per Spec 0010's own Acceptance-style check).
  2. Constructs the same fixed `Mesh`/`Material`/camera-uniform `Buffer`
     fixture `minimal_renderer_demo` uses (exact code-sharing mechanism —
     a small shared header both examples include, vs. duplicated code —
     **flagged for Plan Review**, see Human Review / Plan Review
     Blockers; this plan's default recommendation is duplication for
     this round, matching every prior example's own self-contained
     style, over introducing a new shared `examples/`-level library
     target that no prior spec's Plan needed).
  3. Constructs one `OffscreenTarget` (`device->createOffscreenTarget({.extent
     = {512, 512}, .format = Format::Rgba8Unorm})` — candidate fixed
     resolution/format, confirmable at Plan Review) and one readback
     `Buffer` (`BufferPurpose::Readback`, sized `512 * 512 * 4` bytes for
     `Rgba8Unorm`).
  4. Constructs one depth `Texture` via the existing, unchanged
     `Device::createTexture()` path, sized to match.
  5. Runs the acquire → write-camera → `createCommandList()` →
     `Renderer::drawFrame(..., ResourceState::TransferSource)` →
     caller-built copy-pass graph → `submit()` → `waitIdle()` → read →
     content-check → drop borrow → (loop or exit) cycle from Spec 0010's
     own flow diagram, verbatim.
  6. Runs this cycle more than once against the same `OffscreenTarget`
     (Spec 0010 Requirements: "may be acquired-and-borrowed more than
     once... each cycle independent") — candidate: 3 iterations, logging
     each cycle's basic-content-check result.
  7. Implements the **reproducible basic content check** (Spec 0010
     Requirements: "not uniformly one color/all-zero... a small, fixed
     set of known sample positions differ... in the direction the fixed
     mesh/camera/material fixture predicts") — candidate: read the
     center pixel (expect mesh-colored, non-background) and all four
     corner pixels (expect background-clear-colored) from the mapped
     readback `Buffer`, comparing against a small tolerance; return
     `EXIT_FAILURE` and log specifics on mismatch.
  8. On every exit path (success, any failure branch): `Device::waitIdle()`
     before destroying `OffscreenTarget`, matching ADR-0038's documented
     destruction precondition and correct-order requirement exactly —
     **this file is itself part of this plan's own verification that the
     documented order is followed in real code, not only in prose.**
- **CMakeLists.txt:** mirrors `examples/minimal_renderer_demo/CMakeLists.txt`
  exactly (same library link set — `Atlantis::Core`, `Atlantis::Platform`
  is **not** linked, matching the no-window requirement; `Atlantis::RHI`,
  `Atlantis::VulkanBackend`, `Atlantis::RenderGraph`, `Atlantis::Renderer`,
  `Atlantis::ShaderSystem`, `Atlantis::ShaderSystemRhiIntegration`; same
  `minimal_mesh_shaders` dependency and post-build shader-copy commands;
  a `run_headless_rendering_demo` custom target).
- **Root `CMakeLists.txt`:** add
  `add_subdirectory(examples/headless_rendering_demo)` alongside the
  existing four `examples/` entries.
- **Dependency order:** after every step in Sections 1-6.
- **Tests after this step:** this executable's own successful run (exit
  code 0, Validation-Layers-clean log output) *is* the manual/automated
  verification Spec 0010's Testing & Verification Plan requires; see
  Section 8 for how it is invoked as part of the verification checklist.
- **Stop condition / rollback:** revert this new directory and the one
  root `CMakeLists.txt` line; no other target depends on this example.

### 7.2 New: `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`

- **Input:** `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` and
  `tests/vulkan_backend/frame_execution_gpu_tests.cpp` as the direct
  structural precedent (GPU-required, `[gpu]`-labeled, drives Atlantis's
  own public API only, no `Vk*` type in this file).
- **Output:** new Catch2 test file, `[vulkan_backend][gpu][headless]`,
  covering — per Spec 0010's own Testing & Verification Plan's "GPU
  integration tests" bullet, precisely:
  1. Creating and destroying an `OffscreenTarget` — Validation-Layers-
     clean.
  2. Acquiring, using (a trivial submit with no draw), and returning
     (destroying the borrow) more than once across the same instance's
     lifetime — confirming a second `acquireTarget()` succeeds only
     after the first borrow is destroyed/reset.
  3. A second `acquireTarget()` called *before* returning the first
     fires the expected assertion (exercised under this codebase's
     existing non-terminating test-handler pattern for assertion
     testing — confirm the exact mechanism already used elsewhere, e.g.
     `render_graph`'s Guard tests, and reuse it, not invent a new one).
  4. Destroying `OffscreenTarget` while a borrow is still outstanding
     fires the equivalent assertion (same mechanism).
  5. Dropping a borrow immediately after `submit()` returns (before
     `waitIdle()`), followed by a correct subsequent cycle with no
     Validation Layer warning/error — confirms the minimum-borrow-
     lifetime contract in practice.
  6. Creating and destroying a readback `Buffer`.
  7. One full render-and-readback cycle
     (`Renderer::drawFrame(finalColorState = TransferSource)` → copy
     pass → `submit()` → `waitIdle()` → read), Validation-Layers-clean,
     **confirming no `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` transition is ever
     recorded** for the headless target (assert against
     `resource_state_mapping_tests.cpp`-style captured barrier data if
     feasible, or via Validation Layers' own object-tracking output —
     exact mechanism left to Implementation, confirmable at Plan
     Review). **This is the single test case that, end to end, exercises
     all four of Section 6's corrected call sites against a real
     `VulkanOffscreenRenderTarget`** — `beginRendering()`/
     `transitionResource()` (via `Renderer::drawFrame()`'s own internal
     graph), `copyRenderTargetToBuffer()` (the copy pass), and `submit()`
     — the concrete, real-hardware closure of the Must Fix Section 5/6
     resolve.
  8. A full cycle that calls `Device::waitIdle()` immediately after
     `submit()` — before dropping the borrow, before destroying
     `OffscreenTarget` — confirming the documented, correct-order flow
     is Validation-Layers-clean, with no reliance on `Device`'s own
     destructor-time drain.
- **`tests/vulkan_backend/CMakeLists.txt`:** add this file to the
  existing `atlantis_vulkan_backend_gpu_tests` executable's source list
  — the same executable the existing windowed GPU tests
  (`vulkan_presentation_gpu_tests.cpp`, `frame_execution_gpu_tests.cpp`,
  `minimal_renderer_gpu_tests.cpp`) already use — this test file needs
  the same `minimal_mesh_shaders` dependency/working-directory setup
  already present for that target, since it also exercises
  `Renderer::drawFrame()`; no new test executable target is needed.
- **Dependency order:** after every step in Sections 1-6.
- **Tests after this step:** run via `ctest -L gpu` (real GPU + real
  Vulkan-capable driver required).
- **Stop condition / rollback:** revert this file and its one
  `CMakeLists.txt` source-list entry.

---

## 8. Testing Strategy (consolidated)

Maps directly to Spec 0010's own Testing & Verification Plan; no new
testing philosophy is introduced beyond what
[testing-strategy.md](../docs/process/testing-strategy.md) already
establishes.

- **GPU-independent unit tests (layer 1, `ctest -LE gpu`):** Sections
  1.1, 2.4, 3.3, 6.3 above — new/extended Catch2 cases, zero real
  `VkDevice` anywhere.
- **GPU-required tests (Windows/Vulkan, `ctest -L gpu`):** Section 7.2
  (new headless coverage) plus the **unmodified** re-run of every
  existing GPU-required case
  (`vulkan_presentation_gpu_tests.cpp`, `frame_execution_gpu_tests.cpp`,
  `minimal_renderer_gpu_tests.cpp`) — this is this plan's regression
  gate for the windowed path (Section 9).
- **Manual/automated verification composition:** Section 7.1's
  `headless_rendering_demo` — run interactively (or scripted, since it
  needs no window/input) at least once per Debug and Release build.
- **Headless integration tests (layer 2):** Section 7.2's own test file
  **is** this layer's first instance, per Spec 0010's own framing — this
  plan does not additionally formalize a distinct CI job/label for it
  beyond the existing `gpu` CTest label (see Risks — this remains an
  open question Spec 0006/0007 already flagged and did not resolve
  either).
- **Image regression tests:** not applicable — explicitly out of this
  plan's scope (Non-Goals).
- **Vulkan Validation Layers:** mandatory, zero warnings/errors, for
  every Debug build and every GPU-touching test/demo run this plan adds
  or re-runs — no exception.
- **Debug/Release:** every GPU-required test and the new demo must be
  built and run in both configurations before this plan's Implementation
  is considered complete (matching Plan 0007's own §15 "Debug/Release"
  practice).
- **Real-GPU-unavailable disclosure:** if the environment implementing
  this plan lacks a Vulkan-capable GPU/driver at verification time, the
  Implementation PR must **explicitly state this** (not silently skip
  GPU-required coverage) and list exactly which of Section 7.2's cases
  were exercised on real hardware vs. verified by code inspection/
  GPU-independent tests only — the same disclosure discipline Spec
  0007's own Testing & Verification Plan already required for its
  Extension dynamic-rendering path and format-change case, applied here
  to the headless path's own hardware dependency.

---

## 9. Windows Windowed Path Regression Verification

**Spec Non-Goal:** "Any regression to the windowed path."

- Re-run `examples/frame_execution_demo` and
  `examples/minimal_renderer_demo` interactively after their Section 4
  mechanical updates, confirming behavior identical to their
  Spec 0006/0007-verified baseline: visible frame, correct interactive
  resize, correct minimize/restore (zero Vulkan calls while minimized),
  clean exit on every path including a deliberate mid-frame exit,
  Vulkan Validation Layers clean throughout.
- Re-run the full existing GPU-required test suite
  (`vulkan_presentation_gpu_tests.cpp`, `frame_execution_gpu_tests.cpp`,
  `minimal_renderer_gpu_tests.cpp`) unmodified, confirming identical
  pass counts to the pre-this-plan baseline.
- Confirm, by inspection, that **all four** call sites Section 5/6
  change (`transitionResource()`, `clearColor()`, `beginRendering()` —
  Section 6.1; `submit()` — Section 6.2) produce identical behavior for
  any `VulkanRenderTarget`-sourced argument to what they produced before
  this plan: the same `VkImage`/`VkImageView` values via the checked
  pointer-form `dynamic_cast<VulkanRenderTargetAccess*>` (always
  non-null for a real `VulkanRenderTarget`, so the `ATLANTIS_CHECK_MSG`
  it is paired with never fires on this path) as were previously
  obtained via `static_cast<VulkanRenderTarget&>` (guaranteed identical, since
  `VulkanRenderTarget`'s own accessor bodies are unchanged — Section
  5.2), and `submit()`'s `VkSubmitInfo` contents byte-identical
  (non-null semaphores on both sides, exactly as today, since
  `VulkanRenderTarget::acquireCompleteSemaphore()`/
  `renderFinishedSemaphore()` never return `VK_NULL_HANDLE`). The
  polymorphic-dispatch mechanism must be a behavior-preserving refactor
  for the windowed path at all four sites, not merely "probably fine" —
  and `frame_execution_gpu_tests.cpp` (exercises `clearColor()`/
  `transitionResource()`) plus `minimal_renderer_gpu_tests.cpp`
  (exercises `transitionResource()`/`beginRendering()`/`submit()`)
  re-run unmodified is the concrete evidence for this, not the
  inspection alone.

---

## 10. Documentation and Registry Post-Implementation Updates

Deferred to the Implementation PR itself (not this Plan, not a separate
PR) — matching every prior spec's own precedent
(Spec 0006/0007/0008's own Implementation PRs each updated
`specs/README.md`'s Implementation column and, where applicable,
`docs/project-blueprint.md`, as part of landing, not as a prerequisite
to Plan approval):

- `specs/README.md`: Spec 0010's row — Implementation column updated
  from "Not started" to a description of what actually shipped, mirroring
  Spec 0006/0007's own rows' level of detail (files created, PR link(s),
  verification summary, any disclosed limitations from Section 8's
  real-GPU-availability disclosure).
- `docs/project-blueprint.md`: a new milestone entry (or an update to
  the existing Section 5 roadmap), **only if** Plan Review confirms this
  is in scope for the Implementation PR — Spec 0006/0007's own PRs did
  not always bundle this; left as an explicit Implementation-time
  decision, not pre-committed here.
- `docs/architecture/*.md`: updated only if Implementation reveals a
  genuine as-built architecture fact these documents' own `PROPOSED`
  banners should reflect — not assumed necessary by this plan.

---

## Explicit Prohibitions (grep/code-review checklist)

Every item below must hold, verifiable by inspection, before this
plan's Implementation is considered complete:

- [ ] No `.spv`/reflection JSON, shader source, or Shader System change
      anywhere — this plan touches no shader.
- [ ] No `src/platform/` change, and no `atlantis::platform::*` symbol
      referenced anywhere in `examples/headless_rendering_demo/` or
      `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`.
- [ ] No `VkSwapchainKHR`, `VkSurfaceKHR`, or `Presentation`/
      `VulkanPresentation` reference anywhere in the same two files.
- [ ] No second `planTransition()` entry beyond
      `ColorAttachmentOutput → TransferSource`.
- [ ] No new `Texture`/`DepthFormat` usage kind, method, or field.
- [ ] No new third-party CMake `find_package`/`FetchContent` call.
- [ ] No `VkRenderPass`/`VkFramebuffer` object anywhere (unaffected by
      this plan; confirmed still absent).
- [ ] No public `release()`/`consume()` method on `OffscreenTarget` or
      any `RenderTarget` implementation.
- [ ] No implicit `vkDeviceWaitIdle()`/fence-wait call inside
      `VulkanOffscreenTarget`'s or `VulkanOffscreenRenderTarget`'s own
      destructor.
- [ ] `git grep -n "Android\|ANativeWindow\|android_main"` under `src/`,
      `tests/`, `examples/` added by this plan returns nothing.
- [ ] `Renderer`'s dependency set (`atlantis_renderer`'s
      `target_link_libraries`) is unchanged — still exactly
      `Atlantis::RHI`, `Atlantis::RenderGraph`, `Atlantis::Core`.
- [ ] `git grep -n "static_cast<VulkanRenderTarget" src/vulkan_backend/src/`
      returns **exactly one** match
      (`vulkan_presentation.cpp`'s `present()`, confirmed windowed-only
      and deliberately unchanged) — zero matches in
      `vulkan_command_list.cpp` or `vulkan_device.cpp`, confirming all
      four pre-existing in-scope call sites (`transitionResource()`,
      `clearColor()`, `beginRendering()`, `submit()`) were migrated from
      `static_cast` to the checked pointer-form `dynamic_cast<VulkanRenderTargetAccess*>`/
      `dynamic_cast<const VulkanRenderTargetAccess*>`, with none missed
      or silently left as a `static_cast` — and that the fifth,
      newly-added call site (`copyRenderTargetToBuffer()`, which never
      had a `static_cast` to migrate from, since it does not exist before
      this plan) uses the identical checked pointer-form from the start.
- [ ] `git grep -n "dynamic_cast<VulkanRenderTargetAccess&\|dynamic_cast<const VulkanRenderTargetAccess&"
      src/vulkan_backend/src/` returns **zero** matches — every adoption
      of `VulkanRenderTargetAccess` uses the checked pointer-form
      (`dynamic_cast<VulkanRenderTargetAccess*>`/
      `dynamic_cast<const VulkanRenderTargetAccess*>` +
      `ATLANTIS_CHECK_MSG(... != nullptr, ...)`), never the
      exception-throwing reference-form — **corrected during the final
      Plan Review round**, see Section 5.1's own "`dynamic_cast` form"
      note for why the reference-form would have violated this
      codebase's exception-free render path rule (AGENTS.md).
- [ ] `VulkanOffscreenRenderTarget` declares no `VkImage`/`VkImageView`/
      `Extent2D`/`Format` member of its own — `image()`/`imageView()`/
      `extent()`/`format()` each delegate to `owner_`, per Section 5.3's
      corrected, duplicate-free member list.
- [ ] No new CMake compiler/RTTI option (e.g. no explicit `/GR` or
      `/GR-` toggle) is added anywhere this plan touches — the
      `dynamic_cast` calls Section 5/6 introduce rely on this project's
      existing MSVC default, not a newly-added build setting.
- [ ] `src/rhi/include/atlantis/rhi/device.h`'s `submit()` doc comment
      and `src/rhi/include/atlantis/rhi/submission_signal.h`'s class
      comment state the present()-before-next-submit() precondition as
      windowed-only, consistently with each other (Sections 1.3, 1.6) —
      neither still reads as an unconditional rule that a legitimate
      headless caller would violate.

## Build Integration

- `src/rhi/CMakeLists.txt`: **no change** — `offscreen_target.h` is a
  header-only interface addition (matching `render_target.h`/
  `presentation.h`'s own precedent, neither of which has a corresponding
  `.cpp` in `atlantis_rhi`'s source list); confirm this remains true at
  implementation time before assuming it.
- `src/vulkan_backend/CMakeLists.txt`: add
  `src/vulkan_offscreen_target.cpp` and
  `src/vulkan_offscreen_render_target.cpp` to `atlantis_vulkan_backend`'s
  source list. `vulkan_render_target_access.h` (5.1) needs no entry
  (header-only, no `.cpp`, mirroring every other private interface
  header in this directory).
- `tests/render_graph/CMakeLists.txt`: add `headless_binding_tests.cpp`.
- `tests/vulkan_backend/CMakeLists.txt`: add
  `headless_rendering_gpu_tests.cpp` to `atlantis_vulkan_backend_gpu_tests`'s
  source list.
- `tests/rhi/CMakeLists.txt`: no new file, existing `types_tests.cpp`
  extended in place.
- `tests/renderer/CMakeLists.txt`: no new file, existing
  `renderer_ownership_tests.cpp` extended in place.
- Root `CMakeLists.txt`: add
  `add_subdirectory(examples/headless_rendering_demo)`.
- New `examples/headless_rendering_demo/CMakeLists.txt` (Section 7.1).

## Sequencing & Dependencies

**Revised per Plan Review**, replacing an earlier draft's "Changeset
A–E" grouping, which incorrectly claimed the RHI-interface group (old
Changeset A) was "independently compiling." It is not: adding a new
pure-virtual method to an already-implemented abstract interface
(`Device`, `CommandList`) makes every existing concrete implementer —
`VulkanDevice`, `VulkanCommandList`, **and** the test double
`FakeCommandList` (`tests/render_graph/fake_command_list.h`) — abstract
and uninstantiable the moment that method is declared, not merely
"unreferenced." The steps below are regrouped so that **every step ends
with a repository that compiles and whose own new/updated tests pass**
— a pure-virtual method's declaration and *every one* of its concrete
overrides are treated as one inseparable unit, never split across
steps, per Plan Review's explicit instruction.

1. **Step 1 — RHI value types (no interface change):** Sections 1.1,
   1.5, 1.6. Adds enum values, `OffscreenTargetCreateParams`, the two
   new error enums, and the `Format` doc-comment update — no new pure
   virtual anywhere, so no existing concrete class is affected. 1.6's
   `submission_signal.h` comment fix is independent of everything else
   in this step (and every other step) and may land here or anywhere
   else convenient — grouped into Step 1 only because, like 1.1/1.5, it
   changes no interface and affects no concrete class's compilability.
   Ends compilable; `tests/rhi/types_tests.cpp`'s new/extended cases
   pass.
2. **Step 2 — `OffscreenTarget` interface (brand-new type, zero existing
   implementers):** Section 1.2. Ends compilable (nothing instantiates
   it yet, so its abstractness affects nothing).
3. **Step 3 — RenderGraph binding + `Renderer` + windowed call-site
   updates, landed as one bundle:** Sections 2.1, 2.2, 3.1, 3.2, 4.1,
   4.2, plus `tests/render_graph/headless_binding_tests.cpp`'s items
   1–8 (2.4) and `tests/renderer/renderer_ownership_tests.cpp` (3.3).
   Bundled together for a *different* reason than Steps 4–5: `ResourceBinding::finalState`
   has no default a caller can rely on, so `renderer.cpp`/both demos
   must update in the **same** step to avoid a *silent* behavior
   regression (not a compile error — the windowed path would still
   build, but would quietly stop reaching `PresentSource`, exactly the
   defect Round 1 of this spec's Human Review already found and fixed
   once). Does not touch `Device`/`CommandList`, so `VulkanDevice`/
   `VulkanCommandList`/`FakeCommandList` are entirely unaffected and
   keep compiling exactly as before. Ends compilable; new/updated tests
   pass; windowed demos compile with intended-unchanged behavior
   (verified for real in Step 6).
4. **Step 4 — `Device`/`CommandList` interface additions + every
   concrete override, landed as one single, necessarily large bundle:**
   Sections 1.3, 1.4, 5.1, 5.2, 5.3, 5.4, 6.1, 6.2, 6.3, 6.4, 6.5, plus
   `tests/render_graph/fake_command_list.h`'s `copyRenderTargetToBuffer()`
   override (2.3) and `headless_binding_tests.cpp`'s item 9 (2.4). This
   is the step Plan Review's Must Fix corrects: `Device::createOffscreenTarget()`
   and `CommandList::copyRenderTargetToBuffer()` are declared (1.3, 1.4)
   in the **same** step as every one of their concrete overrides
   (`VulkanDevice`, `VulkanCommandList`, `FakeCommandList`) — anything
   less leaves the repository non-compiling at that step's boundary.
   Internally: 5.1 (the shared private interface) first; 5.2
   (`VulkanRenderTarget` adopts it) and 5.3 (`VulkanOffscreenTarget`/
   `VulkanOffscreenRenderTarget`) next, in either order; 5.4
   (`createOffscreenTarget()`) after 5.3; 6.1/6.2 (the four call-site
   fixes) after 5.1–5.3; 6.3/6.4 independent of the rest and of each
   other; 6.5 (`copyRenderTargetToBuffer()`'s body) after 5.1, 5.3, 6.4.
   Ends compilable; GPU-independent tests
   (`resource_state_mapping_tests.cpp`'s new cases, 6.4) pass
   immediately; the full GPU-required verification (existing windowed
   suite + new headless suite) is Step 6's job, not claimed here.
5. **Step 5 — Headless verification composition and GPU tests:**
   Sections 7.1, 7.2 — depends on Steps 1–4 in full. Ends compilable and
   (on real hardware) passing.
6. **Step 6 — Windowed regression verification and documentation:**
   Sections 8, 9, 10 — depends on Step 5. Confirms Steps 3's and 4's
   combined effect on the windowed path is behavior-preserving, on real
   hardware, not merely by inspection.

A single Implementation PR landing Steps 1–6 together (matching Spec
0006/0007's own single-PR-per-spec precedent, adjusted for this spec's
narrower scope) is this plan's expected shape; splitting into multiple
PRs is a Plan-Review-confirmable choice, provided no PR boundary ever
lands inside Step 4 (which cannot be subdivided further without
breaking compilation, per the analysis above).

## Files / Modules Touched (expected)

**New:**
`src/rhi/include/atlantis/rhi/offscreen_target.h`,
`src/vulkan_backend/src/vulkan_render_target_access.h`,
`src/vulkan_backend/src/vulkan_offscreen_target.{h,cpp}`,
`src/vulkan_backend/src/vulkan_offscreen_render_target.{h,cpp}`,
`examples/headless_rendering_demo/{main.cpp,CMakeLists.txt}`,
`tests/render_graph/headless_binding_tests.cpp`.

**Modified:**
`src/rhi/include/atlantis/rhi/{types,device,command_list}.h`
(`device.h`'s `submit()` doc comment also amended — Section 1.3 — for
the stale-precondition finding below),
`src/rhi/include/atlantis/rhi/submission_signal.h` (comment-only —
Section 1.6 — matches `device.h`'s amendment; not to be confused with
the distinct, unmodified `src/vulkan_backend/src/vulkan_submission_signal.{h,cpp}`
below),
`src/rhi/src/types.cpp`,
`src/render_graph/include/atlantis/render_graph/execution.h`,
`src/render_graph/src/execution.cpp`,
`src/renderer/include/atlantis/renderer/renderer.h`,
`src/renderer/src/renderer.cpp`,
`src/vulkan_backend/src/vulkan_device.{h,cpp}`,
`src/vulkan_backend/src/vulkan_render_target.h` (inheritance-list-only
change — see 5.2; **unconditional**, not contingent on any remaining
Plan Review choice),
`src/vulkan_backend/src/vulkan_command_list.{h,cpp}`,
`src/vulkan_backend/src/resource_state_mapping.cpp`,
`src/vulkan_backend/CMakeLists.txt`,
`examples/frame_execution_demo/main.cpp`,
`examples/minimal_renderer_demo/main.cpp`,
`tests/render_graph/fake_command_list.h`,
`tests/render_graph/CMakeLists.txt`,
`tests/renderer/renderer_ownership_tests.cpp`,
`tests/rhi/types_tests.cpp`,
`tests/vulkan_backend/{CMakeLists.txt,resource_state_mapping_tests.cpp}`,
`CMakeLists.txt` (root),
`specs/README.md` (post-implementation, per Section 10).

**Explicitly not touched, confirmed by this revision's own research:**
`src/vulkan_backend/src/vulkan_presentation.cpp` (its own
`static_cast<VulkanRenderTarget&>` in `present()` stays exactly as-is —
see Section 5's own explanation of why) and
`src/vulkan_backend/src/vulkan_submission_signal.{h,cpp}` — the
**Vulkan Backend's concrete** `VulkanSubmissionSignal` implementation,
distinct from the RHI-level abstract `src/rhi/include/atlantis/rhi/submission_signal.h`
Section 1.6 amends — confirmed, by reading its actual implementation, to
be a trivial, unconditional member-initializer store with no non-null
assumption — an earlier draft of this plan flagged it as needing a
possible fix "if it does" assume non-null; it does not, so no change to
this Vulkan Backend file is needed. (Section 1.6's change is to the
*RHI interface's own doc comment*, a different file with a similar name
— do not conflate the two when implementing.)

If Implementation touches a file not listed here, that is a deviation to
call out explicitly in the Implementation PR, not to slip in silently
(per this plan's own template and AGENTS.md).

## Verification Checklist

- [ ] Unit tests (GPU-independent, `ctest -LE gpu`, Debug **and**
      Release): Sections 1.1, 2.4 (items 1–8), 3.3, 6.4 pass, no new
      warning introduced.
- [ ] Headless integration tests (GPU-required, `ctest -L gpu`, Debug
      **and** Release): Section 7.2 passes on real Vulkan-capable
      hardware (or explicitly disclosed as not exercised, per Section
      8) — including 7.2's own item 7, confirmed to exercise all five
      corrected call sites (`transitionResource()`, `clearColor()`,
      `beginRendering()`, `submit()`, `copyRenderTargetToBuffer()`) end
      to end.
- [ ] Image regression tests: N/A (Non-Goal) — not implemented, not
      stubbed, not partially scaffolded by this plan or its
      Implementation.
- [ ] **No reference-form `dynamic_cast` anywhere this plan
      introduces:** `git grep -n "dynamic_cast<VulkanRenderTargetAccess&\|dynamic_cast<const VulkanRenderTargetAccess&"
      src/vulkan_backend/src/` returns **zero** matches — every one of
      the five call sites (6.1 ×3, 6.2, 6.5) uses the pointer-form
      `dynamic_cast<VulkanRenderTargetAccess*>`/
      `dynamic_cast<const VulkanRenderTargetAccess*>` followed by
      `ATLANTIS_CHECK_MSG(... != nullptr, ...)`, matching the existing
      `vulkan_presentation.cpp:651` precedent. A type mismatch at any of
      these five sites fails via this assertion, in Debug **and**
      Release (`ATLANTIS_CHECK_MSG` is unconditional, per
      `assert.h`) — never via an uncaught `std::bad_cast` or any other
      exception reaching the render path.
- [ ] **Headless `submit()`'s `VkSubmitInfo` has zero wait/signal
      semaphore count, not a null handle in a non-zero-count array:**
      confirmed by 7.2's own GPU test (item 7) and, if feasible, by
      inspecting `VkSubmitInfo::waitSemaphoreCount`/
      `signalSemaphoreCount` directly in a debugger/log during that
      test — both must be `0` and `pWaitSemaphores`/`pSignalSemaphores`
      must be `nullptr` for a `VulkanOffscreenRenderTarget` argument,
      matching Section 6.2's conditional construction exactly.
- [ ] **Windowed semaphore behavior byte-identical to the pre-this-plan
      baseline:** confirmed by `frame_execution_gpu_tests.cpp`/
      `minimal_renderer_gpu_tests.cpp` re-run unmodified and passing —
      `VulkanRenderTarget::acquireCompleteSemaphore()`/
      `renderFinishedSemaphore()` never return `VK_NULL_HANDLE`, so
      `submitInfo.waitSemaphoreCount`/`signalSemaphoreCount` remain `1`
      and `pWaitSemaphores`/`pSignalSemaphores` remain non-null for
      every existing windowed call site, exactly as before Section 6.2.
- [ ] Vulkan Validation Layers clean: for every GPU-touching test and
      demo run this plan adds, and for the full re-run windowed suite
      (Section 9), in both Debug and Release — zero warnings, zero
      errors.
- [ ] Manual verification: `examples/headless_rendering_demo` runs to
      completion, passes its own basic content check, in both Debug and
      Release.
- [ ] Windowed regression (Debug **and** Release): Section 9's full
      checklist passes with no behavior change from the pre-this-plan
      baseline, confirmed for **all four** corrected `VulkanCommandList`/
      `VulkanDevice` call sites, not `submit()` alone.
- [ ] Explicit Prohibitions checklist (above) fully checked, including
      the `git grep` check confirming exactly one remaining
      `static_cast<VulkanRenderTarget` site (`vulkan_presentation.cpp`)
      and the reference-form-`dynamic_cast` check above.
- [ ] `git diff --check` clean on every commit.

## Rollback Plan

Steps 1–6 (Sequencing & Dependencies) are each independently revertible
in reverse order (6 → 1) without touching an earlier, already-verified
step — `git revert` of the Implementation PR's commit(s) in
reverse-chronological order restores the pre-Plan state exactly, since
no step here modifies a file's *meaning* for any existing, already-
shipped caller beyond the explicitly-called-out mechanical updates
(Section 4) and the four-call-site `dynamic_cast` refactor (Section 6.1,
6.2), both of which Section 9's regression checklist exists specifically
to catch before merge. Two narrower, faster rollback points exist within
Step 4 if a GPU-verification problem is isolated to one area:

- If the problem is isolated to `transitionResource()`/`clearColor()`/
  `beginRendering()` specifically: revert Section 6.1's three method
  bodies to their original `static_cast` — this reverts headless drawing
  entirely (an offscreen target passed to any of the three becomes
  undefined behavior again) while leaving `submit()`'s fix and the
  windowed path both intact.
- If the problem is isolated to `submit()` specifically: revert Section
  6.2's body to its original unconditional `static_cast`/`VkSubmitInfo`
  construction — `transitionResource()`/`beginRendering()` (6.1) remain
  fixed, but the offscreen path still fails at the submission step; the
  windowed path is unaffected either way.

Both narrower rollbacks are strictly worse than fixing the underlying
GPU-verification finding directly once diagnosed, but are documented
here as genuine, isolated fallback points, not merely a single
all-or-nothing revert.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- "Image regression tests added/updated if rendered output changed" is
  **N/A** — this plan's own rendered output (the headless demo's clear/
  mesh color) is new, not a change to any existing golden/reference
  output, and Spec 0010 explicitly does not introduce image regression
  tooling.
- "Headless verification performed for any rendering-adjacent change" —
  **this plan is what makes that phrase concretely testable for the
  first time**; Section 7's own coverage is the answer, not a future
  deferral.

## Human Review / Plan Review Blockers

**Resolved during the first Plan Review revision, no longer open:**

- ~~`VulkanDevice::submit()`'s polymorphic-signal-access mechanism~~ —
  **confirmed**: the private-interface-plus-checked-`dynamic_cast`
  approach (`VulkanRenderTargetAccess`, Section 5.1), applied uniformly
  across all five call sites (Section 6.1, 6.2, 6.5), not `submit()`
  alone. The always-real-dummy-semaphore alternative is dropped.
- ~~`OffscreenTargetCreateParams::format`'s default~~ — **confirmed**:
  `Format::Rgba8Unorm` (Section 1.1), matching sibling `*CreateParams`
  structs' precedent of defaulting to a real, usable value rather than
  an assertion-triggering sentinel.
- ~~`VulkanOffscreenRenderTarget`'s constructor/member-list
  consistency~~ — **confirmed**: single-parameter constructor
  (`owner_` only), every accessor delegates to `owner_`, no duplicated
  `VkImage`/`VkImageView`/`Extent2D`/`Format` member (Section 5.3).

**Resolved during the final (second) Plan Review revision, no longer
open:**

- ~~`dynamic_cast<VulkanRenderTargetAccess&>`'s exception-throwing
  reference form~~ — **confirmed**: every one of the five call sites
  (6.1 ×3, 6.2, 6.5) now uses the checked pointer-form
  (`dynamic_cast<VulkanRenderTargetAccess*>`/
  `dynamic_cast<const VulkanRenderTargetAccess*>`) plus
  `ATLANTIS_CHECK_MSG(... != nullptr, ...)`, matching the existing
  `vulkan_presentation.cpp:651` precedent — never a reference-form cast
  that could throw `std::bad_cast` into the render path (Section 5.1's
  own "`dynamic_cast` form" note). No RTTI/build-option change.
- ~~Section 3.3's stale dependency on Section 2.3~~ — **confirmed**:
  the new `[renderer][final_color_state]` case only exercises
  `Renderer::drawFrame()`'s own internal draw pass and asserts against
  `FakeCommandList`'s recorded `transitions`, never
  `copyRenderTargetToBuffer()` — Section 3.3's dependency line now
  reads "after 3.1, 3.2" only, consistent with its placement entirely
  within Step 3 (Sequencing & Dependencies).
- ~~`Device::submit()`/`SubmissionSignal`'s stale
  present()-before-next-submit() doc comments~~ — **confirmed**: both
  `device.h` (Section 1.3) and `submission_signal.h` (Section 1.6) now
  state this precondition as windowed-only, with the headless exemption
  and its ADR-0038 justification spelled out consistently in both
  places — comment-only, no runtime behavior change.

**Still open — design choices flagged for Plan Review; genuinely
different, defensible options exist; this plan does not pick silently:**

1. **`OffscreenAcquireError`'s reachability** (Section 1.1, 5.3): this
   round's concrete Vulkan Backend implementation has no code path that
   actually produces `Err(...)` from `acquireTarget()` — confirm keeping
   the `Result`-wrapped return shape (matching ADR-0038's literal text,
   forward-consistent with `Presentation`'s pattern) vs. a bare
   `std::unique_ptr<RenderTarget>` return (smaller, more honest, but a
   literal deviation from the ADR's stated signature — would need its
   own small ADR-0038 clarification, not silently decided here).
2. **Fixture code-sharing between `minimal_renderer_demo` and
   `headless_rendering_demo`** (Section 7.1): duplicate the fixed
   `Mesh`/`Material`/camera-`Buffer` setup (this plan's default) vs.
   factor it into a small shared header/library neither prior example
   needed — confirm the duplication default is acceptable, or direct a
   specific shared-code shape.
3. **Exact reproducible basic-content-check thresholds** (Section 7.1,
   item 7) — center/corner sampling is the candidate shape; exact
   tolerance values and pass/fail thresholds are left to Implementation
   unless Plan Review wants them fixed here.
4. **Distinct CI/test-category label for headless GPU tests** — Spec
   0006/0007 both flagged this as open and left it open; this plan does
   the same (Section 8) unless Plan Review wants it resolved now.

**Non-blocking, disclosed limitations carried into Implementation:**

- Section 6.5's "no explicit host-visibility barrier" reasoning is
  confirmed only by Section 7.2's own GPU test reading back correct
  pixel data — if that test ever shows incorrect/stale readback bytes
  with Validation Layers otherwise clean, this is the first place to
  suspect, not a re-litigation of Spec 0010/ADR-0040's own design.
- The pointer-form `dynamic_cast`'s RTTI dependency (Section 5.1) relies
  on this project's existing MSVC default (`/GR`, enabled, not
  currently overridden anywhere in this repository's CMake
  configuration) — this plan does not add, remove, or otherwise touch
  any RTTI/compiler-option build setting; if a future spec ever needs
  to disable RTTI project-wide for an unrelated reason, that future
  spec would need to revisit this mechanism, not this plan. The switch
  from reference-form to pointer-form (final Plan Review round) changes
  only the cast's failure semantics (assertion vs. exception), not its
  RTTI dependency.

**No architectural gap requiring a return to Spec/ADR was found while
producing or revising this plan, across either Plan Review round.** The
first round's Must Fix (the three additional unconditional `static_cast`
sites) and three Should Fixes, and the second round's Must Fixes
(reference-form `dynamic_cast`'s exception risk; Section 3.3's stale
2.3 dependency) and Should Fix (the stale `submit()`/`SubmissionSignal`
doc comments), are all implementation-shape or documentation-accuracy
corrections within the boundaries Spec 0010 and
ADR-0022/0038/0039/0040 already fixed — none requires a new public API,
ownership model, synchronization primitive, module boundary, or
dependency beyond what those documents already authorize.
