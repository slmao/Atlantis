# ADR 0117: Benchmark Host, JSON Schema and Baseline Comparison

- **Status:** Accepted
- **Date:** 2026-10-10 (accepted 2026-10-10)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-10 (chat rulings on Spec 0059 Q1–Q12; review of
  Spec 0059's own branch PR, [PR #242](https://github.com/slmao/Atlantis/pull/242)).
- **Related Spec:** [Spec 0059](../specs/0059-profiling-instrumentation.md) (`Approved`)
- **Related ADR(s):**
  - [ADR-0105](0105-runtime-connection-and-cli-client.md): Atlantis CLI links
    Atlantis Connection only — preserved.
  - [ADR-0106](0106-attachable-runtime-transport-and-control.md): the Remote
    protocol — unchanged.
  - [ADR-0108](0108-editor-host-and-viewport-composition.md): the precedent for
    an executable-private adapter beside `atlantis_runtime_host`.
  - Paired with [ADR-0114](0114-profiling-api-build-configuration-and-recorder.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- The `atlantis` executable is a Remote client; it renders nothing and the CLI
  library may link nothing but Atlantis Connection. Running a benchmark means
  running `runFrame()`, which only `atlantis_runtime` does.
- `atlantis_runtime` already hosts optional startup-layer features
  (`--exec`, `--listen`, `--editor`) through executable-private code that
  `atlantis_runtime_host` never links.
- The Windows window size is OS-chosen; the Runtime renders at the swapchain
  size; presentation is FIFO.
- `ConsoleLogSink` writes Info to stdout, which would corrupt JSON output.
- The Bistro golden test already applies World edits (fog off, bloom off, point
  lights off) as variants.
- A performance number must never become a test verdict (Spec 0059 R13).

## Decision

1. **Host.** `atlantis_runtime --benchmark <scene> [--warmup N] [--frames N]
   [--resolution WxH] [--variant V] [--json] [--output PATH] [--baseline PATH]
   [--series] [--gpu-timing=on|off]`, parsed in the executable-private startup
   layer; defaults `--warmup 120 --frames 300 --resolution 1920x1080`. The
   committed baseline scene is `bistro`; any whitelist scene is accepted.
   - A benchmark driver, executable-private like `editor_attachment`, owns the
     loop around `RuntimeApplication::runFrame()`, activates the recorder,
     applies variants through `app.openConnection()`, and writes the result.
   - `--benchmark` is refused with a clear exit code on a build without
     `ATLANTIS_PROFILING`, flagged in metadata on Debug or validation-enabled
     builds, and mutually exclusive with `--exec`, `--editor` and `--listen`.
   - `atlantis benchmark …` as a launcher is future work; it needs process-spawn
     code and no module-rule change.
2. **Fixed resolution.** Platform gains an additive requested initial client
   size (Windows creates the window to that client size; Android ignores it);
   the driver records the actual swapchain extent and exits non-zero if it differs
   from the request. Present mode stays FIFO; `present_mode` and `refresh_hz` are
   metadata.
3. **Variants** (closed list): `baseline`, `no-point-lights`, `no-fog`,
   `no-bloom`, applied as World commands before warmup. Only `baseline` is a
   committed baseline.
4. **Counted frames.** Only frames that reached `Present` count; warmup and
   skipped frames are reported, not measured. One extra frame runs after the
   measured set so the last GPU results (one frame late) arrive.
5. **Output.** One JSON document on stdout (or `--output`); during `--benchmark`
   the log sink writes to stderr. Exit codes are fixed in the Plan: success, usage
   error, environment mismatch (resolution, unsupported build), runtime failure —
   **never a performance verdict**.
6. **Schema `atlantis.benchmark/1`** (versioned; a changed meaning needs a new
   schema string):
   - `meta`: build type, the three options, validation on/off, compiler, git
     revision, OS, CPU, GPU, driver, Vulkan version, timestamp period, present
     mode, refresh rate.
   - `scene`: name, scene GUID, content pin. `settings`: resolution, warmup,
     frames, variant, gpu-timing.
   - `frames`: measured, discarded, skipped.
   - `metrics`: every metric of Spec 0059's counter set as `{avg, p50, p95, p99,
     min, max, n}`, including `zones.*` and `passes.<label>.*`; metrics that the
     hardware or build cannot supply are `null` with a reason, never zero.
   - `series` (only with `--series`): raw per-frame values.
   - Percentiles are nearest rank on sorted samples (ADR-0114).
7. **Baseline comparison.** `--baseline` loads a prior document and adds
   `comparison`: per metric `delta` and `delta_pct` plus each side's p95−p50
   spread, `comparable: true|false` with reasons. Comparable means equal schema,
   scene GUID and content pin, resolution, variant, build type and options, GPU
   and driver. No threshold, no verdict, no failing exit code.

## Consequences

### Positive

- One command line produces one JSON file; no module boundary changes; the
  Remote protocol and C# SDK are untouched.
- The measured loop is the real windowed loop, DPI-correct at a fixed size.
- Comparison cannot silently mix incomparable runs.

### Negative / Trade-offs

- The command is `atlantis_runtime --benchmark`, not the literal
  `atlantis benchmark` (Spec 0059 Q9).
- A Platform API addition solely to request a window size.
- Wall-clock frame time is vsync-quantized for frames faster than the refresh
  interval; the unthrottled metrics are `cpu_active_ms` and `gpu_busy_ms`.
- A window must exist and be visible; a minimized or occluded run produces no
  presented frames (the driver times out with a runtime-failure exit code).

## Alternatives Considered

- **`atlantis benchmark` driving a `--listen` Runtime over Remote** — needs a new
  protocol method (superseding ADR-0106, C# conformance), puts frame-boundary
  latency in the measurement and does not fix resolution.
- **A thin `atlantis benchmark` launcher** — process-spawn code in the `atlantis`
  executable; kept as a follow-up.
- **Fixed size through an offscreen Viewport and a no-op `FrameOverlay`** — no
  Platform change, but the measured path differs from production.
- **Accepting the OS window size** — fails "fixed resolution".
- **A new unthrottled present mode for benchmarking** — an RHI/Presentation
  change outside this work.
- **Interpolated percentiles; arbitrary `--set` edits** — ambiguous across tools;
  an open surface.
