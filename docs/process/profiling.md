# Profiling and Benchmarking

Operator note for the instrumentation foundation of
[Spec 0059](../specs/0059-profiling-instrumentation.md), implemented by
[Plan 0059](../plans/0059-profiling-instrumentation.md). The normative
decisions are [ADR-0114](../adr/0114-profiling-api-build-configuration-and-recorder.md)
(API, options, recorder), [ADR-0115](../adr/0115-rhi-instrumentation-boundary.md)
(RHI and Vulkan instrumentation),
[ADR-0116](../adr/0116-tracy-profiler-dependency.md) (Tracy) and
[ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md)
(benchmark host, schema, comparison). This page says how to use them; it does not
restate them.

Nothing here is built or fetched by default. A default build is what it was
before Spec 0059.

## Build options

| Option | Default | What it adds |
|---|---|---|
| `ATLANTIS_PROFILING` | `OFF` | The `ATL_PROFILE_*` macros become live: the in-process recorder, counters, GPU region timestamps and pipeline statistics, and `atlantis_runtime --benchmark`. |
| `ATLANTIS_PROFILING_TRACY` | `ON`, effective only with `ATLANTIS_PROFILING` | Feeds the Tracy 0.14.1 client as a second sink. Fetches the pinned archive on first configure (network needed once). |
| `ATLANTIS_GPU_MARKERS` | `ON` on Windows, `OFF` on Android | Names every Vulkan object and labels every pass through `VK_EXT_debug_utils`, for RenderDoc. Independent of profiling. |

On Android all of this is forced `OFF` with a configure message, and no Tracy
target is declared.

```
cmake -S . -B build-prof -DATLANTIS_PROFILING=ON
cmake --build build-prof --config Release
```

Add `-DATLANTIS_PROFILING_TRACY=OFF` for a profiling build without Tracy (the
benchmark and the recorder do not need it). A markers-only build is
`-DATLANTIS_GPU_MARKERS=ON` with profiling left `OFF`.

Measure in **Release**. Debug and validation-enabled builds are flagged in the
benchmark document's `meta` and their numbers say nothing about the product.

## The macros

Include `atlantis/profile.h` (Atlantis Core) and nothing else. With
`ATLANTIS_PROFILING` off every macro expands to `((void)0)` and **its arguments
are not evaluated**: never put a side effect inside one.

| Macro | Meaning |
|---|---|
| `ATL_PROFILE_FRAME()` | Frame boundary; closes the previous frame's record. Once per frame, in `runFrame()`. |
| `ATL_PROFILE_SCOPE(name)` | CPU zone to the end of the enclosing scope. `name` is a string literal. |
| `ATL_PROFILE_SCOPE_DYNAMIC(view)` | The same with a runtime name (RenderGraph pass labels). |
| `ATL_PROFILE_SCOPE_NAMED(var, name)` / `ATL_PROFILE_SCOPE_END(var)` | A zone ended before its scope does (early returns, the `runFrame()` diff guard). |
| `ATL_PROFILE_GPU_SCOPE(cmd, name)` | GPU region on an `rhi::CommandList`. |
| `ATL_PROFILE_VALUE(name, v)` / `ATL_PROFILE_ADD(name, d)` | Per-frame gauge (last write wins) / accumulator. |
| `ATL_DEBUG_NAME(...)`, `ATL_PROFILE_NAME_COMMAND_LIST(cmd, name)` | Debug names, effective under `ATLANTIS_GPU_MARKERS`. |

The recorder is global diagnostics state, frame-thread-only, and inactive until
the benchmark driver starts it.

## Benchmark

```
atlantis_runtime --benchmark bistro --warmup 120 --frames 300 --json
```

Needs a profiling build (otherwise exit code 3) and, for `bistro`, the fetched
content (`tools/content/fetch_bistro.ps1`, then re-configure; without it the
scene is absent and the exit code is 3). Flags:

| Flag | Default | |
|---|---|---|
| `--benchmark <scene>` | | Any whitelisted scene; `bistro` is the committed baseline. Exclusive with `--exec`, `--editor`, `--listen`. |
| `--warmup <n>` / `--frames <n>` | 120 / 300 | Warmup frames are discarded; only frames that reached `Present` are measured. |
| `--resolution <W>x<H>` | `1920x1080` | The window is created at exactly this client size; a different swapchain extent exits 3. |
| `--variant <v>` | `baseline` | `baseline`, `no-point-lights`, `no-fog`, `no-bloom`: World commands applied before warmup, to localise cost. |
| `--json` / `--output <path>` | | One JSON document on stdout / in a file. Logs go to stderr during `--benchmark`. |
| `--baseline <path>` | | Adds a `comparison` section against an earlier document. |
| `--series` | off | Adds the raw per-frame series. |
| `--gpu-timing=on\|off` | on | `off` removes the timestamp writes to quantify their own cost. |

Exit codes: `0` success; `1` usage error or initialization failure; `2` runtime
failure (no frame presented, the variant did not take); `3` environment mismatch
or unsupported build (wrong window size, no profiling, absent scene). Never a
performance verdict.

The document schema is `atlantis.benchmark/1`, defined in ADR-0117 D6 and
produced by `src/runtime/benchmark_report.{h,cpp}`: `meta`, `scene`,
`settings`, `frames`, `metrics` (every metric as `{avg, p50, p95, p99, min, max,
n}`, or `null` with a reason where the hardware or build cannot supply it),
optional `series` and `comparison`. Percentiles are nearest-rank. Comparison
reports `comparable: false` with reasons when the schema, scene, content pin,
resolution, variant, build, GPU or driver differ. Benchmark results are
hardware-specific: keep them as PR evidence, do not commit them.

**Vsync caveat.** Presentation stays FIFO, so wall-clock `frame_ms` is
quantised to the refresh interval for any frame faster than it. The unthrottled
metrics are `cpu_active_ms` and `gpu_busy_ms`; read them beside `frame_ms`.
`cpu_active_ms` includes `Present`'s blocking, which `present_ms` reports
separately.

## Tracy

Install the **Tracy 0.14.1** viewer (the client library is pinned to the same
release; mismatched versions refuse to connect). Release archives of the viewer
for Windows are on the project's GitHub releases page; building it from the
same source archive is equally valid.

1. Build with `ATLANTIS_PROFILING=ON` and Tracy on.
2. Start `atlantis_runtime --scene bistro` (or any scene).
3. Start the viewer and connect to `127.0.0.1`.

The client listens on the loopback interface only (`TRACY_ONLY_LOCALHOST`) and
collects nothing until a viewer connects (`TRACY_ON_DEMAND`). The CPU zones, GPU
regions and counter plots (`draw_calls`, `triangles`, ...) appear as the
viewer's timeline, GPU context and plots. The GPU context is uncalibrated; it
may drift relative to the CPU timeline, which the JSON does not depend on.

## RenderDoc

Build with `ATLANTIS_GPU_MARKERS=ON` (the Windows default). In RenderDoc,
launch `atlantis_runtime.exe --scene bistro`, capture a frame, and open the
Event Browser: every render-graph pass, the `Sky`, `Opaque`, `Transparent` and
`Bloom` regions are labelled, and every buffer, image, pipeline, descriptor set
and command buffer carries its name. Names are cheap to ignore: a build without
markers neither builds them nor enables the extension.
