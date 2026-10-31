# Spec: MCP Acceptance — Applicability, Pinning Test Capability and Bounded Investigation

- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-10-31
- **Related Plan(s):** [Plan 0072](../plans/0072-mcp-acceptance-verification.md) (In Review)
- **Approval:** Pending joint human review; no implementation authorization.
- **Related ADR(s):** ADR-0118, ADR-0119, ADR-0130 and ADR-0138 retained; see Architectural Impact.
- **Supplements:** [Spec 0070](0070-agent-tool-layer.md) / [Plan 0070](../plans/0070-agent-tool-layer.md)
  M5 and [Spec 0071](0071-agent-tool-authoring-failures.md) / [Plan 0071](../plans/0071-agent-tool-authoring-failures.md) S5.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Requirements belong here; review and execution evidence remain in their PRs.

## Summary

An additive acceptance correction for [PR #307](https://github.com/slmao/Atlantis/pull/307):
recognize its selected performance gate, repair the Windows test's unconditional power-state
readback assumption, and bound further investigation of the shared performance shift. This
proposal neither changes performance policy nor excuses a failed verdict. The current request
ends at joint-review submission; the feature PR stays Draft.

## Motivation / Problem Statement

The [registered controlled diagnosis](https://github.com/slmao/Atlantis/pull/307#issuecomment-6539088905)
is the evidence home. The approved build/policy edits select the whole PR although 0070/0071
expected `applies=false`. The unchanged pinning test fails on both frozen branches at a Windows
10 power-state query even though setup succeeds. Both branches also cross the historical CPU
gate threshold in a fixed ABBA comparison; neither a stable feature increment nor its physical
cause was established. These are separate acceptance issues, not grounds to change the tool layer.

The [original test](../../tests/runtime/gate_process_gpu_tests.cpp) assumes readback on every
Windows host. [Microsoft's query contract](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessinformation)
requires the correct state structure, size and query rights; the function's generic minimum OS
does not establish each information class's availability. The [pinned Chromium implementation](https://chromium.googlesource.com/chromium/src/%2B/refs/tags/140.0.7327.0/base/win/win_util.cc#389)
corroborates the pre-Windows-11-22H2 query limitation. Its production setting behavior is not
adopted here. Detailed measurements and machine records are linked above, not copied into this Spec.

## Goals

- Correct the feature's applicability expectation through review without rewriting approved history.
- Keep production pinning mandatory while testing optional readback accurately on the reference OS.
- Produce a finite, auditable environmental investigation, including an unresolved outcome.

## Non-Goals

- No selector, tolerance, run-count, baseline, verdict, benchmark schema or acceptance waiver change.
- No Runtime pinning/frame-path fix, optimization, new dependency, SDK/wire/tool or Android change.
- No clock, power-plan, BIOS, driver, OS or device change; no tool installation or new ETW instrumentation.
- No automatic M5 completion, feature readiness or merge; no measurement repeated until it passes.

## Requirements

### Functional

- **R1 — Whole-PR applicability.** Supplement the false expectation in 0070 M5 / 0071 S5 with
  the actual `applies=true`, currently matching `CMakeLists.txt` and
  `cmake/check_editor_app_packages.cmake`. Evaluate the complete base-to-head PR diff with the
  unchanged selector; preserve any additional matches at execution time. This correction adds
  no path exemption and does not edit the approved documents in place.
- **R2 — Pinning versus readback.** Every applicable Windows pinning test still requires the
  production `pinCurrentProcess()` call to succeed, an empty setup error, high priority and the
  exact selected single-processor affinity inside the system mask. Reach those checks independently
  of optional readback. Query power state once with a well-formed current-version structure and
  its exact size, capture last-error immediately on failure, and classify only that subcheck:

  | Observation | Required outcome |
  |---|---|
  | Query succeeds on any OS | Verify execution-speed ControlMask set and StateMask clear; wrong bits fail. |
  | Query fails with error 87; request valid; independently established Windows desktop version 10.0, build 10240 through 22620 | Explicit `readback unavailable` diagnostic with OS, error and request facts; never report successful readback. Other pinning checks remain mandatory. |
  | Invalid request, or a failed query outside the preceding legacy case (including unknown identity/version-probe failure or error 87 on build 22621+) | Fail; no catch-all unsupported/error-87 rule. |

  Obtain real version/product identity in test-private Windows code, independent of application
  manifest version emulation; the recommended mechanism is a dynamically resolved `RtlGetVersion`
  in the already-loaded OS DLL ([user-mode precedent](https://chromium.googlesource.com/chromium/src/%2B/dcb176b30e206edb46062e6dc20d849b3a5e2c30/chrome/updater/util/win_util.cc)),
  with no new link library or driver header. A successful query
  must be checked even on a legacy version. Lack of known legacy identity cannot excuse a failed
  query. No change to production pinning or its recorded protocol is permitted.
- **R3 — Formal performance obligation.** Use the unchanged committed store and policy with
  Release, profiling ON, Tracy OFF and the prescribed reference settings. Selected formal gates
  retain idle, mains, pinning, preheat and run-count checks; record their full verdict, actual exit,
  comparability and conformance. Retain the original M5 fail and every diagnostic outcome.
  A/B ratios, byte-identical instructions, an unavailable readback or a passing later attempt
  do not waive failure, synthesize pass, or make the selected item N/A. Existing repository handling
  of genuinely incomparable/unsupported results is unchanged; report reasons and keep unresolved
  acceptance blocked. This proposal authorizes no baseline recording, refresh or per-metric waiver.
- **R4 — Bounded environmental investigation.** After distinct authorization, conduct one
  read-only audit of at most **30 minutes**, inside a **90-minute total investigation budget**.
  Audit the preserved baseline provenance and frozen A/B inputs against OS/kernel/module and
  toolchain versions, topology, selected GPU/driver/device state, display, mains/power scheme and
  background activity. Missing historical facts stay missing; do not infer environment equality
  from a profile match alone.

  Only if this audit yields a written, testable shared-shift hypothesis and sufficient existing
  observation capability may it proceed to **at most one additional four-attempt A–B–B–A batch**.
  Register commits/binary hashes, configuration, hypothesis, order, count, observer, sampling
  fields and limitations in PR #307 before the first attempt. Prefer the preserved diagnostic A/B
  snapshots; any replacement must be fixed and disclosed before measurement. Native input
  mismatch stops the batch rather than silently changing the comparison.

  Permitted observation is external read-only inventory plus available built-in Windows processor
  frequency/performance and idle-state counters, at no more than **one sample/second**, with the
  observer confined to logical processor 0 on the reference host. Use the same observer on both
  sides; benchmark work retains the policy's selected processor. No new recorder in the Runtime,
  system setting, installed provider, tracing dependency or clock control. Record absent counters
  and observer interference; correlation alone is not causal proof. Keep at least 60 s quiet between
  attempts, no concurrent build/test work, and invoke only R3's unmodified formal gate.
- **R5 — Stops and evidence.** Stop at the first of: completed registered batch, exhausted time
  budget, no testable hypothesis/needed observation after audit, mismatched or dirty inputs,
  changed registered hardware/build/display, AC power source or configured power scheme, or need
  for an unapproved tool, setting, code or scope change. A refused, failed or interrupted attempt
  counts against the four-attempt ceiling;
  no replacement, added phase, alternative order, fresh baseline or restart of the budget. Observed
  CPU frequency/idle variation is the investigation subject, not itself a configuration-change
  stop. A normal comparable `fail` within the registered batch is retained and does not select a different schedule.
  An environment refusal or incomparable state stops further launches for review.

  At the 90-minute limit stop launches and terminate only owned diagnostic processes if still
  running; mark incomplete data explicitly without inventing a gate verdict or changing its
  policy timeouts. Preserve the original exit/verdict when one exists. Retain all child documents
  (including preheats), stdout/stderr, timestamps, counter samples, observations and hashes under
  ignored build output, with conclusions and limitations in PR #307. Do not repeat a run to repair
  missing evidence. A stop with no root cause is a valid investigation deliverable and leaves M5
  blocked; it is not a successful performance acceptance.
- **R6 — Closure and scope.** Positive real-host validation of readback on Windows 11 22H2+
  remains required alongside the Windows 10 unavailable case and pure negative classification
  tests; a simulated supported case cannot replace the real host. If unavailable, report that
  verification gap and stop acceptance work. The environmental investigation can independently
  finish within R4/R5, but does not resolve that gap. Keep PR #307 Draft while any required check,
  failure disposition or approved verification is unresolved. A later acceptance gate or further
  investigation beyond this ceiling requires a new explicit bounded execution authorization;
  a baseline change would additionally require the dedicated review path of ADR-0118.

### Non-functional

- No timing assertion in CTest; classification tests are GPU-independent, even in the existing
  GPU-labeled native test executable. No new native dependency or public test seam.
- Investigation outputs are uncommitted PR evidence, not a new data format or tooling subsystem.
- Production protocols and algorithms remain unchanged; uncertainty is reported rather than hidden.

## Proposed Design

Keep the live pinning assertions and a small, test-private classification helper in the existing
test translation unit. Separate setup/affinity verification from query availability and exercise
the helper with supplied version/result facts. The environmental work uses disposable scripts
and retained worktrees; it does not extend the shipped gate or establish another passing rule.
Plan 0072 maps the requirements to the narrow exception to 0070's test-file scope and to review.

## Architectural Impact

**None — no new or superseding ADR proposed.** Test-private OS inspection changes no production
module, API, threading model, dependency or measurement format. R1 corrects this feature's planning
expectation, not ADR-0118 D5's applicability policy; R3 retains ADR-0118/0119's gate and baseline
rules. ADR-0130 and ADR-0138 remain untouched. If review requests different runtime semantics,
policy, permanent telemetry or dependencies, stop for a separately scoped Spec/ADR decision.

## Alternatives Considered

- Selector exceptions or an incremental diff: would hide an existing whole-PR obligation.
- Skipping the complete pinning test or accepting every error 87: loses required checks and hides bugs.
- Changing production pinning to make a query pass: changes runtime behavior without evidence of setup failure.
- Repeating measurements, pooling them into a new pass bar, or refreshing a baseline here: violates R3/R5.
- An open-ended telemetry/optimization project: exceeds the disclosed evidence and finite diagnostic scope.

## Testing & Verification Plan

The Plan verifies R1 against the actual whole diff; R2 with legacy/supported/error/request cases
and both real OS capability classes; R3 with literal existing gate documents; R4/R5 with a
pre-registered finite ledger and preserved raw evidence; and R6 with explicit unresolved items.
Existing M5 tool, packaging, conformance, golden and Vulkan obligations remain intact.

## Risks & Open Questions

The legacy version/error exception is deliberately narrow and requires human approval. A real
supported-OS host is not established by the current diagnosis. External counters may be absent
or perturb timing; no unseen CPU wake/frequency cause is asserted. Budget exhaustion or a shared
shift may leave the PR blocked. Joint rulings and distinct future authorization are requested in
the Plan; submission of this proposal grants none.

## Out of Scope / Future Work

Runtime fixes, environment interventions, deeper tracing and any baseline action need separate
review/authorization. This proposal is not an approval of the diagnosis as a performance waiver.
