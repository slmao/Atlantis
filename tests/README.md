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
[specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md)/[plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md),
[specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)/[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md),
and
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)/[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md),
built as the `atlantis_rhi_tests` Catch2 v3 executable. GPU-independent:
covers RHI's value types (`Extent2D`, `Format`, `SwapchainMetadata`,
`PresentationError`, `ResourceState`, `ClearColorValue`) — defaults,
equality, and construction — plus `Buffer`/`Texture`/`Pipeline`
construction-parameter validation logic that does not require a real
Vulkan device (`buffer_texture_pipeline_tests.cpp`, Spec 0007). No Vulkan
device or window is required.

**`vulkan_backend/`** — tests for Atlantis Vulkan Backend, per the same
specs/plans (0003 and 0006), built as two entirely separate executables:
- `atlantis_vulkan_backend_tests` — GPU-independent, carries no CTest
  `gpu` label. Covers `VkResult` → RHI/Vulkan Backend error mapping,
  `Presentation`'s pure recreation-decision logic (the zero-extent-skip/
  no-op/recreate dispatch, the concrete-surface support check, format,
  image-usage, and clear-color-image-usage selection), and the
  validation-policy pure functions (effective enablement, severity
  classification, null-safe message selection). No Vulkan device or
  window is required.
- `atlantis_vulkan_backend_gpu_tests` — GPU-required, Windows-only
  integration executable; every test case carries the CTest label `gpu`.
  Drives a real Atlantis Platform window, a real Vulkan `Device`, and
  real `Presentation` instances entirely through Atlantis's public API
  (no Vulkan header, no direct Vulkan call anywhere in this file) to
  cover: surface-only construction/destruction, the initial zero-extent
  structural skip, first swapchain creation, repeated (idempotent)
  recreation, minimize/restore-driven recreation, and destruction at
  each of those points (`vulkan_presentation_gpu_tests.cpp`); and the
  full acquire→execute→submit→present frame-execution state machine,
  including resize, minimize/restore, and cleanup
  (`frame_execution_gpu_tests.cpp`, Spec 0006); and real
  `Buffer`/`Texture`/`Pipeline`/`CommandList` construction and a real,
  multi-`DrawItem` `Renderer::drawFrame()` draw path against a real
  acquired `RenderTarget` and depth `Texture`
  (`minimal_renderer_gpu_tests.cpp`, Spec 0007), including dynamic-
  rendering capability-detection coverage for whichever Core/Extension
  path the test machine's own hardware/driver resolves to — the other
  path and the explicit-error case remain verified by code inspection
  and GPU-independent truth-table tests only, per Spec 0007's own stated
  limitation (no second GPU/driver combination available in this
  environment); and a real headless render-and-readback cycle through a
  real `OffscreenTarget` (`headless_rendering_gpu_tests.cpp`, Spec 0010)
  — no window, no `Presentation`, no `VkSwapchainKHR`/`VkSurfaceKHR`
  anywhere, sharing the same `Renderer`/RenderGraph/RHI/Vulkan Backend
  stack the windowed path above uses. Validation Layers are explicitly
  enabled; the process aborts on any WARNING/ERROR (see
  `src/vulkan_backend/src/validation.cpp`), so a normal exit is itself
  the validation-clean signal.

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

**`render_graph/`** — unit tests for Atlantis RenderGraph, per
[specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md)/[plans/0005-render-graph-foundation.md](../plans/0005-render-graph-foundation.md),
[specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)/[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md),
and
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)/[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md),
built as the `atlantis_render_graph_tests` Catch2 v3 executable.
Entirely GPU-independent — no test in this executable carries the CTest
`gpu` label; a fake, non-Vulkan `CommandList` (`fake_command_list.h`)
exercises `execute()`'s logic without a real device. Covers
`RenderGraphBuilder`/`CompiledGraph` handle and ownership contracts, the
dependency-derivation and compile algorithm (including its white-box
`detail::compile()` layer), cycle detection, pass retention/ordering,
`CompiledGraph` lifetime/move semantics, `execute()`'s two guard checks
(unbound tagged usage; a bound `RenderTarget` with a declared read usage)
plus its transition-insertion and pass-execution behavior, and (Spec
0007, `attachment_execution_tests.cpp`) the generalized multi-binding
transition bookkeeping across a simultaneously bound color `RenderTarget`
and depth `Texture`, draw-pass recognition and attachment-scoping
insertion, and confirming a `ColorAttachmentWrite`-only pass (Spec 0006's
existing `clearColor()` shape) is never treated as a draw pass. No
Vulkan device or window is required. Run via
`ctest --test-dir build -C Debug -LE gpu --output-on-failure`, the same
GPU-independent command as every other suite in this directory.

**`renderer/`** — unit tests for Atlantis Renderer, per
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)/[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md),
built as the `atlantis_renderer_tests` Catch2 v3 executable
(`renderer_ownership_tests.cpp`). Entirely GPU-independent — compile-time
and fake-`CommandList` checks that `Renderer` retains no GPU resource or
frame-to-frame state across calls, and that `Mesh`/`Material` are never
created, cached, or looked up by `Renderer` itself. Run via
`ctest --test-dir build -C Debug -LE gpu --output-on-failure`.

**`asset_system/`** — unit tests for Atlantis Asset System, per
[specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md)/[plans/0012-asset-system-foundation.md](../plans/0012-asset-system-foundation.md),
built as the `atlantis_asset_system_tests` Catch2 v3 executable.
Entirely GPU-independent — no test in this executable carries the CTest
`gpu` label. Covers `StaticMeshAssetData` construction/move semantics;
logical-path normalization's legal/illegal boundary forms
(`logical_path_tests.cpp`); Asset ID computation against independently-
computed FNV-1a reference vectors and little-endian serialization
(`asset_id_tests.cpp`); declared-set collision/case-conflict/duplicate
detection via hand-injected `AssetId` pairs, not a discovered real hash
collision (`asset_set_validation_tests.cpp`); strict parse/serialize
round-trips and every malformed/truncated/version/overflow/non-finite
case for all three formats, including a fixed expected-byte-vector case
pinning the little-endian contract specifically
(`mesh_source_tests.cpp`, `asset_metadata_tests.cpp`,
`mesh_artifact_tests.cpp`); the file-level loader's success, each
failure mode, and a deliberate artifact/metadata mismatch
(`load_tests.cpp`); and a static include scan proving no source under
`src/asset_system/` references RHI, Renderer, RenderGraph, Shader
System, Platform, Vulkan Backend, Tools, or any Vulkan header
(`module_boundary_tests.cpp`). No Vulkan device or window is required.

**`tools/asset_cooker/`** — tests for the `atlantis_asset_cooker` CLI,
per the same Spec/Plan 0012 references, built as two executables:
- `atlantis_asset_cooker_command_tests` — GPU-independent, carries no
  CTest `gpu` label. Exercises `runCookCommand()` in-process (no
  subprocess) across cook mode (success, unreadable/malformed source, an
  escaping logical path) and validate-set mode (a valid declared set, a
  duplicate, a case-only conflict, an unnormalized path, a missing list
  file).
- `atlantis_asset_cooker_determinism_tests` — carries the CTest label
  `tool` (needs the real, just-built cooker executable at test-run time,
  no GPU/Vulkan device), matching the `tool` label
  `tests/tools/shader_compiler/CMakeLists.txt`'s own
  `atlantis_shader_compiler_toolchain_integration_tests` target already
  established. Launches the real `atlantis_asset_cooker` executable
  twice and byte-compares both output files, proving determinism through
  the actual CLI.

**`image_regression/`** — tests for Atlantis's image regression
harness, per
[specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)/[plans/0011-image-regression-testing-foundation.md](../plans/0011-image-regression-testing-foundation.md),
built as two executables:
- `atlantis_image_regression_tests` — GPU-independent, carries no
  CTest `gpu` label. Covers the pixel-diff comparison algorithm
  (`pixel_diff_tests.cpp`), the PNG codec round-trip and malformed-input
  rejection (`png_codec_tests.cpp`), provenance-sidecar parsing
  (`provenance_tests.cpp`), and the four-step golden validity check
  (`golden_validity_tests.cpp`) — all against synthetic in-memory
  buffers and constructed/temp-generated test PNGs, no Vulkan device
  required.
- `atlantis_image_regression_gpu_tests` — GPU-required, every test
  case carries the CTest label `gpu`. Drives a real capture-via-
  `OffscreenTarget` → compare-against-committed-golden cycle
  (`image_regression_gpu_tests.cpp`) against the `minimal_cube` scene's
  own committed golden (`goldens/minimal_cube/`) — exact per-pixel
  match (channel tolerance 0, failing-pixel budget 0), repeated-cycle
  determinism, `INVALID GOLDEN` for a never-committed golden, and
  `PROVENANCE MISMATCH` reported as a diagnostic separate from
  pass/fail.

`support/` (the comparison/codec/provenance/validity library),
`fixture/` (the reused cube scene, duplicating
`examples/headless_rendering_demo`'s own setup byte-for-byte), and
`golden_generator/` (a standalone, non-CTest-registered tool that
regenerates a golden against a clean, committed working tree — never
reachable from an ordinary `ctest` run) are this suite's own
supporting subdirectories, not additional test executables. See
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
for the settled golden format/location/tolerance and
[ADR-0041](../adr/0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)/[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
for the full design. Run the GPU-independent suite the same way as
every other suite above; run the GPU-required suite via
`ctest --test-dir build -C Debug -L gpu --output-on-failure` on a real,
Vulkan-capable Windows machine with
`tests/image_regression/current_environment.sidecar.txt` populated
(git-ignored, machine-local — see
`current_environment.sidecar.txt.example`).

No GPU-touching CI exists yet; the `gpu` CTest label is preparatory
infrastructure a future CI job would opt into explicitly, not a
currently-running pipeline — see
[docs/process/ci-strategy.md](../docs/process/ci-strategy.md). See
[AGENTS.md](../AGENTS.md).

Do not add test files for other modules here without a linked spec and
plan.
