# Spec: Continuous Feature Authorization and Impact-Based Verification

- **Status:** In Review
- **Author:** Codex, at slmao's direction
- **Created:** 2026-11-02
- **Related Plan(s):** [Plan 0080](../plans/0080-feature-workflow.md)
- **Approval:** Pending joint human review.
- **Related ADR(s):** None new; see Architectural Impact.

## Summary

Use one explicit joint Spec/Plan authorization to carry a feature through implementation,
repair, verification and a reviewable PR. Reserve new design reviews for substantive
changes; use the implementation PR for routine adjustments, evidence and bounded blockers.
Architecture approval, truthful required verification and human merge remain mandatory.
This proposal changes no operating rule or feature until reviewed and implemented.

## Motivation / Problem Statement

[PR #307](https://github.com/slmao/Atlantis/pull/307#issuecomment-6560945690) has both a real
unresolved performance failure and avoidable workflow friction. Late applicability discovery,
closed file lists, repeated stage authorizations and promotion of diagnostic collectors into
acceptance prerequisites created a second development loop around verification. Reducing that
loop cannot make an actual gate failure pass. The retrospective and approval-count evidence
belong in this reviewing PR, not in the approved feature specifications.

## Goals / Non-Goals

One normal feature authorization; repairs without planning PRs; reusable impact-matched
evidence; finite diagnosis and clear escalation. No automatic merge, test-result deletion,
baseline/tolerance change, performance exemption, new diagnostic subsystem or feature work here.

## Requirements

**R1 — One continuous authorization.** Joint review names the goal/contracts, affected modules,
required verification/platforms and finite exceptional-work limits, and explicitly authorizes
implementation through a reviewable PR. ADRs must be Accepted where applicable. The same
decision may approve the documents and grant execution; document approval alone is not an
implied authorization. After its human-merge prerequisites, do not request new permission at
each milestone, ordinary failure, offline repair or test-to-live boundary already in scope.
Explicit owner stop/time/scope constraints still prevail. Human review/merge remain separate.

**R2 — Classify the change, not the file count.** Plan file lists are expected work maps,
not immutable boundaries during development; architectural and acceptance contracts are.

| Change | Action |
|---|---|
| Ordinary implementation/bug/test/temporary-script repair preserving approved behavior | Implement, verify and record in the existing PR; no new Spec or authorization. |
| Light adjustment: relevant file additions, step ordering, short build path, equivalent fixture/check method with the same coverage | Record reason, effects and verification before proceeding under R1; no separate planning PR. Lack of proven equivalence is a blocker, not permission to drop a check. |
| Feature-local test-host/evidence-method, resource or environment decision beyond R1, with product contracts/policies unchanged | One concrete owner ruling in the existing PR, naming the changed requirement, limits and unverified claims. No automatic approval or fresh Spec by default. Repository gate policies, failing required tests, thresholds and baseline rules cannot be waived through this route. |
| New goal/public API semantics, module boundary, dependency, protocol/data format, threading/ownership model, platform capability, new build/CI infrastructure or governance/gate policy | Stop only the dependent work; one new/revised Spec/Plan joint review and ADR when architectural. Continue unaffected authorized work. |
| Environment/tool failure | Preserve the failure, distinguish it from a product regression, perform only R4's bounded authorized work and report the external decision needed. Do not turn uncertainty itself into a new Spec. |

Approved specs/ADRs are not silently rewritten. Narrowing a feature's test-host acceptance needs
the explicit ruling above (e.g. Windows10 verified, Windows11 unverified), not a new support claim.
A product-capability, architectural or repository-policy change uses the design-review row.

**R3 — Verify actual impact; reuse proved evidence.** At initial planning and before final
verification, record one PR table: affected area/check, actual source/binary/input/configuration
and relevant environment, result/evidence link, and invalidation reason. Run changed logic and
affected consumers/boundaries; retain applicable builds/platform, GPU/Vulkan and image checks.
Reuse prior results when their dependency inputs remain valid; a documentation-only head need
not rebuild unchanged native code, but must name its real tested revision. Unknown validity
requires the affected check again, not an assumed pass or an automatic whole-repository sweep.
Always evaluate the complete PR against existing mandatory selectors: e.g. packaging CMake
changes can still require the unchanged formal performance gate. Do not hide matched paths.

Keep failures in the same table with cause/action/retest and Open/Resolved/Blocked status.
An attributable repair plus valid retest may resolve an old failure without deleting it;
a later convenient pass alone does not resolve an unexplained earlier failure. Required missing,
failed, nonconformant or unjudged checks remain blockers as the applicable policy requires.
No ready transition with an undisposed #307 performance failure; baseline/golden changes retain
their existing dedicated human-review routes.

**R4 — Keep diagnosis subordinate.** Separate mandatory acceptance evidence from optional
diagnostic enrichment. Default to the existing runner and minimal artifact preservation;
frequency counters, full process-tree explanations and collector completeness are not new
merge gates unless explicitly approved as necessary. A diagnostic failure is reported as such;
if it loses required data or may corrupt validity, that attempt cannot be called passing.
Do not retroactively reinterpret an interrupted run as a verdict.

Within R1, ordinary temporary-tool repairs need no new Spec. Default exceptional diagnosis/tool
construction ceiling is **60 cumulative minutes per feature**, one minimal external tool, no
new provider/dependency/system setting or investigation campaign. The feature's reviewed Plan
may state a different finite limit. Register formal inputs/order/count/stops before acquisition;
retain every outcome, and never repeat unchanged conditions to select a pass. Extensions need
one resource decision in the PR, not a new design package absent a material change. Exhaustion
or an unresolved environment stops that investigation, preserves Draft and permits unaffected
work; renaming a tool/task does not restart its budget.

**R5 — Explicit, prospective #307 migration (joint ruling required).** After this process change
lands and the owner explicitly adopts it, retain 0070/0071's product contracts, 0072's whole-PR
formal obligation, 0073's current-Windows10 mandatory checks/Windows11-unverified status, every
old result and all closed budgets. Replace only the added planning/per-stage authorization and
diagnostic-precondition obligations of 0072/0073/0078, not the native gate contract:

- Ordinary test/helper fixes become PR work items. Pending [0079/#319](https://github.com/slmao/Atlantis/pull/319) stays historical evidence;
  its separate merge/approval is not a prerequisite under this migration. Do not claim it approved.
- Retire the detailed Q1 counter/six-child PID observer and external capture of native-discarded
  temporary preheat/run files as merge prerequisites. Do not classify the unknown image as benign
  or loosen its verifier: use the existing native gate without either collector. Keep trustworthy
  command/source/binary/configuration/input provenance, full stdout/stderr/verdict/exit and all
  persistent native outputs. Native child execution/comparison/conformance stays unchanged;
  missing minimum evidence still blocks. **This specific retirement needs J4, not agent discretion.**
  It replaces future-use 0072 R5 child-artifact capture and 0078 R5 detailed-Q1/six-child
  prerequisites; 0073's readiness rules still apply if a detailed observer is used elsewhere.
- The owner may grant **one new 120-minute closeout envelope in the same adoption ruling**:
  preparation/provenance checks/freeze at most60min, **one** unchanged formal
  gate at most35min, evidence/report at most25min. No Q1, ABBA, extra live probe, replacement or
  retry; this prospectively replaces the next Q1→F1 schedule only. All old attempts stay closed.
- Verify reuse of the actual 9a363fa build/correctness inputs or run affected checks within the
  envelope. A native fail, unjudged result, data-loss/identity/environment anomaly or exhausted
  budget ends the envelope with Draft. Original M5 fail remains Open until evidence-supported
  disposition under the existing contract; this migration itself grants no pass or exemption.

If that specific retirement/adoption is not approved, the old #307 constraints remain binding.
Approval of process text alone does not start a measurement; execution authority must explicitly
name this envelope, and can be given conditionally in the same joint ruling. Nothing runs now.

## Proposed Design

Update the canonical operating rules, existing process pages and templates once, not add a
tracking system. Use one PR impact/result table and concise owner decisions.

## Architectural Impact

**No engine architectural change or new ADR:** ADR-0118/0119 and other Accepted module/API decisions remain
unchanged. R5 explicitly asks human review of additional observer requirements; it does not
alter native gate policy, comparison, baseline or benchmark protocol. Any contrary decision
requires its own architectural/policy review before implementation.

## Alternatives Considered

Retaining a planning PR for every helper bug preserves the loop. Removing architecture review,
weakening the gate, or hiding failed runs sacrifices safeguards. Giving unlimited diagnosis
authority replaces approval friction with tool expansion. This proposal does none of these.

## Testing & Verification Plan

Review table-top cases: ordinary helper bug proceeds; new public API stops for joint review;
docs-only head reuses a proved binary; changed build flags invalidate its evidence; missing GPU
or nonconformant/failed gate stays blocked; optional collector failure produces no invented
verdict; #307 migration never erases M5 fail. Check cross-document consistency and links. This
round is docs-only; later process implementation runs no feature/performance investigation.

## Risks & Open Questions

"Light" can be abused: unchanged contract/coverage and visible PR rationale are required.
Removing extra diagnostics may leave a real performance cause unexplained; that remains a
blocker/resource decision, not a reason to invent a pass. Joint review must rule on R5 separately
and explicitly. Fewer approval rounds do not promise fewer failures or a mergeable #307.
