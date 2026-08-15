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
    Format format = Format::Unknown;` plus a matching `operator==`
    declaration, in the same position/style as `TextureCreateParams`.
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
- **Dependency order:** after 1.1, 1.2.
- **Tests after this step:** none directly; this is an interface-only
  addition, exercised once Section 5 implements it.
- **Stop condition / rollback:** revert this file; `Device`'s existing
  concrete implementation (`VulkanDevice`) will fail to compile until
  Section 5 adds the override — this step and Section 5's
  `VulkanDevice::createOffscreenTarget()` implementation must land in the
  same compiling changeset (see Sequencing & Dependencies).

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
  compile until Sections 2.3/6.4 add the override — must land in the
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
- **Dependency order:** after 2.1, 2.2, 2.3.
- **Tests after this step:** this *is* the test step; run via
  `ctest -LE gpu` once wired into the build (see Section 11.3).
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
- **Dependency order:** after 3.1, 3.2, 2.3 (needs the extended
  `FakeCommandList` for full assertion coverage, though the mechanical
  update alone only needs 3.1/3.2).
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

## 5. Vulkan Backend — `OffscreenTarget` Owner and Borrowed `RenderTarget`

**Spec Requirement:** "Offscreen `RenderTarget` construction and
ownership" (Vulkan Backend implementation shape). **ADR:**
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).

### 5.1 New: `src/vulkan_backend/src/vulkan_offscreen_target.h` / `.cpp`

- **Input:** `vulkan_texture.h`/`.cpp` as the direct structural
  precedent (owning type: `VkImage` + `VkDeviceMemory` + `VkImageView`,
  trivial constructor taking already-created handles, destructor
  releasing all three in view→memory→image order).
- **Output:** candidate class `VulkanOffscreenTarget final : public
  atlantis::rhi::OffscreenTarget`:
  - Owns `VkDevice` (borrowed, not owned — outlives this object,
    caller-enforced, same tier as every other Vulkan Backend type),
    `VkImage image_`, `VkDeviceMemory memory_`, `VkImageView imageView_`,
    `Extent2D extent_`, `Format format_`.
  - `bool outstandingBorrow_ = false;` — backs both the double-acquire
    check (in `acquireTarget()`) and the destroy-while-outstanding check
    (in this class's own destructor).
  - `acquireTarget()`: `ATLANTIS_CHECK_MSG(!outstandingBorrow_, "OffscreenTarget::acquireTarget() called while a previously-vended borrow is still outstanding");`
    then `outstandingBorrow_ = true;` then constructs and returns
    `Ok(std::make_unique<VulkanOffscreenRenderTarget>(this, extent_, format_))`
    — see 5.2. No `Err` path is actually reachable in this round's
    implementation (flagged in Section 1.1 and Human Review / Plan
    Review Blockers).
  - Destructor: `ATLANTIS_CHECK_MSG(!outstandingBorrow_, "VulkanOffscreenTarget destroyed while a vended borrow is still outstanding");`
    then releases `imageView_`/`memory_`/`image_` in that order (mirrors
    `VulkanTexture::~VulkanTexture()` exactly). **Does not** call
    `vkDeviceWaitIdle()` or wait on any fence — per ADR-0038, this is
    deliberately the same "destructor does not itself wait" tier as
    `VulkanPresentation::~VulkanPresentation()`.
  - Private method `void clearOutstandingBorrow() noexcept { outstandingBorrow_ = false; }`
    — called only by `VulkanOffscreenRenderTarget`'s destructor (a
    `friend` relationship, or a narrow private accessor — candidate,
    confirmable at Plan Review).
- **Dependency order:** after Section 1.2 (interface exists), Section 5.2
  (mutually referencing — `VulkanOffscreenTarget` constructs a
  `VulkanOffscreenRenderTarget`; that type's destructor calls back into
  `VulkanOffscreenTarget`). Implemented together, declared via a forward
  declaration in whichever header is compiled first.
- **Tests after this step:** GPU-required, see Section 7.2 (no
  GPU-independent test is possible — this type's entire purpose is
  owning real Vulkan objects).
- **Stop condition / rollback:** revert this file pair; nothing else
  compiles against it until `VulkanDevice::createOffscreenTarget()`
  (5.3) references it.

### 5.2 New: `src/vulkan_backend/src/vulkan_offscreen_render_target.h` / `.cpp`

- **Input:** `vulkan_render_target.h`/`.cpp` as the direct structural
  precedent — a second, distinct, non-owning concrete
  `atlantis::rhi::RenderTarget` implementation.
- **Output:** candidate class `VulkanOffscreenRenderTarget final :
  public atlantis::rhi::RenderTarget`:
  - Non-owning: `VulkanOffscreenTarget* owner_` (raw, non-owning — must
    outlive this borrow, enforced by the destruction/outstanding-borrow
    contract, not the type system), plus `VkImage image_` (borrowed,
    same value as `owner_`'s), `VkImageView imageView_` (borrowed),
    `Extent2D extent_`, `Format format_`.
  - `extent()`/`format()`: trivial accessors, same shape as
    `VulkanRenderTarget`'s.
  - `image()`/`imageView()` accessors, same visibility/purpose as
    `VulkanRenderTarget`'s own (§"Accessors below exist solely for..."
    comment) — needed by `VulkanCommandList::transitionResource()`/
    `copyRenderTargetToBuffer()`/`beginRendering()`.
  - **Acquire-complete / render-finished signal accessors** — see the
    flagged design point in Section 6.1 below; this type's own
    contribution is to provide *some* implementation of whatever shared
    accessor shape Section 6.1 settles on, returning `VK_NULL_HANDLE`
    for "trivial/pre-satisfied" (ADR-0038's own phrase).
  - Destructor: calls `owner_->clearOutstandingBorrow()` (or the
    equivalent accessor 5.1 settles on) — the **only** side effect
    ending the borrow has, per ADR-0038's RAII contract. No Vulkan
    object is destroyed here (non-owning).
- **Dependency order:** with 5.1, same changeset.
- **Tests after this step:** GPU-required, see Section 7.2.
- **Stop condition / rollback:** revert alongside 5.1.

### 5.3 `src/vulkan_backend/src/vulkan_device.h` / `.cpp` (modify) — `createOffscreenTarget()`

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
    only the separate readback `Buffer`, Section 6.2, is host-visible).
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
- **Dependency order:** after 1.1, 1.3, 5.1, 5.2.
- **Tests after this step:** see 7.2.
- **Stop condition / rollback:** revert this method's body alone (the
  declaration from 1.3 stays; `VulkanDevice` simply fails to compile
  until re-added) — this is the natural rollback boundary since 1.3's
  interface addition and this override must land together regardless.

---

## 6. Vulkan Backend — Readback Capability

**Spec Requirement:** "GPU-to-CPU readback capability,"
"RenderGraph execution generalization... Vulkan Backend impact."
**ADR:**
[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md),
[ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).

### 6.1 `VulkanDevice::submit()` — polymorphic signal access (flagged finding)

**This is a real implementation-correctness finding from this plan's own
research, not a restatement of the spec/ADRs: `VulkanDevice::submit()`'s
shipped code (`vulkan_device.cpp`) unconditionally does
`static_cast<const VulkanRenderTarget&>(target)` to read
`acquireCompleteSemaphore()`/`renderFinishedSemaphore()`. Once
`VulkanOffscreenRenderTarget` (5.2) exists as a second concrete
`RenderTarget` implementation, calling `submit()` with an offscreen-
vended target would `static_cast` to the wrong concrete type — undefined
behavior, not merely a missed case.** ADR-0038 already fixes the
*contract* ("a signal that is always-already-satisfied is a valid value
of the same opaque mechanism, not a new case `Device` must recognize")
but not the *mechanism*; this is exactly the kind of small,
implementation-level structural choice this plan may propose, flagged
for Plan Review per this plan's own "Candidate-API Status" section.

- **Candidate (recommended):** introduce a private, Vulkan-Backend-only
  interface — e.g. `class VulkanRenderTargetSignals { public: virtual
  VkSemaphore acquireCompleteSemaphore() const = 0; virtual VkSemaphore
  renderFinishedSemaphore() const = 0; };` — that both
  `VulkanRenderTarget` and `VulkanOffscreenRenderTarget` additionally,
  privately implement (multiple inheritance from one public RHI
  interface plus one private Vulkan-Backend-only interface, a pattern
  with no existing precedent in this codebase but a small, mechanical
  one). `VulkanDevice::submit()` does
  `dynamic_cast<VulkanRenderTargetSignals&>(target)` (or a
  `static_cast` to a common private base, if both concrete types are
  guaranteed to always additionally derive from it — the RTTI cost of
  `dynamic_cast` is negligible against a `vkQueueSubmit` call and keeps
  the cast checked rather than blind) instead of the current unconditional
  `static_cast<const VulkanRenderTarget&>`. `VulkanOffscreenRenderTarget`'s
  implementation returns `VK_NULL_HANDLE` for both accessors.
  `VulkanRenderTarget`'s implementation is unchanged (already has both
  accessors; only its inheritance list gains the new private interface).
- **`VkSubmitInfo` construction change:** when `acquireCompleteSemaphore()`
  returns `VK_NULL_HANDLE`, set `waitSemaphoreCount = 0` (omit
  `pWaitSemaphores`/`pWaitDstStageMask`) instead of the current
  unconditional `waitSemaphoreCount = 1`. When `renderFinishedSemaphore()`
  returns `VK_NULL_HANDLE`, the same applies to `signalSemaphoreCount`;
  `VulkanSubmissionSignal` (already constructed from a `VkSemaphore`)
  must tolerate a `VK_NULL_HANDLE` value — its own `waitOn()`-equivalent
  consumer (`Presentation::present()`) is never called for a headless
  target, so this null value is never itself waited on; verify
  `VulkanSubmissionSignal`'s existing implementation does not
  unconditionally assume a non-null handle (read
  `vulkan_submission_signal.cpp` at implementation time; adjust if it
  does).
- **Alternative considered, not recommended:** give
  `VulkanOffscreenRenderTarget` a *real* (not null) dedicated semaphore
  pair, created once at `VulkanOffscreenTarget` construction and reused
  across every `acquireTarget()` cycle (always already signaled, since
  nothing ever waits on it in one direction and it is immediately
  re-signaled by the next submit in the other) — avoids the
  `VK_NULL_HANDLE`-tolerant `VkSubmitInfo` branching above, at the cost
  of two always-unused, always-allocated semaphore objects per
  `OffscreenTarget` instance. **Flagged for Plan Review** alongside the
  recommended approach — see Human Review / Plan Review Blockers.
- **Dependency order:** after 5.1, 5.2; must land in the same changeset
  as `VulkanDevice::submit()`'s own modified body, since the windowed
  path's existing, verified behavior must not regress (`submit()`'s
  behavior for a `VulkanRenderTarget` argument must be bit-for-bit
  unchanged).
- **Tests after this step:** GPU-required — Section 7.2's headless cycle
  test exercises the offscreen path; the existing
  `frame_execution_gpu_tests.cpp`/`minimal_renderer_gpu_tests.cpp` exercise
  the windowed path unmodified (Section 9), confirming no regression.
- **Stop condition / rollback:** revert `submit()`'s body to its current
  unconditional `static_cast`; this reverts headless `submit()` support
  entirely (Sections 5-7 become uncallable end-to-end, though they still
  compile) — the safest single-file rollback point if a GPU-verification
  problem is found late.

### 6.2 `src/vulkan_backend/src/vulkan_device.cpp` — `createBuffer()` readback purpose mapping

- **Input:** existing `switch (params.purpose)` in `createBuffer()`
  mapping `BufferPurpose` to `VkBufferUsageFlags`.
- **Output:** add
  `case atlantis::rhi::BufferPurpose::Readback: usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; break;`
  — no other change to `createBuffer()`'s body; the existing
  host-visible/host-coherent memory-property selection
  (`VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT`)
  and always-mapped-at-creation behavior already apply uniformly to
  every purpose, per ADR-0023 — readback needs no special case there.
- **Dependency order:** after Section 1.1.
- **Tests after this step:** GPU-required (buffer creation always
  requires a real device) — Section 7.2.
- **Stop condition / rollback:** revert this one `switch` arm; does not
  affect the other three purposes.

### 6.3 `src/vulkan_backend/src/resource_state_mapping.cpp` (modify) — new `planTransition()` entry

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
- **Dependency order:** after Section 1.1 (`TransferSource` must exist).
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

### 6.4 `src/vulkan_backend/src/vulkan_command_list.h` / `.cpp` (modify) — `copyRenderTargetToBuffer()`

- **Input:** `VulkanCommandList::clearColor()`'s existing implementation
  shape (casts the `RenderTarget&`/`Buffer&` arguments to their concrete
  Vulkan types, issues one `vkCmd*` call) as the pattern; ADR-0040's
  exact copy-region contract (full extent, tightly packed).
- **Output:** declare the override (1.4's interface addition) and
  implement:
  ```
  void VulkanCommandList::copyRenderTargetToBuffer(atlantis::rhi::RenderTarget& source, atlantis::rhi::Buffer& destination) {
    // source is dynamic_cast (or the shared-signals-interface pattern
    // from 6.1, if extended to also expose image()/imageView()
    // polymorphically -- candidate, see 6.1's own flagged discussion)
    // to whichever concrete RenderTarget it actually is; destination is
    // static_cast<VulkanBuffer&> (only one concrete Buffer
    // implementation exists in Phase 1, ADR-0001).
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;    // 0 = tightly packed (ADR-0040)
    region.bufferImageHeight = 0;  // 0 = tightly packed (ADR-0040)
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {sourceExtent.width, sourceExtent.height, 1};
    vkCmdCopyImageToBuffer(commandBuffer_, sourceImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            destinationBuffer, 1, &region);
  }
  ```
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
- **Dependency order:** after 1.4, 5.1, 5.2, 6.3 (needs the resolved
  `RenderTarget`/`Buffer` concrete-type access and the transition this
  copy runs after to already be well-defined).
- **Tests after this step:** see 7.2.
- **Stop condition / rollback:** revert this override's body alone
  (declaration stays, matching 1.4's own interface).

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
     Review).
  8. A full cycle that calls `Device::waitIdle()` immediately after
     `submit()` — before dropping the borrow, before destroying
     `OffscreenTarget` — confirming the documented, correct-order flow
     is Validation-Layers-clean, with no reliance on `Device`'s own
     destructor-time drain.
- **`tests/vulkan_backend/CMakeLists.txt`:** add this file to the
  existing `atlantis_vulkan_backend_gpu_tests` executable's source list
  (same executable Sections 4-6's windowed GPU tests already share —
  this test file needs the same `minimal_mesh_shaders` dependency/
  working-directory setup already present for that target, since it also
  exercises `Renderer::drawFrame()`; no new test executable target is
  needed).
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
- Confirm, by inspection, that `VulkanDevice::submit()`'s Section 6.1
  change produces byte-identical `VkSubmitInfo` contents for any
  `VulkanRenderTarget`-sourced call (non-null semaphores on both sides,
  exactly as today) — the polymorphic-dispatch mechanism must be a
  behavior-preserving refactor for the windowed path, not merely
  "probably fine."

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

## Build Integration

- `src/rhi/CMakeLists.txt`: **no change** — `offscreen_target.h` is a
  header-only interface addition (matching `render_target.h`/
  `presentation.h`'s own precedent, neither of which has a corresponding
  `.cpp` in `atlantis_rhi`'s source list); confirm this remains true at
  implementation time before assuming it.
- `src/vulkan_backend/CMakeLists.txt`: add `src/vulkan_offscreen_target.cpp`
  and `src/vulkan_offscreen_render_target.cpp` to `atlantis_vulkan_backend`'s
  source list.
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

Grouped into changesets that must each compile as a whole (C++ does not
allow a partially-updated shared struct/interface to exist mid-codebase):

1. **Changeset A (RHI interface):** Sections 1.1–1.5. Independently
   compiling — nothing outside `atlantis_rhi` references the new names
   yet.
2. **Changeset B (RenderGraph + Renderer + mechanical updates):**
   Sections 2.1–2.4, 3.1–3.3, 4.1–4.2 — must land together, since
   `ResourceBinding`'s `finalState` field (2.1) is consumed by
   `renderer.cpp` (3.2) and both demo call sites (4.1/4.2) in the same
   compiling changeset; depends on Changeset A.
3. **Changeset C (Vulkan Backend):** Sections 5.1–5.3, 6.1–6.4 — depends
   on Changeset A (interfaces) and, for `Renderer`-adjacent GPU testing,
   Changeset B. Internally, 5.1/5.2 (mutually referencing) land together;
   5.3 depends on both; 6.1 depends on 5.1/5.2; 6.2/6.3 are independent
   of 6.1/6.4 and of each other; 6.4 depends on 5.1/5.2/6.3.
4. **Changeset D (verification):** Sections 7.1, 7.2 — depends on
   Changesets A, B, C in full.
5. **Changeset E (regression + docs):** Sections 8, 9, 10 — depends on D.

A single Implementation PR landing Changesets A–E together (matching
Spec 0006/0007's own single-PR-per-spec precedent, adjusted for this
spec's narrower scope relative to Spec 0007's own multi-PR history) is
this plan's expected shape; splitting into multiple PRs is a
Plan-Review-confirmable choice, not a requirement.

## Files / Modules Touched (expected)

**New:**
`src/rhi/include/atlantis/rhi/offscreen_target.h`,
`src/vulkan_backend/src/vulkan_offscreen_target.{h,cpp}`,
`src/vulkan_backend/src/vulkan_offscreen_render_target.{h,cpp}`,
`examples/headless_rendering_demo/{main.cpp,CMakeLists.txt}`,
`tests/render_graph/headless_binding_tests.cpp`,
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp`.

**Modified:**
`src/rhi/include/atlantis/rhi/{types,device,command_list}.h`,
`src/rhi/src/types.cpp`,
`src/render_graph/include/atlantis/render_graph/execution.h`,
`src/render_graph/src/execution.cpp`,
`src/renderer/include/atlantis/renderer/renderer.h`,
`src/renderer/src/renderer.cpp`,
`src/vulkan_backend/src/vulkan_device.{h,cpp}`,
`src/vulkan_backend/src/vulkan_render_target.h` (only if Section 6.1's
recommended shared-interface candidate is confirmed at Plan Review),
`src/vulkan_backend/src/vulkan_command_list.{h,cpp}`,
`src/vulkan_backend/src/resource_state_mapping.cpp`,
`src/vulkan_backend/src/vulkan_submission_signal.cpp` (only if it needs
a null-handle-tolerance fix per 6.1),
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

If Implementation touches a file not listed here, that is a deviation to
call out explicitly in the Implementation PR, not to slip in silently
(per this plan's own template and AGENTS.md).

## Verification Checklist

- [ ] Unit tests: Sections 1.1, 2.4, 3.3, 6.3 pass, `ctest -LE gpu` green,
      no new warning introduced.
- [ ] Headless integration tests: Section 7.2 passes, `ctest -L gpu`
      green, on real Vulkan-capable hardware (or explicitly disclosed as
      not exercised, per Section 8).
- [ ] Image regression tests: N/A (Non-Goal).
- [ ] Vulkan Validation Layers clean: for every GPU-touching test and
      demo run this plan adds, and for the full re-run windowed suite
      (Section 9) — zero warnings, zero errors.
- [ ] Manual verification: `examples/headless_rendering_demo` runs to
      completion, passes its own basic content check, in both Debug and
      Release.
- [ ] Windowed regression: Section 9's full checklist passes with no
      behavior change from the pre-this-plan baseline.
- [ ] Explicit Prohibitions checklist (above) fully checked.
- [ ] `git diff --check` clean on every commit.

## Rollback Plan

Changesets A–E (Sequencing & Dependencies) are each independently
revertible in reverse order (E → A) without touching an earlier,
already-verified changeset — `git revert` of the Implementation PR's
commit(s) in reverse-chronological order restores the pre-Plan state
exactly, since no changeset here modifies a file's *meaning* for any
existing, already-shipped caller beyond the explicitly-called-out
mechanical updates (Section 4) and the `submit()` polymorphic-dispatch
refactor (Section 6.1), both of which Section 9's regression checklist
exists specifically to catch before merge. If Section 6.1's `submit()`
change is found to regress the windowed path after merge, the single-
file rollback path named in Section 6.1's own "Stop condition /
rollback" is the fastest safe recovery, isolating the fix to
`vulkan_device.cpp` alone.

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

**Design choices flagged for Plan Review — genuinely different,
defensible options exist; this plan does not pick silently:**

1. **`OffscreenAcquireError`'s reachability** (Section 1.1, 5.1): this
   round's concrete Vulkan Backend implementation has no code path that
   actually produces `Err(...)` from `acquireTarget()` — confirm keeping
   the `Result`-wrapped return shape (matching ADR-0038's literal text,
   forward-consistent with `Presentation`'s pattern) vs. a bare
   `std::unique_ptr<RenderTarget>` return (smaller, more honest, but a
   literal deviation from the ADR's stated signature — would need its
   own small ADR-0038 clarification, not silently decided here).
2. **`VulkanDevice::submit()`'s polymorphic-signal-access mechanism**
   (Section 6.1): the recommended private-interface-plus-`dynamic_cast`
   approach vs. the always-real-dummy-semaphore alternative — confirm
   before Implementation touches this already-shipped, already-verified
   method.
3. **Fixture code-sharing between `minimal_renderer_demo` and
   `headless_rendering_demo`** (Section 7.1): duplicate the fixed
   `Mesh`/`Material`/camera-`Buffer` setup (this plan's default) vs.
   factor it into a small shared header/library neither prior example
   needed — confirm the duplication default is acceptable, or direct a
   specific shared-code shape.
4. **Exact reproducible basic-content-check thresholds** (Section 7.1,
   item 7) — center/corner sampling is the candidate shape; exact
   tolerance values and pass/fail thresholds are left to Implementation
   unless Plan Review wants them fixed here.
5. **Distinct CI/test-category label for headless GPU tests** — Spec
   0006/0007 both flagged this as open and left it open; this plan does
   the same (Section 8) unless Plan Review wants it resolved now.

**Non-blocking, disclosed limitations carried into Implementation:**

- Section 6.4's "no explicit host-visibility barrier" reasoning is
  confirmed only by Section 7.2's own GPU test reading back correct
  pixel data — if that test ever shows incorrect/stale readback bytes
  with Validation Layers otherwise clean, this is the first place to
  suspect, not a re-litigation of Spec 0010/ADR-0040's own design.

**No architectural gap requiring a return to Spec/ADR was found while
producing this plan.** Every design choice flagged above is an
implementation-shape question within the boundaries Spec 0010 and
ADR-0022/0038/0039/0040 already fixed — none requires a new public API,
ownership model, synchronization primitive, module boundary, or
dependency beyond what those documents already authorize.
