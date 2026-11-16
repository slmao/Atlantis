# Plan 0110 — Create Common Scene Objects

- **Spec:** [Spec 0110](../specs/0110-common-scene-objects.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending approval of both documents and authorization for S2–S4.

## Objective

Deliver a single Hierarchy menu for the named presets and existing empty-child
creation, using ordinary authoring transactions, structural history, tracked
Apply and explicit Save.

## Milestones

1. **S1 — Joint review.** Review J1–J4 and approve the Spec and Plan before
   manually merging the design PR. Check numbering, navigation and links.
2. **S2 — Implement.** Define an Editor Model preset table and compose the
   bounded transaction and expected complete node state. In the existing
   worker, preflight and settle it, verify history before any refresh-dependent
   UI work, then delegate projection to the Apply controller. Wire the menu,
   availability, feedback and guarded selection in the existing Hierarchy.
   Keep 0087's action and the ordinary replay route in place.
3. **S3 — Verify the final candidate.** Run the mapped focused and Windows
   100% checks and applicable gates. Cite any reused evidence with its inputs.
4. **S4 — Submit a review-ready PR.** Link approval, candidate and evidence;
   disclose deviations and any open gates. Leave final review and merge to
   the maintainer.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/{SceneObjectPresets,HierarchyCommands}.cs` | New UI-free preset composition and full expected values. Reuse node state, schema addressing and `NodeLife`; keep existing layout/order and Assets composition. |
| `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Worker request, preflight, submit/drain and known/unknown settlement, history/Apply order, Hierarchy menu and feedback. Reuse the existing tree, event observation, Apply controller and visual resources. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{SceneObjectCreationTests,Program,InspectorTests,SavedSceneTests}.cs` | New model/worker fixture and registration; extend existing actual Editor and Saved Scenes fixtures for the focused desktop round trip as needed. |
| `README.md`, `docs/specs/README.md` | Usage and delivery navigation. |

Connection, CLI, SDKs, MCP, native Runtime, Asset System, Renderer, shader,
catalog, build/CI and format changes are outside the planned diff. Use
disposable scenes and catalog/artifact fixtures for verification. Report
any necessary scope adjustment under the existing repository rules.

## Sequencing and dependencies

Design baseline: `738a2f11`, [manual merge of PR #384](https://github.com/slmao/Atlantis/pull/384).
Start S2 from latest main after explicit joint authorization and manual
merge. Use [0068](../specs/0068-authoring-editor.md) structural history and
the settle-first creation path of [0087](../specs/0087-add-mesh-to-scene.md)
as the composition reference, [0083](../specs/0083-authoring-apply-outcomes.md)
for the projection controller, and [0090](../specs/0090-saved-scene-build-open.md)
for same-source Build/Open. `RequireDocumentRevision` is limited to the
same-field property batch; `RequireDuplicateBatchRevision` applies to the
closed duplicate shape. Neither is a general creation guard. Reuse existing
revision reads to detect observed interference, with no atomic check-and-set
claim. Do not route preset creation through the older pipelined edit path.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Menu, names and placement** | Approve the seven named root presets and the existing unnamed empty-node path. Keep child placement explicit and single-target. Use the authored defaults in R2 and leave the active camera and Viewport mode alone. |
| **J2 — Shared composition and assets** | Approve the three fixed existing Mesh GUIDs, live catalog lookup, no material override, and existing authoring/cook limits. Compose existing operations for all clients; no new ADR, asset registrations or capability negotiation. |
| **J3 — History and failure recovery** | Approve observed lifetime/row checks, submit-and-drain settlement, one structural record for a verified create, and independent tracked Apply. Report unknowns and require explicit recovery where specified. Keep existing observational concurrency and replay semantics. |
| **J4 — Acceptance and delivery** | Approve the focused and 100% Windows same-source round trip below, with existing gates. Authorize S2–S4 together after manual design merge. |

Suggested authorization for the reviewed revision:

> I approve J1–J4 and Spec/Plan 0110 jointly and agree that no new ADR is
> needed. After manual merge of the design PR, proceed from latest main
> through S2–S4, including scope-preserving fixes, applicable verification,
> reuse of valid evidence and submission of a review-ready implementation PR.
> Keep Windows 100% acceptance and existing gates. Final merge is manual.

## Verification checklist

- [ ] **R1/R2 — Preset, menu and ordinary clients.** Verify all seven
  named plans and the empty child against the schema (including enum lookup,
  radians, component defaults, transforms and absent material), one batch
  per action, distinct GUIDs for repeated names, and root placement under
  single and multiple selection. Verify child parent and the legacy imported
  behavior, no menu-open/cancel writes, and absence of implicit active-camera
  changes. Check missing/wrong-type catalog entries and unavailable schema or
  no document. Use ordinary shared calls to compose representative camera,
  light and mesh batches; show existing `scene tx`, SDK and MCP can express
  them (explicit GUID, component/name/field values, submit/drain and Apply).
  Reuse matching protocol/codec and client capability evidence; exercise a
  new client-side preset against a compatible unchanged Runtime.
- [ ] **R2/R3 — Refusal, stale work and settlement.** Exercise duplicate
  activation, pending gesture, observed same-source reopen and refused open,
  selection change and same-GUID child-row replacement during a preflight,
  or late completion after detach/reopen. Check a second directional and
  65th point light against real authoring validation: no newly created node,
  revision advance, history or Apply attributable to the refused request.
  Cover lost/malformed submit reply or interrupted drain, known commit with
  unavailable history or intervening revisions, and a lost Apply receipt.
  Check diagnostic GUID, no automatic retry, no history for an unverified
  create, and no stale selection or auto-apply across an observed reopen.
  Cover preservation of any previously accepted Apply receipt.
- [ ] **R3/R4 — Confirmed creation, failed projection and replay.** In a
  disposable generation, omit an as-yet-unloaded primitive artifact after
  its catalog opens, so lookup succeeds but Runtime reload fails. Verify
  the confirmed node, one history entry and last good projection remain.
  Restore the original artifact and explicitly Retry; verify the same GUID
  projects without another create. Check paused Apply and subsequent Step
  or Resume, and ordinary suspended edits awaiting explicit recovery.
  For replay, verify a full round trip of name/parent/component values and the
  same GUID, and cover external changes (e.g. a new child before Undo, a GUID
  collision before Redo, or an active-camera dependency). Check refusal
  leaves the existing document and last good projection intact.
- [ ] **R1–R4 — Actual Windows Editor at 100%.** In a disposable saved
  hand-authored scene, keep the starter camera active and start with neither
  kind of light. Use the menu to create an Empty, an explicit Empty child, a
  Camera, both lights and the three shapes. Confirm selection/Inspector,
  parent, identity, and unchanged active camera and Assets context. Separate
  shapes with ordinary edits so they can be recognized and picked; use an
  existing lit fixture or existing Runtime diagnostics to verify each light
  projects. Include a compact window check for menu/feedback. Undo and redo
  creations in ordinary history order (or as isolated consecutive actions),
  verifying each exercised undo entry is one step and redo restores the
  same identity and values. Save explicitly, compare source against the
  recorded values/GUIDs, reopen the same source and verify it with fresh
  local history. Use 0090 Build/Open on that exact saved source and compatible
  generation to open a separate Editor; check the same active camera,
  objects, transforms, light values and shape bindings without recreating or
  relinking anything.
- [ ] **Existing gates.** Build the Editor and run changed model, worker and
  UI/Runtime integration checks, including active Vulkan Validation Layers
  for the projected path. Use existing headless/golden evidence only when
  its inputs are unchanged; the new fixture's capture is functional proof.
  Evaluate Android/native build applicability from the final diff. Run the
  existing performance applicability selector on the complete final diff,
  keep the selected gate and baseline; do not update goldens or the baseline
  for this feature. S1 is documentation-only.

## Rollback and definition of done

Revert the Editor menu and preset composition if necessary. Created nodes
are ordinary scene data and remain valid. Follow the repository
[definition of done](../process/definition-of-done.md) and link applicable
final-candidate evidence in the implementation PR.
