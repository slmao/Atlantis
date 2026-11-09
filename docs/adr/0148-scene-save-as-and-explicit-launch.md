# ADR 0148: Scene Save As and Explicit Launch

- **Status:** Proposed
- **Date:** 2026-11-10
- **Deciders:** Pending joint human review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0088](../specs/0088-scene-save-as.md)

## Context

[ADR-0129](0129-authoring-document-ownership.md) binds one authoring document
to the Runtime's startup scene. [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
gives every client the same access to it. Hand-authored source files and
their scene GUIDs enter the build through [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md)
and [ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md).
A new copy needs an independent durable identity and a launchable build
while the original session retains its document and apply projection.

## Decision

1. Extend ADR-0129 D4 and ADR-0130 D1/D3 with one additive authoring operation.
   Authoring captures and serializes its current hand-authored document.
   A narrow sink in the opt-in Windows executable host owns local filesystem
   authority and registration. It atomically commits one no-replace source
   unit under the trusted development assets root. The unit carries a new
   Asset System-minted scene GUID in a standard catalog-source fragment.
   Copy EntityGuids and asset references unchanged. ADR-0097's scene-scoped
   identity makes the new scene's entity references distinct.
2. Preserve ADR-0129 D3 and [ADR-0131](0131-authoring-apply-and-runtime-scene-reload.md).
   Save As settles only the new source unit. The original session retains its
   binding, history and apply state. Existing Save and Open target the
   current session's original scene. A copy becomes the current document by
   starting a Runtime whose loaded scene is the copy.
3. Extend the existing build and executable whitelist with one explicitly
   configured saved-scene slot. Validate the selected unit and add its
   fragment to the effective build catalog source, reusing the hand-authored
   scene producer. Bind the build's reserved saved_scene name to its GUID.
   Fresh Runtime startup loads the cooked result and existing authoring
   lookup opens the copy. A saved-slot development build can consume the
   committed inputs of [ADR-0147](0147-managed-import-registration-and-publication.md)
   but keeps its output separate from profile-backed generation publication.
4. Expose the same result through Editor, CLI, SDKs and MCP. Preserve
   [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md)
   and the existing RuntimeHost and Android boundaries.

## Consequences

The copy and its identity can be retained or built independently of the
original session. Keeping the existing launch boundary requires an
operator-driven configure/build/start sequence, with one copied scene per
configured executable. A committed unit can survive a lost response. The
operator can inspect the unit before deciding whether to retry. The copy
depends on the project providing the asset GUIDs it references.

## Alternatives Considered

- Change the active scene or Save target on the original session. This would
  change authoring ownership and require a Runtime projection transition.
- Let each client write a copy. This would add multiple write authorities
  and a filesystem dependency to clients of a shared host.
- Launch from an arbitrary source path or add a scene registry service.
  Neither is required for a buildable first copy.
- Register and publish through the live asset worker. This would broaden
  both Save As settlement and the existing generation lifecycle.
