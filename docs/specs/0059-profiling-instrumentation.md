# Spec: Profiling and Performance Instrumentation Foundation

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-10
- **Related Plan(s):** None yet — drafted only after this Spec is approved.
- **Approval:** Pending (owner review of this Spec's own branch PR).
  The owner fixed the mission, the five requirement groups, the non-goals and
  the acceptance criteria before drafting (2026-10-10, chat); they are
  recorded under Goals, Non-Goals and Requirements. Every design detail below
  is a proposal with alternatives and a recommendation.
- **Related ADR(s)** (all `Proposed`, drafted alongside; none is a decision
  until accepted):
  - [ADR-0114](../adr/0114-profiling-api-build-configuration-and-recorder.md):
    the profiling API, its build configuration and the in-process recorder
    (Q1, Q2, Q4, Q8, Q12);
  - [ADR-0115](../adr/0115-rhi-instrumentation-boundary.md): what crosses the
    RHI and Vulkan Backend boundary — regions, GPU timestamps, debug names,
    backend counters, RenderGraph pass labels (Q5, Q6, Q7);
  - [ADR-0116](../adr/0116-tracy-profiler-dependency.md): Tracy as a new
    third-party dependency (Q3);
  - [ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md):
    the benchmark's host, command surface, JSON schema and baseline comparison
    (Q9–Q11).

  [ADR-0006](../adr/0006-dependency-management.md) (pinned `FetchContent`),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md) (single frame thread),
  [ADR-0008](../adr/0008-logging.md)'s diagnostics-infrastructure
  precedent, [ADR-0001](../adr/0001-rhi-backend-independence.md) and
  [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md) are
  unchanged.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Bistro and other complex scenes run at low frame rates, and nothing in the
engine can say why: there is no timing code anywhere in the frame path, no GPU
timestamp, no draw-call counter, and no way to see a pass or a resource by
name in a GPU capture. This Spec adds **measurement only**:

```
Atlantis Profiling API (ATL_PROFILE_*; one header in Core; compile-time off = no-ops)
├── Tracy CPU/GPU timeline          (CPU zones, GPU zones, counter plots)
├── RenderDoc-friendly debug names  (VK_EXT_debug_utils: objects + pass labels)
└── Counters + fixed Bistro benchmark (in-process recorder → JSON, baseline compare)
```

Business code includes one Core header and never sees Tracy. The recorder, the
counters and the per-pass GPU timestamps are Atlantis's own and work without
Tracy; Tracy is one viewer of the same events. Nothing is optimized here: the
deliverable is the evidence that lets the next Spec say, from numbers, whether
Bistro is CPU-bound, GPU-bound, submission-bound, lighting-bound, overdraw-bound,
sync-bound or upload-bound.

## Motivation / Problem Statement

### What the frame does today (verified 2026-10-10)

- **One frame function, no timing.** `RuntimeApplication::runFrame()`
  ([runtime_application.cpp:1177](../../src/runtime/src/runtime_application.cpp))
  runs, in order: `applyPending()` (World commands), the Platform event pump,
  swapchain acquire, format/extent-change checks, camera and lighting
  extraction (`collectActiveCamera`, `collectLights`, `collectRenderables`,
  uniform writes), pending-material realization, the `DrawItem` walk,
  `Renderer::drawFrame()`, `Device::submit()` and `Presentation::present()`.
  Nothing times any of it; the only measurement in the repository is a
  `WARN`-printed mean in the Bistro image-regression test
  ([bistro_scene_gpu_tests.cpp](../../tests/image_regression/bistro_scene_gpu_tests.cpp)),
  which includes readback and `waitIdle()` and measures the headless fixture,
  not the Runtime.
- **Renderer passes** (`Renderer::drawFrame()`,
  [renderer.cpp:116-451](../../src/renderer/src/renderer.cpp)): `shadow`
  (depth-only), `draw` (sky, then non-blended items, then blended items back to
  front, in one dynamic-rendering scope), `bloom_down_1..6`, `bloom_up_5..1`,
  `bloom_composite` (only when bloom is on) and `output_transform`. **There is
  no fog pass and no separate opaque/transparent pass**: fog is a per-fragment
  term inside the ten PBR shaders (Spec 0043) and the transparent items are the
  tail of `draw`'s item loop (ADR-0090). The owner's GPU zone list
  (Shadow, Opaque, Transparent, Fog, Bloom, ToneMap) therefore maps onto passes
  only in part — see Q7.
- **RenderGraph execution** (`render_graph::execute()`,
  [execution.cpp:50](../../src/render_graph/src/execution.cpp)): per pass it
  records transitions, `beginRendering`, the pass callback, `endRendering`. Pass
  labels are given at declaration (`declarePass("shadow")`) and survive
  `compile()` (`CompiledGraph::label(pass)`), but nothing in the RHI can receive
  a label.
- **The Vulkan Backend has no instrumentation hooks.** `VK_EXT_debug_utils` is
  enabled only as the validation messenger's extension (Debug builds, not
  Android, [vulkan_instance.cpp:327-333](../../src/vulkan_backend/src/vulkan_instance.cpp));
  no object is named, no `vkCmdBeginDebugUtilsLabelEXT` is ever called, no query
  pool exists.
- **One frame in flight, with the wait inside `submit()`.**
  `VulkanDevice::submit()` first calls `waitAndReleaseRetainedSubmission()`
  (`vkWaitForFences`, UINT64_MAX) on the previous frame's fence
  ([vulkan_device.cpp:556-663](../../src/vulkan_backend/src/vulkan_device.cpp)).
  The CPU's wait for the GPU is thus hidden inside the "submit" step, so any
  honest CPU number must separate that wait from CPU work. Presentation is
  `VK_PRESENT_MODE_FIFO_KHR` (vsync,
  [vulkan_presentation.cpp:429](../../src/vulkan_backend/src/vulkan_presentation.cpp)).
  A frame that realizes materials also calls `waitIdle()` after `submit()`
  (runtime_application.cpp:1882) — a full stall that must be visible as such.
- **Draw state is recorded per item.** Per `DrawItem`, `Renderer` calls
  `bindPipeline`, `bindVertexBuffer`, `bindIndexBuffer`, `bindUniformBuffer`,
  up to four `bindTexture`, one `pushConstant`, `drawIndexed`; the Vulkan
  `bindUniformBuffer`/`bindTexture` each issue `vkCmdBindDescriptorSets` and a
  `vkUpdateDescriptorSets` unless a per-command-list memo says the write is
  redundant ([vulkan_command_list.cpp:413-512](../../src/vulkan_backend/src/vulkan_command_list.cpp)).
  How often the memo hits is unknown today.
- **There is no culling.** No frustum or occlusion test exists in
  Runtime, Renderer or World (`grep` for frustum/cull finds only a comment); every
  resolved renderable is drawn, and, with a directional light, drawn again by the
  `shadow` pass. "Culled renderables" is therefore 0 today by construction.
- **Bistro scale** (Spec 0046, bistro_scene_gpu_tests.cpp): 2909 renderables,
  368 skipped per frame by the conformal-transform rule, so 2541 `DrawItem`s;
  551 meshes, 254 materials, 59 point lights plus a directional light, fog and
  bloom from the committed overlay camera. The fifth `--scene` entry, present
  only when `content/bistro/` was fetched at configure time.
- **The window size is not controllable.** The Windows window is created with
  `CW_USEDEFAULT` and the Runtime renders at whatever the swapchain is
  ([windows_platform.cpp:316](../../src/platform/src/windows/windows_platform.cpp));
  `platform::initialize()` takes no arguments. A "fixed resolution" benchmark
  needs a decision (Q10).
- **Logging goes to stdout.** `ConsoleLogSink` writes Info to stdout, so any
  command that prints JSON to stdout must redirect the log sink (Q9).

### What the code suggests, to be tested and not concluded

Candidate causes the instrumentation must be able to confirm or refute, listed
only so the counters below are chosen to discriminate them: about 5,000 draws
per frame (items drawn by `shadow` and by `draw`); per-item descriptor writes
and binds; whole-world `collectRenderables`/`collectLights` plus several
per-frame `std::vector`/map rebuilds in `runFrame()`; the in-`submit()` fence
wait serializing CPU and GPU by one frame; 59 point lights in the per-frame
lighting uniform (Spec 0040); fog evaluated per fragment; overdraw with no culling. This
Spec decides none of them.

## Goals

Owner-set (2026-10-10), not to be relaxed in review:

- **A lightweight profiling abstraction** that business code uses instead of
  Tracy, with compile-time disable giving no-ops, near-zero cost and zero
  behavior change.
- **Tracy CPU and GPU zones** on the frame's real stages and passes.
- **RenderDoc-readable captures**: named objects and labelled passes.
- **Performance counters** covering frame time, draws, triangles, renderables,
  lights, state changes, upload bytes and per-pass GPU time.
- **A fixed Bistro benchmark** with a JSON result, percentiles, counters and
  baseline comparison, **without a CI gate**.

Goals of this Spec within those boundaries:

- **Measure first, optimize later.** No optimization, no behavior change, no
  image change, with the instrumentation compiled in or out.
- **Evidence for a bottleneck conclusion**, defined concretely in the
  "Bottleneck evidence" table below, so the next Spec can be justified by the
  benchmark's output.

## Non-Goals

Owner-set (2026-10-10):

- GPU-driven rendering or a GPU scene; GPU culling or indirect draw;
  Forward+ or clustered lighting; a job system; a renderer rewrite; SSAO, SSR,
  TAA or ray tracing; **any Bistro optimization**; unrelated refactors.

Also out of scope (proposed by the draft):

- **Culling of any kind**, even though the counters will show its absence.
- **A new present mode, frame pacing or a second frame in flight.** The
  benchmark measures the frame loop as it is (Q10).
- **CI integration or a pass/fail performance gate.** Baseline comparison
  reports deltas and never fails a run (R13).
- **New World or Remote protocol methods.** `atlantis.remote/1` is unchanged
  (ADR-0106); the C# SDK and its conformance vectors are not touched.
- **An in-engine profiler UI** (the Editor is not extended), GPU crash
  diagnostics, memory profiling, shader-level profiling, multi-queue or
  multi-threaded profiling (ADR-0004's single frame thread holds).
- **Android measurement.** Android builds with profiling off (Q8, R10).

## Requirements

### Functional

- **R1 — One abstraction** (Q1). A single Core header, `atlantis/profile.h`,
  provides at least `ATL_PROFILE_FRAME()`, `ATL_PROFILE_SCOPE(name)`,
  `ATL_PROFILE_GPU_SCOPE(cmd, name)` and `ATL_PROFILE_VALUE(name, value)`, plus
  the accumulating `ATL_PROFILE_ADD(name, delta)` the backend counters need.
  No other module includes a Tracy header or names a Tracy type; a boundary test
  scans for it.
- **R2 — Compiled out means gone** (Q2). With profiling disabled every macro
  expands to a no-op; **arguments are not evaluated** (so a disabled build
  cannot differ by a side effect); no symbol, branch, allocation or Tracy
  header is added to a consumer's translation unit; public struct layouts and
  virtual-function sets do not depend on the build option (no ODR hazard).
  Goldens and behavior are unchanged.
- **R3 — CPU zones** (Q4, Q7). The frame emits the CPU zones in the "Zone map"
  below, including separate zones for the three blocking waits (previous-frame
  fence, swapchain acquire, post-realization `waitIdle()`), and one zone per
  RenderGraph pass recording.
- **R4 — GPU zones** (Q5, Q7). Every RenderGraph pass, and the `draw` pass's
  sky/opaque/transparent segments, is bracketed by a region that yields a GPU
  timestamp pair; results are delivered one frame late (after the fence wait
  already in `submit()`), with no new stall. Timestamp support is optional:
  absent support is reported as absent, not as an error.
- **R5 — Names and labels** (Q6). With markers enabled and
  `VK_EXT_debug_utils` available, every Buffer, Texture, SampledTexture,
  Sampler, HdrColorTarget, ShadowMap, offscreen target, Pipeline, descriptor
  pool/set and command buffer the engine creates carries a stable human-readable
  name, and every RenderGraph pass is wrapped in a debug label named after its
  declaration label, **automatically** (a pass author adds nothing). A RenderDoc
  capture shows passes and resources by name.
- **R6 — Counters** (Q8). The engine records, per frame, every counter in the
  "Counter set" below; each has one owner layer and one definition.
- **R7 — Tracy** (Q3). With Tracy enabled, a Tracy viewer connected to a running
  `atlantis_runtime` shows the CPU zones, the GPU zones on a GPU timeline and the
  counters as plots. Tracy listens on loopback only.
- **R8 — Benchmark** (Q9–Q11). A command-line run loads a named scene (the
  committed baseline is `bistro`), applies fixed settings, runs `--warmup` then
  `--frames` presented frames and writes one JSON document (`--json`).
- **R9 — Results** (Q12). The JSON carries, for frame time, CPU time, GPU time,
  every zone, every pass and every counter: average, p50, p95, p99 (plus min,
  max, and the sample count), and the metadata that decides comparability.
- **R10 — Build configuration** (Q2). Three CMake options with the defaults in
  Q2; profiling is **off by default**; on Android it is off and cannot be turned
  on.
- **R11 — Baseline comparison** (Q12). `--baseline <file>` adds a `comparison`
  section: per-metric absolute and relative deltas, and an explicit
  `comparable: false` with reasons when scene, content, resolution, variant,
  build or hardware differ. It never returns a failure status for a slowdown.
- **R12 — No behavior change.** With instrumentation compiled in, off at
  runtime or compiled out, rendered images are unchanged (all existing goldens
  byte-identical) and the frame's command stream differs only by labels and
  timestamp writes.
- **R13 — No CI gate.** Nothing in CTest or CI fails on a measured time.
- **R14 — Existing contracts hold.** `runFrame()` gains only `ATL_PROFILE_*`
  lines (held by a diff guard of the Spec 0055/0056 kind); no `Vk*` type
  appears outside the Vulkan Backend; the RenderGraph stays the path for GPU
  work.

### Non-functional

- **Performance:** compiled out, zero. Compiled in with no recorder active and
  no Tracy client connected, at most a predictable branch per macro. Overhead
  with the recorder, and with Tracy connected, is **measured and reported** in
  the implementation PR (not gated); the Plan records the method.
- **Memory:** the recorder and counters use fixed-capacity per-frame storage
  and a bounded sample store (the benchmark's `frames` plus warmup); no
  allocation on the frame path after the first measured frame.
- **Portability:** Windows measures; Android compiles every RHI addition as a
  no-op. Vulkan 1.0 baseline is kept (timestamp queries and
  `vkCmdResetQueryPool` are core 1.0).
- **Threading:** every API is for the frame thread (ADR-0004). Tracy's own
  internal thread is a third-party implementation detail, not an engine
  threading model (ADR-0116).
- **Determinism:** the instrumented frame's *output* is deterministic; measured
  times are not, and the JSON says so by carrying distributions, not a single
  number.

### Zone map (the frame's CPU zones)

| Zone | Where | Notes |
|---|---|---|
| `Frame` | `runFrame()` | One per call; `ATL_PROFILE_FRAME()` also marks the frame boundary. |
| `Runtime` | events, resize/format checks, material realization, post-submit publish | The Runtime's own orchestration. |
| `World` | `applyPending()` | Command application to the World. |
| `RenderExtraction` | camera, lighting, `collectRenderables`, `DrawItem` walk, uniform writes | Scene → `DrawItem`s. |
| `Renderer` | `Renderer::drawFrame()` | Inclusive; builds the graph. |
| `RenderGraph` | `compile()` + `execute()` | Inclusive of pass recording. |
| `RecordPass:<label>` | each pass callback | Transient name from the pass label. |
| `Submit`, `Present` | `device_->submit()`, `presentation_->present()` | `Submit` contains `WaitPreviousGpu`. |
| `WaitPreviousGpu`, `Acquire`, `WaitIdle` | fence wait, `acquireNextTarget()`, post-realization `waitIdle()` | The three blocking waits, so `cpu_active_ms = Frame − waits` is definable. |

### GPU regions

| Region | Source | Note |
|---|---|---|
| `shadow`, `draw`, `bloom_down_n`, `bloom_up_n`, `bloom_composite`, `output_transform` | RenderGraph, automatic | The pass labels as declared today. |
| `Bloom` | parent of the `bloom_*` passes | Present only with bloom on. |
| `Sky`, `Opaque`, `Transparent` | `draw` pass, in `Renderer` | Contiguous segments of the existing item order (non-blended then blended). |
| *(Fog)* | none | Fog has no pass; Q7 proposes differential measurement instead. |

### Counter set

| Key (JSON, plots) | Definition | Owner layer |
|---|---|---|
| `frame_ms` | wall time between successive `Frame` starts | recorder |
| `cpu_ms`, `cpu_active_ms` | `Frame` zone; `Frame` minus the three waits | recorder |
| `gpu_busy_ms`, `gpu_span_ms` | Σ top-level pass regions; command-buffer first→last timestamp | Vulkan Backend |
| `passes.<label>.gpu_ms`, `.cpu_record_ms` | region pair; `RecordPass` zone | Vulkan Backend / RenderGraph |
| `draw_calls`, `triangles` | `drawIndexed` count; Σ index count / 3 | Vulkan Backend |
| `renderables_total`, `_drawn`, `_skipped`, `_culled` | collected; `DrawItem`s built; total − drawn; **0 until a culling feature exists** | Runtime |
| `lights_directional`, `lights_point` | counts in the extracted lighting block | Runtime |
| `pipeline_binds`, `descriptor_set_binds`, `descriptor_writes`, `vertex_buffer_binds`, `index_buffer_binds`, `push_constants` | counted at the command list; `descriptor_writes` counts real `vkUpdateDescriptorSets` after the memo | Vulkan Backend |
| `upload_bytes`, `uniform_write_bytes`, `materials_realized` | bytes recorded into buffer→texture copies; mapped uniform bytes written by the frame; materials realized this frame | Vulkan Backend / Runtime |
| `wait_previous_gpu_ms`, `acquire_ms`, `present_ms`, `wait_idle_ms` | the wait zones | recorder |
| `fragment_invocations`, `ia_primitives` *(optional)* | pipeline-statistics query on `draw`; absent without hardware support | Vulkan Backend |

### Bottleneck evidence

The JSON's job is to make these calls from numbers (thresholds are the
reader's judgment, not engine constants):

| Conclusion | Decisive evidence |
|---|---|
| GPU-bound | `wait_previous_gpu_ms` is a large share of `frame_ms` **and** `gpu_busy_ms ≈ frame_ms`. |
| CPU-bound | `cpu_active_ms ≈ frame_ms`, `wait_previous_gpu_ms ≈ 0`, `gpu_busy_ms < cpu_active_ms`. |
| Draw submission | `passes.draw.cpu_record_ms` dominates `cpu_active_ms`; cost per draw = that ÷ `draw_calls`; read with `descriptor_writes`, `pipeline_binds`. |
| Extraction / World | `RenderExtraction` or `World` zone dominates `cpu_active_ms`. |
| Lighting | `no-point-lights` variant (Q11): drop in `passes.draw.gpu_ms` vs `baseline`, read with `lights_point`. |
| Fog / bloom | `no-fog` / `no-bloom` variants; `Bloom` region time. |
| Overdraw | `fragment_invocations ÷ (width × height)`; `triangles`; `passes.shadow.gpu_ms`. |
| Sync | `wait_previous_gpu_ms`, `acquire_ms`, `present_ms`, `wait_idle_ms` against `frame_ms`. |
| Resource upload | `upload_bytes`, `materials_realized`, `wait_idle_ms` — visible in the warmup frames; steady-state ≈ 0 is itself the finding. |

## Proposed Design

```
        business code  ── #include <atlantis/profile.h>  (Core) ──► ATL_PROFILE_* macros
                                                                      │ (compiled out: nothing)
 Core profile impl ── recorder (zones, counters, samples) ──► benchmark JSON (Runtime-private driver)
        │                                  └──────────────► Tracy CPU zones/plots (atlantis_tracy, Core-private)
        │
 RHI:  CommandList::beginRegion/endRegion/setDebugName,  *CreateParams::debugName  (additive)
        │
 Vulkan Backend: VK_EXT_debug_utils labels + names ─► RenderDoc
                 timestamp query pool (1 frame in flight) ─► recorder + TracyVk context (Tracy GPU timeline)
                 command-list counters ─► ATL_PROFILE_ADD
 RenderGraph execute(): wraps each pass in beginRegion/endRegion(label)   [automatic]
```

The detailed decisions are the open questions below, each with options and a
recommendation; the ADRs carry the recommended answers in decision form.

## Architectural Impact

Yes. Four ADRs (all `Proposed`):

- **[ADR-0114](../adr/0114-profiling-api-build-configuration-and-recorder.md)**
  — the macro API and its home in Core; the stated exception for global
  diagnostics state (the recorder, like the log sink); the three build options
  and defaults; the recorder and counter model; Android (Q1, Q2, Q4, Q8, Q12).
- **[ADR-0115](../adr/0115-rhi-instrumentation-boundary.md)** — **a public RHI
  change**: `CommandList::beginRegion/endRegion/setDebugName` and an additive
  `debugName` on creation params; timestamps and debug utils inside the Vulkan
  Backend; `render_graph::execute()` wraps passes automatically (Q5, Q6, Q7).
- **[ADR-0116](../adr/0116-tracy-profiler-dependency.md)** — a **new
  third-party dependency**, Tracy, and the first one fetched for profiling
  only (Q3).
- **[ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md)**
  — the benchmark's host (an executable-private driver in `atlantis_runtime`),
  the fixed-resolution mechanism (a **Platform addition** if Q10 (a) is
  chosen), and the JSON schema as a versioned data format (Q9–Q11).

**Expected code impact** under the recommendations:

- **New:** `src/core/include/atlantis/profile.h` and `src/core/src/profile*.cpp`;
  `cmake/AtlantisTracy.cmake`; an executable-private benchmark driver beside
  `src/runtime/cli.*` / `editor_attachment.*`; `tests/core/profile_*`;
  `tests/runtime/benchmark_*`; a short operator note in `docs/process/`.
- **Changed (additive instrumentation only):** `src/rhi` (CommandList and
  create-param fields), `src/render_graph/src/execution.cpp`,
  `src/renderer/src/renderer.cpp`, `src/runtime/src/runtime_application.cpp`
  and `src/runtime/main.cpp`, `src/runtime/cli.*` (`--benchmark` flags),
  `src/vulkan_backend` (labels, names, queries, counters, instance extension),
  top-level and module `CMakeLists.txt`, and — only under Q10 (a) —
  `src/platform` (a requested initial client size).
- **Docs with the implementation:** AGENTS.md (the Core diagnostics exception
  and the dependency), `module_boundaries.md`, `docs/architecture/` notes.
- **Unchanged:** the Remote protocol and C# SDK, the World/Asset/Shader
  modules, Android behavior, every golden.

## Alternatives Considered

Compared inside the questions. Rejected across them: **sampling-only
profilers** (no per-pass or GPU data); **a built-in Atlantis timeline UI**
(scope, and the Editor is out); **instrumenting through Remote/CLI only**
(frame-boundary latency distorts the thing measured, and the protocol would
change); **making the benchmark a test** (a performance number must not turn
into a pass/fail unit test, R13).

## Testing & Verification Plan

- **Compiled out (GPU-independent):** a translation unit built with profiling
  off expands every macro to nothing — arguments with side effects are provably
  not evaluated, no Tracy header is reachable from it; layouts of the RHI
  structs and the `CommandList` vtable are identical across the three build
  configurations (static asserts / a size-and-offset table test).
- **Recorder and statistics (GPU-independent):** zone nesting and per-frame
  aggregation; counters (`VALUE` last-wins, `ADD` sums); percentile definition
  (nearest rank on sorted samples) against hand-computed vectors; bounded
  storage; frames that never reached `Present` are excluded.
- **Boundary scans** (the `cli_boundary_tests.cpp` shape): no Tracy include
  outside Core's profile implementation and the Vulkan Backend's GPU-context
  file; no `Vk*` or Tracy type in `profile.h`, RHI, RenderGraph, Renderer or
  Runtime headers.
- **RenderGraph (fake command list, no GPU):** `execute()` emits
  `beginRegion(label)`/`endRegion()` around every pass, properly nested and in
  compiled order; a pass without a label gets the documented fallback.
- **Vulkan Backend (GPU tests under fatal VVL):** object names retrievable
  through a debug-utils test hook; labels balanced; timestamp results monotonic
  and delivered one frame late without a new `vkWaitForFences`; absent
  timestamp support (simulated) reports absence; counters exact on a fixture
  with a known draw sequence (`draw_calls`, `triangles`, `descriptor_writes` vs
  the memo).
- **Image goldens:** the full suite (all 67 × 2 existing goldens) byte-identical
  with profiling **off**, **on**, and **on + Tracy**; `runFrame()` diff guard:
  only `ATL_PROFILE_*` lines added.
- **Benchmark:** JSON schema validity test; a fake-clock run that proves the
  warmup/frames accounting; self-comparison yields zero deltas and
  `comparable: true`; a changed resolution/variant yields `comparable: false`
  with reasons; exit codes; stdout is exactly one JSON document (logs on
  stderr). Content-gated `bistro` run (SKIP without content, the existing
  pattern).
- **Human acceptance (reported in the implementation PR, with screenshots):**
  (1) Tracy viewer on a live `atlantis_runtime --scene bistro` showing CPU zones,
  GPU timeline and counter plots; (2) a RenderDoc capture of the same showing
  every pass and every resource by name; (3) `--benchmark bistro` JSON from the
  command line, run twice, compared; (4) the written bottleneck conclusion for
  Bistro derived from that JSON using the evidence table (the conclusion is
  reported, not a Spec output).
- **Android:** `assembleDebug` unchanged; the configure log shows no Tracy fetch;
  `ATLANTIS_PROFILING=ON` is refused or forced off with a message.
- **Overhead report (not gated):** frame time OFF / ON / ON + Tracy idle / ON +
  Tracy connected on the benchmark scene.

## Risks & Open Questions

Risks:

- **Probe effect.** Timestamps and zones perturb what they measure; GPU
  timestamps at `ALL_COMMANDS`-style stages can serialize the pipeline. The
  overhead report quantifies it; `--gpu-timing=off` isolates CPU numbers.
- **Vsync caps the wall clock.** FIFO presentation quantizes `frame_ms` to the
  refresh interval for any frame faster than it; below it, as with Bistro today,
  it does not bind. `cpu_active_ms` and `gpu_busy_ms` are the unthrottled
  numbers (Q10).
- **GPU variance.** Clocks, thermal state and background load move results; the
  JSON carries distributions and hardware/driver metadata, and comparison refuses
  mismatches rather than guessing.
- **Tracy version lock.** The viewer must match the client's protocol version;
  a mismatch refuses to connect (ADR-0116 pins both).
- **`runFrame()` is touched.** Its byte-identical guarantee, relied on by Specs
  0055–0058, becomes "plus `ATL_PROFILE_*` lines"; the diff guard is updated in
  the Plan, not silently.
- **Hypotheses are not findings.** The "what the code suggests" list is a
  checklist for the counters; this Spec makes no performance claim.

Open questions. Each lists options and a recommendation; **Judgment wanted**
marks the ones I would most like the owner to rule on.

- **Q1 — Macro home and shape.** *Judgment wanted.*
  - (a) **Core header `atlantis/profile.h`** — Core is already the home of
    assertions and logging and every module depends on it; no new graph edge.
  - (b) A new "Atlantis Profiling" module — a new edge into RHI, RenderGraph,
    Renderer, Runtime and the backend for ~one header.
  - (c) A header per module — duplicates the abstraction.
  - **Recommend (a).** Prefix: the owner's examples are `ATL_PROFILE_*`; the
    repository's existing macros are `ATLANTIS_CHECK`/`ATLANTIS_LOG_*`. I keep
    the owner's spelling as the public API (`ATL_PROFILE_*`) because the
    examples are explicit, and flag the inconsistency; `ATLANTIS_PROFILE_*` is
    the drop-in alternative if you prefer uniformity.
- **Q2 — Build configuration and defaults.** *Judgment wanted.*
  - Options: (a) three independent CMake options — `ATLANTIS_PROFILING`
    (macros, recorder, counters, timestamps), `ATLANTIS_PROFILING_TRACY`
    (Tracy sink), `ATLANTIS_GPU_MARKERS` (debug-utils names and labels);
    (b) one switch; (c) per-configuration defaults.
  - **Recommend (a)** with defaults `PROFILING=OFF`, `PROFILING_TRACY=ON`
    (effective only with PROFILING; `-DATLANTIS_PROFILING_TRACY=OFF` avoids the
    fetch), `GPU_MARKERS=ON` on Windows and OFF on Android. A default build thus
    fetches nothing new and is behaviorally what it is today, except that markers
    request `VK_EXT_debug_utils` when available. Markers are separate so RenderDoc
    use never needs the profiler. On Android, `PROFILING` and `GPU_MARKERS` are
    forced OFF (the existing Android debug-utils gating stands).
- **Q3 — Tracy: pinning and integration.**
  - (a) **`FetchContent` of the release archive by URL + SHA-256** (the
    `AtlantisImGui.cmake` shape), `v0.14.1` (released 2026-08-22, BSD-3-Clause),
    compiled as a private static library `atlantis_tracy` from `public/TracyClient.cpp`
    with `TRACY_ENABLE`, `TRACY_ON_DEMAND`, `TRACY_ONLY_LOCALHOST`; the viewer is a
    developer-installed tool of the matching release, never fetched (ADR-0006's
    host-tool category).
  - (b) Vendored source — rejected by ADR-0006. (c) Git submodule — rejected by
    ADR-0006. (d) A system install of Tracy — unpinned, not reproducible.
  - (e) No third-party profiler: the recorder plus a Chrome-trace JSON export —
    zero dependency, but no live GPU timeline, plots or tooling; recorded as the
    fallback if the owner declines the dependency, and the recorder supports it.
  - **Recommend (a).** Integration shape: Tracy headers are included only by
    Core's profile implementation (via Tracy's C API, so `profile.h` exposes a
    plain source-location struct) and by one Vulkan Backend file (the GPU
    context). The archive hash is measured in the Plan (the ImGui precedent).
- **Q4 — Recorder architecture.**
  - (a) **An in-process recorder that is independent of Tracy**; Tracy is a sink.
    The benchmark needs per-zone, per-pass and per-counter distributions
    headlessly and Tracy cannot hand them to the process.
  - (b) Tracy-only, benchmark post-processes a Tracy capture — needs the viewer's
    CLI tools in the loop, not "from a command line, one command".
  - **Recommend (a).** The recorder is global diagnostics state in Core, the
    deliberate exception AGENTS.md already allows for logging infrastructure
    (ADR-0114 states it as a second instance, not a precedent for engine state).
- **Q5 — GPU timestamps across the RHI/Vulkan boundary.**
  - RHI shape: (a) **`CommandList::beginRegion(std::string_view)` /
    `endRegion()`**, non-pure virtual with empty bodies (the one test fake keeps
    compiling); the Vulkan implementation owns *all* of: debug label, timestamp
    pair, Tracy GPU zone. (b) Separate label and zone pairs — double the surface,
    two call sites per pass. (c) Timestamps as RenderGraph-owned resources —
    puts a query object in the graph vocabulary for diagnostics only.
  - Timing source (inside the backend): (G1) **Atlantis's own query pool**
    (one pool, since one frame is in flight; reset at the start of each command
    list; results read after the fence wait `submit()` already performs), with
    **Tracy's own Vulkan context fed separately** from the same region calls
    (`TracyVkZone`); (G2) Atlantis timestamps only, replayed into Tracy through
    its low-level queue API — unsupported surface, rejected; (G3) Tracy-only
    timing — the benchmark would have no per-pass numbers.
  - **Recommend (a) + G1.** Two timestamp writes per region instead of one is
    cheap and keeps the benchmark independent of Tracy and of Tracy's internals.
    Region capacity per frame is fixed (overflow is counted, never an error).
- **Q6 — Debug names and labels across the boundary.**
  - (N1) **A `debugName` field (`std::string_view`, default empty) on every
    creation-params struct, plus `CommandList::setDebugName`**; the backend names
    the Vk objects (buffer, image, image view, sampler, pipeline, descriptor
    pool/set derived from their pipeline, memory) at creation. Layouts are
    identical in every build configuration. (N2) `Device::setDebugName(handle, name)`
    after creation — every creation site grows a second call and the RHI would need
    a handle type. (N3) An ambient "next object name" scope in Core — hidden state,
    rejected.
  - Names come from the creating site: scene resources as `kind:short-guid`
    (plus the catalog logical path where it is cheaply available), engine
    resources by role (`hdr_color`, `depth`, `shadow_map`, `bloom_d1`, …). Name
    strings are built only when markers are on (the macro-guarded helper, so
    Release-without-markers formats nothing).
  - **Recommend N1.** Pass labels come from `CompiledGraph::label(pass)` in
    `execute()` — no pass author action (R5).
- **Q7 — The owner's GPU zones versus the real passes.** *Judgment wanted.*
  - Reality: Shadow = `shadow`; ToneMap = `output_transform`; Bloom = the
    `bloom_*` passes (grouped under a `Bloom` region); **Opaque and Transparent
    are segments of one pass**, and **Fog is not a pass at all**.
  - (a) **Pass regions automatically; `Sky`/`Opaque`/`Transparent` sub-regions
    inside `draw` (contiguous in the existing order, so no reordering); Fog by
    differential measurement** (`no-fog` variant, Q11). (b) Split `draw` into
    passes so the names match — a RenderGraph/Renderer change that alters
    barriers and possibly images; violates "no renderer rewrite" and R12.
    (c) Report fog as "not separable" and stop — loses an evidence-table row.
  - **Recommend (a).**
- **Q8 — Counter plumbing.**
  - (a) **Counters are accumulated where they are exact** — draw-level and
    state-change counters in the Vulkan `CommandList` (they alone see the
    descriptor-memo hit/miss), extraction counts in Runtime — all through
    `ATL_PROFILE_ADD`/`ATL_PROFILE_VALUE` into the Core recorder; the recorder
    snapshots at the next `ATL_PROFILE_FRAME()` and forwards to Tracy plots.
    (b) A stats struct returned by `CommandList`/`Device` — an RHI API for
    diagnostics, and Renderer/Runtime would have to forward it. (c) Counting in
    Renderer from what it submits — cannot see descriptor writes.
  - **Recommend (a).** String-named macros register their name once per call
    site (a function-local id), so a hot-path add is an array increment.
- **Q9 — Benchmark host and command surface.** *Judgment wanted.* The CLI
  library links Atlantis Connection only and the `atlantis` executable is a
  Remote client that renders nothing, so `atlantis benchmark bistro …` cannot
  itself run a frame.
  - (a) **`atlantis_runtime --benchmark bistro --warmup 120 --frames 300 --json`**:
    flags in the executable-private startup layer (`cli.cpp`, beside `--exec`,
    `--listen`, `--editor`), a driver that owns the measured loop, applies variants
    through the Runtime's own `RuntimeConnection`, and writes JSON. No module rule
    changes; `atlantis_runtime_host` gains nothing it cannot test GPU-free.
  - (b) A thin `atlantis benchmark …` subcommand that spawns (a) — keeps the
    owner's spelling at the cost of process-launch code in the `atlantis`
    executable (OS-specific, outside what that executable does today); a clean
    follow-up, not required for the capability.
  - (c) Drive a `--listen` Runtime over Remote with `control.step` and a new
    `control.profile` method — changes `atlantis.remote/1` (ADR-0106
    supersession, C# SDK conformance), puts frame-boundary latency into the
    measurement, and does not fix the resolution.
  - **Recommend (a)**, with (b) named as future work. This departs from the
    literal `atlantis benchmark …` spelling; I need your ruling that (a) satisfies
    the requirement.
- **Q10 — Fixed resolution and present mode.** *Judgment wanted.*
  - (a) **A requested initial client size in Platform** (Windows creates the
    window with `AdjustWindowRectEx` for that client size; Android ignores it),
    default `1920×1080`; the run records the actual swapchain extent and exits
    non-zero if it differs. Exercises the real windowed loop, DPI-correct (the
    process is per-monitor-DPI-aware v2). Cost: one additive Platform API change.
  - (b) Render the scene to a fixed-size offscreen Viewport through the
    `FrameOverlay` mechanism (Spec 0056) with a no-op overlay — no Platform
    change, but the path differs from production (extra offscreen target and
    overlay pass).
  - (c) Accept the OS-chosen size, record it, refuse to compare across sizes —
    "fixed resolution" would not be met.
  - Present mode stays FIFO in every option: changing it is an RHI/Presentation
    change outside this Spec; the benchmark reports `present_mode` and
    `refresh_hz` in its metadata and relies on `cpu_active_ms`/`gpu_busy_ms` for
    unthrottled numbers.
  - **Recommend (a)** at 1920×1080.
- **Q11 — Diagnostic variants.**
  - (a) **A closed list: `baseline` (the default and the only committed
    baseline), `no-point-lights`, `no-fog`, `no-bloom`**, each applied as World
    commands before warmup (the same edits the Bistro golden test already makes).
    They exist to answer lighting/fog/bloom questions the zones cannot, and the
    JSON records the variant so comparison refuses to mix them. (b) None — loses
    the lighting and fog evidence rows. (c) Arbitrary `--set Type.field=value` —
    an open surface.
  - **Recommend (a).**
- **Q12 — JSON schema, statistics and comparison.**
  - Schema `atlantis.benchmark/1` (ADR-0117 pins the field list): `schema`,
    `meta` (build type, flags, validation on/off, compiler, git revision, OS,
    CPU, GPU, driver, Vulkan version, timestamp period, present mode, refresh
    rate), `scene` (name, scene GUID, content pin), `settings` (resolution,
    warmup, frames, variant, gpu-timing), `frames` (measured, discarded, skipped),
    `metrics` (each `{avg, p50, p95, p99, min, max, n}`; zones; passes; counters),
    optional `series` (`--series`, raw per-frame values), `comparison`.
  - Percentiles: nearest-rank on the sorted measured samples (stated in the
    schema; comparisons depend on it). (b) Interpolated percentiles — rejected,
    ambiguous across tools. Output: one JSON document on stdout, logs on stderr
    (the benchmark swaps the log sink); `--output <path>` writes a file instead.
    Debug builds, validation-enabled runs and builds without profiling are
    refused or flagged (`meta`), never silently measured.
  - Comparison: compares like-for-like keys; reports `delta`, `delta_pct`, and
    each side's p95−p50 spread as a noise hint; `comparable: false` with reasons
    on any metadata mismatch; **no verdict, no failing exit code** (R11, R13).
  - **Recommend as written.**

## Out of Scope / Future Work

- Optimizing anything the benchmark exposes (the next Spec, justified by its
  numbers); culling, GPU-driven work, Forward+ (owner non-goals).
- `atlantis benchmark …` as a launcher subcommand (Q9 (b)); a CI baseline store
  and a regression gate; additional benchmark scenes; per-frame CSV/Chrome-trace
  export; unthrottled presentation (a present-mode parameter on `Presentation`);
  multi-frame-in-flight timing; GPU memory profiling; Android profiling.
