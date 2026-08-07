# Plan: Atlantis RHI and Vulkan Windowed Foundation

- **Spec:** [specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved at joint Spec 0003 + Plan 0003 Human Review on
  2026-08-08 (see the Approval transition note immediately below for the
  exact scope approved).

> **Approval transition complete, 2026-08-08:** Human Review of
> `specs/0003-rhi-vulkan-windowed-foundation.md` together with this Plan
> is complete. This Plan is now `Approved / Ready for Implementation`.
> The reviewer's decision, recorded here rather than only in chat:
> 1. **Scope, candidate public API, module boundaries, file list,
>    implementation order, and verification approach** — the work this
>    Plan proposes across Sections 1–13, the Verification Checklist, and
>    the Acceptance Criteria Mapping — are **accepted as written**.
> 2. **The Vulkan Validation callback's local fatal policy (Section 6) is
>    accepted**: on `WARNING`/`ERROR`, the callback reports through the
>    existing `ATLANTIS_CHECK_MSG`/failure-handler path first; if the
>    currently-installed replacement handler returns normally instead of
>    terminating, an explicit `std::abort()` immediately follows, so
>    Validation failure cannot be bypassed by whichever handler happens to
>    be installed.
> 3. **The explicit `std::abort()` fallback is confirmed to be a private
>    implementation strategy** fulfilling Spec 0003's Validation-failure
>    requirement — **not** a new public assertion mechanism — and does
>    **not** require a new or amended ADR. This resolves the item Section
>    6 previously flagged as "Human Review must confirm or overrule" and
>    closes Consistency Review item 17.
> 4. **The existing inconsistency between
>    `docs/architecture/module_boundaries.md` and Accepted
>    ADR-0011/ADR-0014** (flagged in Section 1) **does not block this
>    Plan's Implementation.** It is not fixed by this Plan and is not
>    resolved by this approval — it is deferred to a separate, future
>    documentation-consistency task, per explicit Human Review direction
>    not to fold it into this approval, and `module_boundaries.md` is not
>    edited by this record.
> 5. **Every Non-Goal (top-of-document Non-Goals section) and every
>    prohibition in Section 9 (Explicit Prohibitions) remain fully in
>    force for Implementation**: no acquire-shaped API, `present()`,
>    `RenderTarget`, GPU command, command buffer, synchronization
>    primitive, RenderGraph, Renderer, or anything else outside Spec
>    0003's scope may be implemented under this Plan.
>
> This record documents the Human Review decision; it does not itself
> constitute Implementation, and **no Implementation has begun** under
> this Plan as of this approval record.
>
> **Revision note (this revision — corrected `ATLANTIS_CHECK_MSG`
> assumption; genuine fatal fallback added):** the previous revision
> claimed the debug-messenger callback's unconditional call to
> `ATLANTIS_CHECK_MSG` was itself a "structural" guarantee that any
> Vulkan Validation `WARNING`/`ERROR` terminates the process. Re-reading
> `src/core/include/atlantis/assert.h` and `src/core/src/assert.cpp`
> directly (both read this revision) shows this was wrong:
> `AssertFailureHandler` returns `void`, not `[[noreturn]]`;
> `setFailureHandler()` accepts any handler, including one that returns
> normally; `reportFailure()` calls that handler and then itself returns
> normally; only the *default* handler happens to call `std::abort()`.
> `ATLANTIS_CHECK_MSG` alone is therefore **not** structurally fatal — it
> is fatal only under whichever failure handler happens to be installed at
> the moment, exactly as `tests/core/assert_tests.cpp` already
> demonstrates by installing a non-terminating replacement. This revision
> corrects the claim and adds a genuine, handler-independent fallback: the
> callback calls `ATLANTIS_CHECK_MSG` first (for full diagnostics/handler
> integration, unchanged), then, immediately after, an explicit
> `std::abort()` that runs regardless of whether the installed handler
> returned. See Section 6 for the full design, why this addition is
> **not** a change to `ATLANTIS_CHECK_MSG`/ADR-0009 itself, and an
> explicit flag for Human Review on whether this local reinforcement needs
> its own governance sign-off. The previous revision's
> `validation_failure_policy_tests.cpp` — which called the full callback
> directly and expected it to return after installing a replacement
> handler — no longer works once the callback can no longer be made to
> return on `WARNING`/`ERROR`; it is removed and replaced with pure-
> function unit tests plus an explicit, honest statement of what is only
> verified by code inspection (Section 8). None of this changes this
> Plan's `Status` — it remains `Draft`, and no Human Review has occurred.
>
> **Revision note (previous revision — structural Validation failure
> guarantee; `ValidationSink` removed):** the revision before that's
> `ValidationSink` design only converted a `WARNING`/`ERROR` into an
> observable, queryable failure when the caller explicitly attached one
> (`DeviceCreateParams::validationSink != nullptr`) — when it was `null`
> (the default), a message was only logged, never made to fail anything.
> This violated the required invariant that validation failure must be
> structural, not dependent on caller discipline. This revision replaces
> that entire design: the debug-messenger callback now calls Core's
> existing, `Accepted` `ATLANTIS_CHECK_MSG` (`ADR-0009`) directly and
> unconditionally on any `WARNING`/`ERROR`, failing the current process at
> the moment the message is observed — with no caller-supplied object
> involved at all. Re-evaluating `ValidationSink` against this new design
> (Section 6's Governance Review), this Plan concludes it is no longer
> necessary and **removes it entirely**: no `ValidationSink` type, no
> `DeviceCreateParams::validationSink` field, no atomic counter, no new
> borrowed-pointer ownership contract, no new thread-safety story. Net
> public-API change for validation enforcement, this round: **zero new
> symbols** — the only mechanism used is Core's already-public, already-
> `Accepted` `ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG`. None of this changes
> this Plan's `Status` — it remains `Draft`, and no Human Review has
> occurred.
>
> **Revision note (two revisions ago — CTest selection semantics and
> destruction-phase validation coverage):** corrected three further review
> findings: (1) the earlier Draft's two `catch_discover_tests(... TEST_SPEC
> ...)` calls on one executable did not make a bare `ctest`
> GPU-independent — CTest labels only filter when `-L`/`-LE` is passed
> explicitly; a bare `ctest` runs everything registered regardless of
> label. Replaced with a **separate GPU test executable**
> (`atlantis_vulkan_backend_gpu_tests`, entirely GPU-required, every case
> labeled `gpu`) plus the exact, unambiguous commands
> `ctest --test-dir <build> -LE gpu --output-on-failure` (GPU-independent)
> and `ctest --test-dir <build> -L gpu --output-on-failure` (GPU-required)
> — see Section 8, unchanged by this revision. (2) a free function that
> could only be queried while its referenced `Device` was still alive
> could never observe messages generated by that `Device`'s own
> destruction — addressed at the time by introducing `ValidationSink`;
> this revision addresses the same underlying requirement differently (see
> above), so `ValidationSink` itself is now removed rather than kept.
> (3) the callback's message counter was `std::atomic<unsigned int>` —
> also removed along with `ValidationSink`, see this revision's note above
> for why no replacement synchronization primitive is needed.
>
> **Revision note (three revisions ago):** corrected five earlier review
> findings against the original Draft: (1) `enableValidationLayers` could
> be set `false` by a caller even in a Debug build — structurally forced on
> in Debug, regardless of the caller-supplied value (unchanged by later
> revisions); (2) a validation warning/error was only logged, not turned
> into an automated test/demo failure — first addressed via an owned
> counter, since superseded twice (see above); (3) `Format`'s enumerators
> used `SCREAMING_SNAKE_CASE`, violating AGENTS.md's PascalCase-enumerator
> convention — renamed; (4) `createPresentation()` never checked
> presentation support against the concrete `VkSurfaceKHR` it just
> created, only the generic Win32 capability check performed at `Device`
> construction — added, without any new public queue/`VkQueue` accessor;
> (5) tightened language throughout so this Draft does not read as having
> already "resolved" or "finalized" any candidate C++ shape.

> **Scope banner — read before anything else.** This Plan implements
> exactly `Presentation`'s **non-frame** lifecycle: `Device`/`Presentation`
> construction, Windows WSI surface creation, swapchain creation/
> recreation/destruction, zero-extent handling, resize-driven recreation,
> and read-only swapchain metadata queries. It implements **none** of the
> following, on purpose, per Spec 0003's Non-Goals and
> [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
> "Deferred as One Bundle": any acquire/present-shaped API, `RenderTarget`,
> any synchronization primitive (semaphore/fence/command pool/command
> buffer/queue submission), any GPU command (image layout transition,
> clear, draw, pipeline), RenderGraph, Renderer, Shader System, the
> Atlantis Runtime module, general `Buffer`/`Texture` resource creation or
> any GPU memory suballocator (VMA or hand-rolled), Android/iOS
> implementation, Linux support, a second graphics backend, or a thread/
> job system. See **Section 9 (Explicit Prohibitions)** for a
> verification-checkable version of this list. This Plan's own Status is
> `Approved / Ready for Implementation`, following joint Spec 0003 + Plan
> 0003 Human Review on 2026-08-08 (see the Approval transition note at the
> top of this document) — the boundaries and prohibitions listed above
> remain fully in force for Implementation, unchanged by that approval.

> **How to read "Section N fixes/resolves X" below (post-approval).**
> Every C++ type, function signature, enumerator set, and file name in
> Sections 2, 3, 5, and 6 was offered as a **candidate shape** during
> Draft review; Human Review has now approved this Plan as written (see
> the Approval transition note at the top of this document), so
> Implementation follows these shapes as this Plan states them. Per
> [AGENTS.md](../AGENTS.md), if reality forces a deviation from an
> approved shape during Implementation, that deviation is called out
> explicitly in the PR rather than silently drifted from — a deviation
> that changes architecture means back to Spec review, not a silent edit
> here.

## Objective

Turn `specs/0003-rhi-vulkan-windowed-foundation.md` into an ordered,
reviewable set of concrete changes: a backend-agnostic **Atlantis RHI**
module (`Device`/`Presentation` abstract interfaces) and a Windows-only
**Atlantis Vulkan Backend** implementing their non-frame lifecycle —
construction, private WSI surface creation, swapchain (re)creation and
safe destruction, zero-extent handling, resize-driven recreation, and
read-only swapchain metadata queries — per
[ADR-0001](../adr/0001-rhi-backend-independence.md),
[ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
[ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
[ADR-0004](../adr/0004-phase1-threading-baseline.md),
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended),
[ADR-0011](../adr/0011-native-window-handle-representation.md),
[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md),
[ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md), and
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).

## Authoritative Sources

Treated as authoritative, not reinterpreted:
`specs/0003-rhi-vulkan-windowed-foundation.md`,
`specs/0002-platform-foundation.md`, `specs/0001-project-foundation.md`,
`adr/0001` through `adr/0005` (0005 amended), `adr/0009`, `adr/0011`,
`adr/0013` through `adr/0016`, `adr/0006` (dependency-management category
for the Vulkan SDK), `adr/0010` (CMake/namespace convention),
`docs/architecture/module_boundaries.md`,
`docs/architecture/threading.md`,
`docs/architecture/resource_lifetime.md`,
`docs/architecture/platform-vulkan-wsi-boundary.md`,
`docs/process/testing-strategy.md`,
`docs/process/definition-of-done.md`, `AGENTS.md`,
`plans/0001-project-foundation.md`, `plans/0002-platform-foundation.md`
(structural precedent for how this Plan proposes Plan-stage
implementation details without treating them as new architecture),
`src/platform/CMakeLists.txt` (read directly, in an earlier revision, to
ground Section 1's CMake dependency claims in the actual current target
declaration rather than restating it from memory),
`src/core/include/atlantis/assert.h` and `adr/0009-assertion.md` (read
directly this revision, to ground Section 6's `ATLANTIS_CHECK_MSG`-based
design in the actual current, `Accepted` assertion mechanism rather than
inventing one).

## Critical Architectural Boundaries (preserved, not re-decided here)

```
Atlantis Platform (existing, Windows-implemented)
  -- SurfaceCreated{ NativeWindowHandle } -->
Runtime-equivalent composition (this plan's verification demo;
NOT the future Runtime module)
  -- NativeWindowHandle, by value, uninterpreted -->
Vulkan Backend's construction API (ADR-0014)
  -- consumes NativeWindowHandle only here, inside its private WSI
     boundary (ADR-0005 amended) -->
  VkSurfaceKHR (private to Vulkan Backend)
  -->
RHI's public Device / Presentation interfaces
  (backend-agnostic; zero Vk*, zero OS types, zero NativeWindowHandle;
  no RenderTarget vended anywhere in this plan)
```

- RHI's public headers: zero `Vk*` types, zero OS-specific types, zero
  `NativeWindowHandle`. Only `Atlantis::Core` is a dependency.
- `NativeWindowHandle` crosses exactly one boundary beyond
  Platform/Runtime: into Vulkan Backend's construction-API header
  ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)).
  It is consumed only inside Vulkan Backend's private WSI boundary — never
  by generic RHI's `Presentation` interface itself
  ([ADR-0011](../adr/0011-native-window-handle-representation.md)).
- Only Vulkan Backend may include Vulkan headers or reference `Vk*` types
  ([ADR-0001](../adr/0001-rhi-backend-independence.md)).
- Vulkan Backend's private WSI boundary may include `vulkan_win32.h` and
  `<windows.h>` strictly to build a `VkSurfaceKHR` from a borrowed
  `NativeWindowHandle`; it never destroys the native window
  ([ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) amended,
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)).
- `Device` has no window/surface knowledge. `Presentation` is constructed
  from a `Device&` and a `NativeWindowHandle` via Vulkan Backend's factory
  functions; construction creates only the `VkSurfaceKHR`, never a
  swapchain
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)).
- `notifyResized()`/`recreateIfNeeded()` are the only entry points that
  ever touch `VkSwapchainKHR`; a `{0, 0}` tracked extent structurally
  skips every Vulkan swapchain call, on the first call after construction
  and on every later call alike (ADR-0016).
- `Presentation` never acquires, vends, or tracks a swapchain image or a
  `RenderTarget`; destruction therefore has no outstanding-acquired-image
  precondition to satisfy (ADR-0016).
- Single logical frame thread: `Device`/`Presentation` construction,
  recreation, and destruction happen only on the thread that owns the
  Windows Platform message pump
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md),
  `specs/0002-platform-foundation.md` Threading).
- RHI resources are explicitly owned, RAII-only; `Presentation` owns its
  own swapchain; nothing is cached or pooled implicitly
  ([ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md)).
- No GPU memory suballocation strategy is introduced, assumed, or
  presumed by any interface shape
  ([ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)).

## Non-Goals (explicitly confirmed, matching Spec 0003)

This Plan does **not** propose implementing, sketching, or illustratively
pre-declaring: an acquire/present-shaped API of any kind; `RenderTarget`;
any semaphore/fence/command pool/command buffer/queue submission; any
image layout transition, clear, draw, or `VkPipeline` object; RenderGraph;
Renderer; Shader System; the Atlantis Runtime module (the verification
demo introduced below is explicitly not a preview of it); general
`Buffer`/`Texture` resource creation; a GPU memory suballocator (VMA or
hand-rolled); Android or iOS implementation; Linux support; a second
graphics backend; a thread/job system; or any speculative abstraction
beyond what Spec 0003's Requirements actually call for. See Section 9 for
the verification-checkable version of this list.

---

## 1. Module and CMake Target Boundaries

Two new modules, following [ADR-0010](../adr/0010-cmake-structure.md)'s
established `src/<module>/{include/atlantis/<module>/, src/}` →
`atlantis_<module>` → `Atlantis::<Module>` pattern exactly:

| Module | Directory | CMake target | Alias | Namespace | Depends on (link) |
|---|---|---|---|---|---|
| Atlantis RHI | `src/rhi/` | `atlantis_rhi` | `Atlantis::RHI` | `atlantis::rhi` | `Atlantis::Core` (PUBLIC) only |
| Atlantis Vulkan Backend | `src/vulkan_backend/` | `atlantis_vulkan_backend` | `Atlantis::VulkanBackend` | `atlantis::vulkan_backend` | `Atlantis::RHI` (PUBLIC), `Atlantis::Core` (PUBLIC), `Atlantis::Platform` (PUBLIC — see note below), `Vulkan::Vulkan` (PRIVATE) |

**Why `atlantis_vulkan_backend` links `Atlantis::Platform` (PUBLIC), and
why this does not reopen or contradict any Accepted ADR — grounded
against the actual current `src/platform/CMakeLists.txt` (read directly
in an earlier revision, not restated from memory):**

`src/platform/CMakeLists.txt` currently declares:
```cmake
add_library(atlantis_platform STATIC src/clock.cpp src/platform_event.cpp)
add_library(Atlantis::Platform ALIAS atlantis_platform)
target_include_directories(atlantis_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(atlantis_platform PUBLIC Atlantis::Core PRIVATE atlantis_compiler_warnings)
if(WIN32)
  target_sources(atlantis_platform PRIVATE src/windows/windows_platform.cpp)
endif()
```
Two facts follow directly from this, not from assumption: (a) linking
`Atlantis::Platform` gives a consumer exactly `src/platform/include/` —
five small headers (`platform_kind.h`, `native_window_handle.h`,
`platform_event.h`, `clock.h`, `platform.h`), none of which include an OS
SDK header, per [ADR-0011](../adr/0011-native-window-handle-representation.md)
— plus transitively `Atlantis::Core`; (b) `windows_platform.cpp` is added
via `target_sources(... PRIVATE ...)`, so it contributes no PUBLIC
include directory, compile definition, or link requirement a consumer of
`Atlantis::Platform` would inherit. Linking `Atlantis::Platform` PUBLIC
from `atlantis_vulkan_backend` therefore pulls in **exactly** those five
headers plus `Atlantis::Core` — nothing Win32-specific, nothing
event/lifecycle-shaped.

[ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)'s
own Decision fixes `createPresentation`'s signature as accepting
`atlantis::platform::NativeWindowHandle` by value. This means Vulkan
Backend's construction-API header must include
`atlantis/platform/native_window_handle.h` to name the parameter type,
making `Atlantis::Platform` a real CMake link dependency of
`atlantis_vulkan_backend` — scoped, per the CMake facts above, to one
trivially-copyable POD type, never to Platform's lifecycle/event-loop
behavior. **This is not "no dependency" — it is a real, narrow,
header-only, lifecycle-free dependency, and this Plan states it as such
rather than rounding it down to zero.** It is also not a new decision:
ADR-0014 already fixed this exact signature; this Plan implements it,
does not decide it.

`docs/architecture/module_boundaries.md`'s Vulkan Backend section still
reads "Does not depend on Atlantis Platform," but that document is
explicitly `PROPOSED`, predates ADR-0011/ADR-0014, and its own phrasing
("the same opaque native-surface handle RHI's Presentation interface
defines") is already inaccurate relative to Accepted ADR-0011, which
fixes `NativeWindowHandle` as Platform's type, never RHI's. Per
`AGENTS.md`'s documentation rule, an Accepted ADR is authoritative over a
`PROPOSED` architecture document. **This Plan flags that staleness and
does not resolve it** — editing `module_boundaries.md` is out of this
task's authorized scope (only this Plan file may be modified). **Resolved
at Human Review, 2026-08-08:** this staleness is confirmed to **not**
block Implementation under this Plan; `module_boundaries.md` is not
edited by this Plan or by its approval and is deferred to a separate,
future documentation-consistency task (see the Approval transition note
at the top of this document).

**What this dependency does not permit:** `atlantis_vulkan_backend` never
calls `atlantis::platform::initialize()`, `processEvents()`,
`shouldQuit()`, or `shutdown()`; never links against
`windows_platform.cpp`'s behavior; never reaches into Platform's
lifecycle state. The dependency is exhausted by one `#include` for one
struct definition.

### File-level layout (expected — not created by this Plan)

```
src/rhi/
  CMakeLists.txt
  include/atlantis/rhi/
    device.h          -- abstract Device
    presentation.h     -- abstract Presentation
    types.h             -- Extent2D, Format, SwapchainMetadata, PresentationError
  src/
    types.cpp           -- Extent2D::isZero()/operator==() (mirrors
                            Platform's WindowExtent precedent; kept as a
                            small STATIC library, not converted to an
                            INTERFACE library, for consistency with
                            atlantis_core/atlantis_platform's existing
                            CMake pattern)

src/vulkan_backend/
  CMakeLists.txt
  include/atlantis/vulkan_backend/
    vulkan_backend.h    -- createDevice/createPresentation,
                            DeviceCreateParams/DeviceCreateError/
                            PresentationCreateError. No diagnostics/
                            observer type -- see Section 6.
  src/
    vulkan_instance.h / .cpp     -- VkInstance creation, layer/extension
                                    enablement (calls validation.h's
                                    effectiveValidationLayersEnabled())
    vulkan_device.h / .cpp       -- concrete VulkanDevice: physical device +
                                    queue selection, VkDevice creation,
                                    installs the debug messenger (holds
                                    only the VkDebugUtilsMessengerEXT
                                    handle -- no validation *state*,
                                    Section 6)
    vulkan_presentation.h / .cpp -- concrete VulkanPresentation:
                                    surface-only construction (incl. the
                                    concrete-surface presentation-support
                                    check), recreateIfNeeded(),
                                    notifyResized(), metadata(), teardown
    vulkan_result.h / .cpp       -- VkResult -> RHI/Vulkan-Backend error
                                    mapping (GPU-independent, unit-testable)
    validation.h / .cpp          -- IsDebugBuild, effectiveValidationLayersEnabled(),
                                    isFatalValidationSeverity(),
                                    validationMessageOrFallback() (all
                                    pure, GPU-independent, unit-testable),
                                    and the debug-messenger callback --
                                    calls ATLANTIS_CHECK_MSG for full
                                    diagnostics/handler integration, then
                                    an explicit std::abort() fallback that
                                    runs regardless of what the installed
                                    handler did (Section 6); the callback
                                    itself is not unit-testable in the
                                    normal test process once this fallback
                                    exists -- verified by code inspection
                                    only (Section 8)
    wsi/
      win32_surface.h / .cpp     -- private WSI boundary; the only files
                                    in this module permitted to include
                                    <windows.h> / <vulkan/vulkan_win32.h>

tests/rhi/
  CMakeLists.txt
  types_tests.cpp        -- Extent2D/Format/SwapchainMetadata, no device

tests/vulkan_backend/
  CMakeLists.txt
  vulkan_result_tests.cpp             -- VkResult mapping, no device
  presentation_logic_tests.cpp        -- decideRecreateAction() and
                                          checkSurfaceSupported(), no device
  validation_policy_tests.cpp         -- effectiveValidationLayersEnabled(),
                                          isFatalValidationSeverity(), and
                                          validationMessageOrFallback() --
                                          three pure functions, no device,
                                          no real Vulkan call, no failure-
                                          handler installed (Section 8) --
                                          the ONE validation-policy test
                                          file; no second, overlapping
                                          "validation_failure_*"-named file
                                          exists
                                        -- all three files above build into
                                           the atlantis_vulkan_backend_tests
                                           executable (no CTest label --
                                           Section 8)
  vulkan_presentation_gpu_tests.cpp -- real Device+Presentation construction,
                                       resize/zero-extent/destruction, real
                                       window + real GPU; a real validation
                                       violation at any point aborts this
                                       process, which CTest reports as a
                                       failed test (Section 6) -- no
                                       explicit REQUIRE against any counter
                                       is written or needed
                                    -- builds into a SEPARATE executable,
                                       atlantis_vulkan_backend_gpu_tests,
                                       every case CTest-labeled "gpu" (via
                                       the CMake catch_discover_tests(...
                                       PROPERTIES LABELS "gpu") call on
                                       that target, not a Catch2 TEST_SPEC
                                       filter -- Section 8), run only via
                                       explicit `ctest -L gpu`, Windows-only,
                                       gated #if defined(_WIN32)

examples/rhi_vulkan_demo/
  CMakeLists.txt
  main.cpp                -- minimal, non-shipping composition; see Section 11
```

### Files to Modify (expected)

```
CMakeLists.txt         -- add_subdirectory for src/rhi, src/vulkan_backend,
                           tests/rhi, tests/vulkan_backend,
                           examples/rhi_vulkan_demo; find_package(Vulkan REQUIRED)
src/README.md          -- add src/rhi/ and src/vulkan_backend/ entries
tests/README.md        -- add tests/rhi/ and tests/vulkan_backend/ entries,
                           documenting the two tests/vulkan_backend/
                           executables and the exact `ctest -LE gpu` /
                           `ctest -L gpu` commands (Section 8)
examples/README.md     -- add rhi_vulkan_demo/ entry
README.md              -- note the Vulkan SDK as a required, pre-installed
                           external dependency (per ADR-0006's already-
                           established category) once this module builds
```

No file under `src/core`, `src/platform`, `examples/foundation_demo`,
`examples/platform_demo`, `tests/core`, or `tests/platform` is modified —
this Plan is purely additive. No `src/render_graph/`, `src/renderer/`, or
Shader System directory is created.

---

## 2. RHI Public Interface — Candidate Shapes

*(Approved as written at joint Spec 0003 + Plan 0003 Human Review,
2026-08-08 — see the Approval transition note at the top of this
document. Each shape below concretizes a requirement Spec 0003's
Functional Requirements already mandate in prose, without proposing a new
module boundary, ownership model, threading model, or dependency — the
same category `plans/0002-platform-foundation.md` treated as Plan-stage
detail for `WindowExtent`/`PlatformEvent`/`PlatformError`. See Section 7
for why none of these rose to a new-ADR-required decision.)

**`types.h`:**
```cpp
namespace atlantis::rhi {

struct Extent2D {
  unsigned int width = 0;
  unsigned int height = 0;
  [[nodiscard]] bool isZero() const { return width == 0 && height == 0; }
};
[[nodiscard]] bool operator==(const Extent2D&, const Extent2D&);

// Describes only the currently-selected swapchain surface format for
// this spec's read-only metadata query -- not a general resource-format
// system. A future Buffer/Texture spec is expected to introduce its own
// format concept, quite possibly superseding this enum's role rather
// than extending it in place. Enumerator names are PascalCase per
// AGENTS.md, spelling out the Vulkan format they name rather than
// reusing VK_FORMAT_*'s SCREAMING_SNAKE_CASE spelling verbatim.
enum class Format {
  Unknown,
  Bgra8Unorm,
  Bgra8Srgb,
  Rgba8Unorm,
  Rgba8Srgb,
};

struct SwapchainMetadata {
  unsigned int imageCount = 0;
  Format format = Format::Unknown;
  Extent2D extent;
};

enum class PresentationError {
  SurfaceLost,
  SwapchainCreationFailed,
  DeviceLost,
  Unknown,
};

}  // namespace atlantis::rhi
```

Every field/enumerator above is read directly from a Functional
Requirement Spec 0003 already states in prose (extent tracking, format/
image-count/extent metadata, `Result`-surfaced recoverable errors) — none
is a speculative addition. `Format`'s four named enumerators are the
formats `vkGetPhysicalDeviceSurfaceFormatsKHR` commonly returns for a
Win32 swapchain on consumer hardware; `Unknown` is the pre-first-
`recreateIfNeeded()`/failure default. `PresentationError`'s four
enumerators map directly to `recreateIfNeeded()`'s and `createPresentation()`'s
own documented failure modes (Section 5) — none is unused.

**`device.h`:**
```cpp
namespace atlantis::rhi {

// Represents a logical GPU device and its queues. Has no window/surface
// knowledge. Owned by whoever constructs it (this plan's verification
// demo; Runtime, once that module exists). Not internally thread-safe;
// construction/destruction happen on the single Phase 1 frame thread
// (ADR-0004). Intentionally declares no method beyond the destructor in
// this spec's scope -- nothing here submits GPU work or queries a queue
// directly; a future RenderGraph/CommandList spec extends this
// interface, not sketched here.
class Device {
 public:
  virtual ~Device() = default;
};

}  // namespace atlantis::rhi
```

**`presentation.h`:**
```cpp
#include <atlantis/result.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// The swapchain-backed drawable-surface abstraction (ADR-0002), scoped to
// its non-frame lifecycle only (ADR-0016). Constructed via Vulkan
// Backend's factory API (ADR-0014) from a Device and a NativeWindowHandle
// -- construction creates the VkSurfaceKHR only, never a swapchain. Owns
// its swapchain once one exists. Must be destroyed before the Device it
// was constructed from (caller-enforced; not tracked by this type -- see
// ADR-0003's explicit-ownership, no-hidden-refcounting model). Not
// internally thread-safe; every method here is caller-thread-only, the
// same single Phase 1 frame thread that owns the Windows Platform message
// pump (ADR-0004). Declares no acquire, present, or synchronization
// primitive of any kind -- see ADR-0016.
class Presentation {
 public:
  virtual ~Presentation() = default;

  // Updates the tracked extent and marks recreation needed. Makes no
  // Vulkan call.
  virtual void notifyResized(Extent2D extent) = 0;

  // The only operation that ever creates, recreates, or destroys the
  // backing VkSwapchainKHR. See ADR-0016 for the exact 4-step contract.
  // Issues zero Vulkan swapchain calls whenever the tracked extent is
  // {0, 0} -- true on the first call after construction and every later
  // call alike.
  [[nodiscard]] virtual atlantis::Result<void, PresentationError> recreateIfNeeded() = 0;

  // Reflects the most recently successfully (re)created swapchain. Never
  // hands out an image handle, a RenderTarget, or any per-image resource.
  [[nodiscard]] virtual SwapchainMetadata metadata() const = 0;
};

}  // namespace atlantis::rhi
```

---

## 3. Vulkan Backend Construction API — Candidate Shapes

Per [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
which itself states "exact names/signatures are a Plan-stage detail, not
fixed by this ADR" — the shapes below are this Plan's candidate proposal
against that latitude, not a restatement of something the ADR already
locked. This section carries **no diagnostics/observer type** — see
Section 6's Governance Review for why that was removed rather than kept
and re-justified.

**`vulkan_backend.h`:**
```cpp
#include <atlantis/platform/native_window_handle.h>
#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/presentation.h>

#include <memory>
#include <string>

namespace atlantis::vulkan_backend {

struct DeviceCreateParams {
  std::string applicationName = "Atlantis";

  // Requests validation layers be enabled even in a Release build.
  // Cannot be used to DISABLE validation layers in a Debug build --
  // Debug builds always enable them regardless of this field's value,
  // per AGENTS.md ("Validation Layers are always enabled in debug
  // builds") and Spec 0003's Testing & Verification Plan. The effective,
  // structurally-enforced value used internally is
  // `detail::effectiveValidationLayersEnabled(detail::IsDebugBuild,
  // enableValidationLayers)` -- see Section 6 -- never this field read
  // directly. Defaults to `false`; this plan's GPU tests and the
  // verification demo set it `true` explicitly regardless of build
  // configuration, as defense in depth beyond the Debug-forced guarantee.
  // Whenever validation layers end up enabled (by either path), ANY
  // WARNING/ERROR severity message unconditionally fails the current
  // process via Core's existing ATLANTIS_CHECK mechanism (Section 6) --
  // there is no way to have validation layers enabled and have a message
  // merely logged, and no separate field or object is needed to opt into
  // that guarantee.
  bool enableValidationLayers = false;
};

enum class DeviceCreateError {
  InstanceCreationFailed,
  ValidationLayerUnavailable,
  NoSuitablePhysicalDevice,
  DeviceCreationFailed,
};

[[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError>
createDevice(const DeviceCreateParams& params);

enum class PresentationCreateError {
  SurfaceCreationFailed,
  UnsupportedDevice,
};

// The only function anywhere in RHI/Vulkan Backend's public surface that
// accepts a NativeWindowHandle, per ADR-0011/ADR-0014. `device` must
// outlive the returned Presentation; passing a Device not produced by
// this module's own createDevice() is a programmer error (ATLANTIS_CHECK
// in the implementation), not a supported input -- impossible in Phase 1
// since no second backend exists. No third parameter is proposed: Spec
// 0003 states no presentation-creation preference (present mode, image
// count, ...) this module needs to accept, and this Plan does not invent
// an empty placeholder struct "for later" -- see Section 7 item 6 and
// AGENTS.md's no-speculative-abstraction principle. A future spec that
// needs one adds a parameter then.
[[nodiscard]] atlantis::Result<std::unique_ptr<atlantis::rhi::Presentation>, PresentationCreateError>
createPresentation(atlantis::rhi::Device& device,
                    atlantis::platform::NativeWindowHandle windowHandle);

}  // namespace atlantis::vulkan_backend
```

`vulkan_backend.h` itself includes no Vulkan header and references no
`Vk*` type — only Vulkan Backend's private `src/` implementation files do.

**Why `PresentationCreateParams` was removed (an earlier revision):** an
earlier Draft declared an empty struct "reserved for a future present-
mode/image-count preference." Nothing in Spec 0003 needs it, and
reserving an empty placeholder for a hypothetical future need is exactly
the speculative-abstraction pattern AGENTS.md warns against. ADR-0014
itself does not fix the parameter list, so dropping it is within this
Plan's latitude, not a deviation from anything Accepted. If a future spec
needs a presentation-creation preference, it adds a parameter to
`createPresentation()` then — a source-compatible, additive change to a
factory function, not an ABI a Phase-1 empty struct was protecting.

**Why no `ValidationSink`-shaped type appears here (this revision):** an
earlier revision introduced `validationMessageCount(const rhi::Device&)`,
found it could not observe `Device`'s own destruction, and replaced it
with a caller-owned `ValidationSink` (a new public type, a new borrowed-
pointer ownership contract, an atomic counter, a new thread-safety story)
specifically so a caller could query "did anything go wrong" after the
fact. This revision removes the need for that entirely: the debug-
messenger callback now fails the process **unconditionally and
immediately** via `ATLANTIS_CHECK_MSG` (Section 6) the moment a
`WARNING`/`ERROR` occurs — there is nothing left to query afterward,
because a successful return already means nothing went wrong. See
Section 6's Governance Review for the full reasoning on why keeping
`ValidationSink` as an *additional*, optional diagnostics layer was
rejected too, not only as the sole failure mechanism.

---

## 4. Windows Private WSI Boundary

`src/vulkan_backend/src/wsi/win32_surface.h`/`.cpp` — the only files in
the repository, besides `src/platform/src/windows/windows_platform.cpp`
and its Windows smoke-test file, permitted to include `<windows.h>`; also
the only files permitted to include `<vulkan/vulkan_win32.h>`. Per
[docs/architecture/platform-vulkan-wsi-boundary.md](../docs/architecture/platform-vulkan-wsi-boundary.md)'s
"Vulkan WSI Responsibilities," proposed to do exactly, and only:

1. Receive the opaque `NativeWindowHandle` (borrowed, non-owning) from
   `createPresentation()`.
2. Confirm `kind == PlatformKind::Windows` (`ATLANTIS_CHECK` — any other
   `kind` reaching this file is a programmer error, since only Windows is
   implemented).
3. Reinterpret `value0`/`value1` back to `HWND`/`HINSTANCE`.
4. Call `vkCreateWin32SurfaceKHR` to produce a `VkSurfaceKHR`; check its
   `VkResult` via the shared checked-call helper (`vulkan_result.h`).
5. Nothing else — no window creation, resize, destruction, or event-loop
   code. Never calls `DestroyWindow` or any window-destroying operation
   (ADR-0013).

This boundary's output (a raw `VkSurfaceKHR`) is handed back to
`createPresentation()`'s implementation, which then performs the
concrete-surface presentation-support check described in Section 5 —
this file itself does not perform that check, keeping the WSI boundary's
responsibility exactly as narrow as
`platform-vulkan-wsi-boundary.md` already scopes it.

Android's equivalent (`wsi/android_surface.*`) is **not created** — Spec
0003 and this Plan implement Windows only.

---

## 5. Presentation Lifecycle — Construction, Recreation, Destruction

This section implements [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
already-fixed contract; nothing about the *contract* here is a new
decision. The concrete function names/signatures used to implement it are
this Plan's candidate proposal (Section 3's caveat applies).

### Construction, including the concrete-surface presentation-support check

`createPresentation()`'s proposed implementation sequence:

1. Call the WSI boundary (Section 4) to produce a `VkSurfaceKHR` from the
   borrowed `NativeWindowHandle`. Check its `VkResult`; on failure, return
   `Err(PresentationCreateError::SurfaceCreationFailed)` — no further
   steps.
2. **Concrete-surface presentation-support check.** `Device` construction
   (Section 7's queue-selection disposition) only confirms, via
   `vkGetPhysicalDeviceWin32PresentationSupportKHR`, that the chosen
   queue family *can generically* present to a Win32 window — that call
   needs no `VkSurfaceKHR` and cannot need one, since `Device` is
   constructed before any `Presentation`/surface exists. Vulkan's own
   specification additionally requires confirming that the *specific*
   surface just created is supported by that exact queue family, via
   `vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex,
   surface, &supported)` — a call that can only happen here, once a real
   `VkSurfaceKHR` exists. `createPresentation()` calls it immediately
   after step 1, checks its own `VkResult` (this function can itself
   fail, independent of the boolean it writes), and dispatches through a
   small, pure, directly-unit-testable function:

   ```cpp
   namespace atlantis::vulkan_backend::detail {

   [[nodiscard]] std::optional<PresentationCreateError> checkSurfaceSupported(bool supported) {
     if (!supported) return PresentationCreateError::UnsupportedDevice;
     return std::nullopt;
   }

   }  // namespace atlantis::vulkan_backend::detail
   ```

   If `checkSurfaceSupported()` returns an error, `createPresentation()`
   destroys the just-created `VkSurfaceKHR` via `vkDestroySurfaceKHR`
   (safe — no swapchain was ever created from it, no other object depends
   on it yet) and returns `Err(UnsupportedDevice)` — an existing
   enumerator (Section 3), not a new one added for this check. **No new
   public queue accessor, `VkQueue`, or queue-family index is exposed
   anywhere** — the physical device handle and queue-family index used
   here are read from the concrete `VulkanDevice`'s own private state
   (the same object `Device&` was downcast from to build the surface in
   step 1), never surfaced through RHI's or Vulkan Backend's public
   interface.
3. Construct the concrete `VulkanPresentation` in a "recreation needed"
   state. **Does not create a swapchain.**

### `notifyResized(Extent2D extent)`

`trackedExtent_ = extent; recreationNeeded_ = true;`. No Vulkan call,
ever.

### `recreateIfNeeded()`

Implemented by first calling a second small, pure, directly-unit-testable
dispatch function (no `VkDevice` involved):

```cpp
namespace atlantis::vulkan_backend::detail {

enum class RecreateAction { Skip, NoOp, Recreate };

[[nodiscard]] RecreateAction decideRecreateAction(atlantis::rhi::Extent2D trackedExtent,
                                                   bool recreationNeeded) {
  if (trackedExtent.isZero()) return RecreateAction::Skip;
  if (!recreationNeeded) return RecreateAction::NoOp;
  return RecreateAction::Recreate;
}

}  // namespace atlantis::vulkan_backend::detail
```

`VulkanPresentation::recreateIfNeeded()` calls `decideRecreateAction()`
first and switches on the result: `Skip` returns immediately with **no
Vulkan call issued anywhere on that path** — this is the exact structural
property Spec 0003's Acceptance Criteria require to be "verifiable by
code inspection," satisfied by construction (the Vulkan-calling branch is
physically unreachable from `Skip`), not by convention. `NoOp` returns
immediately, no Vulkan call. `Recreate` destroys the previous
`VkSwapchainKHR` if one exists, creates a new one at `trackedExtent_` via
`vkCreateSwapchainKHR`, checks every `VkResult`, queries and caches image
count/format/extent via `vkGetSwapchainImagesKHR`/the creation-time
`VkSwapchainCreateInfoKHR`, clears `recreationNeeded_` on success, and
leaves it set (for retry) on failure — mapping any failing `VkResult` to
`PresentationError` via the shared mapping helper (`vulkan_result.h`),
never discarding it.

### Metadata queries, destruction, zero-extent retention, Android

- **Metadata queries**: `metadata()` returns the cached
  `SwapchainMetadata` from the most recent successful `Recreate` branch;
  read-only, no per-image handle ever included.
- **Destruction**: destroys the swapchain (if any) then the
  `VkSurfaceKHR`, in that order, per
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)'s
  dependency-order rule. No acquired-image precondition exists, because
  none is ever acquired under this contract.
- **Zero-extent swapchain retention**: a swapchain created at a prior
  non-zero extent is **left untouched** when extent later becomes zero —
  already decided by ADR-0016's Decision (step 1) and its Alternatives
  Considered (explicitly rejecting eager release "for now"). This Plan
  proposes implementing that as-is; it is not a new decision to make.
- **Android surface destruction**: out of `Presentation`'s own API,
  unchanged from ADR-0016 — not implemented or tested here, since Android
  isn't implemented.

---

## 6. Validation Layer Enforcement — Structural Guarantee and Failure Propagation

This section is substantially revised this round: it replaces the
previous revision's caller-owned `ValidationSink`/atomic-counter design
with an unconditional, fail-fast mechanism that does not depend on any
caller-supplied object at all. All names below are candidate shapes
(Section 3's caveat applies).

### The invariant this section exists to guarantee

> Whenever Vulkan Validation Layers are enabled, any `WARNING` or `ERROR`
> severity message must structurally fail the current build/test/run —
> unconditionally, regardless of whether the caller supplied any
> diagnostics/observer object.

The previous revision's design violated this: when
`DeviceCreateParams::validationSink` was `nullptr` (the default), a
`WARNING`/`ERROR` was only logged, never converted into a failure — the
guarantee depended on caller discipline (remembering to attach a sink and
later read it), which is exactly what this invariant forbids. This
revision closes that gap by removing the optional-observer design
entirely, not by patching around it (e.g. not by making the sink
mandatory, which would still be a caller-suppliable, caller-forgettable
object).

### Debug builds cannot disable validation layers (unchanged)

`validation.h` (private):
```cpp
namespace atlantis::vulkan_backend::detail {

#ifndef NDEBUG
inline constexpr bool IsDebugBuild = true;
#else
inline constexpr bool IsDebugBuild = false;
#endif

// The single place that turns a caller-requested value into the value
// Vulkan Backend actually uses. Parameterized on `isDebugBuild` (rather
// than reading IsDebugBuild internally) specifically so both branches --
// including the Debug-forces-true branch -- are exercised by a
// GPU-independent unit test without needing two separate build
// configurations (see Section 8).
[[nodiscard]] constexpr bool effectiveValidationLayersEnabled(bool isDebugBuild, bool requested) {
  return isDebugBuild || requested;
}

}  // namespace atlantis::vulkan_backend::detail
```
`vulkan_instance.cpp`'s instance-creation path calls
`detail::effectiveValidationLayersEnabled(detail::IsDebugBuild,
params.enableValidationLayers)` **exactly once**, and only that return
value — never `params.enableValidationLayers` read anywhere else —
decides whether `VK_LAYER_KHRONOS_validation` is requested at
`vkCreateInstance` and whether the debug messenger (below) is installed.
Unchanged from the previous revision.

### Validation-layer configuration

`VK_LAYER_KHRONOS_validation` instance layer; `VK_EXT_debug_utils` for a
debug messenger; severity filter `WARNING`+`ERROR` (message types
general/validation/performance). If validation was requested but
`VK_LAYER_KHRONOS_validation` is not available on this machine,
`createDevice()` returns `Err(DeviceCreateError::ValidationLayerUnavailable)`
— it does not silently continue without validation, which would defeat
the guarantee above before it could even start.

**Two messenger installations, same callback, no `pUserData` (revised
this round):** when validation is enabled, `createDevice()`'s
implementation installs two things, both using the exact same callback
function, both passing `pUserData = nullptr` (the callback needs no
per-instance state under this round's design — see "Why This Needs No
Observer Type At All," below):
1. A `VkDebugUtilsMessengerCreateInfoEXT` chained into
   `VkInstanceCreateInfo::pNext` at `vkCreateInstance` time — see
   "Precisely What the `pNext`-Chained Messenger Covers," below.
2. An ordinary, explicitly-created `VkDebugUtilsMessengerEXT` (via
   `vkCreateDebugUtilsMessengerEXT`, right after `vkCreateInstance`
   succeeds), covering everything else — physical/logical device
   creation, every `Presentation` operation built on this `Device`
   (instance-scoped, so no extra wiring is needed in
   `createPresentation()`), and `vkDestroyDevice` itself.

### What `ATLANTIS_CHECK_MSG` actually guarantees — corrected this round

[AGENTS.md](../AGENTS.md)'s Error Handling section draws a sharp line:
"Programmer errors are assertions, not error returns... Recoverable
runtime errors use explicit result/error types." A Vulkan Validation
Layer `WARNING`/`ERROR` is, by definition, evidence that **Atlantis's own
Vulkan Backend code misused the Vulkan API** — a violated usage invariant,
not an external/environmental condition like "device lost" or "surface
lost" (both of which correctly go through `Result`/`Err` elsewhere in
this Plan). AGENTS.md's own Vulkan-specific rules confirm this
categorization directly: "Vulkan Validation Layer output is treated as an
error, not advisory logging." This places a validation message squarely
in the **assertion** category this codebase already has a mechanism for
— [ADR-0009](../adr/0009-assertion.md)'s `ATLANTIS_CHECK`/
`ATLANTIS_CHECK_MSG`.

**The previous revision overstated what that mechanism guarantees on its
own.** Re-reading `src/core/include/atlantis/assert.h` and
`src/core/src/assert.cpp` directly, this round, shows precisely:
- `using AssertFailureHandler = std::function<void(const AssertFailureInfo&)>;`
  — returns `void`, **not** `[[noreturn]]`.
- `setFailureHandler()` accepts any such handler and installs it with no
  restriction on whether it terminates.
- `reportFailure()` — what `ATLANTIS_CHECK_MSG` actually calls on
  failure — copies the currently-installed handler and calls it, then
  itself returns normally. Nothing after that call is `[[noreturn]]`
  either.
- The **default** handler (`defaultFailureHandler` in `assert.cpp`) does
  call `std::abort()` — but it is only the default, and is exactly what
  `setFailureHandler()` exists to replace.
- `tests/core/assert_tests.cpp` already demonstrates this concretely: it
  installs a replacement handler that *records* the failure instead of
  terminating, specifically so the test process survives to make
  assertions about it.

**Conclusion, stated precisely:** `ATLANTIS_CHECK_MSG(false, message)` on
its own guarantees that `reportFailure()` is called with full diagnostic
information (condition text, message, source location) and that whichever
handler is currently installed runs — it does **not**, by itself,
guarantee the process terminates, because the currently-installed handler
is caller-replaceable and permitted to return. This Plan does not repeat
the previous revision's claim that calling `ATLANTIS_CHECK_MSG`
unconditionally is itself a "structural" or "unconditional" fatal
guarantee — it is not, on its own.

### Building a genuine, handler-independent fatal path

The invariant this section exists to guarantee (above) still needs to
hold regardless of which `AssertFailureHandler` happens to be installed
when a Vulkan Validation message arrives — including, hypothetically, one
some other test file installed and forgot to restore (Section 8 confirms
this Plan's own tests never do that, but the guarantee should not depend
on that discipline either). The callback therefore adds an explicit,
unconditional fallback **after** `ATLANTIS_CHECK_MSG`:

```cpp
// validation.cpp (private)
namespace atlantis::vulkan_backend::detail {

[[nodiscard]] bool isFatalValidationSeverity(VkDebugUtilsMessageSeverityFlagBitsEXT severity) noexcept {
  return severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ||
         severity == VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
}

// Never dereferences a null callbackData or a null pMessage -- both are
// defensive, precautionary checks (the Vulkan specification does not
// document either as nullable for a conformant loader/layer, but this
// Plan does not assume every loader/layer implementation is fully
// conformant either).
[[nodiscard]] const char* validationMessageOrFallback(
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData) noexcept {
  if (callbackData == nullptr || callbackData->pMessage == nullptr) {
    return "Vulkan Validation Layers reported a WARNING/ERROR with no message text available";
  }
  return callbackData->pMessage;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/) noexcept {
  if (isFatalValidationSeverity(severity)) {
    const char* message = validationMessageOrFallback(callbackData);
    // Reports through Core's existing, Accepted diagnostics/failure-
    // handler infrastructure (ADR-0009) -- ATLANTIS_LOG_FATAL logging,
    // debugger-break integration, and whatever handler is currently
    // installed all run here, unmodified. This call is NOT, on its own,
    // guaranteed to terminate -- see "What ATLANTIS_CHECK_MSG actually
    // guarantees," above.
    ATLANTIS_CHECK_MSG(false, message);
    // Structural fallback -- this Plan's own addition, not part of
    // ATLANTIS_CHECK_MSG's own contract, and not a change to ADR-0009.
    // reportFailure() is not [[noreturn]], and any AssertFailureHandler
    // is permitted to return (src/core/src/assert.cpp); the DEFAULT
    // handler already calls std::abort() internally, so in the ordinary
    // case this line is unreachable -- but if the currently-installed
    // handler returns instead of terminating, this call still guarantees
    // the process ends here. Not std::unreachable(): a handler returning
    // is legal per the existing API, not undefined behavior.
    std::abort();
  }
  return VK_FALSE;  // per the Vulkan spec: always VK_FALSE outside layer self-tests
}

}  // namespace atlantis::vulkan_backend::detail
```

**Why not return `VK_TRUE` instead:** the Vulkan specification lets a
messenger callback return `VK_TRUE` to abort the *triggering Vulkan call*
with `VK_ERROR_VALIDATION_FAILED_EXT` — but only for calls that actually
*return* a `VkResult` in the first place. Several calls this Plan's
teardown path makes do not: `vkDestroyDevice`, `vkDestroyInstance`,
`vkDestroyDebugUtilsMessengerEXT`, `vkDestroySwapchainKHR`, and
`vkDestroySurfaceKHR` are all `void`-returning. A `VK_TRUE`-based design
therefore could not possibly convert a destruction-phase violation into
any checked result — there is nothing to check. The explicit
`std::abort()` fallback has no such gap: it terminates the process
directly, independent of whether the triggering call has a return value
at all. This Plan does not claim `VK_TRUE` achieves the invariant stated
above, and does not use it for that purpose — the callback still returns
`VK_FALSE` unconditionally, per the Vulkan specification's own
recommendation outside layer self-tests.

**Why not `std::unreachable()` after `ATLANTIS_CHECK_MSG`:** that
intrinsic tells the compiler a code path can never be reached, which
would be an outright lie here — a replacement `AssertFailureHandler`
returning normally is legal, existing, documented behavior of this
codebase's own API (`assert.h`'s own comments impose no such
restriction), not undefined behavior this Plan can assume away. The
explicit `std::abort()` is a real, executed statement precisely because
reaching it is a real, possible outcome, not a violated precondition.

**Avoiding duplicate logging:** the callback does not additionally call
`ATLANTIS_LOG_WARN`/`ATLANTIS_LOG_ERROR` before `ATLANTIS_CHECK_MSG` (an
earlier revision did). `ATLANTIS_CHECK_MSG`'s failure path — via
`reportFailure()` — already logs through `ATLANTIS_LOG_FATAL` with the
condition text, the message, and source location, regardless of which
handler ultimately runs (the default handler logs before aborting; a
replacement handler receives the same `AssertFailureInfo` and may log
however it chooses). A second, separate log call immediately before it
would duplicate the same message at a different severity level for no
diagnostic benefit — one unified failure-reporting path is used instead.

**Why this needs no observer type at all:** because failure is enforced
*inside* the callback, unconditionally (via the explicit fallback, not
`ATLANTIS_CHECK_MSG` alone), the success path still means "no
`WARNING`/`ERROR` occurred" by construction — there is nothing left to
query afterward. This is what makes the earlier `ValidationSink` (a
caller-owned counter, a new public type, a new borrowed-pointer ownership
contract, an atomic, a documented thread-safety story) unnecessary — see
"Governance Review," below.

### Resolved at Human Review: the explicit `std::abort()` fallback does not require a new ADR

**Resolved at Human Review, 2026-08-08:** the reasoning below was
reviewed and **confirmed**, not overruled — the `std::abort()` fallback
is a private implementation strategy fulfilling Spec 0003's
Validation-failure requirement, not a new public assertion mechanism, and
does not require a new or amended ADR. See Consistency Review item 17 for
the closed record. The reasoning that was flagged for, and is now
confirmed by, Human Review:

This Plan's own assessment, reviewed by Human Review rather than left
unsettled: the fallback does **not** modify
`ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG`'s own macro definitions, does not
touch `src/core/include/atlantis/assert.h` or `src/core/src/assert.cpp`
at all, does not introduce a second general-purpose macro other code is
expected to reuse, and is not exposed anywhere outside one private
translation unit (`validation.cpp`). It reads as a **local reinforcement**
of one specific invariant this call site owns (Vulkan Validation
cleanliness — a rule AGENTS.md already singles out by name, distinct from
ordinary programmer-error assertions elsewhere in the codebase), not a
new general assertion tier. On that reasoning, this Plan does not treat
it as requiring its own ADR. **This reasoning was explicitly flagged for
Human Review, not assumed settled — and Human Review has now confirmed
it, 2026-08-08:** ADR-0009 is silent on whether an individual call site
may add its own unconditional-termination fallback after
`ATLANTIS_CHECK_MSG`; a reviewer could reasonably have read that silence
either way — as "not forbidden, therefore fine" or as "an un-reviewed
precedent that itself deserves an ADR amendment before landing." Human
Review read it the former way for this narrow, private, single-call-site
reinforcement and confirmed no new or amended ADR is required. No open
governance item remains on this point; see Consistency Review item 17 for
the closed record.

### Precisely What the `pNext`-Chained Messenger Covers

Per `VK_EXT_debug_utils`'s documented purpose for chaining a
`VkDebugUtilsMessengerCreateInfoEXT` into `VkInstanceCreateInfo::pNext`:
this creates an implicit messenger used specifically for messages
generated during the `vkCreateInstance` call itself and, later, during
that same instance's eventual `vkDestroyInstance` call — the two calls
for which no ordinarily-created `VkDebugUtilsMessengerEXT` object can
exist (not yet, at `vkCreateInstance` time; not anymore, once the
explicit messenger has already been destroyed, at `vkDestroyInstance`
time).

**What is, and is not, claimed about lifetime here, stated precisely
rather than left vague:** per Vulkan's general convention for structures
passed as input to a creation call, the `VkInstanceCreateInfo`/
`VkDebugUtilsMessengerCreateInfoEXT` structures themselves are consumed
synchronously during the `vkCreateInstance` call and need not remain
valid (e.g. as a still-live local variable) after that call returns —
this Plan does not claim the Vulkan loader retains a live pointer into
`vulkan_instance.cpp`'s own stack-allocated struct. What the instance
implementation retains internally, for its own later use during
`vkDestroyInstance`, is the callback **function pointer** and the
**`pUserData` value** those structures named — not the structures
themselves. Under this round's design, `pUserData` is always `nullptr`
(the callback needs no per-instance state at all, per the design above),
so there is no caller-owned object whose lifetime must be tracked here —
this eliminates the exact lifetime question the previous revision's
`ValidationSink` had to answer, rather than answering it differently.

The **explicit** messenger's own `pUserData` is likewise always `nullptr`
for the same reason; it covers every Vulkan call made against this
instance's objects between its own creation and its own destruction
(`vkDestroyDebugUtilsMessengerEXT`) — device creation, every `Presentation`
operation, `vkDestroyDevice`.

**This Plan's understanding above is drawn from `VK_EXT_debug_utils`'s
documented purpose and Vulkan's general input-structure convention; the
exact specification wording should be re-confirmed against the Vulkan
specification text during implementation, not taken as verified merely by
this Plan's own restatement.**

### Debug Messenger Destruction Boundary — exactly what is and isn't covered

`VulkanDevice::~VulkanDevice()`'s proposed teardown order, and which
mechanism (of the two installed above) covers each step — "covered" now
means "a violation here invokes `ATLANTIS_CHECK_MSG`, failing the process
immediately, before the triggering call's teardown step even completes":

| Step | Call | Covered by |
|---|---|---|
| 0 | (already complete before `~VulkanDevice()` runs) every `Presentation` built from this `Device` has already been destroyed by the caller — swapchain + surface teardown | The explicit messenger (still alive; instance-scoped, so it sees `Presentation`-originated calls too) |
| 1 | `vkDestroyDevice(device_, nullptr)` | The explicit messenger (still alive) |
| 2 | `vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr)` — tears down the explicit messenger itself | **Not covered by either mechanism.** Validating the validator's own teardown call is not a meaningful correctness signal for Spec 0003's actual scope, and the Vulkan spec offers no messenger whose scope includes debug-utils object teardown calls themselves. Not claimed as covered anywhere in this Plan — unchanged across revisions; this gap is orthogonal to how a covered violation is turned into a failure. |
| 3 | `vkDestroyInstance(instance_, nullptr)` | The `pNext`-chained instance-creation/destruction messenger (still valid for exactly this call — this is precisely the mechanism that exists to cover it, since by this point the explicit messenger from step 2 is already gone) |

Because every covered step now fails the process **immediately** rather
than merely being observable afterward via a query, this table's
guarantee is stronger than the previous revision's design could offer —
the same honest limitation at step 2 remains, restated rather than
hidden.

### Governance Review: Was `ValidationSink` a New Public API/Ownership Contract?

Per AGENTS.md's Golden Rule, and per the explicit instruction that "it's
not RHI's API" is not sufficient justification on its own: **yes,
unambiguously.** `ValidationSink` was a new public type, a borrowed
pointer from caller to Vulkan Backend, a forced
`Sink outlives Device outlives Presentation` lifetime ordering, a new
documented thread-safety contract, and a new diagnostics-shaped public
surface — every one of AGENTS.md's "what counts as significant" criteria
("introduces or changes a public API, module boundary, or subsystem") on
its own terms, not merely because it happened to live in
`vulkan_backend.h` rather than an RHI header. [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)'s
"exact names/signatures are a Plan-stage detail" sentence licenses this
Plan to choose `createDevice()`/`createPresentation()`'s own parameter
names and error-enum values — it does not, on its own, authorize
inventing an entirely new public type with its own ownership contract;
treating it as sufficient authorization was this Plan's own
overextension in the previous revision, not something ADR-0014's text
actually says.

**Resolution adopted this round: `ValidationSink` is removed, not kept
and re-justified.** The reason is not merely "avoid the governance
question" but that the fail-fast redesign above makes the type's original
purpose (converting an otherwise-silent callback into an observable,
queryable failure) unnecessary — `ATLANTIS_CHECK_MSG` already IS the
failure, at the moment it happens, using a mechanism
([ADR-0009](../adr/0009-assertion.md)) already `Accepted` and already
public (`atlantis::assertions`, `ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG` in
`src/core/include/atlantis/assert.h`) — reused, not newly introduced. No
new public symbol, no new ownership contract, and no new thread-safety
story is added anywhere in Vulkan Backend's public surface for validation
enforcement; see the Public API Surface Audit, below.

Because this Plan resolves the governance question by **removing** the
type rather than by finding it a clean authorization, **there is no open
governance blocker to escalate to Human Review/ADR amendment on this
point** — the alternative (keeping `ValidationSink`) would have been the
one requiring that escalation, precisely because no existing ADR
authorizes it, and this Plan does not keep it.

### Thread-Safety — no new contract to state

This round removes the only new mutable state the previous revision
introduced (`ValidationSink::count_`), so there is no new thread-safety
abstraction to justify, document, or defend, and no atomic or mutex is
introduced anywhere in this Plan for validation handling. The callback's
call into `ATLANTIS_CHECK_MSG` is not different in kind from every other
`ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG` call site already present in
`src/core`/`src/platform` — it relies on whatever thread-safety story
[ADR-0009](../adr/0009-assertion.md) and
`src/core/include/atlantis/assert.h` already establish for
`atlantis::assertions::reportFailure`/`setFailureHandler` (that header
documents `setFailureHandler` as **"Thread-safe"** directly, in its own
comment), not something this Plan re-derives or extends. "No consumer of
an atomic" is the direct, intended consequence of the fail-fast
redesign — not an oversight, and not a case of "keeping a thread-safety
abstraction with nothing left using it."

### Turning a validation message into an automated failure — now genuinely unconditional

- **GPU tests** (Section 8): no per-step `REQUIRE` against any counter is
  written or needed — if any Vulkan-touching operation in a test case
  (construction, `recreateIfNeeded()`, `Presentation` destruction,
  `Device` destruction) produces a `WARNING`/`ERROR`, the callback's
  `ATLANTIS_CHECK_MSG` reports it through Core's full diagnostic path,
  and the explicit `std::abort()` fallback that follows it terminates the
  **entire test process** regardless of which handler was installed
  (Section 8 confirms the GPU test executable never installs a
  replacement handler — the default, log-then-abort one is in effect
  throughout — but the guarantee does not depend on that fact, only on
  the fallback). CTest reports an abnormally-terminated test executable
  as a failed test. A test case reaching its own final assertions
  (concerning ordinary state like `metadata()`'s fields, unrelated to
  validation) and returning normally is therefore only possible if every
  Vulkan call up to that point was validation-clean — the test case
  **completing** is the validation-clean signal, not a separate assertion
  layered on top of it.
- **Verification demo** (Section 11): identically — the demo runs to
  completion and exits `0`, and also never installs a replacement failure
  handler. There is no longer a distinguishing "clean run" exit code to
  compute after the fact: if a `WARNING`/`ERROR` had occurred at any point
  (construction, resize-driven recreation, interactive use, or final
  teardown), the callback's explicit fallback would have already
  terminated the process before reaching `return 0;`. `main()` constructs
  or reads nothing validation-specific.
- **Future GPU CI:** the same guarantee applies with no special wiring —
  a GPU-touching CI job running `ctest -L gpu` observes a validation
  violation as a crashed/failed test process, exactly like a local run,
  because the mechanism does not distinguish between interactive,
  automated-test, or CI invocation, and does not depend on any CI-specific
  handler configuration.

### Process-level failure-handler isolation

- `atlantis_vulkan_backend_gpu_tests` (the GPU test executable) and
  `examples/rhi_vulkan_demo` (the demo) **never call
  `atlantis::assertions::setFailureHandler(...)`** — neither installs a
  replacement handler, so the default (log, then `std::abort()`) handler
  is in effect for the entirety of both, for every `ATLANTIS_CHECK`/
  `ATLANTIS_CHECK_MSG` call site they exercise, Vulkan-related or not.
  This is existing, unchanged behavior — not new machinery introduced by
  this Plan.
- `validation_policy_tests.cpp` (Section 8) does not call
  `setFailureHandler()` either, this round — none of its surviving test
  cases (`effectiveValidationLayersEnabled()`,
  `isFatalValidationSeverity()`, `validationMessageOrFallback()`) invoke
  `ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG` at all; they are pure
  classification/selection functions with no failure-reporting path to
  intercept. (`tests/core/assert_tests.cpp`, unrelated to this Plan,
  already covers `ATLANTIS_CHECK`/`reportFailure()`/`setFailureHandler()`
  mechanics themselves and is not re-tested here.)
- `atlantis_vulkan_backend_tests` (GPU-independent) and
  `atlantis_vulkan_backend_gpu_tests` (GPU-required) are, per Section 8,
  **separate executables, separate processes** — even if some future test
  case in either one did install a replacement handler, it could not leak
  into the other, since process boundaries already provide that isolation
  with no additional mechanism needed.
- No new global state, mutex, atomic, thread, or job/task system is
  introduced anywhere in this Plan to manage failure-handler isolation —
  the isolation above follows entirely from (a) this Plan's own test code
  simply not calling `setFailureHandler()`, and (b) ordinary OS process
  boundaries between the two test executables.

### Public API Surface Audit (this round)

| Symbol | Driven by | Could be private instead? | New ownership/lifetime contract? |
|---|---|---|---|
| `DeviceCreateParams` | Spec 0003 Functional Requirements ("A `Device` RHI interface is constructible..."; "Vulkan Validation Layers are enabled unconditionally in Debug builds") | No — public factory's parameter type; `createDevice()` itself must be public per ADR-0014 | No |
| `DeviceCreateError` | Spec 0003 ("recoverable ... failures are surfaced through `atlantis::Result`, never silently discarded") | No — `createDevice()`'s public error type | No |
| `createDevice()` | ADR-0014's Decision fixes that this factory function exists and is public | No — ADR-0014 fixes that this is public | No; `Device` ownership already covered by Section 2 |
| `PresentationCreateError` | Spec 0003 (same `Result`-surfacing requirement) | No — `createPresentation()`'s public error type | No |
| `createPresentation()` | ADR-0014's Decision fixes that this factory function exists, is public, and is the sole `NativeWindowHandle`-accepting function | No — ADR-0014 fixes that this is public | No; `Presentation`-must-outlive-`Device` ownership already covered by Section 2 |

**`ValidationSink` is removed from this table entirely — no replacement
public symbol is added for validation enforcement.** The debug-messenger
callback (`detail::debugMessengerCallback`) stays private to
`src/vulkan_backend/src/validation.cpp`; the failure mechanism it invokes
(`ATLANTIS_CHECK_MSG`) is Core's existing, `Accepted` public API, not a
new one this Plan introduces. **Net public-API change for validation
enforcement, this round: zero new symbols.** No symbol in this table
exists for a future second backend, future CI, or general-purpose
diagnostics beyond what Spec 0003's own Testing & Verification Plan
requires.

---

## 7. Disposition of Spec 0003's Risks & Open Questions

Spec 0003's Risks & Open Questions section explicitly leaves five items
"to the Plan stage." Each is evaluated below against whether it changes a
public API shape, module boundary, ownership model, threading model,
dependency, or backend contract (the bar `AGENTS.md`'s Golden Rule sets
for requiring a human/ADR decision instead of a Plan-level judgment
call). **None of the five meets that bar** — each is proposed here as an
implementation detail, with rationale. "Proposed disposition" below is
the disposition Human Review accepted, at joint Spec 0003 + Plan 0003
Human Review on 2026-08-08 (see the Approval transition note at the top
of this document), not a decision this document made unilaterally.

| # | Spec 0003 open item | Proposed disposition | Rationale |
|---|---|---|---|
| 1 | GPU-required, non-rendering test-harness category (neither `testing-strategy.md` layer 1 nor layer 2 as currently named) | New CTest label `gpu` plus Catch2 tag `[gpu]` (combined with `[integration]` where a live window is also needed), new binary `atlantis_vulkan_backend_gpu_tests`, see Section 8. | Purely a test-organization/tagging convention; no public API, module boundary, ownership, threading, or dependency change. `testing-strategy.md` itself is not edited by this Plan (out of this task's scope) — flagged as a documentation follow-up, not performed here. |
| 2 | Queue selection policy (single combined graphics+present queue vs. separate families) | Phase 1 requires exactly one queue family supporting both `VK_QUEUE_GRAPHICS_BIT` and Win32 presentation support (`vkGetPhysicalDeviceWin32PresentationSupportKHR`, which needs no `VkSurfaceKHR` — consistent with `Device` being constructed before any `Presentation`/surface exists); if no such family exists on the chosen physical device, `createDevice()` returns `Err(NoSuitablePhysicalDevice)`. This is a **necessary but not sufficient** check — Section 5 adds the authoritative, surface-specific `vkGetPhysicalDeviceSurfaceSupportKHR` check inside `createPresentation()`, once a real `VkSurfaceKHR` exists, closing the gap a Win32-generic check alone leaves. Separate-family fallback is not implemented. | `Device`'s abstract public interface exposes no queue accessor at all in this spec's scope (Section 2), and neither does Vulkan Backend's own public surface — the selection algorithm and both support checks are entirely internal, invisible to any consumer, so this cannot change RHI's public API shape, ownership, or threading. Reversible without any interface change if a future spec needs separate-family support. |
| 3 | Multi-physical-device selection policy | Select the first physical device meeting the minimum requirement (Vulkan API version + item 2's queue-family requirement), enumerated via `vkEnumeratePhysicalDevices`. No enumeration/selection API is exposed to callers. | Same reasoning as item 2 — entirely internal, no public surface exposed, explicitly named by the Spec itself as a candidate for Plan-level resolution ("always select the first suitable one"). Does not foreclose a future explicit-selection API, since none exists to foreclose. |
| 4 | Validation-layer configuration (which layers/extensions, severity filtering) | `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils`, severity `WARNING`+`ERROR`, structurally forced on in Debug builds regardless of caller input, propagated to an **unconditional** process failure via `ATLANTIS_CHECK_MSG` followed by an explicit `std::abort()` fallback that runs regardless of the installed failure handler — `ATLANTIS_CHECK_MSG` alone does not guarantee this; the full design and correction is Section 6, not restated here. | Purely a Vulkan Backend construction-time configuration detail behind the existing `DeviceCreateParams::enableValidationLayers` bool and the internal `effectiveValidationLayersEnabled()` gate. **No new public API at all this round** (Section 6's Public API Surface Audit) — the failure mechanism calls Core's already-`Accepted` `ATLANTIS_CHECK_MSG` plus one private, local `std::abort()` fallback (confirmed by Human Review, Section 6, as not requiring a new ADR and not a public API change either way); no ownership/threading/dependency change beyond the Vulkan SDK, already an ADR-0006-categorized external dependency this spec is the first to actually consume. |
| 5 | Whether a swapchain at zero extent is eagerly released | **Not actually open** — already decided by ADR-0016's Decision (step 1) and Alternatives Considered (explicit rejection "for now"): left untouched. This Plan proposes implementing exactly that, nothing more. | Not a Plan-level decision at all; restated here only because Spec 0003's own Risks section still lists it as "left open," which this Plan addresses by pointing at the ADR text that already settles it, not by deciding anything new. |

**No item required stopping and escalating to a human/ADR decision, and
Human Review confirmed this on 2026-08-08** — every disposition above,
including items 2–4, was accepted as written at joint Spec 0003 + Plan
0003 Human Review (see the Approval transition note at the top of this
document).

---

## 8. Testing Strategy

### Correcting an earlier revision's CTest selection claim

**A bare `ctest` invocation, with no `-L`/`-LE` flag, runs every
registered test regardless of any CTest label.** Labels are filter
criteria, applied only when `-L`/`-LE` is passed explicitly — they do not
change what a plain `ctest` runs. This Plan does not rely on any other
claim anywhere:

- The **GPU-independent** verification command is
  `ctest --test-dir <build> -LE gpu --output-on-failure` (label-exclude).
- The **GPU-required** verification command is
  `ctest --test-dir <build> -L gpu --output-on-failure` (label-include).
- **Neither is spelled as a bare `ctest`** anywhere in this Plan — every
  command example below is one of the two lines above, explicitly.

### Two executables, not two registrations of one executable

A single executable per test-prerequisite category, not two
`catch_discover_tests()` calls with complementary `TEST_SPEC` filters
against one executable (which risks ambiguous/overlapping registration
without a distinguishing `TEST_PREFIX`/`TEST_SUFFIX`) — a real deviation
from Plan 0001/0002's "one test binary per module" precedent, narrowly
justified because `atlantis_vulkan_backend` is the first module with a
genuinely categorical split in test prerequisites (some tests need a real
Vulkan-capable GPU; most don't) that Core/Platform never had:

```cmake
# tests/vulkan_backend/CMakeLists.txt

add_executable(atlantis_vulkan_backend_tests
  vulkan_result_tests.cpp
  presentation_logic_tests.cpp
  validation_policy_tests.cpp)
target_include_directories(atlantis_vulkan_backend_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src)
target_link_libraries(atlantis_vulkan_backend_tests PRIVATE
  Atlantis::VulkanBackend Catch2::Catch2WithMain atlantis_compiler_warnings)
catch_discover_tests(atlantis_vulkan_backend_tests)

add_executable(atlantis_vulkan_backend_gpu_tests
  vulkan_presentation_gpu_tests.cpp)
target_include_directories(atlantis_vulkan_backend_gpu_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src)
target_link_libraries(atlantis_vulkan_backend_gpu_tests PRIVATE
  Atlantis::VulkanBackend Atlantis::Platform Catch2::Catch2WithMain atlantis_compiler_warnings)
catch_discover_tests(atlantis_vulkan_backend_gpu_tests PROPERTIES LABELS "gpu")
```

No `TEST_SPEC` filter is used anywhere: `atlantis_vulkan_backend_tests`
contains *only* GPU-independent source files, so every case it discovers
is GPU-independent by construction; `atlantis_vulkan_backend_gpu_tests`
contains *only* `vulkan_presentation_gpu_tests.cpp`, so
`PROPERTIES LABELS "gpu"` on its single `catch_discover_tests()` call
labels every test it registers, correctly, with nothing to exclude. There
is no way for the same Catch2 test case to be registered twice — each
source file compiles into exactly one of the two binaries, never both.

### GPU-independent unit tests (`ctest -LE gpu`, no Vulkan device, no live window)

| Target | File | Covers |
|---|---|---|
| `atlantis_rhi_tests` | `tests/rhi/types_tests.cpp` | `Extent2D::isZero()`/`operator==`; `SwapchainMetadata`/`Format`/`PresentationError` construct and are inspectable |
| `atlantis_vulkan_backend_tests` | `vulkan_result_tests.cpp` | `VkResult` → `DeviceCreateError`/`PresentationCreateError`/`PresentationError` mapping, exercised with literal `VkResult` enumerators (no instance/device required — `VkResult` is a compile-time enum) |
| `atlantis_vulkan_backend_tests` | `presentation_logic_tests.cpp` | `detail::decideRecreateAction()` — the zero-extent-skip/no-op/recreate trichotomy, exhaustively; `detail::checkSurfaceSupported()` — both `true`/`false` cases |
| `atlantis_vulkan_backend_tests` | `validation_policy_tests.cpp` | `detail::effectiveValidationLayersEnabled()` — all four `(isDebugBuild, requested)` combinations; `detail::isFatalValidationSeverity()` — `WARNING`/`ERROR` return `true`, `INFO`/`VERBOSE` return `false`; `detail::validationMessageOrFallback()` — a normal `pMessage`, a null `pMessage`, and a null `callbackData` all return a safe, non-null string |

**Why `debugMessengerCallback()` itself is not called by any ordinary
Catch2 test case this round (corrected from the previous revision):**
once the callback's `WARNING`/`ERROR` branch ends in an explicit,
unconditional `std::abort()` (Section 6), calling it directly from a
normal test process on a `WARNING`/`ERROR` severity — even with a
replacement `AssertFailureHandler` installed that itself returns
normally — still reaches that `std::abort()` and terminates the whole
test binary. The previous revision's `validation_failure_policy_tests.cpp`
called the full callback directly and expected it to return after
installing such a handler; that design assumed `ATLANTIS_CHECK_MSG` alone
was fatal-or-not-at-all, which Section 6 now corrects — it is neither: a
replacement handler can make it non-terminating on its own, but this
Plan's own fallback then makes the *callback as a whole* unconditionally
terminating regardless. **There is therefore no way to unit-test the full
callback's `WARNING`/`ERROR` branch inside an ordinary Catch2 test case
without crashing the test binary**, and this Plan does not attempt to.
Testing is split instead:

- **Pure-function unit tests** (`validation_policy_tests.cpp`, above):
  `isFatalValidationSeverity()` — the severity classification the
  callback's `if` condition depends on — and `validationMessageOrFallback()`
  — the null-safe message selection — are both extracted as ordinary,
  non-`noexcept`-constrained, non-aborting pure functions specifically so
  they can be exercised directly, exhaustively, with no device and no
  risk of terminating the test process. Neither calls
  `ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG`, so neither test installs or needs
  a replacement failure handler.
- **Code inspection (not dynamically tested — stated honestly, not
  rounded up):** that the `WARNING`/`ERROR` branch of
  `debugMessengerCallback()` itself calls `ATLANTIS_CHECK_MSG(false, message)`
  followed immediately by an unconditional `std::abort()`, with no
  early return, no conditional guard, and no path that could skip the
  `std::abort()` once that branch is entered — verified by reading
  `validation.cpp`, not by any test that lets the branch execute and
  return.
- **Death test / subprocess test: not built.** This repository has no
  death-test or subprocess-test infrastructure today, and this Plan does
  not add one, a general-purpose process-testing framework, or a new
  dependency to acquire one — that would itself be new testing
  infrastructure/scope beyond what this round's narrow fix calls for. If
  Human Review wants the fallback's actual termination behavior
  dynamically exercised (rather than confirmed by code inspection alone),
  that is a candidate follow-up requiring its own scoping decision, not
  something this Plan builds inline.
- **GPU lifecycle tests** (below): confirm the *normal*, non-violating
  path — `Device`/`Presentation` construction, recreation, and
  destruction complete and the test process exits normally on real
  hardware. They do not, and are not claimed to, exercise the fatal
  branch at all, since this Plan does not attempt to provoke a real
  Vulkan Validation violation (see below).
- **This Plan does not currently attempt to manufacture a real Vulkan
  Validation Layer violation anywhere**, in any test or the demo, to
  observe the fatal path firing end-to-end. Finding a violation trigger
  that is safe, harmless to the test machine, and reproducible across
  driver/SDK versions is not guaranteed, and provoking one deliberately
  inside the shared GPU test binary would abort that binary and every
  other test case scheduled in the same run. **The fatal path's actual,
  dynamic, end-to-end termination behavior is therefore not covered by
  any automated test in this Plan — only by the code-inspection point
  above.** This is stated as a real, acknowledged limitation, not implied
  away.

`atlantis_rhi_tests` is registered separately, in `tests/rhi/CMakeLists.txt`,
via an ordinary unlabeled `catch_discover_tests(atlantis_rhi_tests)` —
included by `ctest -LE gpu` (it has no `gpu` label to exclude).

### GPU-required tests (`ctest -L gpu`; real Vulkan device, real window)

**Disposition (Section 7, item 1):** a dedicated executable,
`atlantis_vulkan_backend_gpu_tests` (above), every case CTest-labeled
`gpu` via the CMake target's own `catch_discover_tests(...)` call — not a
Catch2-level `[gpu]` tag filter. Catch2 tags `[gpu][integration]` are
still applied to each `TEST_CASE` for direct-invocation documentation
purposes, but they are not what makes `ctest -L gpu` work — the CMake
`LABELS` property is. `#if defined(_WIN32)`-gated, in
`tests/vulkan_backend/vulkan_presentation_gpu_tests.cpp`. Reuses Windows
Platform (already implemented) to create a real, hidden-from-the-user
window purely within the test process, obtains its `NativeWindowHandle`,
then:

1. **Device construction**: `createDevice({.enableValidationLayers = true})`.
   **If this returns `Err`, the test case `FAIL()`s immediately** with the
   error code and a message identifying the environment as not
   Vulkan-capable (missing SDK/driver/device) or validation-layer-
   unavailable — see "Environment-blocked behavior" below; it never
   silently reports success or an empty result. No further assertion
   about validation cleanliness is written here — a real `WARNING`/
   `ERROR` during construction would already have aborted the process
   before this line, per Section 6.
2. **Presentation construction**: `createPresentation(*device, handle)`
   succeeds; the concrete-surface support check (Section 5) is exercised
   implicitly (this machine's combined queue family is expected to
   support the surface it itself can create — a failure here would itself
   be a `FAIL()`-worthy environment/driver anomaly, not silently ignored).
   No swapchain exists yet (no metadata query is meaningful before the
   first `recreateIfNeeded()`).
3. **Zero-extent skip, real Vulkan**: `notifyResized({0,0})` then
   `recreateIfNeeded()` returns `Ok` — corroborates the structural
   code-inspection guarantee (Section 5) against a real device; does not
   re-derive it.
4. **Resize-driven recreation**: `SetWindowPos` the test window to a
   known non-zero size, `notifyResized(...)`, `recreateIfNeeded()` returns
   `Ok`; `metadata()` reflects the new extent, a non-`Unknown` format, and
   `imageCount() > 0` (ordinary `REQUIRE`s against `metadata()`'s fields —
   unrelated to validation, still written explicitly).
5. **Idempotent no-op**: a second `recreateIfNeeded()` call with no
   intervening `notifyResized()` is a no-op (same metadata).
6. **Repeat resize**: a second, different non-zero size recreates again;
   metadata updates accordingly.
7. **Destruction at multiple points**: separate test cases destroy
   `Presentation` (a) immediately after construction (surface only, no
   swapchain), (b) after one `recreateIfNeeded()`, (c) after a resize +
   second `recreateIfNeeded()`, then `device.reset()` in every case. **No
   explicit assertion is written for any of these** — the test case
   reaching the end of its `TEST_CASE` body and returning normally, for
   every one of these teardown orderings, is itself the evidence that
   `Presentation` destruction, `vkDestroyDevice`, and `vkDestroyInstance`
   were all validation-clean (Section 6's destruction-boundary table); a
   violation at any of those points would have aborted the process before
   the test case could return.

**Environment-blocked behavior:** absence of the Vulkan SDK at build time
fails CMake configuration outright (`find_package(Vulkan REQUIRED)`,
Section 10) — the earliest and most explicit failure point. Absence of a
usable Vulkan-capable GPU/driver at test-run time is caught by test case
1's `FAIL()` on `createDevice()`'s `Err` branch, immediately and loudly —
never a silent 0-tests-passed or falsely-green result. There is no path
in this design where "no GPU available" reads as "tests passed."

These tests require a real, Vulkan-capable Windows machine — the same
requirement as this repository's own primary interactive dev target (see
`CLAUDE.md`'s platform note). **No GPU-touching CI exists today**
([docs/process/ci-strategy.md](../docs/process/ci-strategy.md) — no CI
pipeline of any kind exists yet); this Plan does not claim CI covers
these tests now. The `gpu` CTest label is preparatory infrastructure for
whenever a GPU-touching CI job is specced — that future job would run
`ctest -L gpu` explicitly, on a runner it independently confirms has a
Vulkan-capable GPU, per whatever that future CI spec decides; this Plan
does not design that job.

### Final verification pass (Section 12) runs both commands, explicitly

The full verification pass is: `ctest --test-dir <build> -LE gpu
--output-on-failure` **then, separately and explicitly,**
`ctest --test-dir <build> -L gpu --output-on-failure` **then** the
interactive demo run. Neither substitutes for the other, neither is a
bare `ctest`, and Section 12's Implementation Order states this as three
distinct, sequential commands, not one "run the tests" step.

---

## 9. Explicit Prohibitions (verification-checkable)

None of the following may appear anywhere in `src/rhi/`,
`src/vulkan_backend/`, `tests/rhi/`, `tests/vulkan_backend/`, or
`examples/rhi_vulkan_demo/` introduced by this Plan — each is grep- or
inspection-checkable at review time:

| Prohibited | Check |
|---|---|
| Any acquire-shaped method (`acquireNextTarget`, `acquireNextImage`, or equivalent) | grep for `acquire` (case-insensitive) across new public/private headers |
| Any `present()`-shaped method | grep for `present(` outside comments/doc text |
| `RenderTarget` declared or referenced | grep for `RenderTarget` |
| `VkSemaphore`, `VkFence`, `VkCommandPool`, `VkCommandBuffer`, `vkQueueSubmit` | grep for each token |
| `vkCmd*`, `VkPipeline`, `vkCreateGraphicsPipelines`, `vkAcquireNextImageKHR`, `vkQueuePresentKHR` | grep for each token |
| `src/render_graph/`, `src/renderer/`, Shader System source | directory listing |
| Any Android NDK type/header, `src/vulkan_backend/src/wsi/android_surface.*`, `if(ANDROID)` build logic beyond a no-op guard | grep/directory listing |
| Any Linux-specific source, build configuration, or CI job | grep/directory listing |
| A second graphics-backend module or an abstraction knob "for" one | code review — only `atlantis_vulkan_backend` implements RHI |
| `std::thread`, a job/task system, or any new synchronization primitive (`std::atomic`, `std::mutex`) introduced for validation handling | grep for `std::thread`, `std::async`, `std::mutex`, `std::atomic` in `src/vulkan_backend/` — must be empty; Section 6 explains why none is needed |
| `VMA`/`vk_mem_alloc.h` or a hand-rolled general suballocator | grep for `vk_mem_alloc`, `VmaAllocator` |
| A public queue accessor, `VkQueue`, or queue-family index exposed on `rhi::Device`/Vulkan Backend's public surface | grep for `VkQueue`/`queueFamily` in public headers — must be empty (Section 5's surface-support check keeps this internal) |
| `ValidationSink`, `validationSink`, or any caller-suppliable, caller-optional observer as part of the validation-failure mechanism | grep for `ValidationSink`, `validationSink` in this Plan — must be empty outside historical revision notes (Section 6 — the failure guarantee is unconditional, not opt-in) |
| A validation-layer callback that can throw, or that returns `VK_TRUE` and claims that alone converts a `void`-returning destroy call into a checked failure | code review — `noexcept` on the callback (compiler-enforced); the callback always returns `VK_FALSE`, and termination comes from the explicit `std::abort()` fallback, not the return value (Section 6) |
| A claim anywhere that `ATLANTIS_CHECK_MSG` alone (with no fallback after it) is "structural," "unconditional," or otherwise guaranteed-fatal regardless of the installed `AssertFailureHandler` | grep for `ATLANTIS_CHECK_MSG` in this Plan's prose and confirm every fatal-guarantee claim is attributed to the explicit `std::abort()` fallback, not to `ATLANTIS_CHECK_MSG` in isolation (Section 6) |
| The `WARNING`/`ERROR` branch of `debugMessengerCallback()` missing its `std::abort()` fallback, or that fallback reachable via any early return/guard that could skip it | code review of `validation.cpp` — no test can safely exercise this branch dynamically (Section 8) |
| A normal Catch2 `TEST_CASE` that calls `debugMessengerCallback()` directly with a `WARNING`/`ERROR` severity and expects it to return | grep `tests/vulkan_backend/` for `debugMessengerCallback` — must not appear outside `validation.cpp`/`vulkan_instance.cpp`'s own installation call sites (Section 8) |
| `setFailureHandler()` called anywhere in `atlantis_vulkan_backend_gpu_tests` or `examples/rhi_vulkan_demo` | grep both for `setFailureHandler` — must be empty (Section 6's Process-Level Failure-Handler Isolation) |
| Death-test/subprocess-test infrastructure, or a new dependency acquired to build one | grep `CMakeLists.txt` files under `tests/` for any new `FetchContent_Declare`/`find_package` beyond Vulkan — must be empty (Section 8) |
| A bare `ctest` (no `-L`/`-LE`) documented anywhere as either the GPU-independent or the GPU-required command | grep for `` `ctest` `` without an adjacent `-L`/`-LE` flag in this Plan's own command examples — Section 8 |

---

## 10. Build Integration

- Root `CMakeLists.txt`: add `find_package(Vulkan REQUIRED)` once, before
  `add_subdirectory(src/vulkan_backend)`; `add_subdirectory` calls for
  `src/rhi`, `src/vulkan_backend` (unconditional — Windows is the only
  configured target and this spec is Windows-only, matching how
  `src/platform`'s Android branch is currently a no-op guard, not an
  omitted `add_subdirectory`), `tests/rhi`, `tests/vulkan_backend` (under
  the existing `ATLANTIS_BUILD_TESTS` guard), and
  `examples/rhi_vulkan_demo` (under `ATLANTIS_BUILD_EXAMPLES`).
- The Vulkan SDK is consumed as the external system/toolchain dependency
  [ADR-0006](../adr/0006-dependency-management.md) already categorized it
  as — located via `find_package(Vulkan REQUIRED)`, **not** fetched via
  `FetchContent`, **not** vendored. `REQUIRED` means CMake configuration
  fails outright, loudly, if the SDK is absent — the earliest possible
  "explicit failure, not silent pass" point (Section 8). It must already
  be installed on whichever machine builds this Plan's implementation
  (and, later, any GPU-touching CI image) — installing it is explicitly
  **not** part of this Plan or of drafting it; that is an
  Implementation-phase prerequisite for whoever picks this Plan up after
  Human Review.
- No `vcpkg`/`Conan` step is introduced, consistent with ADR-0006.
- `atlantis_vulkan_backend` links `Vulkan::Vulkan` **PRIVATE** only — its
  own public header (`vulkan_backend.h`) never includes a Vulkan header,
  so consumers (the demo) never see `Vk*` types even transitively.

---

## 11. Verification Demo — `examples/rhi_vulkan_demo/`

Mirrors `examples/foundation_demo/`/`examples/platform_demo/`'s
CMake/structure pattern; a new sibling directory, not a modification of
either. Per Spec 0003's Non-Goals, this is explicitly **not** a preview
of Atlantis Runtime.

`main.cpp` (illustrative — a candidate shape, exact log call sites are
ordinary implementation, not fixed further by this Plan):

```cpp
#include <atlantis/log.h>
#include <atlantis/platform/platform.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

int main() {
  using namespace atlantis;

  auto initResult = platform::initialize();
  ATLANTIS_CHECK(initResult.isOk());

  auto deviceResult = vulkan_backend::createDevice({.enableValidationLayers = true});
  ATLANTIS_CHECK(deviceResult.isOk());
  auto device = std::move(deviceResult.value());

  std::unique_ptr<rhi::Presentation> presentation;  // constructed once SurfaceCreated arrives

  while (!platform::shouldQuit()) {
    bool closeRequested = false;
    for (const auto& event : platform::processEvents()) {
      if (auto* created = std::get_if<platform::SurfaceCreated>(&event)) {
        auto presResult = vulkan_backend::createPresentation(*device, created->handle);
        ATLANTIS_CHECK(presResult.isOk());
        presentation = std::move(presResult.value());
      } else if (auto* resize = std::get_if<platform::WindowResize>(&event)) {
        if (presentation) {
          presentation->notifyResized({resize->framebuffer.width, resize->framebuffer.height});
          auto recreateResult = presentation->recreateIfNeeded();
          ATLANTIS_CHECK(recreateResult.isOk());
          // ATLANTIS_LOG_INFO(...) with metadata() here, elsewhere.
        }
      } else if (std::holds_alternative<platform::WindowCloseRequested>(event)) {
        closeRequested = true;
      }
    }
    if (closeRequested) {
      presentation.reset();  // destroyed before device, before shutdown()
      platform::shutdown();
    }
  }

  // No validation-specific check is needed here: if any Vulkan call
  // anywhere above -- construction, any recreateIfNeeded(), or the
  // teardown below -- had produced a WARNING/ERROR, the debug-messenger
  // callback's ATLANTIS_CHECK_MSG (Section 6) would have already
  // terminated the process before reaching this point. Reaching
  // `return 0;` after full teardown IS the validation-clean signal.
  presentation.reset();
  device.reset();
  return 0;
}
```

Proves, per Spec 0003's Manual Verification requirements: `Device`
construction with validation layers active; `Presentation` construction
from a real `SurfaceCreated` handle (surface only, no swapchain yet, and
the concrete-surface support check from Section 5 exercised); zero-extent
(including a minimized-at-launch window) never issuing a Vulkan swapchain
call; a drag-resize triggering recreation observable via `metadata()`;
minimize/restore cycles; clean destruction at whatever point the window
is closed — and, structurally, that "the process ran to completion and
exited `0`" already certifies validation cleanliness across the object's
**entire** lifecycle including full teardown (Section 6), with no
separate diagnostics object for a human or script to remember to check.
**No `acquireNextTarget()`-shaped call, no `present()` call, and no
command buffer of any kind anywhere in this file** — the window stays
entirely blank, exactly like `examples/platform_demo/`.

---

## 12. Implementation Order

Each step ends with a build-and-test action that is real and runnable —
no informal or throwaway ad hoc checks are proposed anywhere in this
sequence; wherever a real-GPU behavior needs verifying, that verification
is carried out by the formal GPU test suite (step 10) or the formal demo
(step 11), never by unplanned scratch code written and discarded mid-step.
None of these steps has been executed by this Plan document itself; they
describe the Implementation phase this Plan's `Approved / Ready for
Implementation` status now authorizes (joint Spec 0003 + Plan 0003 Human
Review, 2026-08-08 — see the Approval transition note at the top of this
document), not steps already taken.

1. **RHI headers** (Section 2): `device.h`, `presentation.h`, `types.h` +
   `types.cpp`. CMake target `atlantis_rhi` created and builds (Debug +
   Release).
2. **`tests/rhi/types_tests.cpp`**: build + `ctest --test-dir <build> -LE
   gpu --output-on-failure` run — first real `atlantis_rhi_tests` pass, no
   device needed.
3. **Vulkan Backend skeleton**: `CMakeLists.txt` with
   `find_package(Vulkan REQUIRED)`; `vulkan_backend.h` (Section 3, types
   and factory declarations only, no bodies, no diagnostics type).
   Confirms the target configures and links against the Vulkan SDK
   header/import library on the implementer's machine (or fails
   configuration loudly if the SDK is absent — Section 10).
4. **`vulkan_result.h`/`.cpp`** (`VkResult` → error mapping) +
   `vulkan_result_tests.cpp`: build + `ctest --test-dir <build> -LE gpu
   --output-on-failure` run, no device needed.
5. **`vulkan_presentation.h`'s pure `detail::` functions**
   (`decideRecreateAction()`, `checkSurfaceSupported()` — Section 5,
   declared ahead of the full `VulkanPresentation` class body, the same
   way step 4 precedes step 7's full `VulkanDevice`) +
   `presentation_logic_tests.cpp`: build + `ctest --test-dir <build> -LE
   gpu --output-on-failure` run, no device needed.
6. **`validation.h`/`.cpp`**: `IsDebugBuild`,
   `effectiveValidationLayersEnabled()`, `isFatalValidationSeverity()`,
   `validationMessageOrFallback()`, and `debugMessengerCallback()` — the
   latter's `WARNING`/`ERROR` branch calling `ATLANTIS_CHECK_MSG` then an
   explicit `std::abort()` fallback (Section 6) — +
   `validation_policy_tests.cpp` (the three pure functions only; the full
   callback is not called by this or any other Catch2 test case, per
   Section 8): build + `ctest --test-dir <build> -LE gpu
   --output-on-failure` run, no device needed. The `WARNING`/`ERROR`
   branch's actual `std::abort()` behavior is confirmed by code review at
   this step, not by a dynamic test — Section 8.
7. **`vulkan_instance.h`/`.cpp`** (`VkInstance` creation, calls
   `effectiveValidationLayersEnabled()`, chains the instance-creation/
   destruction debug messenger into `VkInstanceCreateInfo::pNext`,
   `pUserData = nullptr` — Section 6) + **`vulkan_device.h`/`.cpp`**
   (physical device + Win32 presentation-capability queue selection per
   Section 7 items 2–3, logical device creation, installs the explicit
   debug messenger — holds only the `VkDebugUtilsMessengerEXT` handle, no
   validation state) + `createDevice()` implementation: build only. This
   is the first step requiring the Vulkan SDK to actually link; it is
   **not** followed by an informal manual smoke check — `Device`
   construction is first formally, automatically verified at step 10.
8. **`wsi/win32_surface.h`/`.cpp`** (Section 4): build only.
9. **`vulkan_presentation.cpp`'s full body** (concrete-surface support
   check via `checkSurfaceSupported()`, swapchain (re)creation via
   `decideRecreateAction()` — Section 5) + `createPresentation()`
   implementation: build only.
10. **`tests/vulkan_backend/vulkan_presentation_gpu_tests.cpp`**, built as
    the separate `atlantis_vulkan_backend_gpu_tests` executable (Section
    8): build, then run explicitly via
    `ctest --test-dir <build> -L gpu --output-on-failure` on a real
    Windows/Vulkan machine — this is the first point `Device`/
    `Presentation` construction, every behavior from steps 6–9, and
    destruction-phase validation cleanliness (a violation during any
    teardown step aborts the test process immediately, per Section 6,
    which CTest reports as a failure) are formally, automatically
    verified; supersedes any need for an ad hoc check earlier in this
    sequence.
11. **`examples/rhi_vulkan_demo/`** (Section 11): build + run
    interactively — resize (drag), minimize, restore, close; confirm the
    process runs to completion and exits `0` — any validation violation
    during construction, use, or teardown would have already aborted it
    (Section 6).
12. **Documentation**: `src/README.md`, `tests/README.md`,
    `examples/README.md`, `README.md` (Vulkan SDK prerequisite) per
    Section 1's Files to Modify.
13. **Full verification pass**: Debug configure/build, Release
    configure/build, `ctest --test-dir <build> -LE gpu --output-on-failure`
    (GPU-independent), `ctest --test-dir <build> -L gpu --output-on-failure`
    (GPU-required), interactive demo run (completion + exit-code check),
    zero compiler warnings, Definition of Done pass before PR. Neither
    `ctest` command is a bare `ctest` — see Section 8.

### Sequencing & Dependencies

- Steps 1–2 have no Vulkan dependency and can complete before the Vulkan
  SDK is even confirmed present on the implementer's machine.
- Steps 4–6 depend only on step 3's declarations (no real device) and can
  proceed in any relative order among themselves.
- Step 7 depends on step 6 (calls `effectiveValidationLayersEnabled()`
  and uses `debugMessengerCallback()`) and step 4 (uses the shared
  `VkResult` mapping) and is the first step requiring a real GPU to
  exercise (though not to build).
- Step 8 depends on step 3 (uses `NativeWindowHandle`) and Platform
  (already implemented); independent of step 7.
- Step 9 depends on step 5 (uses `decideRecreateAction()`/
  `checkSurfaceSupported()`), step 7 (`Device`), and step 8 (WSI
  boundary).
- Step 10 depends on step 9 and on Windows Platform (already implemented,
  reused to create the test's window).
- Step 11 depends on step 10 passing (don't demo against unverified
  internals).
- Steps 12–13 are final and depend on everything above.

---

## 13. Acceptance Criteria Mapping

| Spec 0003 Acceptance Criterion | Satisfied by (step) | Verified by |
|---|---|---|
| RHI public headers contain no `Vk*`/no `#include <vulkan/...>` | Step 1 (Section 2 — RHI never includes Vulkan) | grep across `src/rhi/include` |
| `NativeWindowHandle` appears only in Vulkan Backend's construction-API header | Step 3 (Section 3) | grep across `src/rhi` and `src/vulkan_backend/include` for `NativeWindowHandle` — exactly one match, in `vulkan_backend.h` |
| `Presentation` never declares/implements/calls acquire, `present()`, or a sync primitive | Steps 1, 9 (Sections 2, 5) | Section 9's grep checklist; code review |
| `RenderTarget` never declared/referenced | Steps 1–11 (never introduced) | Section 9's grep checklist |
| `recreateIfNeeded()` issues zero Vulkan calls at `{0,0}`, first call and every later call | Step 9 (Section 5's `decideRecreateAction()` structural early return) | Code inspection (structural — the Vulkan-calling branch is unreachable from `Skip`); corroborated by step 10's GPU test #3 |
| Windows resize recreates the swapchain at the new extent, observable via metadata, no Renderer-level code involved | Steps 9–11 | Step 10's GPU test #4/#6; step 11's interactive run; grep confirms no `src/renderer` exists to be involved |
| Every `VkResult` checked, none discarded | Steps 4, 7, 9 (all Vulkan calls, including `vkGetPhysicalDeviceSurfaceSupportKHR`'s own `VkResult` in step 9's concrete-surface check, routed through the shared checked-call/mapping helper) | Code review — every `vk*` call site paired with a result check |
| Debug builds and any GPU-touching test run with Validation Layers enabled; a warning/error fails the run, including during destruction | Steps 6, 7, 10, 11 (`effectiveValidationLayersEnabled()` structurally forces `true` in Debug regardless of caller input — Section 6; the debug-messenger callback's `ATLANTIS_CHECK_MSG` + explicit `std::abort()` fallback fails the process on any `WARNING`/`ERROR` regardless of the installed failure handler, never gated on a caller-supplied observer) | `validation_policy_tests.cpp`'s pure-function coverage of `effectiveValidationLayersEnabled()`/`isFatalValidationSeverity()`/`validationMessageOrFallback()` (step 6) plus code review of the callback's fatal branch (the branch itself is not dynamically test-covered — Section 8); step 10's GPU test suite completing normally — a violation would crash it, a CTest-reported failure, through full teardown; step 11's demo reaching `return 0;` |
| No draw call, clear, `VkPipeline`, layout transition, or command buffer created/recorded/submitted | Steps 1–11 (never implemented) | Section 9's grep checklist |
| No swapchain image, `RenderTarget`, or per-image resource ever acquired/vended/referenced | Steps 1–11 (no acquire method exists anywhere) | Section 9's grep checklist |
| No `src/render_graph/`, `src/renderer/`, or Shader System source created | Steps 1–13 (never touched) | Directory listing |
| No Android NDK build config, second graphics backend, or thread/job system introduced | Steps 1–13 (never touched) | Section 9's grep checklist |
| All three ADRs (0014, 0015, 0016) `Accepted` before Spec 0003 `Approved` | **Already satisfied** — Spec 0003 is already `Approved`, all three ADRs `Accepted` 2026-08-06 | No action needed; recorded fact, not a Plan deliverable |

Every Spec 0003 acceptance criterion is mapped; none paraphrased away.

---

## Verification Checklist

- [ ] **Unit tests:** `atlantis_rhi_tests` (`types_tests.cpp`) and
      `atlantis_vulkan_backend_tests` (`vulkan_result_tests.cpp`,
      `presentation_logic_tests.cpp`, `validation_policy_tests.cpp` —
      covering `effectiveValidationLayersEnabled()` including the
      Debug-cannot-disable case, `isFatalValidationSeverity()`, and
      `validationMessageOrFallback()`, all pure functions, none calling
      `ATLANTIS_CHECK`/installing a failure handler) pass via
      `ctest --test-dir <build> -LE gpu --output-on-failure`, no device
      required. Not a bare `ctest`.
- [ ] **GPU-required, non-rendering tests (new category, Section 8):** the
      separate `atlantis_vulkan_backend_gpu_tests` executable
      (`vulkan_presentation_gpu_tests.cpp`, every case CTest-labeled `gpu`)
      passes via `ctest --test-dir <build> -L gpu --output-on-failure` on
      a real Windows/Vulkan machine. A validation violation at any point
      (construction, recreation, or destruction) aborts the test process
      via the callback's explicit `std::abort()` fallback, which CTest
      reports as a failure — not observed via any explicit `REQUIRE`
      against a counter, because none exists.
- [ ] **Headless integration tests:** not applicable — no headless
      rendering exists (Spec 0003 Non-Goals).
- [ ] **Image regression tests:** not applicable — nothing is rendered.
- [ ] **Vulkan Validation Layers clean, including destruction:** confirm
      by code review of `validation.cpp` that the `WARNING`/`ERROR` branch
      of `debugMessengerCallback()` calls `ATLANTIS_CHECK_MSG` followed
      immediately by an unconditional `std::abort()`, with no early
      return, guard, or conditional that could skip the `std::abort()`
      (Section 6) — not merely that `ATLANTIS_CHECK_MSG` is called, and
      not merely absent from a human's visual scan of the console. Confirm
      no test dynamically exercises this branch (it cannot be, safely —
      Section 8), and that this Plan states that limitation honestly
      rather than implying dynamic coverage. Confirm the one acknowledged
      gap (`vkDestroyDebugUtilsMessengerEXT`'s own call, Section 6's
      destruction-boundary table) is stated plainly, not silently claimed
      as covered.
- [ ] **No overstated `ATLANTIS_CHECK_MSG` guarantee:** grep this Plan for
      `ATLANTIS_CHECK_MSG` and confirm no sentence attributes an
      unconditional/structural fatal guarantee to `ATLANTIS_CHECK_MSG`
      alone — every such claim must be attributed to the explicit
      `std::abort()` fallback that follows it (Section 6/9).
- [ ] **Failure-handler isolation:** confirm `atlantis_vulkan_backend_gpu_tests`
      and `examples/rhi_vulkan_demo` never call `setFailureHandler()`
      (Section 6/9); confirm `validation_policy_tests.cpp`'s test cases
      touch no failure-handler state at all, since none of the three
      functions it tests call `ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG`.
- [ ] **Debug validation-layer guarantee:** confirm no code path reads
      `DeviceCreateParams::enableValidationLayers` directly for the
      enable/disable decision — only
      `detail::effectiveValidationLayersEnabled(detail::IsDebugBuild, ...)`
      does (Section 6); confirm via code review, corroborated by
      `validation_policy_tests.cpp`.
- [ ] **Concrete-surface presentation support:** confirm
      `createPresentation()` calls `vkGetPhysicalDeviceSurfaceSupportKHR`
      against the actual `VkSurfaceKHR` it created (not only the
      Device-construction-time Win32-generic check), checks its
      `VkResult`, and destroys the surface before returning
      `Err(UnsupportedDevice)` on an unsupported result (Section 5);
      confirm no new public queue/`VkQueue`/queue-family accessor exists
      anywhere (Section 9).
- [ ] **No optional/caller-dependent validation-failure path remains:**
      grep confirms no `ValidationSink`/`validationSink` symbol exists
      anywhere outside this document's own revision-history prose
      (Section 9); the callback's `ATLANTIS_CHECK_MSG` call is
      unconditional, with no `if (userData)`-style gate around it
      (Section 6).
- [ ] **No public `recordMessage()`-shaped mutator or diagnostics type:**
      confirm `vulkan_backend.h` declares only `DeviceCreateParams`,
      `DeviceCreateError`, `createDevice()`, `PresentationCreateError`,
      `createPresentation()` — nothing else (Section 3).
- [ ] **CTest commands:** confirm neither this Plan's own text nor its
      Implementation Order ever describes a bare `ctest` as either the
      GPU-independent or GPU-required command (Section 8/9).
- [ ] **Other:** manual repo scan (Section 9's grep checklist) confirms
      no prohibited symbol/pattern; full Debug + Release build with zero
      new compiler warnings; interactive demo run (resize, minimize,
      restore, close) performed at least once, completion + exit code
      checked.

## Rollback Plan

Purely additive: two new modules (`src/rhi/`, `src/vulkan_backend/`),
their tests (`tests/rhi/`, `tests/vulkan_backend/`), one new demo
(`examples/rhi_vulkan_demo/`), and CMake/documentation touch-ups.
Reverting the implementing PR removes all of the above and restores
`CMakeLists.txt`, `src/README.md`, `tests/README.md`,
`examples/README.md`, and `README.md` to their post-Spec-0002 state — no
migration or cleanup beyond a standard revert, since nothing outside
these new directories is behaviorally changed.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- **"Image regression tests" is not applicable** — nothing is rendered
  (Spec 0003 Non-Goals).
- **"Vulkan Validation Layers run clean" fully applies**, unlike Plans
  0001/0002 — this is the first plan that touches the GPU, and "clean"
  means the callback's `ATLANTIS_CHECK_MSG` + explicit `std::abort()`
  fallback (Section 6) — not `ATLANTIS_CHECK_MSG` alone, which does not
  by itself guarantee termination (Section 6's corrected reasoning) — a
  violation aborts the process, reported by CTest as a failure, or by the
  demo as a crash rather than a normal exit, covering full teardown — not
  a human's read of console output, and not gated on any caller-supplied
  diagnostics object. Add: the Section 9 grep checklist passes; the
  Section 13 acceptance-criteria mapping is re-confirmed against the
  actual diff, not just this Plan's intent; the GPU-required test suite
  (Section 8) has actually been run via
  `ctest --test-dir <build> -L gpu --output-on-failure` on a real machine,
  not merely written; the demo was actually run to completion and its
  exit code checked, not only its console output; code review confirmed
  the fatal branch's `std::abort()` is unconditional and unreachable-
  skipping-wise unguarded, since no dynamic test covers it (Section 8).

---

## Consistency Review

1. **Consistency with Spec 0003:** every Functional Requirement has a
   corresponding implementation section above (Sections 2–6, 10–11); every
   Non-Goal is restated in Section 9's checklist; every Acceptance
   Criterion is mapped in Section 13; every open Risk is dispositioned in
   Section 7.
2. **Consistency with ADR-0001:** no `Vk*` type or Vulkan header appears
   in `src/rhi/`'s public headers (Section 2); only Vulkan Backend
   includes Vulkan headers (Sections 3–6).
3. **Consistency with ADR-0002/0003:** `RenderTarget` is never declared;
   `Presentation` owns its own swapchain with RAII teardown; no hidden
   caching/pooling anywhere in Sections 4–6, and this round removes the
   one piece of new mutable state (`ValidationSink`'s counter) an earlier
   revision had introduced, rather than finding it a new home.
4. **Consistency with ADR-0004:** no thread, lock, or job/task system
   introduced; every construction/recreation/destruction path this Plan's
   own code executes is single-thread-only (Sections 2, 6). This round
   removes the only new synchronization primitive an earlier revision
   introduced (`std::atomic<unsigned int>`) along with the state it
   guarded — see Section 6's Thread-Safety subsection for why "no
   consumer of an atomic" is the point, not an oversight.
5. **Consistency with ADR-0005 (amended)/ADR-0013:** the WSI boundary
   (Section 4) is private to Vulkan Backend, never destroys the native
   window, and is the only place `<windows.h>`/`vulkan_win32.h` appear
   outside Platform's own existing files.
6. **Consistency with ADR-0011:** `NativeWindowHandle` crosses exactly one
   boundary (Section 3's `createPresentation`), never reaching RHI's
   public `Presentation` interface (Section 2).
7. **Consistency with ADR-0014:** `Device`/`Presentation` are abstract
   base classes (Section 2); construction is via Vulkan Backend's free
   factory functions (Section 3) — the dropped `PresentationCreateParams`
   remains within the "exact names/signatures are a Plan-stage detail"
   room ADR-0014 leaves open. `ValidationSink` (introduced, then removed,
   across two earlier revisions) is **not** treated as licensed by that
   same sentence — Section 6's Governance Review explicitly distinguishes
   "choosing a factory's parameter names" (in scope) from "inventing a new
   public type with its own ownership contract" (out of scope for that
   sentence alone), and resolves the latter by removal, not by asserting
   authorization that was never actually there.
8. **Consistency with ADR-0015:** no VMA dependency, no hand-rolled
   suballocator, no method signature presuming either — Section 10
   introduces only `find_package(Vulkan REQUIRED)`, nothing allocator-
   shaped.
9. **Consistency with ADR-0016:** `recreateIfNeeded()`'s 4-step contract
   (Section 5) is implemented verbatim; no acquire/present method exists
   anywhere (Section 9); zero-extent retention matches the ADR's Decision
   exactly, not re-decided.
10. **Consistency with ADR-0009:** the validation-failure mechanism
    (Section 6) calls `ATLANTIS_CHECK_MSG` exactly as
    `ADR-0009`/`src/core/include/atlantis/assert.h` define it — no
    modification to that macro, `reportFailure()`, or `setFailureHandler()`
    anywhere. The explicit `std::abort()` fallback added after it is new,
    local, private code in `validation.cpp`, not a change to Core's
    assertion API, and not a new general-purpose "third tier" between
    assertion and `Result` — it applies to exactly one call site's own
    invariant. Whether this local addition itself needs its own ADR was
    explicitly flagged for Human Review (Section 6) and **confirmed on
    2026-08-08: no new or amended ADR is required** — see Consistency
    Review item 17.
11. **No frame-level API, `RenderTarget`, sync primitive, GPU command,
    RenderGraph/Renderer/Shader-System/Runtime, general resource
    creation, GPU allocator, Android/iOS, Linux, second backend, or
    thread/job system introduced anywhere:** confirmed by Section 9's
    checklist, cross-referenced against Section 1's file list (nothing
    beyond `src/rhi/`, `src/vulkan_backend/`, their tests, and the demo).
12. **No re-litigation of any Accepted ADR's conclusion:** confirmed —
    every decision in Sections 2–6 either quotes or directly implements
    an ADR's already-fixed text; Section 7's five dispositions are all
    Plan-level implementation judgment calls with no public-API/ownership/
    threading/dependency/backend-contract weight, explicitly reasoned
    through rather than asserted.
13. **No candidate shape overclaimed as final, and this is now resolved
    by approval:** the "Candidate Shapes" headings on Sections 2–3
    describe what these shapes were during Draft review; the Approval
    transition note at the top of this document records that Human
    Review approved this Plan, including these shapes, as written on
    2026-08-08 — they are candidate no longer, they are approved.
    Section 6's Public API Surface Audit explicitly justifies why each
    remaining construction symbol exists, and explicitly states that this
    round introduces zero new ones for validation enforcement.
14. **No verification claim overstates what is actually covered:**
    Section 8 does not claim a bare `ctest` excludes GPU-required tests —
    every command example is `-LE gpu` or `-L gpu`, explicitly; Section 8
    explicitly states that the callback's `WARNING`/`ERROR` branch is
    verified by code inspection only, not by any dynamic test, and names
    why (calling it would abort the test binary); Section 6's
    destruction-boundary table names the one call
    (`vkDestroyDebugUtilsMessengerEXT`'s own teardown) this Plan's
    coverage does **not** reach.
15. **`ValidationSink`'s removal is itself governed correctly:** Section 6's
    Governance Review applies AGENTS.md's Golden Rule directly to
    `ValidationSink` (not deferring to "it's not RHI's API"), concludes it
    was a new public API/ownership contract not actually authorized by
    ADR-0014's "Plan-stage detail" sentence, and resolves this by removing
    the type — leaving **no open governance blocker requiring escalation
    to Human Review/ADR amendment** on this specific point, stated
    explicitly rather than left ambiguous.
16. **No overstated `ATLANTIS_CHECK_MSG` guarantee remains (this round's
    correction):** grep-checked in Section 9; every fatal-guarantee
    sentence in Sections 6, 8, 12, and 13 attributes termination to the
    explicit `std::abort()` fallback, never to `ATLANTIS_CHECK_MSG` in
    isolation. The previous revision's `validation_failure_policy_tests.cpp`
    (which relied on the incorrect assumption) is removed, not left in
    place with stale claims; its replacement (`validation_policy_tests.cpp`'s
    three pure-function cases) is honestly scoped, and the fatal branch's
    actual dynamic termination behavior is explicitly documented as
    uncovered by any automated test — not implied to be covered by the
    surviving unit tests.
17. **The `std::abort()` fallback's own governance status: resolved at
    Human Review, 2026-08-08.** Unlike `ValidationSink` (item 15, resolved
    by removal), this Plan's explicit `std::abort()` fallback is **kept**.
    Section 6's own reasoning for why it does not need a new ADR was
    explicitly flagged as a position for Human Review to confirm or
    overrule, not a foregone conclusion — and was reviewed and
    **confirmed**: the fallback is a private implementation strategy
    fulfilling Spec 0003's Validation-failure requirement, not a new
    public assertion mechanism, and requires no new or amended ADR. No
    open governance item remains on this point.

All seventeen checks passed at Draft review; items 15 and 17 above
additionally record the joint Spec 0003 + Plan 0003 Human Review decision
on 2026-08-08 (see the Approval transition note at the top of this
document). This Plan is `Approved / Ready for Implementation`; per that
approval, Implementation may
begin.
