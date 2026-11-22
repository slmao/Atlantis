# Plan 0136 — Viewport Visibility and Picking Controls

- **Spec:** [Spec 0136](../specs/0136-viewport-visibility-picking.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending.
- **ADR:** [ADR-0185](../adr/0185-hosted-viewport-presentation-policy.md) (Proposed)
- **Design baseline:** `45a6018b`, maintainer merge of [#434](https://github.com/slmao/Atlantis/pull/434)

## Objective

Unclutter the hosted viewport so a user can pick through a hidden awning or a
visible locked ground, isolate current subjects, and restore the usual view.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Decide J1–J4 on the exact Spec, Plan and ADR
   revision. Implementation begins from current `main` after maintainer merge.
2. **S2 — Shared policy and Runtime.** Add the bounded control operations,
   single owner and frame admission. Implement optional filtered extraction
   and opt-in picking over existing CPU geometry. Verify generation, entity
   incarnation, output separation and backward compatibility.
3. **S3 — Editor.** Add Hierarchy actions, row indicators, viewport isolation
   and restore. Reuse the hierarchy and projection guards and wire all
   view-relative surface consumers through the same admission state.
4. **S4 — Verification and PR.** Run final-candidate Windows 100% Bistro
   acceptance, shared-client and applicable GPU/performance checks. Retain
   the evidence and submit a review-ready implementation PR.

## Files / Modules Touched (expected)

| Area | Changes |
| --- | --- |
| `src/connection/include/atlantis/connection/runtime_control.h`, adjacent value headers, `src/remote/src`, `src/cli/src`, `src/csharp/Atlantis.Gameplay`, `src/mcp` and corresponding tests | Add typed policy status/replacement and opt-in pick with strict, bounded Remote shapes and compatibility. Record the actual wire shape in the existing Remote protocol reference. |
| `src/runtime/main.cpp`, `src/runtime/{src,include/atlantis/runtime}` control host, application, scene extraction and scene picking, and `tests/runtime` | Use existing hosted-window admission and frame order. Pin baked entity incarnations and apply a single optional draw/pick exclusion without changing ordinary capture assembly. |
| `src/editor_app/Atlantis.Editor` window partials, `EditorSession.cs`, `Atlantis.Editor.Model` and corresponding Editor tests | Temporary actions, view policy model, tree expansion, gesture cancellation, overlay and status. Reuse the existing placement, alignment, focus and history boundaries. |
| Existing Bistro and disposable scene fixture inputs | Add identifiable fixture positions only where necessary to establish the prescribed rays and occlusion. No tracked Bistro asset change. |

## Sequencing & Dependencies

S2 establishes policy and opt-in pick semantics before Editor use.
[Spec 0066](../specs/0066-editor-overlay-and-input.md),
[Spec 0111](../specs/0111-viewport-camera-light-guides.md),
[Spec 0128](../specs/0128-viewport-mesh-placement.md), and
[Spec 0135](../specs/0135-viewport-surface-alignment.md) provide the existing view aid
and surface contracts. Record the concrete implementation file list at S2.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Editor semantics** | Adopt separate manual hide, pick-lock and isolation sets. Freeze selected subtrees on entry, preserve manual state on exit, and clear all three with Restore All. Keep Hierarchy access and suppress ineligible Gizmos. |
| **J2 — Ownership and output** | Accept ADR-0185. Use one connection-owned policy on an already hosted viewport, shared explicit filtered queries, and unfiltered normal output and existing captures. Reuse the existing trust model. |
| **J3 — Projection and recovery** | Require the confirmed projection, bounded membership, baked entity incarnations, expected generation and revision, and a presented revision before picking. Clear on scene swap or document/session change. Preserve the previous state on refusal and reconcile uncertain receipts without retrying writes. |
| **J4 — One implementation authorization** | On joint approval and maintainer merge, authorize S2–S4 once, including in-scope fixes, affected tests, real Windows 100% acceptance, applicable gates and a review-ready PR. Final merge remains with the maintainer. |

## Verification Checklist

- [ ] **R1 — Editor model.** Hide node versus subtree, overlapping roots,
  empty selection, selection change after isolation, manual hides and locks
  before and during isolation, Exit Isolation, and one-step Restore All.
  Include same-name nodes, Hierarchy search, guides, markers and mixed
  eligible/ineligible Gizmo selection.
- [ ] **R2–R3 — Shared clients and settlement.** Use two authenticated clients
  on one embedded Runtime. Test no takeover, ordinary raycast/alignment and
  captures during an owner filter, release and reconnect, unhosted or old host
  compatibility, exact max and exceeded request bound, malformed values,
  stale revision and generation, lost receipt, same-GUID replacement,
  frame-not-presented, swap, and an earlier pick reply arriving after
  Restore All. Verify both mesh placement and alignment use the filtered
  set and reject expired previews without writing. Verify no inactive
  frame scan and preserve ordinary pick results and draw ordering.
- [ ] **Windows 100% — Bistro storefront.** Record native screenshots and
  the identities along fixed rays using the real hosted Editor.
  1. At a fixed camera and pixel where the front awning is hit over a
     chair, record the baseline. Hide the awning mesh or its Hierarchy
     subtree. At the same pixel the awning is absent and the chair is
     selected. If using the subtree, check a sibling outside it stays visible.
  2. Fix a ground-hit pixel. Lock the ground in the Hierarchy. At that
     pixel the ground still draws, the viewport click cannot select it,
     and a second client's ordinary raycast still returns the ground.
     Use a sightline past a raised edge or a disposable fixture to also
     verify that an eligible farther mesh wins when one exists. Retain
     marker and Gizmo checks in the focused suite.
  3. Isolate the chair or a small multiselection that includes the locked
     ground. Exit isolation. Verify the earlier awning hide and ground lock
     still hold and other objects return. Run Restore All once, then
     re-pick the awning and ground with the same rays.
  4. Compare authoring revision, dirty state, history and Apply receipt
     across the view actions. Reapply a hide or lock and save while it
     is active. Verify the save has the same scene contents as a save
     before the policy and the current view still honors the setting.
     Reopen and verify normal display and picks. Compare ordinary
     second-client captures at the same settled pose during the policy
     and after restore. Check successful Apply clears the policy and a
     failed Apply that preserves the projection keeps it.
- [ ] **Applicable gates.** Run affected Editor, SDK, CLI, Remote, MCP and
  Runtime checks. Run Vulkan validation on the shown path and headless
  image regression. Apply human golden review to any image change,
  evaluate the shared Android build and package, and query
  `--gate --applies` on the final diff. Retain the original formal performance
  result when applicable. Record the candidate and evidence provenance.

## Rollback Plan

Retire the opt-in presentation and Editor actions. The ordinary display,
pick, capture and saved-scene contracts remain usable without migration.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md). The
implementation PR cites J1–J4 approval, real Windows evidence and the
applicable gate results.
