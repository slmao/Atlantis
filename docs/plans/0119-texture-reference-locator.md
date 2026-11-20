# Plan 0119: View and Locate Direct Texture References

- **Spec:** [Spec 0119](../specs/0119-texture-reference-locator.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective

Turn the existing affected-material query into a read-only, slot-aware Assets
workflow with a guarded jump to the material and matched slot.

## Milestones and sequencing

1. **S1 — Joint design review.** Approve the Spec and Plan together, including
   the scope and fallback decisions below. Wait for the design PR's manual merge
   before starting S2.
2. **S2 — Scan and intent model.** Extend the 0116 model and worker to build a
   bounded, cancellable scan from the accepted catalog list. Reuse the shared
   detail and full material reads. Build deterministic verified rows and explicit
   coverage and unresolved-edge states. Retire stale scan and locate responses
   with distinct intents tied to the connection and accepted list.
3. **S3 — Assets presentation and guarded location.** Replace the affected
   materials display with the View References action, virtualized results and
   status. Revalidate on activation and reach the exact slot using Assets
   navigation. Add read-only slot focus and preserve independent asset and scene
   state. Add model and Avalonia tests for the requested branches.
4. **S4 — Verification and review.** Run the checklist on the final candidate,
   record evidence and scope in the implementation PR, and submit for manual merge.

## Expected implementation files

| Files | Responsibility |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs` | Scan results, slot evidence, snapshot/intent guards and locate state. |
| `src/editor_app/Atlantis.Editor/EditorSession.cs` | Sliced reads via the SDK and fresh locate check on the existing worker. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs`, `src/editor_app/Atlantis.Editor/EditorVisuals.cs` | Assets action, results, status, guided selection and accessible read-only slot focus. |
| `src/editor_app/tests/Atlantis.Editor.Tests/AssetBrowserTests.cs`, `src/editor_app/tests/Atlantis.Editor.Tests/InspectorTests.cs` | Model coverage and Editor routing, focus and late-reply regressions. |
| `docs/specs/README.md` | Link completed implementation and acceptance evidence once merged. |

Dependencies are the landed 0081 read-only catalog, 0101 combined material
projection, 0114 Assets browse UI, 0116 dependent scan and 0118 reference slot
and completeness precedents. Start on current main after design merge. No
native, shared client, package, renderer or build changes are planned. Apply
repository rules for any file-list adjustment.

## Joint decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope** | One visible catalog texture and all direct catalog Material users in the attached Runtime snapshot. Reuse the 0116 roster and dependency lookup, and the complete shared recipe to resolve five known slot names (R1). |
| **J2 — Coverage** | Reserve the empty claim for a completed check. Keep known rows and distinguish unresolved edges, incomplete coverage and unavailable catalogs. Keep source-proof safety under 0118 (R2). |
| **J3 — Locate** | Revalidate by GUID and exact slot, reject stale intents, then reveal the material and focus its read-only slot in Assets. Keep sort/grouping and preserve user state on refusal (R3–R4). |
| **J4 — Verification and authorization** | Approve the evidence below, including Windows 100% with shared Base Color/Emissive GUID and stale results. Record no new ADR for this client composition (Architectural Impact). |

Suggested authorization after joint approval and manual design merge. Execute
S2–S4 from latest main, including implementation, scoped fixes, applicable
verification, valid evidence reuse and a review-ready PR. Final merge remains
with the maintainer.

## Verification checklist

- [ ] **R1.** Build the Editor. Verify a single material with a shared Base
  Color and Emissive texture yields two independently keyed rows from one
  catalog edge. Include a second material and a result hidden by Assets filters.
  Cover the remaining slot mappings, deterministic order and no duplicate row
  for a repeated detail. Reject malformed or inconsistent identity/dependency
  evidence instead of guessing the slot.
- [ ] **R2.** Cover complete empty, complete with results, incomplete with zero
  valid rows, incomplete with verified rows, known edge with unresolved slot,
  unavailable catalog and unsupported full projection on an older host. Test
  readable imported or unmanaged entries where representable. Verify that a
  positive unresolved edge is not presented as a navigable slot or proof for
  removal. Exercise failure, disconnect and stale response paths.
- [ ] **R3–R4.** With controlled callbacks, let a scan finish, change the requested
  slot while leaving another slot on the same texture, then activate the old row.
  Verify refusal with no selection/filter/scroll/focus change. Test response
  crossing another Locate, selection away and back, asset hidden, reload and
  session change. Verify responsive slices, exact GUID reveal even with repeated
  labels, long-list virtualization and keyboard focus on the requested slot.
  Ensure the jump does not start any writes, create a draft or affect scene
  selection, history, Apply or current Runtime state.
- [ ] **R1–R4, Windows 100%.** Open Assets against a named generation with one
  texture used by Base Color and Emissive in the same material. Search/filter
  Assets to the texture, run View References, then on separate runs activate
  both rows and capture the material selection and focus for each slot. Include
  a successful complete-empty query. With a second generation where one of the
  two slots changed, restart/reconnect explicitly. Show that the old result is
  retired and the new query contains only the current slot. Retain the controlled
  activation-race evidence above for the case where a fresh read detects the
  changed binding before navigation. Record the two generation IDs, native
  screenshots, and unchanged scene/edit and material state during navigation.
- [ ] **Existing gates.** Run applicable Editor/model checks, including the
  existing affected-material browsing and material-editor regressions. Run live
  Windows checks with clean Vulkan validation. Review final diff applicability
  for image/golden, platform and performance checks. Record `--gate --applies`
  and follow the existing gate protocol if selected. Windows 100% is the only
  DPI acceptance target.

## Rollback and definition of done

Revert the Editor/model change to restore the 0116 affected-material view.
There is no migration or persisted feature state to unwind. Meet the repository
[definition of done](../process/definition-of-done.md) and link evidence from the
implementation PR.
