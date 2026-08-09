# Plan: RHI / RenderGraph Frame Execution Foundation

- **Spec:** [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; content authored by the agent, reviewed and approved by a
  human per the Human Review Approval note below.
- **Human Review Approval (2026-08-09):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-09, completing a **joint
  Spec 0006 + Plan 0006 Human Review**. The human reviewed and approved
  this Plan's candidate public API (§2–§7), state machines (acquire's
  three-outcome result, `execute()`'s transition-insertion algorithm,
  `submit()`'s retained-submission state machine, `present()`, and the
  single-frame-in-flight discipline as a whole — §7, §9–§11), ownership
  model, synchronization design, implementation order (§16), and
  verification plan (§13), as this Plan reads after revision 2 (see
  Revision History below) — including, explicitly:

  1. Both items already recorded in "Human Review Confirmations
     Received" below (the ADR-0021 "producer-less" clarification, and
     `RenderTarget`/`CommandList`/`SubmissionSignal` following ADR-0014's
     mechanism) — reconfirmed as part of this approval, not reopened.
  2. **The `submit()`/`present()` calling precondition (§3, §9, §12),
     accepted as a deliberate design constraint, not a defect requiring
     redesign:**
     - A caller must call the matching `present()` for a successful
       `submit()` before calling `submit()` again.
     - This precondition is a documented caller obligation — the type
       system does not, and is not required to, prevent an illegal
       `submit → submit` sequence.
     - `submit()` followed by application exit with no `present()` call
       is legal: `Device::waitIdle()` drains any outstanding work and the
       caller cleans up safely — this is Section 11's mid-frame-exit path
       (already covers acquire-then-exit; the same reasoning covers
       submit-then-exit).
     - Enforcement is via API documentation, the ordinary-sequence tests
       already specified in Section 13, and code review — no new runtime
       check is added by this approval.

  **Implementation is authorized by this approval**, but must not begin
  until this Plan's own PR has merged into `main`, per AGENTS.md's
  Spec → Plan → Human Review → Implementation ordering — the
  implementation branch is created from `main` *after* that merge, not
  from this Plan's own branch.
- **Revision history:**
  - **2026-08-09 (revision 3):** Human Review approval recorded (see
    above). `Status` moves `Draft` → `Approved / Ready for
    Implementation`. No design content changed from revision 2; wording
    in the Objective, the `submit()`/`present()` precondition notes
    (§3, §9), and the Consistency Review's closing note is updated from
    "candidate, pending review" framing to "reviewed and approved"
    framing where the underlying content did not otherwise change.
  - **2026-08-09 (revision 2):** Joint review pass. Resolved both
    revision-1 open questions as Human-Review-confirmed decisions (see
    "Human Review Confirmations Received" below) and added a short,
    non-substantive clarifying note to
    [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
    (its Decision/Consequences unchanged). Fixed a UB-unsafe fallback
    path in `execute()`'s Guard 1 (§7); documented a previously-
    undocumented `submit()`-without-`present()` semaphore-reuse
    precondition (§9, §3, §12); extracted `decideAcquireAction()` as a
    concrete pure function to close a vague test-coverage reference
    (§10, §13); clarified why `RenderTarget`'s move-only Acceptance
    Criterion needs no dedicated test (§13); fixed inaccurate/
    self-contradictory CMake wording for the two `tests/vulkan_backend/`
    additions and the `tests/render_graph/` link line (§1, §15). No
    architectural conclusion changed; still `Draft`.
  - **2026-08-09 (revision 1):** Initial draft against Approved Spec
    0006 and Accepted ADR-0019–0021.

## Objective

Turn Spec 0006's approved contract — a concrete `RenderTarget`, `Presentation`
acquire/present, a minimal `CommandList`/`Device::submit()`, and a
RenderGraph execution capability — into an ordered, reviewable
implementation plan. This plan's candidate C++ signatures and algorithms
(§2–§7) have been reviewed and approved by the joint Spec 0006 + Plan
0006 Human Review recorded above — implementation follows them as
written; per [AGENTS.md](../AGENTS.md), any place reality forces a
deviation during Implementation must be called out explicitly in the PR,
not silently drifted from.

## Authoritative Sources

Read in full before this Plan was drafted: Spec 0006; ADR-0019, ADR-0020,
ADR-0021; Spec/Plan 0003; Spec/Plan 0005; ADR-0001–0004, ADR-0009,
ADR-0014–0018; `docs/architecture/{overview,module_boundaries,threading,
resource_lifetime}.md`; `docs/process/{testing-strategy,definition-of-done}.md`;
the current `src/rhi/`, `src/vulkan_backend/`, `src/render_graph/` source,
tests, and CMake files (read directly, not summarized from memory).

## Critical Architectural Boundaries (preserved, not re-decided here)

- No `Vk*` type or Vulkan header outside `src/vulkan_backend/` (ADR-0001).
- RHI's public interfaces are abstract C++ base classes held behind
  `std::unique_ptr`, constructed only via Vulkan Backend's free factory
  functions (ADR-0014) — this Plan extends that established mechanism to
  `RenderTarget`, `CommandList`, and `SubmissionSignal` rather than
  inventing a second one (see Section 3).
- Renderer never owns a `RenderTarget`; `Presentation` owns every
  swapchain-backed resource (ADR-0002, ADR-0003).
- Single Phase 1 logical frame thread; nothing introduced here is
  thread-safe for concurrent access (ADR-0004).
- RenderGraph is the mandatory, sole path for recorded GPU work — no
  direct-submission bypass (AGENTS.md Golden Rule; ADR-0021).
- RenderGraph records but never submits or presents (ADR-0021).
- Spec 0005's single-producer resource model, dependency derivation,
  cycle detection, and deterministic ordering (ADR-0017, ADR-0018) are
  **unchanged** — this Plan only adds `ResourceState` tagging and
  execution on top.
- `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` for programmer errors, `Result<T,E>`
  for recoverable errors, no exceptions (ADR-0009, AGENTS.md).

## Non-Goals (confirmed matching Spec 0006)

Renderer, Shader System, general `Buffer`/`Texture`, a GPU memory
allocator, pipeline/shader objects, multiple frames in flight,
multi-threading, Android/iOS, headless rendering, image regression
testing, any caller-authored dependency edge or pass culling. This Plan
does not touch `src/renderer/`, does not add a dependency, and does not
reopen any `Accepted` ADR's conclusions.

## Human Review Confirmations Received (2026-08-09)

Two implementation-level questions surfaced while drafting this Plan's
first revision, affecting public API shape and cross-ADR consistency.
Both have since been explicitly confirmed by Human Review and are
recorded here as **decided**, not open — this is this Plan's own
authoritative record of that confirmation; ADR-0021 additionally carries
a short clarifying note (not a rewrite of its Decision) cross-referencing
this record, per the "single authoritative source, everything else
points to it" documentation rule.

1. **"Producer-less" in ADR-0021 describes the bound physical resource's
   external origin, not a constraint on the logical resource's producer
   count.** Confirmed: `RenderTarget` is supplied by `Presentation`, not
   created by RenderGraph — that is what "producer-less" refers to at the
   *physical* (RHI-object) level. The *logical* resource a `RenderTarget`
   is bound to may have exactly one write producer (e.g. the clear pass)
   — Spec 0005's single-producer rule (ADR-0018) governs that exactly as
   it governs every other logical resource, unchanged. The only
   additional, structurally-enforced constraint a bound resource carries
   is ADR-0019's actual guard: **no read usage anywhere in the graph**.
   This is Section 7's `execute()` design exactly as originally drafted —
   confirmed as correct, not merely a Plan-stage candidate. See the
   clarifying note appended to
   [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md#clarification-2026-08-09-not-a-change-in-conclusion).
2. **`RenderTarget`, `CommandList`, and `SubmissionSignal` follow
   ADR-0014's established mechanism exactly**: public RHI abstract base
   classes, unique ownership and move-only semantics expressed through
   `std::unique_ptr`, with every concrete Vulkan type and `Vk*` detail
   kept inside Vulkan Backend. Section 3's design (as originally drafted)
   is confirmed, not merely a candidate reading.

## 1. Module and CMake Target Boundaries

No new module, no new top-level CMake target, no new third-party
dependency. Existing targets `atlantis_rhi`, `atlantis_vulkan_backend`,
`atlantis_render_graph` gain new headers/sources; `atlantis_render_graph`
gains a new `PUBLIC` link to `Atlantis::RHI` (realizing the dependency
`module_boundaries.md` and Spec 0005 already anticipated — ADR-0021).

### Files to Create

```
src/rhi/include/atlantis/rhi/render_target.h        # RenderTarget (abstract)
src/rhi/include/atlantis/rhi/command_list.h          # CommandList (abstract), ClearColorValue
src/rhi/include/atlantis/rhi/submission_signal.h     # SubmissionSignal (abstract)

src/vulkan_backend/src/vulkan_render_target.h/.cpp
src/vulkan_backend/src/vulkan_command_list.h/.cpp
src/vulkan_backend/src/vulkan_submission_signal.h/.cpp
src/vulkan_backend/src/resource_state_mapping.h/.cpp # pure ResourceState -> VkImageLayout/masks mapping

src/render_graph/include/atlantis/render_graph/execution.h   # execute(), ResourceBinding, ExecuteError-free (void)
src/render_graph/src/execution.cpp

tests/vulkan_backend/resource_state_mapping_tests.cpp  # new source, existing atlantis_vulkan_backend_tests target
tests/vulkan_backend/frame_execution_gpu_tests.cpp      # new source, existing atlantis_vulkan_backend_gpu_tests target
tests/render_graph/execution_tests.cpp               # GPU-independent, fake CommandList test double
tests/render_graph/fake_command_list.h               # test-only rhi::CommandList implementation

examples/frame_execution_demo/CMakeLists.txt
examples/frame_execution_demo/main.cpp
```

### Files to Modify

```
src/rhi/include/atlantis/rhi/types.h        # + ResourceState, SubmitError, CommandListCreateError
src/rhi/include/atlantis/rhi/device.h       # + createCommandList(), submit(), waitIdle()
src/rhi/include/atlantis/rhi/presentation.h # + acquireNextTarget(), present()
src/rhi/CMakeLists.txt                      # + 3 new headers (no new .cpp needed beyond types.cpp additions)

src/vulkan_backend/src/vulkan_device.h/.cpp        # + command pool, retained-submission state, submit()/waitIdle()
src/vulkan_backend/src/vulkan_presentation.h/.cpp  # + acquireNextTarget()/present(), acquire semaphore
src/vulkan_backend/src/vulkan_result.h/.cpp        # + SubmitError/CommandListCreateError mapping fns
src/vulkan_backend/CMakeLists.txt                  # + 4 new .cpp files

src/render_graph/include/atlantis/render_graph/render_graph_builder.h  # + tagged reads()/writes(), setExecute()
src/render_graph/include/atlantis/render_graph/compiled_graph.h        # + CompiledResourceId, resource/usage accessors
src/render_graph/src/render_graph_builder.cpp
src/render_graph/src/compile_algorithm.h/.cpp       # carry ResourceState + execute callback through to CompiledGraph
src/render_graph/src/compiled_graph.cpp
src/render_graph/CMakeLists.txt                     # + execution.cpp, PUBLIC link Atlantis::RHI

tests/rhi/CMakeLists.txt                    # unaffected structurally; types_tests.cpp gets new cases
tests/rhi/types_tests.cpp                   # + ResourceState/enum sanity (trivial, GPU-independent)
tests/vulkan_backend/CMakeLists.txt         # + 2 new source files added to existing targets (no new target) --
                                             #   resource_state_mapping_tests.cpp -> atlantis_vulkan_backend_tests;
                                             #   frame_execution_gpu_tests.cpp -> atlantis_vulkan_backend_gpu_tests
tests/vulkan_backend/presentation_logic_tests.cpp   # + pure acquire-outcome decision-logic cases (decideAcquireAction(), Section 10)
tests/render_graph/CMakeLists.txt           # + execution_tests.cpp, fake_command_list.h -- no new link line needed
                                             #   (atlantis_render_graph now PUBLIC-links Atlantis::RHI, already
                                             #   propagates transitively to this test target)

CMakeLists.txt (root)                       # + add_subdirectory(examples/frame_execution_demo)
```

No file under `src/renderer/`, `src/shader_system/`, or any Android/iOS
path is created or modified. No `vcpkg`/`CMakeLists.txt` third-party
dependency line is added.

## 2. RHI Candidate API — Types and Enums

Added to `src/rhi/include/atlantis/rhi/types.h` (extends the existing
file that already holds `Extent2D`, `Format`, `SwapchainMetadata`,
`PresentationError`):

```cpp
// This round's one resource kind (a RenderTarget's color image) and
// nothing else -- see ADR-0020. Extending this set for buffers, depth
// attachments, or shader-read states is future work, gated on a real
// consumer.
enum class ResourceState {
  Undefined,
  ColorAttachmentWrite,
  PresentSource,
};

struct ClearColorValue {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
  float a = 1.0f;
};

enum class CommandListCreateError {
  CommandBufferAllocationFailed,
};

enum class SubmitError {
  QueueSubmitFailed,
  DeviceLost,
};
```

**`AcquireError` reuses `PresentationError`, no new type is introduced.**
Spec 0006's Risks & Open Questions left this to the Plan. Rationale: the
only failures `acquireNextTarget()` can surface as `Result::Err` are
exactly the cases `PresentationError` already names (`SurfaceLost`,
`SwapchainCreationFailed` — from its internal `recreateIfNeeded()` step,
`DeviceLost`, `Unknown`); out-of-date/suboptimal are explicitly *not*
errors under ADR-0019 (folded into `Ok(nullptr)` or ignored). Introducing
a parallel `AcquireError` enum with the same four cases would be pure
duplication. `acquireNextTarget()`'s signature is therefore
`Result<std::unique_ptr<RenderTarget>, PresentationError>` (Section 5).

## 3. RHI Candidate API — `RenderTarget`, `CommandList`, `SubmissionSignal`

Per Human Review Confirmation 2 above, these follow the existing
`Device`/`Presentation` mechanism (ADR-0014): abstract base class in an
RHI public header, concrete subclass in Vulkan Backend, held by the
caller as `std::unique_ptr<Interface>`. "Move-only" (ADR-0019) describes
this `unique_ptr`-held pattern, not a new value-type mechanism.

`src/rhi/include/atlantis/rhi/render_target.h`:

```cpp
#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// One presentable color attachment -- the acquired swapchain image and
// whatever Vulkan Backend needs to record/present it (ADR-0019).
// Non-owning: Presentation owns every swapchain-backed resource behind
// it (ADR-0003). Frame-scoped: valid from the acquireNextTarget() call
// that vended it until the matching present() call consumes it -- using
// it outside that window is a lifetime precondition violation, not a
// guaranteed-detectable error. Write-only this round: no method here (or
// on CommandList) ever reads its prior contents. Held exclusively behind
// std::unique_ptr<RenderTarget> (ADR-0014's mechanism); not copyable.
// Not internally thread-safe; caller-thread-only (ADR-0004).
class RenderTarget {
 public:
  virtual ~RenderTarget() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual Format format() const = 0;
};

}  // namespace atlantis::rhi
```

`src/rhi/include/atlantis/rhi/command_list.h`:

```cpp
#pragma once

#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A sequence of recorded GPU commands (ADR-0020). Exactly two recordable
// operations exist this round -- no general draw call, no pipeline
// binding. Recording is only ever performed from inside a RenderGraph
// pass execution callback (render_graph::execute(), Section 7) -- not
// enforced by this type itself (see ADR-0020's own note on this),
// enforced by inspection/review, matching this codebase's existing
// "no direct vkCmd* outside Vulkan Backend's CommandList implementation"
// convention. Caller-owned only while being recorded into; ownership
// transfers to Device at submit() (Section 4) -- a caller never destroys
// a CommandList it has submitted. Not copyable, not thread-safe.
class CommandList {
 public:
  virtual ~CommandList() = default;

  virtual void transitionResource(RenderTarget& target, ResourceState before, ResourceState after) = 0;
  virtual void clearColor(RenderTarget& target, ClearColorValue color) = 0;
};

}  // namespace atlantis::rhi
```

`src/rhi/include/atlantis/rhi/submission_signal.h`:

```cpp
#pragma once

namespace atlantis::rhi {

// Opaque token returned by Device::submit(), consumed by
// Presentation::present() as the signal to wait on before presenting
// (ADR-0019, ADR-0020). No public method beyond the destructor --
// mirrors Device's own "intentionally declares no method" precedent.
// A caller never constructs, inspects, or stores one beyond passing it
// from submit() straight to present(). Precondition, not enforced here,
// confirmed by Human Review as an accepted design constraint (see the
// Plan's Human Review Approval note): a caller must call present() for
// one submit() call before calling submit() again -- see Section 9 for
// why (submit() followed directly by application exit remains legal).
class SubmissionSignal {
 public:
  virtual ~SubmissionSignal() = default;
};

}  // namespace atlantis::rhi
```

## 4. RHI Candidate API — `Device` Extensions

`src/rhi/include/atlantis/rhi/device.h` (extends the current
zero-method interface):

```cpp
#pragma once

#include <memory>

#include <atlantis/result.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/submission_signal.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

class Device {
 public:
  virtual ~Device() = default;

  // Vends a CommandList already in the recording state (its underlying
  // command buffer has begun) -- see Section 9.
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<CommandList>, CommandListCreateError>
  createCommandList() = 0;

  // Takes ownership of commandList (moved in, per ADR-0020). target is
  // read only for its acquire-complete wait signal -- Device does not
  // take ownership of target. Internally waits on, then releases, any
  // previously-retained submission before accepting this one -- a caller
  // never manages a fence directly (Section 9's state machine). On
  // success, the returned SubmissionSignal is what present() must be
  // given.
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<SubmissionSignal>, SubmitError> submit(
      std::unique_ptr<CommandList> commandList, const RenderTarget& target) = 0;

  // Blocks until every submission this Device has made has finished
  // executing on the GPU. Required before destroying a Presentation/
  // Device with an outstanding acquired RenderTarget or unwaited
  // submission (ADR-0019) -- see Section 9 and Section 11's cleanup
  // sequencing.
  [[nodiscard]] virtual atlantis::Result<std::monostate, SubmitError> waitIdle() = 0;
};

}  // namespace atlantis::rhi
```

## 5. RHI Candidate API — `Presentation` Extensions

`src/rhi/include/atlantis/rhi/presentation.h` (extends the current
`notifyResized`/`recreateIfNeeded`/`metadata` interface, unchanged):

```cpp
  // Tri-state (ADR-0019): Err -- unrecoverable failure (surface/device
  // lost, or a recreateIfNeeded() failure this call performs internally
  // as its first step). Ok(nullptr) -- nothing to draw this frame (zero
  // extent, or an out-of-date swapchain deferred to the next call) --
  // not an error. Ok(non-null) -- a usable RenderTarget. Never retries
  // acquisition within the same call on VK_ERROR_OUT_OF_DATE_KHR.
  [[nodiscard]] virtual atlantis::Result<std::unique_ptr<RenderTarget>, PresentationError> acquireNextTarget() = 0;

  // Consumes target (ends its borrow) and waits on renderFinished before
  // calling vkQueuePresentKHR internally. Out-of-date/suboptimal from
  // present itself is routine (marks recreation needed, not Err); any
  // other Vulkan error is a genuine Err.
  [[nodiscard]] virtual atlantis::Result<std::monostate, PresentationError> present(
      std::unique_ptr<RenderTarget> target, std::unique_ptr<SubmissionSignal> renderFinished) = 0;
```

`recreateIfNeeded()`'s existing 4-step contract (Spec 0003/ADR-0016) is
**unchanged**; `acquireNextTarget()` calls it as its own first step
internally (ADR-0019), it does not duplicate its logic.

## 6. RenderGraph Candidate API — Tagged Usages, Execution Callback, `CompiledGraph` Extensions

**Additive, not breaking**, to `RenderGraphBuilder` (Spec 0005's existing
`declareResource()`/`declarePass()`/`compile()` and untagged
`reads()`/`writes()` are unchanged):

```cpp
// render_graph_builder.h additions
void reads(PassHandle pass, ResourceHandle resource, atlantis::rhi::ResourceState state);
void writes(PassHandle pass, ResourceHandle resource, atlantis::rhi::ResourceState state);

using PassExecuteFn = std::function<void(atlantis::rhi::CommandList&)>;
void setExecute(PassHandle pass, PassExecuteFn fn);
```

Internally, `RenderGraphBuilder::ResourceUsage` gains
`std::optional<atlantis::rhi::ResourceState> state` (empty for an
untagged Spec 0005 usage call); `PassRecord` gains
`PassExecuteFn executeFn` (empty `std::function` if never set — an
isolated or non-executing pass, still legal per Spec 0005's pass-retention
rule).

`CompiledGraph` (Spec 0005, `Accepted`/implemented) gains **additive**
accessors — sanctioned explicitly by Spec 0005's own Out of Scope /
Future Work ("a future spec may extend... this round's compiled graph
shape"), not a re-decision of anything it already fixed:

```cpp
// compiled_graph.h additions

// Same pattern as CompiledPassId -- see that class's own doc comment;
// identical provenance/lifetime contract.
class CompiledResourceId {
 public:
  CompiledResourceId() noexcept = default;
  [[nodiscard]] bool operator==(const CompiledResourceId&) const noexcept = default;
  [[nodiscard]] std::size_t index() const noexcept { return index_; }
 private:
  friend class CompiledGraph;
  explicit CompiledResourceId(std::size_t index) noexcept : index_(index) {}
  std::size_t index_ = static_cast<std::size_t>(-1);
};

struct CompiledResourceUsage {
  CompiledResourceId resource;
  bool isWrite = false;
  std::optional<atlantis::rhi::ResourceState> state;  // empty = untagged Spec 0005 usage
};

// On CompiledGraph:
[[nodiscard]] std::size_t resourceCount() const noexcept;
[[nodiscard]] CompiledResourceId resourceAt(std::size_t index) const;      // index < resourceCount()
[[nodiscard]] std::string_view label(CompiledResourceId resource) const;   // overload of existing label(CompiledPassId)
[[nodiscard]] bool hasProducer(CompiledResourceId resource) const noexcept;
[[nodiscard]] bool requiresRhiBinding(CompiledResourceId resource) const noexcept;  // >=1 ResourceState-tagged usage
[[nodiscard]] std::size_t usageCount(CompiledPassId pass) const noexcept;
[[nodiscard]] CompiledResourceUsage usage(CompiledPassId pass, std::size_t index) const;
```

`compiled_graph.h` gains `#include <atlantis/rhi/types.h>` — this is
`render_graph`'s RHI dependency (ADR-0021) landing concretely in the one
header that needs `ResourceState`. Out-of-range index/id on any new
accessor follows `CompiledGraph`'s existing convention exactly:
`ATLANTIS_CHECK`, non-terminating-handler fallback returns an
invalid-sentinel `CompiledResourceId` or empty `CompiledResourceUsage`
(never mistakable for a real value, same reasoning as `passOrder()`'s
existing fallback).

`compile_algorithm.{h,cpp}` (private, white-box-tested) is extended to
carry each pass's `PassExecuteFn` and each usage's `ResourceState`
through from the builder's `PassRecord`/`ResourceUsage` into
`CompiledGraph`'s own `PassRecord`/new resource-record storage — a
mechanical passthrough, not a change to dependency derivation, cycle
detection, or ordering (Section 6 of Plan 0005's own algorithm is
untouched).

## 7. RenderGraph Candidate API — `execute()`, `ResourceBinding`, the Two Guards

`src/render_graph/include/atlantis/render_graph/execution.h`:

```cpp
#pragma once

#include <vector>

#include <atlantis/render_graph/compiled_graph.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>

namespace atlantis::render_graph {

// Binds one compiled-local resource to a live RHI RenderTarget for
// exactly one execute() call -- frame-scoped, not persisted on the
// builder or CompiledGraph (ADR-0021). target must outlive the
// execute() call; execute() does not take ownership of it.
struct ResourceBinding {
  CompiledResourceId resource;
  atlantis::rhi::RenderTarget* target = nullptr;
};

// Walks graph's compiled pass order once. For each pass, for each
// ResourceState-tagged usage whose declared state differs from that
// resource's most-recently-recorded state, records a transitionResource()
// call before invoking the pass's execution callback. Inserts one
// trailing transitionResource() to ResourceState::PresentSource for
// every bound resource that was actually touched by at least one usage.
// Records only -- never calls Device::submit() or Presentation::present()
// (ADR-0021). Two preconditions are guaranteed-detectable programmer
// errors (ATLANTIS_CHECK_MSG), not Result-typed: every ResourceState-
// tagged usage in graph must have a matching entry in bindings; no
// bound resource may have any declared read usage anywhere in graph
// (protects ADR-0019's always-Undefined-incoming-layout premise
// structurally). Spec 0005's plain untagged usages need no binding and
// produce no transition. Not thread-safe; single Phase 1 frame thread
// only (ADR-0004).
void execute(const CompiledGraph& graph, const std::vector<ResourceBinding>& bindings,
             atlantis::rhi::CommandList& commandList);

}  // namespace atlantis::render_graph
```

### `execute()` algorithm (candidate, `execution.cpp`)

```
1. For each resource r where graph.requiresRhiBinding(r):
     ATLANTIS_CHECK_MSG(bindings contains an entry for r,
                         "ResourceState-tagged resource has no binding")
2. For each entry b in bindings:
     ATLANTIS_CHECK_MSG(graph has no read usage anywhere for b.resource,
                         "bound RenderTarget has a declared read usage")
3. currentState: map<CompiledResourceId, ResourceState>  (empty initially)
4. for position in [0, graph.passCount()):
     pass = graph.passOrder(position)
     for i in [0, graph.usageCount(pass)):
       usage = graph.usage(pass, i)
       if !usage.state.has_value(): continue         // untagged -- no transition bookkeeping
       binding = find in bindings by usage.resource
       if binding not found: continue                // step 1's ATLANTIS_CHECK_MSG already reported this;
                                                       // under a non-terminating handler (test), skip this
                                                       // usage's transition rather than dereferencing a
                                                       // missing binding -- the same UB-safe check-then-
                                                       // early-return pattern RenderGraphBuilder already uses
       previous = currentState.count(usage.resource) ? currentState[usage.resource]
                                                       : ResourceState::Undefined
       if previous != *usage.state:
         commandList.transitionResource(*binding.target, previous, *usage.state)
         currentState[usage.resource] = *usage.state
     // invoke pass's execution callback, if it has one (a declared-but-
     // never-.setExecute()'d pass is legal -- Spec 0005 pass retention)
     if pass has an executeFn: executeFn(commandList)
5. for each entry b in bindings:
     if currentState.count(b.resource):               // only if actually touched -- no spurious transition
       last = currentState[b.resource]
       if last != ResourceState::PresentSource:
         commandList.transitionResource(*b.target, last, ResourceState::PresentSource)
```

This satisfies every Testing & Verification Plan bullet from Spec 0006's
"RenderGraph execution integration" test list (no-op on same-state
usages; exactly one transition on a state change; exactly one trailing
transition regardless of pass count; no transition for an unused binding;
compiled-order-exact callback invocation; both guard-check cases).

## 8. Vulkan Backend Implementation — Concrete `RenderTarget`/`CommandList`/`SubmissionSignal`

`VulkanRenderTarget final : public rhi::RenderTarget` (in
`vulkan_render_target.h`): holds a non-owning `VkImage`, the swapchain
image index (`std::uint32_t`), `Extent2D`, `Format`, and a non-owning
reference to `VulkanPresentation`'s acquire-complete `VkSemaphore`.
Constructed only inside `VulkanPresentation::acquireNextTarget()`. No
`VkImageView` is created or stored — `vkCmdClearColorImage` (the only
write operation this round performs) operates directly on `VkImage` with
a subresource range; a view becomes necessary only once a real
render-pass/attachment path exists (future Renderer spec).

`VulkanCommandList final : public rhi::CommandList` (in
`vulkan_command_list.h`): holds a non-owning `VkCommandBuffer` (owned by
`VulkanDevice`'s command pool) and a non-owning `VulkanDevice&`.
`transitionResource()`/`clearColor()` `static_cast<VulkanRenderTarget&>`
the `rhi::RenderTarget&` argument (safe: only Vulkan Backend ever
constructs one in Phase 1, per ADR-0001's single-backend constraint) and
record the corresponding `vkCmdPipelineBarrier`/`vkCmdClearColorImage`
call. Its destructor calls `vkFreeCommandBuffers` for its one buffer —
matching ADR-0020's "not pooled beyond ordinary RAII."

`VulkanSubmissionSignal final : public rhi::SubmissionSignal` (in
`vulkan_submission_signal.h`): holds a non-owning `VkSemaphore` (the
persistent render-finished semaphore `VulkanDevice` owns — see Section
9). No destructor-time Vulkan call; the semaphore's lifetime is
`VulkanDevice`'s, not this token's.

### `resource_state_mapping.{h,cpp}` — pure, GPU-independent (mirrors `vulkan_result.h`'s existing separation)

```cpp
namespace atlantis::vulkan_backend::detail {

struct ImageBarrierPlan {
  VkImageLayout oldLayout;
  VkImageLayout newLayout;
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;
};

// before must not equal after -- execute() (Section 7) never calls
// transitionResource() for a no-op state pair, so this function does
// not need to handle one; ATLANTIS_CHECK_MSG guards any combination this
// round does not define a mapping for (e.g. PresentSource -> anything,
// which never occurs in this round's own execute() algorithm).
[[nodiscard]] ImageBarrierPlan planTransition(atlantis::rhi::ResourceState before, atlantis::rhi::ResourceState after);

}  // namespace atlantis::vulkan_backend::detail
```

Concrete mapping this round defines (all others `ATLANTIS_CHECK_MSG`-fail
as unimplemented for this scope):

| before | after | oldLayout | newLayout | srcAccess | dstAccess | srcStage | dstStage |
|---|---|---|---|---|---|---|---|
| `Undefined` | `ColorAttachmentWrite` | `UNDEFINED` | `TRANSFER_DST_OPTIMAL` | `0` | `TRANSFER_WRITE_BIT` | `TOP_OF_PIPE` | `TRANSFER` |
| `ColorAttachmentWrite` | `PresentSource` | `TRANSFER_DST_OPTIMAL` | `PRESENT_SRC_KHR` | `TRANSFER_WRITE_BIT` | `0` | `TRANSFER` | `BOTTOM_OF_PIPE` |
| `Undefined` | `PresentSource` | `UNDEFINED` | `PRESENT_SRC_KHR` | `0` | `0` | `TOP_OF_PIPE` | `BOTTOM_OF_PIPE` |

**Deliberate note, not an oversight:** `ColorAttachmentWrite` maps to
`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, not `COLOR_ATTACHMENT_OPTIMAL`.
`clearColor()` records `vkCmdClearColorImage`, which requires `GENERAL`
or `TRANSFER_DST_OPTIMAL` — not the render-pass-only
`COLOR_ATTACHMENT_OPTIMAL` layout. The RHI-level enumerator name
describes *intent* ("this pass writes the color target"); its concrete
Vulkan layout is this round's own implementation choice, tied to the one
write operation (`clearColor`) that exists. A future Minimal Renderer
spec, once it adds real attachment rendering, may need to extend
`ResourceState` with a distinct color-attachment-for-rendering variant
rather than reusing this one — not decided or precluded here.

`transitionResource()`'s Vulkan Backend body: call
`detail::planTransition(before, after)`, then one
`vkCmdPipelineBarrier(commandBuffer_, plan.srcStage, plan.dstStage, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier)`
with `imageBarrier` built from the plan plus the target's `VkImage` and a
full-resource `VkImageSubresourceRange{COLOR_BIT, 0, 1, 0, 1}`.

`clearColor()`'s Vulkan Backend body: `vkCmdClearColorImage(commandBuffer_,
target.image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &vkClearColor, 1,
&fullResourceRange)`. **Precondition, caller-enforced by `execute()`'s own
algorithm (Section 7), not re-checked here:** the target must already be
in `TRANSFER_DST_OPTIMAL` (i.e. `ColorAttachmentWrite`) when this is
called — `execute()`'s transition-insertion step always runs before the
pass callback that would call this, so the precondition holds
structurally for any graph `execute()` itself drives.

## 9. Vulkan Backend Implementation — `VulkanDevice` Submission Ownership and Drain

`VulkanDevice` gains:

```cpp
VkQueue queue() const noexcept { return queue_; }  // new accessor -- VulkanPresentation::present() needs it

// New members:
VkCommandPool commandPool_;                    // created once at construction (RESET_COMMAND_BUFFER_BIT)
VkSemaphore renderFinishedSemaphore_;           // created once at construction
VkFence submissionFence_;                       // created once at construction, initial state unsignaled
std::unique_ptr<atlantis::rhi::CommandList> retainedSubmission_;  // null until the first submit()
bool hasRetainedSubmission_ = false;            // distinguishes "never submitted" from "fence not yet waited"
```

### `createCommandList()`

1. `vkAllocateCommandBuffers(device_, {commandPool_, PRIMARY, 1}, &buffer)` — check `VkResult`, map via
   `toCommandListCreateError()` (new `vulkan_result.h` function).
2. `vkBeginCommandBuffer(buffer, {ONE_TIME_SUBMIT_BIT})` — check, same mapping.
3. Return `Ok(make_unique<VulkanCommandList>(*this, buffer))`. The returned `CommandList` is already recording.

### `submit(commandList, target)` — the ownership-transfer/retained-submission state machine

```
1. downcast commandList to VulkanCommandList; vkEndCommandBuffer(its buffer) -- check, map to SubmitError
2. if hasRetainedSubmission_:
     vkWaitForFences(device_, 1, &submissionFence_, VK_TRUE, UINT64_MAX) -- check, map to SubmitError
     vkResetFences(device_, 1, &submissionFence_) -- check
     retainedSubmission_.reset()   // destroys the prior VulkanCommandList -> vkFreeCommandBuffers
   // else: first submission ever on this Device -- nothing retained, nothing to wait on
3. downcast target to VulkanRenderTarget; read its acquire-complete VkSemaphore
4. VkSubmitInfo{ waitSemaphores = {acquireSemaphore}, waitDstStageMask = {COLOR_ATTACHMENT_OUTPUT_BIT},
                 commandBuffers = {commandList's buffer}, signalSemaphores = {renderFinishedSemaphore_} }
5. vkQueueSubmit(queue_, 1, &submitInfo, submissionFence_) -- check, map to SubmitError
6. retainedSubmission_ = std::move(commandList); hasRetainedSubmission_ = true
7. return Ok(make_unique<VulkanSubmissionSignal>(renderFinishedSemaphore_))
```

**Why one persistent semaphore pair is safe with single frame-in-flight:**
step 2 unconditionally waits (CPU-side) for the *previous* submission's
fence before this call's own `vkQueueSubmit` (step 5) reuses
`renderFinishedSemaphore_` as a signal target again, and before the
*previous* acquire's semaphore could possibly be reused by a new
`acquireNextTarget()` call (which the caller cannot even make until this
`submit()` call returns, per the single-logical-frame-thread ordering).
No second in-flight submission ever exists to collide with.

**Confirmed design constraint, accepted by Human Review (see the Human
Review Approval note above), not a defect to redesign around:** the fence
wait in step 2 only proves the *previous submission's command buffer*
finished executing — it does **not** prove `renderFinishedSemaphore_` was
ever *waited on* by anything. A binary semaphore must be unsignaled
before it is used as a signal operand again; if a caller called `submit()`
twice in a row without an intervening `present()` (which alone waits on
that semaphore), the second `vkQueueSubmit`'s signal operation would be
signaling an already-signaled-but-unconsumed semaphore — a Vulkan valid-
usage violation Validation Layers would catch, not something the fence
wait above protects against. **Precondition, not enforced by the type
system or by an `ATLANTIS_CHECK`:** a caller must call the matching
`present()` for a successful `submit()` before calling `submit()` again.
This holds structurally on the ordinary per-frame path (§11: `submit()`
then always `present()` before the next `acquireNextTarget()`/`submit()`),
and `submit()` followed directly by application exit (no `present()` at
all) is explicitly **legal** — `Device::waitIdle()` drains the outstanding
submission and the caller cleans up safely, the same reasoning §11's
mid-frame-exit path already establishes for an acquired-but-unsubmitted
`RenderTarget`. It is a documented caller obligation, the same
undetected-precondition tier as `RenderTarget`'s own frame-window misuse
(§12) — not something this round adds cross-class bookkeeping between
`VulkanDevice` and `VulkanPresentation` to detect. Enforcement is via API
documentation, the ordinary-sequence tests already specified in §13, and
code review; nothing in this Plan's own Testing Strategy (§13) exercises
calling `submit()` twice without an intervening `present()` — a
deliberate choice, matching how genuinely-undetected preconditions are
tested elsewhere in this codebase (i.e. not tested, by design), confirmed
as the intended enforcement model by Human Review.

### `waitIdle()`

```
1. if hasRetainedSubmission_:
     vkWaitForFences(...) -- check; vkResetFences(...) -- check; retainedSubmission_.reset(); hasRetainedSubmission_ = false
2. vkDeviceWaitIdle(device_) -- check, map to SubmitError  (belt-and-suspenders: also drains any
   presentation-engine-internal work not tracked by step 1's fence, e.g. an acquired-but-never-
   submitted RenderTarget's acquire semaphore on the mid-frame-exit path -- see Section 11)
3. return Ok
```

### `~VulkanDevice()`

Same drain sequence as `waitIdle()`, run unconditionally as a defensive
guarantee (destructor cannot return `Result`, so any failure here is
logged via `ATLANTIS_LOG_ERROR` and swallowed, not propagated) — this is
the "equivalent guarantee built into `Device`'s destructor" Spec 0006
Requirements named as one acceptable shape for the drain capability. The
**documented, tested** discipline (Section 13) is still that a caller
calls `waitIdle()` explicitly and checks its `Result` before destroying
`Presentation`/`Device` on every path, including a mid-frame exit —
matching every other explicit-error-checking convention in this
codebase; the destructor fallback exists only to prevent a crash if that
discipline is ever violated, not to make explicit draining optional.

Destruction order is unchanged from Spec 0003: `Presentation` before
`Device` (caller-enforced, per existing `examples/rhi_vulkan_demo`
convention) — `Presentation`'s own destructor (Section 10) destroys the
swapchain and surface; `VulkanDevice`'s destructor must not run first.

## 10. Vulkan Backend Implementation — `VulkanPresentation` Extensions

New members: `VkSemaphore acquireCompleteSemaphore_` (created once at
construction, one persistent instance — safe to reuse every frame by the
same single-frame-in-flight reasoning as Section 9).

### `acquireNextTarget()`

```
1. recreateResult = recreateIfNeeded()   // existing Spec 0003 method, called as-is, unchanged
2. if recreateResult.isErr(): return Err(recreateResult.error())
3. if trackedExtent_.isZero(): return Ok(nullptr)   // structurally unreachable to any Vulkan call below
4. ATLANTIS_CHECK(swapchain_ != VK_NULL_HANDLE)  // recreateIfNeeded() must have created one for non-zero extent
5. result = vkAcquireNextImageKHR(device_.device(), swapchain_, UINT64_MAX,
                                   acquireCompleteSemaphore_, VK_NULL_HANDLE, &imageIndex)
6. if result == VK_ERROR_OUT_OF_DATE_KHR:
     recreationNeeded_ = true; return Ok(nullptr)          // no in-call retry -- ADR-0019
7. if result == VK_SUBOPTIMAL_KHR:
     recreationNeeded_ = true                              // recreate on the NEXT call; this image is still usable
   else if result != VK_SUCCESS:
     return Err(toAcquireFailureError(result))             // new vulkan_result.h fn -> PresentationError
8. return Ok(make_unique<VulkanRenderTarget>(images_[imageIndex], imageIndex, metadata_.extent,
                                              metadata_.format, acquireCompleteSemaphore_))
```

Step 8 requires `images_` (a `std::vector<VkImage>` of the current
swapchain's images, queried via `vkGetSwapchainImagesKHR` once per
successful `recreateIfNeeded()`) to be added to `VulkanPresentation`'s
existing state — the current implementation queries only image *count*
for metadata, not the handles themselves; this is a genuinely new field
`recreateIfNeeded()`'s existing recreation branch must also populate.

Steps 6–7's branching is extracted into a pure, GPU-independent function
in `presentation_logic.h` (a new private header, alongside the existing
`decideRecreateAction()` in `vulkan_presentation.h`, or added to that
same file — Plan does not fix which, Implementation's choice), mirroring
that function's existing extraction pattern exactly:

```cpp
enum class AcquireAction {
  Proceed,                  // VK_SUCCESS -- use the acquired image, no recreation flag change
  SkipAndAwaitNextCall,     // VK_ERROR_OUT_OF_DATE_KHR -- Ok(nullptr) this call, recreation marked needed
  ProceedButMarkRecreate,   // VK_SUBOPTIMAL_KHR -- use the acquired image, recreation marked needed for next call
  Fail,                     // any other non-VK_SUCCESS result -- Err(toAcquireFailureError(result))
};

// result must be the direct return of vkAcquireNextImageKHR. Pure
// classification -- takes only an already-obtained VkResult, calls no
// Vulkan function, testable with literal VkResult enumerators and no
// device (same reasoning as decideRecreateAction() and
// classifyFailure()).
[[nodiscard]] AcquireAction decideAcquireAction(VkResult result);
```

**Note for §13's test list:** `decideAcquireAction()` above is the "new
pure decision logic" `presentation_logic_tests.cpp` gains — concretely:
`VK_SUCCESS`
→ `Proceed`; `VK_ERROR_OUT_OF_DATE_KHR` → `SkipAndAwaitNextCall`;
`VK_SUBOPTIMAL_KHR` → `ProceedButMarkRecreate`; every other non-success
`VkResult` this codebase's existing `classifyFailure()` already handles
(e.g. `VK_ERROR_DEVICE_LOST`, `VK_ERROR_SURFACE_LOST_KHR`) → `Fail`.

### `present(target, renderFinished)`

```
1. downcast target to VulkanRenderTarget; downcast renderFinished to VulkanSubmissionSignal
2. VkPresentInfoKHR{ waitSemaphores = {renderFinished's semaphore}, swapchains = {swapchain_},
                      imageIndices = {target's image index} }
3. result = vkQueuePresentKHR(device_.queue(), &presentInfo)
4. target.reset(); renderFinished.reset()   // end both borrows regardless of outcome
5. if result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR:
     recreationNeeded_ = true; return Ok()                 // routine, not Err -- ADR-0019
6. if result != VK_SUCCESS: return Err(toPresentFailureError(result))
7. return Ok()
```

### Resize / zero-extent / out-of-date / suboptimal — step-by-step summary

| Trigger | Where handled | Effect |
|---|---|---|
| `WindowResize` event | Caller calls `notifyResized()` (unchanged, Spec 0003) | Marks recreation needed; no Vulkan call |
| Next `acquireNextTarget()` after resize | Step 1 (`recreateIfNeeded()`) | Swapchain recreated at new extent before acquiring |
| Extent `{0,0}` (minimize) | Step 3 | `Ok(nullptr)`, zero Vulkan calls, every call |
| Restore (extent non-zero again) | Step 1 of the next call | Recreates, then acquires normally — no special "recovery" call |
| `VK_ERROR_OUT_OF_DATE_KHR` at acquire | Step 6 | `Ok(nullptr)` this call; recreate on the *next* call |
| `VK_SUBOPTIMAL_KHR` at acquire | Step 7 | Proceeds with this image; recreate on the *next* call |
| `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` at present | present() step 5 | `Ok()`; recreate on the next `acquireNextTarget()` |
| Genuine unrecoverable error (acquire or present) | steps 7 / 6 respectively | `Err(PresentationError)` |

## 11. Single Frame-in-Flight State Machine and Cleanup Paths

State lives entirely inside `VulkanDevice` (Section 9):
`hasRetainedSubmission_` is the only state bit; `true` means exactly one
`CommandList` + its fence are outstanding, `false` means none are. Every
`submit()` call transitions `false → true` (first call) or `true → true`
(waits, releases, re-submits); `waitIdle()`/the destructor transition any
state to `false`.

**Ordinary per-frame path:**
`acquireNextTarget()` → (skip if `nullptr`) → `createCommandList()` →
`execute()` (records) → `submit()` (ownership transfers) → `present()`.

**Mid-frame exit (acquire succeeded, submit/present never called for that
frame):** the acquired `RenderTarget` (and, if one was created, the
unsubmitted `CommandList`) simply go out of scope — no ownership
transfer to `Device` ever happened, so `Device`'s own retained-submission
state is untouched by this path. Before destroying `Presentation`/
`Device`, the caller must still call `Device::waitIdle()` (Section 9 step
2's `vkDeviceWaitIdle` drains any presentation-engine-internal state tied
to the acquired-but-never-submitted image's semaphore) — this is exactly
Spec 0006's destruction precondition, satisfied on this path by the same
one call used on every other exit path.

**`submit()` succeeded, `present()` never called (submit-then-exit):**
legal, per Human Review's explicit confirmation (see the Plan's Human
Review Approval note and §9). `hasRetainedSubmission_` is `true`; the
returned `SubmissionSignal` simply goes out of scope unused (its
destructor does nothing — it never owned the semaphore, §3). The exact
same `Device::waitIdle()` call the caller must make before destroying
`Presentation`/`Device` on every path (§9's `waitIdle()` step 1) waits on
the retained fence and releases the retained `CommandList` — no separate
handling is needed for this case versus the ordinary end-of-run cleanup
already described below.

**Exception/early-return exit inside the frame loop:** this codebase
does not use exceptions (ADR-0009) — every fallible step already returns
`Result`; an `Err` anywhere in the loop is handled by the caller choosing
to set a `failed` flag and fall through to the same cleanup sequence
(`waitIdle()` → `presentation.reset()` → `device.reset()` →
`platform::shutdown()`) already established by `examples/rhi_vulkan_demo`
(Section 8's demo composition, extended — see Section 13's manual
verification design). No new cleanup mechanism is introduced; `waitIdle()`
is inserted into the existing pattern at exactly the point
`presentation.reset()` already happens.

## 12. Error Model Implementation

| Case | Tier | Mechanism |
|---|---|---|
| `RenderTarget`/`CommandList` used outside its frame/lifetime window; `Presentation`/`Device` destroyed with an outstanding acquire/submission; `submit()` called again before the previous `SubmissionSignal` was consumed by `present()` (§9) | Lifetime/sequencing precondition violation | Not detected; caller obligation (documented, tested via correct-discipline manual verification only) |
| Binding missing for a `ResourceState`-tagged usage; a bound resource has a read usage | Guaranteed-detectable programmer error | `ATLANTIS_CHECK_MSG` inside `execute()` |
| `vkAcquireNextImageKHR`/`vkQueuePresentKHR` unrecoverable failure | Recoverable | `Result::Err(PresentationError)` |
| `vkQueueSubmit`/`vkWaitForFences`/`vkDeviceWaitIdle`/`vkAllocateCommandBuffers`/`vkBeginCommandBuffer`/`vkEndCommandBuffer` failure | Recoverable | `Result::Err(SubmitError)` / `Result::Err(CommandListCreateError)` |
| `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` at acquire or present | Not an error | `Ok(nullptr)` / `Ok()`, recreation-needed bookkeeping |
| Zero framebuffer extent | Not an error | `Ok(nullptr)`, zero Vulkan calls |

`vulkan_result.h` gains, mirroring its existing pure-classification
pattern: `toAcquireFailureError(VkResult) -> PresentationError`,
`toPresentFailureError(VkResult) -> PresentationError`,
`toSubmitError(VkResult) -> SubmitError`,
`toCommandListCreateError(VkResult) -> CommandListCreateError` — each a
pure function over an already-non-`VK_SUCCESS` `VkResult`, unit-testable
with literal enumerators exactly like the four existing mapping
functions.

## 13. Testing Strategy

### GPU-independent unit tests (layer 1, no Vulkan device)

- `tests/vulkan_backend/resource_state_mapping_tests.cpp` — every defined
  `(before, after)` pair in Section 8's table produces the documented
  layout/access/stage values; an undefined pair triggers the
  programmer-error/assertion policy.
- `tests/vulkan_backend/vulkan_result_tests.cpp` — extended with cases
  for the four new mapping functions (each `VkResult` enumerator this
  round needs to classify → its documented error).
- `tests/vulkan_backend/presentation_logic_tests.cpp` — extended with
  `decideAcquireAction()` (§10) cases for every `VkResult` this round
  needs to classify (`VK_SUCCESS`, `VK_ERROR_OUT_OF_DATE_KHR`,
  `VK_SUBOPTIMAL_KHR`, and at least one representative unrecoverable
  failure), mirroring `decideRecreateAction()`'s existing extraction
  pattern, exercised without a real device.
- `tests/render_graph/execution_tests.cpp` — every bullet from Spec
  0006's own Testing & Verification Plan "RenderGraph execution
  integration" list (Section 7 above enumerates them), run against
  `tests/render_graph/fake_command_list.h`, a test-only
  `rhi::CommandList` implementation that records which calls it
  received (state, target identity, order) for assertion, with no
  Vulkan device anywhere in the test binary.
- `tests/rhi/types_tests.cpp` — trivial `ResourceState`/`ClearColorValue`
  sanity (equality/default values), consistent with existing
  `Extent2D`/`Format` coverage.

**Spec 0006's `RenderTarget`-is-move-only Acceptance Criterion needs no
dedicated static-assert test.** Under the abstract-base-class-behind-
`std::unique_ptr` mechanism this Plan uses (§3, Human Review Confirmation
2), move-only-ness is a property of `std::unique_ptr<RenderTarget>`
itself — a standard-library guarantee, not Atlantis code to verify. This
differs from `CompiledGraph` (Spec 0005), which is a concrete value type
whose move/copy special members genuinely are Atlantis's own code and
therefore did need an explicit `std::is_move_constructible`-style check.
No such check is meaningful or needed here; this is a structural
consequence of the mechanism, not an oversight.

### GPU-required integration tests (real Windows Vulkan device, no window)

`tests/vulkan_backend/frame_execution_gpu_tests.cpp`, labeled `"gpu"`
(same CTest label convention as the existing
`atlantis_vulkan_backend_gpu_tests` target): construct `Device` +
`Presentation` against a real (possibly headless-adjacent, but this
round still requires a window per windowed-first sequencing — see Spec
0006 Non-Goals) surface, exercise `acquireNextTarget()` →
`createCommandList()` → a one-pass `execute()` → `submit()` → `present()`
end-to-end at least once, and exercise `waitIdle()` after a deliberate
mid-frame exit (acquire, then no submit/present). Vulkan Validation
Layers enabled throughout (existing `enableValidationLayers` construction
param, unchanged).

### Manual verification (interactive, Windows, real GPU)

`examples/frame_execution_demo/main.cpp` — a new, separate composition
from `examples/rhi_vulkan_demo` (which remains Spec 0003's own,
unmodified, still-renders-nothing artifact). Structure mirrors
`rhi_vulkan_demo`'s existing event loop (Section 8's earlier reading)
exactly, with these frame-loop additions inside the existing
`WindowResize`-handling branch's counterpart per-frame tick:

1. `acquireNextTarget()` → skip the rest of this iteration on
   `Ok(nullptr)`; log and set `failed` on `Err`.
2. `createCommandList()`.
3. Build a one-pass `RenderGraphBuilder`: `declareResource()` (label
   `"frame-target"`), `declarePass()` (label `"clear"`),
   `writes(clearPass, frameTarget, ResourceState::ColorAttachmentWrite)`,
   `setExecute(clearPass, [&](CommandList& cmd) { cmd.clearColor(*target, kClearColor); })`,
   `compile()`.
4. `render_graph::execute(compiledGraph, {{resourceId, target.get()}}, *commandList)`.
5. `submit(std::move(commandList), *target)` → `present(std::move(target), std::move(signal))`.
6. On `failed`/`closeRequested` (existing pattern): call
   `device->waitIdle()` **before** `presentation.reset()`/`device.reset()`
   — the one addition to `rhi_vulkan_demo`'s existing cleanup sequence
   this spec's destruction precondition requires.

Confirms, matching Spec 0006's Testing & Verification Plan exactly: a
visible, correctly-colored window across repeated frames; correct color
across interactive resize with no corruption/tearing/validation warning;
zero Vulkan calls while minimized (verified by inspection of step 1's
`Ok(nullptr)` path, mirroring Spec 0003's own guarantee); resumes on
restore with no special recovery step; a deliberate mid-frame exit
(closing the window between a logged "acquired" message and the next
`submit()` call, exercised at least once manually) completes cleanly with
Validation Layers clean throughout, including at shutdown.

### Debug/Release and command sequence per step

Every implementation step in Section 16 ends with:

```
cmake --build build --config Debug
ctest --test-dir build -C Debug -LE gpu
cmake --build build --config Release
```

GPU-touching steps additionally run, on the Windows machine with a real
GPU:

```
ctest --test-dir build -C Debug -L gpu
build\examples\frame_execution_demo\Debug\frame_execution_demo.exe
```

with Validation Layers output inspected for any WARNING/ERROR (the demo
aborts on one via the existing default assertion failure handler,
matching `rhi_vulkan_demo`'s own documented behavior).

## 14. Explicit Prohibitions (grep/code-review checklist)

Run at the end of every implementation step, and again before opening a
PR:

```
# No Vk* type or Vulkan header outside Vulkan Backend
grep -rn "Vk[A-Z]" src/rhi/include src/render_graph/include        # expect: no matches
grep -rln "vulkan" src/rhi/include src/render_graph/include -i     # expect: no matches (excluding this comment style)

# No direct vkCmd* call outside Vulkan Backend's CommandList implementation
grep -rn "vkCmd" src --include=*.cpp --include=*.h | grep -v "src/vulkan_backend/"   # expect: no matches

# RenderGraph never submits or presents
grep -rn "Device::submit\|->submit(\|\.submit(\|->present(\|\.present(" src/render_graph  # expect: no matches

# No recording outside a RenderGraph pass callback
grep -rn "transitionResource\|clearColor" src/render_graph src/rhi --include=*.cpp | grep -v "execution.cpp"  # expect: no matches (execution.cpp's own transitionResource() calls are the only ones)

# No new third-party dependency
git diff --stat CMakeLists.txt cmake/  # expect: no changes to dependency-fetching files

# No src/renderer/, no Shader System source
find src/renderer src/shader_system 2>/dev/null  # expect: nothing (dirs do not exist)

# No pipeline/shader/general-resource type introduced
grep -rn "VkPipeline\|VkShaderModule\|class Buffer\|class Texture\|class Sampler" src/rhi src/vulkan_backend  # expect: no matches

# No multiple-frames-in-flight machinery
grep -rn "std::vector<.*Fence\|std::vector<.*Semaphore\|std::array<.*Fence" src/vulkan_backend  # expect: no matches (exactly one of each, as plain members)
```

Code-review checklist (manual, per step):

- [ ] Every new `VkResult`-returning call's result is checked (no
      discarded return value).
- [ ] Every new public type documents its thread-safety contract in one
      line, per AGENTS.md.
- [ ] No `ATLANTIS_ASSERT`/`ATLANTIS_CHECK` used where a `Result::Err`
      belongs, or vice versa (cross-check against Section 12's table).
- [ ] `CommandList` ownership transfer (Section 9) is never bypassed —
      no code path outside `VulkanDevice::submit()`/its destructor/
      `waitIdle()` destroys a `VulkanCommandList`.

## 15. Build Integration

```cmake
# src/rhi/CMakeLists.txt -- header-only additions, no new .cpp required
# (render_target.h, command_list.h, submission_signal.h are pure
# interface headers; types.h's new enums need no new .cpp beyond
# whatever operator== atlantis_rhi's existing types.cpp already covers
# for similar small enums/structs -- extend types.cpp only if an
# operator== is added for ClearColorValue, per existing Extent2D
# precedent)

# src/vulkan_backend/CMakeLists.txt
add_library(atlantis_vulkan_backend STATIC
  src/vulkan_result.cpp
  src/validation.cpp
  src/vulkan_instance.cpp
  src/vulkan_device.cpp
  src/vulkan_presentation.cpp
  src/vulkan_render_target.cpp     # new
  src/vulkan_command_list.cpp      # new
  src/vulkan_submission_signal.cpp # new
  src/resource_state_mapping.cpp   # new
  src/wsi/win32_surface.cpp
)
# target_link_libraries unchanged (already links Atlantis::RHI, Vulkan::Vulkan)

# src/render_graph/CMakeLists.txt
add_library(atlantis_render_graph STATIC
  src/compile_algorithm.cpp
  src/compiled_graph.cpp
  src/render_graph_builder.cpp
  src/execution.cpp                # new
)
target_link_libraries(atlantis_render_graph
  PUBLIC
    Atlantis::Core
    Atlantis::RHI                  # new -- realizes ADR-0021's dependency
  PRIVATE
    atlantis_compiler_warnings
)

# tests/render_graph/CMakeLists.txt -- add execution_tests.cpp; this
# target already links atlantis_render_graph, which now PUBLIC-links
# Atlantis::RHI, so RHI headers are already reachable with no new link
# line needed for the test executable itself.

# tests/vulkan_backend/CMakeLists.txt -- add resource_state_mapping_tests.cpp
# as an additional source to the existing GPU-independent
# atlantis_vulkan_backend_tests target (same Vulkan::Vulkan link it
# already has, for VkImageLayout etc. types); add frame_execution_gpu_tests.cpp
# as an additional source to the existing atlantis_vulkan_backend_gpu_tests
# target (already LABELS "gpu", DISCOVERY_MODE PRE_TEST) -- no new CMake
# target of either kind, matching this file's existing two-target
# structure exactly.

# examples/frame_execution_demo/CMakeLists.txt -- mirrors
# examples/rhi_vulkan_demo/CMakeLists.txt exactly, new executable name.

# CMakeLists.txt (root)
if(ATLANTIS_BUILD_EXAMPLES)
  add_subdirectory(examples/foundation_demo)
  add_subdirectory(examples/platform_demo)
  add_subdirectory(examples/rhi_vulkan_demo)
  add_subdirectory(examples/frame_execution_demo)  # new
endif()
```

## 16. Implementation Order

Each step is independently reviewable and ends with the Section 13
build/test commands (GPU commands only where noted) and the Section 14
grep checklist.

1. **RHI types and interfaces** — `types.h` additions
   (`ResourceState`/`ClearColorValue`/`SubmitError`/
   `CommandListCreateError`), `render_target.h`, `command_list.h`,
   `submission_signal.h`, `device.h`/`presentation.h` signature
   additions (declarations only — no implementation yet, so nothing
   links). Build: header compilation only (no `.cpp` changes needed
   beyond `CMakeLists.txt` include-dir coverage, already present).
2. **`vulkan_result.h`/`.cpp` mapping functions** — the four new pure
   `VkResult → RHI-error` functions, plus
   `resource_state_mapping.{h,cpp}`. GPU-independent unit tests
   (`resource_state_mapping_tests.cpp`, extended
   `vulkan_result_tests.cpp`) land in this step.
3. **`VulkanRenderTarget`/`VulkanCommandList`/`VulkanSubmissionSignal`**
   concrete classes — no `VulkanDevice`/`VulkanPresentation` wiring yet,
   just the classes and their constructors/accessors.
4. **`VulkanDevice` extensions** — command pool, `createCommandList()`,
   the retained-submission state machine in `submit()`, `waitIdle()`,
   destructor drain, `queue()` accessor. No GPU test yet (needs
   `VulkanPresentation`'s acquire to have a real frame to submit
   against) — build-verify only this step.
5. **`VulkanPresentation` extensions** — `images_` field population in
   the existing recreation branch, `acquireCompleteSemaphore_`,
   `acquireNextTarget()`, `present()`. First step where
   `frame_execution_gpu_tests.cpp` (Section 13) becomes exercisable —
   land a minimal acquire → submit (empty command list, no transition)
   → present smoke test here. Run `ctest -C Debug -L gpu`.
6. **RenderGraph: tagged usages + execution callback** —
   `render_graph_builder.h`/`.cpp`, `compile_algorithm.h`/`.cpp`
   passthrough, `compiled_graph.h`/`.cpp` new `CompiledResourceId`/
   accessors. GPU-independent — extend `tests/render_graph/`'s existing
   suites (compile-time-property checks for the new type, plus
   passthrough-correctness cases) here, before `execute()` exists.
7. **RenderGraph: `execute()`** — `execution.h`/`.cpp`,
   `fake_command_list.h`, `execution_tests.cpp` (every bullet from
   Section 7/Spec 0006's own test list). Fully GPU-independent.
8. **Vulkan Backend GPU integration, full path** — extend
   `frame_execution_gpu_tests.cpp` to the full acquire → `execute()`
   (real one-pass clear graph) → `submit()` → `present()` cycle, plus the
   mid-frame-exit + `waitIdle()` case. `ctest -C Debug -L gpu`,
   Validation Layers inspected.
9. **`examples/frame_execution_demo`** — the interactive manual
   verification composition (Section 13). Run interactively: resize,
   minimize/restore, mid-frame window close, clean exit.
10. **Final consistency pass** — Section 14's full grep checklist, this
    Plan's Section 17 Acceptance Criteria mapping walked line by line,
    `cmake --build build --config Release` clean, `ctest --test-dir build -C Debug`
    (full suite, no label filter) green.

### Sequencing & Dependencies

Steps 1–3 have no cross-dependency beyond declaration order. Step 4
depends on 1–3 (needs the concrete types to exist). Step 5 depends on 4
(needs `Device::submit()` to exist for even a smoke test) and on 1–3.
Steps 6–7 (RenderGraph) have no dependency on 4–5 and could be built in
parallel by a different reviewer, but step 8's full integration test
needs both 5 and 7 complete. Step 9 needs 8. Step 10 is last, always.

## 17. Acceptance Criteria Mapping

| Spec 0006 Acceptance Criterion (abbreviated) | Plan Section(s) |
|---|---|
| No `Vk*`/Vulkan header in RHI/RenderGraph public headers | §1 file list (headers listed contain no Vulkan type), §14 grep |
| No `vkCmd*`/barrier construction outside Vulkan Backend's `CommandList` impl | §8 (`VulkanCommandList` is the only site), §14 grep |
| `RenderTarget` non-owning, frame-scoped, write-only; no read-back capability | §3 (doc comment), §7 guard 2 (structural enforcement) |
| `RenderTarget` move-only (compile-time property) | §3 (mechanism), §13 (why no dedicated test is needed — structural via `std::unique_ptr`) |
| Binding a read-used resource to a `RenderTarget` rejected as programmer error | §7 algorithm step 2 |
| Unbound `ResourceState`-tagged usage rejected as programmer error | §7 algorithm step 1 |
| `acquireNextTarget()` returns `Ok(nullptr)` at zero extent, first call and every later call | §10 step 3 |
| Windows resize → next acquire recreates then acquires at new extent | §10 step 1, table |
| Out-of-date/suboptimal at acquire or present never crashes/hangs/warns | §10 tables, §12 |
| `execute()` never calls `submit()`/`present()` | §7 (records only), §14 grep |
| No GPU command recorded outside a RenderGraph pass callback | §3 `CommandList` doc note, §14 grep |
| Every `VkResult` checked | §9, §10 step-by-step (every step "check"s), §14 checklist |
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
      (extended), `execution_tests.cpp`, `types_tests.cpp` (extended) —
      all pass under `ctest -C Debug -LE gpu`.
- [ ] Headless integration tests: not applicable (Spec 0006 Non-Goals;
      windowed-first sequencing).
- [ ] Image regression tests: not applicable (nothing beyond manual
      visual confirmation of a solid clear color this round).
- [ ] Vulkan Validation Layers clean: `ctest -C Debug -L gpu` and the
      manual `frame_execution_demo` run, including the mid-frame-exit
      case, produce zero WARNING/ERROR output.
- [ ] Other: §14's grep checklist returns the documented expected
      (empty, in every prohibited case) result.

## Rollback Plan

Every change is additive to existing, already-shipped modules (`atlantis_rhi`,
`atlantis_vulkan_backend`, `atlantis_render_graph`) — no existing public
method signature is altered or removed (Spec 0003's `notifyResized()`/
`recreateIfNeeded()`/`metadata()` and Spec 0005's untagged
`reads()`/`writes()`/`compile()` are unchanged). If a defect is found
post-merge, revert is a straightforward `git revert` of this feature's
merge commit(s) — no other module depends on anything this Plan adds
(RenderGraph's new `Atlantis::RHI` link is the only new edge, and nothing
yet depends on RenderGraph beyond its own tests/this Plan's own new demo).
A partial rollback (keep RHI additions, revert RenderGraph execution)
is possible but not expected to be needed, since Section 16's steps are
ordered so RHI (1–5) is independently mergeable/testable before
RenderGraph (6–7) builds on it.

## Definition of Done

Per [docs/process/definition-of-done.md](../docs/process/definition-of-done.md),
with these Plan-specific notes:

- Headless verification: not applicable (Non-Goal, unchanged from Spec 0003/0005's own DoD treatment of this item).
- Image regression: not applicable this round (no golden-image harness exists yet, per AGENTS.md sequencing).
- ADR: none new required by implementation — ADR-0019/0020/0021 already `Accepted` cover every architectural decision this Plan operationalizes; any *deviation* from them discovered during implementation is a Plan/Spec issue to raise, not a new ADR to file unilaterally.
- All other items apply as stated in the linked document.

## Consistency Review

Walked against Spec 0006 in full:

- **Every Functional Requirement** (RenderTarget, Presentation
  acquire/present, minimal RHI resource/command/submission, RenderGraph
  execution integration, resize/zero-extent/out-of-date handling,
  threading) has a corresponding Plan section (§3–§11) and, where
  applicable, a test (§13).
- **Every Non-Goal** is checked against §14's grep list or explicitly
  absent from §1's file list (no pipeline, no buffer/texture, no
  allocator, no Android, no headless, no multi-threading, no second
  backend, no caller-authored dependency edge/culling, no
  `src/renderer/`).
- **Every Acceptance Criterion** is mapped in §17.
- **Both implementation-level questions raised during drafting** are now
  recorded as Human-Review-confirmed decisions (§Human Review
  Confirmations Received), not open tensions — carried forward
  transparently, not silently buried in implementation prose, and
  additionally cross-referenced from a short ADR-0021 clarifying note
  that changes none of that ADR's Decision content.
- **No Accepted ADR's conclusion is restated, reopened, or contradicted**
  — ADR-0017/0018's dependency-derivation/ordering model is extended
  additively (§6), never altered; ADR-0016's `recreateIfNeeded()`
  contract is called, not reimplemented (§10); ADR-0001/0014's mechanism
  is extended to three new types, not replaced (§3).
- **Candidate API status:** every signature in §2–§7 was a Plan-stage
  candidate per Spec 0006's own "concrete C++ type/method names... left
  to the Plan" framing (mirroring Spec 0005's identical disclaimer) until
  the joint Spec 0006 + Plan 0006 Human Review reviewed and approved this
  Plan in full (see the Human Review Approval note at the top of this
  document) — implementation follows these signatures as written, per
  AGENTS.md's explicit-deviation rule, not as still-open candidates.
