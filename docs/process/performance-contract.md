# Performance Contract

Operator note for the Performance Contract of [Spec 0060](../specs/0060-performance-contract.md), implemented by
[Plan 0060](../plans/0060-performance-contract.md). The normative decisions are
[ADR-0118](../adr/0118-performance-contract.md) (the process: the committed baseline store, its refresh rule, the
opt-in gate and its applicability) and [ADR-0119](../adr/0119-benchmark-gate-protocol-and-verdict.md) (the protocol,
the documents and the command surface). This page says how to use them; it does not restate them. Measuring and
reading a single benchmark run is [profiling.md](profiling.md).

The contract makes performance regressable the way image goldens make rendered output regressable: a repeatable
protocol, a committed baseline per hardware profile, a gate whose verdict an agent can read, and a rule for which
changes must run it. It is a **local, manual** gate like image regression: run and reported by whoever opens the PR.
No CI runs it, and no CTest asserts a measured time.

## Running the gate

Build with profiling on and **Tracy off**, Release, on the machine that has a baseline for it (the verdict says if it
has none):

```
cmake -S . -B build-prof -DATLANTIS_PROFILING=ON -DATLANTIS_PROFILING_TRACY=OFF
cmake --build build-prof --config Release
build-prof/src/runtime/Release/atlantis_runtime --gate --json
```

The Tracy switch is part of a baseline's identity (`meta.build.tracy`) and defaults to on with profiling: the
reference baseline is the profiling-on, Tracy-off configuration (Spec 0060's build config B), and a Tracy-on build is
`incomparable` against it. Configure from a clean checkout of the commit you mean to measure: the build records its git
revision and dirty flag when CMake configures, not when it builds.

One command, one verdict document on stdout, one exit code. It takes about three minutes: it waits for the machine to
be idle, measures the **GPU canary** (below), runs one discarded **preheat** of 60 s, then five independent
`--benchmark` processes, waits 3 s and measures the canary again (right after a benchmark child exits the GPU is in
that child's tail for about a second, which is not the machine's state), each child **pinned** (the
child opts out of process power throttling, runs at high priority and is restricted to the cores of the highest
efficiency class -- the P cores of a hybrid CPU -- so the OS scheduler neither parks the render thread on a slow core nor
moves it between core classes from one session to the next; a child that cannot be pinned exits 3). Leave the machine alone while it runs; a build or a
browser started during it shows up as an `inconclusive` verdict.

**Mains power only.** The gate and `--gate --record` refuse to run on battery (exit 3, before any child starts), and
refuse again if the charger is pulled during a session. On the reference laptop (an integrated GPU) the GPU time of the
same scene moved from about 16.5 ms on mains power to about 28 ms on battery within one recording, a shift no
tolerance can absorb. The source is read with the Windows power status (`GetSystemPowerStatus`) before and after every
session and recorded as `protocol.power` (`mains`, `battery`, `unknown` or `mixed`) in `set/1` and `gate/1` and as
`baseline.power` in a baseline's provenance; a machine that cannot say where its power comes from is refused too, and a
run on any source but `mains` is never compared with a mains baseline (`incomparable`).

| Flag | Meaning |
|---|---|
| `--scene`, `--variant`, `--resolution` | What to measure; `bistro`, `baseline`, 1920x1080 by default (the committed baseline). |
| `--json` | The verdict document on stdout (else a short summary). |
| `--output <path>` | The verdict also to a file. |
| `--baselines <dir>`, `--profile <id>\|auto`, `--policy <file>` | Another store, a named profile, another policy. |
| `--runs`, `--preheat-seconds`, `--idle-wait on\|off`, `--pin-process on\|off`, `--require-mains on\|off` | Smoke overrides (`--require-mains off` lets a run go on battery, recorded as such). A run that departs from the policy says `protocol.conformant: false` and is not evidence for a PR; `--record` refuses it. |

## Reading a verdict

`atlantis.benchmark.gate/1`. The `verdict` field and the exit code say the same thing:

| Exit | `verdict` | Meaning |
|---|---|---|
| 0 | `pass` | No gated metric regressed beyond its tolerance. Improvements are listed and never fail the gate. |
| 4 | `fail` | A gated metric regressed. `summary.regressed` names them; `metrics.<key>` carries `baseline`, `candidate`, `delta`, `delta_pct`, `tolerance` and `tolerance_source` (`floor`, `max_pct`, `noise`, `exact`, `relative`). |
| 5 | `incomparable` | Not judged: this machine matches no baseline profile, the profile has no baseline for this scene, variant and resolution, the workload counters differ (the scene changed), or the run and the baseline differ in hardware, build, validation or pinning. `reasons` says which and what to do. |
| 5 | `inconclusive` | Not judged, for one of two reasons. The session's total CPU or GPU time (`cpu_active_ms`, `gpu_busy_ms`) was noisier than the baseline's own spread (`dispersion.exceeded`): run it again on a quiet machine; a zone or pass that was noisy while the totals were steady is listed in `dispersion.reported` and does not withhold the verdict. Or the **machine's GPU was in another state** than the baseline's (`machine_state.classification` is `shifted`, `reasons` says by how much): the GPU tier regressed, but the canary shows the machine, not necessarily the code, moved; run it again later. |
| 1 | | Usage error or an unreadable policy or store. |
| 2 | | A benchmark child failed, timed out or wrote no document. |
| 3 | | Unsupported build (no `ATLANTIS_PROFILING`), the scene is absent, the machine would not go idle in time, pinning failed, the machine is on battery power (or cannot say), or the GPU canary was already above the baseline's reference at the start of the session (nothing was spent: wait and run again). |

Each gated metric in the verdict carries `baseline_sigma` and `candidate_sigma`, the spread behind its comparison.

A counter that grew (`draw_calls`, `descriptor_writes`, ...) fails the gate whatever the noise: counters are
deterministic. CPU and GPU metrics are compared against the baseline's noise band; their tolerance is the largest of an
absolute floor, a percentage of the baseline and the measured noise. The policy file,
[tests/performance/gate_policy.json](../../tests/performance/gate_policy.json), holds every number; its `measured`
block names the calibration study that fixed them (12 sessions of 5 pinned runs on the reference machine:
a 60 s preheat, a tolerance of 5 % on CPU metrics and 10 % on GPU metrics, `k` = 3, a dispersion factor of 4).
One metric is report-only on purpose: `passes.shadow.gpu_ms`, whose per-session level moved by more than the GPU
tolerance between otherwise identical sessions, so gating it would fail unchanged code. It stays in every verdict's
`reported` section; a metric named in the policy's `reported` list is never gated, even when a gated pattern matches it.

## Which PRs must run it

[tests/performance/applicability.txt](../../tests/performance/applicability.txt) lists the paths whose changes require
the gate (the frame path: renderer, render graph, RHI, backend, runtime, World, shaders, the profiling code, the build
files, and the performance tree itself). Ask it about a change set:

```
git diff --name-only origin/main... | atlantis_runtime --gate --applies
```

It prints `{"applies": bool, "matched": [...]}` and judges nothing. A PR it selects reports the verdict document, or
says N/A with the reason; where no baseline exists yet for the machine's profile the verdict is `incomparable` and the
item is N/A with that reason. A `fail` or `inconclusive` is reported with its verdict; the merge decision is a human's.

## The GPU canary and machine state

The GPU of a laptop can sit in a slower state for hours, for no reason the frame can show: on the reference machine the
main pass read 25-60 % slow for most of one run of twenty gates, on mains power, and was normal the day before. From the
frame's own numbers that is indistinguishable from a real regression in the main pass, so the gate carries a measurement
the code under test cannot move: a **fixed GPU micro-workload** (`atlantis_runtime --canary`: an ALU-bound, a
write-bandwidth-bound and a read-bandwidth-bound fragment workload, embedded in the executable, nothing shared with the
renderer), measured right after the idle wait and `canary.settle_seconds` (3 s) after the last run of every session, about a second each. Its
readings are in the set (`canary`) and in the verdict (`canary`, `machine_state`):

| Canary | GPU tier | Verdict |
|---|---|---|
| stable | stable | `pass` |
| stable | `regressed` | `fail`: the machine was in the baseline's state, so the code did it |
| shifted | stable | `pass`, the shift noted in `machine_state` |
| shifted | `regressed` | `inconclusive`, reason stated: the machine's state, not necessarily the code, moved |

"Shifted" means a component's slowest reading is more than `canary.max_shift_pct` (8 %) above the baseline's reference;
a faster machine never counts. CPU and counter regressions are never excused by it. A baseline recorded before the canary
existed, or a different canary workload, is `incomparable`. A gate that finds on a bad-state day only machine-state
outcomes has told the truth, not failed: run it again later, or on a machine in the baseline's state. Do not raise
`canary.max_shift_pct` to make a gate pass. See [ADR-0120](../adr/0120-gpu-state-canary-and-machine-state-classification.md).

## The baseline store and its refresh

Baselines are committed under `tests/performance/baselines/<profile>/`: a `profile.json` (the hardware identity: OS,
CPU, GPU, driver, Vulkan version, present mode, refresh rate, matched exactly) and one baseline per scene, variant and
resolution, each recorded over three sessions of five runs with the pinned protocol and carrying its provenance (commit,
sessions, pinning state, power source, the stated reason) and the minimum detectable regression of its profile. A different driver is a
different profile, so a driver update retires the baseline until it is refreshed.

A baseline is only ever changed by a **dedicated PR**, reviewed by a human, never bundled into another change, never
regenerated by tooling, and never by an agent without an explicit human instruction. Record a candidate with the stated
reason (`--record` refuses without one, and refuses a smoke protocol):

```
atlantis_runtime --gate --record --record-to <dir> --reason-category <1-4> --reason "<why>"
```

It runs the protocol over three sessions at least ten minutes apart (about 30 minutes), writes the candidate under
`<dir>` and nothing else. It also **refuses a recording that is too noisy**: if the recording's own minimum detectable
regression (below) exceeds the policy's record bar (`record.max_mdr_cpu_pct`, 10 %, and `record.max_mdr_gpu_pct`, 20 %),
it exits 3, prints the numbers and writes nothing; record again on a quiet machine on mains power. A recording is also
refused when its own GPU canary readings spread by more than `record.max_canary_spread_pct` (5 %): it was made across two
GPU states. The recording carries the canary reference (`canary` in the baseline). The bar is a
pre-declared objective criterion, fixed in the policy before any recording: refusing a recording by it is measurement
hygiene, while recording until a baseline happens to pass an acceptance run would be cherry-picking. Do not raise the
bounds to get a recording through, and say how many recordings the bar refused when you commit the one that passed. The PR that commits it names one reason category, as ADR-0042 does for goldens:

1. **performance change**: an intentional change to the frame cost, naming it (an accepted regression gets an explicit
   justification: there are no per-metric exemptions, only a refreshed baseline);
2. **reference-environment change**: the driver, OS, CPU or GPU changed; carry the old and new provenance and evidence
   that no regression hides behind it;
3. **approved rebaseline**: a deliberate human decision for another stated reason, with the same reasoning;
4. **initial baseline**: a hardware profile that has none.

The store guard (`tests/runtime`, no device) fails any committed baseline that lacks a stated reason, sits in the wrong
place, belongs to another profile, or was not recorded under the full protocol.

## What the numbers resolve

The gate states its own resolution rather than promising one: each baseline carries `mdr`, the smallest relative
regression (per tier) that a five-run gate detects 90 % of the time against that profile's noise. Counters are exact.
On the reference machine the calibration study measured a CPU resolution of 5-7 % and a GPU resolution of 10-16 %
for a one-session baseline (5.4-6.5 % and 10.6-19.1 % for a three-session one; that range is why the record bar sits at
10 % and 20 %). On noisier hardware the GPU tier resolves only large regressions; a smaller one is not reliably caught, and the
escalation for a regression that matters (interleaved comparison against a freshly built baseline binary) is named in
Spec 0060, not built.
