# Plan: Scene Node Display Names

- **Spec:** [Spec 0069](../specs/0069-scene-node-names.md) (In Review)
- **Status:** In Review
- **Author:** Codex, at slmao's direction
- **Joint Human Review:** Pending review of this Plan, Spec 0069 and proposed ADR-0141/0142 together; no implementation authorization yet

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Objective

Deliver the named-node workflow in R1–R6 using the existing document, client
surface and editor history. This round submits the design only, based on latest
main `37cda711b655b4fb483aef7633614fde4afd9696`; it does not implement or measure.

## Milestones / Task Breakdown

1. **S1 — Joint proposal (this PR).** Confirm 0069 reservation and current code,
   submit this pair, ADRs and narrow roadmap/index updates. Run document checks
   and stop. Put J1–J4 below for one joint decision.
2. **S2 — Model, source and import.** After joint approval/ADR acceptance and
   human merge, add the name value/domain/schema constraint, semantic2 pin,
   source8/v7 reader and lossless mappings, explicit artifact projection and
   glTF import support. Add focused codec/import/conformance checks before
   proceeding; no bulk source migration or runtime artifact redesign.
3. **S3 — Shared edit and editor.** Extend closed command/event/error/summary
   values, native implementation/codecs, CLI, C# SDK and existing MCP command
   schema. Regenerate C++-owned conformance vectors. Add editor label/name field,
   latest-observed draft handling, name history and full node-life restoration.
   Run focused checks as each affected layer changes; no other editor feature.
4. **S4 — Integration and review-ready PR.** Complete the final mapped matrix,
   reuse only unaffected evidence with explicit provenance, inspect full diff,
   update protocol/as-built/lifecycle docs, and open/update an implementation PR.
   Report real failures and specific decisions while continuing independent
   authorized work. Ready only when required checks pass; human merge is separate.

## Files / Modules Touched (expected)

**S1 only (six documentation files):** this Plan, the new Spec, new ADR-0141/0142,
`docs/specs/README.md` and the node-name roadmap entry/adjacent completed MCP status
in `docs/project-blueprint.md`.

**Future implementation, grouped by responsibility; existing files unless noted:**

| Layer | Expected files and narrow changes |
|---|---|
| Asset System | `include/atlantis/asset_system/{authoring_scene.h,scene_source.h,scene_semantic_schema.h,scene_artifact.h}` and `src/{scene_source.cpp,scene_semantic_schema.cpp,authoring_scene_mapping.cpp,cook_scene.cpp}` under `src/asset_system/`: name/domain/versions/codec/mapping, validation and projection only; no binary layout or ValidatedSceneData change. |
| Import | `src/tools/gltf_importer/{scene_import.cpp,scene_import.h,import_command.h,import_command.cpp}` as needed for preserving labels and named refusal; no material/mesh import redesign. |
| Authoring/Connection | `src/connection/include/atlantis/connection/authoring_connection.h`, `src/connection/src/authoring_connection.cpp`; `src/authoring/src/{authoring_session.cpp,document_validation.cpp}`: closed values, filters, atomic rename and validation. |
| Native clients | `src/remote/src/{codec.h,codec.cpp}`; `src/cli/src/{command_layer.cpp,invocation.cpp}` and `src/cli/include/atlantis/cli/command_layer.h` for name parsing/output/help only. Existing generic Remote dispatch needs no new RPC. |
| Managed clients | `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs}`; `src/mcp/Atlantis.Mcp/ToolSchemas.cs` and existing event/output handlers only if required. No new MCP mapping/tool or SDK generator input. |
| Editor | `src/editor_app/Atlantis.Editor.Model/{AuthoringDocument.cs,DocumentTree.cs,StructuralEdits.cs,EditHistory.cs,HierarchyCommands.cs}`; `src/editor_app/Atlantis.Editor/{EditorSession.cs,MainWindow.cs}`: the one name edit, label/state/history wiring. |
| Existing tests | Scene source/cook/mapping/schema/conformance suites in `tests/asset_system/`; `tests/tools/gltf_importer/{gltf_scene_tests.cpp,bistro_end_to_end_tests.cpp}` and existing builder if required; `tests/connection/authoring_connection_tests.cpp`; `tests/authoring/{authoring_session_tests.cpp,authoring_validity_tests.cpp,authoring_round_trip_tests.cpp}`; `tests/remote/{remote_codec_tests.cpp,remote_authoring_tests.cpp}`; `tests/cli/{cli_scene_tests.cpp,cli_e2e_tests.cpp,cli_script_tests.cpp}`. Extend existing fixtures in these suites only where the new value requires them. |
| Managed/conformance tests | `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}` (JSONL generated, never hand-edited); `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite.cs,LiveAuthoringSuite.cs}`; `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests.cs,HistoryTests.cs,InspectorTests.cs,IntegrationTest.cs}` and its existing fake-document fixtures; existing MCP Program/M3/integration tests under `src/mcp/tests/Atlantis.Mcp.Tests/`, including both protocol versions. |
| Documentation | `docs/architecture/{module_boundaries.md,remote_protocol.md}`, narrowly the existing scene/source/import and CLI usage descriptions if now stale; this pair/ADR status metadata and their roadmap/index entries only. |

Paths inside brace groups name a bounded responsibility, not permission to refactor
the directory. Normal file/sequence adjustments follow AGENTS/0080; public-contract,
format, dependency/build or acceptance changes need design review. No Core/World/
Renderer/Runtime production, in-process editor, CMake/CI, package, generated World
bindings, performance policy/store or golden change is planned. If a matched
performance path becomes necessary, its actual gate obligation is retained below.

## Sequencing & Dependencies

Reuse approved 0049/0065/0068 and merged 0080. S1 → one explicit joint sign-off
accepting both ADRs and authorizing S2–S4 → human proposal merge → implementation
through verification and a review-ready PR under that authorization. Milestones
are reports, not new permission gates; ordinary in-scope code/test/helper errors
may be repaired and retested with failure history. No dependency on an unmerged
feature, inherited #307/#315 timing verdict or investigation budget. The current
instruction ends at S1, regardless of this proposed future authorization.

## Joint Review — Put for Ruling

| Ruling | Recommended decision |
|---|---|
| **J1 — Name meaning/domain** | Approve R1/R3: optional non-identity label, exact bounded UTF-8, duplicates/empty allowed, imported labels preserved or invalid import refused; no Runtime/string component. |
| **J2 — Persistence/projection** | Approve R2 and accept ADR-0141: source8 writer plus bounded source7 reader, semantics2/fingerprint change, artifact7 unchanged and explicit name-drop projection. No bulk migration. |
| **J3 — Shared surface/history** | Approve R4/R5 and accept ADR-0142: one closed rename command/event, additive summaries, CLI/native/C# and existing MCP transaction coverage, explicit mixed-peer limits, client-side name/history restoration. |
| **J4 — Delivery/authority/checks** | Approve R6/R7, narrow file scope and S2–S4/checklist; after human proposal merge, one joint authorization continues to review-ready implementation. No per-stage reauthorization, automatic merge or added diagnostic prerequisite. |

## Verification Checklist

**This proposal:**

- [ ] Main/numbering/current-source evidence linked in PR; Spec/Plan In Review,
  ADRs Proposed, joint implementation authority pending. Six-file docs-only diff,
  valid links/numbering and `git diff --check`; no engine/test/measurement claim.

**Future final candidate (focused checks during work, each remaining check once
on the relevant final inputs; valid unaffected results may be reused):**

- [ ] **R1/R2:** existing Asset System scene/schema/cook/mapping/conformance suites
  in Windows Debug/Release: empty/duplicate names,255/256 UTF-8-byte boundaries,
  multibyte text, forbidden controls/line separators/invalid encodings, quote/
  slash/space round trips, malformed placement/escaping, exact v7/v8 behavior,
  semantic declarations/fingerprint and source representability. Property/type
  schemas and fixed artifact bytes/decoding remain unchanged except declared
  scene semantics/projection. A name-only change cooks identical runtime bytes;
  artifact-to-authoring mapping is checked against the documented projection.
- [ ] **R3:** existing importer suite, including actual Bistro when available:
  names/empty/duplicates/invalid name refusal, overlay preservation and identity
  invariance. Compare otherwise identical name-only inputs for identical GUIDs,
  ordering, cooked artifacts and resolved workload; no new render baseline.
- [ ] **R4/R5:** existing Connection/Authoring/Remote/CLI suites Debug/Release and
  C# codec/live suites: atomic mixed transactions including rename-then-failure,
  create+rename, same-value event, UnknownNode/InvalidNodeName, command-order
  events/filter-all/node/component behavior, failure draining, source save/reopen,
  imported save refusal and apply retention. Test new/legacy summary shapes,
  malformed names and actual old decoders' extra-member tolerance without
  promising new command/event support on old peers. Regenerate/check vectors;
  keep generated World bindings and schema pins unchanged.
- [ ] **R5:** existing MCP offline suites, explicit2025-11-25 and2026-07-28
  sessions and integration: new closed transaction alternative, unchanged tool
  inventory, exact node/event data, strict rejection of unknown variants and
  unchanged apply/failure-settlement behavior. No direct native/protocol bypass.
- [ ] **R6:** existing headless editor model tests: label fallback/non-unique
  names/GUID selection, draft cancel/validation/external-change handling, one
  rename undo/redo, refusal after another client renames/deletes, subtree destroy/
  undo exact names, existing history lifetime and unaffected generic models.
  Windows product UI smoke confirms Enter/Escape/focus semantics and labels.
- [ ] **R2–R6 integration:** use the existing Editor App session-layer harness
  with real Runtime and CLI as second client on a hand-authored scene and Bistro:
  read/name/clear/undo/redo/external rename; save→reopen and save→rebuild→restart
  on a disposable hand-authored fixture; imported save remains refused. Confirm
  GUIDs/components/picking/selection and captures do not change for name-only
  edits. Do not mutate committed assets or require a new latency investigation.
- [ ] **R7 builds/boundaries:** final Debug/Release builds of affected native
  targets including Runtime and the above test executables, C# SDK, Editor Model,
  Editor App and MCP with existing pinned/locked package modes. Android
  `assembleDebug` for native portability, serialized with Windows builds. Run
  affected module/package boundaries and generator staleness tests. Record
  configurations/results; no repeated full engine suite at every milestone.
- [ ] **R7 GPU/gates:** existing default and Bistro headless/image smoke on the
  reference machine with clean fatal Vulkan Validation for GPU runs; no golden
  updates expected. Reuse unaffected renderer checks only with input provenance.
  Evaluate the actual whole implementation PR with unchanged `--gate --applies`:
  planned paths are unselected, but actual selection governs. If true, register
  exact final inputs/command and execute the original Release profilingON/
  TracyOFF formal native gate once, with its standard protocol/timeouts; no
  smoke override, added observer, retry-to-pass, baseline/policy/environment
  change or performance investigation. Retain every outcome; a non-pass is a
  blocker with a concrete next decision, not an implicit waiver. If false,
  record the selector result/reason without running unrelated measurements.
- [ ] **Closure:** full diff within scope, no silent accepted-document rewrite,
  all remaining requirements accounted for, source/reuse/hardware evidence and
  failures in the implementation PR. Ready only without required blockers;
  otherwise Draft with specific remaining decisions, no automatic merge.

## Rollback Plan

Revert through human review, preserving this decision and execution history.
Existing Runtime artifacts need no migration. New source8 or named scenes must
be exported to unnamed source7 by an explicitly reviewed downgrade before using
older tools; never silently discard names on rollback. Mixed-peer limits remain.

## Definition of Done

Apply [Definition of Done](../process/definition-of-done.md), no new repository
rules. S1 ends at a reviewable joint proposal; implementation acceptance needs
the authorized scope and applicable checks, not a new approval ceremony for each
ordinary fix. Human architectural approval and final review/merge remain.
