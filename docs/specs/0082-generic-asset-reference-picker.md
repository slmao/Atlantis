# Spec: Generic Asset Reference Picker

- **Status:** In Review
- **Author:** Codex, at slmao's direction
- **Created:** 2026-11-07
- **Related Plan(s):** [Plan 0082](../plans/0082-generic-asset-reference-picker.md)
- **Approval:** Pending joint Spec/Plan review; scope direction is not implementation authorization.
- **Related ADR(s):** None new — see Architectural Impact; reuses ADR-0125, ADR-0135/0136 and ADR-0143.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Add a generic single-asset chooser beside an eligible Inspector reference field.
The chooser reads the existing cooked catalog through the shared C# SDK, then
submits the chosen GUID through the ordinary Inspector edit/apply/history path.
It removes the copy/paste step without adding an asset assignment service or a
promise that a listed asset is compatible with the destination field.

## Motivation / Problem Statement

Baseline: main `882c691e83141ffaccff622557a8f7486ba5ec9b`, the human merge of
[PR #326](https://github.com/slmao/Atlantis/pull/326). Spec 0081 delivered discovery
to Editor, CLI, both SDKs and MCP; reference edits still require typing or pasting
a GUID. Existing schema flags identify asset references but do not identify the
required asset type. A small presentation addition can reuse both delivered
surfaces without inventing editor-specific engine capabilities.

## Goals

- Choose a catalog GUID directly for an existing editable reference field.
- Preserve field identity across asynchronous browsing and confirmation.
- Retain the same validation, apply, conflict, undo/redo and persistence behavior
  as an ordinary reference edit made by any existing client.

## Non-Goals

Target-type metadata, inferred type constraints, compatibility/loadability checks,
preview or thumbnails, drag-and-drop, bulk assignment, asset creation/editing,
import/reimport/build management, source-file access, live catalog refresh,
pagination, new wire methods/tools, custom Inspector plugins and changes to the
frozen in-process editor are excluded. No new transaction, history or apply policy.

## Requirements

- **R1 — Generic field eligibility.** Offer Choose for an editable Inspector
  field whose schema kind is Primitive, primitive is AssetGuid and flags include
  AssetReference. Use the delivered authoring schema, not component names, field
  names, numeric IDs or a mesh/material allow-list. Ordinary GUID text entry and
  existing optional-field clear remain available. Choosing for an absent optional
  field sets a value; Cancel never clears it. Other fields keep their current UI.
- **R2 — Bounded chooser interaction.** One owner-modal chooser may be open per
  editor window. Show the target field label and its current GUID or absent
  state, and a flat single-selection list with type, source and GUID. Reuse
  Spec 0081's local type filter, ordinal case-insensitive source/GUID search,
  metadata detail and direct-dependency inspection. Filters start at All/empty;
  users choose a type filter explicitly. Inspecting a dependency selects it as a
  candidate but never commits it. Preselect the current GUID if listed; otherwise
  show that it is outside this snapshot without erasing or inventing a record.
  Confirm is an explicit button enabled only for a selected record in a complete
  successful current list, not merely text entered into search. Double-click,
  opening, filtering, selection and detail completion do not commit. Cancel,
  Escape and window close discard the candidate. Browsing does not change the
  Assets tab's filters/selection, Hierarchy, authoring document or history.
- **R3 — Existing query and snapshot semantics.** Query only `assets.list/get`
  through the existing worker-owned C# SDK session. Keep the chooser's browser
  state separate from the Assets tab while reusing its UI-free AssetBrowser
  behavior. UI mutation stays on the dispatcher. Load on opening; no polling or
  request per keystroke. Retain the first-query snapshot/restart notice and
  explicit Reload view, preserving a still-listed candidate and re-requesting its
  detail even if the prior detail was pending. Loading, empty catalog, no match,
  CatalogUnavailable, UnknownAsset detail and disconnect remain distinguishable;
  errors are never a successful empty/partial list. A failed/pending list disables
  Confirm. Detail failure is displayed separately and does not itself disqualify
  a GUID still present in the successful list: detail is inspection, not an asset
  validator. Session, chooser-instance, list and detail request guards reject old
  replies, including after cancel/reopen or Reload. Reload never rereads disk,
  retries failed catalog initialization or changes Spec 0081's transport limits.
- **R4 — Target binding and stale choices.** Opening captures the worker/session,
  current document-open lifetime, selected node, component/field address and the
  field's observed value including absence. Confirm must not redirect a path to
  the then-current selection. Invalidate the chooser on session end/disconnect,
  document reopen, target selection change, target deletion/component removal,
  lost eligibility, or an observed target-value/presence change; display the reason
  and require reopening. Before editing, the same worker checks that captured
  identity/lifetime is still valid, refreshes the target through existing document
  queries and compares value/presence. A failed check refuses with no edit/apply or
  history entry. Do not add a wire revision, lock or compare-and-set operation:
  this protects against stale local work and changes observed before submission,
  not a new atomic guarantee against another client writing between the final
  query and transaction. Existing server ordering and conflict/history contracts
  continue to govern that race. Applied scene-generation changes alone are not a
  document reopen.
- **R5 — Ordinary edit and settlement.** A valid Confirm consumes the chooser
  request once, disables duplicate submission and closes the chooser. Selecting
  the unchanged value produces no transaction, apply or history entry. A changed
  GUID goes through the existing Inspector planning and worker Edit path: one
  SetProperty in one authoring transaction, its normal history record, and the
  existing coalesced automatic apply. No direct World edit, preview, independent
  history or asset assignment command. Validation/transaction refusal uses the
  ordinary visible edit notice and records no successful edit. A committed edit
  whose later apply fails remains an ordinary committed document edit with the
  existing apply error/history behavior; do not silently revert it or claim the
  Runtime changed. Pause/Step/Resume, Undo/Redo, external-overwrite conflict, Save,
  imported-save refusal and history clearing on DocumentReopened/session end
  retain their existing semantics. Catalog presence/type is a display fact,
  neither a field-type constraint nor a guarantee of compatible rendering.
- **R6 — Shared boundaries and delivery.** CLI, native SDK, C# SDK and MCP already
  compose the same discovery and SetProperty/apply operations; this UI adds no
  engine operation or capability. Do not change public Connection/SDK/wire/schema
  contracts, generated bindings, dependencies, packages, build/CI behavior, asset
  formats, Runtime or rendering. Complete Plan 0082's mapped checks, retain
  applicable gates and reuse only evidence with unchanged relevant inputs.

### Non-functional

Keep the existing .NET/toolchain/package pins, Windows editor hosting and
UI-free model boundary. Use the existing single session worker and owned result
values; no new thread or scheduler. User filters operate on the complete returned
list under Spec 0081's current-content bounds; no arbitrary-catalog latency,
unlimited-size or new performance threshold is promised.

## Proposed Design

A small chooser window composes AssetBrowser and a UI-free target/candidate state
model. MainWindow passes immutable target values at launch; EditorSession handles
the guarded confirmation on its existing queue and feeds the existing Inspector
plan to Edit. Request tokens are local lifecycle guards only. Opening the modal
from a reference field must not accidentally commit a pending text draft through
LostFocus: discard that field's uncommitted draft and capture its authoritative
observed value for the chooser. Other text-edit behavior remains unchanged.

## Architectural Impact

**None.** This is a new UI workflow inside the existing Editor App/Model ownership
and worker boundaries. No shared public API, module boundary, dependency,
threading, data format or engine ownership changes. It reuses
[ADR-0125 D2](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md),
[ADR-0135](../adr/0135-two-document-edit-and-apply.md),
[ADR-0136](../adr/0136-structural-edit-records.md) and
[ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md).
ADR-0143's "no picker" describes Spec 0081's bounded delivery, not a ban on a
later GUI composition of its queries. This proposal extends that product scope
without changing its architectural decision. No new ADR is proposed; if review
requires target-type metadata or a new concurrency guarantee, revise the design
and identify the applicable ADR before implementation.

## Alternatives Considered

- Keep copy/paste only: already works, but leaves a removable context switch.
- Infer target type from field names: embeds an unreviewed semantic table in the
  editor; target-type metadata is explicitly deferred.
- Add a typed assignment endpoint or GUI filesystem reader: duplicates existing
  operations and breaks the ordinary-client boundary.
- Share the Assets tab's mutable selection: couples two independent workflows.
- Add atomic conditional editing: a new shared contract, beyond this local UI slice.

## Testing & Verification Plan

Plan 0082 maps R1–R6 to headless chooser/Inspector models, actual Windows
window/worker checks and real Runtime reference-edit integration. Exercise the
adverse callback and stale-target sequences, not just a completed happy path.
Preserve the existing save→reopen→rebuild→restart reference acceptance and clean
Vulkan verification through relevant reruns or exact-provenance reuse. This
design-only PR runs documentation checks; it claims no new implementation result.

## Risks & Open Questions

Generic candidates can include the wrong asset kind; display this limitation and
retain existing refusals rather than imply compatibility. The pre-submit check
does not provide atomic cross-client conditional editing. Modal focus can trigger
existing LostFocus handlers; the launch regression is required. Joint review must
rule on the interaction, stale-target boundary and proportional acceptance in the
Plan; implementation authorization remains pending.

## Out of Scope / Future Work

The Non-Goals stay separate reviewed work. Target-type annotations could later
improve filtering for every client, but are not scaffolded here. This round stops
at a reviewable joint design PR, with no code implementation or automatic merge.
