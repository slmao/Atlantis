# Plan: Context-Efficient Documentation and Code Comment Guidelines

- **Spec:** [Spec 0004](../specs/0004-context-efficiency-guidelines.md)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction.
- **Plan approval:** 2026-08-07, [PR #10](https://github.com/slmao/Atlantis/pull/10).
- **Joint Human Review:** [PR #11](https://github.com/slmao/Atlantis/pull/11)
  records approval of Spec 0004 and Plan 0004 together on 2026-08-07, authorizing
  the AGENTS.md-only implementation. This is subsequent evidence for the gate
  that the original Plan-approval note left pending.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #144](https://github.com/slmao/Atlantis/pull/144), original scope and
  obligations retained.

## Objective

Implement Spec 0004 through one compact, additive AGENTS.md section covering
documentation and comment content. Preserve all existing governance and required
contracts. This Plan records the original implementation, not the later 0033
editorial work.

## 1. Plan-stage decision: where the new section goes in `AGENTS.md`

Insert `## Documentation and code comments` immediately after
`## Repository map` and before `## Definition of Done`. Reorder, rename, or
reword no existing section.

Documentation guidance is broader than C++, so it needs a standalone section.
Placing it after the repository map connects document locations to their
authority without interrupting Architecture principles and Module boundaries.
Its proximity to Definition of Done reinforces review applicability. The clear
heading and mandatory full AGENTS.md read make it discoverable without moving
existing content.

## 2. Milestones / Task Breakdown

1. Re-read Spec 0004's current status immediately before implementation and
   confirm it remains Approved.
2. Re-read this Plan's status and confirm Approved / Ready for Implementation.
3. Confirm dated, explicit joint Spec+Plan Human Review naming both documents;
   neither individual approval nor a drafting request substitutes for it.
4. Insert the single section at Section 1's location, covering Section 3.
5. Leave every line outside the insertion byte-for-byte unchanged.
6. Cross-check every Spec requirement and acceptance criterion against the
   inserted section; retain traceability through Section 7.
7. Check specifically for forbidden numeric length/comment-density metrics.
8. Confirm explicit preservation of public-API and Vulkan/WSI/lifetime/workaround
   contracts, not merely absence of contradiction.
9. Use `git status --short` and `git diff --stat` to confirm AGENTS.md-only scope.
10. Run `git diff --check` and resolve every introduced relative Markdown link.
11. Submit through the [PR workflow](../docs/process/git-workflow.md);
    a human reviews and merges, with no direct main commit or push.

## 3. Rule semantics the new section must cover

Semantics derive from the [Spec's core rule content](../specs/0004-context-efficiency-guidelines.md#core-rule-content-semantics-fixed-by-this-spec-exact-wording-is-a).
Implementation chooses concise wording in AGENTS.md's principle-stated style.

### Documentation

Implement D1–D4: one authoritative home, short summaries/links elsewhere,
index-focused navigation, proportional reading with required material retained,
and full preservation of required document content. No numerical size threshold.
Reference existing AGENTS.md contract sections rather than re-deriving them.

### Code comments

Implement C1–C6: non-obvious rationale/contracts, clearer code before explanatory
comments, full public-API documentation, sufficient technical explanation with
Spec/ADR links, same-change stale-comment maintenance, and correctness before
brevity. Section compactness remains a human qualitative judgment.

## 4. Explicitly prohibited in this implementation

- New tools, linters, formatters, Doxygen configuration, dependencies, or CI work.
- Automatic/batch document or comment rewriting; historical Spec/Plan/ADR
  deletion; changes to status/immutability conventions.
- Numeric file/function/comment length, token, or density requirements.
- New modules, APIs, threading, ownership, or backend decisions.
- Any removal, rewording, or weakening of an existing AGENTS.md rule.

## 5. Files / Modules Touched (expected)

**AGENTS.md only:** one section at the fixed location; every other line remains
unchanged. README, blueprint, registry, other Specs/Plans/ADRs, architecture/
process documents, source, tests, builds, and examples are excluded.

There is no optional file-scope branch. A need for another file requires stopping
and returning to Spec review for a revision/follow-up; disclosing it as a Plan
deviation does not authorize proceeding.

## 6. Sequencing & Dependencies

Steps 1–3 are separate, freshly verified gates. Steps 4–5 form one atomic edit;
6–8 are ordered content-review passes, 9–10 are scope/mechanical verification,
and 11 follows all checks. This governance change has no dependency or ordering
relationship with other engine Specs/Plans. Scope remains fixed by Spec 0004.

## 7. Verification Checklist

References A1–A10 identify the Spec's original acceptance obligations.
The checkboxes remain unmarked; compaction is not a new execution certification.

- [ ] **V1:** One section exists at Section 1's fixed location (A2; step 4).
- [ ] **V2:** Single-source-plus-link is explicit (A3; D1–D2).
- [ ] **V3:** Navigation uses summaries and links (Spec Goals; D2).
- [ ] **V4:** Proportional reading explicitly retains necessary material (D3).
- [ ] **V5:** Necessary comments differ from code narration (A4; C1–C2).
- [ ] **V6:** Thread-safety, ownership/lifetime/borrowing, preconditions/error
  semantics, and other required public-API contracts are explicit (A5; F2).
- [ ] **V7:** Vulkan synchronization, WSI, resource lifetime, and workaround
  documentation remain required (C4; step 8).
- [ ] **V8:** No mechanical line/word/token/comment-density metric (A7; step 7).
- [ ] **V9:** No dependency/tool/CI/build change (Spec Non-Goals).
- [ ] **V10:** Only AGENTS.md changes, checked through Git (A6; step 9).
- [ ] **V11:** `git diff --check` passes (A8; step 10).
- [ ] **V12:** All introduced Markdown links resolve (A9; step 10).
- [ ] **V13 — Human Review:** The section is compact without restating the
  Spec's reasoning (A2; not mechanically self-certifiable).
- [ ] **V14 — Human Review:** No existing governance rule is weakened, removed,
  or contradicted (A10; not mechanically self-certifiable).
- [ ] **V15:** Unit/headless/image/GPU validation is N/A because implementation
  changes no source, tests, or build files.

A1's individual approvals and the distinct joint approval are checked by
steps 1–3 before editing, independently of V13–V14.

## 8. Rollback Plan

Before merge, revise/revert the AGENTS.md insertion within the PR or close it.
If its semantics diverge from the approved Spec, stop and return to Spec/Plan
review rather than rewriting the Spec to match implementation. After merge,
propose a reviewed revert PR for the original change; do not reset, force-push,
or rewrite Git history.

## Definition of Done

Use the [repository checklist](../docs/process/definition-of-done.md).
No unit tests, headless verification, image regression, or Vulkan validation
apply to this original documentation-only implementation. V13 and V14 are
additional mandatory human judgments; all other applicable checklist items
remain required.
