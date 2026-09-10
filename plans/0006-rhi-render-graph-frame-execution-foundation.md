# Plan: RHI / RenderGraph Frame Execution Foundation

- **Spec:** [Spec 0006](../specs/0006-rhi-render-graph-frame-execution-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction; reviewed and
  approved by a human per the Human Review Approval note below.
- **Human Review Approval (2026-08-09):** slmao, completing a joint Spec 0006 +
  Plan 0006 Human Review. The reviewer approved this Plan's candidate public
  API (§2–§7), state machines (acquire's three-outcome result, `execute()`'s
  transition-insertion algorithm, `submit()`'s retained-submission state
  machine, `present()`, and single-frame-in-flight), ownership model,
  synchronization design, implementation order (§16), and verification plan
  (§13). Confirmed explicitly, and **accepted as a deliberate design
  constraint, not a defect:** the `submit()`/`present()` calling precondition —
  a caller must call the matching `present()` for a successful `submit()`
  before calling `submit()` again; this is a documented caller obligation (the
  type system does not, and is not required to, prevent an illegal
  `submit → submit`); `submit()` followed by application exit with no
  `present()` is legal (`Device::waitIdle()` drains and the caller cleans up
  safely); enforcement is via API documentation, §13's ordinary-sequence
  tests, and code review — no new runtime check is added. Implementation is
  authorized, but must not begin until this Plan's own PR has merged into
  `main`.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 2. Original scope,
  ordered work, and verification retained. Candidate C++ headers/algorithms and
  the three-round revision history are preserved in this PR's and
  [PR #23](https://github.com/slmao/Atlantis/pull/23)/[PR #24](https://github.com/slmao/Atlantis/pull/24)'s
  history, not narrated here.

## Objective

Turn Spec 0006's approved contract — a concrete `RenderTarget`, `Presentation`
acquire/present, a minimal `CommandList`/`Device::submit()`, and a RenderGraph
execution capability — into an ordered, reviewable implementation plan. This
Plan's candidate C++ signatures and algorithms (§2–§7) are approved;
implementation follows them as written; per
[AGENTS.md](../AGENTS.md), a forced deviation is called out explicitly in the
PR.

## Architectural boundaries (preserved, not re-decided)

- No `Vk*` type or Vulkan header outside `src/vulkan_backend/` (ADR-0001).
- RHI's public interfaces are abstract C++ base classes held behind
  `std::unique_ptr`, constructed only via Vulkan Backend's free factory
  functions (ADR-0014) — this Plan extends that mechanism to `RenderTarget`,
  `CommandList`, and `SubmissionSignal` rather than inventing a second one (§3).
- Renderer never owns a `RenderTarget`; `Presentation` owns every
  swapchain-backed resource (ADR-0002, ADR-0003).
- Single Phase 1 logical frame thread; nothing introduced here is thread-safe
  for concurrent access (ADR-0004).
- RenderGraph is the mandatory, sole path for recorded GPU work — no
  direct-submission bypass (AGENTS.md Golden Rule; ADR-0021). RenderGraph
  records but never submits or presents (ADR-0021).
- Spec 0005's single-producer resource model, dependency derivation, cycle
  detection, and deterministic ordering (ADR-0017, ADR-0018) are **unchanged**
  — this Plan only adds `ResourceState` tagging and execution on top.
- `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` for programmer errors, `Result<T,E>` for
  recoverable errors, no exceptions (ADR-0009, AGENTS.md).

## Non-Goals (matching Spec 0006)

Renderer, Shader System, general `Buffer`/`Texture`, a GPU memory allocator,
pipeline/shader objects, multiple frames in flight, multi-threading,
Android/iOS, headless rendering, image regression testing, any caller-authored
dependency edge or pass culling. This Plan does not touch `src/renderer/`, does
not add a dependency, and does not reopen any `Accepted` ADR's conclusions.

## Human Review Confirmations Received (2026-08-09)

Two implementation-level questions surfaced during drafting, both since
explicitly confirmed by Human Review as **decided**:

1. **"Producer-less" in ADR-0021 describes the bound *physical* resource's
   external origin, not a constraint on the logical resource's producer
   count.** `RenderTarget` is supplied by `Presentation`, not created by
   RenderGraph — that is what "producer-less" refers to at the physical level.
   The logical resource a `RenderTarget` is bound to may have exactly one write
   producer (e.g. the clear pass) — Spec 0005's single-producer rule (ADR-0018)
   governs that unchanged. The only additional, structurally-enforced
   constraint a bound resource carries is ADR-0019's guard: **no read usage
   anywhere in the graph**. Section 7's `execute()` design as originally
   drafted is confirmed correct. See the clarifying note appended to
   [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md#clarification-2026-08-09-not-a-change-in-conclusion).
2. **`RenderTarget`, `CommandList`, and `SubmissionSignal` follow ADR-0014's
   established mechanism exactly**: public RHI abstract base classes, unique/
   move-only ownership through `std::unique_ptr`, every `Vk*` detail inside
   Vulkan Backend. Section 3's design as originally drafted is confirmed.

## 1. Module and CMake target boundaries

No new module, no new top-level CMake target, no new third-party dependency.
Existing targets `atlantis_rhi`, `atlantis_vulkan_backend`,
`atlantis_render_graph` gain new headers/sources; `atlantis_render_graph` gains
a new `PUBLIC` link to `Atlantis::RHI` (realizing the dependency
`module_boundaries.md` and Spec 0005 anticipated — ADR-0021).

**Create:**

```
src/rhi/include/atlantis/rhi/  render_target.h  command_list.h  submission_signal.h  (abstract)
src/vulkan_backend/src/  vulkan_render_target.{h,cpp}  vulkan_command_list.{h,cpp}
                         vulkan_submission_signal.{h,cpp}  resource_state_mapping.{h,cpp}
                         (pure ResourceState -> VkImageLayout/masks mapping)
src/render_graph/include/atlantis/render_graph/execution.h   (execute(), ResourceBinding; void, no ExecuteError)
src/render_graph/src/execution.cpp
tests/vulkan_backend/  resource_state_mapping_tests.cpp (-> atlantis_vulkan_backend_tests)
                       frame_execution_gpu_tests.cpp    (-> atlantis_vulkan_backend_gpu_tests)
tests/render_graph/  execution_tests.cpp  fake_command_list.h (test-only rhi::CommandList)
examples/frame_execution_demo/  CMakeLists.txt  main.cpp
```

**Modify:** `src/rhi/include/atlantis/rhi/types.h` (+ `ResourceState`,
`SubmitError`, `CommandListCreateError`), `device.h` (+ `createCommandList()`,
`submit()`, `waitIdle()`), `presentation.h` (+ `acquireNextTarget()`,
`present()`), `src/rhi/CMakeLists.txt`; `src/vulkan_backend/src/vulkan_device.{h,cpp}`
(command pool, retained-submission state, `submit()`/`waitIdle()`),
`vulkan_presentation.{h,cpp}` (`acquireNextTarget()`/`present()`, acquire
semaphore, `images_` field), `vulkan_result.{h,cpp}` (mapping fns),
`src/vulkan_backend/CMakeLists.txt` (+ 4 `.cpp`);
`src/render_graph/include/atlantis/render_graph/render_graph_builder.h` (+
tagged `reads()`/`writes()`, `setExecute()`), `compiled_graph.h` (+
`CompiledResourceId`, resource/usage accessors), `render_graph_builder.cpp`,
`compile_algorithm.{h,cpp}` (carry `ResourceState` + execute callback through),
`compiled_graph.cpp`, `src/render_graph/CMakeLists.txt` (+ `execution.cpp`,
PUBLIC link `Atlantis::RHI`); `tests/rhi/types_tests.cpp` (+
`ResourceState`/enum sanity), `tests/vulkan_backend/CMakeLists.txt` (+ 2 sources
to the two existing targets), `tests/vulkan_backend/presentation_logic_tests.cpp`
(+ `decideAcquireAction()` cases), `tests/render_graph/CMakeLists.txt` (+
`execution_tests.cpp`, `fake_command_list.h` — no new link line;
`atlantis_render_graph` now PUBLIC-links `Atlantis::RHI` transitively); root
`CMakeLists.txt` (+ `add_subdirectory(examples/frame_execution_demo)`).

No file under `src/renderer/`, `src/shader_system/`, or any Android/iOS path is
created or modified. No third-party dependency line is added.

## 2. RHI candidate API — types and enums

Added to `types.h` (which already holds `Extent2D`, `Format`,
`SwapchainMetadata`, `PresentationError`):

- `enum class ResourceState { Undefined, ColorAttachmentWrite, PresentSource }`
  — this round's one resource kind (a `RenderTarget`'s color image) and nothing
  else (ADR-0020). Extending for buffers, depth attachments, or shader-read
  states is future work.
- `struct ClearColorValue { float r=0, g=0, b=0, a=1; }`.
- `enum class CommandListCreateError { CommandBufferAllocationFailed }`.
- `enum class SubmitError { QueueSubmitFailed, DeviceLost }`.

**`AcquireError` reuses `PresentationError`, no new type is introduced.** The
only failures `acquireNextTarget()` surfaces as `Result::Err` are exactly the
cases `PresentationError` already names (`SurfaceLost`, `SwapchainCreationFailed`
from its internal `recreateIfNeeded()` step, `DeviceLost`, `Unknown`);
out-of-date/suboptimal are not errors under ADR-0019. `acquireNextTarget()`'s
signature is `Result<std::unique_ptr<RenderTarget>, PresentationError>` (§5).

## 3. RHI candidate API — `RenderTarget`, `CommandList`, `SubmissionSignal`

Per Confirmation 2, these follow the existing `Device`/`Presentation` mechanism
(ADR-0014): abstract base class in an RHI public header, concrete subclass in
Vulkan Backend, held by the caller as `std::unique_ptr<Interface>`. "Move-only"
(ADR-0019) describes this `unique_ptr`-held pattern, not a new value-type
mechanism.

- **`RenderTarget`** (`render_target.h`): one presentable color attachment (the
  acquired swapchain image plus what Vulkan Backend needs to record/present
  it). Non-owning (`Presentation` owns every swapchain-backed resource).
  Frame-scoped: valid from `acquireNextTarget()` until the matching `present()`
  — outside that window is a lifetime precondition violation, not
  guaranteed-detectable. Write-only this round. Not copyable, not thread-safe.
  Public methods: `Extent2D extent()`, `Format format()`.
- **`CommandList`** (`command_list.h`): a sequence of recorded GPU commands.
  Exactly two recordable operations this round —
  `transitionResource(RenderTarget&, ResourceState before, ResourceState after)`
  and `clearColor(RenderTarget&, ClearColorValue)`. Recording only ever from
  inside a RenderGraph pass execution callback (`render_graph::execute()`, §7) —
  enforced by inspection/review, not the type (ADR-0020). Caller-owned only
  while being recorded into; **ownership transfers to `Device` at `submit()`**
  — a caller never destroys a `CommandList` it has submitted. Not copyable, not
  thread-safe.
- **`SubmissionSignal`** (`submission_signal.h`): opaque token returned by
  `Device::submit()`, consumed by `Presentation::present()` as the signal to
  wait on before presenting. No public method beyond the destructor (mirrors
  `Device`'s "declares no method" precedent). A caller never constructs,
  inspects, or stores one beyond passing it from `submit()` straight to
  `present()`. Precondition (not enforced, confirmed as an accepted design
  constraint): a caller must call `present()` for one `submit()` before calling
  `submit()` again — §9 explains why (`submit()` followed directly by exit
  remains legal).

## 4. RHI candidate API — `Device` extensions

Extends the current zero-method interface:

- `createCommandList() -> Result<std::unique_ptr<CommandList>, CommandListCreateError>`
  — vends a `CommandList` already in the recording state (its command buffer
  has begun; §9).
- `submit(std::unique_ptr<CommandList>, const RenderTarget&) -> Result<std::unique_ptr<SubmissionSignal>, SubmitError>`
  — takes ownership of the `CommandList` (moved in, per ADR-0020); `target` is
  read only for its acquire-complete wait signal (`Device` does not take
  ownership of `target`). Internally waits on, then releases, any
  previously-retained submission before accepting this one — a caller never
  manages a fence (§9's state machine). On success the returned
  `SubmissionSignal` is what `present()` must be given.
- `waitIdle() -> Result<std::monostate, SubmitError>` — blocks until every
  submission this `Device` has made has finished executing on the GPU. Required
  before destroying a `Presentation`/`Device` with an outstanding acquired
  `RenderTarget` or unwaited submission (ADR-0019) — see §9 and §11.

## 5. RHI candidate API — `Presentation` extensions

Extends the current `notifyResized`/`recreateIfNeeded`/`metadata` interface
(unchanged):

- `acquireNextTarget() -> Result<std::unique_ptr<RenderTarget>, PresentationError>`
  — tri-state (ADR-0019): `Err` on unrecoverable failure (surface/device lost,
  or a `recreateIfNeeded()` failure this call performs internally as its first
  step); `Ok(nullptr)` when nothing to draw this frame (zero extent, or an
  out-of-date swapchain deferred to the next call) — not an error; `Ok(non-null)`
  otherwise. Never retries acquisition within the same call on
  `VK_ERROR_OUT_OF_DATE_KHR`.
- `present(std::unique_ptr<RenderTarget>, std::unique_ptr<SubmissionSignal> renderFinished) -> Result<std::monostate, PresentationError>`
  — consumes `target` (ends its borrow) and waits on `renderFinished` before
  calling `vkQueuePresentKHR` internally. Out-of-date/suboptimal from present
  itself is routine (marks recreation needed, not `Err`); any other Vulkan
  error is a genuine `Err`.

`recreateIfNeeded()`'s existing 4-step contract (Spec 0003/ADR-0016) is
**unchanged**; `acquireNextTarget()` calls it as its own first step, it does
not duplicate its logic.

## 6. RenderGraph candidate API — tagged usages, execution callback, `CompiledGraph` extensions

**Additive, not breaking** — Spec 0005's existing
`declareResource()`/`declarePass()`/`compile()` and untagged
`reads()`/`writes()` are unchanged:

- `reads(PassHandle, ResourceHandle, atlantis::rhi::ResourceState)` /
  `writes(...)` — tagged overloads, used only for transition bookkeeping.
- `using PassExecuteFn = std::function<void(atlantis::rhi::CommandList&)>;
  void setExecute(PassHandle, PassExecuteFn)`.

Internally, `ResourceUsage` gains `std::optional<ResourceState> state` (empty
for an untagged Spec 0005 usage); `PassRecord` gains `PassExecuteFn executeFn`
(empty for an isolated or non-executing pass, still legal per Spec 0005's
pass-retention rule).

`CompiledGraph` gains **additive** accessors — sanctioned by Spec 0005's own
Out of Scope / Future Work ("a future spec may extend... this round's compiled
graph shape"), not a re-decision: `CompiledResourceId` (same pattern/contract
as `CompiledPassId`); `struct CompiledResourceUsage { CompiledResourceId
resource; bool isWrite; std::optional<ResourceState> state; }`;
`resourceCount()`, `resourceAt(index)`, `label(CompiledResourceId)` (overload of
the existing `label(CompiledPassId)`), `hasProducer(CompiledResourceId)`,
`requiresRhiBinding(CompiledResourceId)` (≥1 `ResourceState`-tagged usage),
`usageCount(CompiledPassId)`, `usage(CompiledPassId, index)`. `compiled_graph.h`
gains `#include <atlantis/rhi/types.h>` — this is `render_graph`'s RHI
dependency (ADR-0021) landing concretely. Out-of-range index/id on any new
accessor follows `CompiledGraph`'s existing convention (`ATLANTIS_CHECK`,
non-terminating-handler fallback returns an invalid-sentinel value never
mistakable for a real one).

`compile_algorithm.{h,cpp}` (private, white-box-tested) is extended to carry
each pass's `PassExecuteFn` and each usage's `ResourceState` through from the
builder's records into `CompiledGraph`'s storage — a mechanical passthrough,
not a change to dependency derivation, cycle detection, or ordering (Plan 0005
§6's algorithm is untouched).

## 7. RenderGraph candidate API — `execute()`, `ResourceBinding`, the two guards

`execution.h`: `struct ResourceBinding { CompiledResourceId resource;
atlantis::rhi::RenderTarget* target = nullptr; }` — binds one compiled-local
resource to a live RHI `RenderTarget` for exactly one `execute()` call;
frame-scoped, not persisted; `target` must outlive the call; `execute()` does
not take ownership.

`void execute(const CompiledGraph& graph, const std::vector<ResourceBinding>&
bindings, atlantis::rhi::CommandList& commandList)` — walks `graph`'s compiled
pass order once. For each pass, for each `ResourceState`-tagged usage whose
declared state differs from that resource's most-recently-recorded state,
records a `transitionResource()` call **before** invoking the pass's execution
callback. Inserts one trailing `transitionResource()` to
`ResourceState::PresentSource` for every bound resource actually touched by at
least one usage. Records only — never calls `Device::submit()` or
`Presentation::present()` (ADR-0021). Two preconditions are
guaranteed-detectable programmer errors (`ATLANTIS_CHECK_MSG`), not
`Result`-typed: every `ResourceState`-tagged usage must have a matching binding
entry; no bound resource may have any declared read usage anywhere in `graph`
(protects ADR-0019's always-`Undefined`-incoming-layout premise). Spec 0005's
plain untagged usages need no binding and produce no transition. Not
thread-safe.

**`execute()` algorithm** (`execution.cpp`): (1) for each resource `r` where
`graph.requiresRhiBinding(r)`, `ATLANTIS_CHECK_MSG` a binding exists; (2) for
each binding `b`, `ATLANTIS_CHECK_MSG` `graph` has no read usage for
`b.resource`; (3) `currentState`: a `map<CompiledResourceId, ResourceState>`,
empty initially; (4) for each pass in compiled order, for each usage with a
state, look up the binding (skip under a non-terminating handler if not found —
the UB-safe check-then-early-return pattern `RenderGraphBuilder` already uses),
compute `previous` (`Undefined` if untracked), and if `previous != *usage.state`
record `transitionResource(*binding.target, previous, *usage.state)` and update
`currentState`; then invoke the pass's `executeFn` if it has one (a declared-
but-never-`setExecute()`'d pass is legal); (5) for each binding actually
touched, if its last state `!= PresentSource`, record a trailing transition to
`PresentSource`. This satisfies every Testing & Verification Plan bullet from
Spec 0006's "RenderGraph execution integration" test list.

## 8. Vulkan Backend implementation — concrete `RenderTarget`/`CommandList`/`SubmissionSignal`

- `VulkanRenderTarget final : public rhi::RenderTarget` — holds a non-owning
  `VkImage`, the swapchain image index, `Extent2D`, `Format`, and a non-owning
  reference to `VulkanPresentation`'s acquire-complete `VkSemaphore`.
  Constructed only inside `VulkanPresentation::acquireNextTarget()`. **No
  `VkImageView`** — `vkCmdClearColorImage` (the only write this round) operates
  directly on `VkImage`; a view becomes necessary only once a real
  render-pass/attachment path exists (future Renderer spec).
- `VulkanCommandList final : public rhi::CommandList` — holds a non-owning
  `VkCommandBuffer` (owned by `VulkanDevice`'s command pool) and a non-owning
  `VulkanDevice&`. `transitionResource()`/`clearColor()`
  `static_cast<VulkanRenderTarget&>` the argument (safe — only Vulkan Backend
  constructs one in Phase 1) and record `vkCmdPipelineBarrier` /
  `vkCmdClearColorImage`. Its destructor calls `vkFreeCommandBuffers` for its
  one buffer (ADR-0020's "not pooled beyond ordinary RAII").
- `VulkanSubmissionSignal final : public rhi::SubmissionSignal` — holds a
  non-owning `VkSemaphore` (the persistent render-finished semaphore
  `VulkanDevice` owns, §9). No destructor-time Vulkan call.

**`resource_state_mapping.{h,cpp}`** — pure, GPU-independent (mirrors
`vulkan_result.h`'s separation): `planTransition(ResourceState before,
ResourceState after) -> ImageBarrierPlan { oldLayout, newLayout, srcAccessMask,
dstAccessMask, srcStage, dstStage }`. `before` must not equal `after` (`execute()`
never calls `transitionResource()` for a no-op pair); `ATLANTIS_CHECK_MSG`
guards any combination this round does not map. Concrete mapping:

| before | after | oldLayout | newLayout | srcAccess | dstAccess | srcStage | dstStage |
|---|---|---|---|---|---|---|---|
| `Undefined` | `ColorAttachmentWrite` | `UNDEFINED` | `TRANSFER_DST_OPTIMAL` | `0` | `TRANSFER_WRITE_BIT` | `TOP_OF_PIPE` | `TRANSFER` |
| `ColorAttachmentWrite` | `PresentSource` | `TRANSFER_DST_OPTIMAL` | `PRESENT_SRC_KHR` | `TRANSFER_WRITE_BIT` | `0` | `TRANSFER` | `BOTTOM_OF_PIPE` |
| `Undefined` | `PresentSource` | `UNDEFINED` | `PRESENT_SRC_KHR` | `0` | `0` | `TOP_OF_PIPE` | `BOTTOM_OF_PIPE` |

**Deliberate, not an oversight:** `ColorAttachmentWrite` maps to
`TRANSFER_DST_OPTIMAL`, not `COLOR_ATTACHMENT_OPTIMAL` — `clearColor()` records
`vkCmdClearColorImage`, which requires `GENERAL` or `TRANSFER_DST_OPTIMAL`. The
enumerator name describes *intent*; its concrete layout is this round's
implementation choice, tied to the one write operation that exists. A future
Minimal Renderer spec may need a distinct color-attachment-for-rendering
variant. `clearColor()`'s body assumes the target is already in
`TRANSFER_DST_OPTIMAL` — `execute()`'s transition-insertion always runs before
the pass callback, so the precondition holds structurally.

## 9. Vulkan Backend implementation — `VulkanDevice` submission ownership and drain

`VulkanDevice` gains: a `queue()` accessor (`VulkanPresentation::present()`
needs it); `VkCommandPool commandPool_` (created once, `RESET_COMMAND_BUFFER_BIT`);
`VkSemaphore renderFinishedSemaphore_` and `VkFence submissionFence_` (created
once); `std::unique_ptr<rhi::CommandList> retainedSubmission_` (null until the
first `submit()`); `bool hasRetainedSubmission_`.

- **`createCommandList()`:** `vkAllocateCommandBuffers` (check, map via
  `toCommandListCreateError()`), `vkBeginCommandBuffer` (`ONE_TIME_SUBMIT_BIT`,
  check), return a `VulkanCommandList` already recording.
- **`submit(commandList, target)`:** downcast; `vkEndCommandBuffer` (check, map
  to `SubmitError`). If `hasRetainedSubmission_`: `vkWaitForFences` +
  `vkResetFences` (check), then `retainedSubmission_.reset()` (destroys the
  prior `VulkanCommandList` → `vkFreeCommandBuffers`); else (first submission)
  nothing to wait on. Read `target`'s acquire-complete `VkSemaphore`. Build
  `VkSubmitInfo` waiting on that semaphore at `COLOR_ATTACHMENT_OUTPUT_BIT`,
  the command buffer, signalling `renderFinishedSemaphore_`. `vkQueueSubmit(queue_,
  ..., submissionFence_)` (check). `retainedSubmission_ = std::move(commandList);
  hasRetainedSubmission_ = true`. Return a `VulkanSubmissionSignal` wrapping
  `renderFinishedSemaphore_`.
- **Why one persistent semaphore pair is safe with single frame-in-flight:**
  the step-2 fence wait blocks (CPU-side) for the *previous* submission before
  this call's `vkQueueSubmit` reuses `renderFinishedSemaphore_` as a signal
  target and before the previous acquire's semaphore could be reused (the
  caller cannot call `acquireNextTarget()` until this `submit()` returns, per
  single-thread ordering). No second in-flight submission ever exists.
- **Confirmed design constraint, accepted by Human Review, not a defect:** the
  fence wait only proves the previous command buffer finished — it does not
  prove `renderFinishedSemaphore_` was ever *waited on* (only `present()` does
  that). A caller calling `submit()` twice in a row without an intervening
  `present()` would signal an already-signaled-but-unconsumed binary semaphore
  — a Vulkan valid-usage violation. **Precondition, not enforced by the type
  system or an `ATLANTIS_CHECK`:** a caller must call the matching `present()`
  for a successful `submit()` before calling `submit()` again. This holds
  structurally on the ordinary per-frame path (§11), and `submit()` followed
  directly by application exit (no `present()`) is **legal** —
  `Device::waitIdle()` drains and the caller cleans up safely, the same
  reasoning §11's mid-frame-exit path establishes for an
  acquired-but-unsubmitted `RenderTarget`. A documented caller obligation, the
  same undetected-precondition tier as `RenderTarget`'s own frame-window misuse
  (§12) — not something this round adds cross-class bookkeeping to detect.
  Nothing in §13's Testing Strategy exercises calling `submit()` twice without
  an intervening `present()`, a deliberate choice matching how
  genuinely-undetected preconditions are tested elsewhere (i.e. not tested, by
  design), confirmed by Human Review.
- **`waitIdle()`:** if `hasRetainedSubmission_`, `vkWaitForFences` +
  `vkResetFences` + `retainedSubmission_.reset()` + clear the flag; then
  `vkDeviceWaitIdle(device_)` (check, map to `SubmitError`) —
  belt-and-suspenders, also drains any presentation-engine-internal work not
  tracked by the fence (e.g. an acquired-but-never-submitted `RenderTarget`'s
  acquire semaphore, §11). Return `Ok`.
- **`~VulkanDevice()`:** the same drain sequence, run unconditionally as a
  defensive guarantee (failures logged via `ATLANTIS_LOG_ERROR` and swallowed —
  a destructor cannot return `Result`). This is the "equivalent guarantee built
  into `Device`'s destructor" Spec 0006 named as one acceptable shape for the
  drain capability. The **documented, tested** discipline is still that a
  caller calls `waitIdle()` explicitly and checks its `Result` before
  destroying `Presentation`/`Device` on every path, including a mid-frame exit;
  the destructor fallback exists only to prevent a crash if that discipline is
  violated. Destruction order unchanged from Spec 0003: `Presentation` before
  `Device` (caller-enforced).

## 10. Vulkan Backend implementation — `VulkanPresentation` extensions

New member: `VkSemaphore acquireCompleteSemaphore_` (created once, one
persistent instance — safe to reuse every frame by the same
single-frame-in-flight reasoning as §9). `images_` (a `std::vector<VkImage>` of
the current swapchain's images, queried via `vkGetSwapchainImagesKHR` once per
successful `recreateIfNeeded()`) is a genuinely new field the existing
recreation branch must also populate — the current implementation queries only
image *count*.

- **`acquireNextTarget()`:** call `recreateIfNeeded()` (existing Spec 0003
  method, unchanged); on `Err` return it; if `trackedExtent_.isZero()` return
  `Ok(nullptr)` (structurally unreachable to any Vulkan call below);
  `ATLANTIS_CHECK(swapchain_ != VK_NULL_HANDLE)`; `vkAcquireNextImageKHR(...,
  acquireCompleteSemaphore_, VK_NULL_HANDLE, &imageIndex)`. Branch on the
  `VkResult` via a pure, GPU-independent
  `decideAcquireAction(VkResult) -> { Proceed, SkipAndAwaitNextCall,
  ProceedButMarkRecreate, Fail }` (mirroring `decideRecreateAction()`'s
  extraction, testable with literal enumerators): `VK_SUCCESS` → `Proceed`;
  `VK_ERROR_OUT_OF_DATE_KHR` → `SkipAndAwaitNextCall` (`Ok(nullptr)` this call,
  recreation marked needed — no in-call retry); `VK_SUBOPTIMAL_KHR` →
  `ProceedButMarkRecreate` (use this image, recreate on the *next* call);
  anything else → `Fail` (`Err(toAcquireFailureError(result))`). On success,
  return a `VulkanRenderTarget` wrapping `images_[imageIndex]`, the index,
  extent/format, and `acquireCompleteSemaphore_`.
- **`present(target, renderFinished)`:** downcast both; build `VkPresentInfoKHR`
  waiting on `renderFinished`'s semaphore, the swapchain, `target`'s image
  index; `vkQueuePresentKHR(device_.queue(), ...)`; `target.reset()` +
  `renderFinished.reset()` (end both borrows regardless of outcome);
  `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` → `recreationNeeded_ = true`,
  `Ok()` (routine, not `Err`); any other non-success → `Err(toPresentFailureError(result))`;
  else `Ok()`.

Resize / zero-extent / out-of-date / suboptimal are absorbed as follows:
`WindowResize` → `notifyResized()` (unchanged); the next `acquireNextTarget()`
recreates at the new extent before acquiring; extent `{0,0}` → `Ok(nullptr)`,
zero Vulkan calls, every call; restore → the next call recreates then acquires,
no special "recovery" call; out-of-date/suboptimal at acquire or present →
recreate on the *next* `acquireNextTarget()`; a genuine unrecoverable error →
`Err(PresentationError)`.

## 11. Single frame-in-flight state machine and cleanup paths

State lives entirely inside `VulkanDevice` (§9): `hasRetainedSubmission_` is the
only state bit (`true` = exactly one `CommandList` + fence outstanding).

- **Ordinary per-frame path:** `acquireNextTarget()` → (skip if `nullptr`) →
  `createCommandList()` → `execute()` (records) → `submit()` (ownership
  transfers) → `present()`.
- **Mid-frame exit** (acquire succeeded, submit/present never called): the
  acquired `RenderTarget` (and any unsubmitted `CommandList`) go out of scope —
  no ownership transfer to `Device` happened, so `Device`'s retained state is
  untouched. Before destroying `Presentation`/`Device`, the caller still calls
  `Device::waitIdle()` (§9 step 2's `vkDeviceWaitIdle` drains any
  presentation-engine-internal state tied to the acquire semaphore) — Spec
  0006's destruction precondition, satisfied by the same one call.
- **`submit()` succeeded, `present()` never called (submit-then-exit):** legal,
  per Human Review. `hasRetainedSubmission_` is `true`; the returned
  `SubmissionSignal` goes out of scope unused (its destructor does nothing).
  The same `Device::waitIdle()` the caller must make on every path waits on the
  retained fence and releases the retained `CommandList` — no separate handling.
- **Early-return exit inside the frame loop:** no exceptions (ADR-0009); an
  `Err` sets a `failed` flag and falls through to the same cleanup sequence
  (`waitIdle()` → `presentation.reset()` → `device.reset()` →
  `platform::shutdown()`) `examples/rhi_vulkan_demo` established; `waitIdle()`
  is inserted at exactly the point `presentation.reset()` already happens.

## 12. Error model implementation

| Case | Tier | Mechanism |
|---|---|---|
| `RenderTarget`/`CommandList` used outside its frame/lifetime window; `Presentation`/`Device` destroyed with an outstanding acquire/submission; `submit()` called again before the previous `SubmissionSignal` was consumed by `present()` (§9) | Lifetime/sequencing precondition violation | Not detected; caller obligation (documented, tested via correct-discipline manual verification only) |
| Binding missing for a `ResourceState`-tagged usage; a bound resource has a read usage | Guaranteed-detectable programmer error | `ATLANTIS_CHECK_MSG` inside `execute()` |
| `vkAcquireNextImageKHR`/`vkQueuePresentKHR` unrecoverable failure | Recoverable | `Result::Err(PresentationError)` |
| `vkQueueSubmit`/`vkWaitForFences`/`vkDeviceWaitIdle`/`vkAllocateCommandBuffers`/`vkBeginCommandBuffer`/`vkEndCommandBuffer` failure | Recoverable | `Result::Err(SubmitError\|CommandListCreateError)` |
| `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` at acquire or present | Not an error | `Ok(nullptr)` / `Ok()`, recreation-needed bookkeeping |
| Zero framebuffer extent | Not an error | `Ok(nullptr)`, zero Vulkan calls |

`vulkan_result.h` gains four new pure mapping functions
(`toAcquireFailureError`, `toPresentFailureError`, `toSubmitError`,
`toCommandListCreateError`), each over an already-non-`VK_SUCCESS` `VkResult`,
unit-testable with literal enumerators exactly like the four existing ones.

## 13. Testing strategy

**GPU-independent unit tests (layer 1, no Vulkan device):**

- `resource_state_mapping_tests.cpp` — every defined `(before, after)` pair in
  §8's table produces the documented layout/access/stage values; an undefined
  pair triggers the assertion policy.
- `vulkan_result_tests.cpp` (extended) — cases for the four new mapping
  functions.
- `presentation_logic_tests.cpp` (extended) — `decideAcquireAction()` (§10) for
  `VK_SUCCESS`, `VK_ERROR_OUT_OF_DATE_KHR`, `VK_SUBOPTIMAL_KHR`, and at least
  one representative unrecoverable failure, exercised without a real device.
- `execution_tests.cpp` — every bullet from Spec 0006's "RenderGraph execution
  integration" test list (§7 enumerates them), run against
  `fake_command_list.h`, a test-only `rhi::CommandList` that records the calls
  it received (state, target identity, order), with no Vulkan device anywhere
  in the binary.
- `tests/rhi/types_tests.cpp` — trivial `ResourceState`/`ClearColorValue`
  sanity.

**Spec 0006's `RenderTarget`-is-move-only Acceptance Criterion needs no
dedicated test** — under the abstract-base-class-behind-`std::unique_ptr`
mechanism (§3), move-only-ness is a property of `std::unique_ptr<RenderTarget>`
itself, a standard-library guarantee, not Atlantis code. (This differs from
`CompiledGraph`, a concrete value type whose move/copy special members are
Atlantis's own code and did need an explicit check.)

**GPU-required integration tests** (`frame_execution_gpu_tests.cpp`, labeled
`"gpu"`, added to the existing `atlantis_vulkan_backend_gpu_tests` target):
construct `Device` + `Presentation` against a real surface; exercise
`acquireNextTarget()` → `createCommandList()` → a one-pass `execute()` →
`submit()` → `present()` end-to-end at least once; exercise `waitIdle()` after
a deliberate mid-frame exit (acquire, then no submit/present). Vulkan
Validation Layers enabled throughout.

**Manual verification** (`examples/frame_execution_demo/main.cpp`, a new
composition separate from the unmodified `examples/rhi_vulkan_demo`): mirrors
`rhi_vulkan_demo`'s event loop with per-frame additions — `acquireNextTarget()`
(skip on `Ok(nullptr)`, log+`failed` on `Err`); `createCommandList()`; a
one-pass `RenderGraphBuilder` (`declareResource("frame-target")`,
`declarePass("clear")`, `writes(clearPass, frameTarget,
ResourceState::ColorAttachmentWrite)`, `setExecute(clearPass, [&](CommandList&
cmd){ cmd.clearColor(*target, kClearColor); })`, `compile()`);
`render_graph::execute(compiledGraph, {{resourceId, target.get()}},
*commandList)`; `submit(std::move(commandList), *target)` →
`present(std::move(target), std::move(signal))`; on `failed`/`closeRequested`,
`device->waitIdle()` **before** `presentation.reset()`/`device.reset()` — the
one addition to `rhi_vulkan_demo`'s cleanup sequence. Confirms, matching Spec
0006's Testing & Verification Plan exactly: a visible, correctly-colored window
across repeated frames; correct color across interactive resize with no
corruption/tearing/validation warning; zero Vulkan calls while minimized (by
inspection of the `Ok(nullptr)` path); resumes on restore with no special
recovery step; a deliberate mid-frame exit (close the window between a logged
"acquired" message and the next `submit()`, exercised at least once manually)
completes cleanly with Validation Layers clean throughout, including at
shutdown.

**Per-step commands:** every §16 step ends with
`cmake --build build --config Debug`, `ctest --test-dir build -C Debug -LE gpu`,
`cmake --build build --config Release`. GPU-touching steps additionally run
`ctest --test-dir build -C Debug -L gpu` and the demo on the Windows/GPU
machine, with Validation Layers output inspected for any WARNING/ERROR.

## 14. Explicit prohibitions (grep/code-review checklist)

Run at the end of every implementation step, and again before opening a PR:

- No `Vk*` type / Vulkan header in `src/rhi/include` or
  `src/render_graph/include` — `grep -rn "Vk[A-Z]"` / `grep -rln "vulkan" -i`,
  expect none.
- No direct `vkCmd*` outside Vulkan Backend's `CommandList` implementation —
  `grep -rn "vkCmd" src --include=*.cpp --include=*.h | grep -v src/vulkan_backend/`,
  expect none.
- RenderGraph never submits or presents — `grep -rn "submit(\|present(" src/render_graph`,
  expect none.
- No recording outside a RenderGraph pass callback — `transitionResource`/
  `clearColor` appear in `src/render_graph`/`src/rhi` `.cpp` only inside
  `execution.cpp`'s own transition calls.
- No new third-party dependency — `git diff --stat CMakeLists.txt cmake/`,
  expect no dependency-fetching changes.
- No `src/renderer/`, no Shader System source (directories do not exist).
- No pipeline/shader/general-resource type — `grep -rn "VkPipeline\|VkShaderModule\|class Buffer\|class Texture\|class Sampler" src/rhi src/vulkan_backend`,
  expect none.
- No multiple-frames-in-flight machinery — `grep -rn "std::vector<.*Fence\|std::vector<.*Semaphore" src/vulkan_backend`,
  expect none (exactly one of each, as plain members).

Code-review checklist (manual, per step): every new `VkResult`-returning call's
result is checked; every new public type documents its thread-safety contract
in one line; no `ATLANTIS_CHECK` where a `Result::Err` belongs, or vice versa
(cross-check §12); `CommandList` ownership transfer (§9) is never bypassed — no
code path outside `VulkanDevice::submit()`/its destructor/`waitIdle()` destroys
a `VulkanCommandList`.

## 15. Build integration

`src/rhi/CMakeLists.txt` — header-only additions (`render_target.h`,
`command_list.h`, `submission_signal.h` are pure interface headers; `types.h`'s
new enums need no new `.cpp` beyond whatever `operator==` `types.cpp` already
covers, extended only if a `ClearColorValue` `operator==` is added per the
`Extent2D` precedent). `src/vulkan_backend/CMakeLists.txt` — add the four new
`.cpp` files; `target_link_libraries` unchanged. `src/render_graph/CMakeLists.txt`
— add `execution.cpp`; add `Atlantis::RHI` to the PUBLIC link list (realizes
ADR-0021's dependency). `tests/render_graph/CMakeLists.txt` — add
`execution_tests.cpp`; no new link line (RHI headers reachable transitively via
`atlantis_render_graph`'s new PUBLIC `Atlantis::RHI`). `tests/vulkan_backend/CMakeLists.txt`
— add `resource_state_mapping_tests.cpp` to the existing GPU-independent
`atlantis_vulkan_backend_tests` target, `frame_execution_gpu_tests.cpp` to the
existing `atlantis_vulkan_backend_gpu_tests` target — no new CMake target of
either kind. `examples/frame_execution_demo/CMakeLists.txt` — mirrors
`examples/rhi_vulkan_demo/CMakeLists.txt`, new executable name. Root
`CMakeLists.txt` — `add_subdirectory(examples/frame_execution_demo)` under
`ATLANTIS_BUILD_EXAMPLES`.

## 16. Implementation order

Each step is independently reviewable and ends with §13's build/test commands
(GPU commands only where noted) and §14's grep checklist.

1. **RHI types and interfaces** — `types.h` additions, `render_target.h`,
   `command_list.h`, `submission_signal.h`, `device.h`/`presentation.h`
   signature additions (declarations only). Build: header compilation only.
2. **`vulkan_result.h`/`.cpp` mapping functions** — the four new pure
   `VkResult → RHI-error` functions, plus `resource_state_mapping.{h,cpp}`.
   GPU-independent unit tests land here.
3. **`VulkanRenderTarget`/`VulkanCommandList`/`VulkanSubmissionSignal`** —
   concrete classes, constructors/accessors, no `VulkanDevice`/
   `VulkanPresentation` wiring yet.
4. **`VulkanDevice` extensions** — command pool, `createCommandList()`, the
   retained-submission state machine in `submit()`, `waitIdle()`, destructor
   drain, `queue()`. Build-verify only (needs `VulkanPresentation`'s acquire
   for a real frame to submit against).
5. **`VulkanPresentation` extensions** — `images_` population, the acquire
   semaphore, `acquireNextTarget()`, `present()`. First step where
   `frame_execution_gpu_tests.cpp` becomes exercisable — land a minimal
   acquire → submit (empty command list, no transition) → present smoke test.
   Run `ctest -C Debug -L gpu`.
6. **RenderGraph: tagged usages + execution callback** —
   `render_graph_builder.{h,cpp}`, `compile_algorithm.{h,cpp}` passthrough,
   `compiled_graph.{h,cpp}` new `CompiledResourceId`/accessors. GPU-independent
   — extend `tests/render_graph/`'s suites (compile-time checks + passthrough-
   correctness) before `execute()` exists.
7. **RenderGraph: `execute()`** — `execution.{h,cpp}`, `fake_command_list.h`,
   `execution_tests.cpp` (every bullet from §7/Spec 0006's test list). Fully
   GPU-independent.
8. **Vulkan Backend GPU integration, full path** — extend
   `frame_execution_gpu_tests.cpp` to the full acquire → `execute()` (real
   one-pass clear graph) → `submit()` → `present()` cycle, plus the
   mid-frame-exit + `waitIdle()` case. `ctest -C Debug -L gpu`, Validation
   Layers inspected.
9. **`examples/frame_execution_demo`** — the interactive manual verification
   composition. Run interactively: resize, minimize/restore, mid-frame window
   close, clean exit.
10. **Final consistency pass** — §14's full grep checklist, §17's mapping
    walked line by line, `cmake --build build --config Release` clean, `ctest
    --test-dir build -C Debug` (full suite, no label filter) green.

**Sequencing:** steps 1–3 have no cross-dependency beyond declaration order.
Step 4 depends on 1–3. Step 5 depends on 4 (needs `Device::submit()` for even a
smoke test) and 1–3. Steps 6–7 (RenderGraph) have no dependency on 4–5 and
could be built in parallel, but step 8's full integration test needs both 5 and
7. Step 9 needs 8. Step 10 is last.

## 17. Acceptance Criteria Mapping

| Spec 0006 Acceptance Criterion (abbreviated) | Plan Section(s) |
|---|---|
| No `Vk*`/Vulkan header in RHI/RenderGraph public headers | §1 file list, §14 grep |
| No `vkCmd*`/barrier construction outside Vulkan Backend's `CommandList` impl | §8, §14 grep |
| `RenderTarget` non-owning, frame-scoped, write-only; no read-back capability | §3, §7 guard 2 (structural enforcement) |
| `RenderTarget` move-only (compile-time property) | §3 (mechanism), §13 (why no dedicated test is needed) |
| Binding a read-used resource to a `RenderTarget` rejected as programmer error | §7 algorithm step 2 |
| Unbound `ResourceState`-tagged usage rejected as programmer error | §7 algorithm step 1 |
| `acquireNextTarget()` returns `Ok(nullptr)` at zero extent, first and every later call | §10 |
| Windows resize → next acquire recreates then acquires at new extent | §10 |
| Out-of-date/suboptimal at acquire or present never crashes/hangs/warns | §10, §12 |
| `execute()` never calls `submit()`/`present()` | §7 (records only), §14 grep |
| No GPU command recorded outside a RenderGraph pass callback | §3 `CommandList` doc note, §14 grep |
| Every `VkResult` checked | §9, §10, §14 checklist |
| Validation Layers clean, Debug + GPU CI | §13 |
| Manual demo: visible frame, resize-correct, zero calls while minimized, resumes on restore | §13 manual verification |
| No pipeline/shader/buffer/texture/allocator | §Non-Goals, §14 grep |
| No caller-authored dependency edge / pass culling | §6 (Spec 0005 rules untouched) |
| No `src/renderer/`, no Shader System | §14 grep |
| No Android/second-backend/thread-job-system | §Non-Goals |
| No multi-frame-in-flight machinery | §9 (single retained slot), §14 grep |
| `RenderTarget`/`Device` destruction precondition satisfied on every exit path incl. mid-frame | §11, §13 manual verification |
| `CommandList` ownership transfer; never destroyed pre-submission-wait | §9 `submit()` state machine, §14 checklist |

## Verification Checklist

- [ ] Unit tests: `resource_state_mapping_tests.cpp`,
      `vulkan_result_tests.cpp` (extended), `presentation_logic_tests.cpp`
      (extended), `execution_tests.cpp`, `types_tests.cpp` (extended) — all
      pass under `ctest -C Debug -LE gpu`.
- [ ] Headless integration tests: not applicable (Spec 0006 Non-Goals;
      windowed-first sequencing).
- [ ] Image regression tests: not applicable (nothing beyond manual visual
      confirmation of a solid clear color this round).
- [ ] Vulkan Validation Layers clean: `ctest -C Debug -L gpu` and the manual
      `frame_execution_demo` run, including the mid-frame-exit case, produce
      zero WARNING/ERROR output.
- [ ] Other: §14's grep checklist returns the documented (empty, in every
      prohibited case) result.

## Rollback Plan

Every change is additive to existing, already-shipped modules — no existing
public method signature is altered or removed (Spec 0003's
`notifyResized()`/`recreateIfNeeded()`/`metadata()` and Spec 0005's untagged
`reads()`/`writes()`/`compile()` are unchanged). Revert is a straightforward
`git revert` of this feature's merge commit(s) — RenderGraph's new
`Atlantis::RHI` link is the only new edge, and nothing yet depends on
RenderGraph beyond its own tests and this Plan's new demo. A partial rollback
(keep RHI additions, revert RenderGraph execution) is possible since §16's
steps are ordered so RHI (1–5) is independently mergeable/testable before
RenderGraph (6–7) builds on it.

## Definition of Done

Per [docs/process/definition-of-done.md](../docs/process/definition-of-done.md):

- Headless verification: not applicable (Non-Goal, unchanged from Spec 0003/0005).
- Image regression: not applicable this round (no golden-image harness exists
  yet, per AGENTS.md sequencing).
- ADR: none new required by implementation — ADR-0019/0020/0021 already
  `Accepted` cover every architectural decision this Plan operationalizes; any
  *deviation* discovered during implementation is a Plan/Spec issue to raise,
  not a new ADR to file unilaterally.
- All other items apply as stated in the linked document.
