# Spec: Documentation Lifecycle and Historical Compaction

- **Status:** Approved
- **Author:** slmao (drafted by Codex at explicit human direction)
- **Created:** 2026-09-10
- **Approval:** slmao, [PR #142](https://github.com/slmao/Atlantis/pull/142),
  merged 2026-09-10; maintainer confirmed continuation to Plan drafting.
- **Related Plan(s):** [Plan 0033](../plans/0033-documentation-lifecycle-and-compaction.md)
- **Related ADR(s):** None — see Architectural Impact.

## Summary

Atlantis will make each document type carry only its own authoritative
information: Specs define *what*, Plans define implementation sequencing and
verification, ADRs record *why*, pull requests retain approval and execution
evidence, and registries provide compact navigation. The templates and process
guidance will be tightened first, `specs/README.md` will then be reduced to a
real index, and one completed Spec/Plan pair will be compacted as a reviewed
pilot before any broader historical cleanup.

This extends [Spec 0004](0004-context-efficiency-guidelines.md)'s
single-authoritative-source principle. It does not authorize rewriting Accepted
ADRs, changing an approved technical decision, or editing documentation for
work whose implementation is still in flight.

## Motivation / Problem Statement

The repository's numbered Spec, Plan, and ADR files currently total about
92,700 lines and 704,000 words. The median Spec is 849 lines, the median Plan
1,279 lines, and the median ADR 261 lines. The problem is not the small base
templates; content accumulated outside each document's intended role:

- Plans contain about 7,400 lines of fenced code, including complete candidate
  implementations and fixed diffs that belong in the implementation review.
- Specs and Plans repeatedly restate ADR decisions, repository-wide governance,
  approval gates, and the same requirements in design, review-decision, and
  acceptance sections.
- Approval transcripts, review rounds, implementation status, test counts,
  hardware details, and post-merge corrections are appended to design
  documents even when the same evidence already exists in a PR.
- `specs/README.md`, nominally a status index, is about 143,000 characters and
  18,600 words; individual table rows contain full implementation and review
  narratives.
- The duplicated status has already drifted. Several Specs still claim no Plan
  exists after the matching Plan was approved, and some Plan headers describe
  implementation PRs as open after the registry records them as merged.

The result increases review cost, hides current decisions among superseded
prose, and makes readers reconcile multiple copies of the same status. It also
contradicts [AGENTS.md](../../AGENTS.md)'s existing rule that navigation documents
stay focused on index, status, dependency, and roadmap information.

## Goals

- Give design, process, approval, implementation, and verification information
  one clear authoritative home.
- Prevent new Specs, Plans, ADRs, and registry entries from accumulating review
  transcripts, implementation logs, or duplicated governance prose.
- Make `specs/README.md` a compact, usable registry without losing links to the
  underlying Spec, Plan, ADR, or implementation PR.
- Prove a safe method for compacting completed Specs and Plans while preserving
  every still-effective requirement, decision, risk, and verification
  obligation.
- Preserve Git and PR history as the audit trail instead of reproducing it in
  every design document.
- Reduce the amount of historical material an agent or human must read to find
  the current contract.

## Non-Goals

- No mechanical line, word, token, section, or code-block limit. Complexity may
  justify length; duplication and misplaced content do not.
- No deletion of a numbered Spec, Plan, or ADR.
- No in-place rewrite, compaction, or relocation of an Accepted ADR. A changed
  architectural decision still requires a new ADR that supersedes the old one.
- No change to any engine public API, module boundary, dependency, threading
  model, ownership model, backend contract, source code, build, or test.
- No semantic change to an approved Spec or Plan under the label of editorial
  cleanup.
- No compaction of a Spec or Plan while implementation against it is open,
  pending merge, paused for correction, or otherwise in flight.
- No automatic summarization or unattended batch rewrite of historical files.
- No new documentation generator, database, linter, third-party dependency, or
  CI service.
- No attempt to erase the history of corrections. The PR/commit trail remains
  intact and the compact document links to any correction that still affects
  the current contract.

## Requirements

### Functional

#### Authority and lifecycle

- The repository guidance must assign information as follows:

  | Information | Authoritative home |
  |---|---|
  | Problem, scope, requirements, non-goals | Approved Spec |
  | Architectural decision and rationale | Accepted ADR |
  | Implementation order, expected files, verification mapping | Approved Plan |
  | Review discussion and approval evidence | The reviewing PR |
  | Implementation deviations, commands, results, hardware, test counts | Implementation PR |
  | As-built architecture | `docs/architecture/` and current source |
  | Current navigation and links | Compact registry/index |

- A document may summarize another authority in one short statement and link
  to it. It must not reproduce the other authority's full rationale, checklist,
  approval transcript, or implementation history.
- Approval metadata in a Spec, Plan, or ADR must be compact: current status and,
  once applicable, a reviewer/PR reference. Repository-wide explanations of
  what an approval authorizes remain in AGENTS.md and process documentation.
- The distinct joint Spec+Plan Human Review gate remains mandatory. Its evidence
  is recorded once by a concise reference to the review that explicitly names
  both documents; its rules are not restated throughout either document.
- Implementation progress and verification evidence belong in the
  implementation PR. A registry may state only a coarse current result and link
  the PR; Specs and Plans do not become implementation journals.

#### Prospective document shape

- The Spec template must retain problem, goals/non-goals, requirements,
  proposed design, architectural impact, alternatives, verification, risks, and
  future work. It must discourage duplicate acceptance criteria or a second
  "decisions for review" rendering of the same requirements.
- The Plan template must retain objective, ordered tasks, expected files/modules,
  dependencies, verification mapping, rollback, and Plan-specific Definition of
  Done deltas. It must prohibit complete source files, implementation-ready
  function bodies, large candidate diffs, and exhaustive tables that are more
  appropriately tests or implementation artifacts. Small pseudocode or data
  layout fragments remain allowed when necessary to remove genuine ambiguity.
- The ADR template must remain focused on one decision: context, decision,
  consequences, and alternatives. Acceptance needs only concise metadata. An
  Accepted ADR is not amended in place; a changed decision gets a superseding
  ADR under the existing ADR lifecycle.
- Generic repository requirements are linked, not copied. A document repeats a
  repository-wide rule only when the feature adds a narrower, feature-specific
  constraint.
- Corrections found before approval are folded into the Draft/In Review text.
  After approval and implementation start, a changed requirement or plan uses a
  reviewed follow-up Spec/Plan; a changed architectural decision uses a new
  superseding ADR.

#### Registry

- `specs/README.md` must contain a compact row per numbered Spec with only the
  fields needed for navigation: ID/title, current Spec status, Plan link/status,
  coarse implementation state plus PR link, and concise dependencies/notes.
- Registry rows must not contain design rationale, milestone details, test
  counts, hardware/driver data, correction narratives, or approval transcripts.
- Historical priority changes must not accumulate as a chronological prose log.
  The registry shows the current candidate order; a still-relevant reason links
  to its authoritative decision or issue.
- The registry remains non-authoritative for approval. Status values summarize
  the linked document and must be corrected rather than explained by an
  appended registry-history paragraph.

#### Historical compaction

- Historical work proceeds through multiple Plans rather than one repository-
  wide rewrite. The first implementation Plan covers prospective guidance,
  template/index maintenance rules, registry compaction, and one pilot only.
- The pilot pair is Spec 0004 and Plan 0004. Both are completed governance work,
  directly exercise the rule being introduced, and are small enough for a human
  to compare in full.
- Pilot compaction may remove duplicated approval-gate prose, review-process
  narration, repeated requirements/checklists, and post-implementation status.
  It must preserve Spec 0004's current governance semantics and Plan 0004's
  implementation sequence and verification obligations.
- The pilot PR must include a semantic preservation checklist and before/after
  structural metrics. Human approval of that PR is required before any later
  Plan proposes a wider completed-Spec/Plan batch.
- Later historical Plans must name every document they touch, operate only on
  completed work, and use reviewable batches. They may not infer authorization
  to compact Accepted ADRs.
- A compacted approved Spec or Plan must carry one concise editorial-history
  reference to this Spec and the compaction PR so the change is explicit rather
  than a silent rewrite.

### Non-functional

- **Traceability:** every surviving requirement and decision remains traceable
  to its authoritative document; every implementation remains traceable to its
  PR.
- **Reviewability:** each compaction PR is small enough for a human to compare
  the removed text against retained semantics without trusting an automated
  summary.
- **Link stability:** headings referenced externally are preserved or incoming
  links are updated in the same PR. No known repository-local link is left
  broken.
- **Portability/performance/memory:** not applicable; this is documentation and
  process work only.

## Proposed Design

The change is intentionally staged:

1. **Stop new growth.** Update the repository guidance, Spec/Plan/ADR templates,
   and their README lifecycle descriptions. Adjust the PR template or process
   wording where it currently encourages circular or duplicated approval
   checklists.
2. **Restore the registry.** Reduce `specs/README.md` to current navigation and
   move no design or verification content elsewhere merely to preserve it; the
   existing Git/PR history already retains that evidence.
3. **Run one pilot.** Compact Spec 0004 and Plan 0004, then perform a full human
   semantic comparison. Do not touch Accepted ADRs.
4. **Decide whether to continue.** Only after the pilot is accepted may later
   Plans propose named batches of completed Specs and Plans. In-flight documents
   are excluded until their implementation closes.

The first Plan will determine exact file edits and sequencing within stages
1–3. This Spec may therefore have multiple Plans: one for the policy/registry/
pilot slice and later Plans for explicitly named historical batches.

No archive directory or generated status database is introduced. Git remains
the historical record; Markdown remains the current design and navigation
surface.

## Architectural Impact

**None.** This changes documentation governance and historical-document editing
practice only. It introduces no engine subsystem, public API, dependency,
threading, ownership, memory, platform, or backend decision, so no ADR is
required.

The work is nevertheless significant because [AGENTS.md](../../AGENTS.md)
classifies changes to process/governance documentation as significant. This
Spec must be approved and merged before a Plan is drafted; the later joint
Spec+Plan Human Review must be recorded before templates, guidance, registry
content, or historical documents are changed.

## Alternatives Considered

- **Immediately batch-shorten every document:** rejected because it is not
  human-reviewable and could silently remove active contracts.
- **Set hard length limits:** rejected because document complexity varies and
  Spec 0004 explicitly rejects mechanical size metrics.
- **Move all old documents into an archive directory:** rejected because it
  breaks links, changes repository structure, and does not remove duplication
  inside each file.
- **Generate a new summary database while retaining all existing status prose:**
  rejected because it creates another source that can drift.
- **Compact Accepted ADRs editorially:** rejected by the existing permanent-
  record rule. Future ADRs should be shorter, but accepted history remains
  intact unless a new ADR supersedes a changed decision.
- **Keep implementation evidence in Plans for convenience:** rejected because
  the PR already owns the diff, review, commands, results, and hardware context;
  copying it makes the Plan stale and harder to read.

## Testing & Verification Plan

- Run `git diff --check` and validate all repository-local Markdown links in
  every documentation PR.
- Record line/word/code-fence counts before and after as review information, not
  as pass/fail limits.
- For the registry, verify that every numbered Spec still has a discoverable
  row and that every retained Plan/ADR/PR link resolves.
- For the pilot, map each Spec 0004 requirement and each Plan 0004 task/
  verification item to retained text or an authoritative linked source.
- Search the repository for links to headings removed or renamed by the pilot;
  preserve the heading or update every incoming link in the same PR.
- Confirm the diff contains no source, build, test, asset, shader, or Accepted
  ADR modification.
- Require human review of the pilot's semantic-preservation checklist before a
  later historical compaction Plan is drafted.

## Risks & Open Questions

- A review transcript may contain the only expression of a real requirement.
  The pilot must promote that requirement into the correct normative section
  before deleting the transcript, not simply remove it.
- Git retains removed prose but is less discoverable than the current file.
  The concise editorial-history reference mitigates this without copying the
  old content.
- Registry compaction may expose disagreement among a Spec, Plan, registry, and
  PR. The Plan must define a read-only reconciliation procedure and escalate a
  genuinely ambiguous state rather than guess.
- Some large code/data fragments may be normative formats rather than candidate
  implementation. The Plan must distinguish public binary/layout contracts
  from source code that belongs in implementation.
- The exact later batching strategy is deliberately left open until the pilot
  provides evidence about review cost and semantic risk.

## Out of Scope / Future Work

- Broad compaction of completed Specs and Plans beyond the Spec 0004/Plan 0004
  pilot; later reviewed Plans may propose it.
- Any modification to an Accepted ADR.
- Tooling that automatically derives registry state from GitHub or document
  metadata.
- Reorganization of `docs/architecture/`, `docs/process/`, or source-code
  comments beyond the minimal guidance links needed by the first Plan.
