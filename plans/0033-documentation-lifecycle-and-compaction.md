# Plan: Documentation Lifecycle and Historical Compaction

- **Spec:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md)
- **Status:** Approved / Implemented in [PR #144](https://github.com/slmao/Atlantis/pull/144); merged 2026-09-10
- **Author:** slmao (drafted by Codex at explicit human direction)
- **Joint Human Review:** Completed by slmao in the implementation review for
  [PR #144](https://github.com/slmao/Atlantis/pull/144), which accepted the
  Spec/Plan scope and the pilot's semantic-preservation evidence before merge.

## Objective

Implement Spec 0033's first slice: establish documentation lifecycle guidance,
update templates, restore the Spec registry to a compact index, and compact
the completed Spec 0004 / Plan 0004 pair as the sole historical pilot.

## Milestones / Task Breakdown

### M1 — Establish the baseline and evidence

- Start the implementation branch from updated `main` after joint Human Review
  is recorded and merged. Record that base commit in the implementation PR.
- Inventory numbered Specs, matching Plans, registry entries, candidate order,
  and links in the affected files. Record line, word, and fenced-code counts
  for the registry and pilot using consistent before/after counting rules.
- Reconfirm pilot completion through [PR #11](https://github.com/slmao/Atlantis/pull/11)
  and check for open implementation/correction work touching the pair. If new
  work is in flight, pause the pilot rather than substitute another pair.
- Read the pilot against its base-commit version and begin a preservation
  checklist in the implementation PR. Identify unique requirements embedded
  in approval prose before deciding what can be removed.
- Capture existing broken links separately from changes introduced by this
  work. Temporary inspection scripts/results stay outside tracked files; no
  permanent checker, report directory, or dependency is added.

### M2 — Establish prospective guidance and templates

- Update only the documentation portion of `AGENTS.md`'s `Documentation and
  code comments` section. Make it the canonical rule for information ownership
  and compact references, linking Spec 0033 for rationale. Preserve the code-
  comment rules and all other sections, including the workflow gates.
- Update `docs/process/git-workflow.md`'s PR and document-lifecycle sections to
  define approval references, PR-owned execution evidence, and the reviewed
  editorial treatment of completed Specs/Plans under Spec 0033. Preserve the
  distinction between editorial compaction and a technical decision change.
- Update the three templates in place, retaining their required sections.
  Add concise authoring guidance for their respective roles; permit small
  normative layout/pseudocode fragments where necessary, while directing full
  implementations, diffs, and test-case inventories to code and PR review.
- Add compact optional approval-reference fields. The Plan records the joint
  Spec+Plan review once, with reviewer/date/PR and the documents it covers.
  No field is prefilled as approved, and no review checklist approves itself.
- Update the three directory READMEs to link the canonical guidance and explain
  their document's lifecycle without copying its full rule set. ADR guidance
  directs changed decisions to superseding ADRs; existing numbered ADRs and
  their historical amendments remain untouched.
- Update `.github/PULL_REQUEST_TEMPLATE.md` to distinguish proposal review
  from implementation: allow stage-specific N/A explanations, retain the joint
  review gate for implementation, and provide space for verification evidence
  and pilot preservation results. Do not require a Spec/Plan to be pre-approved
  merely to open its own review PR.
- Validate links, required sections, scope, and whitespace before committing M2.

### M3 — Compact and reconcile the registry

- Retain `Spec Registry`, `A. Existing Specs`, `B. Candidate Spec Backlog`, and
  `Backlog maintenance rules` headings for link stability. Keep one row per
  numbered Spec with ID/title, Spec status, Plan link/status, coarse
  implementation state/PR, and concise dependencies/notes.
- Preserve access to each feature's ADRs through its Spec; retain a direct ADR
  link where it identifies a still-effective correction or limitation. Remove
  implementation narratives, hardware/test logs, review transcripts, and
  obsolete priority-change narration after checking their authoritative source.
- Preserve candidate order, scope, dependencies, and the future-phase limits.
  Preserve any still-current priority or constraint that exists only in prose
  as a short note with evidence, rather than silently treating it as obsolete.
- Reconcile states using the procedure below. Keep unresolved facts visible in
  the PR for human resolution before declaring this milestone verified.
- Verify Spec-ID coverage, retained local/PR links, candidate semantics, and
  whitespace before committing M3.

**State reconciliation procedure:** read the document's current metadata and
dated corrections, then inspect the linked approval/implementation PR's state,
merge commit, and review evidence. Approval status and implementation state are
separate facts: implementation PR merge does not independently approve a design.
For an unambiguous stale registry entry, update it from the applicable source.
If a document header conflicts with clear merged approval evidence, link that
evidence with a short qualifier; do not silently change another numbered
document. An absent Plan file, such as the closed/unmerged Plan 0030, is linked
through its PR with its actual disposition, not a nonexistent local path.
Missing, inaccessible, or conflicting evidence is recorded as unresolved and
escalated for human resolution, never converted to an inferred approval.

### M4 — Compact the completed 0004 pilot

- Edit only Spec 0004 and Plan 0004. Retain their titles, IDs, approval status,
  document roles, and historical implementation scope. Preserve all existing
  Markdown headings so inbound links outside the repository remain usable.
- Consolidate repeated requirements and approval-gate explanations into their
  appropriate normative sections and links. Replace review narration with
  concise, source-backed references; retain a unique requirement even when it
  originally appeared only in a review note.
- Preserve the Spec's necessary-comment and task-proportionate-reading rules,
  complete public-API contract protections, no-numeric-limit policy, goals,
  non-goals, alternatives, risks, and verification obligations.
- Preserve the Plan's original AGENTS-only file scope, exact section placement,
  additive-only constraint, ordered work, dependency gates, verification and
  rollback obligations, and human-only judgments. These describe the original
  0004 implementation; they do not become the scope of this 0033 change.
- Do not rewrite old unmet or pending approval statements as successful events
  without evidence. Keep dated qualifications or evidence gaps visible rather
  than retroactively manufacturing sign-off or marking old checkboxes passed.
- Add a concise editorial reference in each pilot file to Spec 0033 and the
  actual compaction PR. Open that PR as a draft after the pilot commit, then
  commit its real URL; no placeholder link remains at review handoff.
- Finish the PR's preservation checklist: for each original requirement, task,
  verification item, risk, alternative, or scope restriction, cite the retained
  section or authoritative link. Group duplicate clauses only when they carry
  the same obligation. Explain exclusions and any evidence gaps explicitly.
- Compare the final pilot against the M1 base; record before/after metrics in
  the PR. Mark the PR ready only after the final verification below passes.

## Files / Modules Touched (expected)

The implementation file list is exhaustive:

| Files | Permitted change |
|---|---|
| `AGENTS.md` | Documentation rules within the existing documentation/comment section |
| `docs/process/git-workflow.md` | PR evidence and document-lifecycle guidance |
| `.github/PULL_REQUEST_TEMPLATE.md` | Stage-aware review and verification evidence |
| `specs/template.md`, `plans/template.md`, `adr/template.md` | Role-specific authoring and approval metadata |
| `specs/README.md` | Lifecycle guidance, registry and backlog compaction/reconciliation |
| `plans/README.md`, `adr/README.md` | Concise lifecycle guidance and links |
| `specs/0004-context-efficiency-guidelines.md` | Pilot Spec editorial compaction |
| `plans/0004-context-efficiency-guidelines.md` | Pilot Plan editorial compaction |

This Plan-review PR itself adds this Plan and updates only Spec 0033's approval/
related-plan metadata and registry row. Those bookkeeping edits do not implement
M2–M4. Implementation adds no files, changes no engine modules, and does not
compact other numbered documents. Additional file needs return to Plan review.

## Sequencing & Dependencies

M1 → M2 → M3 → M4, with checks after each editing milestone and separate commits
for policy/templates, registry, and pilot. The merged Spec is the design
prerequisite. Joint review must explicitly accept Spec 0033 and this Plan
together and authorize implementation; approval/merge of a proposal alone is
not a substitute for that record.

One implementation PR carries these commits and the pilot comparison. A human
must accept the pilot's semantic preservation and merge the PR before another
Plan proposes a wider historical batch. That later Plan is outside this slice.

## Verification Checklist

Items map to Spec 0033's Requirements and Testing & Verification Plan.

- [ ] **Authority/lifecycle:** each information class has the designated home;
  review references retain evidence and all approval gates. No new journal or
  duplicated source of authority is introduced.
- [ ] **Prospective shape:** each template retains its required sections and
  the appropriate authoring constraints, with no mechanical size threshold.
- [ ] **Registry:** every numbered Spec appears exactly once; candidate order,
  remaining scope, dependencies, and current constraints are preserved; status
  discrepancies have evidence-backed resolution or explicit human disposition.
- [ ] **Pilot semantics:** every original obligation maps to retained text or
  an authoritative source. Review narration is not the only surviving source
  of a requirement. Historical approvals are never invented.
- [ ] **Links:** check repository-local file targets and heading fragments in
  changed documents, plus incoming links to those documents across tracked
  Markdown. Preserve existing headings and validate retained PR/ADR/Plan links.
  Resolve broken links in edited files within scope; report pre-existing
  out-of-scope defects rather than silently expanding the change.
- [ ] **Metrics:** record base/final line, word, and fenced-code counts for the
  registry and pilot in the PR, with counting method; reduction is descriptive.
- [ ] **Scope/whitespace:** base-to-head changed paths match the list above;
  `git diff --check` passes; no numbered ADR, other historical Spec/Plan, code,
  build, test, shader, asset, generated output, or untracked user file is changed.
- [ ] **Human review:** reviewer explicitly accepts the pilot preservation
  checklist and any disclosed evidence gaps before merge; later batches remain
  subject to their own reviewed Plans.

Builds, unit tests, headless/image regression tests, and Vulkan validation are
not applicable to this documentation-only change. Use existing shell/Git tools
for inspection; no test infrastructure is added.

## Rollback Plan

Before merge, revise or close the implementation PR. After merge, revert its
documentation changes through a reviewed PR; restore pilot/registry content
from the recorded base without rewriting Git history. Revert dependent pilot
and registry edits together with any policy they require. If later work has
already adopted the guidance, inspect those dependencies before proposing the
revert rather than silently invalidating subsequent approvals.

## Definition of Done

Apply the [repository Definition of Done](../docs/process/definition-of-done.md)
with the documentation-only exclusions above. The pilot comparison, working
references, verified registry, and human acceptance of semantic preservation
are the Plan-specific completion criteria. Execution evidence remains in the PR.
