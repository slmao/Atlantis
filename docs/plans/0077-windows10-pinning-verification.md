# Plan: Independent Windows 10 Pinning Test Acceptance for PR #315

- **Spec:** [Spec 0077](../specs/0077-windows10-pinning-verification.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** Pending joint review of Spec 0077 and this Plan in [PR #317](https://github.com/slmao/Atlantis/pull/317). No implementation authority from this submission.
- **ADR disposition:** Retain ADR-0118/0119/0139/0140; no new ADR proposed (Spec Architectural Impact).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Independently adopt only 0072's test-private capability correction and 0073's
Windows 10 acceptance choice for #315. Extend the approved World feature's file
scope explicitly; do not import #307 implementation, evidence clearance or budgets.

## Milestones / Task Breakdown

1. **S1 — Read-only review and joint submission (this round).** Reconcile
   0072 R2/R6 and 0073 R1/R2 with their actual owner rulings. Check current main,
   #315's unchanged pinning test/production setup and preserved failure. Draft
   R1–R6 and J1–J4, link code/results/uncertainties in the review PR and update
   only registry navigation. No reproducer, build/test or acquisition. Submit,
   link from #315 while preserving Draft and stop.
2. **S2 — Test-only correction, after approval/merge/authorization.** Record
   joint approval and include the human-merged proposal in #315's independent
   branch. Change only the R1 test file: private real-version/observation/value
   classifier, mandatory assertions before query, single query/immediate error,
   visible subcheck diagnostics and pure cases. Implement the reviewed rules
   against current code; do not merge/cherry-pick #307 or change production.
   Report any need outside this exact scope before adding it.
3. **S3 — Fresh repair verification.** Freeze the changed test/source, Windows
   host identity, build configuration and executable hashes. Build the existing
   `atlantis_runtime_gpu_tests` in Debug and Release; run the pure classification
   and original live pinning case verbosely in each configuration. Check every
   mandatory assertion independently of readback. Record actual version/error/
   status facts and the Windows 11 unverified limitation. No new readback call
   is made to fill missing historical evidence. Run relevant Runtime regressions;
   any GPU path uses clean Validation. Keep all non-passes; stop/report a new
   blocker without retrying or broadening into OS/performance diagnosis.
4. **S4 — Evidence and resumption boundary.** Record actual repair results,
   scope/protected-path checks and remaining acceptance in #315. Repair success
   alone does not complete 0076 S2. The remaining original matrix (including
   the previously unstarted GPU cases), S3 compatibility work and S4 whole-PR
   applicability/formal gate must be fulfilled under applicable owner execution
   authority. Do not treat prior partial Debug results as a pass for changed
   test inputs; reuse only unchanged evidence with explicit provenance and
   retain fresh verification where required. If authorization does not cover
   resumption, hold after reporting rather than launch it implicitly. No
   investigation, new measurement budget, baseline action or automatic ready/merge.

## Files / Modules Touched (expected)

**This joint-review PR:**

- `docs/specs/0077-windows10-pinning-verification.md` and this Plan.
- `docs/specs/README.md`: 0075/0076/0077 and candidate9 coarse status/links only.

**After distinct approval, human merge and implementation authorization:**

- `tests/runtime/gate_process_gpu_tests.cpp`: the sole native-file exception
  to 0075/0076, containing all R1–R5 test changes in its existing Windows guard.
- 0077 Spec/Plan approval/status metadata and registry; narrowly link the
  approved follow-up in 0075/0076 lifecycle metadata if needed, not their bodies.
- Ignored `build*/` logs, manifests and raw reports; #315 owns execution evidence.

No `src/**` change for this repair, including `gate_process_windows.cpp`; no
Runtime consumer, CMake/CI/target/link library, SDK/protocol, Android source,
golden, performance selector/policy/baseline, AGENTS.md or accepted ADR edit.
0072/0073 remain unchanged. Other native files retain their original 0075/0076
scope only when those separately authorized stages resume; this list grants
no opportunistic cleanup or broader test infrastructure.

## Sequencing & Dependencies

S1 → distinct joint Spec0077+Plan0077 approval → owner human merge → explicit
implementation authorization → S2 → S3 → S4. The earlier #307-only 0072/0073
authorizations do not grant this exception; 0075/0076 alone did not name this
native test file. Existing owner direction requests this proposal only.

Source base is latest fetched `origin/main`, independently of #307. The reference
documents and historical design comparison are available without its merge.
No pending #307 commit, MCP tool, observer, passing log or unused investigation
attempt is a dependency or a replacement for fresh #315 evidence.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Independent scope** | Approve R1's single-test-file exception in #315, independently implemented against current code; no #307 merge/cherry-pick, production fix or new dependency. |
| **J2 — Capability and mandatory checks** | Approve R2/R3's exact setup/priority/affinity obligations, real identity, single-query/immediate-error classification and visible diagnostics; no whole-case skip or catch-all unavailable rule. |
| **J3 — Acceptance platform/evidence** | Approve R4/R5: fresh Debug/Release validation on current Windows 10 only; retain all pure positive/negative branches, state Windows 11 unverified, and do not substitute #307's logs or fill old missing errors. |
| **J4 — ADR, gates and authority** | No new ADR; retain R6 and all original #315 acceptance/formal-gate obligations, failure/Draft handling and separate execution authority. No performance investigation, environment/baseline/policy change or budget transfer. |

## Verification Checklist

**This proposal:**

- [ ] **R1/R4:** read both prior owner rulings and distinguish their PR-local
  authority from this proposed adoption; record immutable main/#315 code links.
- [ ] **R5:** link #315's actual zero-return failure without inventing error87;
  verify retained archive hashes, no source/evidence mutation or new execution.
- [ ] Three-document diff, valid local links/numbering, `git diff --check`;
  Spec/Plan In Review, approval pending; #315 remains Draft and unchanged head.

**After authorization:**

- [ ] **R1/R2:** exact native-file delta only; setter/error/priority and exact
  selected affinity within system mask remain mandatory and precede readback.
- [ ] **R3:** pure cases for observed correct/wrong bits on legacy/supported/
  unknown versions; bounds10239/10240/22620/22621; early-Windows-11 values as
  synthetic only; invalid product/platform/major/minor, unknown/probe failure,
  access-denied/unsupported/zero-error failures and malformed version/size.
- [ ] **R3/R4/R5:** fresh Debug/Release builds, verbose current-Windows-10 live
  and pure results with identity/request/status/error/mask facts. Actual
  unavailable is explicit, not successful readback or full-case skip. Windows
  11 unverified; no real Windows 11 prerequisite or universal Windows 10 claim.
- [ ] **R5/R6:** relevant Runtime regression/Validation, immutable old failure
  history, exact source/binary/config provenance; retain and report any blocker.
- [ ] **R6:** original 0075/0076 Debug/Release/full suites, Android, World/Runtime,
  remaining GPU/headless/image/Validation and actual whole-PR applies/formal
  gate tracked as pending until genuinely complete. Expected applies=true;
  no test-only or capability-result exemption, policy/baseline alteration or
  borrowed #307 performance verdict/budget. Keep Draft while unresolved.

## Rollback Plan

If rejected, leave #315 blocked with all evidence. A later authorized test
reversion restores the old assumption and disclosed failure without changing
production or historical results. No engine data, baseline or OS migration.

## Definition of Done

No repository-wide delta. This round ends at joint-review submission. Future
pinning-repair verification is distinct from complete #315 acceptance; all
applicable [Definition of Done](../process/definition-of-done.md) items remain.
