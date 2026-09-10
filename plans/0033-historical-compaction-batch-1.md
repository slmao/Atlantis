# Plan: Historical Spec/Plan Compaction — Strategy and Batch 1

- **Spec:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending; once approved: reviewer, date, and PR naming
  this Plan and Spec 0033 together and explicitly authorizing the Batch 1
  compaction.

Authoring/lifecycle rules:
[AGENTS.md](../AGENTS.md#documentation-and-code-comments).
This is the second Plan under Spec 0033. The first
([Plan 0033](0033-documentation-lifecycle-and-compaction.md), merged in
[PR #144](https://github.com/slmao/Atlantis/pull/144)) delivered prospective
guidance, the templates, the registry, and the Spec 0004 / Plan 0004 pilot.
Spec 0033's [Proposed Design](../specs/0033-documentation-lifecycle-and-compaction.md#proposed-design)
stage 4 permits later Plans to compact explicitly named batches of completed
Specs and Plans once the pilot is accepted.

## Objective

Implement the first increment of Spec 0033 stage 4:

1. Record the batching order for every remaining completed Spec/Plan pair so
   later Plans inherit a fixed, reviewed sequence rather than re-deciding scope.
2. Apply Spec 0033's compaction method to **Batch 1 only** — the Spec/Plan pairs
   for 0001, 0002, and 0003 — in one reviewable PR.

No engine code, build, test, ADR, registry row semantics, or other numbered
document changes. Later batches are out of scope for this Plan and each gets its
own reviewed Plan.

## Compaction method (fixed by Spec 0033; applied per file)

Each target file keeps its identity and every heading; only misplaced or
duplicated prose is removed. For each file:

- **Preserve:** title, ID, current approval status, document role, historical
  implementation scope, and every Markdown heading (inbound links may target
  heading fragments). Preserve every still-effective requirement, non-goal,
  design decision, alternative, risk, and verification obligation.
- **Summarise + link, do not restate:** ADR decisions, repository-wide
  governance and workflow gates, and "what an approval authorizes" prose become
  one short sentence with a link to
  [AGENTS.md](../AGENTS.md#documentation-and-code-comments), the applicable ADR,
  or [git-workflow.md](../docs/process/git-workflow.md).
- **Remove (evidence lives in Git/PRs):** review-round narration, approval
  transcripts, implementation status/logs, test counts, hardware/driver
  details, and post-merge correction narratives. Where a correction still
  affects the current contract, keep a one-line reference to its PR or
  superseding ADR instead of the narrative.
- **Consolidate:** duplicated requirement/checklist renderings collapse into the
  single normative section, cross-referenced by the Spec's own requirement IDs
  (as [Plan 0004](0004-context-efficiency-guidelines.md) now does). A
  requirement that exists **only** in review prose is promoted into the correct
  normative section *before* that prose is removed.
- **Do not** rewrite an unmet or pending historical status as a completed
  event, mark old checkboxes passed, or change any status value except through
  the registry's own evidence-backed reconciliation (already done in PR #144).
- **Add** one `Editorial revision:` metadata line to each compacted file
  pointing to Spec 0033 and this Plan's implementation PR, matching the pilot.

## Batching order (proposed; fixed once this Plan is approved)

Remaining completed pairs, grouped by dependency cluster. `0004` is the merged
pilot; `0030` is deferred with no Plan file and is excluded until its
implementation closes; `0033` is the governing Spec and is excluded. ADRs are
excluded entirely per Spec 0033.

| Batch | Spec/Plan pairs | Theme | Files |
|---|---|---|---:|
| **1 (this Plan)** | 0001, 0002, 0003 | Project / platform / RHI + Vulkan windowed foundation | 6 |
| 2 | 0005, 0006, 0007 | RenderGraph, frame execution, minimal renderer | 6 |
| 3 | 0008, 0009, 0010, 0011 | Shader system, architecture alignment, headless, image regression | 8 |
| 4 | 0012, 0013, 0014, 0015 | Asset system, runtime host, world/scene, serialization | 8 |
| 5 | 0016, 0017, 0018, 0019, 0020 | Texture/sampler, mesh UV, material binding, lighting, mesh normals | 10 |
| 6 | 0021, 0022, 0023 | Descriptor pool capacity, dynamic frame uniforms, PBR direct lighting | 6 |
| 7 | 0024, 0025, 0026, 0027 | HDR pipeline, IBL, visible sky, directional shadow | 8 |
| 8 | 0028, 0029, 0031, 0032 | Showcase scene, tangent-space normal mapping, exposure, scene selection | 8 |

Each later batch is a separate Plan named `0033-historical-compaction-batch-N`
and a separate PR, each carrying its own joint Human Review, obligation
comparison, and metrics. This Plan approves the *order*, not the later edits. A
future Plan may bundle several remaining batches into one review if Batch 1
shows the method is low-risk; that is the reviewer's decision at that time, not
a licence granted here.

## Milestones / Task Breakdown

Ordered; steps 4–9 are the reviewable content passes over Batch 1.

1. Re-read Spec 0033's current status and confirm it remains `Approved`.
2. Confirm this Plan is `Approved` with a dated joint Human Review naming Spec
   0033 and this Plan and authorizing the Batch 1 compaction.
3. Branch from updated `main`; record the base commit in the implementation PR.
   Capture base line / word / fenced-code-line counts for all six Batch 1 files
   with a single documented counting method.
4. For each Batch 1 file, read it against its base version and start the
   obligation-to-retained-text checklist in the PR: enumerate every requirement,
   non-goal, decision, alternative, risk, and verification item before removing
   anything.
5. Compact the three Specs (0001, 0002, 0003) per the method above. One commit.
6. Compact the three Plans (0001, 0002, 0003) per the method above. One commit.
7. Add the `Editorial revision:` line to all six files (folded into the commits
   in 5–6). Open the implementation PR as a draft and replace the placeholder
   with its real URL in a follow-up commit, matching the pilot.
8. Verify: `git diff --check`; every repository-local link and heading fragment
   in the six files resolves; every incoming link to those six files from
   tracked Markdown still resolves (headings preserved); changed-path set is
   exactly the six files.
9. Record base/final metrics in the PR and complete the obligation checklist:
   each original obligation cites a retained section or an authoritative link;
   exclusions and any evidence gaps are stated explicitly. Mark the PR ready.

## Files / Modules Touched (expected)

Exhaustive:

| File | Permitted change |
|---|---|
| `specs/0001-project-foundation.md` | Editorial compaction; headings and effective obligations retained |
| `specs/0002-platform-foundation.md` | Editorial compaction |
| `specs/0003-rhi-vulkan-windowed-foundation.md` | Editorial compaction |
| `plans/0001-project-foundation.md` | Editorial compaction; ordered work and verification retained |
| `plans/0002-platform-foundation.md` | Editorial compaction |
| `plans/0003-rhi-vulkan-windowed-foundation.md` | Editorial compaction (largest reduction expected; candidate implementation fences move to PR history) |

No new files. `specs/README.md` is **not** changed: its rows for 0001–0003
already read `Approved` / `Merged` and carry no stale narrative to reconcile. A
need to touch anything else stops implementation and returns here for a Plan
revision.

## Sequencing & Dependencies

The merged pilot (PR #144) is the prerequisite and is accepted. This Plan's
joint Human Review must explicitly accept Spec 0033 and this Plan together and
authorize the Batch 1 edits; approval of Spec 0033 alone does not authorize
them. Batches 2–8 do not start until their own Plans are approved; they have no
ordering constraint among themselves beyond the numbered sequence above.

This work has no dependency or ordering relationship with any engine Spec/Plan
and touches no engine module.

## Verification Checklist

Maps to Spec 0033's
[Testing & Verification Plan](../specs/0033-documentation-lifecycle-and-compaction.md#testing--verification-plan).
Boxes stay unmarked: compaction is not a re-run of the original 0001–0003
verification.

- [ ] **Authority/lifecycle:** each removed passage's information has a retained
  authoritative home (Spec, ADR, PR, AGENTS.md, or process doc); no new journal
  or second source of authority is introduced.
- [ ] **Headings/links:** every heading in the six files is preserved; all
  repository-local targets and heading fragments in the six files resolve;
  every incoming tracked-Markdown link to the six files still resolves.
  Pre-existing out-of-scope broken links are reported, not fixed here.
- [ ] **Obligation preservation:** every original requirement, non-goal,
  decision, alternative, risk, and verification item maps to retained text or
  an authoritative link in the PR checklist. Review narration is never the sole
  surviving source of a requirement. No historical approval or passed check is
  invented.
- [ ] **Status integrity:** approval-status values and historical pending/unmet
  statements are unchanged; no old checkbox is marked passed.
- [ ] **Metrics:** base and final line, word, and fenced-code-line counts for
  all six files are recorded in the PR with the counting method; reduction is
  descriptive, not an acceptance threshold.
- [ ] **Scope/whitespace:** base-to-head changed paths are exactly the six
  files; `git diff --check` passes; no ADR, registry, other Spec/Plan, code,
  build, test, shader, asset, generated output, or untracked file is changed.
- [ ] **Editorial reference:** each of the six files carries one
  `Editorial revision:` line pointing to Spec 0033 and this Plan's PR.
- [ ] **Human review:** the reviewer explicitly accepts the obligation checklist
  and any disclosed evidence gaps before merge.

Builds, unit tests, headless/image regression tests, and Vulkan validation are
not applicable to this documentation-only change. Inspection uses existing
shell/Git tooling; no checker, report directory, or dependency is added.

## Rollback Plan

Before merge: revise or close the implementation PR. After merge: revert its six
file changes through a reviewed PR, restoring the recorded base content without
rewriting Git history. If a later batch Plan has already built on the batching
order recorded here, adjust that Plan in the same revert rather than silently
invalidating it. A semantic mismatch discovered mid-implementation stops the
work and returns here for a Plan revision; the Spec is not edited to match.

## Definition of Done

Apply the [repository Definition of Done](../docs/process/definition-of-done.md)
with Spec 0033's documentation-only exclusions. Additionally:

- The Batch 1 obligation-to-retained-text comparison and before/after metrics
  are in the implementation PR.
- A human explicitly accepts Batch 1's semantic preservation before merge.
- The batching order above is treated as approved once this Plan merges; later
  batches remain subject to their own reviewed Plans.
