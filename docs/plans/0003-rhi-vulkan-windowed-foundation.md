# Plan: Atlantis RHI and Vulkan Windowed Foundation

- **Spec:** [Spec 0003](../specs/0003-rhi-vulkan-windowed-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction; approved at
  joint Spec 0003 + Plan 0003 Human Review on 2026-08-08. The reviewer accepted
  the scope, candidate public API, module boundaries, file list, implementation
  order, and verification approach as written; confirmed the Validation
  callback's `std::abort()` fallback (Section 6) as a private implementation
  strategy needing no new or amended ADR; and directed that the stale
  `docs/architecture/module_boundaries.md` Vulkan Backend line (Section 1) is
  deferred to a separate documentation-consistency task and does not block
  implementation. Every Non-Goal and Section 9 prohibition remains in force.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 1. Original scope,
  ordered work, and verification retained. Draft-review history — including an
  earlier `ValidationSink` observer type that was designed, then removed as
  unneeded and unauthorized (Section 6), and a corrected claim about what
  `ATLANTIS_CHECK_MSG` alone guarantees — is preserved in this PR's and
  [PR #14](https://github.com/slmao/Atlantis/pull/14)'s history, not narrated
  here.

## Scope

Implements exactly `Presentation`'s **non-frame** lifecycle:
`Device`/`Presentation` construction, Windows WSI surface creation, swapchain
creation/recreation/destruction, zero-extent handling, resize-driven
recreation, and read-only swapchain metadata queries. Implements **none** of:
any acquire/present-shaped API, `RenderTarget`, any synchronization primitive
(semaphore/fence/command pool/command buffer/queue submission), any GPU command
(layout transition, clear, draw, pipeline), RenderGraph, Renderer, Shader
System, the Atlantis Runtime module, general `Buffer`/`Texture` creation, a GPU
memory suballocator (VMA or hand-rolled), Android/iOS implementation, Linux
support, a second graphics backend, or a thread/job system. Section 9 is the
verification-checkable version of this list.

Every C++ type, signature, enumerator, and file name in Sections 2–6 was a
candidate shape during Draft review and is now approved as this Plan states it.
Per [AGENTS.md](../../AGENTS.md), a forced deviation during implementation is
called out in the PR; a deviation that changes architecture returns to Spec
review.

## Objective

Turn [Spec 0003](../specs/0003-rhi-vulkan-windowed-foundation.md) into an
ordered, reviewable set of concrete changes: a backend-agnostic **Atlantis
RHI** module (`Device`/`Presentation` abstract interfaces) and a Windows-only
**Atlantis Vulkan Backend** implementing their non-frame lifecycle, per
ADR-0001–0005 (0005 amended), 0009, 0011, 0013, 0014, 0015, 0016.

## Architectural boundaries (preserved, not re-decided)

```
Atlantis Platform (existing, Windows-implemented)
  --SurfaceCreated{ NativeWindowHandle }-->
Runtime-equivalent composition (this Plan's verification demo; NOT Runtime)
  --NativeWindowHandle, by value, uninterpreted-->
Vulkan Backend's construction API (ADR-0014)
  --consumes NativeWindowHandle only inside its private WSI boundary (ADR-0005 amended)-->
  VkSurfaceKHR (private to Vulkan Backend)
  -->
RHI's public Device / Presentation interfaces
  (backend-agnostic; zero Vk*, zero OS types, zero NativeWindowHandle;
  no RenderTarget vended anywhere in this Plan)
```

- RHI's public headers: zero `Vk*` types, zero OS-specific types, zero
  `NativeWindowHandle`; only `Atlantis::Core` is a dependency
  ([ADR-0001](../adr/0001-rhi-backend-independence.md)).
- `NativeWindowHandle` crosses exactly one boundary beyond Platform/Runtime:
  into Vulkan Backend's construction-API header
  ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)),
  consumed only inside the private WSI boundary, never by generic RHI's
  `Presentation` interface
  ([ADR-0011](../adr/0011-native-window-handle-representation.md)).
- Only Vulkan Backend includes Vulkan headers; its private WSI boundary may
  include `vulkan_win32.h`/`<windows.h>` strictly to build a `VkSurfaceKHR`
  from a borrowed `NativeWindowHandle`, and never destroys the native window
  ([ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) amended,
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)).
- `Device` has no window/surface knowledge. `Presentation` is constructed from
  a `Device&` and a `NativeWindowHandle`; construction creates only the
  `VkSurfaceKHR`, never a swapchain
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)).
- `notifyResized()`/`recreateIfNeeded()` are the only entry points that ever
  touch `VkSwapchainKHR`; a `{0,0}` tracked extent structurally skips every
  Vulkan swapchain call, first call and every later call alike (ADR-0016).
- `Presentation` never acquires, vends, or tracks a swapchain image or
  `RenderTarget`; destruction has no outstanding-acquired-image precondition
  (ADR-0016).
- Single logical frame thread
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)). RHI resources are
  explicitly owned, RAII-only; `Presentation` owns its own swapchain; nothing
  is cached or pooled implicitly
  ([ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md)). No GPU
  memory suballocation strategy is introduced or presumed by any interface
  shape ([ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)).

## Non-Goals (matching Spec 0003)

Does not propose implementing, sketching, or illustratively pre-declaring: an
acquire/present-shaped API; `RenderTarget`; any semaphore/fence/command
pool/command buffer/queue submission; any layout transition, clear, draw, or
`VkPipeline`; RenderGraph; Renderer; Shader System; the Atlantis Runtime module
(the verification demo is not a preview of it); general `Buffer`/`Texture`
creation; a GPU memory suballocator; Android or iOS implementation; Linux
support; a second graphics backend; a thread/job system; or any speculative
abstraction beyond what Spec 0003's Requirements call for. Section 9 is the
grep-checkable version.

## 1. Modules and CMake targets

Two new modules, following [ADR-0010](../adr/0010-cmake-structure.md)'s pattern:

| Module | Directory | Target / alias / namespace | Links |
|---|---|---|---|
| Atlantis RHI | `src/rhi/` | `atlantis_rhi` / `Atlantis::RHI` / `atlantis::rhi` | `Atlantis::Core` (PUBLIC) only |
| Atlantis Vulkan Backend | `src/vulkan_backend/` | `atlantis_vulkan_backend` / `Atlantis::VulkanBackend` / `atlantis::vulkan_backend` | `Atlantis::RHI`, `Atlantis::Core`, `Atlantis::Platform` (all PUBLIC), `Vulkan::Vulkan` (PRIVATE) |

`atlantis_vulkan_backend` links `Atlantis::Platform` **PUBLIC** because
[ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md) fixes
`createPresentation`'s signature as accepting
`atlantis::platform::NativeWindowHandle` by value, so its construction-API
header must include `atlantis/platform/native_window_handle.h`. This is a real
but narrow, header-only, lifecycle-free dependency scoped to one
trivially-copyable POD type plus transitively `Atlantis::Core` — Platform's
five public headers include no OS SDK header, and `windows_platform.cpp` is a
`PRIVATE` source contributing nothing a consumer inherits. Vulkan Backend never
calls `initialize()`/`processEvents()`/`shouldQuit()`/`shutdown()`. This is not
a new decision; ADR-0014 already fixed the signature.
`docs/architecture/module_boundaries.md`'s stale "Does not depend on Atlantis
Platform" line (a `PROPOSED` document predating ADR-0011/0014) is flagged, not
edited here — deferred at Human Review to a separate documentation task.

### File-level layout (expected — not created by this Plan)

```
src/rhi/
  CMakeLists.txt
  include/atlantis/rhi/  device.h  presentation.h  types.h
  src/types.cpp                     Extent2D::isZero()/operator==()

src/vulkan_backend/
  CMakeLists.txt
  include/atlantis/vulkan_backend/vulkan_backend.h   factories + param/error types; no Vulkan header
  src/
    vulkan_instance.{h,cpp}      VkInstance, layer/extension enablement
    vulkan_device.{h,cpp}        VulkanDevice: physical device + queue selection,
                                 VkDevice, installs the debug messenger (handle only)
    vulkan_presentation.{h,cpp}  VulkanPresentation: surface-only construction
                                 (+ concrete-surface support check), recreateIfNeeded(),
                                 notifyResized(), metadata(), teardown
    vulkan_result.{h,cpp}        VkResult -> RHI/backend error mapping (pure, unit-testable)
    validation.{h,cpp}           IsDebugBuild, effectiveValidationLayersEnabled(),
                                 isFatalValidationSeverity(), validationMessageOrFallback()
                                 (all pure), and the debug-messenger callback (Section 6)
    wsi/win32_surface.{h,cpp}    private WSI boundary; the only files here allowed
                                 <windows.h> / <vulkan/vulkan_win32.h>

tests/rhi/types_tests.cpp
tests/vulkan_backend/  vulkan_result_tests.cpp  presentation_logic_tests.cpp
                       validation_policy_tests.cpp        -> atlantis_vulkan_backend_tests
                       vulkan_presentation_gpu_tests.cpp  -> atlantis_vulkan_backend_gpu_tests (labeled "gpu")
examples/rhi_vulkan_demo/  CMakeLists.txt  main.cpp
```

**Files to Modify:** root `CMakeLists.txt` (the five `add_subdirectory` calls
under the existing test/example guards; `find_package(Vulkan REQUIRED)` once
before `add_subdirectory(src/vulkan_backend)`); `src/README.md`,
`tests/README.md` (document the two `tests/vulkan_backend` executables and the
`ctest -LE gpu` / `ctest -L gpu` commands), `examples/README.md`, `README.md`
(Vulkan SDK as a required pre-installed external dependency). No file under
`src/core`, `src/platform`, or their tests/examples is modified — purely
additive. No `src/render_graph/`, `src/renderer/`, or Shader System directory
is created.

## 2. RHI public interface

`types.h` (`namespace atlantis::rhi`):

- `Extent2D { unsigned width, height; isZero(); }` + `operator==`.
- `enum class Format { Unknown, Bgra8Unorm, Bgra8Srgb, Rgba8Unorm, Rgba8Srgb }`
  — PascalCase per AGENTS.md; describes only the currently-selected swapchain
  surface format for this Spec's read-only metadata query, **not** a general
  resource-format system (a future `Buffer`/`Texture` Spec introduces its own,
  possibly superseding this). The four names are the formats a Win32 swapchain
  commonly reports; `Unknown` is the pre-first-`recreateIfNeeded()`/failure
  default.
- `struct SwapchainMetadata { unsigned imageCount; Format format; Extent2D extent; }`.
- `enum class PresentationError { SurfaceLost, SwapchainCreationFailed, DeviceLost, Unknown }`
  — maps to `recreateIfNeeded()`'s and `createPresentation()`'s documented
  failure modes.

Every field/enumerator is read from a Functional Requirement Spec 0003 already
states in prose — none speculative.

`device.h`: `class Device { public: virtual ~Device() = default; };` — a
logical GPU device and its queues, no window/surface knowledge, no method
beyond the destructor in this Spec's scope. A future RenderGraph/CommandList
Spec extends this interface.

`presentation.h`: `class Presentation` (abstract) —

- `virtual void notifyResized(Extent2D) = 0;` — updates the tracked extent,
  marks recreation needed, makes no Vulkan call.
- `[[nodiscard]] virtual Result<void, PresentationError> recreateIfNeeded() = 0;`
  — the only operation that creates, recreates, or destroys the backing
  `VkSwapchainKHR` (ADR-0016's 4-step contract); issues zero Vulkan swapchain
  calls whenever the tracked extent is `{0,0}`, first call and every later call.
- `[[nodiscard]] virtual SwapchainMetadata metadata() const = 0;` — reflects
  the most recent successful (re)creation; never hands out an image handle, a
  `RenderTarget`, or any per-image resource.

Must be destroyed before the `Device` it was constructed from (caller-enforced,
per [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md)). Not
internally thread-safe; caller-thread-only. Declares no acquire, present, or
synchronization primitive.

## 3. Vulkan Backend construction API

`vulkan_backend.h` (`namespace atlantis::vulkan_backend`), per
[ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md) ("exact
names/signatures are a Plan-stage detail"). Includes no Vulkan header,
references no `Vk*` type:

- `struct DeviceCreateParams { std::string applicationName = "Atlantis"; bool enableValidationLayers = false; }`
  — `enableValidationLayers` can *request* validation in a Release build but
  **cannot disable it in a Debug build**; the effective value is always
  `detail::effectiveValidationLayersEnabled(detail::IsDebugBuild, enableValidationLayers)`
  (Section 6), never the field read directly. Whenever validation ends up
  enabled, any WARNING/ERROR message unconditionally fails the current process
  (Section 6) — no field or object opts into that.
- `enum class DeviceCreateError { InstanceCreationFailed, ValidationLayerUnavailable, NoSuitablePhysicalDevice, DeviceCreationFailed }`.
- `Result<unique_ptr<rhi::Device>, DeviceCreateError> createDevice(const DeviceCreateParams&)`.
- `enum class PresentationCreateError { SurfaceCreationFailed, UnsupportedDevice }`.
- `Result<unique_ptr<rhi::Presentation>, PresentationCreateError> createPresentation(rhi::Device&, atlantis::platform::NativeWindowHandle)`
  — the only public function anywhere in RHI/Vulkan Backend that accepts a
  `NativeWindowHandle`. `device` must outlive the returned `Presentation`;
  passing a `Device` not from this module's `createDevice()` is a programmer
  error (`ATLANTIS_CHECK`), impossible in Phase 1. **No third parameter:** Spec
  0003 states no presentation-creation preference this module needs, and this
  Plan does not reserve an empty placeholder struct (AGENTS.md's
  no-speculative-abstraction principle). A future Spec that needs one adds a
  parameter then — a source-compatible additive change.

No caller-suppliable validation observer type exists (an earlier draft's
`ValidationSink` was removed — Section 6).

## 4. Windows private WSI boundary

`src/vulkan_backend/src/wsi/win32_surface.{h,cpp}` — with
`windows_platform.cpp` and its smoke-test file, the only files permitted
`<windows.h>`; the only files permitted `<vulkan/vulkan_win32.h>`. Per
[platform-vulkan-wsi-boundary.md](../architecture/platform-vulkan-wsi-boundary.md),
does exactly and only:

1. Receive the opaque `NativeWindowHandle` (borrowed, non-owning) from
   `createPresentation()`.
2. `ATLANTIS_CHECK(kind == PlatformKind::Windows)` — any other kind reaching
   here is a programmer error.
3. Reinterpret `value0`/`value1` back to `HWND`/`HINSTANCE`.
4. `vkCreateWin32SurfaceKHR` → `VkSurfaceKHR`; check its `VkResult` via the
   shared checked-call helper.
5. Nothing else — no window creation, resize, or destruction; never calls
   `DestroyWindow` (ADR-0013).

Its output is handed back to `createPresentation()`, which performs the
concrete-surface presentation-support check (Section 5). Android's equivalent
(`wsi/android_surface.*`) is **not created**.

## 5. Presentation lifecycle — construction, recreation, destruction

Implements
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
already-fixed contract; nothing about the contract is a new decision.

**`createPresentation()` sequence:**

1. Call the WSI boundary to produce a `VkSurfaceKHR`; on `VkResult` failure
   return `Err(SurfaceCreationFailed)`.
2. **Concrete-surface presentation-support check.** `Device` construction only
   confirms generic Win32 presentation capability
   (`vkGetPhysicalDeviceWin32PresentationSupportKHR`, needs no surface).
   Vulkan additionally requires
   `vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex,
   surface, &supported)` against the *specific* surface — only possible here.
   `createPresentation()` calls it, checks its own `VkResult`, and dispatches
   through a small pure function
   `detail::checkSurfaceSupported(bool supported) -> optional<PresentationCreateError>`
   (returns `UnsupportedDevice` when `!supported`). On error it destroys the
   surface (`vkDestroySurfaceKHR` — safe, no dependents) and returns
   `Err(UnsupportedDevice)` — an existing enumerator. **No public queue
   accessor, `VkQueue`, or queue-family index is exposed anywhere**; the
   physical device handle and queue-family index come from the concrete
   `VulkanDevice`'s private state.
3. Construct `VulkanPresentation` in a "recreation needed" state. **No
   swapchain is created.**

**`notifyResized(Extent2D e)`:** `trackedExtent_ = e; recreationNeeded_ = true;`
— no Vulkan call, ever.

**`recreateIfNeeded()`:** first calls a pure dispatch function
`detail::decideRecreateAction(Extent2D trackedExtent, bool recreationNeeded) -> { Skip, NoOp, Recreate }`
(`Skip` when `trackedExtent.isZero()`; `NoOp` when `!recreationNeeded`;
otherwise `Recreate`) and switches on the result. `Skip` returns immediately
with **no Vulkan call issued anywhere on that path** — the structural property
Spec 0003 requires "verifiable by code inspection", satisfied by construction
(the Vulkan-calling branch is physically unreachable from `Skip`). `NoOp`
returns, no Vulkan call. `Recreate` destroys the previous `VkSwapchainKHR` if
any, creates a new one at `trackedExtent_` via `vkCreateSwapchainKHR`, checks
every `VkResult`, caches image count/format/extent, clears `recreationNeeded_`
on success (leaves it set for retry on failure), mapping any failing `VkResult`
to `PresentationError` via the shared helper, never discarding it.

**Metadata / destruction / zero-extent retention / Android:**

- `metadata()` returns the cached `SwapchainMetadata` from the most recent
  successful `Recreate`; read-only, no per-image handle.
- Destruction destroys the swapchain (if any) then the `VkSurfaceKHR`, in that
  order (ADR-0013's dependency-order rule). No acquired-image precondition —
  none is ever acquired.
- A swapchain created at a prior non-zero extent is **left untouched** when
  extent later becomes zero — already decided by ADR-0016 (step 1 and its
  Alternatives, which reject eager release "for now"); implemented as-is.
- Android surface destruction is out of `Presentation`'s own API (ADR-0016) —
  not implemented or tested here.

## 6. Validation-layer enforcement

**Invariant:** whenever Vulkan Validation Layers are enabled, any WARNING or
ERROR message must structurally fail the current build/test/run,
unconditionally, regardless of any caller-supplied object.

- **Debug cannot disable validation.** `validation.h` (private) defines
  `inline constexpr bool IsDebugBuild` (from `NDEBUG`) and
  `constexpr bool effectiveValidationLayersEnabled(bool isDebugBuild, bool requested)
  { return isDebugBuild || requested; }` — parameterized on `isDebugBuild` so
  both branches are unit-testable without two build configs.
  `vulkan_instance.cpp` calls it **once** with
  `(IsDebugBuild, params.enableValidationLayers)`; only that return value —
  never `params.enableValidationLayers` read elsewhere — decides whether
  `VK_LAYER_KHRONOS_validation` is requested and whether the debug messenger is
  installed.
- **Configuration:** `VK_LAYER_KHRONOS_validation` instance layer;
  `VK_EXT_debug_utils`; severity filter WARNING+ERROR (all message types). If
  validation was requested but the layer is unavailable, `createDevice()`
  returns `Err(ValidationLayerUnavailable)` — it does not silently continue.
- **Two messenger installations, same callback, `pUserData = nullptr`:** a
  `VkDebugUtilsMessengerCreateInfoEXT` chained into
  `VkInstanceCreateInfo::pNext` (covers `vkCreateInstance`/`vkDestroyInstance`
  themselves) plus an explicitly-created `VkDebugUtilsMessengerEXT` covering
  everything else — device creation, every `Presentation` operation
  (instance-scoped, no wiring in `createPresentation()`), and `vkDestroyDevice`.
- **The callback** (`debugMessengerCallback`, `noexcept`, private to
  `validation.cpp`): on `detail::isFatalValidationSeverity(severity)` (WARNING
  or ERROR), takes `detail::validationMessageOrFallback(callbackData)` (null-safe
  message selection), calls `ATLANTIS_CHECK_MSG(false, message)` for full
  diagnostics/handler integration, then an explicit **unconditional
  `std::abort()`**. It always returns `VK_FALSE`.
- **Why the `std::abort()` fallback.** `ATLANTIS_CHECK_MSG` alone is not
  guaranteed fatal: `AssertFailureHandler` is not `[[noreturn]]`,
  `setFailureHandler()` accepts any handler, `reportFailure()` returns
  normally, and only the *default* handler calls `std::abort()`
  ([ADR-0009](../adr/0009-assertion.md);
  `src/core/include/atlantis/assert.h`; `tests/core/assert_tests.cpp` installs
  a non-terminating replacement). The explicit `std::abort()` after
  `ATLANTIS_CHECK_MSG` makes the callback fatal regardless of the installed
  handler. It is not `std::unreachable()` (a returning handler is legal, not
  UB) and not `VK_TRUE` (which only aborts `VkResult`-returning calls;
  destruction-phase calls are `void`). It does not modify
  `ATLANTIS_CHECK`/`ATLANTIS_CHECK_MSG`, touches no Core file, and is not a new
  general-purpose macro — a local reinforcement of one call site's own
  invariant (Vulkan Validation cleanliness, which AGENTS.md singles out by
  name). **Confirmed at Human Review, 2026-08-08: no new or amended ADR
  required.**
- **No observer type.** Because failure is enforced inside the callback
  unconditionally, a successful return already means nothing went wrong —
  nothing to query afterward. The earlier `ValidationSink` (a new public type,
  a borrowed-pointer ownership contract, an atomic counter, a documented
  thread-safety story) is **removed**, not re-justified: AGENTS.md's Golden
  Rule makes it a significant new public API/ownership contract not authorized
  by ADR-0014's "Plan-stage detail" latitude, and the fail-fast redesign makes
  its purpose moot. No new synchronization primitive (atomic/mutex) is
  introduced for validation handling.
- **Destruction-boundary coverage:** the explicit messenger covers
  `Presentation` teardown and `vkDestroyDevice`; the `pNext`-chained messenger
  covers `vkDestroyInstance`. The one acknowledged gap:
  `vkDestroyDebugUtilsMessengerEXT`'s own call — no messenger's scope includes
  debug-utils object teardown, and validating the validator's own teardown is
  not a meaningful signal for Spec 0003's scope. Stated plainly, not claimed as
  covered.
- **`atlantis_vulkan_backend_gpu_tests` and `examples/rhi_vulkan_demo` never
  call `setFailureHandler()`** — the default log-then-abort handler is in
  effect throughout, but the guarantee does not depend on that. The two
  `tests/vulkan_backend` executables are separate processes;
  `validation_policy_tests.cpp` touches no failure-handler state (its three
  functions call no assertion).

## 7. Disposition of Spec 0003's Risks & Open Questions

Spec 0003 leaves five items "to the Plan stage". None changes a public API,
module boundary, ownership model, threading model, dependency, or backend
contract; each disposition below was accepted at Human Review 2026-08-08.

| # | Item | Disposition |
|---|---|---|
| 1 | GPU-required, non-rendering test-harness category | New CTest label `gpu` + Catch2 tag `[gpu]`, new binary `atlantis_vulkan_backend_gpu_tests` (Section 8). A test-organization convention; `testing-strategy.md` is not edited here (flagged as a documentation follow-up). |
| 2 | Queue selection policy | Phase 1 requires one queue family supporting both `VK_QUEUE_GRAPHICS_BIT` and Win32 presentation; else `createDevice()` returns `Err(NoSuitablePhysicalDevice)`. Section 5 adds the authoritative surface-specific `vkGetPhysicalDeviceSurfaceSupportKHR` check. Separate-family fallback not implemented. No public queue accessor exposed — entirely internal, reversible without an interface change. |
| 3 | Multi-physical-device selection | Select the first physical device meeting the minimum (API version + item 2's queue family), via `vkEnumeratePhysicalDevices`. No selection API exposed. Does not foreclose a future explicit-selection API. |
| 4 | Validation-layer configuration | `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils`, WARNING+ERROR, forced on in Debug, propagated to unconditional process failure (Section 6). No new public API; the Vulkan SDK is an already-categorized external dependency this Spec is first to consume. |
| 5 | Eager release of a zero-extent swapchain | Not actually open — ADR-0016 already decided "left untouched". Implemented as-is. |

## 8. Testing strategy

- **A bare `ctest` runs every registered test regardless of label.** The
  GPU-independent command is `ctest --test-dir <build> -LE gpu
  --output-on-failure`; the GPU-required command is `... -L gpu ...`. Neither
  is spelled as a bare `ctest` anywhere in this Plan.
- **Two executables**, not two `catch_discover_tests()` registrations of one:
  `atlantis_vulkan_backend_tests` (GPU-independent source files only) and
  `atlantis_vulkan_backend_gpu_tests` (`vulkan_presentation_gpu_tests.cpp` only,
  `catch_discover_tests(... PROPERTIES LABELS "gpu")`). No `TEST_SPEC` filter;
  no test case can register twice. This is a narrow, justified deviation from
  Plan 0001/0002's "one test binary per module" — Vulkan Backend is the first
  module with a genuinely categorical split in test prerequisites.

**GPU-independent unit tests** (`ctest -LE gpu`, no device, no window):

| Target | File | Covers |
|---|---|---|
| `atlantis_rhi_tests` | `types_tests.cpp` | `Extent2D`; `SwapchainMetadata`/`Format`/`PresentationError` construct and inspect |
| `atlantis_vulkan_backend_tests` | `vulkan_result_tests.cpp` | `VkResult` → error mapping, with literal `VkResult` enumerators |
| `atlantis_vulkan_backend_tests` | `presentation_logic_tests.cpp` | `decideRecreateAction()` trichotomy, exhaustively; `checkSurfaceSupported()` both cases |
| `atlantis_vulkan_backend_tests` | `validation_policy_tests.cpp` | `effectiveValidationLayersEnabled()` all four `(isDebugBuild, requested)` combinations; `isFatalValidationSeverity()` WARNING/ERROR vs. INFO/VERBOSE; `validationMessageOrFallback()` normal / null `pMessage` / null `callbackData` |

**The callback's WARNING/ERROR branch is not dynamically tested.** Once it ends
in an unconditional `std::abort()`, calling it from a normal test process
terminates the binary even with a non-terminating handler installed. Coverage
is split: the pure functions above are exercised directly; that the branch
calls `ATLANTIS_CHECK_MSG` then an unguarded `std::abort()` with no skip path
is **verified by code inspection of `validation.cpp`**, stated as a real
limitation. No death-test/subprocess infrastructure is added, and this Plan
does not manufacture a real Vulkan Validation violation anywhere.

**GPU-required tests** (`ctest -L gpu`; real device, real window, `#if
defined(_WIN32)`, `vulkan_presentation_gpu_tests.cpp`). Reuses Windows Platform
to create a real hidden window, obtains its `NativeWindowHandle`, then:

1. `createDevice({.enableValidationLayers = true})` — on `Err`, the case
   `FAIL()`s immediately identifying the environment as not Vulkan-capable or
   validation-layer-unavailable; never a silent pass.
2. `createPresentation(*device, handle)` succeeds; the concrete-surface support
   check is exercised implicitly. No swapchain yet.
3. Zero-extent: `notifyResized({0,0})` then `recreateIfNeeded()` returns `Ok` —
   corroborates the structural code-inspection guarantee against a real device.
4. Resize: `SetWindowPos` to a known non-zero size, `notifyResized(...)`,
   `recreateIfNeeded()` returns `Ok`; `metadata()` reflects the new extent, a
   non-`Unknown` format, `imageCount > 0` (ordinary `REQUIRE`s).
5. Idempotent no-op: a second `recreateIfNeeded()` with no intervening
   `notifyResized()` is a no-op.
6. Repeat resize: a second, different non-zero size recreates again.
7. Destruction at multiple points (immediately after construction; after one
   `recreateIfNeeded()`; after a resize + second call), then `device.reset()`.
   No explicit assertion — the case returning normally *is* the
   validation-clean evidence for `Presentation` destruction, `vkDestroyDevice`,
   and `vkDestroyInstance` (Section 6); a violation would have aborted the
   process first.

**Environment-blocked behavior:** a missing Vulkan SDK fails CMake
configuration outright (`find_package(Vulkan REQUIRED)`); a missing usable
GPU/driver is caught by case 1's `FAIL()`. No path reads "no GPU" as "tests
passed". No GPU-touching CI exists today; the `gpu` label is preparatory
infrastructure for a future CI Spec.

The full verification pass is three distinct sequential commands:
`ctest ... -LE gpu ...`, then `ctest ... -L gpu ...`, then the interactive demo
run.

## 9. Explicit prohibitions (verification-checkable)

None may appear in `src/rhi/`, `src/vulkan_backend/`, `tests/rhi/`,
`tests/vulkan_backend/`, or `examples/rhi_vulkan_demo/`:

| Prohibited | Check |
|---|---|
| Any acquire-shaped method (`acquireNextTarget`/`acquireNextImage`/equivalent) | grep `acquire` (case-insensitive) across new headers |
| Any `present()`-shaped method | grep `present(` outside comments |
| `RenderTarget` declared or referenced | grep `RenderTarget` |
| `VkSemaphore`, `VkFence`, `VkCommandPool`, `VkCommandBuffer`, `vkQueueSubmit` | grep each token |
| `vkCmd*`, `VkPipeline`, `vkCreateGraphicsPipelines`, `vkAcquireNextImageKHR`, `vkQueuePresentKHR` | grep each token |
| `src/render_graph/`, `src/renderer/`, Shader System source | directory listing |
| Any Android NDK type/header, `wsi/android_surface.*`, `if(ANDROID)` logic beyond a no-op guard | grep / directory listing |
| Any Linux-specific source, build config, or CI job | grep / directory listing |
| A second graphics-backend module or an abstraction knob "for" one | code review — only `atlantis_vulkan_backend` implements RHI |
| `std::thread`, a job/task system, or any `std::atomic`/`std::mutex` introduced for validation handling | grep `src/vulkan_backend/` — must be empty (Section 6) |
| `VMA`/`vk_mem_alloc.h` or a hand-rolled general suballocator | grep `vk_mem_alloc`, `VmaAllocator` |
| A public queue accessor, `VkQueue`, or queue-family index on the public surface | grep `VkQueue`/`queueFamily` in public headers — must be empty |
| `ValidationSink`/`validationSink`, or any caller-optional observer in the validation-failure mechanism | grep — must be empty outside revision history (Section 6) |
| A validation callback that can throw, or returns `VK_TRUE` and claims that alone converts a `void`-returning destroy call into a checked failure | code review — `noexcept` (compiler-enforced); always returns `VK_FALSE`; termination comes from the explicit `std::abort()` |
| The callback's WARNING/ERROR branch missing its `std::abort()` fallback, or that fallback skippable via an early return/guard | code review of `validation.cpp` |
| `setFailureHandler()` in `atlantis_vulkan_backend_gpu_tests` or `examples/rhi_vulkan_demo` | grep both — must be empty |
| Death-test/subprocess infrastructure, or a new dependency acquired for one | grep `tests/**/CMakeLists.txt` for new `FetchContent_Declare`/`find_package` beyond Vulkan |
| A bare `ctest` documented as either the GPU-independent or GPU-required command | grep this Plan's command examples |

## 10. Build integration

- Root `CMakeLists.txt`: `find_package(Vulkan REQUIRED)` once before
  `add_subdirectory(src/vulkan_backend)`; `add_subdirectory` for `src/rhi`,
  `src/vulkan_backend` (unconditional — Windows-only Spec, matching how
  `src/platform`'s Android branch is a no-op guard), `tests/rhi`,
  `tests/vulkan_backend` (under `ATLANTIS_BUILD_TESTS`),
  `examples/rhi_vulkan_demo` (under `ATLANTIS_BUILD_EXAMPLES`).
- The Vulkan SDK is the external system/toolchain dependency
  [ADR-0006](../adr/0006-dependency-management.md) already categorized —
  located via `find_package(Vulkan REQUIRED)`, **not** fetched, **not**
  vendored. `REQUIRED` fails configuration loudly if absent. Installing it is
  an Implementation-phase prerequisite, not part of this Plan.
- No `vcpkg`/`Conan` step.
- `atlantis_vulkan_backend` links `Vulkan::Vulkan` **PRIVATE** — its public
  header never includes a Vulkan header, so consumers never see `Vk*` types
  even transitively.

## 11. Verification demo — `examples/rhi_vulkan_demo/`

Mirrors `examples/foundation_demo/`/`platform_demo/`'s pattern (a new sibling
directory). Not a preview of Atlantis Runtime. `main()` initializes Platform,
`createDevice({.enableValidationLayers = true})`, then in the Platform event
loop: on `SurfaceCreated` calls `createPresentation`; on `WindowResize` calls
`presentation->notifyResized({fb.width, fb.height})` then `recreateIfNeeded()`
(logging `metadata()`); on `WindowCloseRequested` resets `presentation` (before
`device`, before `shutdown()`). Every `Result` is `ATLANTIS_CHECK`ed.

Proves Spec 0003's Manual Verification requirements: `Device` construction with
validation active; `Presentation` construction from a real `SurfaceCreated`
handle (surface only, support check exercised); zero-extent (including
minimized-at-launch) never issuing a Vulkan swapchain call; a drag-resize
triggering recreation observable via `metadata()`; minimize/restore; clean
destruction at close. Reaching `return 0;` after full teardown *is* the
validation-clean signal (Section 6) — no separate diagnostics object. **No
acquire-shaped call, no `present()`, no command buffer anywhere** — the window
stays blank.

## 12. Implementation order

Each step ends with a real, runnable build-and-test action — no throwaway ad
hoc checks. None has been executed by this Plan.

1. **RHI headers** (Section 2) + `types.cpp`; `atlantis_rhi` builds (Debug + Release).
2. `tests/rhi/types_tests.cpp`: build + `ctest ... -LE gpu ...` — first `atlantis_rhi_tests` pass, no device.
3. **Vulkan Backend skeleton**: `CMakeLists.txt` with `find_package(Vulkan REQUIRED)`; `vulkan_backend.h` (types + factory declarations, no bodies). Confirms the target links against the Vulkan SDK, or fails configuration loudly.
4. `vulkan_result.{h,cpp}` + `vulkan_result_tests.cpp`: build + `ctest ... -LE gpu ...`, no device.
5. `vulkan_presentation.h`'s pure `detail::` functions (`decideRecreateAction()`, `checkSurfaceSupported()`) + `presentation_logic_tests.cpp`: build + `ctest ... -LE gpu ...`, no device.
6. `validation.{h,cpp}`: `IsDebugBuild`, `effectiveValidationLayersEnabled()`, `isFatalValidationSeverity()`, `validationMessageOrFallback()`, and `debugMessengerCallback()` (WARNING/ERROR branch: `ATLANTIS_CHECK_MSG` then explicit `std::abort()`) + `validation_policy_tests.cpp` (pure functions only). Build + `ctest ... -LE gpu ...`. The branch's `std::abort()` behavior is confirmed by code review at this step, not a dynamic test.
7. `vulkan_instance.{h,cpp}` (chains the pNext debug messenger, `pUserData = nullptr`) + `vulkan_device.{h,cpp}` (physical device + Win32 presentation-capable queue selection per Section 7 items 2–3, logical device, installs the explicit messenger — handle only) + `createDevice()`: build only. First step requiring the Vulkan SDK to link; not followed by an informal smoke check — `Device` construction is first formally verified at step 10.
8. `wsi/win32_surface.{h,cpp}` (Section 4): build only.
9. `vulkan_presentation.cpp`'s full body (concrete-surface support check, swapchain (re)creation) + `createPresentation()`: build only.
10. `vulkan_presentation_gpu_tests.cpp` as `atlantis_vulkan_backend_gpu_tests`: build, then run via `ctest ... -L gpu ...` on a real Windows/Vulkan machine — first formal verification of construction, steps 6–9 behavior, and destruction-phase validation cleanliness.
11. `examples/rhi_vulkan_demo/`: build + run interactively (resize, minimize, restore, close); confirm the process completes and exits `0`.
12. Documentation: `src/README.md`, `tests/README.md`, `examples/README.md`, `README.md` (Vulkan SDK prerequisite).
13. Full verification pass: Debug + Release configure/build; `ctest ... -LE gpu ...`; `ctest ... -L gpu ...`; interactive demo run (completion + exit-code check); zero compiler warnings; Definition of Done pass before PR.

**Sequencing:** steps 1–2 need no Vulkan. Steps 4–6 depend only on step 3's
declarations and can proceed in any order. Step 7 depends on 6 and 4, first to
need a real GPU to exercise (not to build). Step 8 depends on 3, independent of
7. Step 9 depends on 5, 7, 8. Step 10 depends on 9 and Windows Platform. Step
11 depends on step 10 passing. Steps 12–13 depend on everything.

## 13. Acceptance criteria mapping

| Spec 0003 Acceptance Criterion | Step(s) | Verified by |
|---|---|---|
| RHI public headers: no `Vk*` / `#include <vulkan/...>` | 1 | grep across `src/rhi/include` |
| `NativeWindowHandle` only in Vulkan Backend's construction-API header | 3 | grep `src/rhi` + `src/vulkan_backend/include` — one match, in `vulkan_backend.h` |
| `Presentation` never declares/implements/calls acquire, `present()`, or a sync primitive | 1, 9 | Section 9 grep checklist; code review |
| `RenderTarget` never declared/referenced | 1–11 | Section 9 grep checklist |
| `recreateIfNeeded()` issues zero Vulkan calls at `{0,0}`, first and every later call | 9 | code inspection (structural); corroborated by step 10 test 3 |
| Windows resize recreates the swapchain at the new extent via metadata, no Renderer-level code | 9–11 | step 10 tests 4/6; step 11 run; grep confirms no `src/renderer` |
| Every `VkResult` checked, none discarded | 4, 7, 9 | code review — every `vk*` call site paired with a result check |
| Debug + any GPU-touching run has Validation Layers enabled; a warning/error fails the run, incl. during destruction | 6, 7, 10, 11 | `validation_policy_tests.cpp` pure-function coverage (step 6) + code review of the callback's fatal branch (not dynamically covered — Section 8); step 10 completing normally; step 11 reaching `return 0;` |
| No draw call, clear, `VkPipeline`, layout transition, or command buffer | 1–11 | Section 9 grep checklist |
| No swapchain image / `RenderTarget` / per-image resource ever acquired/vended/referenced | 1–11 | Section 9 grep checklist |
| No `src/render_graph/`, `src/renderer/`, Shader System source created | 1–13 | directory listing |
| No Android NDK build config, second backend, or thread/job system | 1–13 | Section 9 grep checklist |
| ADRs 0014/0015/0016 `Accepted` before Spec 0003 `Approved` | already satisfied 2026-08-06 | recorded fact |

## Verification Checklist

- [ ] **Unit tests:** `atlantis_rhi_tests` and `atlantis_vulkan_backend_tests`
      (incl. `validation_policy_tests.cpp`'s pure-function coverage) pass via
      `ctest --test-dir <build> -LE gpu --output-on-failure`, no device. Not a
      bare `ctest`.
- [ ] **GPU-required tests:** `atlantis_vulkan_backend_gpu_tests` passes via
      `ctest --test-dir <build> -L gpu --output-on-failure` on a real
      Windows/Vulkan machine. A validation violation at any point aborts the
      test process, which CTest reports as a failure.
- [ ] **Headless / image regression tests:** not applicable (Spec 0003
      Non-Goals; nothing rendered).
- [ ] **Vulkan Validation Layers clean, incl. destruction:** code review of
      `validation.cpp` confirms the WARNING/ERROR branch calls
      `ATLANTIS_CHECK_MSG` then an unconditional `std::abort()` with no
      skip path; confirm no test dynamically exercises this branch; confirm the
      one acknowledged gap (`vkDestroyDebugUtilsMessengerEXT`'s own call) is
      stated plainly, not claimed as covered.
- [ ] **No overstated guarantee:** grep confirms every fatal-guarantee claim is
      attributed to the explicit `std::abort()` fallback, never to
      `ATLANTIS_CHECK_MSG` in isolation.
- [ ] **Failure-handler isolation:** `atlantis_vulkan_backend_gpu_tests` and
      `examples/rhi_vulkan_demo` never call `setFailureHandler()`;
      `validation_policy_tests.cpp` touches no failure-handler state.
- [ ] **Debug validation guarantee:** no code path reads
      `DeviceCreateParams::enableValidationLayers` directly for the
      enable/disable decision — only
      `effectiveValidationLayersEnabled(IsDebugBuild, ...)` does.
- [ ] **Concrete-surface support:** `createPresentation()` calls
      `vkGetPhysicalDeviceSurfaceSupportKHR` against the actual `VkSurfaceKHR`
      it created, checks its `VkResult`, and destroys the surface before
      returning `Err(UnsupportedDevice)`; no new public queue/`VkQueue`/
      queue-family accessor exists.
- [ ] **No caller-optional validation path / diagnostics type:** grep confirms
      no `ValidationSink`/`validationSink` symbol outside revision history;
      `vulkan_backend.h` declares only `DeviceCreateParams`,
      `DeviceCreateError`, `createDevice()`, `PresentationCreateError`,
      `createPresentation()`.
- [ ] **CTest commands:** neither this Plan nor its Implementation Order ever
      describes a bare `ctest` as either verification command.
- [ ] **Other:** Section 9's grep checklist finds no prohibited symbol; full
      Debug + Release build with zero new warnings; the interactive demo run
      (resize, minimize, restore, close) performed at least once, completion +
      exit code checked.

## Rollback Plan

Purely additive: two new modules (`src/rhi/`, `src/vulkan_backend/`), their
tests, one demo, and CMake/documentation touch-ups. Reverting the implementing
PR removes all of the above and restores `CMakeLists.txt`, `src/README.md`,
`tests/README.md`, `examples/README.md`, and `README.md` to their
post-Spec-0002 state — a standard revert, nothing outside these new directories
is behaviorally changed.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas:

- **Image regression tests: not applicable** — nothing rendered.
- **Vulkan Validation Layers run clean fully applies** (the first plan that
  touches the GPU): "clean" means the callback's `ATLANTIS_CHECK_MSG` + explicit
  `std::abort()` fallback (not `ATLANTIS_CHECK_MSG` alone) aborts the process on
  any WARNING/ERROR, reported by CTest as a failure or by the demo as a crash,
  covering full teardown — not a human's read of console output, and not gated
  on a caller-supplied object. Add: Section 9's grep checklist passes; Section
  13's mapping is re-confirmed against the actual diff; the GPU-required suite
  has actually been run via `ctest ... -L gpu ...` on a real machine; the demo
  was run to completion and its exit code checked; code review confirmed the
  fatal branch's `std::abort()` is unconditional and unguarded, since no
  dynamic test covers it.
