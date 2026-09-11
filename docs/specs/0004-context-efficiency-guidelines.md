# Spec: Context-Efficient Documentation and Code Comment Guidelines

- **Status:** Approved
- **Author:** Drafted by Claude Code at human direction; the original author
  metadata left human authorship/ownership confirmation pending.
- **Created:** 2026-08-06
- **Approved:** 2026-08-06, [PR #9](https://github.com/slmao/Atlantis/pull/9)
- **Related Plan(s):** [Plan 0004](../plans/0004-context-efficiency-guidelines.md)
- **Related ADR(s):** None — see Architectural Impact.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #144](https://github.com/slmao/Atlantis/pull/144), original scope and
  obligations retained.

## Summary

Add one compact section to [AGENTS.md](../../AGENTS.md) establishing
single-authoritative-source documentation and necessary-comment conventions.
Reduce reading and maintenance cost while preserving required contracts and
the existing Spec → Plan → Human Review → Implementation → Verification →
PR → Merge workflow. This document records the original 0004 scope.

## Motivation / Problem Statement

The [documentation structure](../README.md) intentionally separates
governance, design, process, and navigation. At proposal time, naming and
formatting rules existed, but documentation duplication and comment content
had no explicit convention. Navigation could copy design rationale; duplicated
status could drift; indiscriminate historical reading consumed attention; and
comments restating clear code added maintenance cost.

A blanket demand for shorter prose could instead remove essential constraints,
rationale, risks, verification, or public-API contracts. The rule must guard
against that failure as explicitly as it discourages duplication.

## Goals

- Give each decision, scope, sequence, and governance rule one authoritative
  home; use concise summaries and links elsewhere.
- Keep navigation focused on index/status/dependencies/roadmap.
- Make reading proportional to the task without skipping required material or
  imposing a mechanical reading list.
- Make comments explain non-obvious rationale and contracts rather than clear
  code; preserve all mandatory technical documentation.
- Keep the AGENTS.md addition compact and principle-focused.

## Non-Goals

- Numeric line, word, token, size, or comment-density thresholds.
- Removing necessary rationale, alternatives, risks, constraints, or contracts.
- Automatic/batch rewriting of existing Markdown or comments, deleting
  historical Specs/Plans/ADRs, or changing their status/immutability conventions.
  Each later cleanup needs its own individually reviewable authorization.
- Requiring uncommented code or fewer files read regardless of task need.
- Changing C++ APIs, modules, dependencies, builds, workflow stages, document
  locations, status vocabularies, or authoritative document roles.
- New linting, formatting, CI checks, or third-party tools/dependencies.
  Enforcement remains human/agent judgment at review.

## Requirements

### Functional

- **F1:** Add exactly one compact AGENTS.md section covering documentation and
  comments. The change is additive: no existing rule is removed, weakened,
  reworded to a lesser obligation, or contradicted.
- **F2:** Explicitly preserve thread-safety, ownership/lifetime/borrowing,
  precondition/error semantics, and all other required public-API documentation,
  in full wherever the existing rules require it.
- **F3:** State the single-source-plus-link rule generally enough for README,
  blueprint, and future navigation documents, without prescribing their wording.
- **F4:** Keep comment-content guidance compatible with C++ conventions without
  duplicating naming, formatting, or include rules.
- **F5:** The guidance is prospective. Existing Specs/Plans/ADRs and architecture
  documents are outside this implementation's file scope.

### Non-functional

Runtime performance, portability, and memory are not applicable. The new section
must cost materially less reading effort than the duplication it addresses,
without a numeric limit or sacrificing required information.

## Proposed Design

### Where the new section lives

Add one standalone section near related existing content, without duplicating
it. The proposal considered placement near C++ coding conventions or after
Architecture principles; exact heading, placement, and wording were left to
Plan/Human Review. [Plan 0004](../plans/0004-context-efficiency-guidelines.md)
records the selected placement.

### Core rule content (semantics fixed by this spec; exact wording is a
Plan-stage/Human-Review-stage decision)

**Documentation concision:**

- **D1:** Retain the information required by each document's role. Accepted ADRs
  own architectural rationale, Approved Specs own scope, Approved Plans own
  sequencing/verification, and AGENTS.md owns governance/coding rules.
- **D2:** Other documents summarize and link. Navigation documents prioritize
  index/status/dependencies/roadmap rather than repeating detailed rationale.
- **D3:** Read a task-relevant subset rather than the entire historical set by
  default. Required task reading and AGENTS.md's full-read rule remain binding.
- **D4:** Never omit required requirements, constraints, rationale, risks,
  verification, or governance status to make a document shorter.

**Code comments:**

- **C1:** Explain non-obvious rationale, invariants, lifetime/ownership/borrowing,
  thread safety, platform/backend or protocol/format constraints, and behavior
  that is easy to misuse.
- **C2:** Prefer clear names, types, or a small extracted function over a comment
  compensating for unclear code. Avoid redundant banners, duplicated comments,
  narrated edit history, and TODOs without actionable issue/spec tracking.
  Edit history belongs in commits/PRs.
- **C3:** Fully document existing public-API contracts, including error semantics
  and preconditions. Concision changes how they are expressed, not whether they
  are required.
- **C4:** Preserve enough explanation for complex algorithms, Vulkan
  synchronization, resource lifetimes, WSI/platform code, and workarounds;
  include the backing Spec/ADR link where one exists.
- **C5:** Update or remove a stale comment in the same change that invalidates it.
- **C6:** Never trade correctness or contract completeness for brevity.

### Relationship to existing rules

This formalizes the [blueprint](../project-blueprint.md)'s existing
single-source pattern. It changes neither the as-built/process documentation
split nor any template. Existing thread-safety, ownership, error-handling, and
Vulkan rules retain their force; F2 and C3–C4 explicitly protect them.

## Architectural Impact

**None; no ADR required.** This is a governance/comment-content convention,
with no engine API, module, dependency, threading, ownership, or backend change.
Governance work still requires the full reviewed workflow in
[AGENTS.md](../../AGENTS.md#the-workflow-stage-by-stage). Drafting this Spec/Plan
does not itself edit AGENTS.md; implementation requires both approvals and the
distinct joint review. At Spec approval, Plan and joint review were pending;
the Plan links the subsequent review evidence.

If implementation needs to redefine document roles, reassign authority, or
expand beyond the compact AGENTS.md addition, stop and return to Spec review.

## Alternatives Considered

- **Numeric limits:** rejected; they encourage padding or removal of necessary
  content rather than judgment.
- **Worked examples/checklists in AGENTS.md:** rejected; they would duplicate
  this rationale. Illustrations, if separately approved, belong in a linked
  process document rather than the compact rule.
- **Batch cleanup in this Spec:** rejected; each document needs semantic review
  and such cleanup exceeds the original file scope.
- **Direct governance edits without Spec/Plan/Human Review:** rejected; the
  repository classifies these as significant regardless of apparent risk.
- **Fold into unrelated work, such as Plan 0003:** rejected; documentation
  governance is independent of that RHI/Vulkan scope.

## Testing & Verification Plan

Documentation-only verification; GPU, runtime, and build testing do not apply.

- **T1:** Run `git diff --check` and verify relative Markdown links for this
  Spec/Plan and the eventual AGENTS.md change.
- **T2:** Human review confirms F1–F5, no mechanical metric, full preservation of
  required contracts, and compactness relative to the problem.
- **T3:** Check that the new AGENTS.md section states the rule without restating
  this Spec's explanation.
- **T4:** Record individual Spec/Plan Human Review sign-offs before approval,
  through reviewed merges under [Git workflow](../process/git-workflow.md).
  Implementation additionally requires the distinct joint review.

## Risks & Open Questions

- Exact heading/placement/wording is a Plan-stage choice; it must be deliberate.
- Readers may misuse concision to remove required content or proportionality to
  skip necessary reading. Explicit carve-outs and reviewer judgment mitigate
  both; prose alone cannot guarantee correct application.
- A task-type-to-reading-list reference could help later, but is not decided
  or authorized here.

## Out of Scope / Future Work

The AGENTS.md edit belongs to the separately approved implementation. Retroactive
document cleanup, task-specific reading guidance, and lint/CI enforcement need
separate review; tooling or CI changes also trigger the normal significant-work
process. No other file enters Plan 0004's implementation scope.

## Acceptance Criteria

Original acceptance obligations remain unmarked; this editorial revision does
not certify historical execution:

- [ ] **A1:** Individual Spec/Plan approvals have explicit review evidence (T4).
- [ ] **A2:** The addition is one compact section without duplicated rationale (F1, T3).
- [ ] **A3:** Single-authoritative-source-plus-link is explicit (D1–D2).
- [ ] **A4:** Necessary comments are distinguished from code narration (C1–C2).
- [ ] **A5:** Required public-API contracts remain complete (F2, C3–C4).
- [ ] **A6:** Original implementation touches AGENTS.md only (F5; Plan file scope).
- [ ] **A7:** No mechanical length/comment-density metric is introduced.
- [ ] **A8:** Whitespace checks pass on every touched file (T1).
- [ ] **A9:** Every introduced link resolves (T1).
- [ ] **A10:** Human review confirms no existing governance is weakened (F1, T2).
