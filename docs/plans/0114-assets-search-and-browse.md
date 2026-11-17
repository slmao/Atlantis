# Plan 0114 — Assets Search and Browse

- **Spec:** [Spec 0114](../specs/0114-assets-search-and-browse.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective and sequencing

Make asset names discoverable and list navigation predictable. Keep
preview and edit commands tied to the asset the user can act on. Design
baseline is `cb1c9f39`, the manual merge of
[PR #392](https://github.com/slmao/Atlantis/pull/392). Start S2 from the
latest main after joint review and manual design merge.

## Milestones

1. **S1 — Joint review.** Resolve J1–J4 and submit Spec/Plan 0114 for
   manual approval and merge.
2. **S2 — Implement.** Extract the current display naming into the
   Editor model. Project each accepted catalog into cached match and
   order keys, visible rows and optional headers. Wire the bounded
   Assets result view and accessible details. Keep the active asset
   distinct from a remembered hidden GUID. Fence list and detail
   publication, and coalesce unsent details using the existing worker.
   Apply the common match and default order to the reference chooser.
3. **S3 — Verify.** Run the mapped Editor checks and the Windows 100%
   acceptance against the final candidate. Record what inputs matched
   for any reused evidence and apply the repository gates.
4. **S4 — Submit.** Open a review-ready implementation PR with the
   approval, evidence, deviations and human merge conditions.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/{AssetBrowser,AssetBrowseProjection}.cs` | Shared Editor-only naming, search and sort keys, groups, selection visibility and detail intent state. |
| `src/editor_app/Atlantis.Editor/{EditorVisuals,MainWindow,AssetReferencePickerWindow}.cs` | Assets controls, rows, scroll/focus, accessible metadata and dependency navigation. Reuse the match and order rules in the chooser while preserving its guards. |
| `src/editor_app/Atlantis.Editor/EditorSession.cs` | Coalesce unsent editor detail requests per surface, retaining the existing shared lookup and worker ownership. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{AssetBrowserTests,AssetReferencePickerTests,InspectorTests,MeshPreviewTests,MeshMaterialPreviewTests,Program}.cs` | Model, existing live checks and focused tests for interaction and delayed replies. |

The planned change is managed Editor only. The shared client DTOs,
catalog wire order, runtime cache, build, packaging and graphics paths
follow their existing contracts.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Match, sort and group** | Approve one name/path/GUID search, two stable name orders, and optional type or source-root grouping in Assets (R1–R2). |
| **J2 — Selection and commands** | Preserve the GUID through projection changes. Suppress details and actions for a hidden target, restore by GUID when visible and keep dependency navigation explicit (R3). |
| **J3 — Detail and preview lifetime** | Reuse the current Runtime snapshot, fence late replies and coalesce queued details. Reuse name/path/GUID matching and default sort in the chooser with its existing confirmation rules. Confirm the no-ADR decision (R3–R4). |
| **J4 — Acceptance and authorization** | Approve the model, live and Windows 100% checks below. After manual design merge, authorize S2–S4 together. |

Suggested one-time authorization:

> Approve J1–J4 and Spec/Plan 0114 jointly, including no new ADR. After the
> design is manually merged, execute S2–S4 from the latest main, covering
> implementation, fixes within scope, applicable verification and a
> review-ready PR. Reuse valid evidence, keep Windows 100% acceptance
> and applicable repository gates, and leave final merge to a human.

## Verification checklist

- [ ] **R1–R2 — Projection.** Cover names that differ from file names,
  imported sub-keys, Windows path separators, case and whitespace,
  hyphenated and compact GUIDs, the type intersection and no matches.
  Test stable ties, source prefixes including Other, fixed group order
  and counts under both sort directions. Use a generated long catalog
  (e.g. 10,000 summaries) to confirm deterministic membership and order.
- [ ] **R2–R4 — Selection and asynchronous details.** Cover reorder with
  an unchanged selected asset, hide/reveal, explicit deselection,
  missing GUID after reload, dependency navigation and new connection.
  Delay A's detail while selecting B and A again, hiding then revealing
  the candidate, or issuing Reload view. Verify only the current answer
  can win, a visible retained GUID receives exactly one new detail
  request after reload and superseded queued requests are skipped. Cover
  empty, no-match, catalog unavailable, failed detail and disconnect
  separately. Verify local edits to the query cause no new list or
  preview call, and a pure reorder does not restart current detail work.
- [ ] **R3–R4 — Existing commands.** Verify geometry and material
  previews, stale PNG after a target is hidden or switched, and retained
  preview through pure reorder. Check that **Add to Scene** uses the
  visible mesh GUID with its existing transaction and Undo/Redo, and
  that its admitted result still settles if the view changes. In the
  chooser check typed and unspecified targets, Ctrl+click deselection,
  hidden-candidate confirmation refusal, confirmation without details,
  the captured opening mesh and existing focus and Apply recovery.
- [ ] **R1–R4 — Actual Windows, 100%.** Exercise standard and compact
  windows with real catalog assets, including repeated display names
  and long paths. Verify pinned controls, header-skipping keyboard
  navigation, focus while typing, scroll retention, hidden indication
  and exact path/GUID copying. Use a long disposable list to show that
  scrolling can reach the final result and that only visible rows and
  a bounded buffer are realized. Verify a mesh preview, Add to Scene and
  one typed chooser round trip. Retain native captures and state or
  request evidence. Reuse existing end-to-end Apply and scene persistence
  evidence where its relevant inputs match.
- [ ] **Repository gates.** Build the Editor and run affected model,
  policy, smoke and live checks. Check Vulkan validation on affected
  preview paths and reuse matching independent golden evidence when
  rendered output is unchanged. Run `atlantis_runtime --gate --applies`
  on the full implementation diff and follow the existing protocol if
  selected. Evaluate build/platform coverage from the final diff.
  S1 requires documentation, link and whitespace checks.

## Rollback and definition of done

Revert the Editor view and request handling together. The canonical
catalog, source files and published generations require no migration.
Follow the repository [definition of done](../process/definition-of-done.md).
