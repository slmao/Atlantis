# Plan: Historical Spec/Plan Compaction — Batches 2–8

- **Spec:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending; once approved: reviewer, date, and PR naming
  this Plan and Spec 0033 together and authorizing the Batch 2–8 compaction.

Authoring/lifecycle rules:
[AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Third Plan under Spec 0033, after
[Plan 0033 Batch 1](0033-historical-compaction-batch-1.md)
([PR #146](https://github.com/slmao/Atlantis/pull/146)) which fixed the batching
order and the compaction method, and its implementation
([PR #147](https://github.com/slmao/Atlantis/pull/147), merged) which compacted
the 0001–0003 pairs.

## Objective

Carry the compaction method through the **remaining seven batches** of completed
Spec/Plan pairs (0005–0032, minus the merged 0004 pilot, the deferred 0030, and
the governing 0033). One reviewed Plan authorizes all seven; implementation is
**one PR per batch** — six to ten files each, each PR self-contained with its
own obligation-to-retained-text comparison, before/after metrics, and human
acceptance of semantic preservation before merge.

This is the "future Plan [that] may bundle several remaining batches into one
review" that
[Plan 0033 Batch 1](0033-historical-compaction-batch-1.md#batching-order-proposed-fixed-once-this-plan-is-approved)
anticipated, now that Batch 1 has shown the method is low-risk. It **amends**
that Plan's sequencing note that each later batch gets its own Plan: the
batching *order* it fixed is unchanged; only the per-batch Plan requirement is
replaced by this single Plan plus per-batch implementation PRs.

No engine code, build, test, ADR, registry-row semantics, or non-target
document changes.

## Compaction method

Unchanged from
[Plan 0033 Batch 1 § "Compaction method"](0033-historical-compaction-batch-1.md#compaction-method-fixed-by-spec-0033-applied-per-file),
applied per file. In brief: preserve every heading, the title/ID/`Status`/role/
historical scope, and every still-effective requirement, non-goal, decision,
alternative, risk, and verification obligation; summarise-and-link ADR
decisions, repository governance, and approval-gate prose; remove review/
revision narration, implementation status/logs, and duplicated requirement
renderings; promote a requirement that exists only in review prose before
removing it; change no status value and mark no old checkbox passed; add one
`Editorial revision:` line per file pointing to Spec 0033 and that batch's PR.

## Batches (order fixed by Plan 0033 Batch 1; unchanged)

`0004` is the merged pilot; `0030` is deferred with no Plan file (excluded
until its implementation closes); `0033` is the governing Spec. ADRs are
excluded entirely. Line counts are the current base, informational only.

| Batch | Spec/Plan pairs | Theme | Files | ~Base lines |
|---|---|---|---:|---:|
| 2 | 0005, 0006, 0007 | RenderGraph, frame execution, minimal renderer | 6 | 9,300 |
| 3 | 0008, 0009, 0010, 0011 | Shader system, architecture alignment, headless, image regression | 8 | 10,700 |
| 4 | 0012, 0013, 0014, 0015 | Asset system, runtime host, world/scene, serialization | 8 | 11,300 |
| 5 | 0016, 0017, 0018, 0019, 0020 | Texture/sampler, mesh UV, material binding, lighting, mesh normals | 10 | 17,500 |
| 6 | 0021, 0022, 0023 | Descriptor pool capacity, dynamic frame uniforms, PBR direct lighting | 6 | 5,500 |
| 7 | 0024, 0025, 0026, 0027 | HDR pipeline, IBL, visible sky, directional shadow | 8 | 4,700 |
| 8 | 0028, 0029, 0031, 0032 | Showcase scene, tangent-space normal mapping, exposure, scene selection | 8 | 7,400 |

Batches are compacted in numeric order (2 → 8). Each is one implementation PR
branched from the latest `main`; a batch does not start until the previous
batch's PR is merged, so each builds on a clean base and the registry/link
state stays consistent.

## Per-batch implementation steps

For each batch, in order:

1. Branch `docs/0033-historical-compaction-batch-N` from updated `main`; record
   the base commit in the PR. Capture base line / word / fenced-code-line
   counts for every file in the batch with the documented counting method
   (physical lines excluding trailing blanks; whitespace-split words; fenced
   lines inclusive of the delimiters).
2. Read each file against its base version; enumerate every requirement,
   non-goal, decision, alternative, risk, and verification item in the PR's
   obligation checklist before removing anything.
3. Compact the batch's Specs — one commit.
4. Compact the batch's Plans — one commit.
5. Add the `Editorial revision:` line to every file (folded into the commits in
   3–4). Open the PR as a draft; replace the placeholder with its real URL in a
   follow-up commit.
6. Verify: `git diff --check`; every repository-local link and heading fragment
   in the changed files resolves; every incoming link to those files from
   tracked Markdown still resolves (headings preserved); internal `Section N` /
   `step N` cross-references still resolve against retained numbering; the
   changed-path set is exactly the batch's files.
7. Record base/final metrics and complete the obligation checklist in the PR;
   each original obligation cites a retained section or an authoritative link;
   exclusions and evidence gaps are stated explicitly. Mark the PR ready.
8. A human accepts the batch's semantic preservation and merges before the next
   batch begins.

## Files / Modules Touched (expected)

Exhaustive: the Spec and Plan file for each numbered pair in the batch table
above — 54 files total across the seven PRs
(`specs/NNNN-*.md` and `plans/NNNN-*.md` for
NNNN ∈ {0005–0029, 0031, 0032} \ {0030}). No new files. `specs/README.md` is
**not** changed by any batch: its rows for these Specs already read `Approved` /
`Merged` and carry no stale narrative (the registry was reconciled in
[PR #144](https://github.com/slmao/Atlantis/pull/144)). Anything outside a
batch's own pair files stops that batch's implementation and returns here for a
Plan revision.

## Sequencing & Dependencies

The merged pilot (PR #144) and Batch 1 (PR #147) are the prerequisites and are
accepted. This Plan's joint Human Review must explicitly accept Spec 0033 and
this Plan together and authorize the Batch 2–8 edits; approval of Spec 0033
alone does not. Batches run strictly in numeric order, each on a fresh branch
from `main` after the previous batch merges. This work has no dependency or
ordering relationship with any engine Spec/Plan and touches no engine module.

## Verification Checklist

Per batch PR, mapping to Spec 0033's
[Testing & Verification Plan](../specs/0033-documentation-lifecycle-and-compaction.md#testing--verification-plan).
Boxes stay unmarked — compaction is not a re-run of the original verification.

- [ ] **Authority/lifecycle:** each removed passage's information has a retained
      authoritative home; no new journal or second source of authority.
- [ ] **Headings/links:** every heading in the batch's files is preserved or is
      an internal section heading with no inbound fragment link; all
      repository-local targets and heading fragments in the changed files
      resolve; every incoming tracked-Markdown link still resolves; internal
      `Section N` / `step N` references still resolve. Pre-existing
      out-of-scope broken links are reported, not fixed.
- [ ] **Obligation preservation:** every original requirement, non-goal,
      decision, alternative, risk, and verification item maps to retained text
      or an authoritative link in the PR checklist. Review narration is never
      the sole surviving source of a requirement. No historical approval or
      passed check is invented.
- [ ] **Status integrity:** approval-status values and historical pending/unmet
      statements are unchanged; no old checkbox is marked passed.
- [ ] **Metrics:** base and final line, word, and fenced-code-line counts for
      every file in the batch are recorded in the PR with the counting method;
      reduction is descriptive, not an acceptance threshold.
- [ ] **Scope/whitespace:** base-to-head changed paths are exactly the batch's
      pair files; `git diff --check` passes; no ADR, registry, other Spec/Plan,
      code, build, test, shader, asset, generated output, or untracked file is
      changed.
- [ ] **Editorial reference:** each file carries one `Editorial revision:` line
      pointing to Spec 0033 and that batch's PR.
- [ ] **Human review:** the reviewer explicitly accepts the obligation
      checklist and any disclosed evidence gaps before merging the batch.

Builds, unit tests, headless/image regression tests, and Vulkan validation are
not applicable to this documentation-only change. Inspection uses existing
shell/Git tooling; no checker, report directory, or dependency is added.

## Rollback Plan

Per batch: before merge, revise or close that batch's PR. After merge, revert
its file changes through a reviewed PR, restoring the recorded base content
without rewriting Git history. Batches are independent once merged — reverting
one does not require reverting the others. A semantic mismatch discovered
mid-batch stops that batch and returns here for a Plan revision; the Spec is
not edited to match.

## Definition of Done

Apply the [repository Definition of Done](../process/definition-of-done.md)
with Spec 0033's documentation-only exclusions. Per batch:

- The batch's obligation-to-retained-text comparison and before/after metrics
  are in its PR.
- A human explicitly accepts the batch's semantic preservation before merge.

The overall effort is done when all seven batch PRs have merged. At that point
every completed numbered Spec/Plan pair except the deferred 0030 has been
compacted under Spec 0033.
