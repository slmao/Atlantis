# Plan: Feature Convergence and Proportional Verification

- **Spec:** [Spec 0080](../specs/0080-feature-convergence.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at owner direction
- **Joint Human Review:** Pending joint review of this Plan and Spec 0080;
  no active governance change yet.

## Objective

Make AGENTS.md the concise source for R1–R6 and remove contradictory execution
defaults without changing architecture, test verdicts or performance policy.

## Milestones / Task Breakdown

1. Submit this pair, one registry row and a concrete candidate governance diff
   in the reviewing PR for one joint decision. Candidate text is not active policy.
2. After explicit joint approval and human merge of the proposal, apply the reviewed
   AGENTS.md changes and one Git-workflow alignment on a documentation branch.
   Complete checks and open the implementation PR in the same authorized execution;
   no per-paragraph or per-checkpoint permission requests. Human review/merge remains.

## Files / Modules Touched (expected)

- Proposal: this Plan, `docs/specs/0080-feature-convergence.md`, and its registry row
  in `docs/specs/README.md`.
- Implementation: `AGENTS.md` (workflow stages 2/3/5/6, execution section, targeted
  testing, history clarification and escalation); `docs/process/git-workflow.md`
  (link the narrow nonsemantic adjustment rule). Lifecycle metadata/status only in
  the new pair/registry if necessary.
- No other governance files, engine code, tests, build settings, accepted Specs/ADRs,
  performance policy/selector/store, golden or temporary measurement tooling.

## Sequencing & Dependencies

One joint approval of R1–R6 authorizes the entire two-file implementation through
verification and PR. Until then, the existing rules still apply. In-flight explicit
restrictions are preserved per R6; no authorization for #307 measurements is requested.

## Verification Checklist

- [ ] R1/R2: a feature can proceed after one joint gate; an ordinary fixture repair
      can be corrected and retested without new design or invented milestones.
- [ ] R3: a harmless file/sequence adjustment uses one PR approval; an API, build
      behavior or acceptance change still returns to design review.
- [ ] R4: focused checks, final applicable checks and reuse provenance are explicit;
      docs-only follow-ups do not rerun unrelated engine tests.
- [ ] R5/R6: #307 remains blocked under its specific holds; no prior failure or
      closed budget is erased, and no new diagnostic requirement is invented.
- [ ] Links/whitespace valid; diff limited to the declared stage; no conflicting
      unconditional deviation-stop or semantic-history language remains.
- [ ] Engine build, GPU/image/Vulkan and timing: N/A, documentation-only change.

## Rollback Plan

Revert the governance implementation through human review; preserve its approval
and execution history. No data, binary or benchmark migration is involved.

## Definition of Done

Apply [Definition of Done](../process/definition-of-done.md) for each stage. Proposal
completion is not policy activation. Implementation must match the reviewed diff,
pass documentary checks and be merged by a human. No additional approval rounds
are requested for ordinary corrections within this Plan.
