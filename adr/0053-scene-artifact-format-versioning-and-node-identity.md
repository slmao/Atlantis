# ADR 0053: Scene Artifact Format, Versioning, and Scene-Local Node Identity

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)

## Context

[ADR-0052](0052-scene-asset-module-boundary-and-ownership.md) decided
*where* scene cook/decode/instantiate logic lives. This ADR decides
*what bytes* a scene consists of at each of its three stages — human-
editable authoring source, cooked runtime artifact, and the in-memory
`DecodedScene` between them — and how one node refers to another
(parent/child, active-camera) at each stage. ADR-0035 requires this be
addressed explicitly, not defaulted into "authoring is runtime."

Two already-`Accepted` precedents bound this decision:

- **The mesh authoring format**
  (`src/asset_system/include/atlantis/asset_system/mesh_source.h`,
  `assets/meshes/minimal_cube.mesh.txt`): a strict, fixed-field-order,
  versioned, plain-text grammar (`field_name: value value ...` lines,
  counts declared up front, `std::from_chars` parsing) — human-editable,
  deterministic, no JSON/YAML library.
- **The mesh runtime artifact**
  (`src/asset_system/include/atlantis/asset_system/mesh_artifact.h`):
  a fixed-size binary header (magic, `schema_version`, an 8-byte-aligned
  `asset_id`, counts, offsets) followed by payload bytes, every
  multi-byte field assembled via explicit shift/mask — **never** a
  `memcpy` of a host C++ struct or its padding, unconditionally
  little-endian regardless of host endianness.

Scene data is structurally different from a single mesh in one way that
matters here: a mesh is one flat vertex/index array; a scene is a set
of nodes that **reference each other** (parent/child, active-camera)
and **reference an external asset** (a `Renderable`'s mesh). The
authoring format needs human-stable references; the runtime artifact
needs cheap, load-time-trustworthy ones.

## Decision

**Three distinct shapes, two transformation steps, matching ADR-0035's
own procedural requirement explicitly:**

1. **Authoring source**: a strict, versioned, plain-text grammar
   extending the mesh source's own established style — one line per
   field, fixed order, `std::from_chars`-parsed numbers, no general
   parser library. Each node is declared with an **explicit, author-
   assigned `node_id` (an unsigned integer, unique within one scene
   file, not implied by declaration order)** — not the array position
   it happens to appear at. A node's optional parent is authored as
   another node's own `node_id` (or an explicit "no parent" sentinel,
   e.g. `0` reserved and never a valid author-assigned id, or a literal
   `none` token — exact spelling is a Plan-level detail). A
   `Renderable`'s mesh reference is authored as a **logical path**
   (`atlantis::asset_system::LogicalPath`'s own existing normalization
   rules), never an `AssetId` — the same "author writes what a human
   can read; the cooker derives the opaque ID" split the mesh pipeline
   already establishes (`computeAssetId()` is deterministic over a
   normalized logical path; nothing about it changes here). Explicit
   IDs (not array position) are chosen specifically so an author can
   reorder or insert nodes in the source file without renumbering every
   parent reference elsewhere — and so that this ADR's own required
   `DuplicateNodeId` cook-time error is a real, reachable authoring
   mistake, not a structurally-impossible case.
2. **Cooked runtime artifact**: a fixed binary header (magic,
   `schema_version`, node count, an 8-byte-aligned root/active-camera
   reference, offsets) followed by one fixed-size record per node —
   unconditionally little-endian, explicit shift/mask assembly, the
   identical discipline `mesh_artifact.h` already established, never a
   struct `memcpy`. **The cooker remaps every author-assigned `node_id`
   to a dense, zero-based array index, in the node's own declaration
   order in the (already-validated) authoring source** — a parent
   reference in the artifact is a plain array index, not a hash lookup;
   this matches `World`'s own already-`Accepted` "ascending slot-index
   order" convention for deterministic enumeration
   ([ADR-0049](0049-entity-identity-and-handle-invalidation.md)) and
   keeps the loader's own parent/active-camera resolution O(1). A
   `Renderable`'s mesh reference is written as the already-resolved
   `AssetId` (`toLittleEndianBytes()`, the mesh pipeline's own existing
   encoding) — **the runtime artifact never contains a logical path or
   any other source-file-dependent string.** A metadata sidecar,
   text-format, mirrors the mesh pipeline's own convention (schema
   version, node count, a content hash or equivalent) and is
   cross-checked against the artifact at load time, matching
   `AssetLoadError::MetadataArtifactMismatch`'s own established
   contract.
3. **`DecodedScene`** (the in-memory value type
   [ADR-0052](0052-scene-asset-module-boundary-and-ownership.md)
   defines): a flat array, one entry per node, in the artifact's own
   dense array-index order, each entry carrying its local `Transform`,
   an optional `Camera`, an optional `Renderable{AssetId}`, its parent
   as a plain array index (or "no parent"), plus a separate
   active-camera array index (or "none"). No `node_id`, no logical
   path, and no `Atlantis::World` type appear anywhere in this
   structure — the authoring-time human-facing identity and the
   source-file mesh reference are both already resolved away by cook
   time; only `World`'s own real `EntityId`s, minted fresh at
   instantiation, are the durable identity from this point on
   (`EntityId`/`WorldIdentity` are never written to, or read from, any
   artifact — Spec 0014's own Non-Goal, unaffected by this Spec).
4. **Both the cooker and the loader independently validate their own
   input** — this repository's already-established stance
   (`AssetLoadError` exists specifically because a cooked artifact is
   never assumed trustworthy just because *some* cooker produced it).
   The cooker rejects, before writing any byte: a duplicate `node_id`,
   a parent `node_id` that names no declared node, a parent cycle (a
   node that is, transitively, its own ancestor — reusing the same
   walk-the-ancestor-chain algorithm
   [Plan 0014's own `setParent()`](../plans/0014-world-scene-foundation.md)
   already established, applied to the authoring graph instead of a
   live `World`), more than one node claiming the active-camera role
   (or the active-camera reference naming an undeclared `node_id`), and
   any non-finite (`NaN`/`Inf`) authored float. The loader
   independently re-validates the artifact's own internal consistency
   at load time (magic, schema version, every offset/count internally
   consistent, every parent/active-camera array index in range, no
   node its own transitive ancestor) — **never assuming a well-formed
   cooker output**, matching `ArtifactDecodeError`'s own existing
   philosophy for the mesh artifact exactly. A `Renderable`'s
   `AssetId` is *not* resolved against a known-asset table at decode
   time — mirroring `resolveMeshAsset()`'s own existing split
   ([ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)):
   decode only decodes; asset resolution happens where the consumer
   already knows what assets exist ([ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)).

## Consequences

### Positive

- Every byte-level discipline (little-endian, no `memcpy`, versioned
  header, atomic write, metadata sidecar cross-check) is a direct reuse
  of the mesh artifact's own already-`Accepted`, already-shipped
  pattern — no new format philosophy to review, only a new concrete
  layout.
- Explicit author-assigned `node_id`, remapped to dense array indices
  only at cook time, gives authors reorder-without-renumbering ergonomics
  while keeping the runtime artifact and `DecodedScene` as cheap and
  simple as the mesh artifact's own index buffer — no runtime hash
  lookup, no string anywhere in the hot path.
- `DecodedScene` naming no `EntityId`/`WorldIdentity`/pointer anywhere
  makes "never persist Runtime identity" (this Spec's own Non-Goal)
  true by construction, not by convention someone could forget.
- Duplicate-detection, cycle-detection, and non-finite-value rejection
  are required at **both** cook time and load time — an artifact that
  somehow reached disk in a bad state (hand-edited, corrupted,
  transferred from an incompatible build) still fails safely at load,
  not just at cook.

### Negative / Trade-offs

- Two ID spaces (authoring `node_id`, artifact array index) is one more
  concept than a single, unified identity — accepted because the
  alternative (author-visible array position as the only identity)
  makes the required `DuplicateNodeId` error case impossible to reach
  and couples authoring convenience to array order.
- The cooker must implement a cycle-detection walk independent from
  `World::setParent()`'s own runtime version (different data — an
  authoring-time node list, not a live slot map) — a second, small,
  disclosed implementation of the same *algorithm*, not a shared
  function; consistent with this repository's own "duplicated, not
  shared" precedent for exactly this class of small, independently-
  testable logic.

## Alternatives Considered

- **Array position is the only node identity (no explicit `node_id`).**
  Rejected: makes `DuplicateNodeId` — an error case this Spec's own
  Non-Goals-adjacent Requirements explicitly name — structurally
  unreachable, and forces an author to renumber every parent reference
  after reordering nodes in the source file.
- **A general string name as node identity** (e.g. `"camera_entity"`)
  instead of a small integer. Rejected: adds string comparison/hashing
  to cook-time validation and (if ever needed at runtime) to
  `DecodedScene`, for no capability this Spec's own scope needs (no
  Editor, no runtime scene query by name); a small integer is
  sufficient and matches every other identity scheme this codebase
  already uses (`AssetId`, `EntityId`'s own index).
- **Reuse `EntityId` itself as the artifact's own node reference type.**
  Rejected outright — `EntityId` is a non-owning, in-process handle
  whose three fields (index, generation, a live `World`'s own heap
  address) are meaningless across a process boundary or before the
  `World` that would own them exists yet
  ([ADR-0049](0049-entity-identity-and-handle-invalidation.md)'s own
  explicit prohibition, restated as this Spec's own Non-Goal).
- **Resolve `Renderable` mesh references to `AssetId` at load time,
  not cook time** (store the logical path in the artifact, resolve on
  every load). Rejected: reintroduces a source-path dependency into the
  runtime artifact this ADR's own Decision explicitly closes, and
  duplicates work (the same path would be normalized and hashed on
  every load instead of once at cook time) for no benefit — nothing
  about a mesh's own logical path can change without re-cooking the
  mesh itself, so resolving early loses no information the artifact
  would ever need later.
