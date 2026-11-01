# Plan: Const ECS Query Compatibility Boundary

- **Spec:** [Spec 0076](../specs/0076-ecs-query-compatibility.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** Pending joint review of Spec 0076 and this Plan, including proposed [ADR-0140](../adr/0140-distinct-const-query-entry-point.md), in [PR #316](https://github.com/slmao/Atlantis/pull/316). No implementation authority from this submission.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Resolve #315's R2 design blocker with the smallest explicit public-API revision,
while preserving the rest of approved 0075. This round completes S1 only; it
neither repairs the draft nor advances its acceptance state.

## Milestones / Task Breakdown

1. **S1 — Bounded design review and submission (this PR).** Read approved R1/R2,
   the actual overload/invocation code and retained failure. Evaluate at most
   eight reduced C++20 translation units, once each with the existing Windows
   compiler: old control, four same-name forms, explicit-const primary and
   id-category cases, distinct-name matrix. No engine build/link/run or copied
   production-header repair. Retain every result, stop this exploration after
   the fixed set, and submit C1–C5/ADR-0140 for J1–J4 ruling. Evidence/logs stay
   in the reviewing PR and ignored output, not in normative documents.
2. **S2 — Scoped correction in #315, only after C5 authorization.** Record the
   joint ruling and ADR acceptance metadata, incorporate the human-merged
   proposal on #315's independent implementation branch, and change only the
   new public entry name and corresponding documentation/test calls. Preserve
   old `query` declaration/body and all failure history. Add C4's id-sensitive
   old-call regression alongside the existing generic-mutation case, without
   modifying either old behavior or writable callback constraints. Run the real
   compile checks and World tests plus the original S2 full Debug/Release and
   Android gates before advancing. Any further design need stops for reporting.
3. **S3 — Resume original compatibility/integration work.** Complete Plan 0075
   S3 under C2–C4 using the new name for const traversal, with unchanged
   mixed/const nesting, alias and CommandBuffer tests and Runtime CPU consumers.
   Update only the scoped as-built access paragraph/lifecycle navigation.
4. **S4 — Resume original final verification.** Complete Plan 0075 S4/C5 against
   the actual full implementation diff, retaining all results and pending
   obligations. Do not infer readiness from a design probe or compilation fix.
   Failures/unavailable checks retain Draft; no performance diagnosis, selective
   rerun, baseline/policy change, environmental repair or automatic merge.

## Files / Modules Touched (expected)

**This review PR:**

- `docs/specs/0076-ecs-query-compatibility.md` and this Plan.
- `docs/adr/0140-distinct-const-query-entry-point.md`: Proposed partial successor.
- `docs/specs/README.md`: 0075/0076 and candidate9 navigation/status only.

Reduced models/scripts/logs are ignored review evidence. No native/test/build
file or approved Spec/Plan/ADR is changed by this PR.

**Future authorized #315 work:** retain exactly Plan 0075's implementation list:

- `src/world/include/atlantis/world/ecs/world.h`: C1/C2 spelling/constraints and
  public contract; retain the approved private view/projection/depth design.
- `src/world/src/ecs/world.cpp`: only unfinished approved const traversal work,
  if needed; no mutable traversal algorithm or storage/guard redesign.
- `tests/world/ecs_query_tests.cpp`: new-entry call names and C4 legacy/new
  compile/runtime checks, preserving the current regression and raw evidence.
- `tests/world/ecs_command_buffer_tests.cpp`: original S3 protection checks.
- `docs/architecture/module_boundaries.md`: narrow as-built World access paragraph.
- `docs/specs/README.md`, 0075/0076 Spec/Plan lifecycle metadata and ADR-0139/0140
  status/acceptance/successor metadata only. Do not rewrite approved requirements
  or accepted decisions. 0076 supplies the explicit superseding API delta.

No Runtime, CommandBuffer production, archetype/component storage, descriptor,
schema/version, CMake/CI, dependency, policy or baseline change. A new file need
outside this list is a deviation to report before work.

## Sequencing & Dependencies

S1 → distinct joint Spec0076+Plan0076 approval and ADR-0140 acceptance → human
merge of this proposal → explicit authorization → S2 → original S3 → S4.
The earlier 0075 authorization does not silently approve C1. Do not merge #315
or this proposal automatically, rebase away failure history, or depend on #307.
All unchanged 0075 requirements and gates remain binding.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — API delta** | Approve C1/C2: distinct `queryReadOnly` name; retain unqualified/const/mixed/empty terms and all R2 boundary/legacy compatibility requirements. No const `query` alias. Do not adopt the incomplete explicit-const-only workaround. |
| **J2 — ADR disposition** | Accept ADR-0140 as a partial successor for ADR-0139's same-name public entry only; retain its remaining decisions and accepted historical text. |
| **J3 — Repair scope and evidence** | Approve C3/C4 and the narrow future file list; retain failed evidence and add real legacy id-category regressions. No Runtime adoption, production CommandBuffer edit or broader callback framework. |
| **J4 — Authority and acceptance** | Approve C5 and S2–S4's unchanged build/Android/correctness/formal-gate obligations, expected full-PR applies=true and failure/Draft handling. Implementation still needs human merge and explicit follow-up authorization; no performance investigation. |

## Verification Checklist

**This submission:**

- [ ] S1's eight results retained with commands/compiler/provenance, no retries
  or engine repair, and no claim of general impossibility or #315 acceptance.
- [ ] Four-file docs-only diff, local links/numbering/status and whitespace checked;
  #315 source/tests/evidence and accepted normative bodies unchanged.
- [ ] Spec/Plan In Review, ADR Proposed; joint decisions pending; #315 Draft.

**After authorization:**

- [ ] **C1/C4:** real old generic mutation, all-const and empty id-sensitive
  callbacks compile and preserve behavior; old callable category/overload and
  receiver forms remain valid. No const same-name alias. No old callback/body
  edit or failing-test removal to obtain a pass.
- [ ] **C2:** positive real `const World&`/actually const World calls for
  unqualified/const/mixed/empty terms, typed/generic and move-only lvalue callables;
  dependent negative writable-component/id-reference, invalid-term and
  member-pointer-only checks on the constrained new declaration.
- [ ] **C3:** all original 0075 matching/order/address/live-value, nesting,
  structural refusal/depth balance and whole-CommandBuffer retention checks.
- [ ] **C5:** complete Plan0075 S2/S3 Debug/Release/full World/Runtime CPU and
  Android gates, then S4 headless/image and clean Vulkan Validation for GPU
  runs; no unexpected golden/output change accepted silently.
- [ ] **C5:** actual full-PR applicability and existing formal gate/provenance;
  expected applies=true, all non-passes retained and disclosed. No inherited
  #307 evidence/budget, unused-API waiver or implicit environment investigation.
- [ ] **Closure:** whole diff within scope; reviewable results in #315. Keep
  Draft for any unresolved acceptance blocker; human review/merge separate.

## Rollback Plan

If rejected, withdraw this proposal and leave #315 blocked with its evidence.
A later implementation rollback removes only the additive read-only entry and
its private support under review, retaining the old API. No data, baseline or
consumer migration is introduced.

## Definition of Done

No changes to the repository Definition of Done. This round ends at a submitted
joint proposal. Future completion requires approved C1–C5, accepted ADR-0140,
scoped implementation and every applicable original gate; the design probe is
not a replacement for any of them.
