# Plan: Save As for Hand-Authored Scenes

- **Spec:** [Spec 0088](../specs/0088-scene-save-as.md)
- **ADR:** [ADR-0148](../adr/0148-scene-save-as-and-explicit-launch.md) (Proposed)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending. Implementation authorization pending.

## Objective

Deliver a shared hand-authored scene copy operation and one opt-in offline
build/launch path using the existing scene codec and authoring session model.

## Milestones / Task Breakdown

1. **S1 — Design.** Review J1–J4 and ADR-0148 together. Record the human
   approval and explicit implementation authorization.
2. **S2 — Copy and shared contract.** Add the authoring snapshot/sink and
   opt-in Windows host commit. Add shared values, codec and clients. Report
   a copy without retargeting the original session.
3. **S3 — Build, launch and Editor workflow.** Compose the selected unit
   into the existing build catalog and scene whitelist. Add the Editor
   action, feedback and instructions for the separate development build.
4. **S4 — Verify and submit.** Run the mapped checks, record the exact
   candidate and evidence reuse, and submit a review-ready implementation PR.

## Files / Modules Touched (expected)

- Snapshot/values: Connection authoring headers and implementation,
  Authoring session header/implementation, existing authoring and boundary
  tests. Reuse Asset System GUID minting, scene serializer and catalog
  source codecs without changing formats.
- Windows executable host and startup: src/runtime/authoring_host.*,
  src/runtime/cli.*, src/runtime/main.cpp and src/runtime/CMakeLists.txt,
  an executable-private scene copy helper if needed, and matching Runtime
  startup and filesystem tests.
- Offline build: assets/CMakeLists.txt and the catalog composition helper
  in cmake/AtlantisManagedImports.cmake. Use the existing hand-authored
  scene producer in src/asset_system/CMakeLists.txt. Keep the saved-slot
  source merge compatible with the managed-import effective catalog input.
- Clients: Remote codecs/server/client, existing CLI command parsing and
  dispatch, native Gameplay SDK authoring access and C# authoring values and
  Remote session, MCP authoring schema and registry, Editor MainWindow and
  EditorSession. Include existing focused client, Editor and codec-vector
  tests under those modules. Regenerate vectors through their current tool.
- Documentation: README development workflow, architecture protocol and
  module boundaries, Spec registry and project blueprint.

RuntimeHost, renderer, platform libraries, Android host, scene/catalog
formats and committed goldens are outside the planned implementation.
Record necessary deviations under the repository's existing workflow.

## Sequencing & Dependencies

Start S2 from current main after joint human approval, ADR acceptance and
manual merge of the design PR. Reuse the serializer and catalog through
0065/0047 and the registered import inputs from 0086. Compose S2–S4 under
one authorization.

| Decision | Recommendation |
| --- | --- |
| **J1 — Authority and overwrite** | Approve the opt-in Windows host, fixed saved-scene namespace, logical names and atomic no-replace scene units. |
| **J2 — Identity and state** | Approve a fresh scene GUID, preserved node GUIDs and asset references, and the original document, history and projection remaining current. Settle a lost reply through local inspection. |
| **J3 — Shared contract and launch** | Approve one additive authoring operation and one explicit saved-scene build slot with the saved_scene alias. Reuse ordinary open/save in the new Runtime. |
| **J4 — Acceptance and delivery** | Approve the verification below, retain existing gates and authorize S2–S4 together after manual design merge. |

## Verification Checklist

- [ ] **S1 design.** Check numbering, links, statuses, file scope and
  whitespace.
- [ ] **R1–R3 host and identity.** Exercise successful copy, unconfigured
  host, absent/imported document, invalid names, traversal, aliases,
  reparse inputs and unsafe root. Race two hosts on one destination and
  verify one intact winner. Verify pre-existing files, units and case aliases
  remain byte-identical. Inject preparation/commit failure and process loss
  on both sides of rename. Prove no visible partial unit, temporary units
  ignored, a lost reply can be inspected, and retry cannot overwrite. Check
  fresh scene GUID, matching fragment and no change to original source.
- [ ] **R4 contract and state.** Compare native/C# codec vectors and
  CLI/SDK/MCP/Editor outcomes. Verify old-client Save/Open on the new host
  and new-client refusal on a pinned old host. Only the exact legacy
  unknown-method reply maps to Unsupported. Verify other malformed
  replies and lost-response feedback. Save a snapshot with unsaved
  committed edits while a tracked apply is paused. Compare original
  binding, revision, undo/redo, apply receipt, projection, source and
  catalog before and after copy and refusal. Exercise stale Editor
  completion and duplicate click. Confirm original Save still writes
  the original source.
- [ ] **R2–R5 live acceptance.** In a disposable Windows development
  checkout, use a hand-authored scene with a registered 0086 mesh reference.
  Save As via the Editor after a distinguishable accepted edit and compare
  shared client behavior using independent names. Confirm old Runtime discovery
  and open remain tied to the original. Select the returned name in a
  separate clean build with the same required managed inputs. Verify the
  cooked catalog entry, exact asset references and fresh scene identity.
  Start with --scene saved_scene, call ordinary open, compare semantic
  node values, active camera, node GUIDs and captured rendering. Verify
  the copied document starts with fresh history. Make an edit and Save
  on the new host, rebuild/restart and verify it persists in the copy,
  while the original remains byte-identical. Compare the default build
  and startup without the slot. Refuse malformed/duplicate selected
  catalog input and demonstrate a missing dependency, repair the build
  inputs and restart using the same copied scene GUID.
- [ ] **Applicable gates.** Build Windows Debug/Release and affected
  Android targets that compile shared code. Run focused native, managed
  and Editor checks and live Vulkan validation. Run or justify reuse of
  applicable headless/golden checks and use the full-diff performance
  applicability selector. For selected gates follow the original
  [performance contract](../process/performance-contract.md) and preserve
  original results. Cite exact candidate, configuration, inputs and
  evidence reuse. Keep the rendered fixture separate from committed goldens.

## Rollback Plan

Revert the operation and opt-in build slot. The original authoring source
and Runtime remain usable. Complete copies retain ordinary scene source
and catalog-source formats and can be preserved as project files.

## Definition of Done

See [Definition of Done](../process/definition-of-done.md). S2 starts only
after joint approval, ADR acceptance and human design merge. S4 records
all applicable checks and any reviewed deviations. Final merge is manual.
