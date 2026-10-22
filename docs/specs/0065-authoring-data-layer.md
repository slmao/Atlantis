# Spec: The Authoring Data Layer (E3, no UI)

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-22
- **Related Plan(s):** [Plan 0065](../plans/0065-authoring-data-layer.md) (`Approved`; Joint Human Review 2026-10-22
  recorded in its header). As with Specs 0062–0064, the Plan was drafted together with this Spec at the
  maintainer's direction.
- **Approval:** slmao, 2026-10-22 — owner rulings Q1–Q8, each as its recommendation (Q1 (c); Q2 D-a; Q3 I-a + V-1; Q4 B-a + L-a; Q5 S-a; Q6 P-a; Q7 T-a; Q8 G-b),
  recorded once in [Plan 0065](../plans/0065-authoring-data-layer.md); review of this Spec's own branch PR,
  [PR #280](https://github.com/slmao/Atlantis/pull/280). The open-question text below keeps the options as the record of what was weighed.
- **Correction (2026-10-22, post-Approval, found at the start of Plan 0065 M1; accepted by slmao, 2026-10-22, in the
  review of its own docs PR [PR #281](https://github.com/slmao/Atlantis/pull/281), with Plan 0065 J10 ruled as recommended):** R7's in-memory cook, R8's "identical to a fresh start on the saved document", R9's save and Plan J2's
  cook dry run all assumed that `serializeSceneSource()` writes the document losslessly. It does not: it prints
  floats with `std::to_string` (six fixed decimals) and drops the fog and bloom values while their group is off —
  Plan 0049's known defect F5, which Spec 0049's Correction (2026-10-05, ruling J3) left to "the next serializer's
  spec". This Spec is the first to write source text back, so it fixes F5 (fact 6, R9, Plan P1):
  - `serializeSceneSource()` writes every float as its shortest round-trip form (`std::to_chars`), and writes the fog
    and bloom groups whenever any of their values is non-zero (this group rule is superseded by the second
    Correction below). The grammar, already parsed with
    `std::chars_format::general` and accepting both groups at zero strength, does not change; nor do the source
    format version, the artifact, the catalog or the semantic version.
  - The in-memory cook runs the build's own text path — serialize, parse, every cook check, encode — so a
    transaction can be refused for anything the parser refuses (light limits, component groups) as well as anything
    the cooker refuses, and the in-memory artifact equals the build's cook of the saved text by construction. The
    serializer writes at most one of a node's mesh, camera and light, so a document must also come back unchanged
    from its own text round trip; one that does not (two of them on one node, ComponentExclusivity) is refused, never
    silently narrowed.
  - "Saving an unchanged document rewrites the same bytes" (R9, ADR-0129 D4) holds from a document's first save on:
    the first save of a hand-authored file may rewrite its number formatting (`0.0` as `0`), never a value.
  - No ADR decision changes: ADR-0131 D2's in-memory cook already runs "every `cookScene()` check", the parse
    included, and ADR-0100 D6's "source ⇄ semantic is lossless" now also holds at the text layer. ADR-0129 and
    ADR-0131 each record what this Correction means for them in an Accepted Amendment (2026-10-22).
- **Second Correction (2026-10-22, post-Approval, found while implementing Plan 0065 M1; accepted by slmao,
  2026-10-22, in the review of its own docs PR [PR #282](https://github.com/slmao/Atlantis/pull/282), with the comparison with the no-group default ruled
  bit for bit):** the first Correction's group rule ("written whenever any of their values is non-zero")
  assumed that the no-group defaults are all zero. They are not: a camera with no fog group carries fog colour
  `(1, 1, 1)` and maximum opacity `1`, and one with no bloom group carries threshold `1` (`DecodedCameraFog`,
  `DecodedCameraBloom`, and the semantic `CameraFog`, `CameraBloom`). So that rule writes both groups, at their
  defaults, on every camera, and it drops a group whose values are all zero, which then reads back as the defaults:
  lossy. M1 (commit `344da6a`, on the feature branch) implements the rule as ruled, and its round-trip check refuses
  such a document (`NotRepresentableInSource`) rather than narrowing it. Corrected:
  - **The group rule:** a camera's fog group is written unless it equals the no-group default, value for value, and likewise its bloom group. The values are compared bit for bit, so a group holding a `-0` where the
    default is `0` is written and keeps its `-0` (J10). A group absent from the text parses as the default, so the
    rule is lossless by construction, an all-zero group included.
  - A camera at the defaults writes no group, as before F5's fix, so the first save of a hand-authored file changes
    number formatting only, as ADR-0129's amendment says.
  - Nothing else changes: the grammar, the artifact, the round-trip check and the byte-identity results. Every
    committed artifact is cooked from its unchanged source file, and the Bistro overlay's only camera has both groups
    on, so its generated source is unchanged by this rule.
  - ADR-0129's Accepted Amendment states the superseded rule; a second dated amendment there corrects that one
    sentence (Accepted Amendment — 2026-10-22, second). ADR-0131's amendment does not state the rule and is
    unchanged.
- **Related ADR(s)** (all `Accepted` 2026-10-22, alongside this Spec's own Approval):
  - [ADR-0129](../adr/0129-authoring-document-ownership.md): who owns the authoring document while it is being
    edited — a C++ authoring session, hosted by the `atlantis_runtime` executable, that every client edits through
    (Q1, Q2, Q6).
  - [ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md): the authoring access interface — a
    sibling of `RuntimeConnection` with the same four concepts over the document — its module boundary, and its
    reach over the wire, the CLI and the C# SDK (Q3, Q5, Q7, Q8).
  - [ADR-0131](../adr/0131-authoring-apply-and-runtime-scene-reload.md): how a document change reaches the Runtime
    World — an explicit apply, an in-memory cook byte-identical with the build's, and an all-or-nothing scene
    reload at a frame boundary (Q4).
  - Cited, unchanged: [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) /
    [ADR-0100](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md) (GUID identity, the semantic
    layer, the property address), [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) /
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) (the closed Runtime World surface),
    [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) /
    [ADR-0111](../adr/0111-schema-generated-typed-bindings.md) /
    [ADR-0112](../adr/0112-csharp-gameplay-client-transport-and-bindings.md) (the schema as the one truth),
    [ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md) (the one structured
    interface; no C# components).
  - Extended or superseded in part, each named in its ADR:
    [ADR-0102](../adr/0102-authoring-runtime-world-separation-and-scene-bake.md) (re-bake and reload; the
    Runtime process hosting the authoring stage),
    [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md) (the InProcess endpoint across a reload),
    [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md) (the wire's payload; `RuntimeStatus`),
    [ADR-0124](../adr/0124-editor-transaction-and-client-side-undo.md) (history clearing on reload; the second
    edit target).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

E3 is the editor iteration that edits the **authoring scene** — the document a `.scene` file holds — rather than
the Runtime World. Before any editor UI can do that, three things must exist that do not today: an owner for the
authoring document while it is being edited, a way for every client to read and change it, and a way for a change
to reach the Runtime World, which Spec 0051 deliberately made a bake product with no re-bake or reload.

This Spec specifies that **data layer and nothing above it**. Its central question is where the document lives
(Q1). It recommends a **C++ authoring session** — the existing semantic `AuthoringScene` given an editing API, a
transaction model mirroring the Runtime World's, and events — **hosted by the `atlantis_runtime` executable** beside
the CLI and the Remote server, reached by every client through one more interface on the shared surface
(`AuthoringConnection`, a sibling of `RuntimeConnection`, carried by `atlantis.remote/1`, the CLI and the C# SDK).
A client never holds its own copy, and no second implementation of the scene grammar or the cooker is written.
An explicit **apply** cooks the document in memory exactly as the build cooks it, decodes and bakes it, and the
Runtime **reloads** the result at a frame boundary, all or nothing.

The Runtime World stays what Spec 0051 made it: a bake product with its closed five-command, five-event surface.
Nothing here adds a World verb.

## Motivation / Problem Statement

### What exists, and what this Spec builds on without reopening

- **The pipeline** (Spec 0051, ADR-0102): `.scene` source → cook (Asset System) → artifact → decode →
  `ValidatedSceneData` → `bakeScene()` (World) → `BakedScene`. The Runtime loads cooked artifacts once, at start,
  and never parses source text (ruling Q1, B1). "No re-bake or hot reload" is a Spec 0051 Non-Goal.
- **The authoring model** (Spec 0049, ADR-0097, ADR-0100): GUID identity shared by a node and its baked entity;
  the semantic layer in Asset System (`scene::AuthoringScene`, the scene schema, the semantic version); the
  reserved property address `scene::PropertyAddress` (node GUID, component `TypeId`, `FieldId`).
- **The client surface** (Specs 0052–0055, ADR-0103–0106, ADR-0128): `RuntimeConnection` is exactly Query /
  Command / Event / Transaction over the Runtime World; `RuntimeControl` carries lifecycle and frame-side
  services; `atlantis.remote/1` carries both; the CLI and two SDKs (C++, C#) are clients, the C# one kept
  byte-identical to the C++ codec by conformance vectors.
- **The edit seam** (Spec 0063, ADR-0124 D4): E2's history reaches a world only through an edit target (read an
  address; submit ordered writes as one transaction; the handle's settlement; invalidations), so that E3's
  authoring scene can be a second target with the same record.
- **The platform** (Spec 0064, ADR-0125): the product editor is Atlantis Editor App, an out-of-process C# client;
  every capability it needs is a call of the shared surface, available alike to the CLI, the SDKs and agents
  (D2); the descriptor tables are the schema's one truth (D3); no C# components (D5). Plan 0064's ruling note:
  E3's data layer may go first, as its own Spec. This is that Spec.

### Facts found while drafting

1. **The semantic document exists but is not an editing API.** `scene::AuthoringScene` (nodes in instantiation
   order, each with an EntityGuid, optional parent, `Transform`, optional `Camera`, `Renderable`, `Light`; the
   document's active camera) is a plain value; its header says so. `toAuthoringScene()` / `toParsedSceneSource()`
   map it exactly to and from the source codec's DTOs, and `parseSceneSource()` / `serializeSceneSource()`
   round-trip the text. Every scene constraint (exclusivity, acyclic parents, value domains, light limits) is
   stated by `sceneSchema()` and checked by the cooker, not by these types.
2. **Cook and decode are file-based, and decode is the only constructor of `ValidatedSceneData`.** `cookScene()`
   reads a source path and writes artifact and sidecar files; `decodeScene()` reads them back and is, by a friend
   declaration, the only code allowed to construct `ValidatedSceneData` (Plan 0015 D2). The byte-level pieces
   exist separately (`encodeSceneArtifact()`, `decodeSceneArtifact()`), so an in-memory cook and decode are a
   refactor of existing code, not new logic — but they change Asset System's public decode boundary.
3. **The Runtime loads one scene, once.** `initializeSteps()` runs `loadAndBakeScene()` (catalog → decode →
   dependency load → bake), uploads meshes and materials, and publishes `scene_` and `worldAccess_`. The InProcess
   endpoint, every open connection and the Remote server's per-client connections are built over that one
   `RuntimeWorldAccess` for the process's life.
4. **E2's history clears on a scene-GUID change only.** A reload of the same scene keeps the GUID (`RuntimeStatus`
   carries `scene` only), so nothing today would tell a client that the World under it was replaced.
5. **Bistro's source is a build product.** `bistro.scene.txt` (5,972 nodes) is generated by the glTF importer from
   the fetched content plus a hand-authored overlay (`assets/bistro/bistro_overlay.scene.txt`, 61 nodes: the camera
   and lights). An edit to an imported node cannot be written back to glTF; only hand-authored sources can be saved
   as they are.
6. **The scene source serializer is lossy** (Plan 0049 defect F5; recorded by the Correction above, found after
   Approval): `serializeSceneSource()` prints floats with six fixed decimals and drops fog and bloom values while
   their group is off, so a save would change values and an in-memory cook from text would not equal one from the
   document. The light limits and the component-group rules are checked by `parseSceneSource()`, not by the cooker,
   and the serializer writes at most one of a node's mesh, camera and light, dropping the others silently.

### Why now

Plan 0064's checkpoint (J7, 2026-10-21) adopted the editor platform, and Spec 0064 names E3 as the next editor
iteration. Every E3 screen — a scene Hierarchy, an authoring Inspector, saving — needs this layer, and its shape
(where the document lives) decides the wire, the SDKs and undo for every later iteration. Deciding it without UI
keeps the decision about data.

## Goals

- **One owner for the document being edited,** so the editor, the CLI, scripts and agents edit the same document
  and see each other's changes (ADR-0125 D2).
- **Fine-grained, transactional, observable edits** over the document, in the shape clients already know from the
  Runtime World — enough for an E2-style undo history to target it.
- **A change reaches the Runtime World** through the existing cook → decode → bake, with the result identical to
  starting the Runtime on the saved document.
- **Save and load** of hand-authored scene sources, byte-stable with the existing serializer.
- **The same capability on every surface:** C++ in process, `atlantis.remote/1`, the CLI, the C# SDK.
- **No second implementation** of the scene grammar, the scene schema's constraints or the cooker.

## Non-Goals

- **Editor UI of any kind:** no Hierarchy, Inspector or Gizmo in C# or ImGui over the document, no saving UI, no
  asset browser, no structural-edit undo UI. (The ImGui editor stays frozen at E2, ADR-0125 D4.)
- **Prefab, C# components, plugins, MCP.** Named only, as in Spec 0051.
- **Any new Runtime World verb, event, query, component or error** (ADR-0103, ADR-0104 unchanged).
- **A change to the scene source grammar, the artifact format, the catalog or the semantic version.** The
  serializer's output changes (R9, the F5 fix); the grammar it writes does not.
- **Incremental or live projection** of document edits into the Runtime World without a reload (named in Out of
  Scope; Q4 records why).
- **Saving imported scenes,** "save as" to a new catalog entry, multi-document sessions, and editing without a
  running Runtime.
- **Reordering nodes after creation.** A node's position in the node order is chosen when it is created and is
  immutable afterwards; `SetParent` changes the parent only. A move command is future work.
- **Typed C# bindings of the authoring components** (Q8): E3's C# session is reflective only.

## Requirements

### Functional

- **R1 — The authoring session.** One authoring session per `atlantis_runtime` process holds at most one open
  document: an `AuthoringScene` loaded from a scene source, with its provenance (hand-authored source path, or
  imported) and the scene's AssetGuid. Clients never hold their own copy (ADR-0129).
- **R2 — Open.** A session opens the document of the scene the Runtime loaded, by its catalog entry, through the
  existing parse and mapping (`parseSceneSource()`, `toAuthoringScene()`). An imported scene opens from its
  generated source and is marked not savable.
- **R3 — Queries.** A client can list nodes (in instantiation order), read a node's components, read a property by
  `scene::PropertyAddress`, read the document's active camera, and read the schema of the authoring components
  (`assetSystemSchema()`'s descriptors, as `connection.schema` serves the World's).
- **R4 — Commands.** A closed set over the document: set a property; create a node (with a client-chosen or minted
  EntityGuid, at a position in the node order, immutable afterwards); destroy a node; add and remove a component;
  set a node's parent (the node keeps its position in the order); set the active camera. Values are schema-primitive `PropertyValue`s addressed by `TypeId` / `FieldId`, as on the
  Runtime World.
- **R5 — Transactions and validity.** Commands are submitted singly or as one transaction, all or nothing. The
  document is valid — by `sceneSchema()` and the cooker's own checks — after every applied transaction; a
  transaction that would leave it invalid is refused whole, with a reason, and nothing changes.
- **R6 — Events.** Applied changes are reported as events (property changed, node created, node destroyed,
  component added, component removed, parent changed, active camera changed, document reloaded from disk), and a
  successful reload of an applied document as `Applied{generation}` carrying the Runtime's new scene generation
  (R8). They are delivered per subscription with filters, as on the Runtime World. Every client sees every client's
  changes and every apply. These are authoring events; the Runtime World's closed event set is unchanged.
- **R7 — Apply.** An explicit apply cooks the current document in memory with the build's cook path — the
  document's text from R9's serializer, parsed and checked by the same code as the build — decodes it to
  `ValidatedSceneData`, and hands it to the Runtime for reload (R8). Apply reports success or the reason it failed;
  the document is unchanged either way.
- **R8 — Reload.** The Runtime replaces its scene at a frame boundary, all or nothing: it resolves and uploads any
  mesh or material the new scene needs (reusing those already loaded), bakes, and swaps `scene_` and its World
  access. On failure the old World stays, untouched.
  - The result is identical to starting the Runtime on the saved document: the same frame data and a
    byte-identical capture.
  - Open `RuntimeConnection`s stay open and are re-bound to the new World; their subscriptions keep their filters;
    no Runtime World event is emitted for the reload itself; commands applied before the boundary are applied to
    the old World and are not carried over.
  - `RuntimeStatus` gains a scene generation, incremented by every reload; authoring subscribers also receive
    `Applied{generation}` (R6), so no client has to poll for a reload (ADR-0131).
  - **Resources the new scene no longer references** (meshes, materials, textures, their GPU objects and their
    pick geometry) are released at the swap, once the GPU has finished the last frame that used them; the
    Runtime's resource maps then hold exactly what a fresh start on the new document would load.
- **R9 — Save.** Save writes the document to its source path atomically, through `toParsedSceneSource()` and
  `serializeSceneSource()`, which is lossless (Correction 2026-10-22): every float in its shortest round-trip form,
  and a camera's fog and bloom groups each written unless it equals the no-group default, compared bit for bit
  (second Correction 2026-10-22). Parsing the saved text gives back the
  document, every value bit for bit. Saving a document that has not changed since its last save rewrites the same
  bytes; the first save of a hand-authored file may rewrite its number formatting, never a value. An imported
  scene's save is refused.
- **R10 — Runtime-side edits.** Edits made on the Runtime World (E2's editor, the CLI's `property set`, the
  Gameplay SDKs) are not written back to the document. They last until the next reload. E2's history is cleared
  on a generation change, as it is today on a scene change.
- **R11 — The shared surface.** The authoring interface is reachable in process, over `atlantis.remote/1`
  (`authoring.*` methods), from the CLI (a `scene` command group) and from the C# SDK, with conformance vectors for
  every new wire value (ADR-0130).
- **R12 — The second edit target.** The authoring interface offers everything ADR-0124 D4's edit target needs:
  read an address, submit ordered writes as one transaction returning a handle, report the handle's settlement,
  report invalidations. A test-only target over it runs E2's unchanged history.

### Non-functional

- **Latency:** authoring calls are answered between frames like every wire call (ADR-0106 D1). Apply's reload
  happens at the next frame boundary.
- **Cost:**
  - **Validation:** every transaction is validated by an in-memory cook of the whole candidate document, on the
    frame thread (Plan J2), so its cost grows with the document, not with the edit: on Bistro (about 6,000 nodes)
    it is paid by every single `SetProperty`. Measured and reported against a proposed budget (Plan J9);
    incremental validation is the follow-up if the budget is missed.
  - **Apply:** apply on Bistro (cook, decode, bake, swap, no new GPU uploads) is measured and reported against a
    proposed budget (Plan J3).
- **Threading:** frame thread only (ADR-0004). No thread is added.
- **Dependencies:** none new.
- **Determinism:** the in-memory cook is byte-identical to `cookScene()`'s artifact for the document's saved text;
  save is byte-identical to `serializeSceneSource()`, and the serializer is lossless and idempotent (serializing
  the parse of its own output gives the same text).
- **Performance contract:** the reload touches `runFrame()` (the swap point) and `src/runtime/**`, which are on the
  gate's applicability list. The implementing PR runs the gate, reports its verdict and enumerates the changed
  `runFrame()` lines. Without an apply, the frame path is unchanged.

## Proposed Design

```
 Atlantis Editor App (C#) ─┐   CLI `scene …` ─┐   agents / scripts ─┐
                           ▼                  ▼                     ▼
               atlantis.remote/1:  connection.*   control.*   authoring.*        (one session file)
 ──────────────────────────────────────────────────────────────────────────── atlantis_runtime process
   RuntimeConnection ─┐         RuntimeControl ─┐        AuthoringConnection ─┐
                      ▼                         ▼                             ▼
            Runtime World (BakedScene)   RuntimeControlHost        Atlantis Authoring: AuthoringSession
                      ▲                                             AuthoringScene + provenance
                      │  reload at a frame boundary                 queries / commands / events
                      │  (R8; all or nothing)                       validate (sceneSchema + cook checks)
                      └──── ValidatedSceneData ◄── decode ◄── encode (cook in memory) ◄── apply (R7)
                                                                    save → serializeSceneSource (R9)
```

- **Atlantis Authoring** (`src/authoring/`, a new module) owns the session: the document, its provenance,
  validation, transactions, events and apply. It depends on Asset System (the document, the codecs, the schema)
  and Atlantis Connection (the interface it implements). It names no Runtime, ECS, Platform or GPU type: apply
  ends in a narrow "scene sink" the host provides.
- **`AuthoringConnection`** (in Atlantis Connection, beside `RuntimeConnection`) is the interface every client uses;
  its InProcess endpoint lives in Atlantis Authoring; Remote carries it as `authoring.*`.
- **The host** (`atlantis_runtime`) creates the session on request, wires its sink to
  `RuntimeApplication::requestSceneReload()`, and serves it over `--listen`.
- **Asset System** gains a lossless `serializeSceneSource()` (the F5 fix), an in-memory cook (the document
  serialized, then `cookScene()`'s own parse, checks and `encodeSceneArtifact()`, which `cookScene()` then writes) and an in-memory decode entry (`decodeScene()` reading bytes), keeping decode the only
  constructor of `ValidatedSceneData`.
- **Runtime** gains `requestSceneReload(ValidatedSceneData)` and the frame-boundary swap, re-binding its InProcess
  endpoint; `RuntimeStatus` gains `sceneGeneration`.

## Architectural Impact

Yes — three decisions, each its own ADR (all `Accepted` 2026-10-22):

- **ADR-0129 — ownership** (Q1, Q2, Q6): a C++ authoring session is the one owner of the document being edited,
  hosted by the `atlantis_runtime` executable; clients never hold a copy and no second grammar or cooker is
  written; save and provenance rules. **Extends** ADR-0102: the Runtime *process* now hosts the authoring stage
  (parsing source) beside the Runtime, while `RuntimeApplication`'s load path and the bake still consume cooked,
  decoded data only — Spec 0051's B1 holds for the Runtime, not for the executable's other hosted modules. Keeps
  ADR-0100 and ADR-0125.
- **ADR-0130 — the access interface and module boundary** (Q3, Q5, Q7, Q8): `AuthoringConnection` with the four
  concepts over the document; the new module Atlantis Authoring; `authoring.*` on the wire, a CLI command group,
  a C# SDK session. **Supersedes in part** ADR-0106 D2 (the wire's payload gains the authoring interface) and
  ADR-0105 D1's "exactly the four concepts over the Runtime World" (by adding a sibling interface — the Runtime
  connection itself is unchanged). Keeps ADR-0103/0104.
- **ADR-0131 — apply and reload** (Q4): explicit apply, an in-memory cook byte-identical with the build's, decode
  as the only constructor of `ValidatedSceneData`, an all-or-nothing reload at a frame boundary, connections
  re-bound, a scene generation in `RuntimeStatus`. **Supersedes in part** Spec 0051's "no re-bake or hot reload"
  Non-Goal as recorded in ADR-0102, ADR-0105 D2 (one World per process life), ADR-0106 D3 (`RuntimeStatus`'s
  fields) and ADR-0124 D2 (history clearing on a generation change).

Module boundaries change: Atlantis Authoring is a new module; Atlantis Connection's dependency statement (Core,
and World's access and schema headers) gains one Asset System header, `scene_property_address.h`; Asset System's
decode boundary gains an in-memory entry and its scene source serializer becomes lossless (Correction
2026-10-22); the GUID mint, today private to the asset cooker tool (ADR-0097 D1's
"only place a GUID is minted"), moves to a home the authoring session can share (Plan J8). These are listed in Plan 0065
and land as normative lines in AGENTS.md and `module_boundaries.md` with the implementation.

## Alternatives Considered

The alternatives are recorded with each open question below; the principal ones:

- **The Runtime hosts the document inside `RuntimeApplication`** (Q1 (a)): one class owns both the Runtime World
  and its source, and authoring verbs join `RuntimeConnection`. Rejected for mixing two documents behind one
  surface and for putting a parser on the Runtime's load path.
- **Each client owns its document** (Q1 (b)): a C# grammar, schema checker and cooker beside the C++ ones. Rejected
  for the duplication and because no two clients could edit the same document.
- **A separate authoring process** (Q2 (D-b)): cleaner isolation, at the cost of a second endpoint, a second
  session file and a cross-process reload; kept as the deployment the module boundary allows later.
- **Live projection of edits into the Runtime World** (Q4 (B-c)): fast feedback, but a second mapping from authoring
  to runtime state beside the bake, which Spec 0051 made the only one.

## Testing & Verification Plan

- **Authoring core (GPU-free):** each command and its events; transactions all or nothing; every `sceneSchema()`
  constraint refusing a transaction (one negative case each, reusing the Spec 0049 conformance cases); the
  document after any sequence equal to re-parsing its saved text (R1–R6, R9).
- **Serializer (F5):** for every committed scene and Bistro when present, parsing the serialized document gives
  back every value bit for bit; serializing that parse gives the same text (idempotence); floats that six decimals
  cannot hold (subnormals, large magnitudes, `-0`, values such as `0.1234567`) and fog and bloom groups that are off
  but hold values round-trip, an all-zero group included, and a camera at the no-group defaults writes no group
  (second Correction); every committed scene's artifact, cooked from its unchanged source file, stays
  byte-identical (the cook does not serialize), and Bistro's generated source cooks to the same artifact as before.
- **Cook in memory:** for every committed scene and Bistro when present, the in-memory artifact equals
  `cookScene()`'s file byte for byte — of the scene's own source file and of its saved text — and the in-memory
  decode equals `decodeScene()`'s `ValidatedSceneData` (R7); a document breaking a parse-level rule (a point light
  beyond the limit) is refused by the in-memory cook as the build would refuse its text, and a document the text
  cannot represent (a camera and a light on one node) is refused by the round-trip check rather than narrowed.
- **Reload (GPU, fatal VVL):** after an apply of an edited document, frame data and a capture equal those of a
  fresh Runtime started on the saved document (R8); a failing reload (an unresolvable asset) leaves the old World,
  frame data unchanged; connections survive a reload, a subscription keeps its filter, `sceneGeneration`
  increments and authoring subscribers receive `Applied{generation}`; Bistro's apply time measured.
  - **Scene-sized state on the reload path:** an edited document that adds materials, textures and more lights
    than the original (within the scene limits) reloads to frame data and a capture equal to a fresh start, so the
    Renderer state sized by the scene — descriptor pool capacity (Spec 0021), the light clustering, shadows — is
    exercised by a reload, not only by a start.
  - **Release:** applying back to the original document leaves the resource maps equal to a fresh start's.
- **Validation cost:** one single-property transaction on Bistro's generated document, measured (Plan J9).
- **Runtime-side edits:** a CLI `property set` survives until the next apply and is gone after it; E2's history
  clears on the generation change (R10).
- **Wire, CLI, C# SDK:** codec round trips and conformance vectors for every new value; the CLI's `scene` commands'
  `cli/1` envelopes; the reflective C# session against the fixture server (R11, Q8).
- **The second target:** E2's `EditHistory`, unchanged, over a test-only target on `AuthoringConnection`: a
  recorded edit undoes and redoes; a concurrent client's change refuses an undo ("changed elsewhere") (R12).
- **North star:** on the default scene and Bistro, through `atlantis.remote/1` only (the CLI): open, set a light's
  intensity, reparent a node, apply — the Runtime shows it — save, rebuild, restart: the same frame data.
- **Regression:** Debug and Release full suites; goldens byte-identical; `assembleDebug`; the C# SDK's zero-NuGet
  check; the performance gate's verdict.

## Risks & Open Questions

Risks:

- **Reload cost on large scenes.** A full re-bake of Bistro on every apply may be slow enough to shape the editor's
  workflow. It is measured (R8); incremental projection is the named follow-up if it is.
- **Runtime-side edits disappearing at apply** may surprise users who tweak the Runtime World and then apply. R10
  makes it a rule; the editor's UI (not this Spec) must show it.
- **The Runtime process grows a second responsibility.** ADR-0129 contains it as a hosted module, like the CLI and
  the Remote server, with no Runtime dependency in the module itself, so a separate host stays possible.
- **The wire grows by a third interface.** Every SDK and the conformance vectors grow with it (R11); this is the
  cost ADR-0125 D2 accepted.

Open questions — all eight ruled by the owner (slmao, 2026-10-22, review of [PR #280](https://github.com/slmao/Atlantis/pull/280)), each as its
recommendation; rulings recorded in Plan 0065:

- **Q1 — Where does the authoring document live during editing?** (ADR-0129)
  - **(a) Inside the Runtime: `RuntimeApplication` hosts the document and authoring verbs join the shared surface,
    as part of or beside `RuntimeConnection`.**
    - *Wire / SDKs:* one more set of methods; one endpoint.
    - *Undo:* authoring and runtime edits in one process, but on two documents behind one owner, so an undo history
      must still tell them apart.
    - *Save / load:* the Runtime gains file writing for source text.
    - *Agents:* reach it like any other call.
    - *Spec 0051 B1:* broken where it matters most — the Runtime's own load path would parse source text, and the
      Runtime class would own both the World and its source. Needs ADR-0102 and ADR-0105 superseded at their
      centre.
  - **(b) The client owns the document:** the C# editor, the CLI and the SDKs each hold a copy, a second
    implementation of the grammar, the scene schema's checks and the cooker in C# is kept to the C++ one by
    conformance vectors (the C# SDK's pattern), and a client pushes a cooked scene to the Runtime for reload.
    - *Wire / SDKs:* only a reload call; but the C# side reimplements cook — the artifact format, every
      constraint, the light limits — and every future grammar change lands twice.
    - *Undo:* entirely client-side, and per client.
    - *Save / load:* each client writes files.
    - *Agents:* cannot see or edit what the editor has open; two clients editing one scene diverge. This breaks
      ADR-0125 D2's one interface for GUI, CLI, scripts and agents.
    - *Spec 0051 B1:* kept (the Runtime receives cooked data).
  - **(c) A C++ authoring service that every client edits through, which cooks and pushes** — **recommended**:
    a library module (Atlantis Authoring) holding the one document, its validation, transactions, events, save
    and apply, reached through one more interface on the shared surface (Q3, Q5).
    - *Wire / SDKs:* one more interface, served by the same endpoint (Q2); the C# SDK is a wire client only.
    - *Undo:* an E2-style history targets the authoring interface exactly as it targets the Runtime World (Q7);
      runtime-side edits stay separate and are cleared at reload (Q4).
    - *Save / load:* in C++, through the existing serializer.
    - *Agents:* edit the same document the editor shows, and see each other's changes as events.
    - *Spec 0051 B1:* kept for the Runtime — its load path and the bake take decoded artifact data, which apply
      produces through the build's own cook (Q4).
  - **Why (c):** it is the only option where every client edits one document without a second grammar, and it
    keeps the Runtime World a pure bake product. (a) would merge the two documents Spec 0051 separated; (b) would
    duplicate the cooker and split the document per client.

- **Q2 — Which process hosts the authoring service?** (ADR-0129)
  - **(D-a) The `atlantis_runtime` executable, beside the CLI (`--exec`) and the Remote server (`--listen`)** —
    **recommended**. One process, one session file, one connection per client; apply hands the decoded scene to
    `RuntimeApplication` in process. The module itself names no Runtime type (a scene sink the host provides), so
    D-b remains a deployment change. A Runtime crash loses unsaved edits.
  - **(D-b) Its own process** (`atlantis_authoring`), with its own session file, pushing reloads to a Runtime over
    the wire. Survives Runtime restarts and allows editing with no Runtime; costs every client a second endpoint,
    a second discovery step and a cross-process reload call on the control surface.
  - **(D-c) In every client** (a library each client links): this is Q1 (b) again for the C# editor.
  - **Why D-a:** E3's editor always has a Runtime (it embeds its Viewport, Spec 0064); one endpoint keeps the
    discovery and the SDKs simple. D-b is kept open by the module boundary.

- **Q3 — The access interface's shape and home.** (ADR-0130)
  - **(I-a) A sibling interface, `AuthoringConnection`, with the same four concepts — Query, Command, Event,
    Transaction — over the document, in Atlantis Connection** — **recommended**. Addresses are
    `scene::PropertyAddress`; values are the existing `PropertyValue`; commands are R4's closed set; refusal
    reasons and events are their own closed sets. Clients reuse every pattern they know (tickets, subscriptions,
    drained failures), and an E2 edit target fits it unchanged (Q7).
  - **(I-b) Authoring verbs added to `RuntimeConnection`:** one interface, but two documents behind it with the
    same GUIDs and different states, and the Runtime World's closed surface reopened for something that is not
    the Runtime World.
  - **(I-c) Whole-document get / put** (snapshot in, snapshot out): simple, but no fine-grained events, no
    concurrent editing, no undo target, and every edit by an agent sends the whole document.
  - **Validity, (V-1) after every transaction** — **recommended** — or (V-2) only at apply: V-1 lets every reader
    rely on a valid document, at the cost of a validation per transaction (Plan J9 measures it); V-2 lets a
    multi-step edit pass through invalid states, which a transaction already allows without it.
  - **PropertyValue's home:** reused from World access as is; ADR-0124's hoisting is not needed by this Spec.
  - **Why I-a + V-1:** it is the Runtime World's model, which every client and the E2 seam already speak.

- **Q4 — How a change reaches the Runtime World.** (ADR-0131)
  - **Trigger, (B-a) an explicit apply** — **recommended**. Edits accumulate in the document; apply cooks,
    decodes and hands the scene to the Runtime. Predictable cost, one reload per apply; a client wanting
    immediate effect applies after each transaction.
  - **Trigger, (B-b) automatic after every transaction:** immediate, but a full re-bake per keystroke-sized edit
    on Bistro, and no way to batch.
  - **Trigger, (B-c) live projection:** property edits on nodes whose baked entity has the same GUID are mirrored
    to the Runtime World as World commands; structural edits re-bake. Fast feedback, but a second authoring →
    runtime mapping beside the bake, which Spec 0051 made the only one. Named as the follow-up if apply's
    measured cost demands it.
  - **Reload, (L-a) an all-or-nothing replace at a frame boundary, connections re-bound** — **recommended**. The
    Runtime resolves and uploads the new scene's assets (reusing those already loaded), bakes, swaps the World and
    its access between frames, and releases what the new scene no longer references; on failure nothing changes.
    Open connections stay and are re-bound; subscriptions keep filters; no Runtime World event is emitted for the
    reload. Clients learn of it from the authoring event `Applied{generation}` and from
    `RuntimeStatus.sceneGeneration`. Needs `runFrame()`'s swap point (enumerated in the PR).
  - **Reload, (L-b) connections closed at reload:** clients reconnect; simpler in the Runtime, but every client,
    the editor's embedded Viewport session included, must handle a disconnect as a normal event.
  - **Reload, (L-c) a reload event on `RuntimeConnection`:** a sixth event on the closed surface. Rejected.
  - **Why B-a + L-a:** one reload per intent, the bake stays the only mapping, and clients keep their sessions.

- **Q5 — What the wire, the CLI and the C# SDK expose.** (ADR-0130)
  - **(S-a) Everything in R3–R9 on all three** — **recommended**: `authoring.*` methods on `atlantis.remote/1`
    (open, the queries, submit, submitTransaction, subscribe, drainEvents, drainFailures, apply, save), a CLI
    `scene` group (`scene open | nodes | get | set | create | destroy | add | remove | parent | camera | apply |
    save | tx`, with `Applied` among the events `scene` subscriptions print) in the existing `cli/1` envelope, and
    a C# `AuthoringSession` whose shape Q8 decides. Conformance vectors for every new value.
  - **(S-b) The wire and the CLI now, the C# SDK with E3's UI Spec:** less now, but ADR-0125 D2 asks for every
    surface in the same Spec.
  - **Why S-a:** ADR-0125 D2.

- **Q6 — Save, load and provenance.** (ADR-0129)
  - **(P-a) Open the loaded scene's catalog source; save only hand-authored sources, atomically, through the
    existing serializer; imported scenes are editable and applyable but not savable** — **recommended**. Bistro
    can be edited and applied for a session; saving it needs an overlay model (Out of Scope).
  - **(P-b) Save imported scenes as a new hand-authored source:** needs minting a catalog entry and a "save as"
    flow; deferred.
  - **(P-c) Open any source path a client names:** flexible, but the document would then not be the scene the
    Runtime shows, and apply would replace the Runtime's scene with a different one.
  - **Why P-a:** the document is always the Runtime's scene, and nothing writes a build product.

- **Q7 — What ADR-0124's second edit target binds to.** (ADR-0130)
  - **(T-a) This Spec proves the seam with a test-only target over `AuthoringConnection` running E2's unchanged
    history** — **recommended**. The production target is written where E3's history lives: the C# port of E2's
    models, Phase 2 of Spec 0064.
  - **(T-b) A production `AuthoringEditTarget` in the ImGui editor:** contradicts its freeze (ADR-0125 D4).
  - **(T-c) No target until Phase 2:** leaves R12 unverified.
  - **Why T-a:** R12 is verified now, and no frozen code grows.

- **Q8 — Typed or reflective C# access to the document.** (ADR-0130) `atlantis_sdk_codegen` generates bindings
  from `world::worldSchema()` only (`src/tools/sdk_codegen/main.cpp`); the authoring components are described in
  `assetSystemSchema()` and constrained by `sceneSchema()`, neither of which it reads.
  - **(G-a) Extend the generator to a second schema:** typed C# (and C++) bindings of the scene components, a new
    milestone, `src/tools/sdk_codegen/` on the file list, a second committed generated file with its staleness
    test, and ADR-0111 extended to more than one source table.
  - **(G-b) A reflective-only `AuthoringSession` in E3** — **recommended**: types and fields by name or id from
    `authoring.schema`, values as `PropertyValue`, exactly the Gameplay SDK's reflective layer (ADR-0110 D2) over
    the authoring interface. Typed bindings of the authoring components are deferred to Phase 2, when E3's UI
    shows what it needs typed.
  - **Why G-b:** the data layer needs no typed access to be complete, the reflective pattern already exists in both
    SDKs, and the generator and ADR-0111 stay untouched until a consumer asks for more.

## Out of Scope / Future Work

- E3's editor UI in Atlantis Editor App: a scene Hierarchy, an authoring Inspector, saving and apply in the UI,
  structural-edit undo — the next Spec, built on this one and on the C# port of E1/E2's models.
- Live projection of edits into the Runtime World (Q4 (B-c)), if apply's measured cost needs it.
- Incremental validation (checking only what a transaction's commands can affect), if Plan J9's budget is missed.
- Typed C# (and C++) bindings of the authoring components (Q8 (G-a)); a node move / reorder command.
- Saving imported scenes through an overlay model; "save as"; multi-document sessions; a separate authoring
  process (Q2 (D-b)); editing with no running Runtime.
- Prefab, C# components, plugins, MCP (named only).
