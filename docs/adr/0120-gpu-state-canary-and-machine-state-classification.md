# ADR 0120: GPU-State Canary and Machine-State Classification

- **Status:** Accepted
- **Date:** 2026-10-15 (accepted 2026-10-15)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-15 (chat ruling on the mechanism after the m10c stop; the ADR text is reviewed in this
  branch's own PR, alongside Spec 0060's third Correction).
- **Related Spec:** [Spec 0060](../specs/0060-performance-contract.md) (`Approved`; third Correction 2026-10-15)
- **Related ADR(s):**
  - [ADR-0119](0119-benchmark-gate-protocol-and-verdict.md): the gate protocol and verdict. **D7's "no new
    measurement" is superseded here for the canary only**, and D4's account of when a verdict is `inconclusive` gains
    a second cause (machine state). Everything else in ADR-0119 stands.
  - [ADR-0118](0118-performance-contract.md): the process contract — unchanged.
  - [ADR-0117](0117-benchmark-host-json-schema-and-baseline-comparison.md): `atlantis.benchmark/1` — unchanged; the
    canary has a document of its own.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

The gate's first committed baseline could not be accepted. Twenty live gates on an unchanged build against a bar-passing
baseline (the m10c run) gave 4 pass, 4 fail and 12 `inconclusive`, with the CPU quiet and the GPU tier moving: from the
fourth gate on `gpu_busy_ms` read 20.5–26.5 ms against a baseline of 16.65 ms, back to normal for one gate, then slow
again. The machine was on mains power throughout, and the day before the same duty cycle had given 20 normal sessions.
The GPU of this machine is bimodal in a way no tolerance can absorb and no precondition the gate had could see.

What the 20 sessions show (offline replay of the verdicts' per-metric centers):

- The shift is **carried by the main pass**, not spread over the frame. In the 16 slow sessions `passes.draw.gpu_ms`
  and `passes.Opaque.gpu_ms` were 1.27–1.71 × the baseline and `Transparent` 1.44 ×, Δ`gpu_busy_ms` / Δ`draw` was
  0.96–1.12, while `shadow` moved 1.02–1.15 ×, `output_transform` 0.99 × and the small full-screen bloom passes
  1.03–1.10 ×. It is a state that slows fragment-heavy, texture- and bandwidth-bound work and leaves depth-only and
  trivial full-screen work alone, not a uniform clock scaling.
- A paired check with the fixed workload below, run on the same machine against a short Bistro benchmark, read
  `alu` unchanged and `bw` and `tex` about 2.1 × right after the benchmark, back to normal within a minute.

**The identifiability problem.** From the frame's own numbers in one session, a real regression in the main pass and
this state shift look the same: the same passes slower by a similar factor, the others unmoved. Two cheaper mechanisms
were weighed and rejected for exactly that reason: a **self-referential probe** (judge the GPU tier against a
reference taken from the frame's own non-affected passes) and a **bare proportional heuristic** (classify a GPU shift
that is consistent across passes as the machine's). Both are observationally equivalent, in single-session data, to a
global real regression; and the second does not even describe the observed state, which is pass-selective, so its
"proportional" signature never holds. Only a measurement that the code under test cannot move can tell the two apart.

## Decision

1. **A fixed GPU canary.** The gate owns a micro-workload that shares no code with the renderer and cannot be moved by
   a renderer change: three fragment workloads on a headless device through the public RHI — **alu** (a long dependent
   mad/frac chain per fragment, no memory traffic), **bw** (32 fullscreen draws into a 2560×1440 RGBA16F target: write
   bandwidth) and **tex** (16 dependent pseudo-random fetches from that target: read bandwidth and latency) — each in
   its own GPU-timed region, 25 repetitions after 5 discarded, the median of each as the reading. The shaders are
   embedded in the executable as SPIR-V (`src/runtime/canary/`), so the same bytes run on every machine; the draw
   counts, the target sizes and the shaders are the *workload*, identified by `gate-canary/1`. Changing any of them is
   a new workload id, and a baseline recorded under another id is not comparable. The canary is a child process,
   `atlantis_runtime --canary`, like `--benchmark`: executable-private, Windows, profiling builds, pinned like the
   benchmark children, writing an `atlantis.benchmark.canary/1` document. It reads three components so that whichever
   resource the state affects shows in at least one, and reports which.
2. **Where it is measured.** Once right after the idle wait (before the preheat) and once right after the last run of
   every session, and in every session of `--record`. The readings go into the set as a `canary` member
   (`measurements`, and an `aggregate` of center, sigma, min, max and n per component); a baseline therefore carries
   the **reference** of the state it was recorded in.
3. **The verdict model — four cases.** For the gated GPU tier only: canary stable and GPU tier stable → `pass`; canary
   stable and a GPU metric `regressed` → `fail` (the code did it); canary shifted and GPU tier stable → `pass`, the
   shift noted; canary shifted and a GPU metric `regressed` → **`inconclusive` with the reason stated** (the machine's
   state did it, or at least cannot be told from the code), exit 5. A counter regression or a CPU-tier regression
   fails whatever the canary did, and a headline-total dispersion trip (ADR-0119 D4, Correction 2026-10-14) still
   withholds the verdict first. "Shifted" means that a component's slowest candidate reading is above the baseline's
   reference by more than the policy's `canary.max_shift_pct`; only a slower machine counts, because a faster one
   cannot explain a GPU regression away. gate/1 gains additive members: `canary` (reference, slowest candidate reading
   and shift per component, the threshold, whether it shifted) and `machine_state` (`stable` | `shifted` | `unmeasured`
   | `unjudged`, with the GPU metrics it explains).
4. **A start-of-session precondition.** When the baseline the session will be judged against can be found from the
   canary child's own machine identity, and the start canary is already above that baseline's reference by more than
   the limit, the session is refused before the preheat and the runs are spent: exit 3 (environment), like the idle
   and mains-power preconditions. A machine the store does not recognise is not refused here.
5. **The canary is part of the comparison.** With the policy's `canary.required` (on), a baseline without a canary
   reference, a run without a canary reading, or a different workload id is `incomparable` with the reason stated.
6. **Recording.** `--record` measures the canary in every session and refuses (exit 3, nothing written) a recording
   whose canary readings spread by more than the policy's `record.max_canary_spread_pct` (max over min, worst
   component): a recording made across two GPU states is a baseline for neither. The store guard applies the same bar
   to a committed baseline and requires the reference to be present.
7. **What this supersedes.** ADR-0119 D7 says the protocol "adds nothing to the frame loop, zones, regions or counters
   of Spec 0059" and lists "no new measurement". The canary adds nothing to the frame loop, zones, regions or
   counters and sets no clock or power plan; but it is a new measurement, so D7's clause is superseded **for the canary
   and only for it**. ADR-0119 D4's sentence that a verdict is `inconclusive` "when the candidate runs' own dispersion
   exceeds a policy multiple of the baseline band" remains true and is no longer the only cause. The gate still does
   no hardware control.

The premise — that this fixed workload tracks the state that moves the main pass — is **validated live, at the first
acceptance run, not by this ADR**: the offline evidence is the paired reading above and the identifiability argument,
not a statistic. If the canary stays stable while the main pass slows, the rule would call the machine's state a
failure; the acceptance run's injected global slowdown (canary stable, must fail) and its machine-state outcomes
(canary shifted, must be withheld) are the test.

## Consequences

### Positive

- The contract keeps its promise not to lie while admitting that this machine's GPU has two states: a GPU change is
  a `fail` only when the machine demonstrably was in the baseline's state, and is withheld, with the reason, when it
  was not.
- Detection is unchanged where the canary is stable: the GPU tier fails at exactly the sizes the tolerance resolves
  (the M7 injection model, canary stable). A real regression coinciding with a state shift is `inconclusive`, never
  a pass, and a re-run in the baseline's state judges it.
- The canary reports which component moved (`alu`, `bw`, `tex`), so it also says which resource the state affects.
- No change to `atlantis.benchmark/1`, to the frame path, to the renderer or to the benchmark children's workload.

### Negative / Trade-offs

- A new measurement and a new child process per session (about one second each), a fixed shader set to keep, and a new
  workload id to bump deliberately if the workload ever changes.
- A bad-state day yields machine-state outcomes instead of verdicts; nothing can make a bimodal machine deliver a
  verdict it cannot support. The acceptance criterion is reframed accordingly (Spec 0060 Correction).
- The canary stresses ALU, write bandwidth and read bandwidth through the RHI; a state that affects something else
  (a fixed-function unit, the texture sampler's filtering) would not show in it, and would be misread as a code
  regression. The three components are a hedge, not a proof.
- It needs a profiling build and a device with GPU timestamps, like the benchmark.

## Alternatives Considered

- **A self-referential probe from the frame's own passes.** Rejected: observationally equivalent, in one session, to a
  real main-pass regression; it cannot separate the machine from the code.
- **A bare proportional heuristic** (a GPU shift consistent across passes is the machine's). Rejected: equivalent to
  a real global regression, and contradicted by the data (the observed state is pass-selective).
- **Gating the GPU tier only on counters and report-only for times.** Throws away the signal the contract exists for.
- **Waiting out the state** (cool-down gaps, retries until the machine is normal). Does not classify the outcome and
  cannot be shown to work: the state flipped back to normal for one gate in twenty with nothing different done.
- **Hardware control** (locking GPU clocks or power plans). Out of scope (ADR-0119 D7) and not available without
  elevation.
- **The canary inside the benchmark child** (as extra regions of the frame). Would change `atlantis.benchmark/1` or
  couple the canary to the renderer's code, which is the property being avoided.
- **An interleaved comparison against a freshly built baseline binary.** Named in Spec 0060 as the escalation for a
  regression that matters; not built here.
