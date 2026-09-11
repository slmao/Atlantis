# Plan: Headless Rendering Foundation

- **Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`Approved`)
- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Human Review Approval (2026-08-16):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), following two independent, read-only
  Plan Review rounds against this Plan's drafted text and the real shipped
  implementation —
  [PR #45](https://github.com/slmao/Atlantis/pull/45),
  [PR #46](https://github.com/slmao/Atlantis/pull/46). The rounds
  established, against the shipped code:
  - **Round 1:** only 1 of 5 unconditional `static_cast<VulkanRenderTarget&>`
    sites was being fixed. The five real sites are
    `vulkan_command_list.cpp:50` (`transitionResource()`), `:91`
    (`clearColor()`), `:106` (`beginRendering()`), `vulkan_device.cpp:521`
    (`submit()`), and `vulkan_presentation.cpp:607` (`present()`) — the
    last is **not** in scope (headless never constructs `Presentation`),
    the other four all are.
  - **Round 2:** reference-form `dynamic_cast<T&>` throws `std::bad_cast`,
    and exceptions are not disabled anywhere (`/EHsc` default). Resolution:
    a new shared private interface `VulkanRenderTargetAccess` (in
    `src/vulkan_backend/src/vulkan_render_target_access.h`) with
    `image()`/`imageView()`/`acquireCompleteSemaphore()`/
    `renderFinishedSemaphore()`; adopted via **checked pointer-form**
    `dynamic_cast<VulkanRenderTargetAccess*>(&target)` +
    `ATLANTIS_CHECK_MSG(access != nullptr, ...)` at all 5 sites, mirroring
    the existing `vulkan_presentation.cpp:651` precedent.
    `OffscreenTargetCreateParams::format = Format::Rgba8Unorm` (default
    corrected from `Format::Unknown`, which `toVkFormat()` asserts on).
    Section 3.3's stale dependency on Section 2.3 corrected — the new
    `[renderer][final_color_state]` case exercises only
    `Renderer::drawFrame()`'s internal draw pass, asserting against
    `FakeCommandList`'s recorded `transitions`. Both `device.h`'s `submit()`
    doc comment and `submission_signal.h`'s class comment now state the
    present()-before-next-submit() precondition as windowed-only.

  This approval covers Sections 1–10 as written, the Step 1–6 sequencing
  in "Sequencing & Dependencies" (Step 4 is a single, necessarily large,
  non-subdividable bundle — a pure-virtual method's declaration and every
  one of its concrete overrides land in one step), and the
  single-Implementation-PR shape. It does **not** authorize merging any
  PR on the human's behalf.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #150](https://github.com/slmao/Atlantis/pull/150) Batch 3. Original
  scope, ordered work, and verification retained. Candidate C++ headers,
  class bodies, and `VkImageCreateInfo`/`VkBufferImageCopy` fragments
  drafted here are summarised to their contracts and preserved in
  [PR #45](https://github.com/slmao/Atlantis/pull/45)/[PR #46](https://github.com/slmao/Atlantis/pull/46)
  history. Every C++ signature, algorithm, and file name in Sections 1–7
  was a candidate shape at drafting time and is now the approved basis for
  implementation — Spec 0010 and ADR-0022(amendment)/0038/0039/0040 fix
  *behavior*; this Plan proposed the concrete C++.

## Objective

Implement Spec 0010's approved design: headless rendering and GPU-to-CPU
readback on the exact same `Renderer` → RenderGraph → RHI → Vulkan Backend
stack Spec 0007 shipped, with no window/`Presentation`/`VkSwapchainKHR` —
introducing `OffscreenTarget`, a readback capability
(`ResourceState::TransferSource`, a fourth `BufferPurpose`,
`CommandList::copyRenderTargetToBuffer()`), a generalized RenderGraph
`execute()` binding (caller-declared `incomingState`/`finalState`), and
`Renderer::drawFrame()`'s one new required `finalColorState` parameter.

## Approval Baseline (what this Plan builds on, unchanged)

- **Spec 0010** — `Approved`, Human Review 2026-08-16.
- **ADR-0038, ADR-0039, ADR-0040** — `Accepted` alongside Spec 0010;
  **ADR-0022's Proposed Amendment** — `Accepted Amendment` alongside it.
- **ADR-0002, ADR-0019, ADR-0021, ADR-0023** — `Accepted`, unmodified and
  not reopened.
- Implementation begins only from a branch cut from `main` after this
  Plan's PR has merged.

## Authoritative Sources

Read in full before this Plan was drafted: [AGENTS.md](../AGENTS.md);
[specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md);
[ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
(incl. its Accepted Amendment),
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)–[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md);
[plans/0006-rhi-render-graph-frame-execution-foundation.md](0006-rhi-render-graph-frame-execution-foundation.md)
and [plans/0007-minimal-renderer.md](0007-minimal-renderer.md) (house
style and precedent); the shipped `src/rhi/`, `src/render_graph/`,
`src/renderer/`, `src/vulkan_backend/` (in particular
`vulkan_command_list.cpp`'s `transitionResource()`/`clearColor()`/
`beginRendering()`, `vulkan_device.cpp`'s `submit()`/`createTexture()`/
`createBuffer()`, `vulkan_presentation.cpp`'s `present()` and its
`:651` checked-`dynamic_cast` precedent, `resource_state_mapping.cpp`'s
`planTransition()`); `examples/frame_execution_demo/`,
`examples/minimal_renderer_demo/`; `tests/render_graph/fake_command_list.h`,
the existing `atlantis_vulkan_backend_gpu_tests` sources;
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md),
[definition-of-done.md](../docs/process/definition-of-done.md).

## Critical Architectural Boundaries (preserved, not re-decided here)

- **`Renderer` gains exactly one new parameter** (`finalColorState`) and
  nothing else — same dependency set (`Atlantis::RHI`/`RenderGraph`/`Core`),
  same ownership model, same single internal draw pass. `Renderer` never
  inspects, validates, or branches on the value.
- **`OffscreenTarget` involves no `Presentation` and no
  `VkSwapchainKHR`/`VkSurfaceKHR`.** Its Vulkan Backend implementation is
  an owning private type vending non-owning per-cycle borrows, mirroring
  `VulkanPresentation`/`VulkanRenderTarget`.
- **Two explicitly separate lifetimes** (ADR-0038): the borrow wrapper's
  minimum lifetime runs only through the `Device::submit()` return; the
  `OffscreenTarget` backing resources must outlive GPU completion,
  caller-enforced via `Device::waitIdle()`, and this is **not**
  guaranteed-detectable.
- **Exactly one new `planTransition()` entry:** `ColorAttachmentOutput →
  TransferSource`. No other.
- **The readback copy pass never calls `transitionResource()`** — its
  `incomingState` already equals its declared `TransferSource`.
- **All five `VulkanRenderTargetAccess` adoption sites use the checked
  pointer-form `dynamic_cast<VulkanRenderTargetAccess*>` +
  `ATLANTIS_CHECK_MSG(... != nullptr, ...)`** — never the
  exception-throwing reference-form, per AGENTS.md's exception-free render
  path rule. No RTTI/build-option change (relies on this project's
  existing MSVC `/GR` default).
- **`Texture`/`DepthFormat` are entirely untouched.** No `VkRenderPass`/
  `VkFramebuffer` object anywhere (still absent). No new third-party
  dependency. No Android/iOS/Linux content.
- **Single Phase 1 logical thread** (ADR-0004); no mutex/atomic/job
  system.

## Non-Goals

As Spec 0010's Non-Goals — no golden-image storage/comparison/tolerance/CI
gating; no Android/iOS/Linux; no sampled/shader-readable `Texture` or
`Sampler`; no `Texture` widening; no depth-buffer readback; no
async/non-blocking/multi-frame readback; no multiple frames in flight, no
multi-threading, no job system; no general GPU memory suballocator or
change to ADR-0015; no change to `Renderer`'s boundary beyond
`finalColorState`; no second rendering path/`RenderGraph`/RHI
implementation or `Renderer` fork; no multiple simultaneous
`OffscreenTarget` instances or pooling; no windowed-path regression; no
governance/roadmap document edits beyond the required registry update.

---

## 1. RHI Value Types and Interface Additions

### 1.1 `src/rhi/include/atlantis/rhi/types.h` / `src/rhi/src/types.cpp` (modify)

- Add `ResourceState::TransferSource` (one new enumerator).
- Add `BufferPurpose::Readback` — host-visible, host-coherent, mapped once
  for its lifetime (created via the existing, unchanged
  `Device::createBuffer()`).
- Add `OffscreenTargetCreateParams { Extent2D extent; Format format =
  Format::Rgba8Unorm; }` — the format default is corrected from
  `Format::Unknown` (which `toVkFormat()` asserts on) to a real, usable
  value, matching sibling `*CreateParams` structs' precedent.
- Add error enums `OffscreenTargetCreateError` and `OffscreenAcquireError`
  (exact variant sets a Plan-stage detail; see Blocker 1 for
  `OffscreenAcquireError`'s reachability).
- **GPU-independent tests** (`tests/rhi/types_tests.cpp`, extended in
  place): the new enum values and `OffscreenTargetCreateParams`
  defaulting.

### 1.2 `src/rhi/include/atlantis/rhi/offscreen_target.h` (new)

The `atlantis::rhi::OffscreenTarget` abstract interface — a brand-new
type, zero existing implementers. Vends the exact same abstract
`RenderTarget` (ADR-0019) via a two-outcome acquire-equivalent call
(`Result<std::unique_ptr<RenderTarget>, OffscreenAcquireError>`); no
`present()` counterpart. Header-only (matching `render_target.h`/
`presentation.h`'s precedent — no `.cpp` in `atlantis_rhi`'s source list;
confirm this at implementation time).

### 1.3 `src/rhi/include/atlantis/rhi/device.h` (modify) — `createOffscreenTarget()`

- Declare `createOffscreenTarget(const OffscreenTargetCreateParams&)` →
  `Result<std::unique_ptr<OffscreenTarget>, OffscreenTargetCreateError>`
  as a new pure-virtual on `Device`. (Adding a pure-virtual makes every
  existing concrete implementer abstract until its override lands — see
  Sequencing & Dependencies Step 4.)
- Amend `submit()`'s doc comment: the present()-before-next-submit()
  precondition is **windowed-only** (a headless caller has no
  `Presentation` and does not present) — comment-only, no runtime change.

### 1.4 `src/rhi/include/atlantis/rhi/command_list.h` (modify) — `copyRenderTargetToBuffer()`

Declare `copyRenderTargetToBuffer(RenderTarget& source, Buffer&
destination)` as a new pure-virtual on `CommandList`. Copies the full,
tightly-packed color image into a readback-purpose `Buffer`; no
partial-region copy, no format conversion (ADR-0040).

### 1.5 error enums

`OffscreenTargetCreateError` (device-side creation) and
`OffscreenAcquireError` (acquire-equivalent call) — `Err` reserved for
genuine environmental failures; the double-acquire / destroy-while-borrowed
cases are `ATLANTIS_CHECK` programmer errors, not `Result::Err`.

### 1.6 `src/rhi/include/atlantis/rhi/submission_signal.h` (modify — comment-only)

`SubmissionSignal`'s class comment states the
present()-before-next-submit() precondition as **windowed-only**,
consistently with `device.h` (1.3). This is the RHI-level abstract
`submission_signal.h`, distinct from the Vulkan Backend's concrete
`src/vulkan_backend/src/vulkan_submission_signal.{h,cpp}`, which is **not**
modified.

---

## 2. RenderGraph Execution Generalization

### 2.1 `src/render_graph/include/atlantis/render_graph/execution.h` (modify)

`ResourceBinding` gains two fields, meaningful only for `target`-shaped
(`RenderTarget`) entries:

- `incomingState` (`ResourceState`, default `Undefined`) — safe only for a
  resource being bound to its first `execute()` call within its current
  `CommandList`/frame; using the default for a resource already touched by
  an earlier `execute()` sharing the same `CommandList` is a caller
  precondition violation that silently discards prior contents — **not
  guaranteed-detectable**.
- `finalState` (`std::optional<ResourceState>`, **no default** — every
  `target`-shaped binding entry must supply one explicitly);
  `std::nullopt` means "no trailing transition beyond whatever the last
  pass leaves it in".

### 2.2 `src/render_graph/src/execution.cpp` (modify)

`execute()` seeds a bound resource's tracked state from `incomingState`
(instead of hardcoded `Undefined`) and inserts a trailing
`transitionResource()` to `finalState` when one is supplied and differs
from the resource's ending state (instead of the hardcoded trailing
`PresentSource` transition). Otherwise unchanged: walk compiled pass
order, track most-recently-recorded state per bound resource, insert
`transitionResource()` on a state change. Guard 1 and Guard 2 unchanged;
the bound depth `Texture`'s binding (never trailing-transitioned)
unaffected. Inserts **no** transition at all when `incomingState` already
equals the state a resource's one usage declares (the readback copy pass's
case).

### 2.3 `tests/render_graph/fake_command_list.h` (modify)

Add a `copyRenderTargetToBuffer()` override (recording it into a
test-visible list) so `FakeCommandList` remains instantiable after 1.4's
pure-virtual addition.

### 2.4 `tests/render_graph/headless_binding_tests.cpp` (new)

GPU-independent cases: (1–8) `incomingState` seeding vs. default;
`finalState` trailing transition vs. `std::nullopt` vs. never-used
resource; **no** transition when `incomingState` already equals the
declared state; a single `writes()` usage tagged `TransferSource` does not
trigger draw-pass recognition; Guard 1/Guard 2 with and without the new
fields. (9) the `copyRenderTargetToBuffer()`-recording path via
`FakeCommandList` (added in Step 4).

---

## 3. `Renderer::drawFrame()`'s `finalColorState` Parameter

### 3.1 `src/renderer/include/atlantis/renderer/renderer.h` (modify)

`drawFrame()` gains one new, required parameter `atlantis::rhi::ResourceState
finalColorState` (exact position a Plan-stage detail). Its thread-safety
comment stays "not thread-safe; caller-thread-only".

### 3.2 `src/renderer/src/renderer.cpp` (modify)

`Renderer` passes `finalColorState` through, unmodified and uninspected,
as the `finalState` field of its own internal color `ResourceBinding`
entry; `incomingState` stays at its default. The draw pass itself is
unchanged — still exactly one `writes()` usage tagged
`ColorAttachmentOutput`. `Renderer` gains no knowledge of `Presentation`,
`OffscreenTarget`, or any origin-specific concept.

### 3.3 `tests/renderer/renderer_ownership_tests.cpp` (extend in place)

A new `[renderer][final_color_state]` case: `Renderer::drawFrame()`'s
internal `ResourceBinding` construction correctly threads its new
parameter through as `finalState`, for both a `PresentSource`- and a
`TransferSource`-valued argument, without inspecting or branching on the
value — asserting against `FakeCommandList`'s recorded `transitions` for
`Renderer`'s own internal draw pass only, never
`copyRenderTargetToBuffer()`. Dependency: after 3.1, 3.2 (entirely within
Step 3).

---

## 4. Windowed Call-Site Updates (mechanical, non-behavioral)

Each supplies exactly the value the old hardcoded behavior already
produced — no windowed behavior change:

1. `src/renderer/src/renderer.cpp` — supplies `finalState =
   finalColorState` (Section 3.2).
2. `examples/frame_execution_demo/main.cpp` — its own direct,
   non-`Renderer` `execute()` call supplies `finalState =
   ResourceState::PresentSource` explicitly.
3. `examples/minimal_renderer_demo`'s verification composition — passes
   `ResourceState::PresentSource` as `Renderer::drawFrame()`'s new
   `finalColorState` argument.

These land in the same bundle as Section 2/3 (Step 3) because
`ResourceBinding::finalState` has no default a caller can rely on — a
windowed path left un-updated would still build but quietly stop reaching
`PresentSource`, exactly the silent defect this spec's Round 1 Human
Review found and fixed once.

---

## 5. Vulkan Backend — Offscreen Target and the Shared Access Boundary

### 5.1 `src/vulkan_backend/src/vulkan_render_target_access.h` (new — shared private interface)

`VulkanRenderTargetAccess` — a pure-virtual interface exposing `image()` →
`VkImage`, `imageView()` → `VkImageView`, `acquireCompleteSemaphore()` →
`VkSemaphore`, `renderFinishedSemaphore()` → `VkSemaphore` (each
`noexcept`). Header-only, no `.cpp` (mirroring every other private
interface header in the directory). All adoption is via the **checked
pointer-form** `dynamic_cast<VulkanRenderTargetAccess*>(&target)` +
`ATLANTIS_CHECK_MSG(access != nullptr, "<method name> received a
RenderTarget not produced by this module")`, mirroring
`vulkan_presentation.cpp:651` — never the reference-form
`dynamic_cast<T&>`, which throws `std::bad_cast` into the render path
(exceptions are not disabled anywhere; `/EHsc` default). A failed cast is
a programmer error (ADR-0014 — a `RenderTarget` this module did not
construct). No RTTI/`/GR` build-option change.

### 5.2 `src/vulkan_backend/src/vulkan_render_target.h` (modify — inheritance-list only)

`VulkanRenderTarget` additionally inherits `VulkanRenderTargetAccess`; its
own accessor bodies are unchanged, so for any `VulkanRenderTarget`
argument the checked pointer-form cast is always non-null and produces
byte-identical results to the previous `static_cast<VulkanRenderTarget&>`.
This change is **unconditional**, not contingent on any remaining Plan
Review choice.

### 5.3 `vulkan_offscreen_target.{h,cpp}` and `vulkan_offscreen_render_target.{h,cpp}` (new)

- **`VulkanOffscreenTarget`** (owning; the `atlantis::rhi::OffscreenTarget`
  implementation) — members `VkDevice device_`, `VkImage image_`,
  `VkDeviceMemory memory_`, `VkImageView imageView_`, `Extent2D extent_`,
  `Format format_`, `bool outstandingBorrow_ = false`. Constructor takes
  `(device, image, memory, imageView, extent, format)`. Public
  (Vulkan-Backend-internal-only) accessors `image()`/`imageView()`/
  `extent()`/`format()` back the borrow type's own implementations;
  `VulkanOffscreenTarget` itself does **not** implement `RenderTarget` or
  `VulkanRenderTargetAccess` — it is the owner, never itself vended.
  `acquireTarget()`: `ATLANTIS_CHECK_MSG(!outstandingBorrow_, "...
  outstanding")`, then `outstandingBorrow_ = true`, then
  `Ok(std::make_unique<VulkanOffscreenRenderTarget>(this))` — a
  single-argument constructor call. No `Err` path is actually reachable in
  this round (Blocker 1). Destructor:
  `ATLANTIS_CHECK_MSG(!outstandingBorrow_, "... vended borrow still
  outstanding")`, then releases `imageView_`/`memory_`/`image_` in that
  order (mirrors `VulkanTexture::~VulkanTexture()`). **Does not** call
  `vkDeviceWaitIdle()` or wait on any fence — per ADR-0038, the same
  "destructor does not itself wait" tier as
  `VulkanPresentation::~VulkanPresentation()`. `void
  clearOutstandingBorrow() noexcept` — called only by the borrow's own
  destructor (via `friend` or a narrow accessor — a Plan-stage open
  point).
- **`VulkanOffscreenRenderTarget final : public RenderTarget, public
  VulkanRenderTargetAccess`** (non-owning; precedent
  `vulkan_render_target.h/.cpp`) — **holds no duplicate handle members**:
  a single `VulkanOffscreenTarget* owner_` (non-owning, must outlive this
  borrow — enforced by the outstanding-borrow contract, not the type
  system); `explicit VulkanOffscreenRenderTarget(VulkanOffscreenTarget*
  owner)`; deleted copy/move; destructor calls `owner_->clearOutstandingBorrow()`
  and has no other Vulkan side effect. `extent()`/`format()`/`image()`/
  `imageView()` each delegate to `owner_`; `acquireCompleteSemaphore()`/
  `renderFinishedSemaphore()` return `VK_NULL_HANDLE` (headless has no
  presentation semaphores). Lands together with 5.1 (needs
  `VulkanRenderTargetAccess`) and Section 1.2 (needs the interface), via a
  forward declaration in whichever header compiles first. GPU-required
  tests only (Section 7.2).

### 5.4 `src/vulkan_backend/src/vulkan_device.{h,cpp}` (modify) — `createOffscreenTarget()`

Follows `VulkanDevice::createTexture()`'s exact pattern (`vkCreateImage` →
`vkGetImageMemoryRequirements` → `selectMemoryTypeIndexForDevice`
(`VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT`) → `vkAllocateMemory` →
`vkBindImageMemory` → `vkCreateImageView`), adapted for a color,
transfer-source-capable image: `VkImageCreateInfo` with `format =
toVkFormat(params.format)` (the existing helper, reused verbatim), `usage
= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT`,
otherwise identical in shape to `createTexture()`'s (`VK_IMAGE_TYPE_2D`, 1
mip, 1 array layer, `VK_SAMPLE_COUNT_1_BIT`, `VK_IMAGE_TILING_OPTIMAL`,
`VK_SHARING_MODE_EXCLUSIVE`, `VK_IMAGE_LAYOUT_UNDEFINED` initial); memory
`VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` (an offscreen color target is
GPU-written, GPU-read by the copy, never CPU-mapped directly — only the
separate readback `Buffer`, 6.3, is host-visible); `VkImageViewCreateInfo`
`VK_IMAGE_VIEW_TYPE_2D`, `VK_IMAGE_ASPECT_COLOR_BIT`. Every `VkResult`
checked; every failure path releases whatever was already created before
returning `Err(OffscreenTargetCreateError::...)`, exact mirror of
`createTexture()`'s cleanup ordering. On success:
`Ok(std::make_unique<VulkanOffscreenTarget>(device_, image, memory,
imageView, params.extent, params.format))`. 1.3's declaration and this
override cannot be separated in a compiling repository.

---

## 6. Vulkan Backend — Adopting the Shared Access Boundary at All Four Call Sites, and Readback Capability

### 6.1 `src/vulkan_backend/src/vulkan_command_list.cpp` (modify) — `transitionResource()`, `clearColor()`, `beginRendering()`

**Must Fix — the core of this revision.** All three methods currently do
`auto& vulkanTarget = static_cast<VulkanRenderTarget&>(target|color);` at
their first line (`:50`, `:91`, `:106`). Each becomes, pointer-form,
exception-free:

```
auto* access = dynamic_cast<VulkanRenderTargetAccess*>(&target);  // or &color
ATLANTIS_CHECK_MSG(access != nullptr,
                    "<method>() received a RenderTarget not produced by this module");
```

and every subsequent `vulkanTarget.image()`/`.imageView()` becomes
`access->image()`/`access->imageView()` — no other line in any of the
three methods changes. Each `ATLANTIS_CHECK_MSG` message names its
specific method, matching the existing per-call-site wording convention.
`beginRendering()`'s `vulkanTarget.extent()` becomes `color.extent()`
(`extent()` is public, polymorphic, needs no cast). **`clearColor()` is
fixed even though this spec's headless path never calls it** — leaving its
unconditional cast unfixed would leave a latent, identical-shape UB trap
for any future caller; this Plan fixes all three `VulkanCommandList`
methods uniformly. Dependency: after 5.1, 5.2, 5.3.

### 6.2 `src/vulkan_backend/src/vulkan_device.cpp` (modify) — `submit()`

- Pointer-form, exception-free: `const auto* access = dynamic_cast<const
  VulkanRenderTargetAccess*>(&target); ATLANTIS_CHECK_MSG(access !=
  nullptr, "submit() received a RenderTarget not produced by this
  module");` replaces line 521's `static_cast`. `waitSemaphore`/
  `signalSemaphore` are read from `access->acquireCompleteSemaphore()`/
  `renderFinishedSemaphore()` exactly as today.
- `VkSubmitInfo` construction becomes **conditional**:
  `waitSemaphoreCount = waitSemaphore != VK_NULL_HANDLE ? 1 : 0`,
  `pWaitSemaphores`/`pWaitDstStageMask` set to `nullptr` when the
  semaphore is null; symmetrically for signal. For a `VulkanRenderTarget`
  argument both are always non-null, so that branch is always taken
  exactly as the old unconditional path was — byte-identical `VkSubmitInfo`
  for the windowed case, confirmed by Section 9. For the headless
  (`VulkanOffscreenRenderTarget`) case, both counts are `0` and the
  pointers `nullptr`.
- `std::make_unique<VulkanSubmissionSignal>(signalSemaphore)` is
  constructed with `VK_NULL_HANDLE` for the headless case — safe by
  inspection of `vulkan_submission_signal.cpp` (a trivial unconditional
  member-initializer store, no validation) and by construction (this
  signal is never passed to `Presentation::present()` for a headless
  submission). Dependency: after 5.1–5.3; independent of 6.1.

### 6.3 `src/vulkan_backend/src/vulkan_device.cpp` — `createBuffer()` readback purpose mapping

Add `case BufferPurpose::Readback: usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
break;` to the existing `switch (params.purpose)` — no other change; the
existing host-visible/host-coherent memory-property selection and
always-mapped-at-creation behavior already apply uniformly to every
purpose (ADR-0023). GPU-independent-ish (buffer creation always needs a
real device; Section 7.2).

### 6.4 `src/vulkan_backend/src/resource_state_mapping.cpp` (modify) — new `planTransition()` entry

Add `colorAttachmentOutputToTransferSource()` returning an
`ImageBarrierPlan` with `oldLayout =
VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`, `newLayout =
VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`, `srcAccessMask =
VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT`, `dstAccessMask =
VK_ACCESS_TRANSFER_READ_BIT`, `srcStage =
VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`, `dstStage =
VK_PIPELINE_STAGE_TRANSFER_BIT`; and one new branch in `planTransition()`:
`if (before == ColorAttachmentOutput && after == TransferSource) return
colorAttachmentOutputToTransferSource();`, alongside the existing
`ColorAttachmentOutput`-involving branches. **The one and only new table
entry.** GPU-independent tests (`resource_state_mapping_tests.cpp`,
extended): (a) `ColorAttachmentOutput → TransferSource` produces the exact
plan without asserting; (b) a still-unlisted pair (e.g.
`ColorAttachmentOutput → DepthAttachmentReadWrite`) continues to fire
`ATLANTIS_CHECK_MSG(false, ...)` under a non-terminating test handler,
confirming exactly one entry added.

### 6.5 `src/vulkan_backend/src/vulkan_command_list.{h,cpp}` (modify) — `copyRenderTargetToBuffer()`

Declare 1.4's override and implement it after
`VulkanCommandList::clearColor()`'s implementation shape: pointer-form
`dynamic_cast<VulkanRenderTargetAccess*>(&source)` +
`ATLANTIS_CHECK_MSG(... != nullptr, ...)`; `static_cast<VulkanBuffer&>(destination)`
(only one concrete `Buffer` implementation exists in Phase 1, ADR-0001);
a `VkBufferImageCopy` with `bufferOffset = 0`, `bufferRowLength = 0`,
`bufferImageHeight = 0` (0 = tightly packed, ADR-0040), `imageSubresource
= {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}`, `imageOffset = {0,0,0}`,
`imageExtent = {source.extent().width, source.extent().height, 1}`; then
`vkCmdCopyImageToBuffer(commandBuffer_, access->image(),
VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vulkanBuffer.vkBuffer(), 1,
&region)`. **No additional `vkCmdPipelineBarrier` for host-visibility** —
`destination`'s memory is host-coherent (ADR-0023) and
`Device::waitIdle()`'s `vkDeviceWaitIdle()` already establishes the
device-to-host visibility guarantee for a fully-drained device, matching
the camera-uniform `Buffer`'s own write-timing argument (ADR-0023). **This
must be confirmed empirically by Section 7.2's GPU test reading back real,
correct pixel data — not assumed from this reasoning alone.** Dependency:
after 1.4, 5.1, 5.3, 6.4.

---

## 7. Headless Verification Composition and GPU Tests

### 7.1 New: `examples/headless_rendering_demo/{main.cpp,CMakeLists.txt}`

A new, non-shipping example (structural precedent
`examples/minimal_renderer_demo/main.cpp`) that: constructs a `Device`
(`vulkan_backend::createDevice()`, `enableValidationLayers = true`) with
**no `atlantis::platform::initialize()` call anywhere** (structurally
verifiable by `grep`/inspection); constructs the same fixed
`Mesh`/`Material`/camera-uniform `Buffer` fixture `minimal_renderer_demo`
uses (**duplication** is this Plan's default per Plan 0010 Blocker 2 —
matching every prior example's self-contained style over a new shared
`examples/`-level library target); constructs one `OffscreenTarget`
(`device->createOffscreenTarget({.extent = {512, 512}, .format =
Format::Rgba8Unorm})` — candidate resolution/format, Blocker-confirmable)
and one readback `Buffer` (`BufferPurpose::Readback`, `512*512*4` bytes);
constructs one depth `Texture` via the existing unchanged
`Device::createTexture()`; runs the acquire → write-camera →
`createCommandList()` → `Renderer::drawFrame(..., TransferSource)` →
caller-built copy-pass graph → `submit()` → `waitIdle()` → read →
content-check → drop borrow → (loop or exit) cycle from Spec 0010's flow
diagram; runs the cycle more than once against the same `OffscreenTarget`
(candidate: 3 iterations); implements a **reproducible basic content
check** (candidate: read the center pixel — expect mesh-colored,
non-background — and the four corner pixels — expect background-clear —
from the mapped readback `Buffer`, comparing against a small tolerance;
return `EXIT_FAILURE` and log specifics on mismatch); on **every** exit
path calls `Device::waitIdle()` before destroying `OffscreenTarget`,
matching ADR-0038's documented destruction precondition. `CMakeLists.txt`
mirrors `examples/minimal_renderer_demo/CMakeLists.txt` exactly except
**`Atlantis::Platform` is not linked** (no-window requirement); links
`Atlantis::Core`, `RHI`, `VulkanBackend`, `RenderGraph`, `Renderer`,
`ShaderSystem`, `ShaderSystemRhiIntegration`; same `minimal_mesh_shaders`
dependency and post-build shader-copy; a `run_headless_rendering_demo`
custom target. Root `CMakeLists.txt` adds
`add_subdirectory(examples/headless_rendering_demo)`. The executable's
own successful run (exit 0, Validation-Layers-clean) *is* the
manual/automated verification.

### 7.2 New: `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`

New Catch2 file, `[vulkan_backend][gpu][headless]`, added to the existing
`atlantis_vulkan_backend_gpu_tests` executable's source list (same
`minimal_mesh_shaders` dependency/working-directory setup). Covers, per
Spec 0010's Testing & Verification Plan: (1) creating and destroying an
`OffscreenTarget` — Validation-Layers-clean; (2) acquiring, using (a
trivial submit, no draw), and returning (destroying the borrow) more than
once, confirming a second `acquireTarget()` succeeds only after the first
borrow is destroyed/reset; (3) a second `acquireTarget()` *before*
returning the first fires the expected assertion (under this codebase's
existing non-terminating test-handler pattern — reuse the existing
mechanism, e.g. `render_graph`'s Guard tests, not a new one); (4)
destroying `OffscreenTarget` while a borrow is outstanding fires the
equivalent assertion; (5) dropping a borrow immediately after `submit()`
returns (before `waitIdle()`), followed by a correct subsequent cycle with
no Validation Layer warning/error — the minimum-borrow-lifetime contract
in practice; (6) creating and destroying a readback `Buffer`; (7) **one
full render-and-readback cycle** (`Renderer::drawFrame(finalColorState =
TransferSource)` → copy pass → `submit()` → `waitIdle()` → read),
Validation-Layers-clean, **confirming no
`VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` transition is ever recorded** for the
headless target (via captured barrier data or Validation Layers'
object-tracking output — exact mechanism left to Implementation) — the
single test case that end-to-end exercises all five corrected call sites
(`beginRendering()`/`transitionResource()` via `Renderer::drawFrame()`'s
internal graph, `copyRenderTargetToBuffer()` via the copy pass, and
`submit()`); (8) a full cycle calling `Device::waitIdle()` immediately
after `submit()` — before dropping the borrow, before destroying
`OffscreenTarget` — confirming the documented correct-order flow is
Validation-Layers-clean with no reliance on `Device`'s destructor-time
drain. Run via `ctest -L gpu`.

---

## 8. Testing Strategy (consolidated)

Maps directly to Spec 0010's Testing & Verification Plan; no new testing
philosophy beyond
[testing-strategy.md](../docs/process/testing-strategy.md).

- **GPU-independent unit tests** (`ctest -LE gpu`, Debug **and**
  Release): Sections 1.1, 2.4 (items 1–8), 3.3, 6.4 — new/extended Catch2
  cases, zero real `VkDevice`. No new warning.
- **GPU-required tests** (`ctest -L gpu`, Debug **and** Release): Section
  7.2 (new headless coverage) plus the **unmodified** re-run of every
  existing GPU-required case (`vulkan_presentation_gpu_tests.cpp`,
  `frame_execution_gpu_tests.cpp`, `minimal_renderer_gpu_tests.cpp`) —
  this Plan's regression gate for the windowed path (Section 9).
- **Manual/automated verification composition:** Section 7.1's
  `headless_rendering_demo` — run (scripted, needs no window/input) at
  least once per Debug and Release build.
- **Headless integration tests (layer 2):** Section 7.2's file **is** this
  layer's first instance; this Plan does not additionally formalize a
  distinct CI job/label beyond the existing `gpu` CTest label (an open
  question Spec 0006/0007 also left open).
- **Image regression tests:** N/A (Non-Goals).
- **Vulkan Validation Layers:** mandatory, zero warnings/errors, for every
  Debug build and every GPU-touching test/demo run this Plan adds or
  re-runs.
- **Debug/Release:** every GPU-required test and the new demo built and
  run in both configurations before Implementation is complete.
- **Real-GPU-unavailable disclosure:** if the environment implementing
  this Plan lacks a Vulkan-capable GPU/driver at verification time, the
  Implementation PR must **explicitly state this** (not silently skip
  GPU-required coverage) and list exactly which of Section 7.2's cases
  were exercised on real hardware vs. verified by code inspection/
  GPU-independent tests only — the same disclosure discipline Spec 0007's
  Testing & Verification Plan already required.

---

## 9. Windows Windowed Path Regression Verification

**Spec Non-Goal:** "Any regression to the windowed path."

- Re-run `examples/frame_execution_demo` and
  `examples/minimal_renderer_demo` interactively after their Section 4
  mechanical updates, confirming behavior identical to their Spec
  0006/0007-verified baseline: visible frame, correct interactive resize,
  correct minimize/restore (zero Vulkan calls while minimized), clean exit
  on every path including a deliberate mid-frame exit, Vulkan Validation
  Layers clean throughout.
- Re-run the full existing GPU-required test suite
  (`vulkan_presentation_gpu_tests.cpp`, `frame_execution_gpu_tests.cpp`,
  `minimal_renderer_gpu_tests.cpp`) unmodified, confirming identical pass
  counts to the pre-this-Plan baseline.
- Confirm, by inspection, that **all four** call sites Section 5/6 change
  (`transitionResource()`, `clearColor()`, `beginRendering()` — 6.1;
  `submit()` — 6.2) produce identical behavior for any
  `VulkanRenderTarget`-sourced argument: the same `VkImage`/`VkImageView`
  via the checked pointer-form `dynamic_cast<VulkanRenderTargetAccess*>`
  (always non-null for a real `VulkanRenderTarget`, so the paired
  `ATLANTIS_CHECK_MSG` never fires on this path) as were previously
  obtained via `static_cast<VulkanRenderTarget&>` (identical, since
  `VulkanRenderTarget`'s accessor bodies are unchanged — 5.2), and
  `submit()`'s `VkSubmitInfo` byte-identical (non-null semaphores on both
  sides). The polymorphic-dispatch mechanism must be a behavior-preserving
  refactor for the windowed path at all four sites, not merely "probably
  fine" — `frame_execution_gpu_tests.cpp` (exercises
  `clearColor()`/`transitionResource()`) plus `minimal_renderer_gpu_tests.cpp`
  (exercises `transitionResource()`/`beginRendering()`/`submit()`) re-run
  unmodified is the concrete evidence, not the inspection alone.

---

## 10. Documentation and Registry Post-Implementation Updates

Deferred to the Implementation PR itself (not this Plan, not a separate
PR) — matching Spec 0006/0007/0008's own Implementation PRs:

- `specs/README.md`: Spec 0010's row — Implementation column updated from
  "Not started" to a description of what actually shipped (files created,
  PR link(s), verification summary, any disclosed limitations from Section
  8's real-GPU-availability disclosure), mirroring Spec 0006/0007's rows'
  level of detail.
- `docs/project-blueprint.md`: a new milestone entry (or an update to the
  existing Section 5 roadmap), **only if** Plan Review confirms this is in
  scope for the Implementation PR — an explicit Implementation-time
  decision, not pre-committed here.
- `docs/architecture/*.md`: updated only if Implementation reveals a
  genuine as-built architecture fact these documents' `PROPOSED` banners
  should reflect — not assumed necessary.

---

## Explicit Prohibitions (grep/code-review checklist)

Every item below must hold, verifiable by inspection, before this Plan's
Implementation is considered complete:

- [ ] No `.spv`/reflection JSON, shader source, or Shader System change
      anywhere.
- [ ] No `src/platform/` change, and no `atlantis::platform::*` symbol
      referenced anywhere in `examples/headless_rendering_demo/` or
      `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`.
- [ ] No `VkSwapchainKHR`, `VkSurfaceKHR`, or `Presentation`/
      `VulkanPresentation` reference anywhere in the same two files.
- [ ] No second `planTransition()` entry beyond `ColorAttachmentOutput →
      TransferSource`.
- [ ] No new `Texture`/`DepthFormat` usage kind, method, or field.
- [ ] No new third-party CMake `find_package`/`FetchContent` call.
- [ ] No `VkRenderPass`/`VkFramebuffer` object anywhere (confirmed still
      absent).
- [ ] No public `release()`/`consume()` method on `OffscreenTarget` or any
      `RenderTarget` implementation.
- [ ] No implicit `vkDeviceWaitIdle()`/fence-wait call inside
      `VulkanOffscreenTarget`'s or `VulkanOffscreenRenderTarget`'s own
      destructor.
- [ ] `git grep -n "Android\|ANativeWindow\|android_main"` under `src/`,
      `tests/`, `examples/` added by this Plan returns nothing.
- [ ] `Renderer`'s dependency set (`atlantis_renderer`'s
      `target_link_libraries`) is unchanged — still exactly
      `Atlantis::RHI`, `Atlantis::RenderGraph`, `Atlantis::Core`.
- [ ] `git grep -n "static_cast<VulkanRenderTarget" src/vulkan_backend/src/`
      returns **exactly one** match (`vulkan_presentation.cpp`'s
      `present()`, windowed-only, deliberately unchanged) — zero matches
      in `vulkan_command_list.cpp` or `vulkan_device.cpp`, confirming all
      four pre-existing in-scope call sites were migrated to the checked
      pointer-form, none missed, and the fifth newly-added call site
      (`copyRenderTargetToBuffer()`) uses the identical checked
      pointer-form from the start.
- [ ] `git grep -n "dynamic_cast<VulkanRenderTargetAccess&\|dynamic_cast<const VulkanRenderTargetAccess&"
      src/vulkan_backend/src/` returns **zero** matches — every adoption
      uses the checked pointer-form (`dynamic_cast<...*>` +
      `ATLANTIS_CHECK_MSG(... != nullptr, ...)`), never the
      exception-throwing reference-form (AGENTS.md's exception-free render
      path rule).
- [ ] `VulkanOffscreenRenderTarget` declares no `VkImage`/`VkImageView`/
      `Extent2D`/`Format` member of its own — each accessor delegates to
      `owner_` (Section 5.3's duplicate-free member list).
- [ ] No new CMake compiler/RTTI option (no explicit `/GR` or `/GR-`
      toggle) is added anywhere this Plan touches.
- [ ] `src/rhi/include/atlantis/rhi/device.h`'s `submit()` doc comment and
      `submission_signal.h`'s class comment state the
      present()-before-next-submit() precondition as windowed-only,
      consistently with each other (Sections 1.3, 1.6).

## Build Integration

- `src/rhi/CMakeLists.txt`: **no change** — `offscreen_target.h` is a
  header-only interface addition (matching `render_target.h`/
  `presentation.h`); confirm at implementation time.
- `src/vulkan_backend/CMakeLists.txt`: add
  `src/vulkan_offscreen_target.cpp` and
  `src/vulkan_offscreen_render_target.cpp` to `atlantis_vulkan_backend`'s
  source list. `vulkan_render_target_access.h` needs no entry
  (header-only).
- `tests/render_graph/CMakeLists.txt`: add `headless_binding_tests.cpp`.
- `tests/vulkan_backend/CMakeLists.txt`: add
  `headless_rendering_gpu_tests.cpp` to
  `atlantis_vulkan_backend_gpu_tests`'s source list.
- `tests/rhi/CMakeLists.txt` / `tests/renderer/CMakeLists.txt`: no new
  file — `types_tests.cpp` / `renderer_ownership_tests.cpp` extended in
  place.
- Root `CMakeLists.txt`: add
  `add_subdirectory(examples/headless_rendering_demo)` + the new example's
  own `CMakeLists.txt` (Section 7.1).

## Sequencing & Dependencies

Every step ends with a repository that compiles and whose own new/updated
tests pass. A pure-virtual method's declaration and *every one* of its
concrete overrides are one inseparable unit, never split across steps —
because adding a new pure-virtual to an already-implemented abstract
interface (`Device`, `CommandList`) makes every existing concrete
implementer — `VulkanDevice`, `VulkanCommandList`, **and** the test double
`FakeCommandList` — abstract and uninstantiable the moment that method is
declared.

1. **Step 1 — RHI value types (no interface change):** Sections 1.1, 1.5,
   1.6. Enum values, `OffscreenTargetCreateParams`, the two error enums,
   the `Format` doc-comment update, and 1.6's comment fix — no new pure
   virtual, so no existing concrete class affected. Ends compilable;
   `tests/rhi/types_tests.cpp`'s new/extended cases pass.
2. **Step 2 — `OffscreenTarget` interface (brand-new type, zero existing
   implementers):** Section 1.2. Ends compilable.
3. **Step 3 — RenderGraph binding + `Renderer` + windowed call-site
   updates, one bundle:** Sections 2.1, 2.2, 3.1, 3.2, 4.1, 4.2, plus
   `tests/render_graph/headless_binding_tests.cpp` items 1–8 (2.4) and
   `tests/renderer/renderer_ownership_tests.cpp` (3.3). Bundled because
   `ResourceBinding::finalState` has no default a caller can rely on, so
   `renderer.cpp`/both demos must update in the **same** step to avoid a
   *silent* behavior regression. Does not touch `Device`/`CommandList`.
   Ends compilable; new/updated tests pass; windowed demos compile with
   intended-unchanged behavior (verified for real in Step 6).
4. **Step 4 — `Device`/`CommandList` interface additions + every concrete
   override, one single necessarily large bundle:** Sections 1.3, 1.4,
   5.1, 5.2, 5.3, 5.4, 6.1, 6.2, 6.3, 6.4, 6.5, plus
   `tests/render_graph/fake_command_list.h`'s `copyRenderTargetToBuffer()`
   override (2.3) and `headless_binding_tests.cpp` item 9 (2.4).
   `Device::createOffscreenTarget()` and
   `CommandList::copyRenderTargetToBuffer()` are declared (1.3, 1.4) in
   the **same** step as every one of their concrete overrides
   (`VulkanDevice`, `VulkanCommandList`, `FakeCommandList`). Internal
   order: 5.1 first; 5.2 and 5.3 next (either order); 5.4 after 5.3;
   6.1/6.2 after 5.1–5.3; 6.3/6.4 independent of the rest and of each
   other; 6.5 after 5.1, 5.3, 6.4. Ends compilable; GPU-independent tests
   (6.4's new cases) pass immediately; the full GPU-required verification
   is Step 6's job.
5. **Step 5 — Headless verification composition and GPU tests:** Sections
   7.1, 7.2 — depends on Steps 1–4 in full. Ends compilable and (on real
   hardware) passing.
6. **Step 6 — Windowed regression verification and documentation:**
   Sections 8, 9, 10 — depends on Step 5. Confirms Steps 3's and 4's
   combined effect on the windowed path is behavior-preserving, on real
   hardware, not merely by inspection.

A single Implementation PR landing Steps 1–6 together is this Plan's
expected shape; splitting into multiple PRs is a Plan-Review-confirmable
choice provided no PR boundary ever lands inside Step 4 (which cannot be
subdivided further without breaking compilation).

## Files / Modules Touched (expected)

**New:** `src/rhi/include/atlantis/rhi/offscreen_target.h`,
`src/vulkan_backend/src/vulkan_render_target_access.h`,
`src/vulkan_backend/src/vulkan_offscreen_target.{h,cpp}`,
`src/vulkan_backend/src/vulkan_offscreen_render_target.{h,cpp}`,
`examples/headless_rendering_demo/{main.cpp,CMakeLists.txt}`,
`tests/render_graph/headless_binding_tests.cpp`.

**Modified:** `src/rhi/include/atlantis/rhi/{types,device,command_list}.h`
(`device.h`'s `submit()` doc comment also amended — Section 1.3),
`src/rhi/include/atlantis/rhi/submission_signal.h` (comment-only — Section
1.6; not to be confused with the distinct, unmodified
`src/vulkan_backend/src/vulkan_submission_signal.{h,cpp}`),
`src/rhi/src/types.cpp`,
`src/render_graph/include/atlantis/render_graph/execution.h`,
`src/render_graph/src/execution.cpp`,
`src/renderer/include/atlantis/renderer/renderer.h`,
`src/renderer/src/renderer.cpp`,
`src/vulkan_backend/src/vulkan_device.{h,cpp}`,
`src/vulkan_backend/src/vulkan_render_target.h` (inheritance-list-only,
**unconditional**), `src/vulkan_backend/src/vulkan_command_list.{h,cpp}`,
`src/vulkan_backend/src/resource_state_mapping.cpp`,
`src/vulkan_backend/CMakeLists.txt`,
`examples/frame_execution_demo/main.cpp`,
`examples/minimal_renderer_demo/main.cpp`,
`tests/render_graph/fake_command_list.h`,
`tests/render_graph/CMakeLists.txt`,
`tests/renderer/renderer_ownership_tests.cpp`,
`tests/rhi/types_tests.cpp`,
`tests/vulkan_backend/{CMakeLists.txt,resource_state_mapping_tests.cpp}`,
`CMakeLists.txt` (root), `specs/README.md` (post-implementation, Section
10).

**Explicitly not touched:** `src/vulkan_backend/src/vulkan_presentation.cpp`
(its own `static_cast<VulkanRenderTarget&>` in `present()` stays exactly
as-is — headless never constructs `Presentation`) and
`src/vulkan_backend/src/vulkan_submission_signal.{h,cpp}` (the Vulkan
Backend's concrete `VulkanSubmissionSignal`, distinct from the RHI-level
abstract `submission_signal.h` Section 1.6 amends — confirmed by reading
its implementation to be a trivial unconditional member-initializer store
with no non-null assumption).

If Implementation touches a file not listed here, that is a deviation to
call out explicitly in the Implementation PR.

## Verification Checklist

- [ ] Unit tests (GPU-independent, `ctest -LE gpu`, Debug **and**
      Release): Sections 1.1, 2.4 (items 1–8), 3.3, 6.4 pass, no new
      warning introduced.
- [ ] Headless integration tests (GPU-required, `ctest -L gpu`, Debug
      **and** Release): Section 7.2 passes on real Vulkan-capable hardware
      (or explicitly disclosed as not exercised, per Section 8) —
      including item 7, confirmed to exercise all five corrected call
      sites (`transitionResource()`, `clearColor()`, `beginRendering()`,
      `submit()`, `copyRenderTargetToBuffer()`) end to end.
- [ ] Image regression tests: N/A (Non-Goal) — not implemented, not
      stubbed, not partially scaffolded.
- [ ] **No reference-form `dynamic_cast` anywhere this Plan introduces:**
      `git grep -n "dynamic_cast<VulkanRenderTargetAccess&\|dynamic_cast<const VulkanRenderTargetAccess&"
      src/vulkan_backend/src/` returns **zero** matches — every one of the
      five call sites (6.1 ×3, 6.2, 6.5) uses the pointer-form followed by
      `ATLANTIS_CHECK_MSG(... != nullptr, ...)`, matching
      `vulkan_presentation.cpp:651`. A type mismatch fails via this
      assertion, Debug **and** Release (`ATLANTIS_CHECK_MSG` is
      unconditional) — never via an uncaught `std::bad_cast`.
- [ ] **Headless `submit()`'s `VkSubmitInfo` has zero wait/signal
      semaphore count, not a null handle in a non-zero-count array:**
      confirmed by 7.2's item 7 and, if feasible, by inspecting
      `waitSemaphoreCount`/`signalSemaphoreCount` directly in a
      debugger/log — both `0` and `pWaitSemaphores`/`pSignalSemaphores`
      `nullptr` for a `VulkanOffscreenRenderTarget` argument.
- [ ] **Windowed semaphore behavior byte-identical to the pre-this-Plan
      baseline:** confirmed by `frame_execution_gpu_tests.cpp`/
      `minimal_renderer_gpu_tests.cpp` re-run unmodified and passing —
      `VulkanRenderTarget`'s semaphore accessors never return
      `VK_NULL_HANDLE`, so counts remain `1` and pointers non-null for
      every existing windowed call site.
- [ ] Vulkan Validation Layers clean: for every GPU-touching test and demo
      run this Plan adds, and for the full re-run windowed suite (Section
      9), in both Debug and Release — zero warnings, zero errors.
- [ ] Manual verification: `examples/headless_rendering_demo` runs to
      completion, passes its own basic content check, in both Debug and
      Release.
- [ ] Windowed regression (Debug **and** Release): Section 9's full
      checklist passes with no behavior change from the pre-this-Plan
      baseline, confirmed for **all four** corrected `VulkanCommandList`/
      `VulkanDevice` call sites, not `submit()` alone.
- [ ] Explicit Prohibitions checklist (above) fully checked, including the
      `git grep` check confirming exactly one remaining
      `static_cast<VulkanRenderTarget` site (`vulkan_presentation.cpp`)
      and the reference-form-`dynamic_cast` check above.
- [ ] `git diff --check` clean on every commit.

## Rollback Plan

Steps 1–6 are each independently revertible in reverse order (6 → 1)
without touching an earlier, already-verified step — `git revert` of the
Implementation PR's commit(s) in reverse-chronological order restores the
pre-Plan state exactly, since no step here modifies a file's *meaning* for
any existing, already-shipped caller beyond the explicitly-called-out
mechanical updates (Section 4) and the four-call-site `dynamic_cast`
refactor (6.1, 6.2), both of which Section 9's regression checklist exists
specifically to catch before merge. Two narrower rollback points within
Step 4:

- If the problem is isolated to `transitionResource()`/`clearColor()`/
  `beginRendering()`: revert Section 6.1's three method bodies to their
  original `static_cast` — this reverts headless drawing entirely while
  leaving `submit()`'s fix and the windowed path both intact.
- If the problem is isolated to `submit()`: revert Section 6.2's body to
  its original unconditional `static_cast`/`VkSubmitInfo` construction —
  6.1's fixes remain, but the offscreen path still fails at the submission
  step; the windowed path unaffected either way.

Both narrower rollbacks are strictly worse than fixing the underlying
GPU-verification finding directly once diagnosed, but are documented as
genuine, isolated fallback points.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan:

- "Image regression tests added/updated if rendered output changed" is
  **N/A** — this Plan's own rendered output (the headless demo's
  clear/mesh color) is new, not a change to any existing golden/reference
  output, and Spec 0010 explicitly does not introduce image regression
  tooling.
- "Headless verification performed for any rendering-adjacent change" —
  **this Plan is what makes that phrase concretely testable for the first
  time**; Section 7's coverage is the answer, not a future deferral.

## Human Review / Plan Review Blockers

**Resolved during Plan Review, no longer open:** the polymorphic-signal-
access mechanism (confirmed: the
private-interface-plus-checked-`dynamic_cast` approach,
`VulkanRenderTargetAccess`, applied uniformly across all five call sites —
the always-real-dummy-semaphore alternative dropped);
`OffscreenTargetCreateParams::format`'s default (confirmed
`Format::Rgba8Unorm`); `VulkanOffscreenRenderTarget`'s
constructor/member-list consistency (confirmed single-parameter
constructor, every accessor delegates to `owner_`, no duplicated members);
the reference-form `dynamic_cast<T&>` exception risk (confirmed: every one
of the five call sites uses the checked pointer-form plus
`ATLANTIS_CHECK_MSG(... != nullptr, ...)`, matching
`vulkan_presentation.cpp:651`; no RTTI/build-option change); Section 3.3's
stale dependency on Section 2.3 (confirmed: the new
`[renderer][final_color_state]` case exercises only `Renderer::drawFrame()`'s
internal draw pass and asserts against `FakeCommandList`'s recorded
`transitions`, never `copyRenderTargetToBuffer()`); the stale
`submit()`/`SubmissionSignal` doc comments (confirmed: both `device.h`
(1.3) and `submission_signal.h` (1.6) now state the precondition as
windowed-only, comment-only).

**Still open — design choices flagged for Plan Review:**

1. **`OffscreenAcquireError`'s reachability** (Sections 1.1, 5.3): this
   round's Vulkan Backend implementation has no code path that actually
   produces `Err(...)` from `acquireTarget()` — confirm keeping the
   `Result`-wrapped return shape (matching ADR-0038's literal text,
   forward-consistent with `Presentation`) vs. a bare
   `std::unique_ptr<RenderTarget>` return (smaller, more honest, but a
   literal deviation from the ADR's stated signature — would need its own
   small ADR-0038 clarification).
2. **Fixture code-sharing between `minimal_renderer_demo` and
   `headless_rendering_demo`** (Section 7.1): duplicate the fixed
   `Mesh`/`Material`/camera-`Buffer` setup (this Plan's default) vs.
   factor it into a small shared header/library neither prior example
   needed.
3. **Exact reproducible basic-content-check thresholds** (Section 7.1) —
   center/corner sampling is the candidate shape; exact tolerance/pass
   thresholds left to Implementation unless Plan Review wants them fixed
   here.
4. **Distinct CI/test-category label for headless GPU tests** — Spec
   0006/0007 both flagged this as open and left it open; this Plan does
   the same (Section 8) unless Plan Review wants it resolved now.

**Non-blocking, disclosed limitations carried into Implementation:**

- Section 6.5's "no explicit host-visibility barrier" reasoning is
  confirmed only by Section 7.2's GPU test reading back correct pixel data
  — if that test ever shows incorrect/stale readback bytes with Validation
  Layers otherwise clean, this is the first place to suspect, not a
  re-litigation of Spec 0010/ADR-0040's design.
- The pointer-form `dynamic_cast`'s RTTI dependency (Section 5.1) relies
  on this project's existing MSVC `/GR` default, not currently overridden
  anywhere in this repository's CMake configuration; this Plan does not
  add, remove, or otherwise touch any RTTI/compiler-option build setting.
  A future spec disabling RTTI project-wide would need to revisit this
  mechanism.

**No architectural gap requiring a return to Spec/ADR was found while
producing or revising this Plan, across either Plan Review round.** The
first round's Must Fix (the three additional unconditional `static_cast`
sites) and three Should Fixes, and the second round's Must Fixes
(reference-form `dynamic_cast`'s exception risk; Section 3.3's stale 2.3
dependency) and Should Fix (the stale `submit()`/`SubmissionSignal` doc
comments), are all implementation-shape or documentation-accuracy
corrections within the boundaries Spec 0010 and ADR-0022/0038/0039/0040
already fixed.
