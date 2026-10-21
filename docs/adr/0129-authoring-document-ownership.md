# ADR 0129: The Authoring Document's Owner — One C++ Authoring Session, Hosted by the Runtime Executable

- **Status:** Proposed
- **Date:** 2026-10-22
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0065: The Authoring Data Layer](../specs/0065-authoring-data-layer.md) (`Draft`; its Q1, Q2
  and Q6)
- **Related ADR(s):**
  - **Extends** [ADR-0102](0102-authoring-runtime-world-separation-and-scene-bake.md): the authoring stage gets a
    live owner. D1 (Authoring World ≠ Runtime World), D2 (the bake takes `ValidatedSceneData`; no source-text bake
    in the Runtime path) and D3 (the product is `BakedScene`) are kept. What changes is where the authoring stage
    may run: the `atlantis_runtime` *executable* hosts it, as it hosts the CLI and the Remote server, while
    `RuntimeApplication` and `atlantis_runtime_host` still never parse source text.
  - Keeps [ADR-0100](0100-scene-semantic-schema-layering-model-and-versioning.md): the semantic layer, its home in
    Asset System, `AuthoringScene` as the document model, the property address (D7), the serializers below Scene.
  - Keeps [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md): node and baked entity share the EntityGuid;
    minting stays an explicit authoring act.
  - Keeps [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md) D1–D3: the editor is a
    client; the capability is on the shared surface; the descriptor tables stay the schema's one truth.
  - The interface clients use is [ADR-0130](0130-authoring-access-interface-and-module-boundary.md); how a change
    reaches the Runtime is [ADR-0131](0131-authoring-apply-and-runtime-scene-reload.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **E3 edits the authoring scene,** not the Runtime World (Spec 0065). The document model exists
  (`scene::AuthoringScene`, ADR-0100 D2) but has no owner while it is edited: today it is a plain value used by the
  codecs and the conformance suite, and the Runtime only ever sees a cooked artifact, once, at start (ADR-0102).
- **Several clients must edit the same document:** the C# editor, the CLI, scripts and agents (ADR-0125 D2). Each
  must see the others' changes.
- **The grammar, the scene schema's constraints and the cooker exist once, in C++.** Re-implementing them in C#
  (the C# SDK's pattern for the wire codec) would duplicate several thousand lines of validation and land every
  grammar change twice.
- **Every E3 editor session has a Runtime:** the editor embeds the Runtime's Viewport (Spec 0064).
- **Bistro's source is generated** by the glTF importer from fetched content and an overlay; it cannot be written
  back.

## Decision

1. **One owner: a C++ authoring session.** The document being edited lives in exactly one place — an
   `AuthoringSession` in a new C++ module, Atlantis Authoring (ADR-0130 D2). It holds at most one open document:
   an `AuthoringScene`, its provenance and the scene's AssetGuid. Clients never hold a copy; they read and change it
   only through the authoring interface (ADR-0130 D1). No second implementation of the scene grammar, the scene
   schema's checks or the cooker is written in any language.
2. **Hosted by the `atlantis_runtime` executable** (Spec 0065 Q2, D-a): the host creates the session on request and
   serves it to in-process and remote clients beside `RuntimeConnection` and `RuntimeControl` — one process, one
   session file, one endpoint.
   - **The Runtime proper is unchanged in kind:** `RuntimeApplication`'s load path and the bake consume decoded
     artifact data only (ADR-0102 D2). The session reaches the Runtime only through a narrow scene sink the host
     provides (ADR-0131 D3); the module names no Runtime, ECS, Platform or GPU type.
   - **A separate host process stays possible:** because the module does not depend on Runtime, a later
     `atlantis_authoring` host is a deployment change, not a redesign.
   - **Lifetime:** the session lives with the process; a Runtime crash loses unsaved edits.
3. **The document is the Runtime's scene** (Q6, P-a). A session opens the document of the scene the Runtime
   loaded, found through its catalog entry: a hand-authored scene from its source file; an imported scene from
   its generated source. Opening any other path is not offered, so apply always replaces the Runtime's scene with
   an edited version of itself.
4. **Save writes hand-authored sources only:** atomically (write to a temporary file, then rename), through
   `toParsedSceneSource()` and `serializeSceneSource()`, so an unchanged document saves byte-identically. An
   imported document is editable and can be applied for the session, but its save is refused
   (`ImportedSceneNotSavable`). Saving imported scenes and "save as" are future work.

## Consequences

### Positive

- **Every client edits one document** and sees the others' edits; agents edit what the editor shows.
- **No duplicated grammar or cooker;** the C# SDK stays a wire client.
- **The Runtime World stays a pure bake product,** and its load path still never parses text.
- **Save is byte-stable** with the existing serializer.

### Negative / Trade-offs

- **The Runtime process grows a second hosted responsibility,** beside the CLI and the Remote server.
- **Unsaved edits die with the process.**
- **No editing without a running Runtime** in this decision's deployment.
- **Imported scenes cannot be saved** until an overlay model exists.

## Alternatives Considered

- **The document inside `RuntimeApplication`** (Spec 0065 Q1 (a)): one owner for two documents, a parser on the
  Runtime's load path, and ADR-0102 superseded at its centre.
- **Each client owns its document** (Q1 (b)): a C# grammar, schema checker and cooker kept in step by vectors; two
  clients could never edit one document.
- **A separate authoring process now** (Q2 (D-b)): a second endpoint and discovery step for every client, and a
  cross-process reload, for an isolation E3 does not need yet; kept possible by D2.
