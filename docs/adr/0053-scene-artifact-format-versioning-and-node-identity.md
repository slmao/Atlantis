# ADR 0053: Scene Artifact Format, Versioning, and Scene-Local Node Identity

- **Status:** Accepted
- **Date:** 2026-08-23
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-23 as part of Spec 0015's Human Review Approval
- **Related Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)
- **Acceptance Record (2026-08-23):** Accepted by Human Review as part
  of
  [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)'s
  own Human Review Approval (2026-08-23) — see that Spec's own approval
  note (items 2, 3, 5, 6) and its own Human Review Decision Table's
  "Where" column for the specific items this ADR corresponds to. This
  record does not change this ADR's own Decision, Consequences, or
  Alternatives Considered below. **This ADR's own Decision additionally
  carries a "Human Review Correction (2026-08-23)" section** (end of
  Decision, before Consequences) — `ValidatedSceneData` has no public
  default constructor at all (item 4's own original text describing a
  trivial empty-default case is superseded by this correction, left
  unedited above as the historical record of what was originally
  written); a zero-node scene is now an explicit, named cook-time and
  decode-time error instead. Neither the original Decision nor this
  correction is reopened to `Proposed`.

## Context

[ADR-0052](0052-scene-asset-module-boundary-and-ownership.md) decided
*where* scene cook/decode/instantiate logic lives. This ADR decides
*what bytes* a scene consists of at each of its three stages — human-
editable authoring source, cooked runtime artifact, and the in-memory
`ValidatedSceneData` between them — and how one node refers to another
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
   parser library. Illustrative shape (exact field names/order are a
   Plan-level detail; the *grammar discipline* is fixed here):

   ```
   atlantis_scene_source_version: 1
   node_count: 6
   active_camera: 6
   node: node_id=1 parent=none position=-2.5 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube
   node: node_id=4 parent=3 position=0.0 1.3 0.0 rotation=0.0 0.7854 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube
   node: node_id=6 parent=none position=0.0 2.2 7.0 rotation=-0.3054 0.0 0.0 fov_y=1.0472 near_z=0.1 far_z=100.0
   ```

   Each node is declared with an **explicit, author-assigned `node_id`
   (an unsigned integer, unique within one scene file, not implied by
   declaration order)** — not the array position it happens to appear
   at. A node's optional parent is authored as another node's own
   `node_id`, or the literal token `none` (never a bare `0` or other
   numeric sentinel, to avoid colliding with a legitimate author-chosen
   `node_id` of `0`). A `Renderable`'s mesh reference is authored as
   a
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
3. **`ValidatedSceneData` is an `Atlantis::AssetSystem`-owned value type
   using only `Atlantis::AssetSystem`/`Atlantis::Core`-owned field
   types — never `atlantis::world::Transform`/`Camera`/`Renderable`
   directly.** This is a hard constraint, not a stylistic choice: if
   `ValidatedSceneData` named a `world::Transform` field, `Atlantis::AssetSystem`
   would have to `#include <atlantis/world/transform.h>`, giving
   `AssetSystem` a real compile-time dependency on `Atlantis::World` —
   exactly the cycle [ADR-0052](0052-scene-asset-module-boundary-and-ownership.md)'s
   own Decision forbids (`World` already depends on `AssetSystem`).
   Concretely: a flat array of `ValidatedSceneNode`, one entry per node,
   in the artifact's own dense array-index order, each entry carrying
   a plain `DecodedTransform` (nine `float`s: position xyz, Euler
   radians xyz, scale xyz — the same flat-float shape
   `MeshSourceVertex` already establishes for authoring-facing data, not
   a shared struct with `world::Transform`), an optional
   `DecodedCamera` (`fovYRadians`/`nearZ`/`farZ`, three `float`s), an
   optional `DecodedRenderable` (one `AssetId`), its parent as a plain,
   **range-verified** array index (or a "no parent" sentinel), plus a
   separate, **range-verified** active-camera array index (or "none").
   `DecodedTransform`/`DecodedCamera`/`DecodedRenderable` are new,
   `Atlantis::AssetSystem`-owned types, not aliases for or conversions
   of any `Atlantis::World` type — converting each field into a real
   `world::Transform`/`Camera`/`Renderable` happens exactly once, inside
   `World`'s own new instantiation entry point
   ([ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)),
   which is the one place in this pipeline permitted to know both
   shapes. No `node_id`, no logical path, and no `Atlantis::World` type
   of any kind appears anywhere in `ValidatedSceneData` — the authoring-time
   human-facing identity and the source-file mesh reference are both
   already resolved away by cook time; only `World`'s own real
   `EntityId`s, minted fresh at instantiation, are the durable identity
   from this point on (`EntityId`/`WorldIdentity` are never written to,
   or read from, any artifact or `ValidatedSceneData` — Spec 0014's own
   Non-Goal, unaffected by this Spec — and a `ValidatedSceneNode`'s own
   array index is never treated as, compared against, or convertible to
   an `EntityId`).
4. **`ValidatedSceneData` is encapsulated so that "fully validated"
   is a type-level guarantee, not a caller convention.** This is what
   makes [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)'s
   own infallible `World` instantiation actually true, rather than an
   assumption resting on every caller behaving — reusing the exact
   pattern `atlantis::world::EntityId` already established
   ([ADR-0049](0049-entity-identity-and-handle-invalidation.md)): every
   structural field (the node array, each node's parent index, the
   active-camera index) is **`private`**; the only non-default
   constructor is **`private`**, callable only by `decodeScene()`
   itself (a `friend` relationship, matching `EntityId`'s own
   `friend class World;`); the public surface is **read-only accessors
   only** (`nodeCount()`, a per-index node accessor returning a `const`
   view, `parentOf(index)`, `activeCameraIndex()`) — **no setter, no
   mutable reference, no mutable iterator, and no way to reach a
   `ValidatedSceneNode`'s own fields except through these same
   read-only accessors exists anywhere on the public type.** A default
   constructor producing an empty (zero-node) instance is the only
   other public constructor — trivially valid, since an empty node
   array vacuously satisfies every structural/semantic condition below.
   Copy and move are both defaulted and preserve validity by
   construction: since nothing on the public surface can mutate a
   `ValidatedSceneData` after it exists, a copy or a moved-from/moved-to
   pair are, respectively, two independently valid instances or one
   valid instance relocated — there is no mutation path through which a
   copy or move could ever produce an invalid one. **No caller —
   `Atlantis Runtime`, a test, or any future consumer — can construct a
   `ValidatedSceneData` naming an out-of-range parent, a cycle, a
   duplicate structural position, or an active-camera index pointing at
   a component-less node, because no public constructor accepts
   arbitrary node data at all.** The only path to a non-empty instance
   is a successful `decodeScene()` call, which already performed every
   check in Decision item 5 below before that private constructor ever
   runs.
5. **Both the cooker and the loader independently validate their own
   input, and — critically — that validation is exhaustive enough that
   `ValidatedSceneData` is a fully-proven-valid type by the time anything
   downstream consumes it** (this is what lets
   [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
   make `World` instantiation infallible, rather than reusing
   `WorldError` for a scene-loading-specific condition). This
   repository's already-established stance — `AssetLoadError` exists
   specifically because a cooked artifact is never assumed trustworthy
   just because *some* cooker produced it — is extended here with one
   additional, semantic (not merely structural) check. The cooker
   rejects, before writing any byte: a duplicate `node_id`; a parent
   `node_id` that names no declared node; a parent cycle (a node that
   is, transitively, its own ancestor — reusing the same
   walk-the-ancestor-chain algorithm
   [Plan 0014's own `setParent()`](../plans/0014-world-scene-foundation.md)
   already established, applied to the authoring graph instead of a
   live `World`); more than one node claiming the active-camera role,
   or the active-camera reference naming an undeclared `node_id`;
   **the active-camera-referenced node itself declaring no `Camera`**
   (moved here, deliberately, from a candidate alternative of relying
   on `World::setActiveCamera()`'s own runtime `NoCameraComponent`
   check — see this ADR's own Alternatives Considered for why); and any
   non-finite (`NaN`/`Inf`) authored float. The loader independently
   re-validates every one of these same conditions against the
   artifact's own bytes at load time (magic, schema version, every
   offset/count internally consistent, every parent/active-camera array
   index in range, no node its own transitive ancestor, the
   active-camera node's own decoded record has its `Camera` fields
   present) — **never assuming a well-formed cooker output**, matching
   `ArtifactDecodeError`'s own existing philosophy for the mesh artifact
   exactly. A `Renderable`'s `AssetId` is *not* resolved against a
   known-asset table at decode time — mirroring `resolveMeshAsset()`'s
   own existing split
   ([ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)):
   decode only decodes; **mesh dependency resolution is its own,
   separate, later stage, with its own error domain, not part of
   scene-artifact decoding** ([ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)).

**Human Review Correction (2026-08-23), additive, no new review round
— does not reopen or rewrite Decision items 1–5 above.** Item 4's own
text above states "a default constructor producing an empty (zero-node)
instance is the only other public constructor." This was reviewed
during Plan 0015's own drafting and **rejected**: it was an oversight
between this ADR's own Accepted body and Spec 0015's own Human Review
Approval summary (item 3), which already stated "no public default/
arbitrary construction" — the two were never actually meant to diverge,
and this correction removes the divergence in the direction Human
Review confirms: **no public default construction of any kind.**

Corrected:

- **`ValidatedSceneData` has no public default constructor.** The type
  is `= delete`d (not merely omitted) for its default form — deleted,
  not simply absent, so a caller's attempt to default-construct one
  fails with a compiler error naming the deletion explicitly, per
  `static_assert(!std::is_default_constructible_v<ValidatedSceneData>)`
  ([Plan 0015](../plans/0015-scene-asset-serialization-foundation.md)'s
  own V11).
- **The only creation path, for any instance, empty or not, is a
  successful `decodeScene()` call** (or another function internal to
  `Atlantis::AssetSystem`'s own validation pipeline the `friend`
  relationship names) — never a caller, a test's own hand-assembly, or
  any other public entry point. `private` fields, a `private`
  non-default constructor, and read-only public accessors (all already
  stated in item 4 above) are unchanged and remain the enforcement
  mechanism; this correction only removes the *one* public constructor
  item 4 had left open.
- **Copy and move construct/assign only an already-validated instance
  into another already-validated instance** — both remain defaulted,
  or a caller could never legitimately hold a second copy of a
  `decodeScene()` result, which this Spec's own transactional-load flow
  ([ADR-0054](0054-scene-loading-transactional-instantiation-contract.md))
  genuinely needs. This is unaffected by removing the default
  constructor: copy/move never construct a *new*, independent instance
  from nothing — they only ever duplicate or relocate one that already
  passed `decodeScene()`'s own validation.
- **No empty, default-initialized, or hand-assembled `ValidatedSceneData`
  can ever reach `World`'s own instantiation entry point** — this was
  already true under item 4's own "no arbitrary construction" guarantee
  for a *non-empty* malformed instance; this correction closes the one
  remaining gap (a trivially-empty instance, reachable via the
  now-removed default constructor) the same way.
- **A scene with zero nodes is not "vacuously valid" — it is an
  explicit, named error, rejected at cook time and independently
  re-rejected at decode time**, the same layered-validation discipline
  item 5 above already applies to every other structural condition.
  `SceneCookError`/`SceneArtifactDecodeError` each gain one new
  enumerator for this condition
  ([Plan 0015](../plans/0015-scene-asset-serialization-foundation.md)'s
  own D2). This is the corollary that makes removing the default
  constructor sound: since `decodeScene()` itself now never succeeds
  with zero nodes, there is no remaining scenario — not even a
  legitimately empty scene — where an "empty but valid" instance would
  need constructing at all.

This correction is mechanical: it removes one convenience constructor
and closes the one input condition (an empty scene) that constructor
existed to represent, without changing this ADR's own module boundary,
artifact format, node-identity scheme, or validation algorithm in any
other respect. New verification requirements are recorded in
[Plan 0015](../plans/0015-scene-asset-serialization-foundation.md)'s
own Verification Checklist.

## Consequences

### Positive

- Every byte-level discipline (little-endian, no `memcpy`, versioned
  header, atomic write, metadata sidecar cross-check) is a direct reuse
  of the mesh artifact's own already-`Accepted`, already-shipped
  pattern — no new format philosophy to review, only a new concrete
  layout.
- Explicit author-assigned `node_id`, remapped to dense array indices
  only at cook time, gives authors reorder-without-renumbering ergonomics
  while keeping the runtime artifact and `ValidatedSceneData` as cheap and
  simple as the mesh artifact's own index buffer — no runtime hash
  lookup, no string anywhere in the hot path.
- `ValidatedSceneData` naming no `EntityId`/`WorldIdentity`/pointer anywhere
  makes "never persist Runtime identity" (this Spec's own Non-Goal)
  true by construction, not by convention someone could forget.
- Duplicate-detection, cycle-detection, active-camera-has-`Camera`, and
  non-finite-value rejection are required at **both** cook time and
  load time — an artifact that somehow reached disk in a bad state
  (hand-edited, corrupted, transferred from an incompatible build)
  still fails safely at load, not just at cook.
- `DecodedTransform`/`DecodedCamera`/`DecodedRenderable` being
  `Atlantis::AssetSystem`-owned, `world`-independent types keeps
  `AssetSystem`'s own module boundary exactly as narrow as it already
  is (`Atlantis::Core` only) — no compile-time dependency on
  `Atlantis::World` is introduced anywhere in the decode path.
- Because decode-time validation is exhaustive over every structural
  *and* semantic precondition `World` instantiation needs, **and**
  `ValidatedSceneData` is unforgeable after construction (private
  fields, a private constructor only `decodeScene()` can call, no
  public mutator of any kind), reaching `World::fromValidatedSceneData()`
  with a `ValidatedSceneData` value is a genuine, type-enforced
  guarantee — not a documented convention a careless or hostile caller
  could bypass. This is what lets
  [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  make instantiation infallible instead of inventing a parallel error
  type that would mean the same thing `ArtifactDecodeError`-style
  checks already ruled out.

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

- **`ValidatedSceneData` as a plain, publicly-mutable struct** (public
  node array, public parent/active-camera fields, no private
  constructor). Rejected during self-review — this was the prior
  draft's own design and was corrected because it made "already fully
  validated" a documentation claim, not a fact: any caller holding a
  `ValidatedSceneData` value could mutate a parent index into an
  out-of-range value, introduce a cycle, or point the active-camera
  index at a component-less node, silently invalidating exactly the
  guarantee [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)'s
  own infallible instantiation depends on. Encapsulation (private
  fields, `decodeScene()`-only construction, read-only accessors) is
  chosen instead specifically because it makes that guarantee
  impossible to violate, not merely unlikely.
- **Array position is the only node identity (no explicit `node_id`).**
  Rejected: makes `DuplicateNodeId` — an error case this Spec's own
  Non-Goals-adjacent Requirements explicitly name — structurally
  unreachable, and forces an author to renumber every parent reference
  after reordering nodes in the source file.
- **A general string name as node identity** (e.g. `"camera_entity"`)
  instead of a small integer. Rejected: adds string comparison/hashing
  to cook-time validation and (if ever needed at runtime) to
  `ValidatedSceneData`, for no capability this Spec's own scope needs (no
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
- **Rely on `World::setActiveCamera()`'s own existing
  `Err(WorldError::NoCameraComponent)` runtime check instead of
  validating this at cook/decode time.** Rejected — this was this
  Spec's own original design and was corrected during self-review: it
  would make `World`'s new instantiation entry point return
  `Result<World, WorldError>`, reusing a `WorldError` enumerator whose
  own established meaning is "a caller-supplied `EntityId` handle,
  checked against a live `World`" for a condition that has nothing to
  do with a caller-supplied handle — a scene-authoring mistake wearing
  a different module's error type. Validating it here instead, as one
  more structural/semantic precondition `ValidatedSceneData` already
  guarantees, keeps `WorldError`'s own domain exactly what it already
  is and lets instantiation be infallible.
- **Resolve `Renderable` mesh references to `AssetId` at load time,
  not cook time** (store the logical path in the artifact, resolve on
  every load). Rejected: reintroduces a source-path dependency into the
  runtime artifact this ADR's own Decision explicitly closes, and
  duplicates work (the same path would be normalized and hashed on
  every load instead of once at cook time) for no benefit — nothing
  about a mesh's own logical path can change without re-cooking the
  mesh itself, so resolving early loses no information the artifact
  would ever need later.
