# Plan: Atlantis Platform Foundation (Windows)

- **Spec:** [Spec 0002](../specs/0002-platform-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction; approved at
  Human Review on 2026-08-03 (final review: PASS) after the corrections below
  were applied.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  Batch 1 PR (pending). Original scope,
  ordered work, and verification retained.

Review corrections folded into the text below: `WM_CLOSE`/`WM_DESTROY`/`Quit`
ordering; `processEvents()` returning a reused-buffer `std::span` instead of a
per-frame `std::vector`; removal of the `tests/core` → `tests/platform` CMake
ordering dependency; Windows DPI/extent assumptions; consolidation of the
public header set from 7 to 5. None is a new architectural decision.

This Plan implements the **Windows** portion of Spec 0002 only. Android is
architecturally specified ([ADR-0012](../adr/0012-application-lifecycle-and-event-model.md),
[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)) but not
implemented here, except where a platform-neutral interface shape must be
defined now so Windows and a future Android implementation share it without
rework. iOS is architecture-only. Linux is not a target platform.

## Objective

Implement `Atlantis Platform`'s Windows path: application lifecycle, native
window (`HWND`) creation/ownership/destruction, `NativeWindowHandle`,
`PlatformEvent` delivery, window extent tracking, monotonic timing, and a
proof-of-build demo — per [Spec 0002](../specs/0002-platform-foundation.md) and
ADR-0005 (amended), 0010, 0011, 0012, 0013.

## Architectural boundaries (preserved, not re-decided)

```
Platform -> PlatformEvent -> Runtime -> Runtime/Vulkan integration
  (Presentation orchestration) -> private Vulkan WSI -> VkSurfaceKHR
  -> Presentation / generic RHI
```

- Platform does not depend on Vulkan and includes no Vulkan header.
- Generic RHI does not depend on Platform, consume `PlatformEvent`, or consume
  `NativeWindowHandle`. Renderer and RenderGraph remain platform-independent.
  (Not built by this Plan — stated as constraints it must not violate.)
- `NativeWindowHandle` is opaque, tagged, borrowed, non-owning
  ([ADR-0011](../adr/0011-native-window-handle-representation.md)).
- Vulkan WSI is entirely out of scope; nothing here creates, references, or
  prepares a `VkSurfaceKHR`.

## 1. Files

**Create:**

```
src/platform/
  CMakeLists.txt
  include/atlantis/platform/  platform_kind.h  native_window_handle.h
                              platform_event.h  clock.h  platform.h
  src/clock.cpp
  src/windows/windows_platform.cpp

cmake/AtlantisDependencies.cmake

tests/platform/
  CMakeLists.txt
  native_window_handle_tests.cpp  platform_event_tests.cpp
  clock_tests.cpp  windows_platform_smoke_tests.cpp

examples/platform_demo/  CMakeLists.txt  main.cpp
```

**Modify:** root `CMakeLists.txt` (add the three `add_subdirectory` calls;
`include(cmake/AtlantisDependencies.cmake)` once, before either `tests/`
subdirectory); `tests/core/CMakeLists.txt` (drop its embedded
`FetchContent_Declare(Catch2)` block, link `Catch2::Catch2WithMain` directly —
a pure build-file reorganization, no test behavior change); `README.md`,
`src/README.md`, `tests/README.md`, `examples/README.md` (add the new
module/test/demo entries).

No file under `src/core`, `examples/foundation_demo`, or `tests/core` has its
behavior changed. No RHI/Renderer/RenderGraph/Vulkan Backend directory is
created.

**Conventions** (per [ADR-0010](../adr/0010-cmake-structure.md), not a new
scheme): CMake target `atlantis_platform` (static), alias `Atlantis::Platform`;
public headers under `include/atlantis/platform/*.h`, private implementation
under `src/` and `src/windows/` (CMake-gated); namespace `atlantis::platform`
(a peer subsystem namespace, unlike Core's bare `atlantis::`); test target
`atlantis_platform_tests`; example `atlantis_platform_demo`.

## 2. Public types (5 headers)

These fix the shapes ADR-0011/0012 left to the Plan stage — Implementation
Details per those ADRs' own Open Questions, changing no approved boundary:

- **`platform_kind.h`** — `enum class PlatformKind { Windows, Android, IOS }`
  (IOS: future, no implementation).
- **`native_window_handle.h`** — `NativeWindowHandle` exactly as
  [ADR-0011](../adr/0011-native-window-handle-representation.md)'s Decision:
  `{ PlatformKind kind; void* value0; void* value1; }`, borrowed / non-owning /
  no destructor. Windows convention: `value0` = `HWND`, `value1` = `HINSTANCE`,
  via `reinterpret_cast`. No `PlatformWindow` type anywhere.
- **`platform_event.h`** — `PlatformEvent` as a `std::variant` over plain
  structs (`WindowResize`, `WindowCloseRequested`, `FocusGained`, `FocusLost`,
  `ApplicationPause`, `ApplicationResume`, `SurfaceCreated`, `SurfaceDestroyed`,
  `Quit`), consistent with `atlantis::Result`'s value-type style. Also holds
  `WindowExtent { unsigned width, height; isZero(); operator== }` — its only
  consumer is `WindowResize`.
- **`clock.h`** — `using TimePoint = std::chrono::steady_clock::time_point;`
  and `TimePoint monotonicNow();`.
- **`platform.h`** — the lifecycle interface (`initialize()` returning
  `Result<void, PlatformError>`; `processEvents()` returning
  `std::span<const PlatformEvent>` — see Section 4; `shouldQuit()`;
  `shutdown()`; `currentPlatform()`). Also holds `PlatformError`
  (`{ PlatformErrorCode code; unsigned long nativeErrorCode; }` — a plain
  integer, no OS header) since `initialize()` is its only consumer. Identical
  text on every target OS; no Windows/Android header.

**Header count.** The former `window_extent.h` and `platform_error.h` are
merged into their single consumer's header rather than kept for symmetry — 5
public headers, not 7. No architecture changes; only which file a declaration
lives in.

## 3. Shared test dependency setup

New `cmake/AtlantisDependencies.cmake` (per
[ADR-0006](../adr/0006-dependency-management.md)'s `FetchContent`/pinned
decision and [ADR-0007](../adr/0007-test-framework.md)'s Catch2 v3 decision —
relocated declaration, not a new one): `include_guard(GLOBAL)`, one pinned
`FetchContent_Declare(Catch2)` + `FetchContent_MakeAvailable`, then
`include(Catch)`. The root `CMakeLists.txt` includes it **once**, before
`add_subdirectory`-ing either `tests/core` or `tests/platform`; each test
directory links `Catch2::Catch2WithMain` and calls `catch_discover_tests` with
**no `FetchContent_Declare` of its own**. `add_subdirectory` order between the
two test directories is no longer significant.

## 4. `processEvents()` — reused buffer, no required per-frame allocation

`std::span<const PlatformEvent> processEvents();`, chosen over a
`std::vector<PlatformEvent>` return. Windows Platform holds one persistent
internal buffer (function-local static in `windows_platform.cpp`, mirroring
`log.cpp`'s pattern). Each call: `clear()` (retains capacity), runs the
non-blocking `PeekMessageW(..., PM_REMOVE)` drain loop, `push_back`s one
`PlatformEvent` per relevant message, returns a span over the buffer. After
capacity stabilizes (within a few frames), subsequent calls allocate nothing.

**Lifetime rule** (documented at the call site, honored by every consumer): the
returned span is valid only until the **next** `processEvents()` **or**
`shutdown()` call (`shutdown()` also refills the same buffer — Section 6).
Callers copy out what they need to retain; they never retain the span. A
hand-rolled fixed-capacity array or ring buffer would add complexity
unjustified by Phase 1's single-digit per-frame event counts.

Unit tests use an ordinary local `std::vector<PlatformEvent>` fixture,
unaffected by the internal buffer; smoke tests consume the span directly.

## 5. Timing

Implemented **inside Platform** (`src/platform/src/clock.cpp`), one portable TU
compiled on every target (not `if(WIN32)`-gated), wrapping
`std::chrono::steady_clock::now()`. Resolves Spec 0002's own Open Question
("Platform vs. Core"): keeping it in Platform avoids touching the
already-implemented Core module for a Spec-0002 concern; relocating it to Core
later is a non-breaking move since `monotonicNow()`'s signature would not
change.

## 6. Windows implementation — `windows_platform.cpp`

**`HWND` lifecycle** (per
[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) — Platform
creates, owns, destroys): `initialize()` configures DPI awareness (Section 7)
**before** window creation, `RegisterClassExW` a private class,
`CreateWindowExW` one top-level window, stores `HWND`/`HINSTANCE` in
file-local static state; on failure at either step returns
`Result::Err({WindowClassRegistrationFailed | WindowCreationFailed,
GetLastError()})`, never throws. On success it enqueues one `SurfaceCreated`
event, delivered via the *first* `processEvents()` call (single delivery
mechanism for every event). No Vulkan header, no `VkSurfaceKHR`.

**`WM_CLOSE` / `WM_DESTROY` / `Quit` ordering** (corrected — the prior draft had
`WM_CLOSE` call `DestroyWindow`, conflating a request with completed
destruction):

| Trigger | Event(s) enqueued | `shouldQuit()` |
|---|---|---|
| `WM_CLOSE` (any number of times) | one `WindowCloseRequested` per message; window stays fully valid; `WNDPROC` returns `0` without `DefWindowProc` and without `DestroyWindow` | unaffected |
| `shutdown()` (Runtime's decision) | clears `eventBuffer_`, then `DestroyWindow` synchronously dispatches `WM_DESTROY`, whose handler enqueues `SurfaceDestroyed` then `Quit` into the freshly-cleared buffer | becomes `true` |

- Runtime/application policy decides whether and when to honor a close request.
  This Plan's Phase 1 policy (in the demo, standing in for Runtime) is "honor
  it once, after finishing iteration over the batch that contained it" — not a
  cancel-close UI system.
- `shutdown()` is the only path that calls `DestroyWindow`. It
  `ATLANTIS_CHECK`s Platform is initialized and not already shut down, clears
  the buffer, destroys the window (→ `{SurfaceDestroyed, Quit}` + internal
  `shouldQuit_`), `UnregisterClassW`, and leaves re-initialization unsupported
  in Phase 1. **`processEvents()`/`shouldQuit()` remain legal after
  `shutdown()`** — needed to drain the final `{SurfaceDestroyed, Quit}` batch;
  once drained, `processEvents()` returns an empty span and `shouldQuit()`
  stays `true`.
- `{SurfaceDestroyed, Quit}` is enqueued at most once per Platform lifetime
  (single `shutdown()`, re-entry guarded). Multiple `WindowCloseRequested` are
  possible and not an error; Runtime's "act once" policy absorbs duplicates.

**Other message translation:**

| Win32 message | `PlatformEvent` |
|---|---|
| `WM_SIZE`, `SIZE_MINIMIZED` | `WindowResize{ {0,0}, {0,0} }` |
| `WM_SIZE`, otherwise | `WindowResize{ logical, framebuffer }` — both from `GetClientRect`, reported equal in Phase 1 (Section 7) |
| `WM_SETFOCUS` / `WM_KILLFOCUS` | `FocusGained` / `FocusLost` |

No `ApplicationPause`/`ApplicationResume` is synthesized on Windows.
`currentPlatform()` returns `PlatformKind::Windows` unconditionally (CMake
selects this TU). `#define WIN32_LEAN_AND_MEAN` before `<windows.h>` in this
file only.

## 7. Window extent — DPI

Phase 1 reports equal `logical` and `framebuffer` extents on Windows,
documented as a **limitation**, not a silent simplification:

- Both fields are populated from the same `GetClientRect` **pixel** dimensions
  — computed identically, not coincidentally equal.
- Atlantis **must configure DPI awareness before window creation** (e.g.
  `SetProcessDpiAwarenessContext(...PER_MONITOR_AWARE_V2)`); the exact
  mechanism (manifest vs. API) is an Implementation Detail — the Plan specifies
  the requirement, not the mechanism.
- The two fields stay textually and structurally distinct in the API; a future
  DPI-aware refinement computing genuinely different values is a pure
  implementation change inside `windows_platform.cpp` with zero interface
  break.
- **Zero framebuffer extent remains the presentation/swapchain suspension
  signal**, unchanged, regardless of whether the two values are equal.

The smoke suite adds a resize case (program-driven `SetWindowPos`, asserting a
matching non-zero `WindowResize`) and a minimize case (`SW_MINIMIZE`,
asserting `WindowResize{ {0,0}, {0,0} }`).

## 8. Error handling

`initialize()`'s only fallible path returns
`atlantis::Result<void, PlatformError>` — no exception anywhere in
`src/platform` ([ADR-0009](../adr/0009-assertion.md)). Programmer errors —
calling `processEvents()`/`shouldQuit()` before a successful `initialize()`, or
`shutdown()` when not initialized (including a second call) — use
`ATLANTIS_CHECK`, reusing Core's `atlantis/assert.h`. Calling
`processEvents()`/`shouldQuit()` **after** `shutdown()` is well-defined
(Section 6), not a programmer error.

## 9. Threading

Per Spec 0002's Threading section and
[ADR-0004](../adr/0004-phase1-threading-baseline.md): every Platform operation
— `initialize`, `processEvents`, `shouldQuit`, `shutdown`, and reading any
`NativeWindowHandle` or `PlatformEvent` — is application-thread-only. No
background thread; the Win32 message pump runs synchronously inside
`processEvents()`, on the thread that called `initialize()`.

## 10. Demo — `examples/platform_demo/`

Mirrors `examples/foundation_demo/`'s CMake/structure pattern (a new sibling
directory). `main()` initializes Platform, runs
`while (!shouldQuit()) { for (event : processEvents()) { ... } if (closeRequested) shutdown(); }`,
calling `shutdown()` **after** the batch loop completes (never mid-iteration —
that would invalidate the span). Proves: initialize; create a real Windows
window; process events non-blockingly; log `WindowResize` (drag-resize and
minimize), `FocusGained`/`FocusLost` (alt-tab), `WindowCloseRequested`/`Quit`;
log elapsed time via `monotonicNow()` each iteration; terminate cleanly via
`shutdown()`. No Vulkan rendering — the window is blank.

## 11. Testing

**Unit tests** (`tests/platform/`, no live window, no GPU):

| File | Covers |
|---|---|
| `native_window_handle_tests.cpp` | `PlatformKind` tag preserved through construction/copy; payload round-trips via `reinterpret_cast` on a test-only fake pointer; copying attempts no cleanup |
| `platform_event_tests.cpp` | `WindowExtent` `{0,0}`/equality; each `PlatformEvent` alternative constructs and is retrievable via `holds_alternative`/`get`; a `std::vector<PlatformEvent>` preserves insertion order |
| `clock_tests.cpp` | two `monotonicNow()` calls with a short sleep satisfy `second >= first` (loose, avoids flakiness) |

**Windows smoke tests** (`windows_platform_smoke_tests.cpp`, `[integration]`
tag, `#if defined(_WIN32)` — this file and `windows_platform.cpp` are the only
two in the repo allowed to include `<windows.h>`). Drives a real, never-shown
window via `initialize()`, obtains the `HWND` from `SurfaceCreated`, then
synthesizes Win32 messages to verify:

1. **Request, not destruction:** `WM_CLOSE` → exactly `[WindowCloseRequested]`;
   `IsWindow(hwnd)` true; `shouldQuit()` false.
2. **Duplicate-request tolerance:** two `WM_CLOSE` before draining →
   `[WindowCloseRequested, WindowCloseRequested]`; window still valid.
3. **Close/destroy/quit ordering:** `shutdown()` → next `processEvents()`
   returns exactly `[SurfaceDestroyed, Quit]`; `shouldQuit()` true; a further
   call returns an empty span.
4. **Resize:** `SetWindowPos` to a known non-zero size → a `WindowResize` with
   matching, equal, non-zero `logical`/`framebuffer`.
5. **Minimize:** `SW_MINIMIZE` → `WindowResize{ {0,0}, {0,0} }`.
6. **Focus:** synthetic focus change → `FocusGained`/`FocusLost`.

No image regression, no Vulkan Validation Layers (Spec 0002 Non-Goals).

## 12. Build matrix

- **Windows Debug / Release** — full build + `ctest` (unit + smoke).
- **Android** — not added; no NDK toolchain infrastructure exists.
- **Linux** — not added (AGENTS.md Phase 1).

## 13. Implementation order

1. Platform public API — the 5 headers (declarations/types only); `atlantis_platform` builds.
2. `cmake/AtlantisDependencies.cmake` — shared Catch2 fetch; root `CMakeLists.txt` includes it once; `tests/core/CMakeLists.txt` drops its own `FetchContent_Declare`.
3. `NativeWindowHandle` — confirm against ADR-0011; `native_window_handle_tests.cpp`.
4. `PlatformEvent` (incl. `WindowExtent`) — `platform_event_tests.cpp`.
5. `clock.cpp` + `clock_tests.cpp` — portable, no Windows dependency.
6. Windows Platform implementation — DPI awareness, window class registration, `HWND`/`HINSTANCE` creation, `initialize()`, internal buffer, `currentPlatform()`.
7. Windows message translation — `WM_SIZE`/`WM_SETFOCUS`/`WM_KILLFOCUS`/`WM_CLOSE` (Section 6); `processEvents()`'s drain loop returning `std::span` (Section 4).
8. Window lifetime — `shutdown()`'s `DestroyWindow` → `WM_DESTROY` → `{SurfaceDestroyed, Quit}` path; `SurfaceCreated` synthesis at `initialize()` success.
9. `windows_platform_smoke_tests.cpp` (needs steps 6–8).
10. `examples/platform_demo/` (needs the full Windows implementation).
11. Debug/Release verification — configure, build both, run `ctest`, run the demo interactively, confirm zero warnings.

**Sequencing:** step 2 has no ordering relationship with `tests/core` beyond
both depending on the root having included the shared file once. Steps 1, 3, 4,
5 have no Windows dependency. Steps 6–8 are strictly sequential. Step 9 depends
on 6–8; step 10 on 6–8; step 11 on everything.

## 14. Acceptance criteria mapping

| Spec 0002 Criterion | Step | Verification |
|---|---|---|
| Public headers contain no `Vk*` / `#include <vulkan/...>` | 1, 6–8 | grep across `src/platform` |
| No Renderer-level code includes `<windows.h>`/`<android/native_window.h>`/references `HWND`/`ANativeWindow`/`HINSTANCE` | N/A to implement (no Renderer) — must not violate | grep outside `src/platform/src/windows/` and the gated smoke-test file |
| Windows native window ownership stated unambiguously | Spec/ADR-0013; steps 6/8 | code review vs. ADR-0013; smoke tests 1/3 |
| Android `ANativeWindow` lifetime stated unambiguously | N/A for this Plan — documentation-level via Spec/ADR-0013 | deferred to a future Android plan |
| `WindowExtent`/`WindowResize` represent logical/framebuffer independently, incl. `{0,0}` | steps 4, 7 | `platform_event_tests.cpp`; smoke tests 4/5 |
| Windows and Android both satisfiable by one interface | step 1 (OS-neutral header) | grep `platform.h` for OS types/macros; Android's half is a documentation claim (ADR-0012), not re-verified here |
| `NativeWindowHandle` passable Platform → Runtime → Vulkan Backend WSI without an OS SDK header in RHI/Renderer | steps 1, 6 | `native_window_handle_tests.cpp`; grep the header for OS includes |
| Platform-specific code isolated to `src/platform/src/windows/` | steps 1, 6–8 | grep `include/atlantis/platform/*.h` — must be empty |
| `PlatformKind` includes `IOS`; design not limited to two platforms | step 1 | code review |
| No `src/platform/src/linux/`, no Linux `PlatformKind` value or build config | steps 1–13 | grep / directory listing |

Two criteria (Android lifetime; the Android half of joint satisfiability)
remain explicitly not independently re-verified by this Plan.

## 15. Non-Goals (confirmed)

Does not implement: Vulkan; Vulkan WSI; RHI; RenderGraph; Renderer; Android
implementation; iOS implementation; Linux support; an input system; headless
rendering; image regression; neural rendering; 3D Gaussian Splatting;
world-model integration. All remain future work behind their own Specs.

## Rollback Plan

Purely additive. Reverting the implementing PR removes `src/platform/`,
`cmake/AtlantisDependencies.cmake`, `tests/platform/`,
`examples/platform_demo/`, restores `tests/core/CMakeLists.txt`'s inline
`FetchContent` block, and restores the four documentation touch-ups —
returning the repository to its post-Spec-0001 state.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas: "Vulkan Validation Layers" and "image regression" are not applicable.
Add: the grep-based acceptance-criteria checks (Section 14) pass; the demo has
been run interactively at least once (resize, minimize, focus, close); the
corrected close/destroy/quit ordering is exercised by the smoke tests, not just
asserted.

## Unresolved implementation details (flagged, not architectural)

Each is a concrete expression of a decision the cited ADR/Spec already made
while leaving the exact mechanism to the Plan stage; none required a new ADR:

1. Timing placement (Platform vs. Core) — stays in Platform (Section 5);
   reversible without an interface change.
2. `PlatformEvent`'s concrete C++ representation — `std::variant` over plain
   structs (Section 2).
3. `processEvents()`'s return shape — `std::span<const PlatformEvent>` over a
   reused internal `std::vector` (Section 4).
4. `PlatformError`'s fields — an error-code enum plus a raw native error
   integer (Section 2).
5. Windows logical-vs-framebuffer extent equality — reported equal for Phase 1
   with DPI awareness configured before window creation (Section 7); the exact
   DPI-awareness API call is left to implementation.
6. `WM_CLOSE`/`shutdown()` policy timing — the demo honors a close request
   immediately after the batch containing it; a richer Runtime could defer or
   cancel without any change to Platform's event vocabulary or `shutdown()`
   contract (Section 6).
7. `processEvents()`/`shouldQuit()` remaining legal after `shutdown()` — yes,
   explicitly, to drain the final `{SurfaceDestroyed, Quit}` batch (Section 8);
   re-initialization after `shutdown()` is unsupported/undesigned in Phase 1,
   not an error with its own guard.
