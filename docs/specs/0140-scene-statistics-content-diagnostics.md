# Spec 0140 — Scene Statistics and Content Diagnostics

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0140](../plans/0140-scene-statistics-content-diagnostics.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0188](../adr/0188-on-demand-cooked-content-metrics.md) (Proposed)
- **Design baseline:** 67b40518, maintainer merge of [PR #442](https://github.com/slmao/Atlantis/pull/442)

## Summary

Open a read-only Scene Statistics view for the whole Bistro street or one
selected storefront subtree. Show authored content counts, reuse a captured
Runtime draw-item count when available, and let a user go from a missing
direct mesh or material reference to its existing Inspector field.

## Motivation

The Editor can read authoring nodes and fields, query the asset catalog and
its dependencies, and capture a frame report. These describe different
things. The report reconstructs the whole Runtime scene's capture draw list,
including when the hosted viewport has temporary visibility controls.
It gives no per-node draw list, triangle count or texture byte total. A
failed Apply can leave a newer document and a still usable older Runtime
projection. The view should let the user inspect both without conflating
them or guessing the cause of a generic Apply failure.

## Goals

- Compare the whole open authoring document with a selected node and all
  its descendants, with the scope and source beside each result.
- Expose useful triangle and texture figures from supported cooked artifacts.
- Identify a known missing direct reference and reveal its exact field.

## Non-goals

GPU residency, device allocations, pixels drawn, visibility or per-pass draw
cost, viewport triangle counts, project-wide checks, source parsing outside
the open document, automatic repair, and diagnostics that walk nested
dependency chains. Asset import, scene format, and render behavior are
outside this change.

## Requirements

### R1 — Select the document scope and read it on demand

Add a modeless **Scene Statistics** entry in Editor. Offer **Whole scene** and
**Selected subtree**. The latter needs one current Hierarchy selection and
includes the root and all descendants, even when collapsed, filtered or
temporarily hidden. An empty or multiple selection makes subtree statistics
unavailable with guidance to choose one node. A selected root's own
Renderable counts once. Whole scene counts nodes in the open authoring
document and excludes Runtime-only entities. Read imported documents through
the same established queries when available. A Runtime with no open
authoring document can still show an available prior frame report.

Open or change scope to set context. **Refresh** explicitly reads that
context. Use the existing Editor Worker, document queries, and schema.
Bracket the reads with the document identity and revision, and associate
them with the current Remote session and the frozen catalog selected by
that Runtime. Show the scope name and GUID when applicable, document
revision, and catalog context. Name the active named generation when
known; use **Running Runtime catalog** when no named generation is known.
A published generation pending restart is outside the running catalog.

A refresh handles up to 8,192 nodes and 4,096 distinct referenced asset
GUIDs, with a 30-second overall read budget. Read the document summary before
a full node request, and bound the existing property and asset reads.
An authoritative whole-document node count can still be shown if its
node scan is over budget. Never infer a complete subtree count from an
incomplete hierarchy. Label coverage separately for node counting, direct
reference checks, and each statistics total. Show a known subtotal only
when correctly labelled; a missing sample is unavailable, not zero.
Selection changes in subtree scope, document changes, a reconnect, closing
the view and a newer refresh retire superseded work. Mark retained old data
as stale and disable its actions. Use existing observations and
subscriptions for invalidation. The view introduces no scheduled reads,
per-frame collection, capture, or stepping.

### R2 — Show source, units and coverage

| Display | Definition |
| --- | --- |
| Nodes | Count of authored nodes in the scope. |
| Estimated content draw items | One candidate per Renderable whose mesh and explicit material, if any, resolve to the expected catalog types. An unassigned optional material uses the normal fallback. Exclude known invalid bindings and show their coverage separately. This is an estimate of content draw items; Runtime readiness, visibility and per-pass draws are outside the estimate. |
| Indexed triangles | Sum of validated cooked mesh index counts divided by three, once per in-scope Renderable with a readable supported mesh. Shared mesh instances each contribute. This is an authored geometry total, independent of material readiness and Runtime rendering. |
| Unique cooked texture payload | Byte sum over the distinct texture GUIDs on the direct catalog dependency edges of in-scope explicit materials. Include all stored mip levels and the actual format's packing, across RGBA8 and BC7. Count a GUID once across nodes, material assets and slots within a scope. Exclude header and sidecar bytes, Runtime fallback assets, environment maps outside these edges, and allocations. This exact cooked-byte figure is a content-size proxy for texture usage. Whole scene and subtree totals overlap and cannot be added. |
| Runtime frame report draw items | The existing draw-item count from the most recent successful 0131 capture in this Editor session, with its frame number, Runtime scene and projection generation. It describes the full capture assembly, even if hosted visibility differs. It is neither a submitted draw-call count nor a selected-subtree value. Keep it separate from document metrics and label an earlier projection; when correlation cannot be established, label it as a historical sample. |

The report consumes the existing capture result only. A statistics refresh
does not invoke capture or control.step. With no sample, show how to obtain
one using the existing Bistro capture flow.

Use existing assets.list/get catalog identity, type and dependency values
for resolution and deduplication. The new bounded shared asset metadata read
in ADR-0188 provides only mesh and texture measurements. An unreadable
binding, invalid or unsupported artifact, unresolved dependency, unexpected
dependency type, timeout or exhausted budget affects only the evidence that
depends on it. Do not infer texture occupancy from compressed source files,
a resolution guess or a missing dependency. A deliberately unassigned
optional material contributes no explicit texture edges. An absent frame
sample does not make the document scan itself incomplete.

### R3 — Diagnose and locate a missing direct reference

V1 checks only non-nil values in the existing Renderable.meshAsset and
Renderable.materialAsset fields. Emit a **Missing in running catalog** row
only when the document value and its absence are both established against
the current successfully loaded catalog. Show the node label, node GUID,
field and missing asset GUID. A catalog refusal, malformed or unreadable
value, wrong type, absent component, or unsupported data cannot prove a
missing GUID. An unassigned optional material is valid. Incomplete property or catalog
coverage and wrong reference types have explicit reasons. A catalog entry
whose artifact cannot be read affects the relevant statistics total only. A
missing nested texture is a coverage gap for its total, without a
fabricated missing binding on the node. Known rows may remain visible
alongside **Check incomplete**; use **No missing direct references** only
for a complete direct check. This message is scoped to this check, not full
scene validation or readiness to Apply.

**Locate** rechecks the same Runtime, document token, selected scope, live
node and component occurrence, current field value, and absence in the
running catalog. Apply the existing Worker and UI delivery guards so a
later request, an observed replacement with the same GUID or a late reply
cannot change selection. The read and UI reveal are separate steps; an
external change after the last verification can invalidate the report on
the next observation. On success reveal the existing Hierarchy row and
focus its Inspector field using the existing locate behavior. If the
successful reveal changes the selected subtree, retire the old report
without automatically refreshing. Keep active drafts and gestures protected. If revalidation fails, preserve selection,
draft and focus, explain why and offer Refresh. Report and locate perform no
document write, Save, Apply, publication, history operation, or Runtime
mutation. Use the same read path after an Apply fails, when the document
still contains the unresolved binding.

### Non-functional

Read bounded artifact headers and logical file lengths only, with no GPU
resource access or raw paths accepted from clients. Reads use checked
arithmetic and the existing layouts. Keep one latest report per Editor
session. No changes to the render loop, allocation tracking, catalog
formats, existing packet shapes, dependencies, or cooking output.

## Proposed Design

Compose authoring observations and catalog queries in Editor's existing
Worker. Use the shared metadata read for typed artifact facts that current
public responses omit. Keep presentation, aggregation, and locate intent in
the Editor model. The Runtime frame sample retains its own provenance.
The view shows incomplete evidence where appropriate and can be refreshed
explicitly after repair or restart.

## Architectural Impact

The shared bounded read adds an Asset Connection contract for all clients.
[ADR-0188](../adr/0188-on-demand-cooked-content-metrics.md) records the host,
artifact validation, lifetime, and compatibility boundary. World,
Authoring, Renderer, and RenderGraph interfaces are unchanged.

## Alternatives Considered

A fully client-side view cannot derive triangles or cooked texture bytes
from existing catalog details. A continuous Runtime profiler introduces
collection and a different measurement contract. Stepping to obtain a
fresh frame on statistics refresh can apply held Runtime commands. Reuse
the present frame report only when a separate capture has completed.

## Testing & Verification Plan

[Plan 0140](../plans/0140-scene-statistics-content-diagnostics.md) maps R1–R3
to shared-contract and model tests, Windows 100% Bistro acceptance, and
the applicable repository gates.

## Risks & Open Questions

- A failed Apply leaves two versions visible. The view labels each source,
  revision and projection, and continues to diagnose the document.
- Cooked payloads and catalog entries can be externally changed without a
  global transactional file snapshot. ADR-0188 defines what the read can
  claim, and which observations require a fresh result.
- The design review should approve the bounded read, metric wording, and
  single diagnostic/locate path before implementation.

## Out of Scope / Future Work

GPU memory accounting, submitted draw counts, automatic monitoring,
dependency-chain diagnostics, source-file repair and scene-wide batch edits.
