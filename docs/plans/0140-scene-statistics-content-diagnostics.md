# Plan 0140 — Scene Statistics and Content Diagnostics

- **Spec:** [Spec 0140](../specs/0140-scene-statistics-content-diagnostics.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** 2026-11-22, maintainer approval of [e17cb1dc](https://github.com/slmao/Atlantis/pull/443#issuecomment-7079327582) approved Spec/Plan and J1–J4, authorizing S2–S4 after merge as d4ef16a2.
- **ADR:** [ADR-0188](../adr/0188-on-demand-cooked-content-metrics.md) (Accepted)
- **Design baseline:** 67b40518

## Objective

Give a Bistro author a bounded, on-demand report for a scene or a selected
storefront, and let them locate a confirmed missing direct binding without
altering the document or Runtime.

## Milestones

1. **S1 — Joint review.** Review the metric definitions, shared read,
   incomplete evidence and locate guard. Start S2 only after approval
   and maintainer merge of the design.
2. **S2 — Shared metadata.** Reuse Asset System layout checks for a header
   and file length, then wire the bounded AssetHost request through
   Connection, Remote, CLI, SDKs and MCP. Verify schema handling,
   authoritative catalog refusals and compatibility before UI work.
3. **S3 — Editor view.** Compose the existing document/schema/catalog
   reads on demand. Present scope, coverage and historical frame evidence
   separately. Add the guarded missing-field locate using the existing
   Hierarchy and Inspector reveal path. Test supersession before delivery.
4. **S4 — Verify and submit.** Run the final applicable checks and
   Windows 100% acceptance below. Record evidence with its source/input
   provenance and submit a PR for human review and merge.

## Files / modules touched (expected)

| Area | Scope |
| --- | --- |
| Asset System mesh and texture artifact readers and their tests | Factor checked header and logical file length validation from existing decoders. Keep full-decode payload checks and cooked schemas intact. |
| Runtime AssetHost and focused runtime asset tests | Add the catalog-bound header read. Reuse artifact path protections and keep the main render path untouched. |
| Asset Connection, Remote asset dispatch/client/codec, CLI asset command, native and C# SDK asset surface, MCP asset tool and associated client tests | Add the same bounded, read-only operation, response correlation, and old-host refusal. Update [Remote protocol](../architecture/remote_protocol.md) with the as-built contract. |
| Editor MainWindow, EditorSession, Bistro capture result hand-off, one Scene Statistics view and its model/tests | Add the on-demand command, aggregation, coverage labels, one frame summary, and guarded reveal of an existing Inspector field. No changes to authoring transactions or saved workspace state. |
| Disposable Windows Bistro fixture and evidence | Verify the two scopes, exact byte counts, direct missing-reference path and recovery. Keep shipped Bistro assets and reference images unchanged. |

## Sequencing and dependencies

Reuse [0081 catalog snapshot](../specs/0081-asset-browser-shared-interface.md),
[0083 Apply outcomes](../specs/0083-authoring-apply-outcomes.md),
[0113 Hierarchy locate](../specs/0113-hierarchy-search-and-filter.md),
[0131 frame report](../specs/0131-bistro-reference-views.md), and
[0138 guarded slot navigation](../specs/0138-viewport-material-slot.md).
Reuse an already loaded complete catalog list where available. An
alternative is the existing asset detail query with one read per distinct
GUID and bounded pipelining. Only authoritative catalog absence can
support a missing-reference row. A missing report should never trigger
a step as a fallback.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and metric definitions** | Whole authored scene and one selected subtree. Keep the content estimate and a historical whole-scene Runtime frame report visibly separate. Compute indexed triangles per Renderable and cooked texture bytes per unique GUID. |
| **J2 — Shared read and budgets** | Accept ADR-0188's additive GUID batch, no payload reads, old-host degradation and no hot-path collection. Approve the bounded document scan and partial results. |
| **J3 — Missing binding and location** | Limit actionable diagnostics to proven missing direct mesh or material GUIDs in the running catalog. Include known rows under incomplete coverage. Require latest token, scope, occurrence, field and absence checks before revealing. |
| **J4 — Implementation authorization** | After approving the exact design revision and maintainer merge, authorize S2–S4 once for implementation, in-scope fixes, the checks below and a review-ready PR. The maintainer decides final merge. |

## Verification checklist

- [ ] **R1 — Scope, lifecycle and read-only behavior.** Verify whole
  document, nested selected root including its own Renderable, empty and
  multiple selection, hidden/collapsed descendants, and no open document.
  Cover a limit hit, partial component/property reads, timeout, disconnect
  or stale revision, late replies and overlapping refreshes. An over-limit
  hierarchy must not produce a complete subtree count. Observe no periodic
  traffic with the view idle. Verify no Step, Capture, Apply, command,
  Save, or history changes from opening, refreshing or locating.
- [ ] **R2 — Content evidence.** Exercise mesh v4/v5 and RGBA8/BC7
  texture v3 with short and full mip chains. Compare exact counts to
  separately decoded fixture artifacts. Include repeated mesh instances
  and a single texture reused in multiple slots and materials. Verify
  closed schema/type refusal, malformed header, size/offset mismatch,
  integer limits, path protection, file change and incomplete reads.
  Header-only evidence may not claim vertex or pixel validation. Verify
  consistent batch results and old/new compatibility through the native
  wire, CLI, C# SDK and MCP. Confirm the old host still supports ordinary
  authoring and asset reads.
- [ ] **R2 — Frame provenance.** Compare the displayed frame number and
  draw-item count to a previously captured 0131 raw frame report. Test
  unavailable, stale projection and current historical sample. Confirm
  no subtree count is copied from a whole-scene capture. Temporary
  visibility policy does not change authored scopes or relabel the
  frame report as a visible-subset measure.
- [ ] **R3 — Direct references.** Test a missing explicit mesh and
  material, valid fallback, a known valid GUID with an unreadable
  artifact, wrong asset type, nested texture absence and catalog
  unavailability. Incomplete evidence gives a reason and retains known
  rows. Test stale result after field change, document reopen, scope
  change and same-GUID node replacement. Check the actual node and
  field focus only after revalidation and preserve an active draft or
  gesture when refused.
- [ ] **Windows 100% — Bistro.** Use the pinned full Bistro street and
  a named storefront subtree. Open the readable authoring document and
  capture the established view separately with 0131. Refresh both
  scopes, record each sample's context and raw reference values, and
  retain native screenshots. Confirm a full street scan, compare both
  node and triangle totals against independent document and artifact
  counts, and prove shared texture GUIDs are counted once per scope.
  Use an otherwise valid disposable editable Bistro storefront scene
  with a deliberately shared texture to prove the deduplication delta
  explicitly. Validate the healthy scene gives no direct false positives.
  In that disposable scene, commit an absent non-nil mesh or material
  GUID through the existing authoring transaction and explicitly Apply
  to produce a failed reload. Keep the earlier projection usable. Refresh
  and locate the affected storefront node and exact Inspector field.
  Restore the binding using the ordinary edit path and explicitly Apply,
  then refresh to clear the issue. In the disposable run, withhold one
  binding or catalog read and verify **Check incomplete** replaces the
  empty direct-check result. Withholding only a cooked artifact should
  leave independently verified direct-reference coverage intact.
- [ ] **Applicable gates.** Run affected Asset System, Runtime, native and
  managed client and Editor checks. Verify Vulkan validation and any
  applicable headless or image checks on the exercised paths, without
  changing goldens. Check Android build and packaging applicability for the
  shared native changes. Run the performance applicability check on the
  final diff and preserve the existing performance protocol if selected.
  Cite exact inputs and revisions for reused results. This review PR needs
  document link, status and whitespace checks.

## Rollback plan

Remove the optional Editor view and additive request together. Existing
queries and capture continue to work. No persisted state or content
migration is required.

## Definition of Done

Use the [repository definition of done](../process/definition-of-done.md).
Implementation evidence belongs in the implementation PR.
