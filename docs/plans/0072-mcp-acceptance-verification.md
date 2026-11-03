# Plan: MCP Acceptance — Applicability, Pinning Test Capability and Bounded Investigation

- **Spec:** [Spec 0072](../specs/0072-mcp-acceptance-verification.md) (Approved)
- **Status:** Approved
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** slmao, 2026-10-31 — this Plan and Spec 0072 reviewed together;
  J1–J4 each approved as recommended. [Owner ruling and distinct authorization](https://github.com/slmao/Atlantis/pull/309#issuecomment-6539457400).
  **Authorization:** S2–S4 on `feature/0070-agent-tool-layer` in PR #307 after the human merge
  of PR #309 (satisfied 2026-10-31, 13:14:56 China Standard Time), within the approved scope,
  budget and stops. Missing Windows 11 verification and unresolved performance stay blocking;
  PR #307 stays Draft. Stop after this execution; no automatic merge.
- **Related:** Approved [Plan 0070](0070-agent-tool-layer.md) M5 and
  [Plan 0071](0071-agent-tool-authoring-failures.md) S5;
  [PR #307 diagnosis](https://github.com/slmao/Atlantis/pull/307#issuecomment-6539088905).
- **ADR disposition:** J4 confirmed; Accepted ADR-0118/0119/0130/0138 retained unchanged.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
This is an additive review proposal; approved documents do not become execution journals.

## Objective

Resolve the applicability expectation and precisely scope a test-only compatibility repair,
then conduct a finite environmental investigation if explicitly authorized. Completing the
investigation may leave M5 acceptance blocked; it is not authority to forgive a performance fail.

## Milestones / Task Breakdown

1. **S1 — Joint review only (this PR).** Review Spec R1–R6 together, rule on J1–J4 below and
   separately decide future execution authorization. Record any approval concisely in this header
   with reviewing-PR evidence only after the human ruling; leave implementation/results in PR #307.
   Keep that feature PR Draft; do not integrate or implement during this request.
2. **S2 — Test-only capability repair, after the gate.** On `feature/0070-agent-tool-layer` in
   PR #307, include the human-merged approved supplement, then change only the existing pinning
   test translation unit. Add the private version/query classification and pure negative cases
   for R2 without a public seam, link-library change or production edit. Verify setup and exact
   affinity before optional readback can affect control flow. Emit query subcheck status and
   actual facts even when the outer case passes. No full-case `SKIP` for missing readback.

   Build `atlantis_runtime_gpu_tests` in Debug and Release. Run the classification and original
   pinning cases with verbose output on the Windows 10 reference host; run the real readback
   case on Windows 11 22H2+ with the same change. Log all success-bit/error/version outcomes.
   Use a separate already-available supported-OS host; do not upgrade the reference machine or
   install tools to obtain this check. Missing supported hardware is a reported R6 verification
   blocker, not permission to simulate it away or to alter production behavior. Run relevant native Runtime regression tests after
   the change; any GPU-touching tests use the existing fatal-clean validation configuration.
3. **S3 — Bounded environmental investigation.** Independently of supported-host availability,
   perform R4's one read-only audit and check the evidence/hardware/binary manifests. Within the
   single R4/R5 budget, register one testable hypothesis and the exact observational method before
   choosing to start its optional batch. If none is available, stop with an unresolved report.

   For a batch, freeze and publish both commits, native flags/content hashes, policy/store hashes,
   observer fields/cadence, the A–B–B–A schedule and evidence paths. Keep compile/test work outside
   measurement and meet the quiet separation. Use the unchanged official gate; count every attempt
   and preserve its raw children before the gate cleans its temporary directory. Check collection
   completeness afterwards; a gap is a limitation, not a reason to repeat. Follow every R5 stop.
4. **S4 — Evidence and hold point.** Update PR #307 with literal gate outcomes, supported/unsupported
   readback checks, raw-artifact/hash references, any hypothesis evidence and unresolved causes.
   Evaluate applicability on the current complete PR diff under R1, without altering the selector.
   Retain earlier failed results and original M5 checks. Verify protected paths and document the
   exact test-file delta. No automatic fifth invocation, M5 completion, ready transition or merge.
   Further investigation or a fresh final-acceptance execution beyond this budget needs another
   explicit bounded authorization; policy/baseline changes remain outside this Plan.

## Files / Modules Touched (expected)

**This joint-review PR only:**

- New `docs/specs/0072-mcp-acceptance-verification.md` and this Plan.
- `docs/specs/README.md`: one review/status/navigation row.

**Only after approval and distinct implementation authorization:**

- `tests/runtime/gate_process_gpu_tests.cpp`: the single intentional scope extension to Plan
  0070, containing the test-private classification/version helper, cases and query diagnostic.
  No new CMake target, file, library or general-purpose OS abstraction is planned.
- `docs/specs/README.md`: coarse implementation status link if needed under the standing lifecycle.
- Disposable investigation helpers, logs/raw data and manifests only under ignored `build*/`
  output of clean isolated worktrees; PR #307 owns their method/results. No committed tool system.

**Unchanged:** production `src/**` (including `src/runtime/gate_process_windows.cpp`), root/CMake
configuration, `tests/performance/**`, SDKs, wire, schemas, packages, Android, goldens, AGENTS.md,
approved Specs/Plans and every Accepted ADR. Unexpected implementation needs are deviations to
report before coding; this list is not an invitation to expand the test infrastructure.

## Sequencing & Dependencies

- S1 is docs-only on an independent branch from `origin/main`, separate from PR #307's code.
- S2–S4 start only after joint review explicitly authorizes them and a human merges this proposal.
  S2's build/relevant regression precede measurement; no test/build runs during S3's batch.
- The supported-Windows positive check blocks test-repair acceptance even if the Windows 10
  checks and environmental work finish. S3 has its own stop/budget and does not wait indefinitely
  or restart when that host becomes available.
- This proposal does not alter the approved original milestones, replay implementation history,
  or resolve the original failure by the diagnostic A/B mean. No prerequisite is silently waived.

## Joint Review — Put for Ruling (J1–J4)

| Ruling | Recommended decision |
|---|---|
| **J1 — Applicability and gate** | Accept R1/R3: record whole-PR true additively, keep the unchanged formal obligation and every prior fail; no selector exception or waiver. |
| **J2 — Capability repair** | Accept R2/R6 and S2's single-file scope, narrowly recognizing the known legacy desktop query case while preserving mandatory setup/priority/affinity and supported-OS readback. Require real Windows 10 and Windows 11 22H2+ evidence plus negative cases. |
| **J3 — Investigation limit** | Accept R4/R5: one audit, at most one registered ABBA batch, the stated time/attempt caps, read-only built-in observations and explicit unresolved stops. No environment intervention, Runtime instrumentation, additional batch or baseline action. |
| **J4 — Architecture** | Keep ADR-0118/0119/0130/0138 Accepted and unchanged; no new/superseding ADR because this proposal changes feature verification/test capability handling only. Any contrary architectural ruling returns to Spec/ADR review before code. |

**Distinct future authorization requested:** after jointly reviewing Spec/Plan 0072, explicitly
authorize S2–S4 on `feature/0070-agent-tool-layer` in PR #307 only after this proposal's human merge,
subject to its limits and unresolved holds. Individual document approval, creating this PR or
approving a diagnosis alone grants no execution permission. This request executes S1 only and stops.

## Verification Checklist

**Proposal stage:**

- [ ] Check the original whole-PR selector inputs, unchanged test/production implementation and
      controlled-diagnosis evidence against R1/R2/R3; use existing evidence without new measurements.
- [ ] Validate relative Markdown links, `git diff --check` and a three-file docs-only diff.
      Check no requirement/ADR, test, policy or baseline change and PR #307 remains Draft.
- [ ] Obtain J1–J4 rulings and the distinct joint authorization before any later implementation.

**After authorization, mapped to Spec requirements:**

- [ ] **R1/R3:** whole-diff selector result and exact matched paths; literal formal gate documents,
      exits and conformance; unchanged policy/store hashes. No invented N/A, median-based waiver,
      new CTest timing assertion or omitted earlier failure.
- [ ] **R2:** Debug/Release builds and pure classification coverage for successful correct/wrong
      masks, known legacy/version boundary, error 87 on a supported build, unknown identity,
      other errors and malformed requests. Always verify setter/priority/affinity independently.
- [ ] **R2/R6:** actual reference Windows 10 subcheck marked unavailable with all mandatory checks
      passed, actual Windows 11 22H2+ readback verified, or explicitly report missing evidence as
      blocking. Relevant Runtime tests run; GPU paths remain Vulkan-validation clean.
- [ ] **R4/R5:** frozen inputs and a pre-measurement registration, timestamps/budget ledger, no more
      than four total attempts, complete raw data or declared gaps, before/after state and observer
      limitations; stop evidence retained. An interrupted batch is not replaced or called passed.
- [ ] **R6:** final unresolved list in PR #307, protected-path/golden/conformance hash/diff checks;
      original M5 obligations retained. No ready/merge while blockers remain.

## Rollback Plan

Withdraw this proposal without changing approved history if rejected. If an authorized repair
is later reverted, the old query failure remains disclosed; retain all diagnostic results.
Temporary observers stop with their owned processes and make no persistent machine change.
No schema, baseline or engine state migration is involved.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). No repository-wide deltas.
This stage is complete at joint-review submission with documentary checks; approval remains
pending. Later test/investigation completion is distinct from M5 acceptance and cannot resolve
missing supported-host evidence or a performance failure by itself.
