# Plan: Temporary Observer Environment Evidence and Identity Boundaries

- **Spec:** [Spec 0078](../specs/0078-observer-environment-identity.md) (Approved)
- **Status:** Approved
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** slmao jointly approved Spec 0078 and this Plan, J1–J4 as recommended,
  2026-11-01, retaining existing ADRs; [owner ruling and distinct S2 authorization](https://github.com/slmao/Atlantis/pull/318#issuecomment-6560323466).
  The human merge prerequisite is satisfied by `bb76829`. S2 is authorized under a new
  independent 45-minute offline budget, including verification/reporting. No live collection,
  performance measurement or S3 authority; #307 stays Draft and all old evidence is retained.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Make the temporary #307 observer retain evidence before deciding environment equality,
and separate machine identity from process-local capacity/affinity. Complete only the
proposal in this round; preserve the stopped Q1 and all performance obligations.

## Milestones / Task Breakdown

1. **S1 — Evidence review and joint submission (this round).** Create this pair and one
   registry entry on an independent branch from latest main. Read the frozen Q1 error,
   start/end/identity, environment source/reference, registration and sealed packet;
   distinguish observed facts from the unrecorded field difference. Submit J1–J4 below,
   link the review from #307 and leave it Draft. Stop without implementing, collecting
   live state or measuring, even if a ruling arrives during this round.
2. **S2 — Offline repair, only after joint approval, human merge and distinct authority.**
   Record concise approval metadata, preserve the original archive hashes and copy
   candidates into a new ignored directory. Within R5's proposed 45-minute cap, change
   only the R1–R4 helpers listed below: selected-source/read-status capture; pure closed
   machine projection/comparison; immutable snapshot/difference publication before a
   decision; shared controller/sampler use; final error/CPU bookkeeping. Keep identity,
   counter, output/archive and mandatory process-enforcement checks intact. Add and run
   injected offline cases, retaining every attempted result. Submit scripts, hashes,
   coverage, limits and updated real-input reuse inventory in #307. Retain failed
   offline iterations; corrections within this file scope/cap may use new output
   directories, never erase an earlier result. An unresolved failure blocks closure;
   unexpected scope or exhausted budget stops work. No live probe or weakened check.
   Offline completion is a hold point, not authority for S3.
3. **S3 — Register, then optionally execute under new explicit live authority.** Recheck
   main/PR inputs and applicable correctness evidence outside measurement. Decide and
   disclose actual binary-source reuse versus a matching fresh build per R5; freeze
   the complete manifest and report all changed inputs. Use the R5 phase caps, publish
   actual immutable hashes and the fixed plan in #307 before quiet/Q1. If the owner
   requires approval of that manifest, wait; elapsed time is not approval. No separate
   live helper/idle/benchmark preflight. Only the authorized Q1-to-F1 protocol may run;
   first anomaly cancels later launches, with no repair/retry or budget transfer.
4. **S4 — Evidence, acceptance review and stop.** Close any actual execution with all
   outcomes, retained source/inputs, snapshots/differences, association completeness,
   CPU/IO accounting/gaps, cleanup and budget usage. Preserve the prior M5 fail and
   blocked acceptance independently of the observer result. Update #307's current
   checklist, remaining holds and evidence links; no ready claim while anything is
   unresolved, no automatic merge and no further acquisition.

## Files / Modules Touched (expected)

- Proposal: `docs/specs/0078-observer-environment-identity.md`, this Plan, and one
  row in `docs/specs/README.md`. Later approval metadata is limited to these new
  documents/row; discussion and results remain in the PRs.
- S2 disposable candidates, in a **new** ignored `build/0078-observer-offline-<unique>/`:
  `environment.ps1`, `observer_json.ps1`, `run-formal.ps1`, `observe-gate.ps1`,
  `test-adapters.ps1`; one small `environment_identity.ps1` pure helper and one
  `test-environment.ps1` injected fixture runner if separation is needed. Candidate
  configuration, fixtures, logs, snapshots, budget and hash manifest are evidence only.
- S3, only with new authority: copied/frozen candidates plus manifests, launch
  registration and raw result packets under a new ignored
  `build/pr307-formal-0078-<unique>/`; PR #307's current report/checklist.
- Existing association helper, archive/power assemblies and input-freeze helper may
  be copied unchanged and hashed. Their native/collection semantics are not repaired
  in this supplement; a required change to them is a reported scope deviation.

No tracked helper subsystem, native source/test, SDK, CMake/CI, performance policy/
store/applicability, golden, AGENTS.md, approved 0070–0073 body or Accepted ADR edit.
Old evidence directories/ZIPs are read-only inputs; never reuse their output paths.

## Sequencing & Dependencies

The [stopped execution](https://github.com/slmao/Atlantis/pull/307#issuecomment-6557483792)
and [frozen registration](https://github.com/slmao/Atlantis/pull/307#issuecomment-6557384320)
are the evidence basis, not unused authorization. S1 ends at submission. S2 requires
joint Spec+Plan approval, human merge and distinct offline authority. S3 additionally
requires successful S2 and new bounded live authority plus completed registration;
neither joint approval alone nor an old 150-minute budget permits it. PR #307 need
not merge for this proposal or offline work; implementation evidence remains there.
Windows 10 scope follows approved 0073; Windows 11 remains unverified.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Evidence and uncertainty** | Approve R1's persist-before-decision snapshots/errors and preserve the unrecorded Q1 delta as unknown; no reconstructed historical state or proved-root-cause claim. |
| **J2 — Identity and enforcement** | Approve R2/R3: stable keyed hardware projection from existing CIM fields, process-local count diagnostic only, independent mandatory affinity/priority/identity checks, existing observation cadence and explicit cost gaps. No environment or performance-policy change. |
| **J3 — Offline scope** | Approve R4 and S2's narrow disposable-file scope, proposed 45-minute cap, no-live guards and isolated fixtures. Offline success proves no live readiness. Human merge and distinct implementation authorization remain required. |
| **J4 — Execution and architecture** | Approve R5/R6's proposed fresh 150-minute, Q1-to-F1 two-parent ceiling/caps/registration/stops, retained failures and Windows 10 scope. No ADR change. Live execution still requires separate explicit authority after offline success; all prior budgets stay closed. |

## Verification Checklist

**This proposal:**

- [ ] Compare Q1 error/stop, empty raw outputs and frozen scripts to the problem statement;
      keep the specific observed delta unknown and distinguish the documented API behavior.
- [ ] Check relative links, `git diff --check`, exactly three tracked documentation files,
      old evidence ZIP hashes and unchanged #307 source/Draft status.
- [ ] Submit J1–J4 for joint review; claim neither approval nor implementation.

**Only after the respective authorization:**

- [ ] **R1/R4:** Frozen reference and observed values/error facts are published before each
      decision, with immutable sequence names/hashes, UTC/monotonic time and caller identity.
      Inject partial reads, malformed JSON and storage failures; each stops, with no ready result.
- [ ] **R2/R4:** Same hardware with supplied available-count 8 versus 1 compares equal as
      hardware; wrong mask/priority still fails. Test startup-cached count independently of
      current mask, property/device reordering, exact integral widths, duplicate keys,
      null-versus-missing, string-versus-number, invalid counts/unknown fields and real changes
      to every compared field class. Inactive GPU nulls remain explicit and constrained.
- [ ] **R3/R4:** Both script callers use the same pure rules; injected reads verify existing
      Q1 cadence and F1 periodic-call prohibition. All exits attempt accounting/error records;
      missing data is a reported gap. Verify full creation-time JSON round trips and preserve
      existing path/PID/output/counter/archive/lifetime regression coverage. Two fixture
      invocations have distinct directories; first-run hashes remain unchanged. Live calls
      are guarded failures, not executed tests or real process-enforcement claims.
- [ ] **R5:** Offline budget/result and explicit live authority precede a new registration;
      input reuse/matching build is proved, never relabeled. Record all caps, real identities,
      complete raw versions and actual exits; normal conformant/comparable fail follows the
      fixed order, all other anomalies stop. No retry, ABBA, new canary or extra idle probe.
- [ ] **R6:** Old packets and protected files remain unchanged, whole-PR formal obligation and
      old failures remain visible, missing Windows 11 evidence is not claimed, and #307 stays
      Draft if any verification or failure disposition is unresolved. No policy/baseline waiver.

## Rollback Plan

Withdraw an unapproved proposal without editing earlier requirements. An authorized
candidate may be abandoned while its failed evidence is retained; no stored result is
migrated or deleted, and no engine or machine setting needs rollback. A later scope or
comparison change requires further review rather than modifying a frozen running batch.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). No repository-wide deltas.
S1 completes at a reviewable docs-only submission and #307 cross-link; approval remains
pending. S2/S3/S4 completion and formal M5 acceptance are separate later decisions.
