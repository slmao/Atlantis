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
| **J1 — Authority and scope** | Put one optional declaration on the Asset System authoring descriptor. Annotate the mesh and optional material slots only. Verify agreement with existing Runtime expectations. |
| **J2 — Client compatibility** | Extend the existing field JSON with an omitted-when-unspecified member. Validate present values strictly, keep versions and persisted hash stable, and expose it through reflective SDKs, `scene schema <Type>` and existing MCP schema. |
| **J3 — Chooser behavior** | Fix the chooser to the declared type and guard confirmation against its typed list. Keep the 0082 generic fallback for absent metadata. Treat type matching as advisory for loadability and retain 0083 recovery. |
| **J4 — Delivery** | Approve the R1–R5 checks and the scope above. If the owner chooses to authorize implementation, one joint approval can cover S2–S4 after the design PR is manually merged. Existing gates and human final merge apply. |

## Verification Checklist

- [ ] **S1 documentation.** Confirm unique Spec/Plan/ADR numbers against
  current main and open work. Check relative links, statuses, scope and
  whitespace. Build, rendering and performance are inapplicable to S1.
- [ ] **R1 descriptor consistency.** Assert the exact mesh/material slot
  pair, material optionality, and absence on unrelated and ineligible
  descriptors. Exercise structural validation for metadata on the wrong
  shape. Verify both expected types against the current Runtime resolver
  using the loader's existing fixtures. Retain authoring field IDs, flags,
  type versions and the scene semantic hash. Read a pre-change cooked scene
  and verify existing World binding generation is still current.
- [ ] **R2 shared schema.** Use C++/C# codec vectors and real authoring
  responses to verify omission, valid canonical values and rejection of
  invalid values. Exercise unknown member tolerance separately. Compare
  native reflection, C# reflection, CLI text/JSON and raw MCP output for
  the same field identities and types. Verify World inspection keeps its
  existing shape and the MCP tool set remains unchanged.
- [ ] **R2 mixed versions.** Run pinned pre-change native and managed
  clients against the updated host and exercise authoring schema, edit and apply. Run an
  updated client against the pinned 0083 host and verify absent metadata
  decodes and the chooser retains 0082 behavior. Record both directions
  and the revisions used. An intentionally malformed declaration produces
  the existing protocol error, with no edit dispatched by the chooser.
- [ ] **R3 list behavior.** Use mixed mesh, material and unrelated entries
  to prove exact filtering in both slots and ordinary search within the
  allowed type. Use a synthetic field with different names and IDs to prove
  filtering follows its leaf declaration. Exercise optional absence, valid
  current preselection,
  unlisted and wrong-type current GUID display, empty typed matches,
  catalog error, dependency navigation, deselection, Reload view with
  pending detail and stale callbacks. Confirm neither list/detail arrival
  nor filter state writes the document or alters the Assets panel.
- [ ] **R4 edit and recovery.** Cover wrong-type, stale and duplicate
  confirmations, Cancel, unchanged choice and optional Clear. Exercise
  real Windows Inspector/worker/Runtime with default and available Bistro
  in managed Debug/Release. A changed valid choice makes one ordinary
  edit and history entry. Verify Undo/Redo, Pause/Step, shared Reopen,
  Save and imported refusal through their existing paths. Retain the 0083
  wrong-type manual/shared write failure path. Also exercise an in-type
  candidate whose load fails. A disposable fixture can use a cataloged
  material with a dependency that is unused by the initial scene and fails
  a loader check when selected. Its failure must appear through the
  typed apply outcome with the old projection and committed history
  retained. Repair through the filtered chooser while suspended, then
  explicitly Retry. Verify history replay while suspended and the 0083
  external revision/projection guard. The fixture may use existing
  controlled worker inputs where required and must document that boundary.
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
