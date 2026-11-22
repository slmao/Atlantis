# Spec 0138 — Locate and Edit a Material Binding from the Viewport

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan(s):** [Plan 0138](../plans/0138-viewport-material-slot.md)
- **Approval:** Pending joint review
- **Related ADR(s):** [ADR-0186](../adr/0186-shared-material-binding-pick.md) (Proposed)
- **Design baseline:** `66bfd0ba`, maintainer merge of [PR #438](https://github.com/slmao/Atlantis/pull/438)

## Summary

Click the fabric of a Bistro storefront awning in the hosted Editor.
Reveal the exact node and its existing material binding in Inspector,
show other known uses, and offer either an explicitly shared material edit
or a duplicate followed by an explicit replacement of that one binding.

## Motivation / Problem Statement

Ordinary raycast returns only an entity and distance. It chooses a nearest
result and does not prove an unambiguous material binding. The
[0135 face query](0135-viewport-surface-alignment.md) already examines retained
CPU triangles and rejects ambiguous geometry. Its rectangular end-cap
test is specific to alignment and cannot serve the general fabric surface.

A Runtime Renderable draws a single optional material reference.
Bistro import already splits fabric and metal into separate renderable
children. Several awning fabric subsets in the pinned Bistro share one GUID.
The practical slot to reveal is the owning child's existing
`Renderable.materialAsset` property. An imported primitive label is useful
context when available. It is neither a durable slot identity nor a new
index the user can edit.

[0094](0094-basic-material-authoring.md) already manages material Edit,
Duplicate, Save & Publish, Resume and explicit activation.
[0092](0092-mesh-material-preview.md) provides isolated preview, and
[0082](0082-generic-asset-reference-picker.md) and
[0084](0084-asset-reference-target-types.md) already provide typed reference
editing. The feature connects these operations to verified viewport
evidence.

## Goals

- Explain what the clicked awning surface actually uses and which one
  authored field controls it.
- Show known direct sharing from the current document with an explicit
  completeness label.
- Keep shared asset editing and a single binding replacement distinct and
  let users see the result at the same Bistro viewpoint.

## Non-Goals

Per-triangle authoring, multiple independently editable slots on one
Renderable, fallback material editing, transparent or alpha-tested surface
identification, project-wide usage indexing, picking through blockers,
automatic activation or replacement, and bulk material edits.

## Requirements

### Functional

- **R1 — Resolve a unique drawn binding.** A one-shot **Locate Material**
  action in the hosted Editor arms the next eligible viewport click. Reuse
  the current acknowledged perspective and physical drawable coordinates.
  The shared read in ADR-0186 returns the nearest verified node, mesh and
  effective material GUID, and the one `Renderable.materialAsset` slot.
  Limit success to an explicit opaque PbrDirectLit material and a single
  eligible Renderable at the front of the ray. Fog, bloom and overlays do not
  create material targets. A miss, near tie between different Renderables
  (even if they share the material), missing geometry or identity, fallback
  material, or unsupported front surface leaves selection and draft intact
  and gives a reason. The query must never look past an unsupported front
  blocker to choose a farther target. Adjacent triangles on the same
  single-binding Renderable can identify the same slot. V1 requires the
  default hosted visibility policy. If Hide, Isolation or Pick Lock is
  active, prompt the user to restore it explicitly. Ordinary picking and
  other viewport input retain their existing behavior.
- **R2 — Reveal and show known sharing.** Only reveal a hit when a settled
  Runtime Apply projection matches the exact open hand-authored document
  revision, the entity maps to the same live Hierarchy occurrence, and the
  document's explicit mesh and material references match the Runtime
  result. Reveal the node and focus its existing Material row. Show the
  material name and GUID from the active catalog, the scene/node and slot,
  and the confirmed direct slots in that document using the same GUID.
  Read through existing authoring list and property APIs in bounded
  requests and bracket the scan with the document token. Consider at most
  8,192 nodes and label unavailable, truncated or unreadable data as
  incomplete. Show only confirmed entries. State that the count covers
  this open document. Whole project, other scenes, saved prefab sources and
  Runtime-only objects have no inferred count. An observed token or
  context change invalidates the panel's actions, even if already-known
  sharing is still displayed as historical. Keep drafts and active gestures
  protected and reject late results against the Worker observations at
  delivery as well as the current Editor intent.
- **R3 — Explicit asset operation.** Show **Edit shared material** and
  **Duplicate for this binding** as separate choices. Shared Edit acts on
  the asset GUID. The confirmation states that every use of that asset can
  change after activation, including uses outside known sharing. Duplicate
  writes a new material GUID through the existing workflow and leaves the
  original asset and all slot bindings intact during publication. Reuse
  current Edit and Copy eligibility, workspace authority, exact recipe and
  dependency validation, preview, conflict and unknown-result recovery.
  Admit neither job from a stale target. Recheck R1–R2 when opening the
  operation and immediately before the existing Save & Publish admission.
  Report settled asset job outcomes independently from document and Runtime
  Apply status. An admitted job may still settle if the context changes
  afterward. It can never write a slot.
- **R4 — Explicit single binding replacement.** After a Duplicate is
  published, show its new GUID and existing explicit activation guidance.
  A new Runtime must have the copy in its verified catalog before replacing
  a binding. Offer a copyable review note with the scene, node, mesh,
  original material, and copy GUIDs plus the slot. A restart ends the
  original locate context. In the new session the user re-picks the awning,
  pastes the note into **Replace this binding with copy**, and explicitly
  confirms the existing and replacement GUIDs. Preselect the copy by its
  exact GUID in the existing typed chooser. The Editor compares the note
  to the fresh hit and current document. Any mismatch, missing or identical
  destination GUID, or unsupported slot refuses replacement. Treat the
  note as untrusted, versioned plain text of at most 512 UTF-8 bytes with
  canonical GUIDs. It supplies expected identities and carries no authority
  or pending transaction. Keep it only while that confirmation is open.
  At confirmation, re-read the live hit and verify document lifetime, node
  and component occurrence, projection, visibility, expected binding and
  destination material GUID. Write only
  `Renderable.materialAsset` with the existing `RequireDocumentRevision`
  plus one `SetProperty` on that verified revision. A stale/refused
  transaction writes no field, history or Apply. A lost submit receipt
  retains the existing result-unknown and inspection flow. A verified
  commit adds one Undo entry and uses the existing tracked Apply path.
  Apply failure retains the committed binding and history, and explicit
  recovery applies only the latest document. Undo and Redo check the
  expected field value and occurrence and change only the slot. A successful
  copy is a project asset even when the later binding is canceled, rejected,
  undone or left unperformed. Saving the scene source remains explicit.

### Non-functional

Use the existing Editor conventions, asset authority and Win32 hosted input.
The extra geometry work runs once per admitted read. No new per-frame
diagnostic work, render pass, retained GPU resources or new asset, scene or
material format. Keep existing raycast consumers byte-compatible.

## Proposed Design

Provide a single Inspector section for the located material and its verified
scope. Show complete or incomplete known usage, and reuse the existing
material editor and reference picker for the actions. Preserve the normal
Inspector for controls other than the binding. The approved 0094 source
and activation workflow determines which assets can be edited or copied.
A read-only imported material can be located and only offers Copy when
already eligible. The Bistro fixture uses two real fabric nodes with a
common eligible managed material so both choices can be exercised.

Keep the Runtime ray proof, authoring slot guard, catalog snapshot and
material publication independent. The Runtime result is a sample of the
settled projection. A second client may change transient World state
between the sample and a later authoring submission. The revision guard
covers authoring at commit. Asset job admission and scene writes use their
own existing authority and settlement. Do not promise distributed atomic
writes or silently follow a changed target.

## Architectural Impact

[ADR-0186](../adr/0186-shared-material-binding-pick.md) adds one shared read
on RuntimeControl and the existing Remote protocol, with equivalent access
for CLI, both SDK paths and MCP. Connection owns the small protocol values.
The Runtime owns geometry and catalog verification, and Editor owns the
interaction. Those are the only proposed boundary changes.

## Alternatives Considered

- Use ordinary entity raycast then display its Inspector material. This
  cannot tell a resolved draw from fallback or a near-equal intersecting
  binding, and cannot guarantee the World and document agree.
- Reuse the full 0135 rectangular face predicate. This rejects valid fabric
  on shape alone and exposes irrelevant face topology to clients.
- Auto-bind on publication or restart. This would need cross-session intent
  and mutation recovery while the original authoring selection may have
  changed.

## Testing & Verification Plan

Cover success, miss, same-slot edges, cross-slot near ties, unsupported
front occlusion, clipped hits, changed policy and result stamps,
unsupported hosts, stale references, Apply and publication recovery, and
unchanged selection and drafts. Use the Windows 100% storefront workflow
and compare the same fixed pose before and after replacement, then validate
Undo and a formal saved-scene cold open. [Plan 0138](../plans/0138-viewport-material-slot.md#verification-checklist)
defines measurable acceptance and the applicable gates.

## Risks & Open Questions

The geometric lookup is constrained to opaque scene geometry and cannot
identify a blended pixel. The acceptance fixture requires two clearly
separated visible fabric locations and an eligible managed copy source.
Confirm the cross-restart manual handoff and modern hosted capability
boundary in joint review.

## Out of Scope / Future Work

Transparent hit attribution, native multiple-slot binding, durable
continuations, global reference indexes and bulk replacement.
