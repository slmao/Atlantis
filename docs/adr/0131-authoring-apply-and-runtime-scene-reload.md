# ADR 0131: Apply and Reload — From the Authoring Document to the Runtime World

- **Status:** Accepted
- **Date:** 2026-10-22 (accepted 2026-10-22)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-22 (owner rulings on Spec 0065 Q1–Q8, each as recommended, recorded once in
  [Plan 0065](../plans/0065-authoring-data-layer.md); the ADR text was reviewed in [PR #280](https://github.com/slmao/Atlantis/pull/280))
- **Related Spec:** [Spec 0065: The Authoring Data Layer](../specs/0065-authoring-data-layer.md) (`Approved` 2026-10-22; its Q4)
- **Related ADR(s):**
  - **Supersedes in part** [ADR-0102](0102-authoring-runtime-world-separation-and-scene-bake.md) as it records Spec
    0051's Non-Goal "no re-bake or hot reload": the Runtime may now replace its scene by a new bake. The bake
    itself (D2–D5), its input and its order guarantee are unchanged, and the bake stays the only authoring →
    runtime mapping.
  - **Supersedes in part** [ADR-0105](0105-runtime-connection-and-cli-client.md) D2: the InProcess endpoint is no
    longer bound to one World for the process's life; it is re-bound at a reload.
  - **Supersedes in part** [ADR-0106](0106-attachable-runtime-transport-and-control.md) D3: `RuntimeStatus` gains
    `sceneGeneration`. The wire stays `atlantis.remote/1` (an additive field).
  - **Supersedes in part** [ADR-0124](0124-editor-transaction-and-client-side-undo.md) D2: E2's history is cleared
    when the scene generation changes, not only when the scene GUID does.
  - Keeps [ADR-0103](0103-runtime-world-operation-boundary.md) / [ADR-0104](0104-runtime-world-transaction-atomicity.md):
    no Runtime World event or error is added for a reload.
  - Ownership is [ADR-0129](0129-authoring-document-ownership.md); the interface is
    [ADR-0130](0130-authoring-access-interface-and-module-boundary.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The Runtime loads one scene, once** (ADR-0102; Spec 0065 fact 3): `loadAndBakeScene()` in
  `initializeSteps()`, then `scene_` and `worldAccess_` for the process's life, with every connection built over
  them.
- **Cook and decode are file-based,** and `decodeScene()` is the only constructor of `ValidatedSceneData`
  (Plan 0015 D2). The byte-level encoder and decoder already exist.
- **The Runtime World's surface is closed** (ADR-0103): no event can announce a reload.
- **Runtime-side edits exist** (E2's editor, the CLI, the Gameplay SDKs) and are not authoring edits.
- **Bistro is large** (about 6,000 nodes); a full re-bake is the cost of every apply.

## Decision

1. **Apply is explicit** (Spec 0065 Q4, B-a): `AuthoringConnection::apply()` (ADR-0130 D1) is the one way a
   document change reaches the Runtime World. Edits accumulate in the document until then; a client wanting
   immediate effect applies after each transaction.
2. **Apply cooks in memory, exactly as the build does.** Asset System's scene cook is split into an in-memory
   encode (every `cookScene()` check, then `encodeSceneArtifact()`) and the existing atomic file write, which
   writes those bytes. Decode gains an entry reading bytes and the sidecar's fields, and remains the only
   constructor of `ValidatedSceneData`. For any document, the in-memory artifact equals the build's file byte for
   byte.
3. **The scene sink.** Apply hands the decoded scene to a sink the host provides; in `atlantis_runtime` it is
   `RuntimeApplication::requestSceneReload(ValidatedSceneData)`. Apply's result says whether the request was
   accepted. The sink reports back when the reload has swapped, and the session then emits `Applied{generation}`
   to its subscribers (ADR-0130 D1); a failed reload emits nothing and is a Runtime diagnostic.
4. **Reload is all or nothing, at a frame boundary** (L-a). At the start of the next frame, after pending World
   commands are applied to the old World:
   - the Runtime resolves the new scene's mesh and material references through the catalog, reusing every resource
     already loaded and uploading the rest;
   - it bakes (`bakeScene()`, unchanged) and builds the new World access;
   - it swaps `scene_` and `worldAccess_` together and re-binds the InProcess endpoint, so every open
     `RuntimeConnection` — the Remote server's per-client connections included — now reads and writes the new
     World. Subscriptions keep their filters; queued Runtime World events of the old World are dropped; no event
     is emitted for the reload;
   - pick geometry, if retained, is rebuilt;
   - **what the new scene no longer references is released at the swap:** meshes, materials, textures and their GPU
     objects, and their pick geometry, once the GPU has finished the last frame that used them (a `waitIdle()` at
     the swap, the pattern of the existing `SurfaceDestroyed` teardown). The resource maps then hold exactly what a
     fresh start on the new document would load, so repeated applies do not accumulate resources;
   - on any failure (an unresolvable asset, an upload failure) nothing is swapped and the old World continues.
   The swap point is a named place in `runFrame()`, enumerated in the implementing PR.
5. **`RuntimeStatus.sceneGeneration`,** incremented by every successful reload, names the World's generation.
   Authoring subscribers are told by the event `Applied{generation}` (D3); a client with no authoring subscription
   (E2's editor, a Gameplay SDK client) compares generations. E2's history clears on a change of it, as on a change
   of scene GUID.
6. **The result equals a fresh start.** After a reload, the frame's data and a capture equal those of a Runtime
   started on the same document saved and cooked by the build.
7. **Runtime-side edits are not written back.** They last until the next reload, which replaces them with the
   document's state. The document is the source of truth; the Runtime World is its projection.

## Consequences

### Positive

- **The bake stays the only mapping** from authoring to runtime state, and the build's cook the only cook.
- **Clients keep their sessions** across a reload; nothing in the closed World surface changes.
- **A reload is testable against a fresh start,** byte for byte.

### Negative / Trade-offs

- **Every apply is a full re-bake;** on Bistro its cost is measured and may call for incremental projection later.
- **Runtime-side tweaks are lost at apply,** by rule, silently as far as the Runtime World's clients are
  concerned. The frozen ImGui editor will not show it (it only clears its history, ADR-0125 D4); Atlantis Editor
  App's UI must.
- **A client with no authoring subscription** learns of a reload only by comparing generations; authoring
  subscribers get `Applied{generation}`.
- **A `waitIdle()` at the swap** costs one GPU drain per apply, on a path that already re-bakes.
- **`runFrame()` gains a swap point,** so the performance gate applies to the implementing PR.

## Alternatives Considered

- **Automatic apply after every transaction** (Spec 0065 Q4 (B-b)): a full re-bake per small edit.
- **Live projection** (B-c): property edits mirrored to the Runtime World as World commands, structural edits
  re-baked — a second mapping beside the bake. Named as a follow-up.
- **Connections closed at reload** (L-b): every client, the editor's included, would treat a reload as a
  disconnect.
- **A reload event on `RuntimeConnection`** (L-c): a sixth event on the closed surface. The `Applied{generation}`
  event chosen instead is an *authoring* event (ADR-0130 D1), on the authoring interface's own closed set; ADR-0103's
  closed World surface is untouched and the rejection of L-c stands.
- **Keeping unreferenced resources until exit:** simpler, but repeated applies would grow GPU memory without bound
  and the resource maps would no longer equal a fresh start's.
- **Reloading from a source path in the Runtime:** the Runtime's load path would parse text, against ADR-0102 D2.

## Accepted Amendment — 2026-10-22

**Status of this section:** accepted by Human Review on 2026-10-22, together with the Spec 0065 Correction
(slmao, review of its docs PR [PR #281](https://github.com/slmao/Atlantis/pull/281)). **This section does not itself change this ADR's top-level Status**
(`Accepted` above is unchanged). The Decision, Context, Consequences and Alternatives Considered above are preserved
verbatim and are not superseded except where this section says so explicitly.

1. **D2's in-memory cook runs the build's text path.** It maps the document to its parsed source, serializes it with
   the corrected `serializeSceneSource()`, and cooks that text through `cookSceneSource()`: the parse, every
   `cookScene()` check, then `encodeSceneArtifact()`. A transaction is therefore refused for anything the parser
   refuses (light limits, component groups) as well as anything the cooker refuses.
2. **A document its text cannot represent is refused.** The cook also checks that the document comes back unchanged
   from its own text, and refuses one that does not (`SceneCookError::NotRepresentableInSource`; for example a
   camera and a light on one node) instead of narrowing it silently.
3. **The byte-identity guarantee is unchanged:** for any document, the in-memory artifact equals the build's cook of
   the saved text, byte for byte, now by construction.

### Human Review — Amendment Acceptance (2026-10-22)

**Deciders:** slmao — accepted the [Spec 0065](../specs/0065-authoring-data-layer.md) Correction (2026-10-22) and
ruled [Plan 0065](../plans/0065-authoring-data-layer.md) J10 as recommended (`std::to_chars` with no format argument,
the shortest round-trip text, `-0` kept), in the review of [PR #281](https://github.com/slmao/Atlantis/pull/281). This amendment records what that
Correction means for this ADR; no decision above is reopened.

## Accepted Amendment — 2026-10-23

**Status of this section:** accepted by Human Review on 2026-10-23, with the Spec 0065 third Correction (slmao,
ruled before its docs PR). **This section does not itself change this ADR's top-level Status** (`Accepted` above is
unchanged). The Decision, Context, Consequences, Alternatives Considered and the Accepted Amendment of 2026-10-22
are preserved verbatim and are not superseded except where this section says so explicitly. It amends D4 only.

1. **The swap point is gated on the same condition as command application.** D4's "at the start of the next frame,
   after pending World commands are applied to the old World" reads: at the start of the next frame *that applies
   commands*. While a pause holds command application (ADR-0106 D4), a requested reload stays pending and nothing
   is swapped; a newer apply still replaces it (Plan 0065 J5).
2. **The held commands apply to the old World first.** On a Step or on resume, that frame applies every held
   command to the old World — each settling as an event or a drained failure, as ADR-0103 D6 and D9 require — and
   only then performs the reload.
3. **The InProcess endpoint's "unreported" behaviour is withdrawn.** With the swap gated, no command can be pending
   in the outgoing World access when the endpoint is re-bound; the endpoint checks that, and drops nothing.
4. **The old World's queued events are delivered, not dropped** (ruled 2026-10-24). D4's "queued Runtime World
   events of the old World are dropped" is withdrawn: at the re-bind the endpoint hands the outgoing World access's
   queued events to the matching subscriptions, as it already hands its failures to their submitters. A command
   applied in the swap frame — a held command on Step, or one submitted with an apply while running — therefore
   settles as its event, like any other. No event is emitted for the reload itself, as before.
5. **Kept unchanged:** [ADR-0103](0103-runtime-world-operation-boundary.md) (every submitted command settles) and
   [ADR-0106](0106-attachable-runtime-transport-and-control.md) D4 (pause holds command application; a step
   releases exactly its frames). Neither is amended.

### Human Review — Amendment Acceptance (2026-10-23)

**Deciders:** slmao — ruled the [Spec 0065](../specs/0065-authoring-data-layer.md) third Correction (2026-10-23): a
reload waits while commands are held; the swap happens at the next command application, after the held commands
are applied to the old World; no command is dropped unreported; under pause an apply is accepted and stays pending;
the CLI's `scene apply` under pause returns its pending outcome and reports `Applied{generation}` when it arrives.
Recorded in the review of its docs PR. On 2026-10-24, while implementing it, slmao ruled item 4: the old World's
queued events are delivered at the re-bind rather than dropped, so a command applied in the swap frame settles as
its event. This amendment records what the Correction means for D4; no other decision above is reopened.

## Accepted Amendment — 2026-10-27

**Status of this section:** accepted by Human Review on 2026-10-27, with Spec 0068 and
[ADR-0135](0135-two-document-edit-and-apply.md) (slmao). **This section does not itself change this ADR's top-level
Status** (`Accepted` above is unchanged). The Decision, Context, Consequences, Alternatives Considered and the Accepted
Amendments of 2026-10-22 and 2026-10-23 are preserved verbatim and are not superseded except where this section says so
explicitly. It amends D5's history clause only.

1. **D5's "E2's history clears on a change of scene generation" applies to a history over the Runtime World,** as E2's
   is. A history over the authoring document (ADR-0135 D4) is not cleared by a change of scene generation, because the
   editor's own applies change it at every committed edit. Such a history clears when the document is reopened from
   disk (`DocumentReopened`) and when its editor session ends.
2. **Kept unchanged:** D5's scene generation itself (`RuntimeStatus.sceneGeneration`, incremented by every reload) and
   every other decision of this ADR.

### Human Review — Amendment Acceptance (2026-10-27)

**Deciders:** slmao — ruled Spec 0068 Q1–Q8 (2026-10-27), each as recommended, accepting ADR-0135, whose D4 needs this
amendment. Recorded in the review of Spec 0068's branch PR, [PR #302](https://github.com/slmao/Atlantis/pull/302).
