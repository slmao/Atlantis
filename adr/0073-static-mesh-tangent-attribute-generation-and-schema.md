# ADR 0073: Static Mesh Tangent Attribute — Cooker-Generated Schema and Algorithm

- **Status:** Proposed
- **Date:** 2026-09-06
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — pending Human Review
- **Related Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Draft`)
- **Related ADR(s):** [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format/versioning policy — gains a Proposed Amendment in this
  same round, see that ADR's own end), [ADR-0058](0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (the "one, single static mesh vertex layout" closed attribute-count/
  byte-size Decision — genuinely narrowed again by this ADR's own
  fifth attribute, exactly as ADR-0063 itself previously narrowed it
  for the fourth; gains its own Proposed Amendment in this same round,
  see that ADR's own end), [ADR-0063](0063-static-mesh-normal-attribute-schema-version-and-convention.md)
  (the immediately-prior, structurally-similar precedent for adding a
  mandatory attribute to the static mesh vertex layout — its own
  Decision content, being normal-specific, is unaffected by tangent's
  addition and needs no amendment).

## Context

Confirmed directly against current `main`:

- The runtime mesh artifact is schema version 3, 44 bytes/vertex (11
  floats: position xyz, color rgb, UV0 uv, normal xyz — no padding),
  `kMeshArtifactSchemaVersion`/`kMeshArtifactVertexStrideBytes`
  (`mesh_artifact.h:33-46`). No tangent field exists anywhere in this
  format.
- The authoring grammar (`mesh_source.cpp:16,142-153`) parses exactly
  11 space-separated floats per `vertex:` line
  (`atlantis_static_mesh_source_version: 3`) and performs **zero
  geometric derivation** — every field is either copied verbatim
  (position/color/UV0) or validated-but-not-computed (normal's own
  unit-length check, ADR-0063). `cookStaticMesh()` (`cook.cpp:82-104`)
  is a straight parse → encode pipeline.
- The static mesh format uses **shared, indexed vertices** — a vertex
  may be referenced by multiple triangles.
- `atlantis::rhi::VertexAttributeFormat` (`types.h:95-97`) and
  `atlantis::shader_system::VertexAttributeType` each currently
  declare only `Float3`/`Float2` — no 4-component format exists.
- `CookError` (`errors.h:77-83`) and `ArtifactDecodeError`
  (`errors.h:52-67`) have no enumerator for "the cooker could not
  derive a well-defined attribute from the input geometry."

**A direct computational audit of every committed mesh source**, run
against the exact accumulate-then-orthogonalize algorithm this ADR
defines below, found real, disqualifying problems in two of the five:

| Mesh | Triangles | Result |
|---|---|---|
| `ground_plane.mesh.txt` | 2 | Clean — 4 distinct UVs, 0 degenerate triangles, 0 handedness conflicts. |
| `textured_quad_left.mesh.txt` | 2 | Clean — same shape as `ground_plane`. |
| `textured_quad_right.mesh.txt` | 2 | Clean — same shape as `ground_plane`. |
| `pbr_sphere.mesh.txt` | 768 | **96 of 425 vertices (22.6%) have a genuine tangent-handedness conflict** — two or more triangles sharing the same vertex produce opposite-sign handedness. Concentrated at the pole rings (e.g. vertex 0, position `(0,1,0)`, is one of several per-longitude pole copies whose adjacent wedge triangles disagree in handedness sign). UV area itself is never degenerate at these vertices — the conflict is a genuine chirality disagreement, not a zero-area triangle. |
| `minimal_cube.mesh.txt` | 12 | **All 8 vertices, all 12 triangles are UV-degenerate** — every vertex's own UV is the literal same value, `(0.0, 0.0)` (confirmed by direct inspection: `grep` shows one single, repeated UV pair across all 8 lines). Every triangle's own UV-space determinant is exactly zero. No tangent can be derived for any vertex. |

This audit is the reason this ADR's own Decision below makes
**rejecting a handedness conflict, not silently averaging it, a hard
requirement** (see item 5) — a real, currently-authored mesh
(`pbr_sphere`) proves this is not a theoretical edge case.

## Decision

**A fifth attribute, tangent (4 floats: `tx, ty, tz` object-space
direction + `tw` handedness, `±1.0` exactly), is added to the one,
single static mesh vertex layout — 15 floats / 60 bytes per vertex,
replacing the current 44-byte layout. The tangent is deterministically
computed by the Asset Cooker from each mesh's own existing
position/UV0/normal/index data. This is not an implementation of
MikkTSpace — it is a standard, simpler per-triangle UV-Jacobian
accumulation (Lengyel's method) with strict, deterministic rejection
of any vertex whose own accumulated contributions disagree in
handedness, never MikkTSpace's own more elaborate per-vertex
weighting/averaging scheme.**

1. **Authoring grammar unchanged.** `atlantis_static_mesh_source_version`
   stays `3`; `vertex:` lines keep exactly 11 fields — tangent is
   never hand-authored, and this is a deliberate divergence from
   ADR-0058's/ADR-0063's own precedent of bumping the source version
   in lockstep with the artifact schema (justified because no author
   ever types a tangent).
2. **Runtime artifact layout:** per-vertex bytes become position xyz
   (offset 0), color rgb (offset 12), UV0 uv (offset 24), normal xyz
   (offset 32), tangent xyzw (offset 44) — 60 bytes total, no padding.
   `kMeshArtifactVertexStrideBytes` becomes `60`;
   `kMeshArtifactSchemaVersion` becomes `4`; a new
   `kMeshArtifactTangentOffsetBytes = 44` constant is added. An
   artifact declaring schema version 1–3, or any stride other than
   60, is rejected outright — no migration reader.
3. **Fixed epsilons — every one a literal decided here, not an open
   Plan-stage value.** All comparisons operate on `double`, matching
   ADR-0063's own numeric-contract precision discipline:
   - **UV-degeneracy epsilon:** a triangle's own UV-space determinant
     `det = d1.u·d2.v - d2.u·d1.v` is degenerate when `|det| < 1e-12`.
     Chosen four orders of magnitude below `ground_plane`'s/
     `textured_quad_*`'s own real determinants (`±1.0`, unit-square
     UVs) and `pbr_sphere`'s own real determinants (`~4×10⁻³`, from
     its `1/24 × 1/16`-scale UV grid) — comfortably separating every
     currently-authored non-degenerate triangle from true zero, while
     still catching `minimal_cube`'s own exactly-zero case.
   - **Orthogonalization-degeneracy epsilon:** after Gram-Schmidt
     (`T_ortho_raw = T_accumulated - N · dot(N, T_accumulated)`), the
     vertex is degenerate when `|T_ortho_raw| < 1e-6` (the same
     `kDegenerateLengthEpsilon` value `scene_extraction.cpp` already
     uses for a different, but numerically analogous, near-zero-
     length check — reused, not a new independent constant).
   - **Handedness sign:** `sign(dot(cross(N, T_ortho), B_accumulated))`,
     with the input treated as exactly zero (an arbitrary, disclosed
     `+1.0` tie-break) only when the dot product's own magnitude is
     below `1e-9` — a measure-zero input no currently-authored mesh
     exhibits.
4. **Generation algorithm**, inserted in `cookStaticMesh()` between
   `parseMeshSource()` and `encodeMeshArtifact()`:
   - For each triangle `(v0, v1, v2)`: edge vectors `e1 = pos(v1) -
     pos(v0)`, `e2 = pos(v2) - pos(v0)`; UV deltas `d1 = uv(v1) -
     uv(v0)`, `d2 = uv(v2) - uv(v0)`; determinant `det` (item 3). If
     `|det|` is below the UV-degeneracy epsilon, this triangle
     contributes nothing to any of its own three vertices.
   - Otherwise: face tangent `T_face = (e1·d2.v - e2·d1.v) / det`,
     face bitangent `B_face = (e2·d1.u - e1·d2.u) / det` (both
     unnormalized), **and this face's own handedness**,
     `h_face = sign(dot(cross(vertexNormal, T_face), B_face))`,
     computed **per triangle-vertex pair** (using that vertex's own
     already-validated unit normal) and recorded alongside the
     accumulator — not deferred to after accumulation. Both `T_face`
     and `B_face` are added into each of `v0`/`v1`/`v2`'s own running
     sum.
   - **Handedness-conflict check (new, mandatory, whole-mesh
     failure):** for each vertex, if any two of its own recorded
     `h_face` values disagree in sign, the whole mesh's own cook fails
     with `CookError::TangentHandednessConflict` (item 6) — the
     accumulated `T`/`B` sums for that vertex are never computed
     into a final tangent, and no partial artifact is written for any
     vertex. This is a hard, deterministic rejection, never a
     silent average of the two conflicting contributions — confirmed
     necessary by this ADR's own audit (`pbr_sphere`, above).
   - Only if every vertex referenced by at least one non-degenerate
     triangle has zero handedness conflict: for each such vertex,
     Gram-Schmidt orthogonalize the accumulated `T` against its own
     normal (item 3's second epsilon), normalize, and emit `tw` as
     that vertex's own single, agreed-upon `h_face` value (not
     re-derived from the orthogonalized `T`/accumulated `B` a second
     time — it is already known and already confirmed unanimous).
5. **Whole-mesh failure, two independent causes** —
   `CookError::DegenerateTangentBasis` (a vertex left with zero
   non-degenerate triangle contribution: every adjacent triangle was
   UV-degenerate, or the vertex is referenced by zero triangles, or
   its accumulated tangent fails the orthogonalization epsilon) and
   `CookError::TangentHandednessConflict` (item 4's own conflict
   check) — kept as two distinct enumerators since they represent two
   different authoring problems an implementer would fix differently
   (re-author UVs vs. split a seam), matching this codebase's own
   "distinct enumerators for distinct causes" discipline
   (`errors.h`'s own header comment). Confirmed, by direct inspection
   of every existing `CookError` enumerator, that neither is expressed
   by any of them today. No partial artifact is ever written for
   either failure — the whole mesh's own cook fails, matching this
   format's existing one-pass/fail discipline.
6. **Decode-time re-validation**, matching ADR-0063's own
   "never trust a well-formed cooker" discipline: `ArtifactDecodeError`
   gains `NonUnitTangent` (tangent xyz's own length-squared, same
   `[0.9801, 1.0201]` double-precision method as `NonUnitNormal`),
   `NonOrthogonalTangent` (tangent must be orthogonal to normal within
   the same relative-tolerance shape `checkConformalTransform()`
   already uses for a structurally similar check — `|dot(N,T)|` below
   a fixed, small absolute tolerance, `1e-3`, chosen one order of
   magnitude looser than the cook-time orthogonalization epsilon to
   tolerate the float32 round-trip through the artifact's own binary
   encoding), and `InvalidTangentHandedness` (the `tw` component must
   decode to exactly `1.0` or `-1.0` bit-for-bit — a discrete flag,
   an equality check, never a tolerance-based one). Three distinct
   enumerators, matching three distinct decode-time failure shapes.
7. **`kMeshArtifactVertexStrideBytes`/`kMeshArtifactTangentOffsetBytes`
   become real, named, public `constexpr std::size_t` constants**,
   matching ADR-0063 item 8's own precedent — every composition root
   gains a `static_assert(offsetof(Vertex, tangent) ==
   kMeshArtifactTangentOffsetBytes)`.
8. **RHI/reflection addition:** `atlantis::rhi::VertexAttributeFormat`
   and `atlantis::shader_system::VertexAttributeType` each gain
   exactly one new value, `Float4` — the same class of additive
   change ADR-0058's own `Float2` addition already established.
9. **Existing-mesh migration is a real, disclosed prerequisite of this
   ADR's own Implementation, not a side effect discovered later:**
   - `minimal_cube.mesh.txt` must be re-authored with a real,
     non-degenerate UV mapping (e.g. a standard cube-face unwrap, one
     UV island per face) before this ADR's own cooker change lands —
     its current, uniform `(0,0)` UV fails item 5's own
     `DegenerateTangentBasis` check unconditionally. Its own smooth,
     vertex-averaged normals (ADR-0063 item 6) and its own 8-shared-
     vertex/12-triangle/36-index topology are otherwise unaffected —
     only the UV field per vertex changes.
   - `pbr_sphere.mesh.txt` must be re-authored to eliminate the 96
     pole-ring handedness conflicts before this ADR's own cooker
     change lands — standard practice for a UV-sphere: duplicate each
     conflicting pole vertex further, one copy per triangle wedge that
     would otherwise disagree, so each copy's own single adjacent
     triangle set agrees unanimously (this is an extension of the
     mesh's own already-established "poles duplicated per-longitude-
     segment" pattern, one level further — not a new topology
     concept). This is bounded, deterministic mesh-authoring work, not
     a new architectural mechanism; the single, fixed 60-byte vertex
     layout is unchanged, only `pbr_sphere`'s own vertex/index *count*
     grows. Re-authoring is verified complete by re-running this same
     audit algorithm (item 4) against the candidate mesh and confirming
     zero conflicts remain — a concrete, mechanical Implementation-time
     acceptance gate, not a subjective judgment call.
   - `ground_plane.mesh.txt`/`textured_quad_left.mesh.txt`/
     `textured_quad_right.mesh.txt` need no UV/topology change — the
     audit already confirms them clean.
   - **Every mesh's own artifact bytes change** once re-cooked under
     schema 4 (stride 44→60) — a build-output-only effect (`.amesh`
     files are never tracked in git). Decoded position/color/UV0/
     normal values for `ground_plane`/`textured_quad_*` are byte-for-
     byte identical to today; `minimal_cube`'s own decoded UV values
     change (a real, disclosed content change, not merely a format
     change — see Spec 0029's own compatibility statement for which
     rendered outputs this can and cannot affect); `pbr_sphere`'s own
     decoded position/normal values for its *existing* (non-pole)
     vertices are unchanged, while its pole-region vertex/index data
     changes (more vertices, re-triangulated poles).

## Consequences

### Positive

- Reuses every existing mechanism (explicit little-endian byte
  serialization, `static_assert`-checked offset constants, the
  `[Cook/ArtifactDecode]Error`-pair validation discipline).
- The mandatory handedness-conflict rejection, confirmed necessary by
  a real audit finding (not a hypothetical), guarantees no mesh with
  an unresolved tangent-space seam ever silently ships a wrong,
  averaged tangent.
- `ground_plane`/`textured_quad_left`/`textured_quad_right` need zero
  re-authoring — the audit proves this, not merely a design intention.

### Negative / Trade-offs

- Every static mesh's own per-vertex byte cost grows from 44 to 60
  bytes (+36%), including meshes whose own shader never reads the
  tangent region.
- Two of the five currently-committed meshes (`minimal_cube`,
  `pbr_sphere`) require real, disclosed re-authoring before this ADR's
  own Implementation can land — a genuine, non-trivial prerequisite,
  not a rounding error. `minimal_cube`'s own new UV mapping is a real,
  visible content change to that mesh's own future non-degenerate
  behavior once any shader actually samples it (today, no shader
  samples `minimal_cube`'s UV — Spec 0029's own compatibility
  statement covers exactly what this can and cannot affect).
- This ADR requires a Proposed Amendment to **both** ADR-0045's own
  format-scope sentence and ADR-0058's own "one, single vertex layout"
  closed attribute-count/byte-size Decision — filed alongside this ADR
  (see each ADR's own end), matching exactly the same two-amendment
  pattern ADR-0063 itself required for the normal attribute; both
  pending the same Human Review pass. ADR-0063's own Decision content
  is unaffected and needs no amendment.
- Handedness's own invariance under an object-to-world transform is
  only guaranteed for a positive-determinant transform (a negative-
  determinant conformal transform passes `checkConformalTransform()`
  today but flips true chirality). Disclosed, not corrected — Spec
  0029's own Open Questions.

## Alternatives Considered

- **Hand-authored tangent in the source grammar.** Rejected: a
  hand-typed tangent is exactly the failure mode this generation
  approach exists to avoid.
- **Runtime or shader-side tangent generation** (screen-space
  derivatives). Rejected per Spec 0029's own explicit Non-Goal —
  view-dependent, non-deterministic, unsuitable for a byte-stable
  artifact.
- **Silently averaging conflicting handedness at a shared vertex**
  (the naive accumulate-without-checking approach). Rejected outright
  by this ADR's own audit finding: `pbr_sphere` proves this would
  silently corrupt 22.6% of one real, currently-shipped mesh's own
  tangent data with no error, no warning, and a visually wrong result
  at the poles.
- **A determinant-sign correction for mirrored transforms.** Rejected
  as scope beyond this ADR's own minimal target — no current scene
  authors a negative-determinant conformal transform; deferred as a
  disclosed limitation.
- **Leaving `minimal_cube`/`pbr_sphere` unmigrated and excluding them
  from any normal-mapped consumer.** Rejected: this ADR fixes one,
  single, global mesh schema — there is no per-mesh opt-out
  mechanism, and introducing one (an optional vertex layout) is
  explicitly out of scope per this Spec's own instruction to keep a
  single, fixed vertex layout. Re-authoring the two affected meshes is
  the only option that preserves that single-layout invariant.
