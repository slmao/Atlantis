# ADR 0100: Scene Semantic Schema — Layering, Authoring Model, and Semantic Versioning

- **Status:** Accepted
- **Date:** 2026-10-05 (accepted 2026-10-05)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-05 (review of this branch's own PR,
  [PR #202](https://github.com/slmao/Atlantis/pull/202); accepted together with Spec 0049's Approval, its seven open
  questions ruled as recommended)
- **Related Spec:** [Spec 0049: Schema-driven Authoring Scene](../specs/0049-schema-driven-authoring-scene.md) (`Approved`)
- **Related ADR(s):** builds on
  [ADR-0099](0099-engine-schema-core-and-descriptor-vocabulary.md) (vocabulary
  and module-owned tables; extended in use, not amended),
  [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) (AssetGuid,
  EntityGuid, `EntityRef`) and
  [ADR-0052](0052-scene-asset-module-boundary-and-ownership.md) /
  [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)
  (Asset System owns the scene asset; it never depends on World). It sits
  beside [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  format versioning, which it leaves unchanged.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **Scene meaning lives in codec code** (Spec 0049 Motivation):
  - component exclusivity follows from a token-count dispatch;
  - "material requires mesh" is a grammar property and, separately, a decode
    error;
  - one value domain is enforced in the parser, the cooker and the decoder
    with different error types;
  - scene-level light caps are parse errors.
- **The only version is a format version.** The source and artifact
  constants (both 7) each counted semantic and syntax changes together. A
  second serializer could not state which meaning it encodes.
- **References mix identity and syntax.** The persistent node identity is the
  EntityGuid (ADR-0097 D5), but in-document references use the file-local
  `node_id` (source) or array indices (artifact).
- **Spec 0048 ruling Q1 deferred describing the `Decoded*` DTOs** to the
  scene-authoring spec. Spec 0048 identity is name-derived, so whatever this
  layer describes fixes persistent `TypeId`s.
- **The maintainer fixed the boundaries before drafting** (2026-10-05):
  - a semantic schema, not a syntax;
  - no grammar change and no artifact byte change;
  - Schema above Scene, Prefab and Editor; Serializer below Scene;
  - JSON5, binary, Editor Transaction, Agent Patch, Prefab and Editor named
    as future consumers only, with no design, implementation or scaffold.
- **Constraints that bind the answer:**
  - Asset System may depend only on Core (ADR-0043, ADR-0053);
  - World depends on Asset System, never the reverse;
  - Spec 0048 descriptors describe standard-layout C++ types with byte
    offsets and have no container kind.

## Decision

1. **Layering and dependency direction.**
   - **Schema:** Core's `atlantis::schema` plus module tables.
   - **Scene:** the authoring-scene semantic layer, a consumer of Schema.
   - **Serializer:** the scene source codec, the scene artifact codec and
     their cook/decode drivers, below Scene.
   - Serializers may name Scene types. Scene never names a serializer type or
     concept: no `node_id`, token, record offset or format version.
   - Mapping between a serializer's shapes and the semantic model is owned by
     the serializer side, so a new serializer adds a mapping without changing
     Scene.
   - Prefab and Editor are peer consumers of Schema. This ADR creates nothing
     for them.
2. **Home and model.**
   - The semantic layer lives in Atlantis Asset System, the scene asset's
     owner (ADR-0052). Its namespace is `atlantis::asset_system::scene`
     (Spec 0049 ruling Q6).
   - It consists of:
     - standard-layout semantic component value types, described by Spec 0048
       descriptors in `assetSystemSchema()`;
     - an in-memory authoring document;
     - one immutable scene schema stating identity, component set and
       cardinality, exclusivity, relations, value domains and document
       constraints as data.
   - The semantic model is **independent of the codec DTOs** (`Decoded*`,
     `ValidatedSceneData`). Those stay undescribed serializer-layer shapes;
     this answers Spec 0048 Q1's deferral (Spec 0049 ruling Q1).
3. **Identity and references.**
   - A node's identity is its EntityGuid, non-nil and unique per document.
   - Every reference from one node of a document to another (parent, active
     camera, and any future one) is EntityGuid-valued. `node_id`s and indices
     are serializer syntax.
   - Asset references in the authoring model are AssetGuids. The artifact's
     `AssetId` keys are a projection.
   - Node order is semantic: it is the instantiation order.
4. **v1 semantics are today's semantics.** The first scene schema states
   exactly what source v7 and artifact 7 accept. Adding, tightening or
   relaxing a constraint is a semantic-version change, never a side effect of
   codifying.
5. **Semantic version, decoupled from format versions.**
   - One aggregate scene semantic version starts at 1. It is pinned together
     with a deterministic fingerprint of the scene schema and every descriptor
     it references.
   - Each serializer declares the semantic version it encodes, beside its
     ADR-0045 format version.
   - A semantic change bumps the semantic version, re-pins the fingerprint,
     and requires every serializer to declare it (changing its format under
     ADR-0045 as needed) in the same change.
   - A format-only change leaves the semantic version alone.
   - Catalog schema numbers (ADR-0098) stay format-scoped.
   - This extends Spec 0048 R8's descriptor-level decoupling to the document
     (Spec 0049 ruling Q2).
6. **Contract plus conformance, not codec rewrite (for now).**
   - The scene schema is normative, and the existing codecs are held to it by
     schema-driven conformance tests: per-field round trips, one negative case
     per constraint, and declared-version agreement.
   - Source ⇄ semantic is lossless at the semantic level. Semantic → artifact
     is a declared projection.
   - Codecs become schema-driven, and `Decoded*` is retired, in the spec that
     introduces the next serializer (Spec 0049 ruling Q3).
7. **Reserved property address.**
   - A property inside a scene document is addressed by (node EntityGuid,
     component `TypeId`, `FieldId`). Components are keyed per node by
     `TypeId` with cardinality 0..1.
   - Cross-document entity references use `EntityRef` (ADR-0097 D6).
   - Document-level fields are kept separate from the node model.
   - These are the only Prefab and Editor reservations. Nothing for either is
     implemented (Spec 0049 ruling Q4).
8. **Domains and constraints are scene-local data** in Asset System. No Core
   vocabulary is added. Promotion to `atlantis::schema` needs a second
   consumer and its own ADR (Spec 0049 ruling Q7).

## Consequences

### Positive

- One normative statement of scene meaning. Today's scattered parser,
  cooker and decoder rules become checked implementations of it.
- A semantic change can no longer land silently: the fingerprint pin forces
  the version bump, and the serializers must declare it.
- A future serializer, Prefab or Editor starts from the same model,
  identity and address form, with no breaking change to them needed.
- No syntax, byte, golden or Runtime change. Asset System's dependency
  boundary is unchanged.

### Negative / Trade-offs

- A third shape per component exists until the codecs move onto the semantic
  types: `world::Camera`, `DecodedCamera` and the semantic camera. Tests keep
  them in agreement; conventions no longer do.
- Mapping code and conformance tests are new maintenance surface, paid per
  semantic field.
- Every semantic change now costs a version bump and a fingerprint re-pin,
  even a small one.
- The artifact cannot reproduce authoring identity for assets (GUID → key),
  so artifact conformance is a projection check, not a round trip.
- Scene-local domain vocabulary may later be generalized into Core, which
  would be a superseding decision.

## Alternatives Considered

- **Describe `Decoded*` / `ValidatedSceneData` as the semantics.** It freezes
  codec names into persistent IDs. The documents are outside the
  standard-layout vocabulary. The cooked side holds keys, not authoring
  identity.
- **World components as the scene semantics.** It inverts ADR-0053's
  dependency direction, puts the serializer above the semantics, and mixes
  runtime keys with authoring GUIDs.
- **Schema-driven codecs now.** It is a large dual-codec refactor under a
  byte-identity requirement, with no second serializer to justify it yet.
- **Layout-free semantic descriptors.** These need a Spec 0048 vocabulary
  change and lose the sync tests.
- **Format version doubling as semantic version.** That is today's
  conflation.
- **Generic container and node-reference kinds in Core.** This is
  speculative generality for one consumer. Revisit with Prefab.
