# Spec: Feature Convergence and Proportional Verification

- **Status:** Approved
- **Author:** Codex, at owner direction
- **Created:** 2026-11-02
- **Related Plan(s):** [Plan 0080](../plans/0080-feature-convergence.md)
- **Approval:** slmao, 2026-11-02, [PR #320](https://github.com/slmao/Atlantis/pull/320).
- **Related ADR(s):** None — see Architectural Impact

## Summary

Put a shorter execution workflow in AGENTS.md: one joint design approval,
continuous implementation, ordinary repairs within that authorization, and
verification proportional to actual changes. Keep architectural review,
truthful evidence, existing mandatory gates and human merge.

## Motivation / Problem Statement

PR #307 completed its feature and live acceptance, but repeated preparation
failures led to separate authorization rounds and growing diagnostic tooling.
Repository defaults should distinguish a design decision from repairing a
fixture, and a feature acceptance requirement from optional investigation.
This proposal does not decide the cause or disposition of its performance fail.

## Goals

- Reduce approval round trips without letting agents choose architecture.
- Define when to continue, request one narrow decision, or return to design.
- Make the original acceptance checklist the convergence target.

## Non-Goals

No engine/API/build changes, performance selector or protocol change, baseline
refresh, test waiver, retroactive pass, or automatic merge. No new process tool.

## Requirements

- **R1 — One joint gate.** Spec and Plan may be drafted together and reviewed in
  one PR. The explicit joint sign-off approves both and authorizes implementation;
  separate sequential approval ceremonies are unnecessary. New architectural
  decisions still require Accepted ADRs before coding. Existing merge prerequisites
  remain. By default, authorization continues through implementation, verification
  and a review-ready PR. Milestones are reporting points unless the human specifies
  a stop, scope limit, budget or intermediate approval.
- **R2 — Repair without redesign.** Bugs, failed tests and disposable helper or
  fixture errors within approved scope may be fixed and retested without another
  Spec or permission request when contracts, acceptance semantics, dependencies,
  module boundaries and protected paths are unchanged. Preserve failures and the
  reason for rerunning. This does not authorize unlimited timing attempts or reset
  an explicit stop/budget; ordinary offline retests are not performance sampling.
- **R3 — Classify deviations once.** Report routine implementation choices in the
  implementation PR. A necessary file-list or sequencing adjustment with no contract,
  architecture, dependency, governance or acceptance change needs one explicit human
  approval in that PR, not another Spec/Plan pair. Link the approved adjustment
  without rewriting historical documents. Public API, architectural, dependency,
  build/CI behavior, data-format, governance or substantive acceptance changes still
  require a reviewed revision/follow-up and ADR disposition where applicable. Stop
  dependent work only; continue independent authorized work.
- **R4 — Relevant verification and reuse.** The Plan maps requirements to required
  checks and states completion criteria. Run focused build/tests after meaningful
  implementation changes; execute all remaining applicable gates for the final
  candidate. Reuse evidence only when its source, tests, dependencies, build inputs,
  assets and relevant environment are unchanged; record revision/configuration and
  applicability. Documentation-only edits need document checks, not an automatic
  full engine rebuild. Reassess affected checks after integration or input changes.
  Evidence with uncertain provenance is not reusable. No full-suite obligation in
  an approved Plan is silently reduced. Vulkan, golden and performance rules remain.
- **R5 — Bounded convergence.** Existing approved completion criteria are the target;
  agents must not add a new observer, proof obligation or diagnostic subsystem as
  an implicit prerequisite. A minimal helper needed for a required check may be
  developed and repaired under R2. Broader diagnosis is a separately scoped task.
  Classify product regressions, pre-existing/base failures, environment refusals
  and helper defects with evidence and explicit uncertainty. Report a blocker once
  with a concrete next decision; do not create repetitive status-only proposals.
  Independent work may proceed while the PR remains blocked. Preserve all outcomes;
  unresolved required gates still prevent declaring completion.
- **R6 — In-flight transition.** After this governance change is approved and merged,
  apply its defaults prospectively. For #307 and other existing PRs, specific human
  holds, stop conditions, budgets and explicit verification obligations remain until
  the owner revises them. The owner may consolidate execution-only holds in one PR
  instruction under R3; changing acceptance itself still needs design review.
  This proposal does not resume Q1/F1, waive an old failure or authorize a new run.

### Non-functional

Documentation only; no runtime performance, memory, portability or dependency impact.
AGENTS.md remains the authoritative rules source; other process pages link to it.

## Proposed Design

Revise the existing workflow and testing paragraphs, add one compact execution
section, and clarify the escalation paragraph. Align the Git workflow's historical
change rule with R3 by reference, so it does not demand a new Spec for nonsemantic
file/sequence adjustments. Keep review evidence and concrete candidate diffs in the
reviewing PR; approval is recorded once in the Plan.

## Architectural Impact

None. This changes repository governance, not engine architecture or any accepted
performance decision. ADR-0118/0119 and all other Accepted ADRs stay unchanged.
Performance path selection remains binding; a different applicability policy is
outside this proposal, not implicitly adopted as part of proportional verification.

## Alternatives Considered

- Remove design review: rejected; it would weaken architectural control.
- Require a supplement for every helper repair: retains the demonstrated delay.
- Automatically waive environment/base failures: rejected; diagnosis is not a pass.

## Testing & Verification Plan

Check links, whitespace and the exact docs-only scope. Walk R1–R6 against a normal
feature, a fixture repair, a nonsemantic file adjustment, an API change, a docs-only
follow-up and #307's explicit holds. Inspect the proposed edits for contradictory
workflow/testing/history language. No engine build, GPU or timing run is applicable.

## Risks & Open Questions

The main risk is treating a semantic change as routine. R3 names the protected
categories and requires escalation of the unresolved category, not automatic
waiver. Joint review must approve R1–R6 together and explicitly authorize the
two-file governance implementation.

## Out of Scope / Future Work

Reclassifying performance applicability, changing tolerances or baselines, removing
existing feature gates, retrospective document compaction, and resuming #307.
