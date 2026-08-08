# tests/

**`core/`** — unit tests for Atlantis Core, per
[specs/0001-project-foundation.md](../specs/0001-project-foundation.md),
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md),
and [ADR-0007](../adr/0007-test-framework.md) (Catch2 v3, fetched via
CMake `FetchContent` per [ADR-0006](../adr/0006-dependency-management.md)).
Run via `ctest --test-dir build -C Debug -LE gpu --output-on-failure`
(GPU-independent; see below for why a bare `ctest` is not recommended
once `gpu`-labeled tests exist in the same build), or by invoking
`atlantis_core_tests` directly.

**`platform/`** — tests for Atlantis Platform, per
[specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)
and [plans/0002-platform-foundation.md](../plans/0002-platform-foundation.md),
built as the `atlantis_platform_tests` Catch2 v3 executable:
- Portable, no-GPU, no-live-window unit tests (`NativeWindowHandle`,
  `PlatformEvent`, `WindowExtent`, the monotonic clock) plus a compile/
  link check of the public Platform API.
- `windows_platform_smoke_tests.cpp` — Windows-only (`#if defined(_WIN32)`)
  integration tests, tagged `[integration]`, that drive a real (created
  and destroyed within the test process) window through
  `initialize()`/`processEvents()`/`shutdown()` to verify `SurfaceCreated`
  ordering, close-request/duplicate-close handling, resize, minimize,
  focus, and shutdown ordering. This is one of a small, fixed set of
  files across the repository permitted to include `<windows.h>`, each
  confined to its own `#if defined(_WIN32)` block:
  `src/platform/src/windows/windows_platform.cpp`,
  `src/vulkan_backend/src/wsi/win32_surface.cpp` (the private Vulkan WSI
  boundary), and `tests/vulkan_backend/vulkan_presentation_gpu_tests.cpp`
  (below) — a separate, explicit Windows test boundary covering RHI/
  Vulkan Backend's `Presentation`, distinct from this file's own
  Platform-lifecycle coverage.

Run via `ctest --test-dir build -C Debug -LE gpu --output-on-failure`
(GPU-independent — Core and Platform have no GPU-required tests, but this
excludes the unrelated `atlantis_vulkan_backend_gpu_tests` cases
registered elsewhere in the same build), or by invoking
`atlantis_platform_tests` directly (optionally filtered, e.g.
`atlantis_platform_tests "[integration]"`).

**`rhi/`** — unit tests for Atlantis RHI, per
[specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md)
and [plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md),
built as the `atlantis_rhi_tests` Catch2 v3 executable. GPU-independent:
covers RHI's value types (`Extent2D`, `Format`, `SwapchainMetadata`,
`PresentationError`) — defaults, equality, and construction. No Vulkan
device or window is required.

**`vulkan_backend/`** — tests for Atlantis Vulkan Backend, per the same
spec/plan, built as two entirely separate executables:
- `atlantis_vulkan_backend_tests` — GPU-independent, carries no CTest
  `gpu` label. Covers `VkResult` → RHI/Vulkan Backend error mapping,
  `Presentation`'s pure recreation-decision logic (the zero-extent-skip/
  no-op/recreate dispatch, the concrete-surface support check, format and
  image-usage selection), and the validation-policy pure functions
  (effective enablement, severity classification, null-safe message
  selection). No Vulkan device or window is required.
- `atlantis_vulkan_backend_gpu_tests` — GPU-required, Windows-only
  integration executable; every test case carries the CTest label `gpu`.
  Drives a real Atlantis Platform window, a real Vulkan `Device`, and
  real `Presentation` instances entirely through Atlantis's public API
  (no Vulkan header, no direct Vulkan call anywhere in this file) to
  cover: surface-only construction/destruction, the initial zero-extent
  structural skip, first swapchain creation, repeated (idempotent)
  recreation, minimize/restore-driven recreation, and destruction at
  each of those points. Validation Layers are explicitly enabled; the
  process aborts on any WARNING/ERROR (see
  `src/vulkan_backend/src/validation.cpp`), so a normal exit is itself
  the validation-clean signal — no acquire, present, or GPU command of
  any kind is issued anywhere in this executable.

Run the GPU-independent suite, which excludes
`atlantis_vulkan_backend_gpu_tests`:
```
ctest --test-dir build -C Debug -LE gpu --output-on-failure
```
Run the GPU-required suite, which needs a real, Vulkan-capable Windows
machine (replace `Debug` with `Release` for a Release build):
```
ctest --test-dir build -C Debug -L gpu --output-on-failure
```
A bare `ctest` runs every registered test regardless of label, including
the GPU-required ones — prefer the explicit `-LE gpu`/`-L gpu` commands
above over a bare invocation.

Headless integration and image-regression test layers (see
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md))
are not implemented — they're blocked on headless rendering, which
doesn't exist yet. Their concrete directory structure and test framework
choice (if different from Catch2) will be established by the spec that
introduces that harness, not invented ahead of time. No GPU-touching CI
exists yet either; the `gpu` CTest label is preparatory infrastructure a
future CI job would opt into explicitly, not a currently-running pipeline.
See [AGENTS.md](../AGENTS.md).

Do not add test files for other modules here without a linked spec and
plan.
