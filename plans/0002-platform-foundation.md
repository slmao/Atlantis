# Plan: Atlantis Platform Foundation (Windows)

- **Spec:** [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved at Human Review on 2026-08-03 after the
  corrections below were applied and verified (final review: PASS).

> **Revised 2026-08-03** per Human Review: corrects `WM_CLOSE`/`WM_DESTROY`/
> `Quit` ordering, replaces `processEvents()`'s per-frame `std::vector`
> return with a reused-buffer `std::span`, removes the `tests/core` ->
> `tests/platform` CMake ordering dependency, clarifies Windows DPI/extent
> assumptions, and consolidates the public header set from 7 to 5. None of
> these are new architectural decisions — see Unresolved Implementation
> Details.

> This plan implements the **Windows** portion of Spec 0002 only. Android
> is architecturally specified (ADR-0012, ADR-0013) but not implemented
> here, except where a platform-neutral interface shape must be defined
> now so Windows and a future Android implementation share it without
> rework. iOS remains architecture-only. Linux is not a target platform
> and is not touched by this plan.

## Objective

Implement `Atlantis Platform`'s Windows path: application lifecycle,
native window (`HWND`) creation/ownership/destruction, `NativeWindowHandle`,
`PlatformEvent` delivery, window extent tracking, monotonic timing, and a
proof-of-build demo — per `specs/0002-platform-foundation.md` and
ADR-0005 (amended), ADR-0010, ADR-0011, ADR-0012, ADR-0013.

## Authoritative Sources

Treated as authoritative, not reinterpreted: `specs/0002-platform-foundation.md`,
`adr/0011-native-window-handle-representation.md`,
`adr/0012-application-lifecycle-and-event-model.md`,
`adr/0013-platform-window-ownership-and-lifetime.md`,
`adr/0005-platform-module-multi-os-windowing.md` (amended),
`adr/0010-cmake-structure.md`, `docs/architecture/platform-vulkan-wsi-boundary.md`,
`AGENTS.md`, `specs/0001-project-foundation.md`.

## Critical Architectural Boundaries (preserved, not re-decided here)

```
Platform -> PlatformEvent -> Runtime -> Runtime/Vulkan integration
  (Presentation orchestration) -> private Vulkan WSI -> VkSurfaceKHR
  -> Presentation / generic RHI
```

- Platform does not depend on Vulkan and includes no Vulkan header.
- Generic RHI does not depend on Platform, does not consume
  `PlatformEvent`, does not consume `NativeWindowHandle`. (Not built by
  this plan — stated as a constraint this plan must not violate.)
- Renderer and RenderGraph remain platform-independent. (Not built by
  this plan — same.)
- `NativeWindowHandle` is opaque, tagged, borrowed, non-owning.
- Vulkan WSI is out of scope for this plan entirely; nothing here
  creates, references, or prepares a `VkSurfaceKHR`.

## 1. Platform Module — File-Level Plan

### Files to Create

```
src/platform/CMakeLists.txt
src/platform/include/atlantis/platform/platform_kind.h
src/platform/include/atlantis/platform/native_window_handle.h
src/platform/include/atlantis/platform/platform_event.h
src/platform/include/atlantis/platform/clock.h
src/platform/include/atlantis/platform/platform.h
src/platform/src/clock.cpp
src/platform/src/windows/windows_platform.cpp

cmake/AtlantisDependencies.cmake

tests/platform/CMakeLists.txt
tests/platform/native_window_handle_tests.cpp
tests/platform/platform_event_tests.cpp
tests/platform/clock_tests.cpp
tests/platform/windows_platform_smoke_tests.cpp

examples/platform_demo/CMakeLists.txt
examples/platform_demo/main.cpp
```

(`window_extent.h` and `platform_error.h`, present in the prior draft of
this plan, are no longer created as separate files — see Section 2a,
Public Header Review.)

### Files to Modify

```
CMakeLists.txt          -- add_subdirectory for src/platform, tests/platform,
                            examples/platform_demo; include(cmake/AtlantisDependencies.cmake)
                            once, before either tests/ subdirectory
tests/core/CMakeLists.txt -- remove its embedded FetchContent_Declare(Catch2 ...) /
                            FetchContent_MakeAvailable / include(Catch) block;
                            link Catch2::Catch2WithMain directly (now provided
                            by cmake/AtlantisDependencies.cmake)
README.md               -- note the Platform module's existence, mirroring how Spec 0001's Core entry reads
src/README.md           -- add a src/platform/ entry mirroring the existing src/core/ entry
tests/README.md         -- add a tests/platform/ entry mirroring tests/core/
examples/README.md      -- add a platform_demo/ entry alongside foundation_demo/
```

No file under `src/core`, `examples/foundation_demo`, or `tests/core` has
its **behavior** changed — `tests/core/CMakeLists.txt`'s edit is a pure
build-file reorganization (dependency declaration moved to a shared
location), not a change to what Spec 0001 tests or how they run. No RHI,
Renderer, RenderGraph, or Vulkan Backend directory is created.

### Module/Naming Conventions (per ADR-0010 — not a new scheme)

- CMake target: `atlantis_platform` (static library), alias
  `Atlantis::Platform`, mirroring `atlantis_core`/`Atlantis::Core`.
- Public headers under `include/atlantis/platform/*.h`; private
  implementation under `src/` (portable) and `src/windows/` (Windows-only,
  CMake-gated).
- Namespace: `atlantis::platform` (per AGENTS.md's namespace list) — a
  peer subsystem namespace, unlike Core's bare `atlantis::`.
- Test target: `atlantis_platform_tests` (Catch2 v3, dependency now
  fetched once via `cmake/AtlantisDependencies.cmake` — see Section 3).
- Example target: `atlantis_platform_demo` under `examples/platform_demo/`,
  mirroring `atlantis_foundation_demo`.

## 2. Public Types — Exact Shapes

*(These fix the shapes ADR-0011/0012 left to "the Plan stage" — flagged
as Implementation Details per those ADRs' own Open Questions, not new
architectural decisions: neither changes any approved boundary.)*

**`platform_kind.h`:**
```cpp
namespace atlantis::platform {
enum class PlatformKind {
  Windows,
  Android,
  IOS,  // future — no implementation
};
}
```

**`native_window_handle.h`** (exactly ADR-0011's Decision, unaltered):
```cpp
namespace atlantis::platform {
struct NativeWindowHandle {
  PlatformKind kind;
  void* value0 = nullptr;  // Windows: HWND · Android: ANativeWindow*
  void* value1 = nullptr;  // Windows: HINSTANCE · Android: unused
};
}
```
Windows payload convention: `value0` holds the `HWND` (via
`reinterpret_cast<void*>`), `value1` holds the `HINSTANCE`. No accessor
methods beyond the struct's own fields — consistent with ADR-0011's
"borrowed, non-owning, no destructor" model. No `PlatformWindow` type is
introduced anywhere in this plan.

**`platform_event.h`** (Implementation Detail: concrete C++ representation
of the `PlatformEvent` set ADR-0012 defines conceptually — `std::variant`
chosen over a polymorphic base, consistent with `atlantis::Result`'s
existing value-type style. Also now contains `WindowExtent` — see Section
2a — since `WindowResize` is its only consumer):
```cpp
namespace atlantis::platform {

struct WindowExtent {
  unsigned int width = 0;
  unsigned int height = 0;
  [[nodiscard]] bool isZero() const { return width == 0 && height == 0; }
};
[[nodiscard]] bool operator==(const WindowExtent&, const WindowExtent&);

struct WindowResize { WindowExtent logical; WindowExtent framebuffer; };
struct WindowCloseRequested {};
struct FocusGained {};
struct FocusLost {};
struct ApplicationPause {};
struct ApplicationResume {};
struct SurfaceCreated { NativeWindowHandle handle; };
struct SurfaceDestroyed {};
struct Quit {};

using PlatformEvent = std::variant<
  WindowResize, WindowCloseRequested, FocusGained, FocusLost,
  ApplicationPause, ApplicationResume, SurfaceCreated, SurfaceDestroyed,
  Quit>;

}
```

**`clock.h`** (Implementation Detail resolving Spec 0002's own flagged
Open Question — see Section 5):
```cpp
namespace atlantis::platform {
using TimePoint = std::chrono::steady_clock::time_point;
[[nodiscard]] TimePoint monotonicNow();
}
```

**`platform.h`** (the lifecycle interface, plus `PlatformError` — see
Section 2a for why it lives here rather than its own file):
```cpp
namespace atlantis::platform {

enum class PlatformErrorCode {
  WindowClassRegistrationFailed,
  WindowCreationFailed,
};
struct PlatformError {
  PlatformErrorCode code;
  unsigned long nativeErrorCode = 0;  // e.g. GetLastError(); plain integer, no OS header needed
};

[[nodiscard]] atlantis::Result<void, PlatformError> initialize();

// Returns a view over this call's drained events. Valid only until the
// next call to processEvents() or shutdown() — see Section 4.
[[nodiscard]] std::span<const PlatformEvent> processEvents();

[[nodiscard]] bool shouldQuit();
void shutdown();
[[nodiscard]] PlatformKind currentPlatform();

}
```
This header includes no Windows/Android header; it is identical text
regardless of target OS, satisfying Acceptance Criterion 6 (Windows and
Android both satisfiable by the same interface) at the declaration level.

## 2a. Public Header Review (5 headers, down from 7)

Reviewed against "keep separate headers only where they provide a
meaningful API boundary; do not create one public header per trivial type
merely for symmetry":

| Header | Kept separate because | Merged/removed |
|---|---|---|
| `platform_kind.h` | Shared tag type referenced by both `native_window_handle.h` and `platform.h` — a genuine cross-header dependency, not symmetry | — |
| `native_window_handle.h` | The carefully-designed ADR-0011 artifact; referenced by `platform_event.h`'s `SurfaceCreated` | — |
| `platform_event.h` | The event vocabulary itself — ADR-0012's central artifact | **Absorbs the former `window_extent.h`**: `WindowExtent` has exactly one consumer (`WindowResize`), so a dedicated header added a file without adding a boundary |
| `clock.h` | A genuinely independent capability — a consumer wanting only monotonic time (e.g. a future Runtime timing utility) shouldn't need to see window/event types | — |
| `platform.h` | The lifecycle entry point | **Absorbs the former `platform_error.h`**: `PlatformError` exists solely to serve `initialize()`'s return type and has no other consumer |

**Final public-header list:** `platform_kind.h`, `native_window_handle.h`,
`platform_event.h`, `clock.h`, `platform.h`. No new architecture
introduced by this consolidation — it only changes which file a
declaration lives in.

## 3. Shared Test Dependency Setup (removes `tests/core` -> `tests/platform` ordering)

**`cmake/AtlantisDependencies.cmake`** (new; per ADR-0006's `FetchContent`/
pinned decision and ADR-0007's Catch2 v3 decision — same decisions,
relocated declaration, not a new one):

```cmake
include_guard(GLOBAL)

include(FetchContent)

FetchContent_Declare(
  Catch2
  URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
  URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
```

Root `CMakeLists.txt` calls `include(cmake/AtlantisDependencies.cmake)`
**once**, before `add_subdirectory`-ing either `tests/core` or
`tests/platform`. `include_guard(GLOBAL)` makes the file idempotent
defensively, though the root should only include it once.

`tests/core/CMakeLists.txt` and `tests/platform/CMakeLists.txt` each
independently do:
```cmake
target_link_libraries(<target> PRIVATE Atlantis::<Module> Catch2::Catch2WithMain atlantis_compiler_warnings)
catch_discover_tests(<target> DISCOVERY_MODE PRE_TEST)
```
with **no `FetchContent_Declare` of their own**. Neither directory depends
on the other having run first; both depend only on the root having
included `cmake/AtlantisDependencies.cmake`, which happens once, before
either. `add_subdirectory` order between `tests/core` and `tests/platform`
is no longer semantically significant.

## 4. `processEvents()` — Reused Buffer, No Required Per-Frame Allocation

**API:** `std::span<const PlatformEvent> processEvents();` (Section 2),
chosen over the prior draft's `std::vector<PlatformEvent>` return.

**Design:**
- Windows Platform holds one persistent internal buffer:
  `std::vector<PlatformEvent> eventBuffer_` as file-local static state in
  `windows_platform.cpp` (mirroring `src/core/src/log.cpp`'s
  function-local-static pattern — no new state-management convention).
- Each `processEvents()` call: `eventBuffer_.clear()` (retains capacity,
  does not deallocate), runs the non-blocking `PeekMessageW(...,
  PM_REMOVE)` drain loop, `push_back`s a `PlatformEvent` into
  `eventBuffer_` per relevant message, then returns
  `std::span<const PlatformEvent>{eventBuffer_.data(), eventBuffer_.size()}`.
- After the buffer's capacity has grown to accommodate the largest batch
  seen so far (typically within the first few frames), subsequent calls
  perform **no heap allocation** — `clear()` + `push_back()` within
  existing capacity is allocation-free. This satisfies "no per-frame
  allocation is required after capacity is established" without a
  custom fixed-capacity ring buffer, which would be unjustified
  complexity for Phase 1's event volumes (single digits per frame).
- **Lifetime rule (must be documented at the call site and honored by
  every consumer, including the demo):** the returned span is valid only
  until the **next** call to `processEvents()` **or** `shutdown()`
  (`shutdown()` also clears and refills the same buffer — see Section 6).
  Runtime must copy out whatever it needs to retain past that point (e.g.
  a `bool` flag noting "a close was requested this batch"), never retain
  the span or a reference into it.
- **Why `std::span` + reused `std::vector` over other allocation-free
  options:** a hand-rolled fixed-capacity array would need an arbitrary
  capacity bound (risking silently dropped events if exceeded) for no
  benefit over a capacity-stabilizing `std::vector`, which already
  provides this idiomatically; a ring buffer would add complexity
  unjustified by Phase 1's tiny per-frame event counts. This is flagged
  as an Implementation Detail, not re-litigated as an ADR, per this
  correction's own instruction.

**Unit test impact:** `platform_event_tests.cpp` still tests `PlatformEvent`
construction/`std::holds_alternative`/ordering using an ordinary local
`std::vector<PlatformEvent>` test fixture — independent of Platform's
internal buffer, unaffected by this change. Smoke tests (Section 8) now
consume `std::span<const PlatformEvent>` from `processEvents()` directly.

## 5. Timing

**Implementation Detail — resolves Spec 0002's own flagged Open Question
("whether Application Timing belongs in Platform or Core"):** implemented
**inside Platform** (`src/platform/src/clock.cpp`), as a single portable
translation unit compiled on every target (not gated by `if(WIN32)`),
wrapping `std::chrono::steady_clock::now()` directly — no OS-specific
code, no per-OS clock file. Rationale: Spec 0002 stated this decision
doesn't change the interface either way, so keeping it inside Platform
avoids touching the already-approved, already-implemented `Atlantis Core`
module (Spec 0001) for a Spec-0002 concern; relocating it to Core later,
if ever desired, is a non-breaking move since `monotonicNow()`'s
signature would not change.

## 6. Windows Implementation — `windows_platform.cpp`

**Window class & `HWND` lifecycle (per ADR-0013 — Platform creates, owns,
destroys):**
- `initialize()`: configure DPI awareness (see Section 7) **before**
  window creation; `RegisterClassExW` a private window class (e.g.
  `L"AtlantisWindowClass"`); `CreateWindowExW` one top-level window;
  store `HWND`/`HINSTANCE` in file-local static state. On failure at
  either step, return `Result::Err({PlatformErrorCode::WindowClassRegistrationFailed
  or WindowCreationFailed, GetLastError()})`; do not throw.
- On success, enqueue one `SurfaceCreated{handle}` event into
  `eventBuffer_` (Implementation Detail: delivered via the *first*
  `processEvents()` call after `initialize()` succeeds, not synchronously
  inside `initialize()` itself — keeps a single delivery mechanism,
  `processEvents()`, for every event including the first).
- **Vulkan WSI is not involved anywhere in this file** — no Vulkan header,
  no `VkSurfaceKHR` reference.

### `WM_CLOSE` / `WM_DESTROY` / `Quit` ordering (corrected)

The prior draft had `WM_CLOSE` unconditionally call `DestroyWindow`,
which conflated a *request* with completed destruction. Corrected model:

| Trigger | `PlatformEvent`(s) enqueued | Order | `shouldQuit()` |
|---|---|---|---|
| `WM_CLOSE` (any number of times, e.g. repeated clicks before Runtime acts) | `WindowCloseRequested` | one per message received | unaffected |
| `shutdown()` is called (Runtime's decision, at whatever point it chooses to act on a prior `WindowCloseRequested`, or for any other reason) — internally clears `eventBuffer_`, then calls `DestroyWindow`, which synchronously dispatches `WM_DESTROY` to the `WNDPROC` before `DestroyWindow` returns | `SurfaceDestroyed`, then `Quit` | `SurfaceDestroyed` always precedes `Quit`, in the same (now-cleared, freshly-filled) buffer | becomes `true` |

Precisely:
- **`WM_CLOSE` handling:** the `WNDPROC` enqueues `WindowCloseRequested`
  and returns `0` **without** calling `DefWindowProc` for this message —
  it does **not** call `DestroyWindow`. The window remains fully valid.
  `WindowCloseRequested` represents a request, never completed
  destruction — resolving the conflict this correction targets.
- **Runtime/application policy** decides whether and when to honor the
  request. This plan's Phase 1 policy (implemented in the demo, standing
  in for Runtime — see Section 9) is "always honor it, once, after
  finishing iteration over the batch that contained it" — **not** a full
  cancel-close UI system, per this task's explicit instruction; just
  correct request semantics.
- **`shutdown()`** is the only path that calls `DestroyWindow` in this
  plan. It: (1) `ATLANTIS_CHECK`s that Platform is currently initialized
  and not already shut down (guards double-`shutdown()`); (2) clears
  `eventBuffer_`; (3) if the `HWND` is valid, calls `DestroyWindow`,
  synchronously triggering `WM_DESTROY`, whose handler enqueues
  `SurfaceDestroyed` then `Quit` into the just-cleared buffer and sets an
  internal `shouldQuit_` flag `true`; (4) `UnregisterClassW`; (5) marks
  re-initialization as unsupported in Phase 1 (not designed, not a
  programmer-error guard — simply out of scope). **`processEvents()`
  remains legally callable after `shutdown()`** specifically to drain
  this final `{SurfaceDestroyed, Quit}` batch; once drained, further
  calls return an empty span, and `shouldQuit()` continues reporting
  `true`.
- **No duplicate/ambiguous ordering:** `SurfaceDestroyed`/`Quit` can only
  ever be enqueued once per Platform lifetime, because only one
  `shutdown()` call is supported (re-entry is guarded). Multiple
  `WindowCloseRequested` events *are* possible (e.g. rapid repeated
  clicks before Runtime acts) and are **not** an error — Platform does
  not de-duplicate them; Runtime's "act once" policy naturally absorbs
  any duplicates without special handling.

**Other message translation:**

| Win32 message | `PlatformEvent` | Notes |
|---|---|---|
| `WM_SIZE`, `wParam == SIZE_MINIMIZED` | `WindowResize{ {0,0}, {0,0} }` | Zero-extent per Spec 0002's minimize rule |
| `WM_SIZE`, otherwise | `WindowResize{ logical, framebuffer }` | See Section 7 — both computed from `GetClientRect`, reported equal in Phase 1 |
| `WM_SETFOCUS` | `FocusGained` | |
| `WM_KILLFOCUS` | `FocusLost` | |

No `ApplicationPause`/`ApplicationResume` is synthesized anywhere on
Windows, per ADR-0012 and this task's explicit instruction.

**`processEvents()`:** see Section 4 for the buffer/span mechanics; the
non-blocking `PeekMessageW(..., PM_REMOVE)` drain loop itself is
unchanged from the prior draft.

**`currentPlatform()`:** returns `PlatformKind::Windows` unconditionally
in this `.cpp` (selected by CMake, not runtime branching).

Implementation Detail: `#define WIN32_LEAN_AND_MEAN` before `#include <windows.h>`
in this file only, to avoid pulling in unrelated Win32 subsystem headers —
standard practice, not an architectural decision.

## 7. Window Extent — DPI Clarification

Retaining the Phase 1 choice that Windows reports equal `logical` and
`framebuffer` extents, now documented explicitly as a **limitation**, not
a silent simplification:

- Both `WindowResize.logical` and `WindowResize.framebuffer` are
  populated from the same Windows client-area **pixel** dimensions
  (`GetClientRect`) in Phase 1 — they are computed identically, not
  merely coincidentally equal.
- **Atlantis must configure appropriate DPI awareness before window
  creation** (Section 6, `initialize()`'s first step) — e.g.
  `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
  called once at process/`initialize()` start, before
  `CreateWindowExW`. Without this, Windows may virtualize/scale the
  client rect for a DPI-unaware process, making even the "framebuffer"
  value inaccurate for future pixel-precise rendering. Exact API call is
  an Implementation Detail (Windows offers several DPI-awareness
  mechanisms — manifest-based and API-based); this plan specifies the
  *requirement* (must be configured before window creation), not which
  mechanism, since either is compatible with everything else here.
- **The two fields remain textually and structurally distinct in the
  API** (`WindowResize` has two separately-named `WindowExtent` fields)
  — Phase 1's *values* happen to match; the *shape* never collapsed them
  into one. A future DPI-aware refinement (computing genuinely different
  logical-vs-physical values, e.g. dividing by the monitor's DPI scale
  factor for `logical`) is a pure implementation change inside
  `windows_platform.cpp`, with **zero interface break** — no consumer
  code needs to change.
- **Zero framebuffer extent remains the presentation/swapchain
  suspension signal**, unchanged: Runtime must not attempt to create or
  recreate a graphics surface/swapchain while `framebuffer.isZero()` —
  this applies regardless of whether `logical` and `framebuffer` are
  equal or (in a future refinement) different.

**Verification added:** the Windows smoke test suite (Section 8) includes
a resize case (program-driven `SetWindowPos` to a known non-zero size,
asserting a matching `WindowResize` with equal, non-zero `logical`/
`framebuffer`) and a minimize case (`ShowWindow(hwnd, SW_MINIMIZE)`,
asserting `WindowResize{ {0,0}, {0,0} }`).

## 8. Error Handling

- `initialize()`'s only fallible path (window class registration,
  window creation) returns `atlantis::Result<void, PlatformError>` — no
  exception is thrown anywhere in `src/platform`, per AGENTS.md's
  exception-free-core-modules rule and ADR-0009.
- Programmer errors — calling `processEvents()`/`shouldQuit()` before a
  successful `initialize()`, or calling `shutdown()` when not currently
  initialized (including a second `shutdown()` call) — use
  `ATLANTIS_CHECK` (always-on, per ADR-0009), reusing `Atlantis Core`'s
  existing `atlantis/assert.h` exactly as `src/core` already does. Calling
  `processEvents()`/`shouldQuit()` **after** `shutdown()` is explicitly
  well-defined, not a programmer error (Section 6) — needed precisely so
  the final `{SurfaceDestroyed, Quit}` batch remains observable.

## 9. Threading

Per Spec 0002's Threading section and ADR-0004's Phase 1 baseline:
**every Platform operation in this plan — `initialize`, `processEvents`,
`shouldQuit`, `shutdown`, and reading any `NativeWindowHandle` or
`PlatformEvent` — is application-thread-only.** No background thread is
introduced anywhere in `src/platform`; the Win32 message pump runs
synchronously inside `processEvents()`, called by Runtime (in this plan,
the demo's `main()`) on the same thread that called `initialize()`.

## 10. Demo — `examples/platform_demo/`

Mirrors `examples/foundation_demo/`'s CMake/structure pattern exactly
(new sibling directory, not a modification of that existing demo).
`main.cpp`:

```cpp
#include <atlantis/log.h>
#include <atlantis/platform/platform.h>

int main() {
  auto result = atlantis::platform::initialize();
  ATLANTIS_CHECK(result.isOk());

  while (!atlantis::platform::shouldQuit()) {
    bool closeRequested = false;
    for (const auto& event : atlantis::platform::processEvents()) {
      if (std::holds_alternative<atlantis::platform::WindowCloseRequested>(event)) {
        closeRequested = true;
      }
      // ATLANTIS_LOG_INFO(...) per event kind, elsewhere
    }
    if (closeRequested) {
      atlantis::platform::shutdown();  // called only after the batch
                                        // iteration above has finished,
                                        // per the span-lifetime rule
    }
  }

  return 0;
}
```

Note `shutdown()` is called **after** the `for` loop over the current
`processEvents()` span completes, never from inside it — calling it
mid-iteration would invalidate the span being iterated (Section 4).

Proves, per this task's explicit demo requirements: initialize; create a
real Windows window; process events non-blockingly; log `WindowResize`
(drag-resize and minimize), `FocusGained`/`FocusLost` (alt-tab away and
back), `WindowCloseRequested`/`Quit` (closing the window, via the
corrected request/act-on-it flow above); log elapsed time via
`monotonicNow()` each loop iteration to demonstrate monotonic timing;
terminate cleanly via `shutdown()`. **No Vulkan rendering** — the window
is otherwise blank.

## 11. Testing

### Unit tests (`tests/platform/`, no live window, no GPU)

| File | Covers |
|---|---|
| `native_window_handle_tests.cpp` | `PlatformKind` tagging is preserved through construction/copy; payload fields round-trip via `reinterpret_cast` in a test-only fake pointer (no real `HWND` needed); copying a handle does not attempt any cleanup |
| `platform_event_tests.cpp` | `WindowExtent`'s `{0,0}` `isZero()`/equality/inequality; each `PlatformEvent` alternative constructs and is retrievable via `std::holds_alternative`/`std::get`; a `std::vector<PlatformEvent>` preserves insertion order |
| `clock_tests.cpp` | Two `monotonicNow()` calls with a short sleep between them satisfy `second >= first` (loose assertion, avoids flakiness) |

### Windows integration/smoke tests (`tests/platform/windows_platform_smoke_tests.cpp`, `[integration]` Catch2 tag, gated `#if defined(_WIN32)` — this file is Windows-only and, along with `windows_platform.cpp` itself, is one of the only two files in the repository allowed to include `<windows.h>`)

Drives a real (created and torn down entirely within the test process,
never shown interactively) window via `initialize()`, obtains the `HWND`
from the `SurfaceCreated` event, then synthesizes Win32 messages via
`SendMessage`/`PostMessage`/direct API calls to verify, per this task's
explicit final-verification requirements:

1. **Request-not-destruction:** `SendMessage(hwnd, WM_CLOSE, 0, 0)` →
   `processEvents()` returns exactly `[WindowCloseRequested]`; the `HWND`
   is still valid (e.g. `IsWindow(hwnd)` true); `shouldQuit()` is still
   `false`.
2. **Duplicate-request tolerance:** send `WM_CLOSE` twice before draining
   → `processEvents()` returns `[WindowCloseRequested, WindowCloseRequested]`
   (documenting, not preventing, this case); window still valid.
3. **Close/destroy/quit ordering:** call `shutdown()` → call
   `processEvents()` once more → returns exactly `[SurfaceDestroyed,
   Quit]`, in that order; `shouldQuit()` now `true`; a further
   `processEvents()` call returns an empty span.
4. **Resize:** `SetWindowPos` to a known non-zero size → `processEvents()`
   includes a `WindowResize` with matching, equal, non-zero `logical`/
   `framebuffer`.
5. **Minimize:** `ShowWindow(hwnd, SW_MINIMIZE)` → `processEvents()`
   includes `WindowResize{ {0,0}, {0,0} }`.
6. **Focus:** synthetic focus change → `FocusGained`/`FocusLost` observed.

No image regression, no Vulkan Validation Layers — not applicable, per
Spec 0002's Non-Goals.

## 12. Build Matrix

- **Windows Debug** — full build + `ctest` run (unit + smoke tests).
- **Windows Release** — full build + `ctest` run.
- **Android** — not added. This repository has no Android NDK toolchain
  infrastructure yet (per `specs/0001-project-foundation.md`'s Non-Goals,
  still true), so Android is correctly excluded per this task's own
  instruction ("must not be added ... unless the repository already has
  the necessary toolchain infrastructure" — it does not).
- **Linux** — not added, per AGENTS.md Phase 1 constraints.

## 13. Implementation Order

1. Platform public API — the 5 headers in Section 2/2a (declarations/
   types only). CMake target `atlantis_platform` created and builds.
2. `cmake/AtlantisDependencies.cmake` — shared Catch2 fetch; root
   `CMakeLists.txt` updated to include it once; `tests/core/CMakeLists.txt`
   updated to drop its own `FetchContent_Declare` (Section 3).
3. `NativeWindowHandle` — confirm the type against ADR-0011;
   `native_window_handle_tests.cpp` wired into the new
   `tests/platform` target.
4. `PlatformEvent` (incl. `WindowExtent`) — `platform_event_tests.cpp`.
5. `clock.cpp` + `clock_tests.cpp` — portable, no Windows dependency.
6. Windows Platform implementation — `windows_platform.cpp`: DPI
   awareness, window class registration, `HWND`/`HINSTANCE` creation,
   `initialize()`, internal `eventBuffer_`, `currentPlatform()`.
7. Windows message translation — `WM_SIZE`/`WM_SETFOCUS`/`WM_KILLFOCUS`/
   `WM_CLOSE` handling (Section 6); `processEvents()`'s drain loop
   returning `std::span` (Section 4).
8. Window lifetime — `shutdown()`'s corrected `DestroyWindow` ->
   `WM_DESTROY` -> `{SurfaceDestroyed, Quit}` path (Section 6); `SurfaceCreated`
   synthesis at `initialize()` success.
9. Tests — `windows_platform_smoke_tests.cpp` (needs steps 6–8 complete).
10. `examples/platform_demo/` — needs the full Windows implementation,
    including the corrected close-handling policy (Section 10).
11. Debug/Release verification — configure, build both configurations,
    run `ctest`, run the demo interactively (resize/minimize/focus/close
    it), confirm zero warnings.

### Sequencing & Dependencies

- Step 2 (shared Catch2 setup) must land before step 3 needs a working
  `tests/platform` target, but has **no ordering relationship with
  `tests/core`** beyond both depending on the root having included
  `cmake/AtlantisDependencies.cmake` once — this removes the prior
  draft's directory-order constraint entirely.
- Steps 1, 3, 4, 5 have no Windows dependency and can proceed in any
  relative order once step 1's headers exist.
- Steps 6–8 are strictly sequential (each depends on the previous).
- Step 9 depends on 6–8. Step 10 depends on 6–8 (ordered after 9 here to
  demo a tested implementation, not a hard dependency on it). Step 11
  depends on everything.

## 14. Acceptance Criteria Mapping

| Spec 0002 Criterion | Implementation Step | Verification |
|---|---|---|
| Platform's public headers contain no `Vk*` type / no `#include <vulkan/...>` | Steps 1, 6–8 (no file in `src/platform` ever includes a Vulkan header) | Manual repo scan (grep) across `src/platform` |
| No Renderer-level code includes `<windows.h>`/`<android/native_window.h>`/references `HWND`/`ANativeWindow`/`HINSTANCE` | N/A to implement (no Renderer exists) — this plan must not violate it | Grep confirms no such reference anywhere outside `src/platform/src/windows/` and the one gated smoke-test file |
| Windows native window ownership stated unambiguously | Already satisfied by the Spec/ADR-0013; Step 6/8 implements it exactly (Platform creates/owns/destroys `HWND`; Vulkan WSI never involved) | Code review against ADR-0013's table; smoke test #1/#3 confirm request-vs-destruction semantics |
| Android `ANativeWindow` lifetime stated unambiguously | **N/A for this plan** — Android is not implemented; already satisfied at the documentation level by the approved Spec/ADR-0013 | Not re-verified here; deferred to a future Android implementation plan |
| `WindowExtent`/`WindowResize` represent logical and framebuffer independently, including `{0,0}` | Step 4 (type), Step 7 (`WM_SIZE`/`SIZE_MINIMIZED`/DPI handling) | `platform_event_tests.cpp` (unit); smoke tests #4/#5 (resize/minimize) |
| Windows and Android both satisfiable by the same `initialize`/`processEvents`/`shouldQuit`/`shutdown` interface | Step 1 (OS-neutral header, no `#ifdef` in the declaration) | Grep confirms `platform.h` contains no Windows/Android type or macro; Android's own satisfiability is a documentation claim already established by ADR-0012, not re-verified by this plan |
| `NativeWindowHandle` passable Platform → Runtime → Vulkan Backend's private WSI without generic RHI/Renderer including an OS SDK header | Step 1 (opaque `void*` payload design), Step 6 (Windows payload population) | `native_window_handle_tests.cpp`; grep confirms `native_window_handle.h` includes no OS header |
| Platform-specific code isolated to `src/platform/src/windows/` (and, when it exists, `.../android/`) — none in `include/atlantis/platform/*.h` | Steps 1, 6–8 (all Win32 types/macros confined to `windows_platform.cpp`) | Grep across `include/atlantis/platform/*.h` for `HWND`/`windows.h`/etc. — must be empty |
| `PlatformKind` includes `IOS`; design doesn't assume exactly two platforms | Step 1 (`platform_kind.h` enum) | Code review — trivial, no test needed beyond compiling |
| No `src/platform/src/linux/`, no `PlatformKind` Linux value, no Linux build config | Steps 1–13 (never introduced) | Grep/directory listing confirms absence |

Every Spec 0002 acceptance criterion is mapped; none paraphrased away.
Two (Android lifetime, Windows/Android joint satisfiability's Android
half) remain explicitly marked as not independently re-verified by this
plan, unchanged from the prior draft.

## 15. Non-Goals (explicitly confirmed)

This Plan does **not** implement: Vulkan; Vulkan WSI; RHI; RenderGraph;
Renderer; Android implementation; iOS implementation; Linux support; an
input system; headless rendering; image regression; neural rendering;
3D Gaussian Splatting; world-model integration. All remain future work,
gated behind their own specs. This revision adds no new non-goal and
removes none.

## Rollback Plan

Purely additive: a new module (`src/platform`), its tests, its demo, and
one shared CMake dependency file. Reverting the implementing PR removes
`src/platform/`, `cmake/AtlantisDependencies.cmake`, `tests/platform/`,
`examples/platform_demo/`, restores `tests/core/CMakeLists.txt`'s inline
`FetchContent` block, and restores the four documentation touch-ups —
returning the repository to its post-Spec-0001 state.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan: "Vulkan Validation Layers" and "image
regression" items are not applicable (no rendering exists). Add: all
grep-based acceptance-criteria checks in Section 14 pass; the demo has
been run interactively at least once (resize, minimize, focus change,
close) as part of verification; the corrected close/destroy/quit ordering
is exercised by the smoke tests, not just asserted in prose.

## Unresolved Implementation Details (flagged, not architectural)

None of these change an approved boundary; each is called out per this
task's instruction rather than decided silently:

1. **Timing placement** (Platform vs. Core) — resolved here as "stays in
   Platform" (Section 5); reversible later without an interface change.
2. **`PlatformEvent`'s concrete C++ representation** — resolved here as
   `std::variant` over plain structs (Section 2); ADR-0011 left this
   explicitly open.
3. **`processEvents()`'s exact return shape** — resolved here as
   `std::span<const PlatformEvent>` over a reused internal
   `std::vector` (Section 4), per this revision's correction; satisfies
   "no callback/observer architecture" and "no required per-frame
   allocation."
4. **`PlatformError`'s exact fields** — resolved here as an error-code
   enum plus a raw native error code integer (Section 2); no ADR fixed
   this shape.
5. **Windows logical-vs-framebuffer extent equality** — resolved here as
   "report them equal on Windows for Phase 1, with DPI awareness
   configured before window creation so the shared value is at least
   accurate" (Section 7); Spec 0002 permits but does not require Windows
   to differ them; the exact DPI-awareness API call is left to
   implementation (manifest vs. `SetProcessDpiAwarenessContext`).
6. **`WM_CLOSE`/`shutdown()` policy timing** — this plan's demo always
   honors a close request immediately after the batch containing it; a
   future, richer Runtime could defer or cancel that decision (e.g. an
   "unsaved changes" prompt) without any change to Platform's event
   vocabulary or `shutdown()` contract.
7. **`processEvents()`/`shouldQuit()` remaining legal after `shutdown()`**
   — resolved here as "yes, explicitly, to allow draining the final
   `{SurfaceDestroyed, Quit}` batch" (Section 8); re-initialization after
   `shutdown()` is simply unsupported/undesigned in Phase 1, not an
   error condition with its own guard.

None of these seven required a new ADR: each is a concrete expression of
a decision the cited ADR/Spec already made in principle while explicitly
leaving the exact mechanism to "the Plan stage."

## Final Consistency Review

1. **Consistency with Spec 0002:** every Requirements subsection has a
   corresponding implementation section above; no `PlatformWindow`
   reintroduced.
2. **Consistency with ADR-0011:** `NativeWindowHandle`'s shape is copied
   verbatim from the ADR's Decision; borrowed/non-owning,
   validity-via-events, API-visibility rules all preserved.
3. **Consistency with ADR-0012:** the lifecycle interface and event
   categories match exactly; Windows synthesizes only the events this
   task's instruction lists; no `ApplicationPause`/`ApplicationResume` on
   Windows; `Quit` now precisely represents post-decision termination
   state, not a conflated close-and-destroy event.
4. **Consistency with amended ADR-0005:** no Vulkan header or type
   anywhere in `src/platform`; Vulkan WSI explicitly out of scope.
5. **Consistency with ADR-0010:** `atlantis_platform`/`Atlantis::Platform`
   naming, `include/atlantis/platform/...` public path, `atlantis::platform`
   namespace — the same pattern `atlantis_core`/`Atlantis::Core`
   established.
6. **No `PlatformWindow` abstraction reintroduced:** confirmed.
7. **No Vulkan dependency entered Platform:** confirmed — Section 6
   states this explicitly.
8. **No generic RHI dependency on Platform introduced:** trivially true
   — this plan builds no RHI code at all.
9. **No Android implementation accidentally scheduled:** confirmed —
   Section 12's build matrix explicitly excludes Android with its
   rationale.
10. **All Spec 0002 acceptance criteria mapped:** all nine, in Section 14,
    two explicitly marked as not independently re-verified here.

All ten checks pass. No new architectural decision was introduced; all
seven items in "Unresolved Implementation Details" are concrete
expressions of decisions the cited ADRs/Spec already made while
deferring the exact mechanism to this Plan.
