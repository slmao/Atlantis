# ADR 0119: Benchmark Gate Protocol, Aggregation and Verdict

- **Status:** Accepted
- **Date:** 2026-10-11 (accepted 2026-10-11)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-11 (chat rulings on Spec 0060, including keeping this ADR
  and its pair separate; review of Spec 0060's own branch PR,
  [PR #246](https://github.com/slmao/Atlantis/pull/246)).
- **Related Spec:** [Spec 0060](../specs/0060-performance-contract.md) (`Approved`)
- **Related ADR(s):**
  - [ADR-0117](0117-benchmark-host-json-schema-and-baseline-comparison.md): the
    benchmark host, `atlantis.benchmark/1`, its comparability rule and exit codes
    0–3 — unchanged and extended, not superseded.
  - [ADR-0114](0114-profiling-api-build-configuration-and-recorder.md): the
    nearest-rank statistics — unchanged; robust statistics are added beside them.
  - Paired with [ADR-0118](0118-performance-contract.md) (the process contract).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- One benchmark run is one sample of a noisy process: on the reference machine the
  same build varies from about 1 % to above 10 % run to run depending on the signal
  and the session; the first runs after idle read about 20 % faster in CPU metrics
  (Spec 0060, Motivation).
- Fresh processes were a more reproducible sample than blocks of one long run
  (2.7 % spread across processes against 9.7–12.1 % across blocks).
- `atlantis.benchmark/1` is frozen: a changed meaning needs a new schema string
  (ADR-0117 D6). Its comparability machinery (`comparable`, `reasons`) already
  refuses scene, content, resolution, variant, build, GPU and driver mismatches.
- `atlantis_runtime` hosts `--benchmark` as executable-private code and may launch
  child processes on Windows without any module boundary changing.

## Decision

1. **Protocol.** One gate invocation: (a) wait for an idle machine (system CPU use
   below a threshold for a fixed window; a timeout is an environment refusal, exit
   3); (b) one **preheat**, an unrecorded child `--benchmark` run of a fixed duration
   whose result is discarded; (c) **R independent child processes** (default R = 5),
   each `atlantis_runtime --benchmark <scene> …` with the ADR-0117 defaults, run back
   to back, each writing an `atlantis.benchmark/1` document. The parameters (idle
   threshold, preheat duration, R, k) are fixed by the committed policy file and
   recorded in the output.
2. **Aggregation.** Each run is reduced to run-level values (the p50 of every gated
   metric; p95 and the full statistics kept for reporting). The gated statistic per
   metric is the **median of the R run-level p50s**. `atlantis.benchmark.set/1` holds
   the aggregate: `meta`, `scene` and `settings` exactly as in `benchmark/1` (so the
   0059 comparability code applies unchanged), a `protocol` object, the per-run
   run-level values, and per metric `{center, sigma, min, max, n}` where `sigma` is
   the robust sigma (1.4826 × MAD).
3. **Noise band and tolerance.** A baseline is a `set` document recorded over several
   sessions (D6), so `sigma` includes session-to-session variation. For a metric
   with K baseline values and R candidate runs,
   `tolerance = max(abs_floor, max_pct × center, k × sigma × 1.25 × √(1/K + 1/R))`
   with `k` default 3; `abs_floor` and `max_pct` come from the policy per **tier**:
   *counters* (deterministic workload-cost counters: tolerance 0, with a small
   relative tolerance only for `fragment_invocations` and `ia_primitives`), *CPU*
   (`cpu_active_ms`, CPU zones, per-pass `cpu_record_ms`) and *GPU* (`gpu_busy_ms`,
   per-pass `gpu_ms`). `frame_ms`, `cpu_ms`, the wait zones and `gpu_span_ms` are
   reported and never gated. A metric is `regressed` when `candidate − center >
   tolerance`, `improved` when `center − candidate > tolerance`, `ignored_floor` when
   the absolute delta is under `abs_floor`, `unavailable` when either side lacks it.
   The baseline also ships each tier's **minimum detectable regression** (the
   smallest relative change the band can resolve), so the contract states its
   resolution.
4. **Documents and identity.** Two new schemas, `atlantis.benchmark.set/1` (D2) and
   `atlantis.benchmark.gate/1` (the verdict: `verdict` ∈ `pass`, `fail`,
   `incomparable`, `inconclusive`; `exit_code`; the baseline reference; `protocol`;
   `comparable` and `reasons`; per metric `tier`, `baseline`, `candidate`, `delta`,
   `delta_pct`, `tolerance`, `tolerance_source`, `status`; a `summary`). A committed
   baseline is a `set` document with an added `baseline` object (profile id, recorded
   commit, sessions, reason category and text). A **hardware profile** is the exact
   equality of `meta.system.{os, cpu, gpu, driver, vulkan_api}` and
   `meta.display.{present_mode, refresh_hz}` under a human-chosen slug stored in
   `profile.json`. The gate's comparability is the 0059 list **plus** CPU, OS, present
   mode and refresh rate; a *workload* change (`renderables_total`, `lights_*`
   differing) is `incomparable`, not `fail`. A machine matching no profile gets
   `incomparable`. `inconclusive` is returned when the candidate runs' own dispersion
   exceeds a policy multiple of the baseline band (the session was noisy). The
   evaluation is a pure function of candidate set, baseline and policy.
5. **Command surface and exit codes.** `atlantis_runtime --gate [--scene] [--variant]
   [--resolution] [--runs] [--baselines <dir>] [--profile <id>|auto] [--policy <file>]
   [--json] [--output <path>]` and `--gate --applies` (reads a changed-file list on
   stdin, prints whether ADR-0118 D5's applicability file selects it). stdout is the
   verdict document only (logs to stderr, as for `--benchmark`). Exit codes extend
   Plan 0059 J7 / ADR-0117 D5: `0` pass; `1` usage or initialization failure; `2`
   runtime failure; `3` environment mismatch or unsupported build; **`4` the gate
   failed**; **`5` not judged** (`incomparable`, no baseline, or `inconclusive`; the
   verdict says which). `--gate` is mutually exclusive with `--benchmark`, `--exec`,
   `--editor` and `--listen`, and is refused on a build without `ATLANTIS_PROFILING`
   (exit 3). The default `--benchmark --baseline` comparison is unchanged.
6. **Recording.** `atlantis_runtime --gate --record` (reason category 1–4 and reason
   text mandatory; a path for the output) runs D1's protocol over S sessions (default
   3, at least 10 minutes apart) of R runs and writes a candidate baseline; it never
   writes into the committed store unless the caller names that path, and the commit
   is a dedicated PR (ADR-0118 D3).
7. **No hardware control and no new measurement.** The protocol does not set clocks
   or power plans, and it adds nothing to the frame loop, zones, regions or counters
   of Spec 0059. Robust statistics (median, MAD) are added beside the nearest-rank
   statistics; no new third-party dependency.

## Consequences

### Positive

- The threshold comes from the measured noise of the hardware profile, so the gate is
  quiet where the signal is quiet (CPU, counters) and says so where it is not (GPU).
- The verdict is deterministic and agent-readable; its status vocabulary separates a
  regression from "could not judge".
- `atlantis.benchmark/1` and the Remote protocol are unchanged; comparability logic
  is shared.

### Negative / Trade-offs

- A gate takes minutes (idle wait, preheat, R runs); recording a baseline takes tens
  of minutes.
- The GPU tier resolves only regressions larger than its band (≈ 10–15 % on the
  reference iGPU); smaller GPU regressions are not reliably caught without the
  interleaved A/B escalation (Spec 0060, Q10).
- The gate launches child processes (Windows-only, executable-private code).
- The policy numbers (floors, `max_pct`, k, R, preheat duration) are defaults that the
  implementation must re-measure under the protocol before fixing them.

## Alternatives Considered

- **Blocks within one process as samples.** Cheaper but drift-prone (measured).
- **A fixed percentage threshold.** Too loose for CPU metrics, too tight for GPU
  metrics, and blind to the hardware.
- **A rank-based test on pooled runs.** Needs more runs than a manual gate can afford
  for a small effect, and its output is harder for an agent to act on than a per-metric
  tolerance.
- **Interleaved A/B against a freshly built baseline binary.** The rigorous answer to
  session drift; costs a second build per gate. Kept as the named escalation, not
  built here.
- **Extending `atlantis.benchmark/1` with a gate section.** Would change a frozen
  schema's meaning (ADR-0117 D6).
- **A gate inside the `atlantis` CLI.** That executable is a Remote client that
  launches no Runtime (ADR-0117).
- **A PowerShell orchestrator.** Adds a second place for the protocol's logic that the
  unit tests cannot reach.
