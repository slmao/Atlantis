# Plan: Read-only Query on a Const ECS World

- **Spec:** [Spec 0075](../specs/0075-ecs-const-query.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** Pending joint review of Spec 0075 and this Plan, with
  [ADR-0139](../adr/0139-ecs-const-query-and-logical-iteration-state.md); reviewing PR to be linked on submission.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Propose the smallest World-only const-query extension for candidate 9, retaining
the guard and old query contract. The current round completes S1 only, from main
`b319fba`, with no implementation, engine/test execution or performance investigation.

## Milestones / Task Breakdown

1. **S1 — Static review and joint submission (this PR).** Confirm the receiver-
   const gap, writable view, depth guard, alias and CommandBuffer behavior; link
   immutable evidence in the reviewing PR. Draft the Spec/Plan/ADR and update
   candidate/registry navigation. Put J1–J4 for joint ruling. Stop after submission.
2. **S2 — Const access and shared protection, only after authorization.** Add
   R1/R2's constrained overload and R3's const view/walker without changing the
   old traversal's algorithm. Apply R4's sole mutable bookkeeping member and
   existing RAII guard, documenting R5's aliasing/threading/lifetime rules on the
   public header. Add focused const access, matching and guard tests. Build/run
   Debug/Release suites and Android `assembleDebug` under the existing gates,
   including the complete World suite, before continuing.
3. **S3 — Compatibility and integration.** Extend the existing query/CommandBuffer
   checks for nested combinations, alias-based refusal and later successful apply.
   Run all existing World cases and the existing Runtime CPU extraction/equivalence
   tests on the unchanged consumers in both configurations; retain the full-suite
   and Android build gates. Update the as-built
   World's access paragraph and lifecycle navigation only; no consumer conversion.
4. **S4 — Final verification and review evidence.** Check the actual full diff,
   build/test matrix, existing headless/image/Validation requirements and formal
   performance applicability/gate under R7. Preserve raw results/limitations in
   the implementation PR/ignored output. Stop on a scope deviation for reporting;
   do not repair environment/performance failures or alter requirements implicitly.
   Retain Draft for unresolved acceptance blockers; human review/merge is separate.

## Files / Modules Touched (expected)

**This joint-review PR only:**

- `docs/specs/0075-ecs-const-query.md` and this Plan.
- `docs/adr/0139-ecs-const-query-and-logical-iteration-state.md`: Proposed decision.
- `docs/specs/README.md`: 0075 row and existing candidate9 link/status only.

**After joint approval, human merge and separate implementation authorization:**

- `src/world/include/atlantis/world/ecs/world.h`: overload/constraints, const view,
  reference projection/private const walker declaration, `mutable queryDepth_` and
  required public access/threading comments. No new public service or result type.
- `src/world/src/ecs/world.cpp`: const walk using the same matching/order and shared
  guard; keep existing mutable walk and structural refusal behavior intact.
- `tests/world/ecs_query_tests.cpp`: compile-time receiver/callback checks and
  const/mutable traversal, aliasing and guard regressions using the existing harness.
- `tests/world/ecs_command_buffer_tests.cpp`: refusal/retention/later apply from a
  const-query context, including existing set-only refusal behavior.
- `docs/architecture/module_boundaries.md`: narrowly update World's access paragraph.
- `docs/specs/README.md` and Spec/Plan/ADR lifecycle metadata only as authorized.

No `archetype.h`, Component/CommandBuffer production file, Runtime file, new test
target, CMake/CI, descriptor/generated file, policy/store or accepted ADR edit is
planned. Unexpected needs are reported before adding them. Existing accepted
0050/0051 documents remain historical contracts; no compaction or silent rewrite.

## Sequencing & Dependencies

S1 → explicit J1–J4 joint review and ADR-0139 Accepted → human merge → separately
authorized S2 → S3 → S4. Review submission or one artifact's approval alone is no
implementation permission. No dependency on #307 or its merge, commits, observer
or measurement/investigation budget; source scope is World-only from latest main.
Runtime adoption remains outside this Plan regardless of the new overload.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — API and overloads** | Approve R1/R2: const receiver accepts `T` or `const T`, always projects const component references, constrains the actual lvalue direct call, supports empty terms, and preserves mutable overload selection/behavior. |
| **J2 — Constness and protection** | Approve R3–R5's separate read-only view/walk and sole mutable depth member, shared guard and existing alias/nesting/CommandBuffer behavior; retain single-thread/no-snapshot semantics. |
| **J3 — ADR and scope** | Accept proposed ADR-0139 as an additive access decision extending ADR-0101 D6; approve the narrow file list, no descriptor/version change, no Runtime adoption or accepted-document rewrite. |
| **J4 — Verification and gates** | Approve S2–S4/checklist below, including real full-PR `applies=true` and the unchanged formal performance obligation. No unused-API exemption, policy/baseline change or unapproved environmental repair/investigation. |

## Verification Checklist

**This proposal:**

- [ ] Static main-revision evidence and approval boundaries linked; no executed
  reproducer, build, test, GPU or performance result claimed.
- [ ] Four-file docs-only scope, relative links and `git diff --check`; existing
  accepted requirements/ADRs, #307 and native/test/build files unchanged.
- [ ] Spec/Plan In Review, ADR Proposed; joint ruling and implementation authority pending.

**After authorization (map to Spec requirements):**

- [ ] **R1/R2:** positive compile-time checks on `const World&` with unqualified,
  explicitly const and mixed terms, typed/generic read-only and move-only callable,
  plus `query<>`. Negative dependent probes with explicitly typed mutable-reference
  callables, unsupported member-pointer invocation and non-component terms; avoid a probe that only checks an unconstrained
  declaration. Ordinary mutable calls stay unambiguous and still expose their
  existing per-term mutable/const references.
- [ ] **R1/R3/R5:** use an actually const populated World initialized by move as
  well as `const World&`/`std::as_const` views. Compare complete entity order,
  addresses/values and matches against the mutable path across multiple archetypes
  and chunks, no-match/empty and repeated queries. Callback references remain live,
  not copies; generic callback parameter types are const. No data/state mutation
  by the const walk other than the depth bookkeeping.
- [ ] **R4/R5:** const/const and both mixed nesting directions with a legitimate
  mutable alias; same-World create/destroy/add/remove refused with Debug assertions
  and unchanged Release outcomes/state. Other-World changes remain allowed.
  Preserve get/has/set through the existing mutable alias and document that this
  can change values during traversal. Confirm protection lasts after inner exit
  and ends only after outer exit; balance on normal exit and test-owned callback
  unwinding without adding an engine exception policy.
- [ ] **R4:** recording a buffer during const traversal is allowed; applying it
  during that traversal or mixed nesting changes nothing and retains all commands,
  including a set-only buffer. Apply after the outer traversal succeeds under
  the existing semantics. Production CommandBuffer stays unchanged.
- [ ] **R2/R6/R7:** full Debug/Release builds/suites and Android `assembleDebug`
  at implementation gates; include full `atlantis_world_tests`, including
  existing query/storage/command/boundary cases, after S2/S3. Existing
  `atlantis_runtime_tests` CPU frame-input equivalence/extraction regressions pass
  with untouched consumers and public collector signatures. Current schema/version
  pins and module/dependency boundaries stay unchanged; no new compiler warning.
- [ ] **R7 correctness:** run the relevant existing Runtime headless smoke and
  frame/image regression checks on the reference machine to guard old-path
  behavior after the World header change. Vulkan Validation is enabled and clean
  for every GPU path run. No new rendering output/golden update is expected or
  authorized; unexpected differences are reported, never accepted automatically.
  Retain failures/unavailable verification as limitations, not invented clean runs.
- [ ] **R7 performance:** evaluate the actual full implementation PR with the
  unchanged selector; planned World paths mean `applies=true`. Build/run the
  existing formal gate in its required configuration/protocol and retain all
  results/provenance under existing rules. No measurement-derived CTest verdict,
  special tolerance, smoke override as acceptance, inherited #307 evidence/waiver,
  baseline refresh or environment fix. If it cannot be completed compliantly or
  returns a non-pass, report the actual result/reason and unresolved acceptance
  for human disposition; do not expand into diagnosis or selective reruns.
- [ ] **Scope/closure:** compare the whole diff against this file list; record
  commands, configuration, results and deviations in the implementation PR.
  No automatic ready/acceptance/merge from checklist completion.

## Rollback Plan

Withdraw the proposal if rejected. A later reviewed implementation rollback
removes the additive overload/const walker and restores ordinary depth metadata;
the old consumers already use the old API. No serialized data, schema, asset,
baseline or dependency migration is involved. Keep prior verification history.

## Definition of Done

Use [Definition of Done](../process/definition-of-done.md); no repository-wide
rule changes. This stage ends at a reviewable joint proposal. Future completion
needs accepted ADR/joint authorization, the scoped implementation and applicable
verification; unresolved failures are not completion.
