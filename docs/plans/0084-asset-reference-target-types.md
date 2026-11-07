# Plan: Shared Asset Reference Target Types

- **Spec:** [Spec 0084](../specs/0084-asset-reference-target-types.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending. This PR requests approval of the Spec,
  Plan and [proposed ADR-0145](../adr/0145-authoring-asset-reference-target-types.md).

## Objective

Publish the two initial target-type declarations and constrain the Inspector
chooser using the same metadata other clients can inspect.

## Milestones / Task Breakdown

1. **S1 — Joint design review.** Review R1–R5 and ADR-0145 together. Record
   the owner's decision on the issues below.
2. **S2 — Publish and expose.** Annotate the two native field descriptors.
   Carry the new member through the existing authoring schema and expose
   it in native/C# reflection, CLI inspection and the existing MCP tool.
   Confirm that old readers and the persisted scene schema remain compatible.
3. **S3 — Filter and confirm.** Capture the field annotation with the
   Inspector target. Use the existing chooser list to filter and gate typed
   choices, and retain the ordinary transaction and recovery paths.
4. **S4 — Verify and review.** Run the mapped checks and record commands,
   fixture provenance, environment, reuse and gate verdicts in the
   implementation PR. Human review and merge follow.

## Files / Modules Touched (expected)

S1 adds this Plan, the Spec and proposed ADR, and updates the spec index
and editor navigation in the project blueprint. The following paths are
proposed for S2–S4 only.

| Responsibility | Expected scope |
|---|---|
| Native declaration | `src/core/include/atlantis/schema.h` and `src/asset_system/src/asset_system_schema.cpp`. Use Authoring's existing descriptor publication. No semantic hash, validation, loader or format change. |
| Wire and C# | `src/remote/src/codec.cpp`, and `codec.h` only if a declaration needs updating. `src/csharp/Atlantis.Gameplay/Schema.cs` and `Remote/{Codec.cs,AssetCodec.cs}` for the optional property and shared token conversion. Preserve the native schema ownership contract and C# positional constructor. |
| Client inspection | `src/cli/src/{command_layer.cpp,invocation.cpp}` for the authoring inspection command and help. Reuse existing private schema formatting where possible. `src/mcp/Atlantis.Mcp/ToolRegistry.cs` may update the existing schema tool description. Native Gameplay SDK consumes the existing reflective connection. |
| Editor | `src/editor_app/Atlantis.Editor.Model/AssetReferencePicker.cs`, `src/editor_app/Atlantis.Editor/{AssetReferencePickerWindow.cs,EditorSession.cs}` and the existing picker wiring in `MainWindow.cs` as needed. Reuse AssetBrowser in a separate chooser instance. Keep typed eligibility and the confirmation guard local to the chooser. |
| Verification | Existing `tests/core/schema_tests.cpp`, `tests/asset_system/{asset_system_schema_tests.cpp,scene_semantic_schema_tests.cpp,scene_conformance_tests.cpp}`, `tests/runtime/scene_load_tests.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite.cs,LiveAuthoringSuite.cs}`, `src/mcp/tests/Atlantis.Mcp.Tests/{Program.cs,IntegrationTest.cs}`, `src/editor_app/tests/Atlantis.Editor.Tests/{AssetReferencePickerTests.cs,InspectorTests.cs,IntegrationTest.cs,ApplyOutcomeTests.cs,HierarchyTests.cs}` as applicable. If needed, use a disposable catalog/artifact fixture within the existing test harness to demonstrate a type match followed by reload failure. |
| Contract docs | `docs/architecture/{remote_protocol.md,module_boundaries.md}`, CLI usage in `README.md`, approval metadata and registry/navigation updates. |

No package, build/CI, renderer, Runtime production, World, generated binding,
committed asset or golden changes are expected. Apply the existing narrow
file-list approval rule for a nonsemantic deviation. A contract or acceptance
change requires reviewed design.

## Sequencing & Dependencies

Start from merged 0081, 0082 and 0083. Publish the native descriptor and
codec contract before wiring the Inspector. The design PR is the only
deliverable of S1.

| Joint decision | Recommendation |
|---|---|
| **J1 — Authority and scope** | Approve the Asset System descriptor as the field authority for the mesh and optional material slots. The catalog supplies actual record types and loader fixtures check agreement with both Runtime expectations. |
| **J2 — Client compatibility** | Approve the optional existing-schema member and strict present-value validation. Preserve the old field shape when absent, field identities, type versions, the persisted hash and existing protocol. Use existing reflective connections and MCP schema plus `scene schema <Type>`. |
| **J3 — Chooser behavior** | Approve the fixed type restriction and confirmation bound to the displayed list request. Keep the 0082 fallback for omission. Preserve ordinary writes and 0083 recovery, including the case where a matching asset fails at reload. |
| **J4 — Delivery** | Approve the scoped files and R1–R5 checks. Use one joint authorization for S2–S4 after human design merge and retain all applicable gates and human final merge. |

### Suggested joint authorization

> I approve J1–J4 and jointly approve Spec/Plan 0084 at the reviewed
> revision. I accept ADR-0145. After the design PR is manually merged,
> proceed through S2–S4 from current main, including implementation,
> applicable verification and submission of a review-ready implementation PR.
> Ordinary fixes within scope and reuse of verified unchanged inputs are
> authorized. Existing gates and human final merge apply.

## Verification Checklist

- [ ] **S1 documentation.** Confirm unique Spec/Plan/ADR numbers against
  current main and open work. Check relative links, statuses, scope and
  whitespace. Build, rendering and performance are inapplicable to S1.
- [ ] **R1 descriptor consistency.** Assert the exact mesh/material slot
  pair, material optionality, and absence on unrelated and ineligible
  descriptors. Exercise structural validation for metadata on the wrong
  shape. Read the declared types and compare them with observed outcomes of
  the existing loader fixture for both slots, using valid references and a
  wrong-type reference for each slot. Retain authoring field IDs, flags,
  type versions and the scene semantic hash. Read a pre-change cooked scene
  and verify existing World binding generation is still current.
- [ ] **R2 shared schema.** Use C++/C# codec vectors and real authoring
  responses to verify omission, valid canonical values and rejection of
  present null, bad spelling, unknown token, wrong JSON kind and ineligible
  placement. Exercise unknown member tolerance separately. Compare
  native reflection, C# reflection, CLI text/JSON and raw MCP output for
  the same field identities and types. Verify World inspection keeps its
  existing shape and the MCP tool set remains unchanged.
- [ ] **R2 mixed versions.** Use actual pinned pre-change native and managed
  clients against the updated host. Read the annotated authoring schema
  through the old decoders and exercise an existing edit and apply.
  Against the pinned 0083 host, verify updated native/C# decoders and CLI
  inspection see no declaration. Verify the updated Editor App still
  chooses and edits through 0082 behavior. Record host/client revisions in
  both directions. A deliberately malformed declaration produces the
  existing protocol failure and no chooser edit, even if preceding fields
  decoded successfully.
- [ ] **R3 list behavior.** Use mixed mesh, material and unrelated entries
  to prove exact filtering in both slots and ordinary search within the
  allowed type. Use a synthetic field with different names and IDs to prove
  filtering follows its leaf declaration. Exercise optional absence,
  valid preselection, unlisted and wrong-type current GUID display, empty
  typed matches, catalog error, dependency navigation, deselection, Reload
  view with pending detail and stale callbacks. A detail refusal leaves an
  otherwise valid visible selection eligible. Browsing leaves the document
  and Assets panel unchanged.
- [ ] **R4 edit and recovery.** Cover wrong-type, stale and duplicate
  confirmations, including a stale list identity for a GUID that appears
  in both lists and an unlisted GUID submitted directly to the worker.
  Exercise Cancel, unchanged choice and optional Clear. Exercise
  real Windows Inspector/worker/Runtime with default and available Bistro
  in managed Debug/Release. A changed valid choice makes one ordinary
  edit and history entry. Verify Undo/Redo, Pause/Step, shared Reopen,
  Save and imported refusal through their existing paths. Move any existing
  0083 wrong-type chooser setup to a manual or shared write when retaining
  that check. The typed chooser must now refuse that wrong-type selection.
  Separately, run one default-scene recovery case with a type-matching
  choice. In a disposable fixture, select a cataloged material that the
  initial World has not loaded and make its artifact unreadable after the
  chooser's snapshot succeeds. Confirm list/detail still show `material`.
  Verify the edit is accepted and reload reports `ScenePreparationFailed`
  for that receipt, with old generation/projection and committed history
  retained. Repair through the filtered chooser while suspended and verify
  no apply is sent. Exercise guarded history replay, then explicitly Retry
  and verify the matching latest projection resumes normal editing. Retain
  the 0083 external revision/projection guard evidence with input audit.
  Restore disposable artifacts and record routed or controlled worker
  input boundaries.
- [ ] **R5 builds and gates.** Build affected native Debug/Release and
  Android targets and managed SDK, Editor and MCP with existing pins.
  Run affected schema, transport, CLI/SDK/MCP and Editor suites and
  inspect Vulkan Validation output from live rendering paths. Preserve
  fixture inputs and byte-compare restoration. Run applicable
  headless/golden checks or reuse results only after an input/provenance
  audit. Run the whole implementation PR's performance applicability
  selector and retain any selected gate under the
  [performance contract](../process/performance-contract.md). S2–S4 scope
  predicts no frame-path match. The actual diff decides. Report every
  unresolved required gate before requesting human merge.

## Rollback Plan

Revert the declaration, optional schema member, client display and chooser
restriction together. Existing stored GUIDs and scene artifacts need no
migration. The chooser resumes generic selection.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
S1 ends with a reviewable design. S2–S4 require joint approval, ADR acceptance,
and the design merge prerequisite. Final implementation merge remains human.
