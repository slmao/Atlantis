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

Build with profiling on, Release, on the machine that has a baseline for it (the verdict says if it has none):

```
cmake -S . -B build-prof -DATLANTIS_PROFILING=ON
cmake --build build-prof --config Release
build-prof/src/runtime/Release/atlantis_runtime --gate --json
```

One command, one verdict document on stdout, one exit code. It takes about three minutes: it waits for the machine to
be idle, runs one discarded **preheat** of 60 s, then five independent `--benchmark` processes, each **pinned** (the
child opts out of process power throttling and runs at high priority, so the OS scheduler does not park the render
thread on a slow core; a child that cannot be pinned exits 3). Leave the machine alone while it runs; a build or a
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
| 5 | `inconclusive` | Not judged: the session was noisier than the baseline's own spread (`dispersion.exceeded`). Run it again on a quiet machine. |
| 1 | | Usage error or an unreadable policy or store. |
| 2 | | A benchmark child failed, timed out or wrote no document. |
| 3 | | Unsupported build (no `ATLANTIS_PROFILING`), the scene is absent, the machine would not go idle in time, pinning failed, or the machine is on battery power (or cannot say). |

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
`<dir>` and nothing else. The PR that commits it names one reason category, as ADR-0042 does for goldens:

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
for a one-session baseline. On noisier hardware the GPU tier resolves only large regressions; a smaller one is not reliably caught, and the
escalation for a regression that matters (interleaved comparison against a freshly built baseline binary) is named in
Spec 0060, not built.
