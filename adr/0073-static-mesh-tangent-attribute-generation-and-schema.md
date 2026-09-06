# ADR 0073: Static Mesh Tangent Attribute — Cooker-Generated Schema and Algorithm

- **Status:** Proposed
- **Date:** 2026-09-06
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — pending Human Review
- **Related Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Draft`)
- **Related ADR(s):** [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format/versioning policy) and [ADR-0063](0063-static-mesh-normal-attribute-schema-version-and-convention.md)
  (the immediately-prior, structurally-similar precedent for adding a
  mandatory attribute to the static mesh vertex layout) — **ADR-0045's
  own format-scope sentence will need a Proposed Amendment before this
  ADR may reach `Accepted`; not filed in this Draft round** (Spec 0029
  is Spec-stage only).

## Context

Confirmed directly against current `main`:

- The runtime mesh artifact is schema version 3, 44 bytes/vertex (11
  floats: position xyz, color rgb, UV0 uv, normal xyz — no padding),
  `kMeshArtifactSchemaVersion`/`kMeshArtifactVertexStrideBytes` in
  `src/asset_system/include/atlantis/asset_system/mesh_artifact.h:33-46`.
  No tangent field exists anywhere in this format.
- The authoring grammar (`mesh_source.cpp:16,142-153`) parses exactly
  11 space-separated floats per `vertex:` line
  (`atlantis_static_mesh_source_version: 3`) and performs **zero
  geometric derivation** — every field is either copied verbatim
  (position/color/UV0) or validated-but-not-computed (normal's own
  unit-length check, `ADR-0063`). `cookStaticMesh()`
  (`cook.cpp:82-104`) is a straight parse → encode pipeline, with no
  step that derives one attribute from others.
- Unlike normal (ADR-0063, hand-authored, cooker validates but never
  generates), a tangent basis is not something an author can
  practically hand-type per vertex — it must be derived from the
  triangle's own position+UV+normal data. This is a **new class of
  cook-time operation** this codebase's Asset Cooker has never
  performed before.
- The static mesh format uses **shared, indexed vertices**
  (`vertex_count`/`index_count` are independent, `mesh_source.cpp:186-202`)
  — a vertex may be referenced by multiple triangles (confirmed by
  `minimal_cube.mesh.txt`'s own 8 shared vertices / 12 triangles, cited
  in ADR-0063 item 6). A per-vertex tangent must therefore be an
  accumulation across every triangle that references it, not a
  per-triangle-only value.
- `atlantis::rhi::VertexAttributeFormat`
  (`src/rhi/include/atlantis/rhi/types.h:95-97`) and
  `atlantis::shader_system::VertexAttributeType`
  (consumed by `vertex_input_mapping.cpp:10-18`) each currently declare
  only `Float3`/`Float2` — no 4-component format exists for a
  tangent+handedness vertex attribute.
- `errors.h`'s own `CookError` (mesh cook orchestration,
  `errors.h:77-83`) and `ArtifactDecodeError` (mesh artifact decode,
  `errors.h:52-67`) enums have no enumerator representing "the cooker
  could not derive a well-defined attribute from the input geometry" —
  every existing enumerator in both is about malformed/out-of-range
  *authored* input, never a computed-and-then-found-degenerate one.

## Decision

**A fifth attribute, tangent (4 floats: `tx, ty, tz` object-space
direction + `tw` handedness, `±1.0` exactly), is added to the one,
single static mesh vertex layout — 15 floats / 60 bytes per vertex,
replacing the current 44-byte layout. The tangent is deterministically
computed by the Asset Cooker from each mesh's own existing
position/UV0/normal/index data — it is never hand-authored in the
source grammar, and never computed by Runtime or by any shader.**

1. **Authoring grammar unchanged.** `atlantis_static_mesh_source_version`
   stays `3`; `vertex:` lines keep exactly 11 fields. This is a
   deliberate, disclosed divergence from ADR-0058's/ADR-0063's own
   precedent of bumping the source version in lockstep with the
   artifact schema — justified because, unlike position/color/UV0/
   normal, tangent is never part of what an author types; widening the
   authoring grammar for a field no author ever supplies would only
   invite a hand-typed, uncheckable, silently-ignored value.
2. **Runtime artifact layout:** per-vertex bytes become position xyz
   (offset 0), color rgb (offset 12), UV0 uv (offset 24), normal xyz
   (offset 32), tangent xyzw (offset 44) — 60 bytes total, no padding,
   no gap. `kMeshArtifactVertexStrideBytes` becomes `60`;
   `kMeshArtifactSchemaVersion` becomes `4`; a new
   `kMeshArtifactTangentOffsetBytes = 44` constant is added alongside
   the existing four offset constants (ADR-0063 item 8's own
   precedent). An artifact declaring schema version 1–3, or any stride
   other than 60, is rejected outright (`ArtifactDecodeError::UnknownSchemaVersion`/
   `UnsupportedVertexStride`) — no migration reader, matching every
   prior schema bump in this codebase.
3. **Generation algorithm** (`cookStaticMesh()`, inserted between
   `parseMeshSource()` and `encodeMeshArtifact()`), standard
   per-triangle UV-Jacobian accumulation (Lengyel's method), computed
   in `double` throughout to match ADR-0063's own numeric-contract
   precision discipline:
   - For each triangle `(v0, v1, v2)`: edge vectors `e1 = pos(v1) -
     pos(v0)`, `e2 = pos(v2) - pos(v0)`; UV deltas `d1 = uv(v1) -
     uv(v0)`, `d2 = uv(v2) - uv(v0)`; UV-space determinant
     `det = d1.u·d2.v - d2.u·d1.v`. If `|det|` is below a fixed epsilon
     (degenerate/zero-area UV mapping for this triangle), this
     triangle contributes nothing to any of its own three vertices'
     accumulators (see item 5 for when this becomes a hard cook
     failure).
   - Otherwise, face tangent `T_face = (e1·d2.v - e2·d1.v) / det`,
     face bitangent `B_face = (e2·d1.u - e1·d2.u) / det` (both
     unnormalized). Both are **added** (accumulated, not overwritten)
     into a running per-vertex sum for each of `v0`, `v1`, `v2` — the
     mechanism that correctly handles a vertex shared by multiple
     triangles.
   - After every triangle is processed, for each vertex: Gram-Schmidt
     orthogonalize the accumulated `T` against that vertex's own
     already-validated unit normal `N` —
     `T_ortho = normalize(T - N · dot(N, T))`. If the pre-normalization
     length of `T - N·dot(N,T)` is below a fixed epsilon (the
     accumulated tangent is degenerate or anti-parallel to the normal
     — see item 5), this vertex cannot receive a well-defined tangent.
   - Handedness `tw = sign(dot(cross(N, T_ortho), B_accumulated))`,
     resolving to exactly `+1.0` or `-1.0` — never `0.0` (the sign of a
     dot product that happens to be exactly zero is resolved as `+1.0`,
     a disclosed, arbitrary tie-break for a measure-zero input, not a
     new failure kind).
4. **Deterministic, offline, cooker-only.** No Runtime code and no
   `.slang` shader ever computes a tangent — Runtime only reads the
   already-cooked `tangent.xyzw` bytes (Spec 0029's own explicit
   Non-Goal). Re-cooking the same source produces byte-identical
   tangent output every time (pure function of position/UV0/normal/
   index, no randomness, no floating-point-order-dependent parallel
   reduction).
5. **New failure kind — `CookError::DegenerateTangentBasis`.**
   Confirmed, by direct inspection of every existing `CookError`
   enumerator, to be the one genuinely new failure class none of them
   expresses ("the cooker could not derive a well-defined tangent for
   at least one vertex," distinct from every existing "malformed/
   out-of-range authored input" enumerator). Triggers on: (a) every
   triangle referencing a given vertex has degenerate UV-space area
   (item 3's `|det|` epsilon), leaving that vertex with a zero
   accumulator; (b) a vertex's own accumulated tangent survives item
   3(a) but fails the orthogonalization epsilon in item 3 (accumulated
   tangent numerically parallel to the vertex's own normal); (c) a
   vertex referenced by zero triangles (an orphan vertex — already
   possible in the existing format, since `vertex_count` and
   `index_count` are independent, but never previously rejected; this
   ADR makes it a hard cook failure, since such a vertex cannot receive
   any tangent contribution at all). The whole mesh's own cook fails
   — no per-vertex partial output, matching this format's own existing
   "one mesh, one pass/fail cook" discipline (no artifact is ever
   written from a partially-valid source).
6. **Decode-time re-validation**, matching ADR-0063's own
   "never trust a well-formed cooker" discipline: `ArtifactDecodeError`
   gains `NonUnitTangent` (the tangent xyz's own length-squared,
   computed identically to `NonUnitNormal`'s own double-precision
   method, must fall within the same `[0.9801, 1.0201]` tolerance) and
   `InvalidTangentHandedness` (the `tw` component must decode to
   exactly `1.0` or `-1.0` bit-for-bit — a discrete flag, not a
   magnitude, so this is an equality check, never a tolerance-based
   one, and is kept as its own enumerator rather than folded into
   `NonUnitTangent` since the two represent genuinely different
   failure shapes).
7. **`kMeshArtifactVertexStrideBytes`/`kMeshArtifactTangentOffsetBytes`
   become real, named, public `constexpr std::size_t` constants**,
   exactly matching ADR-0063 item 8's own precedent — every
   composition root gains a `static_assert(offsetof(Vertex, tangent)
   == kMeshArtifactTangentOffsetBytes)`.
8. **No new RHI/RenderGraph/Renderer public API beyond one new
   enumerator.** `atlantis::rhi::VertexAttributeFormat` and
   `atlantis::shader_system::VertexAttributeType` each gain exactly one
   new value, `Float4` — the same class of additive change ADR-0058's
   own `Float2` addition already established (the header comment at
   `types.h:94` already anticipates this: "extends this enum"). No new
   vertex-attribute mechanism, no per-mesh optional layout.
9. **Existing meshes without a normal-mapped consumer are unaffected
   in content, but every mesh is re-cooked** under the new schema
   (every `.amesh` artifact's own bytes change size/layout) — this is
   a build-output-only effect (artifacts are never tracked in git);
   no existing committed golden PNG is affected, since decoded
   position/color/UV0/normal values are unchanged and every existing
   shader's own vertex input layout is updated to the new 60-byte
   stride while keeping the same location/offset mapping for the
   attributes it already reads (Spec 0029's own migration scope).

## Consequences

### Positive

- Reuses every existing mechanism (the explicit little-endian
  byte-serialization routine, the `static_assert`-checked offset-
  constant pattern, the `[Cook/ArtifactDecode]Error`-pair validation
  discipline) — zero new serialization primitive.
- Deterministic, cooker-only generation means no shader or Runtime
  code ever needs to reconstruct tangents at load time or draw time —
  Spec 0029's own explicit Non-Goal (no runtime tangent generation)
  is structurally guaranteed, not merely a convention.
- The accumulate-then-orthogonalize algorithm correctly handles shared
  vertices (the format's own existing indexed-triangle structure)
  without any special-casing.

### Negative / Trade-offs

- Every static mesh's own per-vertex byte cost grows from 44 to 60
  bytes (+36%), including meshes whose own shader never reads the
  tangent region — identical in kind to ADR-0063's own already-
  accepted normal-attribute cost.
- A real, new class of cook-time failure (`DegenerateTangentBasis`)
  that did not exist before this ADR — a mesh with degenerate UV
  mapping (zero-area UV triangles, an unreferenced vertex) that
  cooked successfully under schema 3 will now fail to cook at all
  under schema 4. No currently-committed mesh source is known to
  exhibit this (Implementation must confirm against every real mesh
  source before landing).
- This ADR requires a Proposed Amendment to ADR-0045's own format-
  scope sentence before it may itself reach `Accepted` — not filed in
  this Draft round (Spec-stage only), matching ADR-0063's own
  "amendment filed alongside, both accepted together" precedent for
  when Implementation is actually authorized.
- Handedness's own invariance under an object-to-world transform is
  only guaranteed for a transform with positive determinant (a
  uniform-scale-times-rotation of *either* sign passes the existing
  `checkConformalTransform()` gate, but a negative-determinant
  conformal transform — e.g. a single-axis mirror combined with a
  uniform scale — flips true chirality without failing that check,
  since it only tests column length/orthogonality, never sign). This
  ADR does not add a determinant-sign check or a corrective push-
  constant — a mirrored, tangent-consuming entity's own normal-mapped
  shading is a disclosed, accepted limitation (Spec 0029's own Open
  Questions), not silently handled.

## Alternatives Considered

- **Hand-authored tangent in the source grammar** (a 4th field group on
  each `vertex:` line, mirroring how normal itself was added).
  Rejected: a hand-typed tangent is exactly the failure mode MikkTSpace-
  style generation exists to avoid (inconsistent, non-orthogonal,
  wrong-handedness values an author has no practical way to compute or
  verify by hand) — the explicit reason the user's own recommended
  direction requires cooker generation instead.
- **Runtime or shader-side tangent generation** (screen-space
  derivatives, `ddx`/`ddy`, or a Runtime-side compute pass). Rejected
  per Spec 0029's own explicit Non-Goal — screen-space-derivative
  tangents are view-dependent and non-deterministic across frames/
  viewing angles, unsuitable for a byte-stable artifact and for this
  codebase's own "deterministic, offline cook" discipline.
- **A per-face (non-shared, non-accumulated) tangent, duplicating
  vertices at UV/tangent seams** (the "hard-tangent" analogue of a
  hard-face-shaded mesh). Rejected as unnecessary scope for this ADR's
  own minimal target meshes (`pbr_sphere`, `ground_plane`) — no
  current mesh has a UV seam whose smoothing this would meaningfully
  change; deferred as a future, separate, disclosed decision if a
  future mesh actually needs it (mirroring ADR-0063's own identical
  deferral for `minimal_cube`'s hard-face normals).
- **A determinant-sign correction for mirrored transforms** (computing
  and passing an extra per-draw sign flag). Rejected as scope beyond
  this ADR's own minimal target — no current scene authors a
  negative-determinant conformal transform; deferred as a disclosed
  limitation (see Consequences) rather than solved speculatively.
