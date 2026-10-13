# Plan: Performance Regression Gate and Baseline Store

- **Spec:** [Spec 0060: Performance Regression Gate and Baseline Store](../specs/0060-performance-contract.md)
  (`Approved`, 2026-10-11, review of [PR #246](https://github.com/slmao/Atlantis/pull/246),
  merged; owner rulings recorded once below) —
  [ADR-0118](../adr/0118-performance-contract.md) (the process contract; D1–D7)
  and [ADR-0119](../adr/0119-benchmark-gate-protocol-and-verdict.md) (protocol,
  aggregation, verdict, CLI; D1–D7), both `Accepted`.
  - Unchanged: [ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md)
    (the benchmark host and `atlantis.benchmark/1`; extended, not superseded),
    [ADR-0114](../adr/0114-profiling-api-build-configuration-and-recorder.md)
    (nearest-rank statistics), [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
    (the precedent), ADR-0106 (`atlantis.remote/1`), ADR-0001, ADR-0004.
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending — to be recorded here once the owner has
  reviewed this Plan and Spec 0060 together and explicitly authorized
  implementation.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0060 R1–R11 as ruled: the repeatable-measurement protocol and its
sampling runtime (`atlantis_runtime --gate`, independent child benchmark processes
after one discarded preheat); the `atlantis.benchmark.set/1` and
`atlantis.benchmark.gate/1` documents and the pure evaluator behind them; the
committed baseline store with its recording and refresh flow; the applicability
data file and `--gate --applies` helper; the Definition-of-Done, PR-template,
AGENTS.md and operator-note changes; and the acceptance runs. It changes nothing
about what the benchmark measures: no frame-loop, renderer, backend or RHI change,
no new third-party dependency, `atlantis.benchmark/1` and the default
`--benchmark`/`--baseline` behavior unchanged, and no CTest that asserts a measured
time.

## Governance record (this PR)

**Owner rulings, 2026-10-11 (chat tasking), recorded once here.** The Spec body
keeps its original option text; these rulings bind it.

| Q | Ruling |
|---|---|
| ADR split | ADR-0118 (the process contract) and ADR-0119 (the measurement protocol) stay separate. |
| Q1 | Independent benchmark processes after one discarded preheat; the run-level statistic is the run's p50; the gate statistic is the median of the run p50s. |
| Q2 | Threshold defaults are **re-measured under the final protocol during implementation and then fixed**; the Plan carries that measurement task (M7) and the rule by which the numbers lock the defaults (P8). |
| Q5 | Hardware profiles exact-match OS, CPU, GPU, driver, Vulkan version, present mode and refresh rate; baselines live under `tests/performance/baselines/<profile>/`. |
| Q9 | Applicability includes `src/world/**` and the build files; enforcement is option (b): a committed path-list data file, a PR-template and Definition-of-Done item, and a `--gate --applies` helper. |
| Q10 | v1 accepts the measured GPU-tier resolution, stated per profile; the interleaved fresh-baseline comparison is **named as the escalation path and not built**. |
| Q11 | The preheat length is measured under the protocol at implementation (observed cold-start transition ≈ 5 runs). |
| Q12 | An accepted regression goes through a category-1 baseline refresh with a stated reason; no per-metric exemptions. |

**Owner ruling on the M7 stop, 2026-10-11 (chat), recorded once here.** The first calibration study (11 valid
sessions, 55 runs, preheat 60 / 90 / 120 s, run before any pinning) showed session-level machine performance states
that no preheat removes: `cpu_active_ms` 12.8 ms in most sessions and 7-8 ms in others, `passes.shadow.gpu_ms`
2.1-2.25 ms against 2.9 ms; the Spec defaults gave 5.8 % false failures and 23 % `inconclusive`; the preheat rule
(consecutive runs within 2 % on at least 90 % of sessions) failed at every duration. Ruled:

- **Option A.** Inside the benchmark child process only, opt out of process power throttling
  (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED` off) and raise the priority class to HIGH, as the new
  `--pin-process on|off` flag of `--benchmark` (default off, so a plain benchmark is unchanged) and of `--gate`
  (default: the policy's `protocol.pin_process`, which is on). Executable-private, no change to the measured loop. This
  is process-level measurement-environment control, inside the spirit of Spec 0060's "no hardware clock/power control"
  non-goal; a pinned run that cannot be pinned exits 3 rather than pose as one.
- The **pinning state** is recorded as `protocol.pin` in `set/1` and `gate/1` and as `baseline.pin` in a baseline's
  provenance, and the evaluator treats a difference as `incomparable`: a pinned run never compares against an unpinned
  baseline. (`profile.json` stays a hardware identity; the pin lives with each baseline.)
- **Preheat 60 s** (Q11).
- **M7 is re-run in full under the final protocol** (at least 60 runs, at least 6 sessions), the stop rule unchanged:
  all tiers meet their rules, lock the defaults; only `passes.shadow.gpu_ms` remains an offender, the owner
  pre-authorizes demoting shadow alone to report-only and everything else is locked per the Spec's defaults; anything
  broader, stop and report. The pre-pin data is kept as evidence of why the pin exists.

**Owner ruling after the M7 study and PR-1's acceptance, 2026-10-12 (chat), recorded once here.** During the M9
baseline recording the integrated GPU's time moved from about 16.6 ms to about 28 ms when the laptop went onto
battery, a shift no tolerance absorbs. Ruled: **a power-source precondition** of the same shape as the idle
precondition. The gate and `--gate --record` refuse to run on battery power (exit 3, executable-private, a Windows
power-status query, no change to any measurement); the source is recorded as `protocol.power` in `set/1` and `gate/1`
and as `baseline.power` in the baseline provenance, the evaluator treats a difference as `incomparable`, and the store
guard accepts only a baseline recorded on `mains`; the policy's `protocol.require_mains_power` is on, and
`--require-mains on|off` is the smoke override (a run with it off is not conformant and `--record` refuses it). It
landed after PR-1 had merged, as its own PR.

**Not separately listed in the tasking** (Q3 scope = `bistro`/`baseline` with
variants diagnostic; Q4 two new documents; Q6 four refresh categories and
`--record` over S = 3 sessions of R = 5; Q7 `atlantis_runtime --gate`; Q8 exit codes
`4`/`5`): the tasking's required Plan content presupposes the recommended option for
each — the documents `set/1` and `gate/1`, the `--gate` command, exit codes
0/4/5, the recording and refresh flow — so this Plan follows the Spec's
recommendation. **The owner is asked to confirm this at the Joint Review**; no
milestone is hard to change if any ruling differs.

**Status metadata changed in this PR** (no decision text is edited):

- Spec 0060: `Draft` → `Approved`; `Related Plan(s)` links this Plan.
- ADR-0118 and ADR-0119: `Proposed` → `Accepted`, acceptance evidence = the owner's
  rulings above and Spec 0060's review PR #246.
- `docs/specs/README.md`: the 0060 row updated (status, Plan link).

The Plan stays `Draft` until the Joint Human Review.

## Pre-drafting reading (cited, not restated)

Read before drafting: [AGENTS.md](../../AGENTS.md); Spec 0060 and ADR-0118/0119;
[Spec 0059](../specs/0059-profiling-instrumentation.md) R8–R13 and
[ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md);
[Plan 0059](0059-profiling-instrumentation.md) (the exemplar: the gate matrix P12,
J8, J11, the M9 driver);
[ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
(golden-update reasons); `docs/process/definition-of-done.md`,
`docs/process/testing-strategy.md`, `.github/PULL_REQUEST_TEMPLATE.md`; and the code
this Plan extends:

- `src/runtime/benchmark_{loop,report,variants,run}.{h,cpp}`, `cli.{h,cpp}`,
  `main.cpp` — the executable-private driver; `compareDocuments()` already refuses
  scene, content pin, resolution, variant, GPU timing, build/options, GPU and driver
  mismatches and reports `p50`/`p95` per metric path (`gatherStats`);
- `src/core/include/atlantis/profile_stats.h` — nearest-rank statistics (the
  robust helpers are added beside them);
- `tests/runtime/` — the GPU-free `atlantis_runtime_tests` target, which compiles the
  benchmark sources directly, and the process-level test that runs the real
  executable (`ATLANTIS_RUNTIME_EXECUTABLE`).

## Plan-stage decisions

**P1 — Files.**

| Where | Contents |
|---|---|
| `src/core/include/atlantis/profile_stats.h`, `src/core/src/profile_stats.cpp` | add `median` and `medianAbsoluteDeviation` / robust sigma (1.4826 × MAD) beside the nearest-rank functions (ADR-0119 D7) |
| `src/runtime/gate_set.{h,cpp}` (new, executable-private) | run-level extraction from `benchmark/1` documents; the `atlantis.benchmark.set/1` builder and its aggregate (P3, P4) |
| `src/runtime/gate_policy.{h,cpp}` (new) | load and validate `tests/performance/gate_policy.json` (P5) |
| `src/runtime/gate_evaluate.{h,cpp}` (new) | the pure evaluator: identity/comparability, tolerance rule, statuses, verdict `gate/1`, exit-code mapping (P6, P7) |
| `src/runtime/gate_store.{h,cpp}` (new) | read the store (profile match, baseline load); write a candidate baseline and `profile.json` (P4) |
| `src/runtime/gate_applies.{h,cpp}` (new) | the applicability file and a glob matcher (P9) |
| `src/runtime/gate_orchestrate.{h,cpp}`, `gate_process_windows.cpp` (new) | child-process runs, the idle precondition, preheat, sessions; the only OS-specific file (P2, P10) |
| `src/runtime/gate_run.{h,cpp}` (new) | the `--gate`, `--gate --record` and `--gate --applies` drivers |
| `src/runtime/cli.{h,cpp}`, `main.cpp` | `GateRequest`, flags, conflicts, usage text, dispatch before scene loading; exit codes `4`/`5` |
| `src/runtime/benchmark_loop.{h,cpp}`, `benchmark_run.cpp` | additive `--warmup-seconds` (P2); nothing else in the measured loop |
| `src/runtime/CMakeLists.txt`, `tests/runtime/CMakeLists.txt` | the new sources into `atlantis_runtime` and the GPU-free test target |
| `tests/runtime/gate_*_tests.cpp` (new), `cli_tests.cpp` (extended) | GPU-free tests (checklist); one GPU-labelled process-level smoke test |
| `tests/performance/gate_policy.json`, `applicability.txt`, `baselines/…` (new, data) | policy, applicability and (PR-2, M10) the first baseline |
| `.github/PULL_REQUEST_TEMPLATE.md`, `docs/process/definition-of-done.md`, `AGENTS.md`, `docs/architecture/module_boundaries.md` | the contract wording (M8) |
| `docs/process/performance-contract.md` (new), `docs/process/profiling.md` | operator note; a pointer from the benchmark section (M8) |

**Dependency-graph changes:** none. `atlantis_runtime` and its GPU-free test target
gain executable-private sources (as `benchmark_*.cpp`); Core gains additive
statistics functions; no module's link list changes; no third-party dependency.

**P2 — Child runs and the preheat.** A *run* is a child process
`atlantis_runtime --benchmark <scene> --warmup W --frames F --variant V --resolution
WxH --output <temp file>` started with `CreateProcessW` on the running executable's
own path; its stdout and **stderr are discarded** (stderr to NUL is part of the
protocol: with stderr captured to a file the existing per-frame conformal-skip log
cost 5.7 ms of `RenderExtraction` in Bistro, Spec 0059 report); the temp file is the
only channel; a child's exit code `3` (environment) or `2` (runtime failure) ends the
gate with the same code and no verdict. The **preheat** is an unrecorded child run
whose duration is fixed in seconds, not frames (frame time varies by hardware): an
**additive** benchmark flag `--warmup-seconds <s>` (default 0) makes the warmup phase
continue until both `--warmup` frames and `<s>` seconds have elapsed, implemented in
the loop's fake-clock-testable warmup phase and nowhere else; the measured frames are
unchanged. Timeouts: a child that exceeds a policy limit is killed and reported as
runtime failure (exit 2).

**P3 — Run-level values.** From each child's `atlantis.benchmark/1` document, every
metric leaf that has statistics — `metrics.<key>`, `metrics.zones.<Zone>`,
`metrics.passes.<label>.{gpu_ms,cpu_record_ms}`, flattened exactly as
`compareDocuments()`'s `gatherStats` names them — contributes its `p50` (and `p95`
for reporting). A leaf `{"unavailable": …}` stays unavailable. No new measurement.

**P4 — Normative layouts** (field names; the Spec and ADR-0119 fix the semantics).

- `atlantis.benchmark.set/1`: `schema`, `protocol` (`runs`, `preheat_seconds`,
  `idle_wait`, `k`, `child_stderr: "discarded"`), `meta`, `scene`, `settings` (all as
  in `benchmark/1`, taken from the first run and checked equal across runs),
  `runs` (array of `{ "values": {key: p50}, "p95": {key: p95} }`), `aggregate`
  (`{key: {center, sigma, min, max, n}}`), `mdr` (`{tier: percent}`, written by
  recording only).
- Committed baseline = a `set/1` document plus `baseline`: `profile`, `recorded_commit`,
  `recorded_at`, `sessions` (each `{start, runs}`), `reason` (`category` 1–4, `text`).
- `atlantis.benchmark.gate/1`: `schema`, `verdict`, `exit_code`, `baseline` (`profile`,
  `file`, `commit`), `protocol`, `comparable`, `reasons`, `metrics` (per key `tier`,
  `baseline`, `candidate`, `delta`, `delta_pct`, `tolerance`, `tolerance_source`
  ∈ `floor|max_pct|noise`, `status`), `dispersion`, `summary`
  (`regressed`, `improved`, `ok`, `ignored`, `unavailable`). Members appear in this
  fixed order so identical inputs give identical bytes; no timestamps.
- `tests/performance/baselines/<profile>/profile.json`: `schema`
  (`atlantis.benchmark.profile/1`, a store file, not a benchmark document), `id`,
  `label`, `identity` (`os`, `cpu`, `gpu`, `driver`, `vulkan_api`, `present_mode`,
  `refresh_hz`).
- Baseline file name: `<scene>_<variant>_<W>x<H>.baseline.json` under
  `baselines/<profile>/<scene>/`.

**P5 — The policy file** (`tests/performance/gate_policy.json`, schema-validated at
load): `version`; `protocol` (`runs`, `preheat_seconds`, `k`, `idle` — `cpu_idle_pct`,
`window_seconds`, `timeout_seconds` — and `child_timeout_seconds`); `dispersion_factor`;
`tiers` (`counter`, `cpu`, `gpu`, each with `abs_floor`, `max_pct`, optional
`relative_tolerance`); `gated` (the metric patterns per tier: the counter list,
`cpu_active_ms`, `zones.RenderExtraction`, `zones.Renderer`, `zones.RenderGraph`,
`passes.*.cpu_record_ms`, `gpu_busy_ms`, `passes.*.gpu_ms`); `reported` (never gated:
`frame_ms`, `cpu_ms`, `acquire_ms`, `present_ms`, `wait_*`, `gpu_span_ms`);
`workload` (the counters whose change makes a run `incomparable`: `renderables_total`,
`lights_directional`, `lights_point`); and `measured` (the M7 study identifier, date
and profile that fixed the numbers). M2 ships the Spec's proposed defaults with
`measured` empty; M7 replaces them; a validity test requires `measured` to be
populated once M7 lands.

**P6 — Evaluation order** (a pure function, no clock, no randomness):
(1) *identity and workload* — the 0059 comparability fields plus CPU, OS, present mode
and refresh rate, then the workload counters; any mismatch, or no profile match in the
store, → `incomparable` (exit 5); (2) *counters* — `candidate > center + tolerance`
(tolerance 0 except the relative tolerance) → `regressed`; a counter regression is
deterministic and ends in `fail` regardless of noise; (3) *dispersion* — if any gated
CPU or GPU metric's candidate dispersion (robust sigma of the R run values) exceeds
`dispersion_factor × baseline sigma` the verdict is `inconclusive` (exit 5) unless a
counter regressed; (4) *CPU and GPU tiers* — `regressed`, `improved`, `ignored_floor`
or `ok` per the tolerance rule of ADR-0119 D3; (5) any `regressed` → `fail` (exit 4),
else `pass` (exit 0). Improvements never fail.

**P7 — Exit codes** are the Spec's: `0` pass, `1` usage/initialization, `2` runtime
failure, `3` environment mismatch/unsupported build (`--gate` on a build without
`ATLANTIS_PROFILING`, no idle machine within the timeout, absent scene), `4` the gate
failed, `5` not judged. `--gate` conflicts with `--benchmark`, `--exec`, `--editor`
and `--listen`; stdout carries exactly the verdict document (the `StderrLogScope`
precedent).

**P8 — How the M7 numbers lock the defaults.** The calibration study is at least 60
protocol-conformant runs over at least six sessions, at least two of them started
right after a build. Fixed in this order, each by a stated rule, and the study, rules
and chosen values are recorded in the PR and in `gate_policy.json`'s `measured` block:

1. **Preheat seconds:** the smallest duration after which `cpu_active_ms` of
   consecutive runs agree within 2 % on at least 90 % of the study's sessions
   (measured from the cold-start transition, which the Spec observed as ≈ 5 runs);
   if none ≤ 180 s qualifies, the fallback of Q11(b) (adaptive) is raised to the owner
   rather than adopted silently.
2. **k:** 3 unless the bootstrap false-failure estimate exceeds 1 % (95 % upper bound
   3 %) per gate, then 4 (bootstrap = resampling baselines of K and candidates of R
   from the study, the Spec's method).
3. **`max_pct` and `abs_floor` per tier:** the smallest values that keep (2) satisfied
   while giving the best minimum detectable regression (MDR, the smallest relative
   change detected ≥ 90 % of the time); the counter tier stays exact.
4. **R:** 5 unless (1)–(3) cannot be met at R = 5, then the smallest R that does,
   within the cost limit stated below.
5. **`dispersion_factor`:** the smallest value for which `inconclusive` stays ≤ 10 % on
   the study's unchanged runs.

The noise bands recorded in the first baseline come from M10's recording under these
final parameters; the M7 study's own band estimates are reported, not committed.

**P9 — Applicability.** `tests/performance/applicability.txt` holds `include:` and
`exclude:` sections of repository-relative globs (`**` matches across directories; the
matcher is specified by its tests, not by prose). Includes: `src/renderer/**`,
`src/render_graph/**`, `src/rhi/**`, `src/vulkan_backend/**`, `src/runtime/**`,
`src/world/**`, `shaders/**`, `src/core/**/profile*`, `CMakeLists.txt`, `cmake/**`,
`tests/performance/**`. Excludes: `docs/**`, `android/**`, `src/editor/**`,
`src/gameplay_sdk/**`, `src/csharp/**`, `src/remote/**`, `src/cli/**`,
`src/connection/**`, `tests/**` other than `tests/performance/**`. A path matched by
both is an include; the helper `atlantis_runtime --gate --applies` reads one path per
line on stdin and prints `{ "applies": bool, "matched": […] }`, exit 0 (it judges
nothing and needs no device).

**P10 — The idle precondition** samples system CPU idle time (Windows
`GetSystemTimes`) over `window_seconds` and proceeds when idle ≥ `cpu_idle_pct`;
otherwise it keeps sampling until `timeout_seconds`, then exits 3. The sampler is an
injectable interface so the GPU-free tests drive it; an `--idle-wait off` flag (and
`--preheat-seconds 0` / `--runs N`) exist for the smoke test and are recorded in the
verdict's `protocol`, which marks such a run as non-conformant and therefore never a
candidate for recording.

**P11 — Where it runs.** Normal milestone gates run the GPU-free tests and the
GPU-labelled smoke test only (the default scene, `--runs 2 --preheat-seconds 0
--idle-wait off`, asserting structure and exit-code membership, never a time). The
**full protocol** runs only in M7 (calibration), M9 (acceptance) and M10 (baseline and
live acceptance), always on the reference machine in Release, config B, 1920×1080.

**P12 — Gate matrix** (as Plan 0059 P12/J8). Every milestone: configuration A
Debug+Release full; B and C Release full; B and C Debug GPU-labelled suites under
fatal validation; Android `assembleDebug`. M9 additionally runs A, B, C × Debug and
Release full. The per-test timeout is 900 s (the orphan-process hang seen in Plan
0059's gates is why). Goldens are never touched, and a path guard asserts the diff
touches none of `src/{renderer,render_graph,rhi,vulkan_backend,platform,world}`,
`src/runtime/src/runtime_application.cpp`, `shaders/`, `assets/`, `android/` or
`tests/image_regression/`.

**P13 — Cost of the protocol** (reference machine, Bistro, from Spec 0059's runs: a
child run is about 19 s — roughly 6.5 s scene load plus 120 warmup and 300 measured
frames at ≈ 28 ms). A gate = idle wait (≥ 10 s) + preheat (≈ 97 s: load plus ≈ 90 s of
rendering) + 5 × 19 s ≈ **3.5 minutes**. Recording = 3 sessions × ≈ 3.3 min + two
≥ 10-minute gaps ≈ **30 minutes**. The M7 study ≈ 60 runs (≈ 19 min of runs) plus
preheats and ≥ 6 sessions with gaps ≈ **1.5–2 hours wall clock**; M10's 20 live gates
≈ **70 minutes**; M9's injections ≈ 4 gates per magnitude. None of it is in the
per-milestone gate.

**P14 — Injection (acceptance A2).** An **uncommitted** local patch, described here
and attached to the PR as evidence, never merged: (a) a calibrated CPU spin inside the
`RenderExtraction` zone at magnitudes of +2 %, +5 %, +10 % and +25 % of its baseline
time; (b) every opaque draw issued twice (≈ +100 % of `passes.draw.gpu_ms`) and a
milder variant issuing every fourth draw twice; (c) one extra draw call per frame (a
counter regression). Each is built into scratch, gated against the baseline, and the
verdicts are recorded. The smallest reliably detected magnitude per tier is written
into the baseline as its `mdr` (Q10: the resolution is stated per profile, not
promised).

## Milestones / Task Breakdown

Two PRs: **PR-1** implements M1–M9 (code, data files other than baselines, docs,
calibration and acceptance against a locally recorded, uncommitted baseline);
**PR-2** is M10, a dedicated baseline PR (category 4, ADR-0118 D3) opened after PR-1
merges. Each milestone builds and runs its gate before the next starts.

### M1 — Robust statistics and the run-level / set document (R1, R2; ADR-0119 D2, D7)

1. `profile_stats.{h,cpp}`: add the median, MAD and robust sigma beside the
   nearest-rank functions (additive; the nearest-rank behavior untouched).
2. `gate_set.{h,cpp}`: extract run-level values from `benchmark/1` documents (P3),
   check `meta`/`scene`/`settings` equal across runs, build `set/1` with `aggregate`
   (center = median of run p50s, sigma, min, max, n).
3. Tests (GPU-free): the statistics against hand-computed values and edge cases (n = 1,
   even n, ties, a zero MAD); extraction from synthetic documents (including
   `unavailable` leaves); aggregate arithmetic; `set/1` schema validity and
   byte-stable output; refusal when runs disagree on scene or resolution.

*Gate:* A, B, C. (GPU-free; the new tests also run in A.)

### M2 — Policy and the pure evaluator (R6, R7, R8; ADR-0119 D3, D4)

1. `gate_policy.{h,cpp}` and `tests/performance/gate_policy.json` with the Spec's
   proposed defaults and an empty `measured` block (P5).
2. `gate_evaluate.{h,cpp}`: identity and workload checks, the tolerance rule, the
   statuses, the dispersion check, the verdict `gate/1` and the exit-code mapping, in
   the order of P6.
3. Tests (GPU-free): every status and the tolerance boundaries (exactly at, just
   over); each tier; `ignored_floor`; improvements never failing; a counter regression
   failing under noise; each `comparable: false` reason including CPU, OS, present mode
   and refresh rate; a workload change → `incomparable`; no matching profile;
   `inconclusive` precedence; the verdict byte-identical for identical inputs and
   free of timestamps; the policy loader's rejection of malformed files.

*Gate:* A, B, C.

### M3 — The command surface (R5; ADR-0119 D5)

1. `cli.{h,cpp}`: `GateRequest`, the `--gate` flags (`--scene`, `--variant`,
   `--resolution`, `--runs`, `--baselines`, `--profile`, `--policy`, `--json`,
   `--output`, `--idle-wait`, `--preheat-seconds`), `--gate --record` (`--sessions`,
   `--session-gap-minutes`, `--reason-category`, `--reason`, `--record-to`), `--gate
   --applies`; conflicts with the other modes; usage text; exit-code constants `4`/`5`.
2. `main.cpp`: dispatch to the gate drivers before any scene or device work, with the
   `StderrLogScope`; the build-without-profiling refusal (exit 3).
3. Tests (GPU-free): parsing, defaults, every conflict and usage error, the `--help`
   text, exit-code constants; the existing `--benchmark` parsing cases untouched.

*Gate:* A, B, C.

### M4 — Child runs, preheat and the idle precondition (R1; ADR-0119 D1)

1. `benchmark_loop.{h,cpp}` and `benchmark_run.cpp`: the additive `--warmup-seconds`
   (P2); the default behavior and the measured loop unchanged.
2. `gate_orchestrate.{h,cpp}` and `gate_process_windows.cpp`: launch a child run with
   discarded stdout/stderr and a timeout, collect its document, map its exit code;
   run the preheat; the idle precondition behind an injectable sampler (P10); the
   session loop for recording.
3. Tests: GPU-free — the warmup-seconds accounting on the fake clock (frames and
   seconds both satisfied; the measured frames unchanged), the idle sampler
   decisions, the orchestration sequence (idle → preheat → R runs → sessions) driven by
   a fake launcher including a child failing with 2 and 3, a timeout, and a malformed
   child document. A GPU-labelled test runs one real child on the default scene.

*Gate:* A, B, C.

### M5 — The gate driver and the store reader (R3, R5, R6, R7; ADR-0118 D2, ADR-0119 D4–D5)

1. `gate_store.{h,cpp}`: read `profile.json`, match this machine's identity to a
   profile, load the baseline for `(scene, variant, resolution)`.
2. `gate_run.{h,cpp}`: `--gate` end to end — protocol, `set/1`, store lookup, evaluate,
   write the verdict (stdout, `--output`), exit code; `--gate --applies` (M8 supplies
   the matcher; M5 wires the verb).
3. Tests: GPU-free — store lookup (match, no match, wrong files), the driver against a
   fake launcher and a synthetic baseline for each of `pass`, `fail`, `incomparable`,
   `inconclusive`. GPU-labelled process-level smoke on the default scene with a
   throwaway baseline in a temp directory: exit code ∈ {0, 4, 5}, stdout is exactly one
   valid `gate/1` document, the same run against a baseline with a different profile
   gives exit 5 `incomparable`; the build-without-profiling refusal in A.

*Gate:* A, B, C.

### M6 — Recording and the store's guards (R3, R4; ADR-0118 D2–D3, ADR-0119 D6)

1. `gate_store.{h,cpp}`: write a candidate baseline and `profile.json` to the path the
   caller names — never into the committed store implicitly — with the `baseline`
   provenance block; `--gate --record` requires a reason category (1–4) and text.
2. Store validity tests (GPU-free, read the committed tree): every file under
   `tests/performance/baselines/` is a schema-valid `set/1` with a `baseline` block
   carrying a reason category and text, sits under a `profile.json` whose identity it
   matches, and is named by the layout; `gate_policy.json` and `applicability.txt` are
   valid; they pass vacuously while the store is empty.
3. Tests: `--record` refuses without a reason or with an invalid category; the writer
   output is byte-stable on a fake launcher; the recorded `aggregate` equals M1's.

*Gate:* A, B, C.

### M7 — Noise re-measurement under the final protocol; the defaults are fixed (Q2, Q11; R1, R2)

1. Run the calibration study of P8 (≥ 60 runs, ≥ 6 sessions, ≥ 2 after a build) on the
   reference machine using the real `--gate` protocol with the Spec's proposed
   defaults; keep every run document (PR evidence, not committed).
2. Apply P8's five rules to fix the preheat, k, per-tier `max_pct` and `abs_floor`, R and
   `dispersion_factor`; record the study identifier, the rules and the resulting numbers,
   the bootstrap false-failure estimate and the per-tier MDR in the PR.
3. Update `tests/performance/gate_policy.json` (data only) with the measured values and
   its `measured` block; extend the validity test to require that block.
4. If any rule cannot be met (preheat > 180 s, a tier whose MDR is above the owner's
   acceptable resolution, `inconclusive` > 10 %), stop and report to the owner rather
   than adopting a weaker value.

*Gate:* A, B, C (the data-only change and its validity test).

### M8 — Applicability, the rules and the operator note (R9, R10; ADR-0118 D5–D7)

1. `gate_applies.{h,cpp}` and `tests/performance/applicability.txt` (P9); wire
   `--gate --applies`.
2. `.github/PULL_REQUEST_TEMPLATE.md`: the performance-gate line (run with verdict, or
   N/A with the reason) and a note that a PR touching `tests/performance/baselines/`
   states its refresh category; `docs/process/definition-of-done.md`: the Testing item,
   worded so that where no baseline exists for the machine's profile the verdict is
   `incomparable` and the item is N/A with that reason; `AGENTS.md` Testing
   requirements: the contract and the agent rule (never refresh a baseline without an
   explicit human instruction); `docs/architecture/module_boundaries.md`: the Runtime
   paragraph; `docs/process/profiling.md`: a pointer.
3. `docs/process/performance-contract.md`: how to run the gate, read a verdict
   (statuses, exit codes), the cost, record a baseline, and file a refresh PR with its
   category; states the per-profile resolution recorded in M10.
4. Tests (GPU-free): the matcher over a table of paths (each include, each exclude, a
   path matched by both, `**` and root-level files); the helper's output shape.

*Gate:* A, B, C. Docs reviewed against ADR-0118 D6.

### M9 — Acceptance runs and the final gate (A1–A6)

On the reference machine, Release, config B (and one confirming run in C), against a
**locally recorded, uncommitted** baseline recorded under the M7-fixed protocol:

1. **A1 (part 1)** — the bootstrap false-failure estimate from M7's study stated for the
   final parameters.
2. **A2** — the injections of P14; each fails with a per-metric report naming the
   regressed metrics; the smallest reliably detected magnitude per tier is recorded.
3. **A3** — a coding agent (this session) runs `atlantis_runtime --gate --json` as one
   command and reads the verdict and exit code; the same inputs reproduce the verdict
   byte for byte; exit `0`, `4` (an injection) and `5` (a mismatched profile and a
   missing baseline) are each shown.
4. **A4, A5** — the applicability helper run on this PR's own changed-file list and on a
   docs-only list; the rules and note present.
5. The full matrix: A, B, C × Debug and Release (full suites, serial content/tool
   part), fatal-validation GPU suites, Android `assembleDebug`, the diff guards of P12,
   goldens byte-identical, `--benchmark`/`--baseline` output for the default scene
   byte-identical to `origin/main`'s.
6. Open **PR-1** (M1–M9) with the implementation summary, deviations, the M7 study and
   the A1–A5 evidence (not committed: J11 still holds for ad-hoc results).

### M10 — The first committed baseline, in its own PR (R3, R4, R11; A1 part 2, A6)

After PR-1 merges: a dedicated **PR-2**, category 4 (initial baseline for the reference
profile), containing only `tests/performance/baselines/<profile>/profile.json` and the
`bistro_baseline_1920x1080` baseline recorded by `--gate --record` (S = 3 sessions of
R = 5, at least 10 minutes apart, from a clean `origin/main` build, Release, config B).
Evidence in the PR: the provenance block, the recorded bands and per-tier MDR, the
store validity test passing, then **20 consecutive live gates on the unchanged build
against the committed baseline** (no `fail`; `inconclusive` ≤ 10 %), one agent
`--gate --json` run, and one injected-slowdown gate confirming `fail`. PR-2 changes no
code; it never bundles anything else (ADR-0118 D3).

## Files / Modules Touched (expected)

**New:** the P1 rows marked new (`src/runtime/gate_*`, `tests/runtime/gate_*_tests.cpp`,
`tests/performance/*`, `docs/process/performance-contract.md`).

**Changed:** the P1 rows marked changed — `profile_stats.{h,cpp}` (additive),
`src/runtime/{cli,main,benchmark_loop,benchmark_run}`, the two runtime CMake files,
`cli_tests.cpp`, the PR template, Definition of Done, AGENTS.md,
`module_boundaries.md`, `profiling.md`; plus `docs/specs/README.md` (this PR).

**Not touched:** `src/{renderer,render_graph,rhi,vulkan_backend,platform,world,
asset_system,connection,remote,cli,editor,gameplay_sdk,csharp}/`,
`src/runtime/src/runtime_application.cpp` and the rest of `atlantis_runtime_host`,
`shaders/`, `assets/`, `android/`, every golden, the Remote protocol and conformance
vectors, `atlantis.benchmark/1` and `compareDocuments()`.

## Sequencing & Dependencies

- M1 → M2 → M3 are GPU-free and independent of the sampling runtime; M2 needs M1's
  documents, M3 needs M2's exit codes.
- M4 needs M3 (flags) and the existing benchmark driver; M5 needs M2, M3, M4; M6 needs
  M1 and M5.
- M7 needs M4–M6 working end to end (it measures the real protocol); M8 follows M7 so
  the operator note and the rules cite final numbers; M9 needs M7 and M8.
- M10 needs PR-1 merged and a clean `origin/main` build.
- Build and test run after every milestone, per P12.

## Verification Checklist

Every requirement and acceptance criterion of Spec 0060 mapped to concrete tests or runs.

- [ ] **R1 repeatable measurement (M1, M4, M7):** the statistics and aggregate tests
  (M1); the orchestration sequence, idle precondition and `--warmup-seconds` accounting
  on fakes (M4); the real protocol's calibration study (M7).
- [ ] **R2 noise band (M1, M6, M7, M10):** the aggregate's `sigma`/`min`/`max`; the
  tolerance rule's noise term (M2); the bands and per-tier MDR recorded in the baseline
  (M10).
- [ ] **R3 committed store (M5, M6, M10):** lookup tests; the store validity tests; the
  committed baseline's provenance.
- [ ] **R4 refresh (M6, M8, M10):** `--record` refuses without a reason; nothing writes
  into the committed store implicitly (tests on the writer's path handling); the PR
  template and AGENTS.md rules; PR-2 is a dedicated category-4 PR.
- [ ] **R5 gate mode (M3, M5):** parsing/conflict tests; the smoke test's exit-code
  membership; the build-without-profiling refusal.
- [ ] **R6 verdict (M2, M5):** every status; byte-identical verdicts; schema validity.
- [ ] **R7 metadata integrity (M2, M5):** every `comparable: false` reason, workload
  changes, no profile match → `incomparable` with the way forward.
- [ ] **R8 tiers (M2):** tier assignment from the policy; reported-not-gated metrics;
  counters exact; workload counters → `incomparable`.
- [ ] **R9 applicability (M8):** the matcher table and the helper on real changed-file
  lists; the PR-template and Definition-of-Done items.
- [ ] **R10 contract in the rules (M8):** AGENTS.md, Definition of Done, the operator
  note, reviewed against ADR-0118 D6.
- [ ] **R11 no behavior change (every gate, M9):** goldens byte-identical (A, B, C); the
  P12 path guard; `--benchmark`/`--baseline` output and `atlantis.benchmark/1` unchanged
  (a byte comparison with `origin/main` on the default scene and `compareDocuments()`'s
  existing tests untouched); **no CTest asserts a measured time** (review of the diff:
  the smoke test asserts structure and exit-code membership only); Vulkan validation
  clean; Android `assembleDebug` unchanged.
- [ ] **A1 unchanged build passes repeatedly (M7, M9, M10):** the bootstrap estimate ≤ 1 %
  (95 % upper bound ≤ 3 %) and, in PR-2, 20 consecutive live gates with no `fail` and
  `inconclusive` ≤ 10 %.
- [ ] **A2 injected slowdowns fail with a per-metric report (M9, M10):** the P14
  injections; per-tier MDR recorded; one injection re-confirmed in PR-2.
- [ ] **A3 one command, deterministic verdict (M9, M10):** the agent run with exit codes
  0, 4 and 5; byte-identical verdicts from identical inputs.
- [ ] **A4 applicability defined and wired (M8, M9):** data file, helper, PR template,
  Definition of Done.
- [ ] **A5 contract in the rules (M8):** AGENTS.md, Definition of Done, operator note.
- [ ] **A6 (R11 plus the baseline):** as R11, and the reference profile's baseline
  committed through the category-4 refresh flow (M10).
- [ ] **Validation layers:** fatal-validation GPU suites clean at every gate.
- [ ] **Android:** `assembleDebug` unchanged at every gate.

## Risks

- **The study may not meet the rules.** A preheat longer than 180 s, a GPU tier whose
  resolution the owner finds too coarse, or too many `inconclusive` verdicts is a
  finding, reported at M7 (the owner has accepted the measured GPU resolution, Q10;
  not an arbitrary one).
- **A noisy recording poisons the contract.** Recording spans three sessions and PR-2
  reviews the band; a bad baseline is refreshed under category 3.
- **Killing or orphaning child processes** hung the gate matrix during Plan 0059; the
  orchestrator owns its children (kill on timeout and on its own exit), and the gates use
  a per-test timeout.
- **The machine is not idle.** The precondition times out to exit 3; a human decides
  whether to retry.
- **Driver updates retire the baseline** (the identity includes the driver); that is a
  category-2 refresh by design.
- **Policy defaults drift from the Spec's proposals** after M7; the Spec keeps the
  proposed numbers as the record of what was weighed, the policy file and the PR record
  what was measured.
- **PR-1 lands before a baseline exists**; the Definition-of-Done wording makes the
  item N/A with `incomparable` in that interval.

## Rollback Plan

Everything is behind `--gate` (and the additive `--warmup-seconds`, default 0): reverting
PR-1 removes the executable-private sources, the additive Core statistics and the rules,
with no data or format migration. The committed baselines (PR-2) are inert data; a
partial rollback may leave them and revert only the rules (M8) or only the gate (M2–M6).

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- PR-1 records, per gate, the A/B/C results, the diff guards and the byte comparison of
  the default benchmark output; the M7 study (runs, rules, chosen values, bootstrap
  estimate, MDR); and the A1–A5 evidence.
- PR-2 records the provenance, the bands, the 20 live gates and the agent run, and is
  opened only after PR-1 merges.
- Benchmark JSON, verdicts, the injection patch and captures are PR evidence, not
  committed (J11 still holds for ad-hoc results; only the baseline store is committed).
- Post-merge, a docs PR updates the registry row and the blueprint (the repository's
  usual follow-up).
