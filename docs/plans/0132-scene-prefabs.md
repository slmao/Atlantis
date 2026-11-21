# Plan 0132 — Create and Instantiate Static Scene Prefabs

- **Spec:** [Spec 0132](../specs/0132-scene-prefabs.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending
- **ADR:** [ADR-0181](../adr/0181-static-prefab-registration-and-provenance.md) (Proposed)
- **Design baseline:** `e132b2f5`, maintainer merge of [#426](https://github.com/slmao/Atlantis/pull/426)

## Objective

Make one shared Prefab registration from Hierarchy and two independent
instances from Assets. Commit each instance atomically and retain its source
association in the saved scene.

## Milestones

1. **S1 — Joint review.** Resolve J1–J4 and record accepted ADR and exact
   revision before implementation. Start from up-to-date `main` after the
   maintainer merges the design.
2. **S2 — Register and build.** Add the bounded source capture, checked checkout
   registration and trusted launch opt-in. Include registrations in the saved
   build catalog, freeze their source snapshots and expose the shared query.
3. **S3 — Instantiate and persist.** Add source v9 provenance and its shared
   guarded command, then extend verified append and replay. Implement the
   Hierarchy and Assets actions and source feedback in Inspector.
4. **S4 — Verify and submit.** Run the shared, conflict and Windows 100%
   checks below and capture applicable gate evidence. Submit the implementation
   for maintainer review.

The checked registration and build snapshot in S2 supply S3's source and
dependency identity. The v9 round trip and history guards must pass before the
complete Windows workflow.

## Expected files and modules

| Files or module area | Scope |
| --- | --- |
| `src/asset_system/{include/atlantis/asset_system,src}` scene source and semantic mapping, `tests/asset_system` scene codecs | Add optional root origin to the source mapping and test v7/v8/v9 and byte-identical cooked projection. Reuse the existing serializer, schema and cooker. |
| `src/authoring/{include,src}` session and transaction, `src/connection/include/atlantis/connection/{authoring_connection,asset_connection}.h` and their implementations, `tests/authoring` | Authoritative snapshot capture, optional node summary, checked origin on new roots and replay conflict coverage. |
| `src/runtime/{authoring_host,scene_save_as,asset_host}.{h,cpp}`, `src/runtime/main.cpp` and development launch arguments, prefab registration/snapshot helpers, `assets/CMakeLists.txt` and Saved Scenes selection in `cmake/AtlantisManagedImports.cmake`, related Runtime and build tests | Reuse the checked writer and single-slot build. Validate and pair built source/carrier. Keep capture and cooking outside the frame path. |
| `src/remote/src` codecs and client/server dispatch, `src/cli/src` invocation/command layer, native Gameplay SDK asset wrapper, `src/csharp/Atlantis.Gameplay` authoring/assets and codecs, `src/mcp/Atlantis.Mcp` exact tools, protocol documentation and matching client/conformance tests | Expose the same shared registration, query and limited append. Exercise unknown replies and bidirectional compatibility. |
| `src/editor_app/Atlantis.Editor` main window, session, launch and Saved Scenes workspace, `src/editor_app/Atlantis.Editor.Model` asset browser, document and structural history, and affected Editor tests | Hierarchy Save as Prefab, explicit permission forwarding, Assets instantiate, root Inspector association and existing history/Apply. |
| Spec/Plan 0132, ADR-0181 and `docs/specs/README.md` | Approval record and navigation. |

Enumerate concrete files within these areas before edits in the implementation
PR. Record out-of-list additions with the existing narrow approval process.
Use a disposable checkout for the table and chair fixture.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Capture and registration** | Approve one 64-node static subtree, retained local values, owner-checked snapshot and atomic checkout registration. Preserve the original tree and scene state. Grant Prefab writes by separate explicit opt-in. |
| **J2 — Availability and compatibility** | Approve discovery through explicit Saved Scenes Build/Open, existing Scene carrier with frozen source, and the shared Prefab read. Preserve legacy clients and accept the v9 authoring source boundary in ADR-0181. |
| **J3 — Instantiation and provenance** | Approve root-level guarded append with fresh GUIDs, source GUID on the instance root, exact Undo/Redo and independent explicit Apply recovery. Keep expanded scenes usable with an unavailable source. |
| **J4 — Execution and evidence** | After joint approval and maintainer merge, authorize one continuous S2–S4 run covering implementation, in-scope fixes, applicable verification and a review-ready PR. Keep the final merge with the maintainer. |

## Verification checklist

- [ ] **R1 — Capture and registration.** Check a single root in nontrivial
  document order, saved and committed-but-unapplied values, exact local TRS
  and names, explicit and absent material overrides, 64/65 nodes and the size
  bound. Refuse pending input, stale occurrence/revision, linked selections,
  unsupported components, missing typed dependencies, duplicate and case
  aliases, exchanged paths, no permission, and interrupted staging. Verify
  atomic source/fragment registration and known or unknown receipt behavior.
  Verify original nodes and scene state through independent read-back.
- [ ] **R2 — Build and shared query.** Check receipt invalidation, frozen
  snapshot and cooked-source agreement, invalid build or Open and explicit
  retry with the same registered GUID. Compare Editor, CLI, native and C#
  SDK and MCP results. Confirm old hosts refuse only the new operations, old
  clients retain existing reads and edits, and v7/v8 remain usable. Confirm a
  v9 scene requires an upgraded source reader while its cooked data remains
  compatible. Check legacy authoring writes and Save As/recovery carry links.
- [ ] **R3–R4 — Append and persistence.** Verify complete independent GUID
  maps and selection, old source/target or document refusal and zero partial
  nodes, unknown settlement with attempted root GUID, and independent
  Apply failure recovery with no additional append. Cover linked-root
  nesting refusal, external replay conflicts and unavailable source.
  Verify Undo/Redo serializes the link and restores the original GUID block.
- [ ] **Windows 100% — table and four chairs.** In a trusted disposable
  checkout open a saved setup with camera and an ordinary source subtree.
  Give the table and chairs distinct local transforms, include one nested
  transform if needed to prove hierarchy, and use two existing material
  bindings.
  1. From Hierarchy register the selected subtree. Record the asset GUID,
     source digest, and original tree, revision, Undo state and Runtime
     projection before and after. Confirm duplicate name refusal and zero
     partial writes. Complete a deliberate Build/Open, then locate the
     Prefab in Assets and verify the shared description.
  2. Instantiate twice, move the two roots to visibly different poses and
     compare their node GUID sets with one another and the original.
     Capture the scene and Inspector source feedback. Undo the moves in
     normal history order, undo one instantiation and redo it. Confirm the
     whole removed block, restored GUIDs and original source GUID. Redo the
     moves to recover the distinct placements. Exercise one controlled bad
     admission and one failed Apply with explicit retry.
  3. Save with both instances placed. Reopen the same source, Apply if
     needed, then use 0090 to freshly build and independently open the same
     saved source. Compare order, parent GUIDs, local TRS, mesh/material
     bindings and both root origins to the saved data and captures. Keep
     native screenshots and raw read-back.
- [ ] **Applicable gates.** Run affected Windows authoring, Runtime, client
  and Editor checks. Verify Vulkan validation and evaluate headless, golden,
  Android build/link and performance applicability on the final diff.
  Preserve the current baselines and raw results.

## Rollback

Keep registered units and saved originals. Older tools may retain and render
already cooked scenes. Continue to use a v9-aware reader for authoring files
with links. Any deliberate source downgrade needs a separately reviewed
conversion that specifies how to account for those links.

## Definition of Done

Follow [Definition of Done](../process/definition-of-done.md). Link the
exact Windows evidence, two instances' identities and applicable gates.
