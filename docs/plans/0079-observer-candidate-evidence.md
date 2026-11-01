# Plan: Temporary Observer Candidate Evidence Before Identity Assertions

- **Spec:** [Spec 0079](../specs/0079-observer-candidate-evidence.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** Pending joint review of Spec 0079 and this Plan;
  no implementation or live execution authority is granted by submission.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Preserve already selected direct-child facts and partial read failures before the
strict identity verifier runs. Submit only the additive proposal in this round;
leave the stopped Q1, unchanged admission rules and formal acceptance holds intact.

## Milestones / Task Breakdown

1. **S1 — Evidence review and joint submission (this round).** On an independent
   main-based branch, add this pair and one registry row. Check the latest Q1 error,
   frozen sampler/verifier and absence of a retained rejected identity. Separate
   known failure location from unknown image/cause. Submit J1–J4, cross-link #307
   and retain Draft; no candidate editing/testing, live query or measurement.
2. **S2 — Offline repair after joint approval, human merge and distinct authority.**
   Record concise approval metadata; copy sealed candidates into a new ignored
   directory under R5's proposed45-minute budget. Add R1's batch/read/decision
   publication, partial-error/handle cleanup, sampler integration and controller
   referenced-record checks. Keep R2's verifier/normalizer byte-identical. Add R4's
   injected tests and actual integration/audit assertions, with fail-on-live-call
   guards and independent output directories. Retain all attempted source/results;
   corrections stay within scope/cap and never erase failures. Report completed
   coverage, script/input hashes, reuse conditions and any unresolved failure.
   Unexpected semantics/scope or budget exhaustion stops work; no live probe.
3. **S3 — Hold, then only separately authorized bounded execution.** After successful
   S2, seek new explicit authority under R5. Check current source/base and reuse
   evidence without relabeling old binaries; freeze scripts/record rules/native
   inputs and publish the actual manifest before quiet/Q1. Follow the inherited
   150-minute caps, fixed Q1→F1 order, complete-evidence condition and first-anomaly
   stop. A retained wrong-image rejection still cancels F1. No repair/retry or extra
   diagnostic phase may be added after seeing the result.
4. **S4 — Evidence and stop.** Report actual verdict/exit or absence, candidate
   publication/completeness, association, environment/counter/archive/accounting
   results and gaps, cleanup and all budget usage. Keep narrow negative-evidence
   validation separate from six-child/formal acceptance. Preserve prior failures,
   update #307's current checklist/remaining holds and stop; no automatic merge.

## Files / Modules Touched (expected)

- Proposal: new `docs/specs/0079-observer-candidate-evidence.md`, this Plan and one
  `docs/specs/README.md` row. Later approval metadata is confined to these new
  documents/row; review and results belong in the reviewing PR/#307.
- Later S2, disposable files only under a new ignored
  `build/0079-observer-offline-<unique>/`: `observe-gate.ps1`, `run-formal.ps1`
  (candidate-evidence auditing/accounting only), `test-adapters.ps1` (regressions),
  and small `candidate_evidence.ps1` / `test-candidates.ps1` helper/fixtures.
- Copy without semantic edits: `association.ps1`, `observer_processes.candidate.ps1`,
  `observer_json.ps1`, `environment.ps1`, `environment_identity.ps1`,
  `test-environment.ps1`, archive/power assemblies and input-freeze helper. Reuse
  their publisher, input checks and existing tests; the new packet's candidate
  configuration, logs, snapshots, manifests and budgets are evidence only.
- Only under later S3 authority: frozen copies and raw outputs in a new ignored
  `build/pr307-formal-0079-<unique>/`, and #307's report/checklist.

No tracked tool subsystem, native production/test, SDK/protocol, build/CI,
performance policy/store/applicability, golden, AGENTS.md, approved prior Spec/Plan
body or Accepted ADR edit. No admission or classification change to any helper.
Unexpected file/semantic needs are deviations to report before implementation.

## Sequencing & Dependencies

[0078's stopped Q1](https://github.com/slmao/Atlantis/pull/307#issuecomment-6560945690)
and [pre-launch registration](https://github.com/slmao/Atlantis/pull/307#issuecomment-6560834102)
are the evidence basis. Their execution is closed; unlaunched F1 and unused time
do not carry forward. S1 stops at submission. S2 requires joint approval, human
merge and distinct offline authority; S3 requires completed S2, separate live
authority and the frozen registration. Neither this review nor its approval alone
permits collection. This proposal does not require merging #307.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Evidence/order** | Approve R1's selected-batch publication before process reads, partial candidate publication before assertions, hash-linked outcomes/errors and immutable failure records. The lost image remains unknown. |
| **J2 — Strict rules/scope** | Approve R2/R3: keep verifier/normalizer byte-identical, no image whitelist/filter, same query/cadence and explicit IO/CPU limitations. Retained rejection still stops. No ADR change. |
| **J3 — Offline verification** | Approve R4 and S2's minimal disposable-file scope, injected ordering/read/storage/cleanup cases, previous regressions and proposed new45-minute cap. Offline success is not live authority. |
| **J4 — Bounded next execution** | Retain R5's separately authorized new150-minute Q1→F1 protocol, original phase caps/registration/stops, strict completeness and old failure/Windows10 obligations. No retry, budget reuse or automatic acceptance. |

## Verification Checklist

**This proposal:**

- [ ] Check the frozen query/read/assert/save ordering and latest stop against R1/R2;
      do not infer or reconstruct the rejected image.
- [ ] Exactly three documentation files, relative links, `git diff --check`, preserved
      archive hashes, unchanged #307 source/Draft; no implementation or collection.
- [ ] Submit J1–J4 for joint review; approval and later authority remain pending.

**After the respective authorization:**

- [ ] **R1/R4:** Publisher/read/verifier spies establish durable batch before first
      read and durable candidate before actual verifier. Exercise each read-step
      failure, missing/null/unattempted facts, command quoting/full-time round trips,
      write faults and no overwrite. No verifier on failed/incomplete publication;
      preserve rejection/error references and dispose untransferred fake handles.
- [ ] **R2/R4:** Verifier/normalizer hashes match the sealed source. Correct fixture
      still passes; wrong image/parent/creation/PID reuse/stage/command and unknown
      identity still fail, without a second candidate read or ignored nonmatching row.
- [ ] **R3/R4:** Actual sampler/controller integration and candidate-record auditing
      use injected inputs; incomplete/hash-inconsistent/outcome-missing evidence
      cannot qualify Q1. Existing environment, counter, output, archive and lifecycle
      regressions remain. Two isolated fixture invocations preserve first-run hashes;
      zero live calls. Account for added logical IO without inventing a total bound.
- [ ] **R5:** Fresh explicit authority/input review and pre-launch registration;
      actual native revision and evidence reuse disclosed, caps/order/stops unchanged.
      Preserve every actual result and missing record; cancel F1 on any identity/
      collection problem, even if the rejection evidence is now complete. No extra
      helper/idle/benchmark/canary/ABBA or in-session fix/retry.
- [ ] **R5:** Old packets/protected paths, whole-PR formal obligation and old failures
      remain; Windows11 unverified, Draft while unresolved, no readiness/merge claim.

## Rollback Plan

Withdraw an unapproved proposal without editing earlier requirements. Abandon an
authorized candidate without deleting its failed evidence; no native or machine
state migration is involved. A frozen execution is never patched/restarted to roll
forward; any new classification or acquisition returns to review/authorization.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). No repository-wide
deltas. S1 ends with a reviewable proposal and #307 cross-link, not approval or
implementation. Later evidence-retention validation is distinct from M5 acceptance.
