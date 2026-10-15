# Spec: Performance Regression Gate and Baseline Store

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-11
- **Related Plan(s):** [Plan 0060](../plans/0060-performance-contract.md)
  (`Draft`; pending Joint Human Review).
- **Approval:** slmao, 2026-10-11 (chat rulings on the ADR split and Q1, Q2, Q5,
  Q9, Q10, Q11, Q12; review of this Spec's own branch PR,
  [PR #246](https://github.com/slmao/Atlantis/pull/246), merged). The rulings are
  recorded once in [Plan 0060](../plans/0060-performance-contract.md); the
  open-question text below keeps the options as the record of what was weighed.
  Implementation still awaits the Plan's separate Joint Human Review.
  **Correction (2026-10-14, post-Approval, owner ruling on the M10 stop after
  [PR #248](https://github.com/slmao/Atlantis/pull/248)/[#250](https://github.com/slmao/Atlantis/pull/250)):**
  the record flow had no quality bar.
  - **The approved text** (R2, R4, Q6, Risks) records a baseline over three sessions of five runs and leaves a noisy
    one to the PR's review ("a bad one is refreshed under category 3"); `--record` accepted any recording.
  - **Why that is not enough:** the first M10 recording (mains power, clean build) had a `cpu_active_ms` spread of
    6.46-10.98 ms and a CPU minimum detectable regression of 18.2 %; 20 live gates on the unchanged build gave
    9 pass, 7 fail and 4 inconclusive. Review by eye was the only defence, and re-recording until an acceptance run
    passes would be cherry-picking.
  - **Corrected:** `--record` refuses (exit 3, no store write, the numbers and how to re-record printed) a recording
    whose minimum detectable regression exceeds the policy's `record.max_mdr_cpu_pct` / `record.max_mdr_gpu_pct`
    (10 % and 20 %: above the 5.4-6.5 % and 10.6-19.1 % that three-session baselines resolved in the calibration
    study, below the 18.2 % case); the store guard applies the same bar to a committed baseline. The bound is the
    policy's, fixed before any recording, so a refusal is measurement hygiene, not selection.
  - **ADRs:** ADR-0118 and ADR-0119 are unchanged (D4 already has the policy hold the numbers and the baseline ship
    its minimum detectable regression; D6 `--record` still runs the protocol and writes a candidate).
  **Correction (2026-10-14, second, post-Approval, owner ruling on the dispersion study after the M10 acceptance
  run):** which metrics' dispersion can make a verdict `inconclusive`.
  - **The approved text** (Honesty, Testing, Risks; Plan 0060 P6 "any gated CPU or GPU metric") withholds the verdict
    when the candidate's own run dispersion exceeds the band; the implementation applied
    `dispersion_factor × baseline sigma` to every gated CPU and GPU metric.
  - **Why that is not enough:** on the accepted baseline (bar-passing, mains, clean build), 20 live gates on the
    unchanged build gave 15 pass, 0 fail and **5 inconclusive (25 %, limit 10 %)**. About 38 gated metrics each carry
    their own test, several against a baseline sigma of 0.03 ms, so any one of them tripping withholds the whole
    verdict, and four of the five withheld sessions tripped only zone or pass metrics while the totals were steady.
  - **Corrected:** the dispersion check judges the policy's `dispersion_metrics`, the headline totals `cpu_active_ms`
    and `gpu_busy_ms`, against `dispersion_factor × the baseline's robust sigma`, floored at the tier's
    `abs_floor ÷ k`. The dispersion of every other gated CPU or GPU metric is reported in the verdict's `dispersion`
    section (`dispersion.reported`) and never makes a verdict `inconclusive`; those metrics are judged by the
    tolerance rule alone. Counters are unchanged. `gate/1` also carries `baseline_sigma` and `candidate_sigma` per
    metric, so a later rule study can replay a verdict (the study that led here could not).
  - **Evidence:** an offline replay of nine candidate rules over the 12 calibration sessions, the accepted baseline and
    the first M10 recording (the control reproduces M7: 0.00 % false-fail, 2.83 % inconclusive). Replayed exactly on
    the 20 live sessions the adopted rule gives 19 pass, 1 inconclusive, 0 fail; its false-fail is within 0.01 pp of the control's
    (0.22 % and 0.57-0.58 % on the two pools, all single-metric cross-day level drift) and its detection is not lower.
  - **ADRs:** ADR-0118 and ADR-0119 are unchanged: D4 says `inconclusive` is returned when the candidate runs' own
    dispersion exceeds a policy multiple of the baseline band and does not enumerate the metrics; which metrics are
    checked is the policy's (`dispersion_metrics`), as the numbers are.
  **Correction (2026-10-15, third, post-Approval, owner ruling on the m10c stop):** the GPU tier on a machine whose
  GPU has two states, and what the acceptance run promises.
  - **The approved text** (Honesty, A1, R11, the iGPU Risk) treats a GPU regression as the code's; A1 asks for at
    least 20 consecutive live gates on the unchanged build with no `fail` and `inconclusive` at most 10 %.
  - **Why that cannot hold:** with the dispersion rule corrected, 20 live gates on the accepted baseline gave 4 pass,
    4 fail and 12 inconclusive. The CPU was quiet; from the fourth gate on the GPU tier read 25-60 % slower (the
    main pass carried the whole shift: `draw`/`Opaque` 1.27-1.71 ×, `shadow` and the full-screen passes about 1.0-1.1 ×)
    and was normal again for one gate, on mains power, the day after twenty normal sessions. No tolerance absorbs a
    bimodal GPU, and the frame's own numbers cannot separate this state from a real main-pass regression (the
    identifiability argument in ADR-0120).
  - **Corrected:** the gate owns a fixed GPU canary (`atlantis_runtime --canary`, three fragment workloads that share
    no code with the renderer), measured after the idle wait and after the last run of every session and in every
    recording session, its reference stored in the baseline. A GPU regression with the canary **stable** fails; with the
    canary **shifted** beyond the policy's `canary.max_shift_pct` it is `inconclusive`, reason stated (a machine-state
    outcome); a shifted canary at the start of the session is refused (exit 3). CPU-tier and counter regressions are
    unaffected. A baseline or a run without a canary reading is `incomparable`; `--record` refuses a recording whose
    canary readings spread beyond `record.max_canary_spread_pct`. gate/1 gains `canary` and `machine_state`.
  - **Acceptance reframed (A1, R11):** at least 20 consecutive live gates on the unchanged build return **no `fail`**;
    outcomes classified as machine state (canary shifted: `inconclusive` with that reason, or the start-of-session
    refusal) are **reported separately and are not failures**; `inconclusive` for any other reason stays at most 10 % of
    the gates. The contract's promise is never to lie, not always to pass: a machine whose GPU is in a bad state for a
    day cannot deliver 20 verdicts, and the gate says so. The gate's baselines carry the canary reference, so a baseline
    recorded before it is recorded again (a refresh under category 4 while no baseline is committed).
  - **Premise:** that the canary tracks the state that moves the main pass is validated live, at the first acceptance
    run (an injected global GPU slowdown with the canary stable must fail; machine-state outcomes must be withheld),
    not by this Correction.
  - **ADRs:** [ADR-0120](../adr/0120-gpu-state-canary-and-machine-state-classification.md) is new. It supersedes
    ADR-0119 D7's "no new measurement" for the canary only, and records the canary, the four-case verdict model and
    the start-of-session precondition; ADR-0119 D4's `inconclusive` account gains the machine-state cause. ADR-0118
    and the rest of ADR-0119 are unchanged.
- **Related ADR(s)** (both `Accepted` 2026-10-11, together with this Spec's
  approval):
  - [ADR-0118](../adr/0118-performance-contract.md): performance as a gated
    contract — the committed baseline store, its refresh rule, the opt-in gate
    and its applicability, and the Definition-of-Done change;
  - [ADR-0119](../adr/0119-benchmark-gate-protocol-and-verdict.md): the
    repeatable-measurement protocol, the aggregation and thresholds, the two new
    documents (`atlantis.benchmark.set/1`, `atlantis.benchmark.gate/1`) and the
    gate's command surface and exit codes.

  [ADR-0114](../adr/0114-profiling-api-build-configuration-and-recorder.md)–[ADR-0117](../adr/0117-benchmark-host-json-schema-and-baseline-comparison.md)
  (Spec 0059) and [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)
  (the image-regression precedent) are unchanged.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

[Spec 0059](0059-profiling-instrumentation.md) made performance *measurable*:
`atlantis_runtime --benchmark` produces numbers and a never-failing `--baseline`
comparison. This Spec makes performance a **Performance Contract**: a
repeatable-measurement protocol, a **committed baseline store** scoped to a
hardware profile, an explicit **opt-in gate** that returns a failing exit code and
a machine-readable verdict, and an **applicability policy** saying which PRs must
run it — the way image goldens already make rendered output regressable and
agent-verifiable. It is a local/manual gate like image regression today (no CI
exists). It changes nothing about *what* the benchmark measures, and the default
`--baseline` comparison still never fails (Spec 0059 R11). The first optimization
Spec (point-light cost) is to be validated under this contract.

## Motivation / Problem Statement

Spec 0059 delivered numbers that a person can read but a program cannot yet
*trust*. Three gaps stand between those numbers and a contract:

1. **One run is not a measurement.** On the reference machine the same build gives
   different numbers run to run (table below). A single-run comparison either
   cries wolf or misses real regressions.
2. **There is nothing to compare to.** Plan 0059 J11 kept benchmark JSON out of the
   repository (hardware-specific, ad hoc). So a future PR has no committed
   reference; the numbers in PR #244 are prose.
3. **There is no verdict.** An agent can run `--baseline` but must interpret
   deltas itself; no exit code says "this PR made the frame slower".

Image regression solved the same shape of problem for pixels
([ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)):
committed goldens, a read-only comparison, a categorized reason for every update,
a local gate reported by whoever opens the PR. This Spec applies that precedent to
time — with the difference that pixels are exact and time is noisy, so the
foundation here is the **noise model**, not the comparison.

### What the measurements show today (verified 2026-10-11)

Reference machine: Intel Core Ultra 5 338H, Intel Arc B370 integrated GPU, driver
101.8509, Windows 11, 60 Hz FIFO. Build config B (profiling on, Tracy off),
Release, Bistro `baseline`, 1920×1080, `--warmup 120 --frames 300`, run-level
statistic = the run's p50. "σ" is the robust sigma (1.4826 × MAD) of run-level
values; "range" is (max − min) ÷ median. Everything here is one machine; the
protocol below must be re-measured on any other hardware profile.

| Signal | 12 back-to-back runs | 12 runs, 10 s apart (steady state) | 14 runs from earlier sessions, hours apart |
|---|---|---|---|
| `cpu_active_ms` | σ 4.6 %, range 25 % (**first run 9.5 ms, steady runs 12.0–12.6**) | σ 1.1 %, range 2.7 % | σ 6.4 %, range 28 % |
| `zones.RenderExtraction` | σ 4.6 %, range 13 % | σ 2.6 %, range 5.7 % | σ 4.2 %, range 14 % |
| `passes.draw.cpu_record_ms` (`RecordPass:draw`) | σ 4.8 %, range 36 % | σ 1.2 %, range 4.0 % | σ 7.4 %, range 37 % |
| `gpu_busy_ms` | σ 4.0 %, range 14 % | σ 3.7 %, range 11.7 % (one 18.5 ms run among ~17.1) | **σ 10.3 %, range 32 %** |
| `passes.draw.gpu_ms` | σ 4.4 %, range 17 % | σ 2.9 %, range 15 % | **σ 12.7 %, range 38 %** |
| `frame_ms` (vsync-quantized) | σ 4.5 % | σ 2.4 % | σ 6.2 % |
| `draw_calls`, `descriptor_writes` | identical (4,778; 1,093) in all 38 runs | | |
| `fragment_invocations` | varies by at most 0.007 % | | |

Three further observations drive the design:

- **A cold-start transient.** The first five back-to-back runs (about 15 s each)
  read 9.5, 10.8, 11.9, 11.3 and 11.8 ms of `cpu_active_ms` against 12.0–12.6 ms
  afterwards; discarding them takes the CPU signal from σ 4.6 % to about 1 %.
- **Process starts are more reproducible than long runs.** Three 1,500-frame runs
  split into 250-frame blocks drifted 9.7–12.1 % block to block in `cpu_active_ms`
  and had one block at 29.7 ms of `gpu_busy_ms` against 17.2–17.8 ms for the
  others — more than the 2.7 % spread across fresh processes. Noise is time-varying
  (power and thermal state of a shared-package iGPU, background load), so one
  long run is not several samples.
- **The GPU signal is the noisy one**, and worst across sessions: the earlier
  sessions include runs taken right after builds. The unthrottled GPU metrics are
  still the *primary* ones (`frame_ms` is vsync-quantized), so the contract must
  say honestly how small a GPU regression it can resolve on a given hardware
  profile (Open Question Q10).

A resampling simulation on these runs (baseline = 8 random runs, candidate = the
median of 5 random runs, tolerance = max(floor, k × σ̂ × √(1/K + 1/R) × 1.25),
k = 3) gives: `cpu_active_ms` false positives ≈ 0.0 % with the +5 % regression
detected 82–85 % of the time and +8 % 99.8 %; `gpu_busy_ms` false positives 2.4 %
at a 3 % floor (+10 % detected 77 % of the time, +15 % 96 %) and 0.1 % at an 8 %
floor (71 %, 96 %). These numbers come from runs **not** taken under the protocol this
Spec proposes (no preheat discard, no idle precondition, several taken right after
builds), so they bound what the protocol must beat; the implementation re-measures
under the protocol (Testing & Verification).

## Goals

Owner-fixed (2026-10-11):

- **Repeatability first.** A repeatable-measurement protocol: multi-run
  aggregation with a fixed sample policy, an estimated noise band, and the
  **unthrottled** metrics (`cpu_active_ms`, `gpu_busy_ms`, per-pass `gpu_ms`) as
  primary signals; `frame_ms` is vsync-quantized and secondary. The gate must be
  demonstrably quieter than the regressions it gates.
- **A committed baseline store**, scoped to a hardware profile, superseding Plan
  0059 J11's "not committed" **for the store only** (ad-hoc PR-evidence JSON stays
  uncommitted). Baseline refresh is human-reviewed with a stated reason, mirroring
  ADR-0042.
- **A local/manual gate**, like image regression: run and reported by whoever
  opens the PR.
- **An explicit opt-in failing mode.** The default `--baseline` comparison never
  fails; a separate gate mode returns a failing exit code and a machine-readable
  verdict — one command, deterministic semantics, agent-readable.
- **Metadata integrity.** Comparisons between unlike runs (hardware, build, scene,
  variant, resolution, validation) are refused, reusing 0059's `comparable: false`
  machinery.

## Non-Goals

Owner-set (2026-10-11):

- **A CI pipeline** or any CI integration (no CI exists).
- **Any optimization** — the point-light work is a later Spec validated *under*
  this contract.
- **New benchmark scenes.** `bistro` stays the committed baseline scene.
- **Hardware clock or power control** (locking GPU/CPU frequency, power plans).
- **A change to what the benchmark measures.** Additive driver changes such as
  multi-run aggregation are in scope; the frame loop, zones, regions and counters
  of Spec 0059 are not touched.
- **Changing the never-failing default comparison.** R11 is unchanged.

Also out of scope (proposed by this draft):

- **Gating on CTest.** Spec 0059 R13 stands: nothing in CTest fails on a measured
  time. The gate's *logic* is unit-tested on synthetic documents; the gate itself
  is a command a person or agent runs.
- **Android measurement** (profiling is off there, Spec 0059 Q8).
- **A second hardware profile's baselines** (the store and flow support them; this
  Spec ships the reference machine's only).
- **Cross-hardware comparison or normalization.**
- **Interleaved A/B against a freshly built baseline binary** — named as the
  escalation path for tiny regressions, not built (Alternatives, Q10).

## Requirements

### Functional

- **R1 — Repeatable measurement** (Q1, Q2, Q11). One gate invocation measures with
  a fixed, documented protocol: an idle precondition, a discarded preheat, then a
  fixed number of independent runs, each reduced to run-level statistics, and
  aggregates them with a robust statistic. The protocol parameters are part of the
  recorded result.
- **R2 — Noise band** (Q2). A baseline ships with a per-metric estimated noise
  band (robust sigma of its run-level values across multiple sessions) and the
  minimum regression the gate can resolve at that noise. The gate's tolerance for a
  metric derives from the baseline's band, never from the candidate alone. (Correction
  2026-10-14: a recording whose own minimum detectable regression exceeds the policy's
  record bar is refused at record time.)
- **R3 — Committed baseline store** (Q5). Baselines are committed files scoped to a
  hardware profile; a baseline identifies the scene, variant, resolution, build
  configuration, protocol and the profile it was recorded on. The gate only reads
  the store.
- **R4 — Baseline refresh** (Q6). A baseline file changes only in a dedicated PR,
  human-reviewed, with a categorized stated reason; tooling never regenerates the
  store silently; an agent never refreshes a baseline without an explicit human
  instruction.
- **R5 — Gate mode** (Q7, Q8). `atlantis_runtime --gate` runs the protocol, compares
  with the matching baseline and returns a deterministic exit code and one JSON
  verdict. It is opt-in; no existing command or default changes.
- **R6 — Verdict** (Q4). The verdict is a machine-readable document: an overall
  result (`pass`, `fail`, `incomparable`, `inconclusive`), the baseline used, the
  protocol, and per metric the baseline center, candidate value, delta (absolute and
  relative), the tolerance applied and its source, and a status
  (`ok`, `regressed`, `improved`, `ignored_floor`, `unavailable`). Identical inputs
  give an identical verdict.
- **R7 — Metadata integrity** (Q5). The gate refuses to judge unlike runs: scene,
  content pin, resolution, variant, build type and options, validation, GPU, driver,
  CPU, OS, present mode and refresh rate must match the baseline's profile, reusing
  the `comparable: false` machinery. A missing baseline for the machine's profile is
  a refusal with a stated way forward, never a pass.
- **R8 — Metric tiers** (Q2). Metrics are gated by tier: deterministic counters
  (exact), CPU (`cpu_active_ms`, CPU zones, per-pass `cpu_record_ms`), GPU
  (`gpu_busy_ms`, per-pass `gpu_ms`). `frame_ms` and the wait zones are reported,
  not gated. A change that makes a deterministic workload counter differ
  (`renderables_total`, `lights_*`) is a refusal (the scene changed), not a
  regression.
- **R9 — Applicability** (Q9). The Spec's policy defines which PRs must run the gate
  (frame-path code, renderer, render graph, backend, runtime, shaders, build flags)
  and which are exempt, as a committed data file; the Definition of Done and the PR
  template carry the item.
- **R10 — Contract in the repository's rules** (Q9). AGENTS.md and
  `docs/process/definition-of-done.md` state the contract per ADR-0118; a short
  operator note (`docs/process/performance-contract.md`) says how to run the gate,
  read a verdict and refresh a baseline.
- **R11 — No behavior change.** Rendered images and all existing goldens are
  unchanged; the frame loop, zone map and counter set are untouched; the default
  `--benchmark` and `--baseline` behavior and `atlantis.benchmark/1` are unchanged;
  no CTest asserts a measured time (Spec 0059 R13).

### Non-functional

- **Determinism of the verdict:** the evaluation is a pure function of the
  candidate set, the baseline and the policy; no clock, no randomness.
- **Cost:** a full gate on the reference machine completes in about 3–4 minutes
  (idle wait, preheat, five runs); recording a baseline takes a few tens of minutes and is
  rare.
- **Portability:** Windows measures (it spawns processes); the pure evaluation logic
  is platform-independent and tested everywhere the suites run. Android is not
  measured.
- **Honesty:** the gate states what it could not decide. A noisy session yields
  `inconclusive`, not a guessed pass or fail. (Correction 2026-10-14: "noisy" is judged on the
  headline totals `cpu_active_ms` and `gpu_busy_ms`; the noise of any other gated metric is
  reported, not deciding. Correction 2026-10-15: a GPU regression on a machine whose GPU canary
  has shifted is withheld as `inconclusive`, the reason being the machine's state.)

## Proposed Design

```
atlantis_runtime --gate                         (Windows; one command)
  ├─ idle precondition ──► preheat (unrecorded) ──► run 1..R  (each: a child
  │                                                  `atlantis_runtime --benchmark …`)
  ├─ per-run documents (atlantis.benchmark/1) ──► reduced to run-level stats
  ├─ aggregate: median of run medians, robust sigma ──► atlantis.benchmark.set/1
  ├─ load baseline for this machine's profile  (tests/performance/baselines/…)
  └─ evaluate(candidate set, baseline set, policy) ──► atlantis.benchmark.gate/1
       exit 0 pass │ 4 fail │ 5 not judged │ (1,2,3 as ADR-0117)
```

**The protocol (Q1).** One gate invocation: wait until the machine is idle (system
CPU use stays below a threshold for a fixed window, with a timeout that is an
environment refusal), run one **preheat** (an unrecorded run of a fixed duration, about 90 s of rendering, to
get past the cold-start transient above), then run **R = 5**
independent benchmark processes back to back, each loading the scene fresh and
running the 0059 default warmup and measured frames. Independent processes rather
than blocks of one process because process starts proved more reproducible than
stretches of one long run. Each run reduces to its run-level statistics (the p50 of
every gated metric, plus p95 for reporting).

**The statistic (Q2).** The gated statistic per metric is the **median of the R
run-level p50s**. It is robust to one bad run (the 29.7 ms block above) and needs no
distributional assumption. p95/p99 are reported, not gated, in v1.

**The noise band.** A baseline is recorded over several **sessions** (the machine
left idle between them), so the band includes the session-to-session variation that
dominated the GPU numbers above: K run-level values per metric, from which the
baseline ships `center` (median), `sigma` (1.4826 × MAD) and `min`/`max`. The gate's
tolerance for a metric is

`tolerance = max(abs_floor, max_pct × center, k × sigma × 1.25 × √(1/K + 1/R))`

— the standard error of a median of R runs against a median of K, scaled by k
(default 3). `max_pct` and `abs_floor` come from the committed gate policy per tier
(Q2); the third term is the measured noise. A metric is `regressed` when
`candidate − center > tolerance`, `improved` when `center − candidate > tolerance`,
`ignored_floor` when the delta is below the absolute floor. Improvements never fail
the gate; they are reported so a stale baseline can be refreshed on purpose.

**Tiers.** *Counters* (`draw_calls`, `triangles`, `pipeline_binds`,
`descriptor_set_binds`, `descriptor_writes`, vertex/index buffer binds,
`push_constants`, `upload_bytes`, `uniform_write_bytes`, `materials_realized`) are
deterministic: tolerance 0, so +1 draw call per frame fails; `fragment_invocations`
and `ia_primitives` get a small relative tolerance (0.1 %; measured spread 0.007 %).
*CPU* (`cpu_active_ms`, `zones.RenderExtraction`, `zones.Renderer`,
`zones.RenderGraph`, `passes.*.cpu_record_ms`): default `max_pct` 5 %, floor
0.10 ms. *GPU* (`gpu_busy_ms`, `passes.*.gpu_ms`): default `max_pct` 10 %, floor
0.25 ms. Reported, not gated: `frame_ms`, `cpu_ms`, `acquire_ms`, `present_ms`,
`wait_*`, `gpu_span_ms`. The defaults are proposals; ADR-0119 records the
mechanism, the policy file records the numbers, and the implementation re-measures
the false-positive and detection rates under the protocol before they are fixed.

**Variants (Q3).** Each variant is its own baseline entry; a gate invocation checks
one `(scene, variant, resolution)` and never compares across variants. v1 gates
`bistro` / `baseline`. The other variants (`no-point-lights`, `no-fog`, `no-bloom`)
are *diagnostic*: a failed verdict tells the reader which to run to localize the
cost; they can be recorded and gated later without a schema change.

**Documents (Q4).** `atlantis.benchmark/1` is unchanged. Two new schemas:
`atlantis.benchmark.set/1` (the aggregate of R runs: the same `meta`, `scene` and
`settings` objects as `benchmark/1` so the existing comparability machinery applies
unchanged, a `protocol` object, per-run run-level values, and an `aggregate` per
metric) and `atlantis.benchmark.gate/1` (the verdict). A committed baseline is a
`set` document with an added `baseline` provenance object (profile, recorded
commit, sessions, reason category and text). Shapes are in ADR-0119; an abbreviated
verdict:

```json
{"schema":"atlantis.benchmark.gate/1","verdict":"fail","exit_code":4,
 "baseline":{"profile":"intel-arc-b370-win11-60hz","file":"…/bistro_baseline_1920x1080.baseline.json","commit":"…"},
 "protocol":{"runs":5,"preheat":true,"k":3},
 "comparable":true,"reasons":[],
 "metrics":{"passes.draw.gpu_ms":{"tier":"gpu","baseline":14.1,"candidate":16.9,
   "delta":2.8,"delta_pct":19.9,"tolerance":1.41,"tolerance_source":"max_pct","status":"regressed"}},
 "summary":{"regressed":["passes.draw.gpu_ms","gpu_busy_ms"],"improved":[],"ok":31}}
```

**The store (Q5).** Mirroring the goldens' layout:
`tests/performance/baselines/<profile-id>/profile.json` (the identity) and
`tests/performance/baselines/<profile-id>/<scene>/<scene>_<variant>_<W>x<H>.baseline.json`,
plus `tests/performance/gate_policy.json` (tiers, thresholds, metric list) and
`tests/performance/applicability.txt` (Q9). A **hardware profile** is identified by
exact equality of the JSON's `meta.system.{os, cpu, gpu, driver}`,
`meta.system.vulkan_api`, `meta.display.{present_mode, refresh_hz}` — the fields
0059 already records — under a human-chosen slug. A machine matching no profile gets
`incomparable` with the instruction to record one through the refresh flow.

**Refresh flow (Q6).** Mirroring ADR-0042's golden-update reasons: a PR touching
`tests/performance/baselines/**` states one of — (1) **performance change**: an
intentional change to the frame cost, naming the change (an accepted regression
needs an explicit justification); (2) **reference-environment change**: driver, OS,
CPU or GPU changed and the stored baseline no longer matches though no frame code
changed — the PR carries old and new provenance and evidence (a gate run showing
counters unchanged and CPU zones inside the band) that no regression hides behind
the environment change; (3) **approved rebaseline**: a deliberate human decision for
another stated reason (e.g. the baseline itself was noisy); (4) **initial baseline**
for a new hardware profile. Recording is explicit — `atlantis_runtime --gate
--record` takes the reason category and text, writes candidate files to a path the
caller names, and never touches the committed store by itself; the PR is where a
human reviews them. Baseline files are never bundled into an unrelated change.

**CLI and exit codes (Q7, Q8).** `atlantis_runtime --gate [--scene bistro]
[--variant baseline] [--resolution WxH] [--runs N] [--baselines <dir>] [--profile
<id>|auto] [--policy <file>] [--json] [--output <path>]`, and `--gate --record …`.
stdout is exactly the verdict (as for `--benchmark`). Exit codes extend those fixed by Plan 0059 J7 and ADR-0117 D5:
`0` pass; `1` usage or initialization failure; `2` runtime failure; `3` environment
mismatch or unsupported build (as today); **`4` the gate failed (a gated metric
regressed)**; **`5` not judged** (`incomparable`, no baseline for this profile, or
`inconclusive` noise — the verdict's `verdict` field says which). The result is never
inferred from a log.

**Applicability (Q9).** A PR must run the gate when it changes a path in
`tests/performance/applicability.txt`: `src/renderer/**`, `src/render_graph/**`,
`src/rhi/**`, `src/vulkan_backend/**`, `src/runtime/**`, `src/world/**`,
`shaders/**`, `src/core/**/profile*`, the build files that set compiler or
optimization flags (`CMakeLists.txt`, `cmake/**`) and `tests/performance/**` itself.
Exempt: docs-only, tests-only outside that tree, `android/**`, Editor, Gameplay SDK,
C# SDK, Remote, CLI and Connection changes that do not touch the listed paths.
Enforcement is a PR-template line and a Definition-of-Done item ("performance gate
run with its verdict, or N/A with the reason"), with the path list a single data
file that `atlantis_runtime --gate --applies` can evaluate against a changed-file
list for an agent. No CI enforces it — consistent with image regression today.

## Architectural Impact

Yes, and it is narrow:

- **A new repository artifact class and process rule** — committed performance
  baselines under `tests/performance/`, a categorized refresh rule, an opt-in gate
  and a Definition-of-Done item. Recorded in
  [ADR-0118](../adr/0118-performance-contract.md); it supersedes Plan 0059 J11's
  "not committed" for the store only.
- **A measurement protocol, two new documents and a command surface** — recorded in
  [ADR-0119](../adr/0119-benchmark-gate-protocol-and-verdict.md). `atlantis.benchmark/1`,
  ADR-0117 and the Remote protocol are unchanged.
- **No module boundary moves.** The gate is executable-private in `atlantis_runtime`
  (as `--benchmark` is, ADR-0117), uses the Connection JSON code the executable
  already links, adds a Windows-only child-process launch private to the executable,
  and adds robust-statistics helpers (median, MAD) beside the existing nearest-rank
  statistics. No RHI, Vulkan Backend, RenderGraph, Renderer, World or Platform
  change; no new third-party dependency.
- **Frame loop untouched:** the `runFrame()` diff guard keeps its Spec 0059 form.

## Alternatives Considered

- **Compare against one earlier run.** Rejected: the table above is the argument.
- **A fixed percentage threshold, no noise band.** Simple and agent-friendly, but
  either too loose for the CPU metrics (1–3 % noise) or too tight for the GPU
  metrics (3–13 %); the band makes the threshold honest per hardware profile.
- **Blocks within one process as the samples.** Cheaper (no repeated scene load) but
  the data show a long process drifting more than fresh processes differ (Q1).
- **Interleaved A/B against a freshly built baseline binary** (the rigorous way to
  cancel session drift). Requires a second build and tree per gate; named as the
  escalation path when a verdict is `inconclusive` on a PR that matters (Q10), not
  built here.
- **Baselines in an artifact store or a database outside the repository.** Loses
  review-by-diff and the commit-level provenance the goldens have; there is no CI to
  host one.
- **Gate on `frame_ms`.** Vsync-quantized (Spec 0059); the unthrottled metrics are
  the signal.
- **Make the existing `--baseline` comparison fail.** Violates the owner's fixed
  requirement and R11.
- **Hardware control (locked clocks, power plans).** Out of scope by the owner's
  direction and non-portable across the machines agents run on.

## Testing & Verification Plan

GPU-free tests (synthetic documents, no device): the statistics (median, MAD,
robust sigma); the tolerance rule at its boundaries; every status in the verdict;
tier assignment from the policy; `ignored_floor`; improvements never failing;
workload-counter changes becoming `incomparable`; every `comparable: false` reason
(scene, content pin, resolution, variant, build, validation, GPU, driver, CPU, OS,
present mode, refresh rate); a missing profile; `inconclusive` when the candidate's
own run dispersion exceeds the band for a headline metric (Correction 2026-10-14) and not for
any other; exit codes; the verdict being byte-identical
for identical inputs; schema validity of both new documents; the refresh-category
requirement on `--record`.

Acceptance (run in the implementation PR on the reference machine; results recorded
there; none is a CTest — Spec 0059 R13):

1. **A1 — unchanged build passes repeatedly.** A calibration study of at least 60
   protocol-conformant runs over at least six sessions (at least two after a build)
   yields a bootstrap false-failure estimate per gate of at most 1 % (95 % upper
   bound at most 3 %); and at least 20 consecutive live gates on the unchanged build
   return no `fail`, with `inconclusive` at most 10 %. (Correction 2026-10-15: outcomes
   classified as machine state are reported separately and are not failures; `inconclusive` for
   any other reason stays at most 10 %.)
2. **A2 — injected slowdowns fail with a per-metric report.** Using an uncommitted
   local patch (documented in the PR): a calibrated CPU spin in a named zone at
   several magnitudes (including +10 % of `cpu_active_ms`), a doubled draw loop for
   the GPU, and one extra draw call per frame (a counter regression) each fail the
   gate, the report names the regressed metrics, and the smallest reliably detected
   magnitude per tier is recorded as that profile's minimum detectable regression.
3. **A3 — one command, one deterministic verdict.** A coding agent runs
   `atlantis_runtime --gate --json` and reads the verdict and exit code; the same
   inputs reproduce the verdict byte for byte.
4. **A4 — the applicability policy is defined and wired** (data file, PR template,
   Definition of Done).
5. **A5 — the contract is in the rules** (AGENTS.md, Definition of Done, operator
   note).
6. **R11:** all goldens byte-identical in configurations A, B, C; `--benchmark` and
   `--baseline` output unchanged; Vulkan validation clean; Android `assembleDebug`
   unchanged; the gate's baselines for the reference profile recorded under the
   protocol and committed through the refresh flow (category 4). (Correction 2026-10-15: each
   baseline carries the GPU canary reference.)

## Risks & Open Questions

Risks:

- **The GPU signal may be too noisy to gate small regressions** on integrated or
  thermally constrained hardware. The contract states the resolution per profile
  (minimum detectable regression) and returns `inconclusive` rather than guessing;
  it is strongest for counters and CPU, and for GPU regressions larger than the band
  (Q10).
- **A shared-package iGPU couples CPU and GPU clocks**; a heavy background process
  or a build just before the gate moves both. The idle precondition and the
  dispersion check are the mitigation; they cannot remove all of it. (Correction 2026-10-14:
  the check is on the headline totals; a single-session comparison also cannot tell that a
  whole session ran in a fast CPU state, which reads as an improvement. Correction 2026-10-15:
  the GPU can also sit in a slow state for hours that only a measurement independent of the code
  can recognise; the canary classifies it, and what it cannot recognise would read as a code
  regression.)
- **Baseline staleness.** A driver update silently changes the profile identity
  (driver is part of it), so the gate refuses rather than comparing; the cost is a
  baseline refresh PR per driver change on that profile.
- **A noisy baseline recorded once poisons the contract.** Recording spans several
  sessions and the PR reviews its band; a bad one is refreshed under category 3.
  (Correction 2026-10-14: `--record` itself refuses a recording above the policy's
  record bar, so review no longer has to catch it by eye.)
- **A gate people skip.** Local/manual gates depend on the applicability rule being
  visible (PR template, Definition of Done) and on the one-command surface being
  cheap enough to run.

Open questions. Each lists options and a recommendation; **Judgment wanted** marks
the ones I would most like the owner to rule on.

- **Q1 — The sample unit.** *Judgment wanted.*
  - (a) **R independent benchmark processes** after one discarded preheat.
  - (b) R measured blocks inside one process (no repeated scene load).
  - (c) Interleaved A/B against a freshly built baseline binary.
  - **Recommend (a).** Fresh processes were the more reproducible sample (2.7 %
    across processes against 9.7–12.1 % across blocks of one long run); (b) saves
    ~5 s per run but samples a drifting state; (c) is the strongest and costs a
    second build (kept as the escalation path). (a) needs the gate to launch child
    processes — Windows-only code private to the executable.
- **Q2 — Statistic, tolerance rule and defaults.** *Judgment wanted.*
  - Options: (a) median of R run-level p50s with `tolerance = max(floor, max_pct,
    k·σ·1.25·√(1/K+1/R))`, tiered (counters exact, CPU 5 % / 0.10 ms, GPU 10 % /
    0.25 ms), k = 3, R = 5; (b) a fixed percent only; (c) a rank test (e.g.
    Mann-Whitney) on the pooled runs; (d) gate p95 as well.
  - **Recommend (a).** It uses the measured band, is explainable in one line to an
    agent, and the simulation supports it; the defaults are proposals to be
    re-measured under the protocol. p95 is reported, not gated (it is the noisiest).
- **Q3 — What the gate covers.**
  - (a) `bistro`/`baseline` only, variants diagnostic; (b) all four variants gated;
    (c) `baseline` plus `no-point-lights` (isolates non-lighting regressions).
  - **Recommend (a)** for v1; (c) is a one-file addition once the first optimization
    lands and makes lighting a moving target.
- **Q4 — Verdict shape.**
  - (a) Two new documents (`atlantis.benchmark.set/1`, `atlantis.benchmark.gate/1`);
    (b) extend `atlantis.benchmark/1` with a `gate` section; (c) one new document.
  - **Recommend (a).** `benchmark/1` stays frozen (ADR-0117 D6: a changed meaning
    needs a new schema string); the set document reuses its `meta`/`scene`/`settings`
    so the comparability code is shared.
- **Q5 — Baseline store layout and hardware-profile identity.** *Judgment wanted.*
  - Layout (a) `tests/performance/baselines/<profile>/<scene>/…` beside the goldens'
    convention; (b) under `docs/`; (c) a single JSON file.
  - Identity (a) exact equality of OS, CPU, GPU, driver, Vulkan API, present mode and
    refresh rate; (b) GPU and driver only (what 0059's comparison checks today);
    (c) a coarser GPU-class label.
  - **Recommend layout (a) and identity (a).** CPU time depends on the CPU and OS and
    frame time on the refresh rate, so they belong in the identity; the cost is that
    two physically identical laptops with different driver builds are different
    profiles — which is the honest reading.
- **Q6 — Refresh categories and recording.**
  - Categories (a) the four above, mirroring ADR-0042; (b) fewer; (c) none, "a
    human approves".
  - Recording (a) `--record` with S = 3 sessions of R = 5 runs, ≥ 10 minutes apart,
    reason category mandatory; (b) one session.
  - **Recommend (a) and (a).** Multiple sessions put session-to-session variation in
    the band; a one-session band is what made the cross-session simulation above
    optimistic.
- **Q7 — Where the command lives.**
  - (a) `atlantis_runtime --gate` (self-launching children); (b) a PowerShell
    orchestrator plus a C++ evaluator; (c) a subcommand of the `atlantis` CLI.
  - **Recommend (a).** One executable, the orchestration is unit-testable, and the
    Remote-protocol CLI stays a client of a running Runtime (Spec 0055). (c) would
    need an `atlantis` that launches Runtimes (ADR-0117 names that launcher as future
    work).
- **Q8 — Exit codes.**
  - (a) `4` fail, `5` not judged (cause in the verdict); (b) a code per cause
    (fail, incomparable, no baseline, inconclusive); (c) reuse `2`/`3`.
  - **Recommend (a).** Two meaningful classes for a shell; the verdict JSON carries
    the cause. Existing codes 0–3 keep their meaning.
- **Q9 — Applicability list and enforcement.**
  - Enforcement (a) docs only; (b) docs + PR-template and Definition-of-Done item +
    a committed path-list data file with a `--gate --applies` helper; (c) tooling
    that blocks a PR (needs CI).
  - **Recommend (b).** It is the image-regression model (a reported local gate) with
    the one thing an agent lacks there — a machine-readable "does this PR need it?".
    The path list in Proposed Design is a proposal; `src/world/**` and the build
    files are the two I am least sure about (World shapes extraction cost; flag
    changes move everything).
- **Q10 — How small a regression must the GPU tier resolve?** *Judgment wanted.*
  - (a) Accept the measured resolution (≈ 10–15 % on this iGPU within a session,
    worse across sessions), state it per profile, and rely on counters and CPU for
    small structural regressions; (b) require interleaved A/B for any GPU claim;
    (c) tighten the protocol (longer preheat, more runs) until a target such as 5 %
    is met.
  - **Recommend (a) for v1 with (b) as the named escalation.** The first
    optimization Spec's expected effect (the `no-point-lights` variant removed ≈ 70 %
    of the draw GPU time) is far above the band; the contract should not promise
    resolution the hardware cannot give.
- **Q11 — The idle precondition and preheat.**
  - (a) fixed: system idle for 10 s (timeout 120 s → environment refusal), preheat =
    a fixed duration (about 90 s of rendering); (b) adaptive: preheat until two
    consecutive runs agree within 2 %; (c) none.
  - **Recommend (a).** The cold-start transient lasted about five runs (roughly
    75 s) here; the implementation measures it under the protocol and sets the
    duration before the numbers are fixed. (b) is the fallback if no fixed value is
    stable.
- **Q12 — Accepted regressions.** When a PR deliberately trades speed for something
  else: (a) refresh the baseline under category 1 with the justification; (b) a
  per-metric waiver in the policy file; (c) forbid.
  **Recommend (a)** — one mechanism, reviewed by diff; waivers rot.

## Out of Scope / Future Work

- CI integration and a blocking performance check (the gate's exit code and verdict
  are designed so a future CI can call it unchanged).
- Interleaved A/B gating (`--gate --against <revision>`), p95/p99 gating, per-variant
  gating beyond the first entry, and gating `frame_ms` on a non-vsync-limited
  present mode.
- Baselines for further hardware profiles, including a discrete-GPU profile whose
  noise band is expected to be tighter.
- A trend history of gate results and automatic refresh proposals.
- Validating the first optimization (point-light cost) under this contract — that is
  its own Spec, written once this one is implemented.
