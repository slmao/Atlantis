# ADR 0149: Create Hand-Authored Scenes Through Shared Authoring

- **Status:** Accepted
- **Date:** 2026-11-10
- **Deciders:** slmao
- **Acceptance:** Accepted on 2026-11-10 at revision `89305f09` of [PR #341](https://github.com/slmao/Atlantis/pull/341).
- **Related Spec:** [Spec 0089](../specs/0089-new-hand-authored-scene.md)

## Context

[ADR-0148](0148-scene-save-as-and-explicit-launch.md) gives an opt-in
development Runtime authority to publish a named scene copy as an atomic
source unit. A build selects that unit explicitly. Existing authoring
opens the source of the scene the Runtime loaded; it cannot retarget a live
session. Starting from scratch needs a durable identity and valid scene
content, but does not need to change live scene ownership.

## Decision

Extend [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
with one additive authoring creation operation. Authoring owns the starter
document and uses Asset System identity and codec services. The existing
opt-in Windows executable host owns publication. Create Scene and Save As
share the destination namespace, identity checks and atomic no-replace
publisher. Their source selection and success values remain distinct:
creation takes no snapshot of the current document.

Creation commits source, not a new active document or a Runtime projection.
Reuse ADR-0148's explicit offline selection and separate Runtime launch.
The ordinary Editor may pass through the existing explicit development
opt-in to its child. It has no filesystem authority of its own. The user
can open the separately built scene in a second ordinary Editor instance,
leaving the original session and its unsaved work in place.
Existing Open, Save, undo/redo and apply rules then govern the new session.
Editor, CLI, SDKs and MCP use the same authority and result; no editor-owned
files or separate transport are introduced.

## Consequences

A new unit is durable, buildable and independent of unsaved edits in the
original session. Existing source and artifact formats and apply semantics
stay intact. The user must build and start the new scene explicitly. The
old Runtime does not gain catalog discovery or live switching; an
interrupted response uses the shared publication recovery rules.

## Alternatives Considered

- **Retarget the current session or use an untitled document.** Both require
  new dirty-state, session and projection transitions before the first Save.
- **Generate the starter in each client.** This would duplicate validation
  and give clients filesystem authority now confined to the host.
- **Register and activate it through the live asset worker.** This changes
  asset generation and scene activation for a flow already covered by the
  offline saved slot.
