# Plan 0142 — Static Mesh Thumbnails in Assets

- **Spec:** [Spec 0142](../specs/0142-assets-mesh-thumbnails.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer, 2026-11-23, [joint approval of `fc5c7974`](https://github.com/slmao/Atlantis/pull/447#issuecomment-7105301892); J1–J4 and S2–S4 authorized after maintainer merge as `a53e111b`.
- **ADR:** [ADR-0190](../adr/0190-editor-mesh-thumbnail-admission-and-cache.md) (Accepted)
- **Design baseline:** `0372c4a3`

## Objective

Make the static mesh catalog visually scannable while keeping preview calls
bounded and keeping existing Asset actions responsive and independent.

## Milestones

1. **S1 — Joint review.** Decide J1–J4, approve the exact documents and
   await maintainer merge.
2. **S2 — Admission and cache.** Extend the current preview admission,
   cover stale delivery and failure policy, and implement bounded Editor
   thumbnail ownership using the existing SDK.
3. **S3 — Virtualized list.** Observe visible rows after layout, bind
   thumbnails only to current rows and connect view/screen invalidation.
   Exercise existing search, selection, drag and manual preview.
4. **S4 — Verify and submit.** Complete the directed checks and Windows
   100% Bistro acceptance, then submit a review-ready implementation PR
   with evidence and the applicable gate results.

## Files and modules touched (expected)

| Area | Files and work |
| --- | --- |
| Editor model | `src/editor_app/Atlantis.Editor.Model/AssetBrowser.cs` for shared preview admission and a focused `AssetThumbnails.cs` in the same directory for demand, cache metadata and policy. |
| Editor view | `src/editor_app/Atlantis.Editor/MainWindow.cs`, `EditorVisuals.cs` and an Assets partial `MainWindow.AssetThumbnails.cs` beside them. Scope thumbnail presentation to the main catalog list, sharing existing virtualization and viewport geometry. Own scaling, binding and disposal here. |
| Verification | Add focused tests in `src/editor_app/tests/Atlantis.Editor.Tests/` and use the pinned Bistro assets on Windows. Record the exact catalog GUIDs and capture conditions in the implementation PR. |
| Architecture reference | Update the Editor preview admission note in `docs/architecture/module_boundaries.md` with the as-built consumer. |

## Sequencing and dependencies

Reuse [0091](../specs/0091-static-mesh-preview.md) for geometry and failure
decoding, [0114](../specs/0114-assets-search-and-browse.md) for catalog
projection and virtualization, and [0134](../specs/0134-project-assets-explorer.md)
for the current catalog and source surfaces. Existing asset drag behavior
remains the acceptance path, including its current item identity checks.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and presentation** | Main Assets list, visible static meshes, fixed icon/thumbnail slot and the existing geometry preview. |
| **J2 — Admission** | Accept ADR-0190. One shared slot with manual priority, 200 ms dwell, 500 ms minimum spacing between automatic sends, and stale-intent checks before send and delivery. |
| **J3 — Cache and failure** | At most 128 records and 2 MiB of retained pixels. Scope reuse to the Runtime and catalog epoch. Clear on reload or Runtime change. Keep per-row fallback and require new visible demand for transient retry. |
| **J4 — Authorization** | After exact Spec/Plan/ADR approval and maintainer merge, authorize S2–S4 together, including in-scope fixes, applicable verification and the review-ready implementation PR. Maintainer retains final merge. |

## Verification checklist

- [ ] **R1 — Virtualization and input.** With a long mixed catalog, exercise
  clipped and recycled rows, partial visibility, groups, reordering, resize
  and tab changes. Check that hover and thumbnail paint preserve row
  selection, keyboard navigation, scroll anchor and original drag GUID.
  Verify fixed row metrics for icon, loading, ready and failure states.
- [ ] **R2 — Scheduling and stale output.** Use controlled replies and clock
  checks for the 200/500 ms budgets, one active slot, and a stationary view
  with no new work after settlement. Hold setup and reply across fast scroll,
  name/type filter, sorting, switching surface and disconnect/reconnect.
  Recycle one container from mesh A to B. Change Runtime while reusing A's
  GUID. A late A result cannot show in the recycled row or enter the new
  cache epoch. Verify manual preview preempts unsent demand and follows a
  sent thumbnail without automatic pacing. Check that material/draft
  previews keep their existing behavior.
- [ ] **R3/R4 — Cache and failure.** Verify cache hits cause no request,
  reload and Runtime replacement retire cached images, an inactive
  publication keeps the running cache eligible, and volume beyond 128
  records respects eviction, pins, release counts and the pixel budget.
  Exercise Busy, old host Unsupported, unavailable catalog, invalid mesh,
  malformed transport result and bitmap decode failure. Verify bounded
  re-entry retry and no retry while standing on a failed row. Confirm the
  editing/catalog connection remains usable after preview transport failure.
- [ ] **Windows 100% — Bistro props.** Use the pinned Bistro catalog on a
  hardware-backed Runtime. Record distinct cooked mesh GUIDs for at least
  three recognizable props, such as a chair, an awning and a streetlight.
  Search or group the list, wait for the visible thumbnails and capture the
  native window. Confirm the geometry stills distinguish the props and
  match their explicit geometry previews with fixed framing. Scroll the
  props out and back, and use request counts to prove valid repeats are
  cache hits. During rapid scroll and filter, confirm the row label and
  thumbnail still agree. Switch Runtime and confirm fresh results are tied
  to its own catalog and outstanding older output is ignored. In a
  disposable preview-failure run, search for, select and drag the affected
  asset while the failed thumbnail shows a fallback. Retain 100% screenshots,
  GUIDs and focused request/cache evidence with reproducible inputs.
- [ ] **Applicable gates.** Build the Windows Editor and run affected
  model/UI checks. Run the exercised Vulkan preview path with validation
  and check the existing geometry preview golden using valid evidence or
  an applicable run. Take the performance applicability verdict on the
  final diff. Record idle and active request behavior with the functional
  evidence. The present design PR needs only document/link/whitespace checks.

## Rollback plan

Remove the thumbnail consumer and storage from the Editor. Continue using
the existing virtualized type-icon rows and explicit previews.

## Definition of Done

Use the [repository definition of done](../process/definition-of-done.md).
Record acceptance evidence and deviations in the implementation PR.
