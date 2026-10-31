# Plan: Close Scene Codec Validation Gaps F1, F3 and F4

- **Spec:** [Spec 0074](../specs/0074-scene-codec-validation-gaps.md) (joint approval recorded; human merge pending)
- **Status:** In Review; joint approval recorded, human merge pending
- **Author:** Codex, at slmao's explicit direction
- **Joint Human Review:** slmao jointly approved Spec 0074 and this Plan, J1–J4 as recommended,
  on 2026-11-01; [PR #311 evidence](https://github.com/slmao/Atlantis/pull/311#issuecomment-6546350130).
  Existing ADRs retained. S2–S4 are authorized after slmao's manual merge of #311, on a separate
  implementation branch from freshly fetched `origin/main`, independent of #307. Human merge pending.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Implement only the three deferred codec validation gaps in Spec 0074 after joint approval
and distinct implementation authority. This proposal is cut from `origin/main` `447b03f`,
independent of PR #307; the current round completes S1 only and stops.

## Milestones / Task Breakdown

1. **S1 — Read-only review and joint submission (this PR).** Trace Plan 0049 F1/F3/F4 against
   current codec, schema, capacity, public loader and conformance cases. Put immutable line
   evidence and static-review limitations in the reviewing PR. Create this pair and update the
   existing registry/candidate navigation. Request J1–J4 below; do not change native code/tests.
2. **S2 — Error contract and artifact refusal, only after the review gate.** Append the R4
   decoder errors; implement R1/R2 in the one final validation pass without changing prior
   checks or inactive-slot interpretation. Add focused tests and convert the two KnownGap
   expectations to strict errors. Remove the now-unused KnownGap acceptance branch/marker only
   where necessary to prevent a stale conformance bypass. Test the public memory/file decode
   wrappers with matching metadata, not merely hand-constructed `ValidatedSceneData`.
3. **S3 — Shared cook bound and diagnostic.** Append the R3 error, add the early common-path
   guard, advance the R5 independent cooker behavior marker, and update the existing
   asset-cooker diagnostic switch. Exercise inclusive boundary,
   one over, empty behavior and file-output preservation. Do not move the capacity rule into
   `sceneSchema` or add a parser maximum. Build/run relevant tests after each implementation step.
4. **S4 — Verification and review evidence.** Verify R5 valid corpus byte identity and existing
   version/fingerprint pins, boundary scans and relevant downstream regressions in Debug/Release.
   Review the full diff against this file list, record any deviation before expanding it, and
   report results/limits in the implementation PR. No automatic acceptance, ready or merge.

## Files / Modules Touched (expected)

**This review PR only:**

- `docs/specs/0074-scene-codec-validation-gaps.md` and this Plan.
- `docs/specs/README.md`: add 0074 row and point the existing codec candidate at it; no unrelated
  status cleanup or priority change.

**After joint review, human merge and distinct implementation authorization:**

- `src/asset_system/include/atlantis/asset_system/errors.h`: append exactly the R4 cases.
- `src/asset_system/src/scene_artifact.cpp`: final bounded decode validation pass.
- `src/asset_system/src/cook_scene.cpp`: shared count guard.
- `src/asset_system/include/atlantis/asset_system/cook_scene.h`: existing behavior-marker increment
  and narrowly updated cook-boundary comment; no signature or result-shape change.
- `src/tools/asset_cooker/cook_command.cpp`: new cook-error message.
- `tests/asset_system/scene_conformance_tests.cpp`: F1/F3 strict refusals, no stale acceptance escape.
- `tests/asset_system/decode_scene_tests.cpp`: artifact combinations/key controls and public loader paths.
- `tests/asset_system/scene_in_memory_cook_tests.cpp`: common text/document boundaries and propagation.
- `tests/asset_system/cook_scene_tests.cpp`: file-cook boundary/output preservation.
- `tests/tools/asset_cooker/cook_command_tests.cpp`: reachable CLI refusal diagnostic.
- Registry lifecycle metadata only as needed; execution data stays ignored/PR evidence.

Unchanged: scene source/encoder layout and format versions, semantic descriptors/schema/fingerprint,
catalog identity, source corpus, goldens, CMake/CI, policy/store, World/Runtime/Renderer, Authoring,
Connection/Remote and SDKs, approved requirements and Accepted ADRs. Any need outside this list
is a reported deviation before implementation, including any architectural/versioning change.

## Sequencing & Dependencies

S1 → explicit joint J1–J4 decision → human merge → separately authorized S2 → S3 → S4.
Record concise approval metadata only after the ruling; keep review discussion/results in PRs.
No step waits for #307 and no #307 commits are brought into the proposal or future implementation.
Future implementation branches from then-current `main` with this approved supplement.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Refusal/API compatibility** | Approve R1/R2/R4's two appended decoder cases, final-pass precedence and intentional refusal of formerly accepted malformed artifacts; preserve inactive slots, flag interpretation and deferred asset resolution. |
| **J2 — Capacity** | Approve the existing inclusive 65536 bound at the shared cook boundary, appended `NodeCountOutOfRange`, error precedence and no-write behavior; no new schema/parser constraint. |
| **J3 — Versions/architecture** | Treat this as enforcement of already-declared contracts and projection capacity: increment only the existing cooker behavior marker to /2; keep format7, semantic1/fingerprint and Accepted ADRs unchanged. If instead judged a normative tightening under ADR-0100 D4/D5, revise the proposal/ADR disposition before implementation. |
| **J4 — Scope and verification** | Approve the narrow file list, S2–S4 ordering and checks below; keep F5, performance work and #307 integration excluded. |

Distinct future authorization is required for S2–S4 after joint approval and human merge.
This round remains proposal-only even if a ruling arrives while submission is in progress.

## Verification Checklist

**This proposal:**

- [ ] Static evidence at the recorded main revision supports all three gaps, with no new
      reproducer/runtime/performance execution or code/test change claimed.
- [ ] Three-file docs-only diff; relative links and `git diff --check`; #307 unchanged.
- [ ] Joint J1–J4 rulings and later implementation authority remain explicitly pending.

**After authorization, mapped to Spec requirements:**

- [ ] **R1/R4:** each component pair/triple with otherwise-valid payload rejected; none/single
      controls and noncanonical nonzero presence values preserve current meaning. Existing
      artifact-field/reference errors retain precedence; new same-node R1-over-R2 ordering pinned.
- [ ] **R2/R4:** zero active mesh, zero active material, nonzero unknown references, absent material,
      ignored inactive slots and existing material-without-renderable behavior. Public
      `decodeScene` memory/file entry points propagate both new errors before construction.
- [ ] **R3:** flat, otherwise-valid scenes with distinct 32-bit node IDs and sufficiently wide
      deterministic GUIDs at 65536 succeed and decode; 65537 returns the new cook error across
      text/document/file entry points. Zero remains EmptyScene; oversized inputs with another
      error pin capacity precedence. File failure preserves existing artifact/metadata bytes.
      Scratch-generated inputs only; no timing assertion or giant committed fixtures.
- [ ] **R4:** old enum values stable, exhaustive switch compiles; Tools CLI emits the new
      count-refusal message and fails without valid output. Existing high-level Authoring
      mapping of other cook failures to CookRefused stays unchanged, not a wire/SDK extension.
- [ ] **R5:** conformance registries stay complete and strict; existing schema/declaration
      version and fingerprint tests unchanged/pass; only the independent cooker behavior marker
      advances to /2 without adding a metadata field. Recook the declared corpus (including
      available pinned Bistro) and compare artifacts plus metadata byte for byte against the
      pre-change manifest; disclose unavailable content rather than imply it was checked.
- [ ] **Integration:** Debug/Release builds; Asset System and asset-cooker suites plus relevant
      authoring/World GPU-independent regressions and existing boundary scans. No new rendered
      output or golden update is expected. GPU checks, if invoked, require clean Vulkan Validation;
      otherwise state N/A for this CPU codec patch instead of inventing a new clean run.
- [ ] **Applicability:** evaluate the actual whole implementation diff against the unchanged
      performance selector. The planned AssetSystem/Tools/test paths select no frame path;
      record N/A with that reason if confirmed. Unexpected selected scope requires reporting
      and applicable verification, never a policy/baseline exception. No performance investigation
      is part of this proposal round.

## Rollback Plan

Withdraw the proposal if rejected. A later implementation rollback restores the previous
KnownGap expectations and retains the failure history; it requires normal review. No asset
migration, baseline refresh or automatic version rewrite is involved. Existing valid bytes
stay usable; malformed inputs newly refused are not silently repaired.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). No repository-wide rule changes.
This stage ends at a reviewable, evidence-linked proposal; approval, implementation and runtime
acceptance remain pending. Future completion requires the checklist and human review, not merge by an agent.
