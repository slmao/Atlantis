# Plan: Profiling and Performance Instrumentation Foundation

- **Spec:** [Spec 0059: Profiling and Performance Instrumentation Foundation](../specs/0059-profiling-instrumentation.md)
  (`Approved`, 2026-10-10, review of [PR #242](https://github.com/slmao/Atlantis/pull/242),
  merged at `9726f3c`; owner rulings Q1–Q12 binding, recorded once below) —
  [ADR-0114](../adr/0114-profiling-api-build-configuration-and-recorder.md)
  (API, build options, recorder; D1–D6),
  [ADR-0115](../adr/0115-rhi-instrumentation-boundary.md)
  (RHI regions, timestamps, debug names; D1–D7),
  [ADR-0116](../adr/0116-tracy-profiler-dependency.md)
  (Tracy; D1–D7) and
  [ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md)
  (benchmark host, schema, comparison; D1–D7), all `Accepted`.
  - Unchanged: ADR-0001 (backend independence), ADR-0004 (single frame
    thread), ADR-0006 (pinned `FetchContent`; the Tracy viewer is a host tool),
    ADR-0106 (`atlantis.remote/1`).
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending — to be recorded here once the owner has
  reviewed this Plan and Spec 0059 together and explicitly authorized
  implementation.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0059 R1–R14 as ruled: the `ATL_PROFILE_*` API and in-process
recorder in Core; additive RHI regions and debug names with automatic
RenderGraph pass regions; Vulkan Backend labels, object names, GPU timestamps
and counters; Tracy as an optional sink; the frame, renderer and submission
instrumentation; a requested initial window size in Platform; and
`atlantis_runtime --benchmark` with the `atlantis.benchmark/1` JSON, variants
and baseline comparison. Measurement only: no optimization, no behavior or
image change in any configuration.

## Governance record (this PR)

**Owner rulings, 2026-10-10 (chat tasking), recorded once here.** The Spec body
keeps its original option text; these rulings bind it.

| Q | Ruling |
|---|---|
| Q1 | Keep `ATL_PROFILE_*` as the public macro prefix. |
| Q2 | Three options: `ATLANTIS_PROFILING=OFF`, `ATLANTIS_PROFILING_TRACY=ON` (effective only with profiling), `ATLANTIS_GPU_MARKERS=ON` on Windows / `OFF` on Android; Android forces profiling and markers off. |
| Q3 | Tracy v0.14.1 by `FetchContent` URL + SHA-256, `atlantis_tracy` private static library. |
| Q4 | Recorder independent of Tracy; Tracy is a sink. |
| Q5 | `beginRegion`/`endRegion`, Atlantis-owned query pool (G1), Tracy's Vulkan context fed separately. |
| Q6 | `debugName` on creation params (N1) plus `CommandList::setDebugName`. |
| Q7 | Automatic pass regions plus `Sky`/`Opaque`/`Transparent` sub-regions in `draw`; fog by the `no-fog` variant. **`draw` is not split.** |
| Q8 | Counters at their exact owner layers through `ATL_PROFILE_ADD`/`VALUE`. |
| Q9 | `atlantis_runtime --benchmark bistro --warmup 120 --frames 300 --json` (plus `--baseline`, `--output`, `--series`, variants). The `atlantis benchmark …` launcher is future work, not in this Plan. |
| Q10 | A small additive Platform change: requested initial client size, default 1920×1080, Android ignores it; the run records the actual swapchain extent and exits non-zero on mismatch; present mode stays FIFO. |
| Q11 | Closed variant list: `baseline`, `no-point-lights`, `no-fog`, `no-bloom`. |
| Q12 | JSON schema `atlantis.benchmark/1`, nearest-rank percentiles. |
| — | The `runFrame()` diff guard changes from "byte-identical" to "plus `ATL_PROFILE_*` lines" (task M7.4, P12). |

**Status metadata changed in this PR** (no decision text is edited):

- Spec 0059: `Draft` → `Approved`; `Related Plan(s)` links this Plan.
- ADR-0114 … ADR-0117: `Proposed` → `Accepted`, acceptance evidence = the
  owner's rulings above and Spec 0059's review PR #242.
- `docs/specs/README.md`: the 0059 row updated (status, Plan link).

The Plan stays `Draft` until the Joint Human Review.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `9726f3c` (#242 merged).

1. **The frame** (`runtime_application.cpp`, `runFrame()` 1177–1932): early
   `return`s exist on seven paths (no presentation, null acquire, failed
   rebuilds, extraction failures, submit/present failure), so any RAII zone is
   exited by the destructor; explicit-end macros are needed for stage zones
   (P4). The post-realization `waitIdle()` is at 1882, `submit()` at 1839,
   `present()` at 1925.
2. **Renderer / RenderGraph** (`renderer.cpp` 116–451, `execution.cpp`): the
   pass labels are `shadow`, `draw`, `bloom_down_n`, `bloom_up_n`,
   `bloom_composite`, `output_transform`; `CompiledGraph::label(pass)` returns a
   view valid only while the graph lives. `computeDrawOrder()` returns non-blended
   items first, then blended, so `Opaque` and `Transparent` are the two
   contiguous runs of `drawOrder` (`std::partition_point` on `isBlended`).
3. **Vulkan Backend:** `submit()` drains the previous fence in
   `waitAndReleaseRetainedSubmission()` (vulkan_device.cpp:556); debug utils is
   enabled only with validation (vulkan_instance.cpp:327) and not on Android;
   descriptor sets live in a device-wide pool, one set per pipeline
   (vulkan_device.cpp:964); the `bindUniformBuffer`/`bindTexture` memo skips
   redundant writes (vulkan_command_list.cpp:413-512); entry points are
   resolved by name pairs (`dynamic_rendering_entry_points.*`), the precedent
   for the debug-utils entry points.
4. **Creation sites outside the backend** (counted 2026-10-10):
   `runtime_application.cpp` 43, `material_realization.cpp` 9,
   `environment_realization.cpp` 6, `mesh.cpp` 4, `frame_capture.cpp` 3,
   `bloom.cpp` 2, `material.cpp` 1, `shader_system/descriptor_contract.cpp` 2.
   All creation params are in `rhi/types.h` and are built with C++20
   designated initializers, so a trailing `debugName` member is source
   compatible.
5. **Test shapes to reuse:** `tests/render_graph/fake_command_list.h` (the one
   non-Vulkan `CommandList`), `tests/cli/cli_boundary_tests.cpp` (include
   scanning), `tests/runtime/cli_tests.cpp` plus `src/runtime/cli.cpp` compiled
   straight into `atlantis_runtime_tests` (executable-private code, GPU-free),
   `editor_attachment.cpp` (executable-private code that needs the host library),
   GPU tests labelled `gpu` under fatal VVL.
6. **Reusable code:** `atlantis::connection::json` (`Value`, `write`, `parse`;
   numbers keep literal text) is already linked by Runtime; it writes and reads
   the benchmark document. Bistro content is present on this machine
   (`content/bistro/`); the pin is `source_commit` in
   `tools/content/bistro_source.provenance.txt`.
7. **The goldens do not see `runFrame()`** (Plan 0055 P7): they come from the
   headless fixture, which exercises Renderer, RenderGraph and the backend but
   not `RuntimeApplication`. `runFrame()` is covered by the diff guard, the
   Runtime smoke/north-star tests and the capture equality check (P12).
8. **Windows window sizing:** the per-monitor-DPI-aware-v2 process makes the
   client rect equal framebuffer pixels, but `WM_GETMINMAXINFO`'s default
   `ptMaxTrackSize` clamps a window larger than the primary monitor, so an exact
   1920×1080 client on a 1080p display needs the Platform change to raise it
   (P11, J9).

## Plan-stage decisions

**P1 — Files.**

| Where | Contents |
|---|---|
| `src/core/include/atlantis/profile.h` (new) | macros and the minimal types they expand to (P2) |
| `src/core/include/atlantis/profile_recorder.h` (new) | recorder control and readout, device/display metadata, name interning (P3) |
| `src/core/include/atlantis/profile_stats.h` (new) | nearest-rank statistics, shared by recorder readers and the benchmark |
| `src/core/src/profile.cpp`, `profile_recorder.cpp`, `profile_stats.cpp` (new) | implementations; `profile.cpp` is the only Core file that includes Tracy (M2) |
| `cmake/AtlantisTracy.cmake` (new) | Tracy fetch and `atlantis_tracy` (P14) |
| `CMakeLists.txt`, `src/core/CMakeLists.txt` | options, Android forcing, public compile definitions, Tracy include and link |
| `src/rhi/include/atlantis/rhi/types.h`, `command_list.h` | `debugName` members; `beginRegion`/`endRegion`/`setDebugName` |
| `src/render_graph/src/execution.cpp` | automatic pass regions, `RecordPass` zones |
| `tests/render_graph/fake_command_list.h` | records regions and names |
| `src/renderer/src/renderer.cpp` | `Renderer`/`RenderGraph` zones, `Sky`/`Opaque`/`Transparent` regions, names for graph-owned objects |
| `src/renderer/src/{mesh,material,bloom}.cpp`, `src/runtime/src/{material_realization,environment_realization,frame_capture}.cpp`, `src/shader_system/src/descriptor_contract.cpp` | `debugName` at creation sites (P6) — **not in the Spec's file list; J1** |
| `src/runtime/src/runtime_application.cpp` | `ATL_PROFILE_*` lines in `runFrame()`; names at its creation sites; counters; the extent accessor's definition |
| `src/runtime/include/atlantis/runtime/{runtime_application,bootstrap_config,platform_session}.h`, `src/runtime/src/platform_session.cpp` | extent accessor; requested client size; session overload — **J1** |
| `src/runtime/cli.{h,cpp}`, `main.cpp` | `--benchmark` flags and dispatch |
| `src/runtime/benchmark_*.{h,cpp}` (new, executable-private, P9) | stats→JSON, comparison, variants, driver |
| `src/runtime/CMakeLists.txt`, `tests/runtime/CMakeLists.txt` | the new sources into `atlantis_runtime` and the GPU-free test target |
| `src/platform/include/atlantis/platform/platform.h`, `src/platform/src/windows/windows_platform.cpp`, `src/platform/src/android/android_platform.cpp` | `WindowOptions`, `initialize(const WindowOptions&)` (P11) |
| `src/vulkan_backend/src/*`, `src/vulkan_backend/CMakeLists.txt` | debug utils, names, query pool, regions, counters, Tracy Vulkan context (P5, P14) |
| `tests/core/`, `tests/rhi/`, `tests/render_graph/`, `tests/renderer/`, `tests/vulkan_backend/`, `tests/runtime/`, `tests/platform/` | tests (checklist) |
| `docs/process/profiling.md` (new), `AGENTS.md`, `docs/architecture/module_boundaries.md` | operator note and normative docs (M10) |

**Dependency-graph changes:** Core gains a private link to `atlantis_tracy`
(profiling + Tracy builds only); the Vulkan Backend gains the same private link;
no other module changes its link list. `atlantis_runtime` and
`atlantis_runtime_tests` gain executable-private sources (as `cli.cpp`).

**P2 — Core API.**

- `profile.h` is the only header business code includes. Macros (all expand to
  `((void)0)` unless `ATLANTIS_PROFILING` is defined, arguments **unevaluated**):
  `ATL_PROFILE_FRAME()` (frame boundary **and** the `Frame` zone for the
  enclosing scope), `ATL_PROFILE_SCOPE(name)`,
  `ATL_PROFILE_SCOPE_DYNAMIC(string_view)`,
  `ATL_PROFILE_SCOPE_NAMED(var, name)` with `ATL_PROFILE_SCOPE_END(var)` (an
  early, idempotent end; the destructor ends it otherwise),
  `ATL_PROFILE_GPU_SCOPE(cmd, name)`, `ATL_PROFILE_VALUE(name, v)`,
  `ATL_PROFILE_ADD(name, delta)`, and `ATL_DEBUG_NAME(expr)` (yields
  `std::string_view{}` without evaluating `expr` unless `ATLANTIS_GPU_MARKERS`).
  The extra macros are J5.
- Name literals register once per call site (a function-local static id);
  dynamic names go through `internName()` (allocates only the first time a
  distinct string is seen).
- `ATLANTIS_PROFILING`, `ATLANTIS_PROFILING_TRACY`, `ATLANTIS_GPU_MARKERS` are
  PUBLIC compile definitions of `atlantis_core` set to 1 when ON; they are the
  one source for every module. **No public header outside `profile*.h` tests
  these macros** (scanned, checklist), which is what keeps layouts and vtables
  identical across configurations (R2).
- `ATL_PROFILE_GPU_SCOPE` is live when `ATLANTIS_PROFILING` *or*
  `ATLANTIS_GPU_MARKERS` is defined (ADR-0115 D2), expanding to a guard over
  `CommandList::beginRegion`/`endRegion`.

**P3 — Recorder.** `profile_recorder.h` declares: `start()/stop()/isActive()`;
`FrameRecord readFrame(n)` over a ring of the last four frames; `NameId
internName(string_view)`; `setDeviceInfo`/`setDisplayInfo` (name, driver, API
version, timestamp period, present mode, refresh rate; each optional);
`deliverGpuRegions(frameId, span)`; and a test seam `setClock(fn)`.

- **Storage:** per frame, fixed arrays — 256 zone slots by `NameId`, 128
  counter slots, 128 GPU region records; overflow is counted in
  `dropped_*` and reported, never an error. No allocation after the first
  measured frame.
- **Same-named zones in one frame are summed** (inclusive duration of each
  non-nested occurrence), so a stage that is not contiguous in `runFrame()` is
  several scope pairs with one name.
- **Frame closing:** `ATL_PROFILE_FRAME()` closes the previous record. A record
  is `presented` only if its `Present` zone ended; others are `skipped`.
  GPU regions arrive one frame late and are written into the *earlier* record
  (the ring is why); the benchmark runs one extra frame (P9).
- **Inactive cost:** when the recorder is not started, each macro costs one
  predictable branch; Tracy forwarding is independent of `start()`.
- **Thread-safety:** frame thread only (stated in the header, ADR-0004); no
  locks.

**P4 — Zone placement in `runFrame()`.** Every addition is a whole line starting
`ATL_PROFILE_`; no existing line moves, no brace is added (the guard, P12,
depends on it).

| Zone (name) | Placement |
|---|---|
| `Frame` | first line, `ATL_PROFILE_FRAME();` |
| `World` | scope pair around the existing `applyPending()` statement |
| `Runtime` | summed segments: event pump + acquire + format/extent checks; command list + environment/material realization; post-submit publish |
| `Acquire` | around `acquireNextTarget()` |
| `RenderExtraction` | summed segments: camera/lighting/uniform writes; `collectRenderables` + material id lists; the `DrawItem` walk |
| `Submit`, `Present` | around `device_->submit()` and `presentation_->present()` |
| `WaitIdle` | around the post-realization `waitIdle()` only |
| `Renderer` | first line of `Renderer::drawFrame()` |
| `RenderGraph` | named scope before `builder.compile()` to the end of `drawFrame()` |
| `RecordPass:<label>` | inside `render_graph::execute()` around each pass callback (dynamic name) |
| `WaitPreviousGpu` | inside `VulkanDevice::waitAndReleaseRetainedSubmission()` |

Values (`ATL_PROFILE_VALUE`): `renderables_total` after `collectRenderables`,
`renderables_drawn` = `drawItems.size()`, `renderables_culled` = 0 (a literal
with a comment that no culling exists), `lights_directional`/`lights_point` from
the extracted block, `materials_realized`, `uniform_write_bytes`; `_skipped` is
derived (`total − drawn`) in the recorder's frame close.

**P5 — GPU regions in the backend** (ADR-0115 D1–D3).

- A query pool of 256 queries (128 regions). On the **first `beginRegion` of a
  command list**, `vkCmdResetQueryPool` for the pool is recorded (always outside
  a rendering scope: pass regions open before `beginRendering`); a command list
  with no region records nothing and is not read back.
- `beginRegion` writes `TOP_OF_PIPE`, `endRegion` writes `BOTTOM_OF_PIPE` (Vulkan
  1.0 core). Region names are interned ids stored in the command list (no
  string copy per frame).
- Readback in `waitAndReleaseRetainedSubmission()`, right after the fence wait,
  with `VK_QUERY_RESULT_64_BIT | WITH_AVAILABILITY_BIT`, converted by
  `timestampPeriod`, delivered with the submitting frame id. Unavailable
  queries are reported, not waited for. `timestampValidBits == 0` → "GPU timing
  unavailable" metadata, no pool.
- `--gpu-timing=off` skips region timestamps (labels and Tracy unaffected).
- `gpu_busy_ms` = Σ top-level pass regions; `gpu_span_ms` = first region start →
  last region end (a clarification of the Spec's "first→last timestamp" now that
  no list-level timestamp is written; J6).
- Optional `draw` pipeline-statistics query (fragment invocations, input-assembly
  primitives), created only if `pipelineStatisticsQuery` is supported and
  enabled at device creation; else the two counters are `unavailable`.

**P6 — Names.** Scheme (built only under `ATL_DEBUG_NAME`): scene resources
`kind:<8-hex-of-guid>` (`mesh:`, `material:`, `texture:`, `sampler:`), engine
resources by role (`hdr_color`, `depth`, `shadow_map`, `shadow_light_space`,
`camera_uniform`, `bloom_d1…d6`, `bloom_u1…u5`, `bloom_composite`,
`fullscreen_tri_vb/ib`, `capture_target`, `editor_viewport`, per-pipeline
`pipeline:<role>`), the per-frame command list `frame`. The backend names
`VkBuffer`, `VkDeviceMemory`, `VkImage`, `VkImageView`, `VkSampler`,
`VkPipeline`, and the pipeline's descriptor set and pipeline layout as
`<name>/set`, `<name>/layout`. Debug-utils entry points are resolved once after
device creation (the `dynamic_rendering_entry_points` pattern, a pure
name-selection function that is unit-testable); a missing extension leaves them
null and every naming/label call returns early.

**P7 — Backend counters** (added with `ATL_PROFILE_ADD` at the issuing method):
`draw_calls` and `triangles` in both `drawIndexed` overloads (`indexCount / 3`),
`pipeline_binds`, `vertex_buffer_binds`, `index_buffer_binds`,
`descriptor_set_binds` (each `vkCmdBindDescriptorSets`), `descriptor_writes`
(each real `vkUpdateDescriptorSets`, i.e. after the memo), `push_constants`,
`upload_bytes` (bytes of each `copyBufferToTexture` region set). All are
per-frame accumulators reset at `ATL_PROFILE_FRAME()`.

**P8 — Statistics, schema, comparison** (ADR-0117 D6–D7).

- Stats object: `{avg, p50, p95, p99, min, max, n}`; nearest rank:
  index `ceil(p/100 · n) − 1` on the ascending sorted samples; `avg` is the
  arithmetic mean; `n = 0` → the metric is `{"unavailable": "<reason>"}`.
- Numbers: milliseconds as shortest round-trip `double` text via
  `std::to_chars` into `Value::numberLiteral`; counters as integers when
  integral. The document is compact (`json::write`), members in the fixed order
  below.

```
{ "schema": "atlantis.benchmark/1",
  "meta": { "build": {type, profiling, tracy, gpu_markers, validation, compiler,
                       git_revision, git_dirty},
            "system": {os, cpu, gpu, driver, vulkan_api, timestamp_period_ns},
            "display": {present_mode, refresh_hz} },
  "scene": {name, guid, content_pin},
  "settings": {resolution: {width, height}, warmup, frames, variant, gpu_timing},
  "frames": {measured, discarded, skipped},
  "metrics": { <flat keys of Spec 0059's counter set>: stats | unavailable,
               "zones": {<name>: stats}, "passes": {<label>: {gpu_ms, cpu_record_ms}} },
  "series": {…}            // only with --series
  "comparison": {…} }      // only with --baseline
```

- **Comparable** = equal `schema`, `scene.guid`, `scene.content_pin`,
  `settings.resolution/variant/gpu_timing`, `meta.build.{type,profiling,
  tracy,gpu_markers,validation}`, `meta.system.{gpu,driver}`. `comparison`
  holds `comparable`, `reasons[]`, and for every metric present on both sides and
  each of `avg/p50/p95/p99`: `baseline`, `current`, `delta`, `delta_pct`, plus
  `noise` = `p95 − p50` of each side. No threshold, no verdict.
- Sources of `meta`: build facts are compile definitions; `git_revision`/`dirty`
  and `content_pin` (`source_commit`) are read by CMake at configure time into
  compile definitions; `gpu`, `driver`, `vulkan_api`, `timestamp_period_ns`,
  `present_mode` are published into the recorder by the Vulkan Backend at device
  and presentation creation; `refresh_hz` by the Windows Platform; `cpu` via the
  compiler `__cpuid` brand string and `os` a constant — **no new file includes
  `<windows.h>`** (J2).

**P9 — Benchmark driver** (ADR-0117 D1, D4–D5; executable-private).

- Files: `benchmark_options.*` (flag values), `benchmark_stats.*`/
  `benchmark_json.*` (stats→document, comparison; GPU-free, compiled into
  `atlantis_runtime_tests`), `benchmark_variants.*`, `benchmark_driver.*` (needs
  the host library; compiled into `atlantis_runtime`, as `editor_attachment`).
- The measured loop is a pure function of three injected things — a "run one
  frame" callable, the recorder, and a clock — so the fake-clock tests drive it
  without a GPU; the executable wires `RuntimeApplication::runFrame()`.
- Flow: parse → refuse (no profiling build, scene absent, `--exec`/`--editor`/
  `--listen` combined) → create the application with the requested client size →
  apply the variant before warmup → warmup frames → `recorder.start()` →
  `frames` *presented* frames → one extra frame (so the last GPU results land) →
  `stop()` → document. Skipped frames (no present) are counted, not measured; no
  presented frame for 15 s → runtime failure.
- The first presented frame's extent is compared with the request through a new
  read-only `RuntimeApplication::presentationExtent()` accessor; mismatch →
  environment-mismatch exit.
- During `--benchmark` the log sink writes everything to stderr; stdout carries
  exactly the one JSON document (or `--output <path>`).
- **Exit codes:** `0` success; `1` usage error or initialization failure (the
  existing meaning); `2` runtime failure (existing); `3` environment mismatch or
  unsupported build (resolution differs, no profiling in the build, scene absent
  or content missing) (J7).
- Defaults `--warmup 120 --frames 300 --resolution 1920x1080 --variant baseline`.

**P10 — Variants** (through the application's own `RuntimeConnection`, as
transactions, before warmup): `no-fog` sets `Camera.fog.density` to 0 on the
active camera; `no-bloom` sets `Camera.bloom.strength` to 0; `no-point-lights`
**removes the `Light` component** from every entity whose `Light.kind` is `Point`
(so `lights_point` reads 0 and the lighting loop's trip count really drops;
setting intensity to 0 would leave it unchanged — J3). Each applies by exact
property address, is verified by a read-back, and is recorded in `settings`.

**P11 — Platform requested size** (ADR-0117 D2). `platform.h` gains
`struct WindowOptions { std::optional<ClientSize> requestedClientSize; }` and an
overload `initialize(const WindowOptions&)`; `initialize()` forwards `{}`
(every existing caller unchanged). Windows sizes the window with
`AdjustWindowRectEx` for that client size and, while an option is set, raises
`ptMaxTrackSize` in `WM_GETMINMAXINFO` so the window can exceed the monitor
(J9); it publishes the monitor refresh rate to the recorder. Android's
`initialize` ignores the option. `BootstrapConfig` gains the requested size
(zero = OS default); `createPlatformSession` gains a matching overload.

**P12 — Gates and guards.**

- **Configurations:** **A** = default (all new options as defaulted: profiling
  OFF); **B** = `-DATLANTIS_PROFILING=ON -DATLANTIS_PROFILING_TRACY=OFF`;
  **C** = `-DATLANTIS_PROFILING=ON -DATLANTIS_PROFILING_TRACY=ON`. Separate
  untracked build trees. From M1, every gate builds A Debug and Release with the
  full `ctest`; B and C from the milestone that introduces them, Release full and
  Debug GPU suites; the final gate runs A, B, C × Debug, Release full (J8).
- **Every gate also:** every golden compared exactly (a moved golden stops the
  work and is reported, never re-captured); the golden-directory guard; fatal-VVL
  GPU suites; Android `assembleDebug`; the path guard (changes limited to P1,
  nothing under `shaders/`, `assets/`, `android/`, `src/world/`,
  `src/asset_system/`, `src/connection/`, `src/remote/`, `src/cli/`,
  `src/editor/`, `src/gameplay_sdk/`, `src/csharp/`, goldens).
- **`runFrame()` diff guard (accepted change from byte-identical).** Extract
  `RuntimeApplication::runFrame()` from base (`9726f3c`) and HEAD. After deleting
  from HEAD every line whose first non-blank token starts `ATL_PROFILE_`, the two
  bodies must be **byte-identical**. Applied from M7; before M7 the strict
  byte-identical guard applies. The same extraction guards the other
  non-instrumentation-only bodies: `Renderer::drawFrame()` and
  `render_graph::execute()` are reviewed by diff, not mechanically.
- **Capture equality:** for each of A, B, C, the default scene captured through
  `atlantis runtime step --capture` (Spec 0055) at a fixed step is byte-identical
  PNG across configurations; with `--listen`, nothing else differs.

**P13 — Overhead report method (not gated).** Release, this machine, `bistro`,
fixed 1920×1080 where available. Configurations: A (OFF), B (recorder compiled,
inactive), B-active (the benchmark running), C-idle (Tracy compiled, no viewer),
C-connected (viewer attached, capturing). Metric common to all: the client-side
wall time of `atlantis runtime step --frames 600` after one 120-frame step,
five repetitions, the median of the per-run mean frame time; B-active and C add
the benchmark's own `frame_ms`. Reported as a table of deltas against A with the
spread; the method's FIFO caveat stated.

**P14 — Tracy.**

- `cmake/AtlantisTracy.cmake`: `FetchContent_Declare(tracy URL
  https://github.com/wolfpld/tracy/archive/refs/tags/v0.14.1.tar.gz
  URL_HASH SHA256=<measured in M2.1> DOWNLOAD_EXTRACT_TIMESTAMP TRUE)`;
  `atlantis_tracy` static library from `public/TracyClient.cpp`, `SYSTEM`
  include of `public/`, definitions `TRACY_ENABLE TRACY_ON_DEMAND
  TRACY_ONLY_LOCALHOST`, no repository warning flags; included only when both
  options are ON and not Android.
- Core's `profile.cpp` uses Tracy's C API (`TracyC.h`) behind
  `ATLANTIS_PROFILING_TRACY`; `profile.h` exposes an own source-location struct
  whose layout is `static_assert`ed equal to `___tracy_source_location_data` in
  that .cpp.
- The backend's GPU context file includes `TracyVulkan.hpp`: a calibrated context
  when `vkGetCalibratedTimestampsEXT` can be resolved (the device extension
  `VK_EXT_calibrated_timestamps` is enabled only if the physical device offers
  it), else uncalibrated; regions emit `TracyVkZoneTransient`; `TracyVkCollect`
  runs where the recorder reads its own results.

## Milestones / Task Breakdown

Ten milestones; commit prefixes `feat:` / `test:` / `chore:` / `docs:`. Each
ends at a gate (P12) with build and tests run. Branch
`feature/0059-profiling-instrumentation` off `origin/main` after this PR merges.

**Preconditions for M2, M6, M10:** network access (Tracy archive); for M10 the
Tracy viewer release asset `windows-0.14.1.zip` (host tool, ADR-0116 D4) and
RenderDoc installed on the implementing machine. M1, M3–M5 need none.

### M1 — Options, Core API and recorder (R1, R2, R10; ADR-0114)

1. Top-level `CMakeLists.txt`: the three options, Android forcing with a status
   message, definitions on `atlantis_core` (P2). Defaults leave configure output
   and the build unchanged.
2. `profile.h`, `profile_recorder.h`, `profile_stats.h` and the three
   implementations (P2, P3); no Tracy yet.
3. Tests (`tests/core/`): compiled-out expansion (argument side effects not
   evaluated, macros expand to `((void)0)` by stringised expansion, forced by
   `#undef` in a dedicated TU so the proof runs in A, B and C); enabled
   behavior (VALUE last-wins, ADD sums, zone summing and nesting, frame closing,
   skipped frames, ring and late GPU delivery, overflow counting, bounded
   storage, fake clock); statistics against hand-computed vectors; a scan that
   no public header outside `profile*.h` names the three macros.

*Gate:* A, B Debug+Release.

### M2 — Tracy dependency and Core sink (R7 part; ADR-0116 D1–D3)

1. **Measure the archive SHA-256.** Download the v0.14.1 tag archive into a new,
   empty scratch directory (untrusted data; nothing run from it), compute
   `Get-FileHash -Algorithm SHA256`, repeat the download from a second request
   and require equal hashes, record the hash, size and date in the PR. A mismatch
   or unexpected archive layout stops the work.
2. `cmake/AtlantisTracy.cmake` and the Core/Tracy wiring (P14); `profile.cpp`
   forwards zones, plots and frame marks through the C API.
3. Tests: the Tracy-include boundary scan (only `profile.cpp` and the backend GPU
   context file may include Tracy); configuration C builds, links and runs the M1
   recorder tests with Tracy compiled in (no viewer needed); `atlantis_tracy`'s
   compile definitions contain `TRACY_ONLY_LOCALHOST` and `TRACY_ON_DEMAND`.
4. Android configure with `-DATLANTIS_PROFILING=ON` appended to the Android
   CMake arguments: the status message appears, no `tracy` target or
   `_deps/tracy-*` directory exists (recorded once by hand in the PR).

*Gate:* A, B, C.

### M3 — RHI regions and automatic RenderGraph regions (R5 part; ADR-0115 D1, D4, D5)

1. `rhi/types.h`: `std::string_view debugName = {}` as the **last** member of the
   eight `*CreateParams`; `command_list.h`: `beginRegion`, `endRegion`,
   `setDebugName`, non-pure virtuals with empty bodies.
2. `execution.cpp`: each pass wrapped (region opens before the pass's
   transitions, closes after `endRendering()` or the callback) with
   `ATL_PROFILE_GPU_SCOPE`, plus a `RecordPass:<label>` CPU zone around the
   callback; an empty label falls back to `pass_<position>`.
3. `fake_command_list.h` records regions and names in call order.
4. Tests: regions balanced, nested and in compiled order for the existing
   execution fixtures including a bloom-shaped graph; fallback label; a size/
   offset table for the RHI structs identical in A, B, C; goldens exact.

*Gate:* A, B, C.

### M4 — Vulkan debug utils: names and labels (R5; ADR-0115 D2, D5, D6)

1. Optional instance extension request under `ATLANTIS_GPU_MARKERS` (absence not
   an error), entry points resolved after device creation (P6's pure selection
   function unit-tested).
2. Backend naming at creation for every object kind in P6; `setDebugName` and
   region labels (`vkCmdBeginDebugUtilsLabelEXT`/`End`) in `VulkanCommandList`.
3. GPU tests under fatal VVL: names retrievable through a test hook, labels
   balanced, a pass of every kind labelled, the extension absent (simulated) is a
   silent no-op; goldens exact; Android unchanged (markers off).

*Gate:* A, B, C; GPU suites in Debug under fatal VVL.

### M5 — Names at the creation sites (R5; P6)

1. `ATL_DEBUG_NAME(...)` at every creation site in P1's list, one `debugName`
   argument each; no logic change. `Renderer`/`Runtime` build name strings only
   under the macro.
2. Tests: a Runtime GPU test that creates the application on the default scene
   and asserts the P6 role names exist through the M4 hook; the Bistro build
   (content-gated) asserts scene-resource names have the `kind:<hex>` shape.

*Gate:* A, B, C. (RenderDoc inspection is the human step of M10.)

### M6 — Vulkan timestamps, backend counters, Tracy GPU (R4, R6, R7; ADR-0115 D3, D7, ADR-0116 D5)

1. Query pool, lazy reset, region timestamps, readback in
   `waitAndReleaseRetainedSubmission()`, delivery to the recorder, device/display
   metadata publication, optional pipeline statistics (P5).
2. Backend counters (P7) and the `WaitPreviousGpu` zone.
3. Tracy Vulkan context and transient zones (P14).
4. GPU tests under fatal VVL: timestamps monotonic and non-negative, delivered
   one frame late with **no additional `vkWaitForFences`** (call count asserted
   through a test hook), unsupported timestamps reported as unavailable, counters
   exact on a fixture with a known draw sequence (draws, triangles, binds,
   `descriptor_writes` against the memo's expected hits), region overflow counted.

*Gate:* A, B, C; GPU suites in Debug under fatal VVL.

### M7 — Frame, renderer and submission instrumentation; the diff-guard change (R3, R6, R12, R14)

1. `runFrame()`: `ATL_PROFILE_*` lines per P4 and the `ATL_PROFILE_VALUE`s;
   `Renderer::drawFrame()`: `Renderer`/`RenderGraph` zones and the
   `Sky`/`Opaque`/`Transparent` sub-regions over the `drawOrder` partition
   (no change to draw order or recorded commands); a `Bloom` parent region around
   the `bloom_*` passes.
2. `RuntimeApplication::presentationExtent()` accessor.
3. **Update the `runFrame()` diff guard** to the P12 form: add its extraction
   procedure to the PR template evidence and run it at this gate and every
   later one; the Plan 0055/0056/0057/0058 byte-identical statements are
   superseded for `runFrame()` by this Plan's guard, recorded in the PR.
4. Tests: Runtime GPU smoke with the recorder active asserts every Zone-map name
   and every counter-set key appears with sane values on the default scene; the
   P12 capture equality across A, B, C; the guard run.

*Gate:* A, B, C; the new guard.

### M8 — Platform requested client size (R8 prerequisite; ADR-0117 D2)

1. P11: `WindowOptions`, the Windows implementation (including
   `WM_GETMINMAXINFO`), the Android no-op, `BootstrapConfig` and
   `createPlatformSession` overloads, refresh-rate publication.
2. Tests: portable tests of the options value; Windows smoke test creating a
   window with a requested 1920×1080 client size and asserting the
   `WindowResize` framebuffer extent equals it (including a size larger than the
   primary monitor); the default path's events unchanged.

*Gate:* A, B, C; `assembleDebug`.

### M9 — The benchmark (R8, R9, R11, R13; ADR-0117)

1. `cli.cpp`/`cli.h`: the flags of ADR-0117 D1 and their conflict rules;
   `cli_tests.cpp` cases (combinations, defaults, each refusal).
2. `benchmark_*` (P8–P10): stats→document, the schema, comparison, variants, the
   driver, the log redirect, exit codes.
3. GPU-free tests: schema validity (parse back, required keys, types, `null`
   reason shape); a fake-clock run that proves warmup/measured/discarded/skipped
   accounting and the extra frame; self-comparison = zero deltas and
   `comparable: true`; each comparability mismatch yields `comparable: false`
   with its reason; stdout carries exactly one document; exit codes; refusal on a
   non-profiling build.
4. GPU tests: variant application verified by read-back; a content-gated
   `bistro` smoke run (`--frames 20`) producing a valid document.

*Gate:* A, B, C; the benchmark tests are B/C-only and `SKIP` in A with the
reason.

### M10 — Documentation, acceptance runs and the closing measurements (R7, A1–A5)

1. **Docs (normative):** `docs/process/profiling.md` (build options, viewer
   release to install, RenderDoc use, benchmark command, schema pointer, vsync
   caveat); AGENTS.md (the Core diagnostics exception as ADR-0114 D5 states it,
   the Tracy dependency, the benchmark flags and the `runFrame()` guard
   change); `module_boundaries.md` (Core's profile API, RHI additions, Platform's
   requested size).
2. **Overhead report** (P13) into the PR.
3. **Human acceptance** (screenshots in the PR): (a) Tracy 0.14.1 viewer on a
   live `atlantis_runtime --scene bistro` showing the CPU zones, the GPU
   timeline and the counter plots; (b) a RenderDoc capture of the same showing
   every pass and every resource by name.
4. **Closing Bistro measurements — the report inputs.** Release, config B (and C
   for one confirming run), validation off, 1920×1080:
   `--benchmark bistro --warmup 120 --frames 300 --json` for `baseline`,
   `no-point-lights`, `no-fog`, `no-bloom`, **each run twice**; the second
   `baseline` compared against the first with `--baseline`; the JSON files
   attached to the PR (not committed: hardware-specific, J11). From them, the
   written bottleneck conclusion using Spec 0059's evidence table (reported in
   the PR, not a Spec output).
5. Final guards and a review of the whole diff against P1.

*Gate:* A, B, C × Debug and Release, full.

## Files / Modules Touched (expected)

**New:** the P1 rows marked new (Core profile headers and sources, Tracy cmake,
`benchmark_*`, tests, `docs/process/profiling.md`).

**Changed:** the P1 rows marked changed; plus `docs/specs/README.md` (this PR).

**Not touched:** `shaders/`, `assets/`, `android/`, goldens,
`src/{world,asset_system,connection,remote,cli,editor,gameplay_sdk,csharp}/`,
the Remote protocol and conformance vectors, every existing test file except
those listed (`fake_command_list.h`, `cli_tests.cpp`, CMake files, and
instrumentation-only additions).

## Sequencing & Dependencies

- M1 first (everything includes `profile.h`); M2 needs M1.
- M3 needs M1 (macros) and is independent of M2. M4 needs M3; M5 needs M4.
- M6 needs M1, M2 (Tracy GPU) and M4 (region plumbing). M7 needs M3 and M6.
- M8 is independent of M3–M7 (after M1) and may land earlier; M9 needs M6, M7
  and M8. M10 last.
- Gates run A from M1, B from M1, C from M2.

## Verification Checklist

- [ ] **R1 (M1, M2):** one Core header, the macro set; the Tracy-include and
  Vulkan-free scans (`profile.h`, RHI, RenderGraph, Renderer, Runtime headers).
- [ ] **R2 (M1, M3):** unevaluated arguments and `((void)0)` expansion proven in
  every configuration; no public header outside `profile*.h` tests the options;
  RHI struct layout table and `CommandList` overload set identical in A, B, C;
  goldens exact.
- [ ] **R3 (M7):** every Zone-map name present in a recorded Runtime frame,
  including the three wait zones and `RecordPass:*`.
- [ ] **R4 (M6, M7):** every pass and `Sky`/`Opaque`/`Transparent`/`Bloom`
  region has a timestamp pair, one frame late, no new fence wait; unsupported →
  unavailable.
- [ ] **R5 (M3–M5, M10):** automatic pass regions (fake list); names and labels
  (GPU test hook); RenderDoc capture by hand.
- [ ] **R6 (M6, M7):** every counter-set key produced; exact on the fixture;
  `renderables_culled` is the documented 0.
- [ ] **R7 (M2, M6, M10):** Tracy loopback-only (`TRACY_ONLY_LOCALHOST` in the
  target definitions, M2.3), live timeline by hand.
- [ ] **R8, R9 (M9):** command-line run, schema test, accounting test.
- [ ] **R10 (M1, M2):** defaults; Android forcing and no Tracy fetch.
- [ ] **R11 (M9):** comparison tests, `comparable: false` reasons, exit code 0.
- [ ] **R12 (every gate):** goldens exact in A, B, C; P12 capture equality.
- [ ] **R13 (M9):** no CTest assertion on a measured time (review of the diff).
- [ ] **R14 (every gate, from M7):** the `runFrame()` guard in its new form;
  no `Vk*`/Tracy type outside the Vulkan Backend / Core; path guard.
- [ ] **Acceptance A1–A5 (M10):** Tracy timeline; RenderDoc names; JSON from the
  command line; the written bottleneck conclusion; compiled-out goldens (R2/R12).
- [ ] **Validation layers:** fatal-VVL GPU suites clean at every gate; the
  Bistro benchmark smoke run with validation on in Debug shows no message.
- [ ] **Android (every gate):** `assembleDebug` unchanged; M2 configure check.
- [ ] **Overhead report (M10):** P13 table in the PR.

## Risks

- **Probe effect and vsync.** Described in the Spec; the report and the
  `--gpu-timing=off` run quantify them. `cpu_active_ms` includes `Present`'s
  blocking, as Spec 0059 defines it; `present_ms` is reported beside it.
- **Exact 1920×1080 may be impossible** on some displays or remote sessions;
  P11/J9 raise the track-size limit, and a mismatch still exits `3` rather than
  measuring the wrong size.
- **Tracy GPU calibration** is optional; an uncalibrated context drifts and is
  noted in the viewer, not the JSON.
- **Pipeline-statistics queries** need a device feature enabled at device
  creation; if enabling it changes the device chain for non-profiling builds that
  is a deviation to report (it is created only under `ATLANTIS_PROFILING`).
- **Name-building cost** at load for Bistro (about 1,100 assets); built only
  under markers, measured in the overhead report.
- **`runFrame()` density.** About fifteen new lines in a 750-line function; the
  guard keeps the existing text untouched.
- **A reader mismatch between recorder and Tracy** (two timing paths): the M6/M7
  tests compare the recorder's zone totals with a fake-clock Tracy stub, not
  with a live viewer.

## Joint Review decisions — proposed (for the owner's Joint Human Review)

Each carries a recommendation; none re-opens an owner ruling above.

- **J1 — The Spec's expected-files list is incomplete for R5/R8.** Name
  propagation needs the creation sites in P1 (about 70 one-line additions
  outside `runtime_application.cpp`), the extent accessor in
  `runtime_application.h`, and the `BootstrapConfig`/`platform_session`
  overloads. **Recommend:** authorize the P1 list as the scope (a Plan-stage
  reading, no Spec Correction), as Plan 0058 J1 did for its slip.
- **J2 — Device and display metadata without a new API.** The backend and
  Windows Platform publish GPU/driver/timestamp period/present mode/refresh rate
  into the Core recorder instead of adding `Device::info()` or a Platform getter;
  CPU and OS come from `__cpuid` and a constant so no new file includes
  `<windows.h>`. **Recommend** this; it needs no ADR change (ADR-0115 D3 and
  ADR-0117 D6 already route measurements through the recorder).
- **J3 — `no-point-lights` removes the `Light` component** (Plan P10) rather than
  zeroing intensity. **Recommend** removal: zero intensity keeps the loop's trip
  count and would under-report lighting cost. The variant name and list (Q11)
  are unchanged.
- **J4 — Three Core headers** (`profile.h`, `profile_recorder.h`,
  `profile_stats.h`) instead of one: business code includes only `profile.h`; the
  others serve the backend, the benchmark and tests. **Recommend.**
- **J5 — Extra macros** `ATL_PROFILE_SCOPE_NAMED/END/DYNAMIC` and
  `ATL_DEBUG_NAME` beyond the Spec's "at least" set (ADR-0114 D1 allows
  `_DYNAMIC`). Needed for early returns and the diff guard. **Recommend.**
- **J6 — `gpu_span_ms`** is first region start → last region end, since no
  list-level timestamp is written. **Recommend** (clarification, not a semantic
  change).
- **J7 — Exit codes** `0/1/2/3` as in P9, keeping the existing `1` and `2`.
  **Recommend.**
- **J8 — Gate cost.** Three configurations (A, B, C) × two build types is heavy;
  P12 runs the full matrix only at the final gate (M10) and A-full plus
  B/C-Release-full + Debug-GPU elsewhere. **Recommend**; the alternative (full matrix every gate)
  triples gate time for little extra signal.
- **J9 — `WM_GETMINMAXINFO`** handling as part of the Platform change, active
  only when a client size is requested. **Recommend**; without it exact
  1920×1080 fails on a 1080p monitor.
- **J10 — Acceptance preconditions.** M10 needs the Tracy 0.14.1 viewer and
  RenderDoc on the implementing machine; the owner confirms which machine
  performs the acceptance runs. **Recommend** the machine with `content/bistro/`
  and the target GPU.
- **J11 — Benchmark results are PR evidence, not committed files.** **Recommend**:
  they are hardware-specific; a future Spec may add a baseline store.

## Rollback Plan

All behavior is behind defaults-off options, so reverting the implementation PR
is a plain revert with no data or format migration. The additive RHI members and
the Platform overload are inert when unused; a partial rollback can leave them
and remove only the benchmark (M9) or only Tracy (M2, M6's Tracy part) because
each is a separate commit range with its own gate.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- The implementation PR records, per gate, the A/B/C results, the `runFrame()`
  guard output, the capture-equality result and the overhead table.
- The M10 acceptance screenshots, the benchmark JSON files and the written
  bottleneck conclusion are attached to the PR.
- Post-merge, a docs PR updates the registry row and the blueprint (the
  repository's usual follow-up).
