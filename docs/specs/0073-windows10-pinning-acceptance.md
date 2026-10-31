# Spec: Windows 10 Acceptance Scope for the Pinning Test Repair

- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Created:** 2026-10-31
- **Related Plan(s):** [Plan 0073](../plans/0073-windows10-pinning-acceptance.md) (In Review)
- **Approval:** Pending joint human review in [PR #310](https://github.com/slmao/Atlantis/pull/310).
- **Related ADR(s):** None new; see Architectural Impact.
- **Supplements:** [Spec 0072](0072-mcp-acceptance-verification.md) / [Plan 0072](../plans/0072-mcp-acceptance-verification.md), whose joint approval is recorded in [PR #309](https://github.com/slmao/Atlantis/pull/309#issuecomment-6539457400).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Limit acceptance of PR #307's existing pinning test repair to the current Windows 10
reference host, as explicitly requested by the owner. Replace only the real Windows 11
validation obligation through this additive review; preserve mandatory pinning checks,
capability classification and every performance obligation. This round submits the proposal
and permits offline observer diagnosis only; it grants no new measurement execution.

## Motivation / Problem Statement

0072 requires both legacy-host and Windows 11 22H2+ real-host evidence. The owner now chooses
Windows 10 alone for this repair. That acceptance change needs a visible reviewed supplement,
not an in-place rewrite of approved requirements. The existing [execution report](https://github.com/slmao/Atlantis/pull/307#issuecomment-6539697114)
also discloses an observer path-matching defect and an investigation stopped under R5.
Neither the owner's platform decision nor an offline observer fix resolves performance acceptance.

## Goals

- Record the narrowly changed acceptance platform and retain an honest unverified-Windows-11 statement.
- Preserve the existing repair and Windows 10 verification obligations without weakening classification.
- Distinguish offline helper validation from authority to resume the stopped investigation.

## Non-Goals

- No change to repository-wide Windows support, production pinning, test semantics, APIs or dependencies.
- No new performance measurements, observer deployment, environment intervention or attempt-budget reset.
- No change to policy, baselines, applicability, gate preconditions, approval requirements or failed outcomes.
- No automatic M5 completion, PR readiness transition or merge.

## Requirements

### Functional

- **R1 — Local acceptance replacement.** For this pinning repair only, replace 0072 R6's
  real Windows 11 22H2+ positive-validation requirement, Plan 0072 S2's corresponding run,
  sequencing hold and R2/R6 verification item with acceptance on the current Windows 10
  reference host. This also narrows the associated J2 real-host evidence choice. The
  Windows 11 real-host check is no longer a prerequisite once this supplement is jointly
  approved and human-merged; state explicitly that Windows 11 has not been validated.
  This does not assert behavior on every Windows 10 build or change general platform support.
  Leave 0072's approved text intact and cite this supplement at the acceptance decision.
- **R2 — Retained Windows 10 checks.** Keep 0072 R2 unchanged: production setup success,
  empty error, high priority and exact selected single-processor affinity within the system
  mask are mandatory. Keep its single well-formed query, immediate error capture, explicit
  unavailable diagnostic and all capability classification rules/tests, including supported
  version, unknown identity, malformed request, other-error and successful wrong-mask failures.
  No entire-case skip or synthetic Windows 11 success claim. Reuse the retained Debug/Release
  builds, real Windows 10 cases, pure classification and relevant Runtime regression evidence
  only after verifying it still corresponds to the unchanged repair; a code/input change
  requires relevant verification again, outside any measurement interval.
- **R3 — Offline observer boundary.** Offline diagnosis may normalize fully qualified Windows
  executable paths and compare the exact registered executable rather than a raw worktree
  prefix. Validate with injected snapshots, preserve PID/creation-time identity and surface
  path/read failures instead of silently omitting them. Require evidence that the known gate
  process identity maps to the expected executable before any future live observation can be
  considered ready; a counter-only ready file or empty process list is insufficient.
  Candidate scripts and fixtures remain disposable ignored evidence. Preserve all original
  scripts/data and label fixtures synthetic; missing historical process records stay missing.
  Offline success establishes neither live collection readiness nor observer overhead.
- **R4 — Retained holds and authorization.** 0072 R1/R3/R4/R5 and all remaining R6 holds
  continue unchanged: whole-PR `applies=true`, formal performance acceptance, retained failures,
  investigation budget and stop conditions. The stopped execution remains closed, with no
  reuse of unlaunched attempts. This proposal and its approval authorize no fresh gate,
  benchmark, live counter sampling or replacement batch. Any future live observer validation
  or performance work requires a new explicit bounded execution authorization that identifies
  its validation, acquisition limits and stops before launches. Keep PR #307 Draft while
  performance or other applicable acceptance remains unresolved.

### Non-functional

No committed tooling subsystem, new dependency, timing assertion, production/configuration edit
or data migration. Historical measurements and evidence hashes remain intact.

## Proposed Design

Use this Spec as the additive acceptance override and Plan 0073 for the review/recording sequence.
No native repair is proposed: the existing 0072 test remains the subject of Windows 10 acceptance.
Keep offline observer candidates separate from archived evidence and from shipped gate code.
Record owner direction, review decisions, validation results and unresolved limits in the PRs.

## Architectural Impact

**None — no new or superseding ADR.** This changes one repair's real-host acceptance scope,
not a module, public API, threading/ownership model, dependency or backend contract.
Accepted ADR-0118/0119/0130/0138 remain unchanged, including the formal gate and baseline rules.
Any need to change those decisions returns to separately scoped Spec/ADR review.

## Alternatives Considered

- Retain the Windows 11 hold: conflicts with the owner's explicit local acceptance decision.
- Delete approved text or weaken the tests: obscures history or exceeds the requested scope.
- Declare Windows 11 validated from synthetic cases: supplies no real-host evidence.
- Repair and rerun the observer in the stopped batch: violates the existing stop and current prohibition.

## Testing & Verification Plan

Map the limited override to the exact 0072 obligations (R1), verify retained Windows 10 evidence
against the unchanged repair (R2), and test path matching/identity/failure handling using offline
fixtures only (R3). Check protected paths and original evidence integrity, retain the literal
performance failures and closed budget, and verify PR #307 remains Draft (R4).
No rendered-output change is proposed; no new GPU, golden or live performance run is claimed.

## Risks & Open Questions

Windows 11 remains unverified. Offline path normalization cannot prove live process accessibility,
short-lived child capture, physical file identity through aliases, time alignment or collection
overhead. Joint review must decide the local scope override and evidence reuse; it must not
conflate those decisions with performance clearance or new execution authorization.

## Out of Scope / Future Work

New live investigation, deeper tracing, environment changes, production fixes and any baseline
action require their own applicable authorization/review. None follows automatically from this PR.
