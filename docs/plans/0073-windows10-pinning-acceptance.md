# Plan: Windows 10 Acceptance Scope for the Pinning Test Repair

- **Spec:** [Spec 0073](../specs/0073-windows10-pinning-acceptance.md) (Approved)
- **Status:** Approved
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** slmao jointly approved Spec 0073 and this Plan on 2026-10-31,
  J1–J3 each as recommended; [approval and distinct S2–S3 authorization](https://github.com/slmao/Atlantis/pull/310#issuecomment-6544807766).
  Execution in PR #307 was conditional on the human merge of PR #310, satisfied by `447b03f`.
  No new live observation or performance execution is authorized.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Review the minimal acceptance-scope override in Spec 0073 without rewriting approved 0072.
Keep the unchanged pinning repair, closed investigation and unresolved performance holds distinct.

## Milestones / Task Breakdown

1. **S1 — Proposal and offline evidence (this round).** Create this additive Spec/Plan pair
   on an independent branch from `main`, plus one registry row. Compare R1 against 0072 R6,
   S2, the supported-host sequencing hold, J2 and R2/R6 checklist. Link existing Windows 10
   evidence in the reviewing PR. Under the owner's current offline authorization, retain a
   separate disposable observer candidate and injected-fixture results for R3; never edit or
   rerun the archived observer. Submit joint review and link it from PR #307, preserving Draft.
   Stop after submission even if a ruling arrives during this round.
2. **S2 — Scope recording only, after joint approval and human merge.** When separately
   authorized to integrate the accepted supplement in PR #307, record concise approval metadata
   and the new acceptance basis, verify R2 evidence still matches the repair, and distinguish
   the removed Windows 11 prerequisite from its unverified status. Update only the current
   acceptance summary/registry as needed; preserve approved 0072 requirements and historic results.
   No code repair, new measurement, ready transition or M5 closure is included.
3. **S3 — Hold.** Report remaining performance/observer-live limitations under R4 and stop.
   Any future acquisition needs explicit new bounded authorization; this Plan schedules none.

## Files / Modules Touched (expected)

- New `docs/specs/0073-windows10-pinning-acceptance.md` and this Plan.
- `docs/specs/README.md`: one navigation/status row for 0073.
- Disposable offline candidate, fixtures, results and integrity manifest only under ignored
  `build/0073-observer-offline/` in the existing feature workspace; evidence belongs in PRs.
- S2 only: concise lifecycle metadata in the new 0073 pair/row and PR #307's current summary.

No native test/production file, CMake, SDK, protocol, policy, baseline, golden, AGENTS.md,
approved 0070/0071/0072 requirement or Accepted ADR edit. Unexpected tracked files or semantics
are deviations to report before implementation, not an invitation to expand scope.

## Sequencing & Dependencies

0072 joint approval and the existing repair are prerequisites, with execution evidence in
[PR #307](https://github.com/slmao/Atlantis/pull/307#issuecomment-6539697114).
S1 ends at review submission. S2 requires joint Spec+Plan approval, a human merge and distinct
authorization; S3 preserves the existing stop. No future live observer preflight or acquisition
is implicit in approval of this acceptance supplement.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Platform scope** | Approve R1's repair-local replacement; require no real Windows 11 acceptance check for this repair and explicitly retain its unverified status. |
| **J2 — Evidence and tests** | Approve R2 evidence reuse only for the unchanged repair; retain every mandatory Windows 10 assertion and capability classification case. |
| **J3 — Observer, holds and architecture** | Accept R3's offline-only boundary and R4's unchanged performance/closed-budget holds. No live execution authority or ADR change; defer any new bounded authorization. |

## Verification Checklist

- [ ] **R1:** Review the exact override mapping; no silent edits to approved requirements or
      repository-wide support claims. Obtain distinct joint approval before S2.
- [ ] **R2:** Compare retained Debug/Release build and real Windows 10 logs, classification
      coverage and Runtime regression to the unchanged test-file revision. No full-case skip,
      erased supported/error scenarios or invented Windows 11 result.
- [ ] **R3:** Offline fixtures reproduce the slash mismatch; cover normalized exact matches,
      neighboring paths/configurations, invalid/missing paths, process-read errors and known
      gate PID/creation-time readiness. Preserve old artifacts; no live collection claim.
- [ ] **R4:** Check a three-file docs-only diff, local Markdown links, `git diff --check`,
      original evidence hashes, unchanged policy/store/repair and PR #307 Draft. Preserve
      prior failures and closed attempt ledger; launch no new performance work.
- [ ] **S2/S3, later:** Record the reviewed scope basis separately from remaining holds;
      no performance waiver, automatic ready/merge or reuse of stopped attempts.

## Rollback Plan

Withdraw this proposal if rejected; approved 0072 remains the acceptance basis. A later reversal
also uses an explicit reviewed supplement. Offline candidates can be discarded without changing
the archived evidence, native binaries, machine settings or gate store.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). No repository-wide deltas.
This round finishes at a reviewable proposal plus truthful offline evidence and remaining holds;
joint approval and any later S2 recording are not claimed as completed here.
