# Plan: Context-Efficient Documentation and Code Comment Guidelines

- **Spec:** [specs/0004-context-efficiency-guidelines.md](../specs/0004-context-efficiency-guidelines.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by the human reviewer on 2026-08-07. This is
  **Plan Approval only** — see the note immediately below for exactly
  what it does and does not authorize.

> **Plan Approval recorded 2026-08-07.** The human reviewer
> independently reviewed and approved this Plan, confirming:
> (1) Implementation file scope is `AGENTS.md` only, with no optional
> or conditional branch for README, Blueprint, Spec Registry, or any
> other file (Section 5); (2) the new `AGENTS.md` section's placement —
> immediately after `## Repository map` and immediately before
> `## Definition of Done` — is confirmed (Section 1); (3) the rule
> semantics in Section 3 are confirmed as strictly derived from
> Approved Spec 0004, introducing no additional scope; (4) the
> Verification Checklist in Section 7 correctly maps to Spec 0004's
> Acceptance Criteria; (5) the "compact" and "does not weaken existing
> governance" checklist items are confirmed as Human-Review-only
> qualitative judgment calls, not mechanically verifiable conditions.
>
> **This is Plan Approval, not the joint Human Review checkpoint.**
> Spec 0004 was separately `Approved` (merged via PR #9, 2026-08-06).
> This Plan's approval above is independent of that Spec Approval —
> neither approval implies or substitutes for the other. The distinct,
> joint Spec 0004 + Plan 0004 Human Review checkpoint
> [AGENTS.md](../AGENTS.md)'s workflow stages require before
> implementation **has not occurred and is not recorded by this
> approval**. This approval does **not** authorize any edit to
> `AGENTS.md`, and does **not** authorize starting Implementation.
> Implementation may begin only once that joint checkpoint is
> separately and explicitly recorded.

## Objective

Turn `specs/0004-context-efficiency-guidelines.md` (`Approved`) into a
single, narrowly-scoped implementation: one new, compact `AGENTS.md`
section establishing (1) a context-efficient documentation convention
(single-authoritative-source-plus-link, navigation documents stay
index-focused, task-proportionate reading) and (2) a necessary-comment
convention for code (explain non-obvious rationale/constraints/
contracts, don't restate code), with every existing `AGENTS.md` rule —
including every existing thread-safety, ownership, lifetime, borrowing,
precondition, error-semantics, and architectural-governance
requirement — left fully intact.

This plan's scope is fixed entirely by the Approved Spec: it does not
widen the spec's rules, its file scope, or its governance meaning in
any direction. Where this plan makes a Plan-stage decision the spec
left open (see Section 1 below), that decision is a concretization of
something the spec already authorized, not a new scope.

**This plan does not authorize its own execution.** Per
[AGENTS.md](../AGENTS.md)'s workflow stages, Implementation may not
begin until: this plan independently reaches
`Approved / Ready for Implementation`, **and** the distinct joint
Spec + Plan Human Review checkpoint is explicitly recorded — a separate
checkpoint from either document's individual approval, not implied by
either alone. As of this Plan's Approval (2026-08-07): Spec 0004 is
`Approved` (merged via PR #9); this plan is now
`Approved / Ready for Implementation`; the joint Spec + Plan Human
Review checkpoint has not occurred; `AGENTS.md` remains unmodified.

## 1. Plan-stage decision: where the new section goes in `AGENTS.md`

The spec's Proposed Design left the exact placement open, naming two
candidates (adjacent to **C++ coding conventions**, or a standalone
section after **Architecture principles**) and deferring the final
choice to this plan. Having read `AGENTS.md`'s full current section
list —

```
The Golden Rule
What counts as "significant"
The workflow, stage by stage
Phase 1 constraints (do not silently expand these)
Architecture principles
Module boundaries
C++ coding conventions
Error handling
Ownership and lifetime rules
Threading rules
Vulkan-specific rules
Testing requirements
Git workflow
Repository map
Definition of Done
When you're unsure
```

— this plan fixes the placement as a **new, standalone `##` section
titled `Documentation and code comments`, inserted immediately after
`## Repository map` and immediately before `## Definition of Done`.**
No existing section is reordered, renamed, or reworded to accommodate
it.

**Rationale:**

- **Not buried inside C++ coding conventions.** Half of this section's
  content (documentation concision) is not C++-specific at all — it
  governs specs, plans, ADRs, and READMEs. Nesting it under, or
  immediately merging it into, C++ coding conventions would misrepresent
  its scope and match exactly what the task driving this plan warns
  against.
- **Does not duplicate or crowd Architecture principles.** Architecture
  principles (and the Module boundaries section that immediately
  follows it) form one tight, already-coherent block about engine
  module/backend/threading architecture. Inserting an unrelated
  documentation/comment-style section between or next to them would
  interrupt that pairing and read as thematically mismatched.
- **Sits with its closest existing thematic neighbor.** `Repository
  map` is the one existing section that is *already* about "which
  directory is the authoritative home for which kind of document"
  (`specs/` for proposals, `plans/` for approved implementation plans,
  `adr/` for decisions, `docs/architecture` for as-built design,
  `docs/process` for process). The new section's single-authoritative-
  source principle is a direct, natural continuation of that map, not
  an unrelated insertion.
- **Sits immediately before Definition of Done.** An agent already
  working through `Definition of Done` before opening a PR passes
  directly by this section, reinforcing that its rules are also part of
  what a complete, mergeable change must respect — without editing
  Definition of Done itself.
- **No reordering required.** The new section is added as a pure
  insertion between two existing sections; nothing else in `AGENTS.md`
  moves, and no existing section's meaning changes because of where the
  new one landed.
- **Discoverability:** `AGENTS.md` opens by instructing any agent to
  read the whole file each session before touching anything, so
  discoverability here is a function of a clear, self-explanatory
  heading rather than position — `Documentation and code comments` is
  unambiguous and searchable on its own.

## 2. Milestones / Task Breakdown

1. **Confirm Spec 0004 is still `Approved`.** Re-read
   `specs/0004-context-efficiency-guidelines.md`'s Status field
   immediately before any implementation step — not relying on memory
   of an earlier read, in case it was revised or superseded since this
   plan was drafted.
2. **Confirm this Plan has reached `Approved / Ready for
   Implementation`.** Re-read this plan's own Status field in its
   final, approved form.
3. **Confirm the distinct joint Spec + Plan Human Review checkpoint has
   been explicitly recorded** — a dated note naming both documents
   together, not inferred from Spec approval, Plan approval, or a chat
   request. Do not proceed past this step without it, per this task's
   explicit instruction and [AGENTS.md](../AGENTS.md)'s workflow
   stages.
4. **Insert exactly one new section into `AGENTS.md`** — `##
   Documentation and code comments` — at the location fixed in
   Section 1 above, covering the content requirements in Section 3
   below.
5. **Do not reorder, rename, or reword any other `AGENTS.md`
   section.** Every existing line outside the single insertion point
   remains byte-for-byte unchanged.
6. **Cross-check the drafted section against Spec 0004's Requirements
   and Acceptance Criteria, line by line** — confirm every Functional
   Requirement and every Acceptance Criterion in the spec has a
   corresponding, traceable statement in the new section (see
   Verification Checklist).
7. **Check for mechanical metrics.** Re-read the drafted section
   specifically hunting for any numeric line/word/token/comment-density
   threshold — none is permitted (Spec Non-Goals; see Section 4 below).
8. **Check that required public-API and technical contracts are not
   weakened.** Confirm the drafted section explicitly preserves — not
   merely fails to contradict — thread-safety, ownership/lifetime/
   borrowing, precondition/error-semantics documentation requirements
   already stated elsewhere in `AGENTS.md` (Error handling, Ownership
   and lifetime rules, Threading rules), and that Vulkan synchronization,
   WSI boundary behavior, resource lifetime, and non-obvious-workaround
   comment requirements remain intact per the Vulkan-specific rules
   section.
9. **Check that the diff touches only `AGENTS.md`.** Run `git status
   --short` and `git diff --stat`; confirm no other file appears.
10. **Run Markdown/link/whitespace validation**: `git diff --check` on
    the modified `AGENTS.md`; confirm every Markdown link the new
    section introduces (if any) resolves to an existing repository
    file.
11. **Submit via PR.** Open a PR targeting `main` per
    [docs/process/git-workflow.md](../docs/process/git-workflow.md);
    never push directly to `main` and never commit directly to `main`.
    The agent opens the PR; a human reviews and merges.

## 3. Rule semantics the new section must cover

*(Fixed by the Approved Spec's Proposed Design; this plan converts them
into an implementation checklist rather than pre-drafting the section's
final prose — the exact wording is an implementation-time decision, made
against `AGENTS.md`'s existing terse, principle-stated style, and
reviewed against this checklist per Milestone 6 above.)*

### Documentation

- Preserve the information required for each document's stated role.
- Keep one authoritative source for a given decision, rule, or status
  (an Accepted ADR for *why*, an Approved Spec for *what*, an Approved
  Plan for *how/sequencing*, `AGENTS.md` for repository-wide governance
  rules).
- Other documents use concise summaries and links instead of
  duplicating detailed content from the authoritative source.
- Navigation/index-style documents (README, blueprint, spec/plan
  registries, and similar) focus on indexes, status, dependencies, and
  roadmap information — not full design rationale or argumentation.
- Agents read documentation proportionate to the task at hand, rather
  than defaulting to loading the entire historical documentation set
  regardless of task need.
- Concision never authorizes removing requirements, constraints,
  design rationale, risks, verification steps, or governance status
  that a document's own role or template requires it to state.
- No mechanical line, word, token, or size limit is introduced.

### Code comments

- Comments explain non-obvious rationale, invariants, constraints,
  ownership, lifetime, borrowing, thread safety, protocol requirements,
  platform behavior, workaround reasons, or easy-to-misuse behavior.
- Comments do not restate code that is already clear from naming,
  structure, and types.
- Prefer clear names, precise types, and small well-named functions
  before reaching for an explanatory comment.
- Required public API contracts (thread-safety, ownership/lifetime,
  precondition/error semantics — already required elsewhere in
  `AGENTS.md`) remain fully documented; this convention narrows *how*
  such documentation is written, never *whether* it exists.
- Complex algorithms, Vulkan synchronization, WSI boundary behavior,
  resource lifetime, and non-obvious workarounds retain sufficient
  explanation, including a link to the backing spec/ADR where one
  exists.
- A comment that no longer matches the code it describes is updated or
  removed in the same change that invalidated it.
- Concision must never override correctness or completeness of a
  documented contract.

The new section must be compact — short paragraphs or a tight bullet
list per convention, referencing `AGENTS.md`'s own existing sections
(e.g. Error handling, Ownership and lifetime rules, Threading rules,
Vulkan-specific rules) by name for contract requirements rather than
re-deriving their content — but "compact" is judged qualitatively at
Human Review (see Verification Checklist), not by any numeric measure.

## 4. Explicitly prohibited in this implementation

Per the Approved Spec's Non-Goals, this plan prohibits implementation
from introducing any of the following, regardless of convenience:

- A new lint tool, formatter, Doxygen configuration, or any other
  tooling.
- A new third-party dependency.
- Any CI change.
- Any automatic or batch rewrite of existing documentation or code
  comments.
- Deletion of any historical Spec, Plan, or ADR, or any change to their
  status/immutability conventions.
- A fixed file-length, line-count, word-count, token-count, or
  comment-density metric of any kind.
- A new module, public API, threading model, ownership model, or
  backend decision.
- Any rewrite, reword, or weakening of an existing `AGENTS.md` rule.

## 5. Files / Modules Touched (expected)

- **`AGENTS.md`** — the only file this plan expects to modify: one new,
  compact section inserted per Section 1 above; no existing content
  removed or reworded.

**This is the complete and exhaustive file scope. There is no
conditional or optional branch.** Approved Spec 0004 does not authorize
modifying `README.md`, `docs/project-blueprint.md`,
`specs/README.md`, `docs/README.md`, any existing spec, plan, or ADR,
or any source/test/build/example file, and this plan does not invent
such authorization. If implementation determines that achieving the
spec's goals genuinely requires touching a file outside `AGENTS.md`,
that is **not** a deviation to note in a PR description and proceed
with — it is a scope change that exceeds what Spec 0004 approved.
Implementation must stop immediately and return to Spec review; a
wider file scope requires its own spec revision or follow-up spec, per
[AGENTS.md](../AGENTS.md)'s Implementation-stage rule that a deviation
changing scope means going back to step 1 of the workflow, not
resolving it inside implementation.

## 6. Sequencing & Dependencies

- This plan has two independent gating dependencies, both required
  before Milestone 4 (the actual `AGENTS.md` edit) may begin: (a) this
  plan itself reaching `Approved / Ready for Implementation`, and
  (b) the joint Spec + Plan Human Review checkpoint being explicitly
  recorded. Neither alone is sufficient; both are required, per
  [AGENTS.md](../AGENTS.md)'s workflow stages treating Human Review as
  a distinct checkpoint from either document's individual approval.
- Milestones 1–3 are gating checks, re-verified at implementation time
  regardless of what was true when this plan was drafted or approved.
- Milestones 4–5 are a single atomic editing step (the insertion and
  the constraint that nothing else changes).
- Milestones 6–8 are sequential review passes over the drafted text,
  each depending on Milestone 4 being complete.
- Milestones 9–10 are mechanical verification, depending on 4–8.
- Milestone 11 is final and depends on all of the above.
- This plan has no dependency on, and no ordering relationship with,
  any other in-flight spec or plan (e.g. Spec 0003's not-yet-written
  plan) — it is an independent, repository-wide governance change.

## 7. Verification Checklist

Maps to `specs/0004-context-efficiency-guidelines.md`'s Acceptance
Criteria and Requirements. Items marked **(Human Review)** are
qualitative judgment calls that a human reviewer must make — they are
not mechanically self-verifiable, and this plan does not pretend
otherwise.

- [ ] **New section exists at the fixed location** — `##
      Documentation and code comments`, immediately after `##
      Repository map` and immediately before `## Definition of Done`,
      matching Section 1 of this plan. *(Milestone 4)*
- [ ] **Single-authoritative-source-plus-link principle** is explicitly
      present in the documentation half of the new section. *(Spec
      Acceptance Criterion: single-authoritative-source-plus-link
      principle; Section 3 above)*
- [ ] **Navigation documents directed to use summaries and links**,
      not full detail reproduction, is explicitly present. *(Spec
      Goals; Section 3 above)*
- [ ] **Task-proportionate reading principle** is explicitly present,
      and explicitly does not authorize skipping documents a task
      genuinely requires. *(Spec Goals; Section 3 above)*
- [ ] **Necessary-comment distinction** ("explains non-obvious
      rationale/constraints/contracts" vs. "restates what the code
      already says") is explicitly present in the code-comment half.
      *(Spec Acceptance Criterion 4; Section 3 above)*
- [ ] **Required-contract carve-out is complete**: thread-safety,
      ownership/lifetime/borrowing, precondition/error-semantics
      documentation requirements already stated in `AGENTS.md`'s Error
      handling, Ownership and lifetime rules, and Threading rules
      sections are explicitly preserved, not merely un-contradicted.
      *(Spec Acceptance Criterion 5; Milestone 8)*
- [ ] **Vulkan synchronization, WSI boundary behavior, resource
      lifetime, and non-obvious-workaround comment requirements**
      remain intact and are explicitly referenced as retained.
      *(Spec Proposed Design "Code comments"; Milestone 8)*
- [ ] **No mechanical line/word/token/comment-density metric** appears
      anywhere in the new section. *(Spec Acceptance Criterion 7;
      Milestone 7)*
- [ ] **No new dependency, tool, CI change, or build-system change** is
      introduced anywhere in the implementing PR. *(Spec Non-Goals;
      Section 4 above)*
- [ ] **No file other than `AGENTS.md`** is modified — verified via
      `git status --short` and `git diff --stat` before the PR is
      opened. *(Spec Acceptance Criterion 6; Section 5 above)*
- [ ] **`git diff --check` passes** on the modified `AGENTS.md`. (Spec
      Acceptance Criterion 8; Milestone 10)
- [ ] **Every Markdown link** the new section introduces (if any)
      resolves to an existing repository file. *(Spec Acceptance
      Criterion 9; Milestone 10)*
- [ ] **(Human Review)** The new section is judged **compact** —
      it does not restate Spec 0004's own reasoning or explanation in
      detail. *(Spec Acceptance Criterion 2 — qualitative; no numeric
      substitute is used)*
- [ ] **(Human Review)** The reviewing human explicitly confirms the
      new section does **not weaken, remove, or contradict** any
      existing `AGENTS.md` rule. *(Spec Acceptance Criterion 10 —
      qualitative, not self-certifiable by the implementing agent)*
- [ ] Unit tests / headless integration tests / image regression tests
      / Vulkan Validation Layers: **not applicable** — no source, test,
      or build file is touched by this implementation.

## 8. Rollback Plan

Implementation is scoped to a single file (`AGENTS.md`), so rollback is
correspondingly narrow:

- **Before the implementing PR merges:** revert the plan-scoped change
  to `AGENTS.md` within that PR (e.g. by editing the branch further or
  closing the PR without merging) — no other file is touched, so there
  is nothing else to unwind.
- **If implementation discovers the drafted rule's semantics diverge
  from what the Approved Spec actually authorized:** stop
  implementation and return to Plan/Spec review — do not adjust the
  Spec after the fact to match whatever got written, per
  [AGENTS.md](../AGENTS.md)'s explicit rule against modifying a spec to
  make implementation easier.
- **After merge, if a problem is found:** revert the single merged
  commit/PR through the normal PR-revert flow (a follow-up PR proposed
  and reviewed the same way as any other change) — no destructive Git
  operation (`reset --hard`, force-push, history rewrite) is used or
  proposed anywhere in this plan.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- "Vulkan Validation Layers run clean," "Image regression tests," and
  "Headless verification" items are not applicable — no rendering or
  source code is touched.
- "Unit tests added/updated for new logic" is not applicable — no
  logic is added.
- Add: both Human Review checklist items in Section 7 above (section
  compactness; no weakening of existing governance) are required,
  non-optional parts of this plan's Definition of Done, in addition to
  the standard checklist.
