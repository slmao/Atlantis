# ADR 0130: The Authoring Access Interface, Atlantis Authoring, and Their Reach

- **Status:** Accepted
- **Date:** 2026-10-22 (accepted 2026-10-22)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-22 (owner rulings on Spec 0065 Q1–Q8, each as recommended, recorded once in
  [Plan 0065](../plans/0065-authoring-data-layer.md); the ADR text was reviewed in [PR #280](https://github.com/slmao/Atlantis/pull/280))
- **Related Spec:** [Spec 0065: The Authoring Data Layer](../specs/0065-authoring-data-layer.md) (`Approved` 2026-10-22; its Q3, Q5,
  Q7 and Q8)
- **Related ADR(s):**
  - **Supersedes in part** [ADR-0105](0105-runtime-connection-and-cli-client.md) D1 and D3: Atlantis Connection now
    holds a second client interface, `AuthoringConnection`, beside `RuntimeConnection`. `RuntimeConnection` itself
    — exactly the four concepts over the Runtime World — is unchanged.
  - **Supersedes in part** [ADR-0106](0106-attachable-runtime-transport-and-control.md) D2: the wire's payload is
    `RuntimeConnection`, `RuntimeControl` and `AuthoringConnection` calls. Transport, framing, the session file
    and draining are unchanged; the version string stays `atlantis.remote/1` (additive methods).
  - Keeps [ADR-0103](0103-runtime-world-operation-boundary.md) and
    [ADR-0104](0104-runtime-world-transaction-atomicity.md): no Runtime World command, event or query changes.
    This interface's own command and event sets are closed in the same way.
  - Keeps [ADR-0100](0100-scene-semantic-schema-layering-model-and-versioning.md) D7: addresses are
    `scene::PropertyAddress`.
  - Keeps [ADR-0112](0112-csharp-gameplay-client-transport-and-bindings.md) D2: conformance vectors cover every new
    wire value.
  - **Extends** [ADR-0124](0124-editor-transaction-and-client-side-undo.md) D4: the authoring interface is
    sufficient for a second edit target; the production target is Phase 2's.
  - Ownership is [ADR-0129](0129-authoring-document-ownership.md); apply and reload are
    [ADR-0131](0131-authoring-apply-and-runtime-scene-reload.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **Clients already speak one model:** Query / Command / Event / Transaction, with tickets, subscriptions and
  drained failures (ADR-0103–0105). The CLI, both SDKs and E2's edit target are built on it.
- **The authoring document is not the Runtime World.** Its GUIDs are the same, its state differs (hierarchy,
  unbaked values, uncommitted edits), and its operations differ (reparenting, a node's place in the node order
  at creation, the active camera).
- **ADR-0125 D2:** every capability on every surface, in the same Spec.
- **E2's edit target** (ADR-0124 D4) asks for: read an address; submit ordered writes as one transaction with a
  handle; the handle's settlement; invalidations.

## Decision

1. **`AuthoringConnection`, a sibling of `RuntimeConnection`,** in Atlantis Connection
   (`atlantis/connection/authoring_connection.h`), with the same four concepts over the open document:
   - **Queries:** the document's identity and provenance; nodes in order; a node's components; a property by
     `scene::PropertyAddress`; the active camera; the authoring schema (the descriptors of `assetSystemSchema()`'s
     scene components).
   - **Commands, a closed set:** `SetProperty`, `CreateNode` (EntityGuid given or minted; position in the node
     order, immutable afterwards; initial components), `DestroyNode`, `AddComponent`, `RemoveComponent`,
     `SetParent` (the parent only; the node keeps its place in the order), `SetActiveCamera`. Reordering is not a
     command (Spec 0065 Non-Goals).
     Values are `world::access::PropertyValue`, addressed by `TypeId` / `FieldId` as on the Runtime World; not
     hoisted to a new home by this ADR.
   - **Transactions:** submitted singly or as one transaction, all or nothing, with tickets. **The document is
     valid after every applied transaction** — by `sceneSchema()` and the cooker's checks — and a transaction that
     would break validity is refused whole, with a reason from a closed set (the Spec 0049 constraint names plus
     the addressing errors).
   - **Events, a closed set,** per subscription with filters: `PropertyChanged`, `NodeCreated`, `NodeDestroyed`,
     `ComponentAdded`, `ComponentRemoved`, `ParentChanged`, `ActiveCameraChanged`, `DocumentReopened`, and
     `Applied{generation}` — emitted when the Runtime has swapped in an applied document, carrying its new
     `RuntimeStatus.sceneGeneration` (ADR-0131 D5). They are authoring events: the Runtime World's closed event set
     (ADR-0103) is unchanged.
   - **Session calls:** `open`, `apply` (ADR-0131), `save` (ADR-0129 D4), each with a closed error set.
   - It names no Runtime, ECS or GPU type. Connection's dependency statement (Core, and World's access and schema
     headers) gains one Asset System header, `scene_property_address.h`, which World already depends on.
   - **Minting:** a `CreateNode` without an EntityGuid mints one. Creating a node is an explicit authoring command,
     so this keeps [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) D1; the mint moves from the asset cooker
     tool to a home both share, still the only minting code.
2. **Atlantis Authoring** (`src/authoring/`, `atlantis::authoring`) is a new module: the `AuthoringSession`, its
   InProcess endpoint implementing `AuthoringConnection`, validation, events and apply's in-memory cook.
   - **Depends on:** Atlantis Asset System (the document, the codecs, the scene schema) and Atlantis Connection.
     No Runtime, World ECS, Platform, RHI, Renderer or OS header (a boundary test, as for the CLI and the editor).
   - **Depended on by:** the `atlantis_runtime` executable (the host), and its own tests. Android builds it; the
     Android host does not create a session.
3. **Every surface carries it** (Spec 0065 Q5, S-a):
   - **Wire:** `authoring.*` methods on `atlantis.remote/1`, one per call, with values encoded by the codec and
     documented in `remote_protocol.md`; Remote's client half implements `AuthoringConnection` as
     `RemoteAuthoring`, its server half serves the session the host gives it.
   - **CLI:** a `scene` command group in the existing `cli/1` envelope (open, nodes, get, set, create, destroy,
     add, remove, parent, camera, apply, save, tx); its event output includes `Applied`.
   - **C# SDK:** a reflective `AuthoringSession` on `RemoteSession` (Spec 0065 Q8, G-b) — types and fields by
     name or id from `authoring.schema`, values as `PropertyValue`, records for every command and event (`Applied`
     included), the pattern of the Gameplay SDK's reflective layer
     ([ADR-0110](0110-gameplay-sdk-client-library-and-execution-model.md) D2); a wire client only. Typed bindings
     of the authoring components are deferred to Phase 2: `atlantis_sdk_codegen` keeps reading `worldSchema()` only
     and [ADR-0111](0111-schema-generated-typed-bindings.md) is unchanged.
   - **Conformance vectors** for every new value and the C# codec held to them.
4. **The second edit target is proven, not shipped** (Q7, T-a): a test-only edit target over `AuthoringConnection`
   runs E2's `EditHistory` unchanged. The production target belongs to the C# port of E2's models (Spec 0064 Phase
   2); the frozen ImGui editor gains nothing (ADR-0125 D4).

## Consequences

### Positive

- **Clients reuse what they know:** the same four concepts, tickets, subscriptions and failures.
- **Undo targets the document exactly as it targets the Runtime World,** with no new history design.
- **The Runtime World's closed surface stays closed;** the authoring surface has its own closed sets.
- **Agents and the CLI edit the document through the same calls as the editor.**

### Negative / Trade-offs

- **A third interface on the wire,** with its codec, vectors, CLI commands and C# session — the cost ADR-0125 D2
  accepted.
- **Two interfaces with the same GUIDs:** a client must know which one it addresses; the CLI keeps them in
  separate command groups.
- **Validation per transaction** costs time proportional to the document, paid on the frame thread by every
  single-property edit (measured against Plan 0065 J9; incremental validation is the follow-up).
- **Reflective-only C# access** in E3: no compile-time field names for the authoring components until Phase 2.

## Alternatives Considered

- **Authoring verbs on `RuntimeConnection`** (Spec 0065 Q3 (I-b)): one interface over two documents, and the closed
  World surface reopened for something that is not the World.
- **Whole-document get / put** (Q3 (I-c)): no events, no concurrent editing, no undo target.
- **Validity only at apply** (Q3 (V-2)): every reader would have to cope with invalid documents; transactions
  already allow multi-step edits.
- **The C# SDK later** (Q5 (S-b)): against ADR-0125 D2.
- **A production edit target in the ImGui editor** (Q7 (T-b)): against its freeze.
- **Typed C# bindings now** (Q8 (G-a)): a second generator input, a second generated file and ADR-0111 extended,
  for a data layer that needs none of them.
- **A node move command:** not needed by the data layer; reordering is future work.
