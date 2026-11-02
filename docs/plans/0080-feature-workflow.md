# Plan: Continuous Feature Authorization and Impact-Based Verification

- **Spec:** [Spec 0080](../specs/0080-feature-workflow.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's direction
- **Joint Human Review:** Pending one review of this Plan and Spec 0080; no rule change,
  feature implementation or performance execution is authorized by submission.

## Objective

Replace repeated implementation/verification planning gates with continuous bounded authority,
impact-based evidence and concise escalation, preserving architectural review and human merge.

## Milestones / Files

1. **S1 — Proposal only (this round).** Add this pair and one `docs/specs/README.md` row
   on an independent main-based branch. Put #307's retrospective, proposal counts and
   evidence in the reviewing PR; submit J1–J4, cross-link #307 and stop. Do not change
   operating rules, candidate scripts, #319 status, feature code or measurement inputs.
2. **S2 — One process implementation after joint authorization/human prerequisites.**
   Change only the existing files below, with no feature work:

   | File | Approved delta |
   |---|---|
   | `AGENTS.md` | R1/R2 continuous scope and decision boundary; preserve architecture, platform and human-merge safeguards. |
   | `docs/process/git-workflow.md` | One joint authorization through ready PR, light adjustments and PR-local owner rulings. |
   | `docs/process/testing-strategy.md` | R3 impact table, validity/invalidation and scoped reruns; preserve GPU/image requirements. |
   | `docs/process/definition-of-done.md` | Valid evidence reuse, explicit blocker/failure disposition and optional-diagnostic distinction. |
   | `docs/process/performance-contract.md` | R4 minimal evidence/registration and diagnosis limits; no change to native policy/baseline/protocol. |
   | `docs/plans/template.md` | Goal/contracts, expected files, impact checks, exceptional-work limit and explicit end-to-end joint authorization. |
   | `.github/PULL_REQUEST_TEMPLATE.md` | Link the one continuous authorization and one compact impact/result/blocker table in existing sections. |

   Update only 0080 lifecycle metadata/registry as needed. Do not rewrite historical
   approved Specs/ADRs or create another process subsystem. Stage boundaries, commits
   and relevant fixes remain under this one authorization, not new planning PRs.
3. **S3 — Consistency review and reviewable process PR.** Walk the Spec's cases across
   all changed documents, check links/scope/whitespace and cite preserved gates. Open
   the docs implementation PR ready for human review; do not merge it. If J1 grants
   S2–S3 through ready PR, request no new stage approval.
4. **S4 — Prospective adoption, only after rules are human-merged.** If the owner has
   explicitly granted J4, record the R5 transition in #307 once, keep history/#319
   discussion, and continue within its named finite envelope without a separate
   offline/live planning review. If not granted, report the one adoption decision
   needed; do not infer authority. This proposal round performs none of S2–S4.

## Sequencing & Dependencies

Current AGENTS.md governs this proposal. New rules become operative only after human
review and merge of their implementation. Design decisions retain their own Accepted
ADRs. S4 neither depends on #307 merge nor reopens an old budget. The owner may grant
S2–S3 and conditional S4 execution together at the one joint checkpoint; feature/native
work remains separate from the process-only implementation PR.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Continuous authority** | Approve R1 and explicitly authorize S2–S3 through a reviewable process PR after human prerequisites, without further stage gates. Human merge remains reserved. |
| **J2 — Escalation boundary** | Approve R2: ordinary/light repairs need no new Spec; local coverage/resources need one explicit owner ruling; material architecture/API/policy changes still need joint design review/ADR. |
| **J3 — Evidence and diagnosis** | Approve R3/R4: required impact checks and proved evidence reuse, full failure history, minimal diagnostics with the cumulative60min default, no gate/baseline waiver. |
| **J4 — #307 adoption** | Explicitly retire supplemental detailed-observer and temporary-child-file capture prerequisites under R5; after process rules land, authorize one120min native-only/single-formal-gate envelope. Keep all native checks, old evidence/failures and Draft holds; no current execution/merge. |

## Verification Checklist

- [ ] **S1/R1/R2:** PR evidence supports the retrospective/counts. Table-top helper
      repair versus public-API change: one continues, one requires joint design review.
- [ ] **R3/R4:** Docs-only reuse versus changed flags/assets; failed/missing gate versus
      optional diagnostic failure. No invented pass, unlimited budget or weaker selector.
- [ ] **R5:** J4 alone can retire added collector/temporary-file prerequisites; old
      packets/failures and native contract remain. Process-only S1–S3 execute no feature,
      collector or measurement. Proposal approval is not assumed execution authority.
- [ ] All relative links, docs-only diff, `git diff --check`, current #307 Draft/source
      unchanged and existing evidence intact; new rules not claimed active prematurely.

## Rollback / Definition of Done

Withdraw this unapproved proposal without altering current rules. A later reversal uses
an explicit reviewed process change; execution history is never deleted. S1 is done at
one reviewable proposal plus cross-link; S3 ends at human review, not merge. Apply the
[Definition of Done](../process/definition-of-done.md), with code/GPU/performance runs
N/A for the process-only PR rather than pretending those obligations disappeared for #307.
