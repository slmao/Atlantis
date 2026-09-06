# ADR 0073: Static Mesh Tangent Attribute — Cooker-Generated Schema and Algorithm

- **Status:** Accepted
- **Date:** 2026-09-06
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review, approved 2026-09-06
- **Related Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Approved`)
- **Acceptance Record (2026-09-06):** Accepted by Human Review as part
  of Spec 0029's own Human Review Approval against
  [PR #129](https://github.com/slmao/Atlantis/pull/129). Does not
  change this ADR's own Decision, Consequences, or Alternatives
  Considered below. Authorizes drafting Plan 0029 only, once PR #129
  merges to `main` — not any Implementation, asset migration, or
  golden capture.
- **Related ADR(s):** [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format/versioning policy — gained an Accepted Amendment in the
  same Human Review pass, see that ADR's own end), [ADR-0058](0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (the "one, single static mesh vertex layout" closed attribute-count/
  byte-size Decision — genuinely narrowed again by this ADR's own
  fifth attribute, exactly as ADR-0063 itself previously narrowed it
  for the fourth; gained its own Accepted Amendment in the same pass,
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

**A direct, executed computational audit of every committed mesh
source** (re-run to produce the exact figures below, not recalled),
against the exact accumulate-then-orthogonalize algorithm this ADR
defines below, found real problems in two of the five:

| Mesh | Triangles | UV-degenerate triangles | Vertices with handedness conflict | Result |
|---|---|---|---|---|
| `ground_plane.mesh.txt` | 2 | 0/2 | 0/4 | Clean. |
| `textured_quad_left.mesh.txt` | 2 | 0/2 | 0/4 | Clean. |
| `textured_quad_right.mesh.txt` | 2 | 0/2 | 0/4 | Clean. |
| `pbr_sphere.mesh.txt` | 768 | 0/768 | **96/425 (22.6%)** | **Genuine tangent-handedness conflict** — two or more triangles sharing the same vertex produce opposite-sign handedness, concentrated at the pole rings (e.g. vertex 0, position `(0,1,0)`, one of several per-longitude pole copies whose adjacent wedge triangles disagree in sign). UV area is never degenerate at these vertices — this is a genuine chirality disagreement, not a zero-area triangle. Requires real mesh migration (item 9). |
| `minimal_cube.mesh.txt` | 12 | **12/12 (100%)** | 0/8 | Every vertex's own UV is the literal same value, `(0.0, 0.0)` — every triangle's own UV-space determinant is exactly zero, so **zero** triangles contribute a valid tangent to any vertex. Handled entirely by this ADR's own fallback-tangent path (item 4a) — no cook failure, no mesh migration (item 9). |

**Correction to this ADR's own earlier drafting:** `minimal_cube`'s UV
is not inert data. `assets/scenes/lighting_demo.scene.txt` places
`minimal_cube.mesh.txt` under `materials/lit_textured_quad.material.txt`
(`kind: lit_textured`), and `lit_textured.slang` genuinely samples
`input.uv` (`texturedSampler.Sample(input.uv)`, confirmed by direct
read) to produce the `lighting_demo` golden's own real pixels. Any
re-authoring of `minimal_cube`'s UV values would therefore change that
golden's rendered output — confirmed by tracing the real consumer
chain, not assumed. This is the reason item 4a's fallback path (below)
leaves `minimal_cube`'s UV, topology, and every other byte of its
authored content completely untouched, rather than re-unwrapping it.

This audit is the reason this ADR's own Decision below makes
**rejecting a handedness conflict, not silently averaging it, a hard
requirement** (see item 5) — a real, currently-authored mesh
(`pbr_sphere`) proves this is not a theoretical edge case. The same
audit is also why `minimal_cube` needs a cook-time fallback rather
than re-authoring: it has zero handedness conflicts (there is nothing
to disagree about when zero triangles contribute at all), so a
deterministic, per-vertex fallback derived solely from its own already-
validated normal is well-defined and requires no source change.

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
     with `CookError::TangentHandednessConflict` (item 5) — the
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

4a. **Deterministic fallback tangent for a vertex with zero
    non-degenerate triangle contribution — a real, disclosed part of
    the algorithm, not an error path.** A vertex reaches this rule
    when every triangle referencing it was UV-degenerate (`minimal_cube`,
    all 8 vertices), or when it is referenced by zero triangles (an
    orphan vertex, no currently-committed mesh has one). Its own
    already-validated unit normal `N` (ADR-0063) is the only input:

    - Compute `|dot(N, X)|`, `|dot(N, Y)|`, `|dot(N, Z)|` against the
      three fixed Cartesian basis vectors `X=(1,0,0)`, `Y=(0,1,0)`,
      `Z=(0,0,1)`. Pick the axis with the **smallest** absolute dot
      product (the one least parallel to `N`) — a fixed, deterministic
      tie-break of `X` before `Y` before `Z` applies on an exact tie
      (no currently-committed mesh's normal produces one; disclosed for
      completeness, not left undefined).
    - `T_raw = axis - N * dot(N, axis)`; normalize to unit length —
      this can never be degenerate (the axis is chosen specifically to
      not be parallel to `N`, so `|T_raw|` is bounded well away from
      zero for every possible unit `N`; no epsilon check is needed
      here, unlike item 3's other two cases).
    - Handedness is fixed: `tw = +1.0`. There is no second contributor
      to disagree with, so no handedness-conflict check applies to a
      fallback vertex.
    - This fallback is a mathematically valid, decode-time-conformant
      tangent (unit length, orthogonal to `N`, `tw = ±1.0` exactly) —
      it satisfies every check in item 6 below. It is explicitly
      **not** claimed to carry any UV-derived directional meaning: a
      vertex with no non-degenerate UV contribution has, by
      construction, no UV data expressing a spatial "along the surface"
      direction, so this fallback exists solely to produce a fixed,
      well-defined 60-byte artifact and keep the existing, non-normal-
      map rendering path byte-behavior-unchanged — never to claim
      standard, UV-derived tangent quality for that vertex. A future
      normal-mapped consumer of `minimal_cube` (none exists today) would
      see a flat, arbitrarily-but-deterministically-oriented tangent
      basis at every one of its 8 vertices, disclosed here, not hidden.
5. **Whole-mesh failure, one remaining cause after item 4a** —
   `CookError::DegenerateTangentBasis` now fires only when a vertex
   *has* at least one non-degenerate contributing triangle (so item 4a's
   fallback does not apply) yet its accumulated tangent still fails the
   orthogonalization epsilon (item 3) — a real safety net for a
   pathological geometric case, not observed in any of the 5 currently-
   committed meshes. `CookError::TangentHandednessConflict` (item 4's
   own conflict check) is unchanged — `pbr_sphere`'s own 96/425
   conflicting vertices are its real, confirmed trigger. Kept as two
   distinct enumerators since they represent two different authoring
   problems an implementer would fix differently (a numerically
   degenerate UV parameterization vs. splitting a seam), matching this
   codebase's own "distinct enumerators for distinct causes" discipline
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
9. **Existing-mesh migration, real but bounded to exactly one mesh —
   item 4a's fallback resolves `minimal_cube` with zero source change:**
   - `minimal_cube.mesh.txt` needs **no** UV, topology, or any other
     source change. Every one of its 8 vertices has zero non-degenerate
     UV contribution, so item 4a's fallback path applies uniformly and
     deterministically — no `CookError` of any kind, no source edit, no
     migration. `lighting_demo`'s own real, golden-backed consumption of
     this mesh's UV data (via `lit_textured.slang`, see Context above)
     is completely undisturbed, since neither its UV bytes nor any other
     authored field changes.
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
     acceptance gate, not a subjective judgment call. This is the
     **only** mesh requiring any source edit.
   - `ground_plane.mesh.txt`/`textured_quad_left.mesh.txt`/
     `textured_quad_right.mesh.txt` need no UV/topology change — the
     audit already confirms them clean.
   - **Every mesh's own artifact bytes change** once re-cooked under
     schema 4 (stride 44→60) — a build-output-only effect (`.amesh`
     files are never tracked in git). Decoded position/color/UV0/
     normal values for `ground_plane`/`textured_quad_*`/`minimal_cube`
     are byte-for-byte identical to today (`minimal_cube`'s own UV is
     untouched by item 4a's fallback, which reads only its normal);
     `pbr_sphere`'s own decoded position/normal values for its
     *existing* (non-pole) vertices are unchanged, while its
     pole-region vertex/index data changes (more vertices,
     re-triangulated poles). Every existing image-regression golden
     therefore stays byte-identical (Spec 0029's own compatibility
     statement and Testing & Verification Plan).

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
- `minimal_cube` needs zero re-authoring either, once item 4a's
  fallback path is in place — a real fact this ADR's own earlier
  drafting got wrong (it originally claimed no shader reads
  `minimal_cube`'s UV, and proposed re-unwrapping it; both were
  incorrect — `lighting_demo` genuinely samples it, so a UV change
  would have been a real, undisclosed golden-affecting regression).

### Negative / Trade-offs

- Every static mesh's own per-vertex byte cost grows from 44 to 60
  bytes (+36%), including meshes whose own shader never reads the
  tangent region.
- One of the five currently-committed meshes (`pbr_sphere`) requires
  real, disclosed re-authoring before this ADR's own Implementation can
  land — a genuine, non-trivial prerequisite, not a rounding error.
- `minimal_cube`'s own 8 fallback-generated tangents (item 4a) carry no
  UV-derived directional meaning, disclosed explicitly — any future
  normal-mapped consumer of this mesh (none exists today) would see a
  flat, arbitrary-but-deterministic tangent basis, not a standard,
  surface-aligned one. This is judged an acceptable, disclosed
  limitation of a mesh whose own real UV data is a uniform, non-varying
  value in the first place, not a regression from any better tangent
  quality a UV re-unwrap could realistically have provided without
  itself changing `lighting_demo`'s current rendered output.
- This ADR required an Accepted Amendment to **both** ADR-0045's own
  format-scope sentence and ADR-0058's own "one, single vertex layout"
  closed attribute-count/byte-size Decision — filed alongside this ADR
  (see each ADR's own end), matching exactly the same two-amendment
  pattern ADR-0063 itself required for the normal attribute; both
  accepted in the same Human Review pass. ADR-0063's own Decision
  content is unaffected and needs no amendment.
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
- **Re-authoring `minimal_cube`'s UV instead of a cook-time fallback.**
  Rejected once the real consumer chain was traced: `lighting_demo`
  genuinely samples `minimal_cube`'s UV through `lit_textured.slang`,
  so any UV re-unwrap would be a real, visible change to that golden's
  own rendered pixels — a regression this Spec's own compatibility
  goal explicitly forbids. A cook-time fallback derived from the
  vertex's own already-validated normal (item 4a) produces a valid
  60-byte tangent with zero source change and zero golden risk.
- **Leaving `pbr_sphere` unmigrated and excluding it from any
  normal-mapped consumer.** Rejected: this ADR fixes one, single,
  global mesh schema — there is no per-mesh opt-out mechanism, and
  introducing one (an optional vertex layout) is explicitly out of
  scope per this Spec's own instruction to keep a single, fixed vertex
  layout. Unlike `minimal_cube`, `pbr_sphere`'s own problem is a
  genuine handedness *conflict* between two valid, UV-contributing
  triangles — item 4a's fallback only applies to a vertex with zero
  contribution, so it cannot resolve this case; re-authoring (pole
  vertex splitting) is the only option that preserves the single-layout
  invariant and produces an unambiguous tangent.

## Accepted Correction — 2026-09-06 (`pbr_sphere` handedness-conflict count and location)

**Status:** Accepted. Approved by Human Review, 2026-09-06, against
[PR #130](https://github.com/slmao/Atlantis/pull/130), named
individually (not by a blanket approval of Plan 0029 alone) alongside
the matching correction to
[Spec 0029](../specs/0029-tangent-space-normal-mapping-foundation.md#human-review-correction--2026-09-06-pbr_sphere-handedness-conflict-count)
and [Plan 0029](../plans/0029-tangent-space-normal-mapping-foundation.md)
itself, in the same review pass. Does not rewrite the Decision,
Consequences, or Alternatives sections above; supersedes only the
specific `96/425 (22.6%)` figure and the "concentrated at the pole
rings" framing appearing in the Context table and in Consequences/
Alternatives, which did not hold under re-audit. This ADR's own
top-level `Status: Accepted` (unchanged since this ADR's own original
acceptance) is unaffected by this correction.

A temporary, uncommitted probe, run strictly to this ADR's own already-
Accepted algorithm (`h_face = sign(dot(cross(vertexNormal, T_face),
B_face))`, raw `T_face`/`B_face`, no orthogonalization before the sign
check — Decision item 4's own exact formula, re-read and matched
verbatim, not an approximation), found the real conflict count is
**48 of 425 vertices (11.3%), not 96 (22.6%)**. The earlier figure came
from a draft-stage probe that orthogonalized `T_face` against the
vertex normal *before* taking its sign — a different computation from
the one this ADR's own Decision text actually specifies.

**Precise location, by latitude ring** (`pbr_sphere.mesh.txt` has 17
rings of 25 vertices each, `y` from `-1` to `1`; vertex indices 0-24
are the north-pole ring, 25-49 the adjacent ring, confirmed by direct
inspection):

- Ring `y = 1.0` (north pole): 24 of 25 vertices conflict (indices
  0-23; index 24, the seam-closure duplicate, does not).
- Ring `y = 0.980785` (the ring immediately adjacent to the north
  pole — **not** a pole ring itself): 24 of 25 vertices conflict
  (indices 26-49; index 25, the seam-closure duplicate, does not).
- Every other ring, **including the south pole** (`y = -1.0`): zero
  conflicts. The two poles are not symmetric under this mesh's own
  real triangulation — the south-pole fan's own winding produces
  unanimous handedness at every one of its 25 vertices, confirmed by
  direct per-triangle inspection, not assumed from north-pole symmetry.

**Triangles touching a conflicting vertex:** indices 0-95 (the first 96
of 768) — the north-polar fan and its own first adjacent band.
`ground_plane`/`textured_quad_left`/`textured_quad_right`/`minimal_cube`
are unaffected by this correction (none of their own audit figures
change).

**Corrected migration scope, mechanically simpler than "one copy per
triangle wedge":** since `h_face` is binary (`±1`), each of the 48
real conflicting vertices needs exactly **one** additional copy, not
one per wedge — group that vertex's own contributing triangle corners
by their own recorded `h_face` sign (two groups, by construction);
keep the original vertex index for one sign's group, repoint the other
group's own triangle corners to one new, identical-content (position/
color/UV/normal copied verbatim) vertex. Simulated on a temporary,
uncommitted in-memory copy of the real, current `pbr_sphere.mesh.txt`:
starting from 425 vertices / 768 triangles / 2304 indices / 0
UV-degenerate / 48 conflicts, this exact 2-way split produces **473
vertices** (425 + 48, one new vertex per conflicting vertex) / **768
triangles** (unchanged) / **2304 indices** (unchanged) / **0
UV-degenerate** (unchanged) / **0 handedness conflicts** — re-audited
with the identical, unmodified algorithm. This supersedes this ADR's
own Decision item 9 prose ("one copy per triangle wedge... vertex/
index count grows") with an exact, real, verified result: only the
vertex count grows (by exactly 48), the index and triangle counts do
not change at all.

This correction changes no Decision, no epsilon, no error enumerator,
and no consequence beyond the numbers above — the handedness-conflict
rejection requirement itself (item 4/5), and the fact that `pbr_sphere`
is the one mesh needing real migration while `minimal_cube` needs
none, both stand unchanged. [Plan 0029](../plans/0029-tangent-space-normal-mapping-foundation.md)
depends on this correction and uses its corrected figures throughout,
not the original `96/425` estimate.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-06, accepting this correction in full, as
drafted, with no change: the real `48/425` conflict count, its
location at the north-pole ring and its one adjacent ring, zero
conflicts at the south pole or any other ring, and the corrected,
mechanically-simpler sign-split migration method (one new vertex per
conflicting vertex, `425→473` vertices, `768` triangles/`2304` indices
unchanged, re-audited to `0` conflicts).

## Proposed Correction — 2026-09-06 (tangent-generation algorithm omits a geometric-degenerate-triangle check)

**Status:** Proposed. Pending Human Review. Does not rewrite the
Decision, Consequences, or Alternatives sections above, nor the
"Accepted Correction — 2026-09-06 (`pbr_sphere` handedness-conflict
count and location)" section immediately above — that section's own
`48/425` finding is preserved verbatim as historical record, not
deleted, even though this correction supersedes it. Supersedes: the
`48/425 (11.3%)`, north-pole-ring-0-plus-adjacent-ring-1 figure from
the section directly above; the original Decision-context table's own
`pbr_sphere` row (`96/425 (22.6%)`, "concentrated at the pole rings,"
framed as "a genuine chirality disagreement... not a zero-area
triangle"); and Decision item 9's own `pbr_sphere` migration
requirement ("must be re-authored to eliminate the... handedness
conflicts before this ADR's own cooker change lands"). None of these
three prior figures/framings survive this correction — see the
derivation below. Implementation of Plan 0029 stays **blocked pending
Human Review** of this correction, alongside the matching corrections
to [Spec 0029](../specs/0029-tangent-space-normal-mapping-foundation.md)
and [Plan 0029](../plans/0029-tangent-space-normal-mapping-foundation.md)
filed in the same pass. This ADR's own top-level `Status: Accepted`
(unchanged since this ADR's own original acceptance) is unaffected.

**Root cause.** This ADR's own first Accepted Correction, immediately
above, attributes its own `96→48` change to a specific difference
between probes: the original audit's own draft-stage probe
orthogonalized `T_face` against the vertex normal before taking its
sign, while the first Accepted Correction's own probe did not,
matching Decision item 4's literal text. That attribution is not
disputed here — but it is not the operative cause of either figure's
own inaccuracy. **Regardless of any pre-orthogonalization difference
between those two earlier probes, neither checked whether a triangle
is *geometrically* degenerate** — zero or near-zero real 3D area —
before computing `h_face` from it. A fresh re-audit, run strictly to
this ADR's own current, literal Decision item 4 formula verbatim (no
pre-orthogonalization, matching the first Accepted Correction's own
method exactly), reproduces **96/425** against the real, unmodified
`pbr_sphere.mesh.txt` — not the first Accepted Correction's own
`48/425`. **The first Accepted Correction's own `48/425` figure and its
own attributed root cause are both superseded by this Proposed
Correction**; that section's own text stays in place above as
historical record, but is no longer a valid basis for Implementation.
`pbr_sphere.mesh.txt` triangulates every
latitude band, poles included, as a uniform quad grid split into two
triangles per quad, rather than a triangle fan at the poles. At each
pole, one of the two triangles per quad is a real wedge (non-degenerate:
two ring vertices plus the pole point); the other closes the quad by
connecting two *different per-longitude copies of the same pole point*
(e.g. triangle `(0, 26, 1)`: vertices 0 and 1 are both authored at
position `(0, 1, 0)`, differing only in UV `u`). That second triangle
has a real 3D area of zero (or, at the south pole, a value within
floating-point noise of zero — see below) — but its UV-space
determinant is **not** small (`|det| ≈ 2.6×10⁻³`, since the pole's UV
is stretched into a full texture-space edge), so this ADR's own
existing UV-only degeneracy check (`|det| < 1e-12`) never flags it.
`T_face` computed from such a triangle is either the exact zero vector
or a vector dominated by float rounding noise; the handedness sign
function's own `|dot| < 1e-9` tie-break (item 3) then arbitrarily
assigns it `h_face = +1.0`, which "conflicts" against the real wedge
triangle's own genuine, well-defined `h_face` at the same vertex — a
false conflict between one real contribution and one meaningless,
degenerate one, never a true chirality disagreement between two valid
triangles.

**Re-audit, temporary and uncommitted, run against the current, real,
unmodified `pbr_sphere.mesh.txt`:**

- The existing, already-Accepted algorithm (raw `h_face`, UV-
  degeneracy check only) re-confirms exactly **96/425 (22.6%)**
  conflicting vertices, at rings 0, 1, 15, and 16 (24 each, vertex-
  index-order numbering — both poles and their one adjacent ring
  each), reproducing this ADR's own original, pre-correction figure,
  not its first Accepted Correction's `48/425`.
- Two new per-triangle diagnostics were added: `area2 = length(cross(e1,
  e2))` and `edgeScale = max(length(e1), length(e2), length(e2 - e1))`,
  `geometricRatio = area2 / edgeScale²`. Of `pbr_sphere`'s 768
  triangles, exactly **48** are geometrically degenerate by this
  measure — the pole-closing triangles above, none of them flagged by
  the UV-only check. At the **north pole** (rings 0/1, 24 triangles):
  the two vertices each closing triangle connects are authored as
  bit-identical duplicate positions (e.g. both exactly `(0, 1, 0)`),
  so `area2` computes to **exactly `0.0`**. At the **south pole**
  (rings 15/16, 24 triangles): the duplicate pole positions carry tiny
  floating-point noise in their `x`/`z` components (e.g.
  `1.2246468e-16` vs. `-2.9995196e-32`, both nominally `(0, -1, 0)`),
  so `area2` is not bit-exact zero but is still vanishingly small — the
  largest `geometricRatio` among these 24 is **≈1.8275×10⁻¹⁶**, twenty
  orders of magnitude below any plausible threshold.
- Every one of the 720 remaining (non-geometrically-degenerate)
  triangles has `geometricRatio ≥ 0.2276` (the smallest observed
  value) — the maximum degenerate `geometricRatio` (`≈1.83×10⁻¹⁶`) and
  this minimum valid `geometricRatio` (`≈0.2276`) sit approximately
  **15 orders of magnitude** apart, confirming a clean, well-separated
  gap between "degenerate sliver" and "real triangle," exactly the
  same kind of separation this ADR's own existing UV-degeneracy
  epsilon (item 3) already relies on for its own threshold choice.
- **All 96 conflicting vertices found by the existing algorithm are
  attributable to one of these 48 geometrically-degenerate triangles**
  (confirmed by direct cross-reference, not assumed) — none is a
  disagreement between two valid, non-degenerate triangles. Once the
  48 degenerate triangles are excluded from contributing `T_face`/
  `B_face`/`h_face` at all, the real, corrected conflict count for
  `pbr_sphere` is **`0/425`** — not `48` and not `96`.
- `ground_plane.mesh.txt`, `textured_quad_left.mesh.txt`,
  `textured_quad_right.mesh.txt`, and `minimal_cube.mesh.txt` are
  unaffected: zero geometrically-degenerate triangles in any of the
  first three (all-clean, matching this ADR's own original audit
  exactly); `minimal_cube`'s existing 12/12 UV-degenerate result is
  unaffected in kind (its own triangles are UV-degenerate, not
  geometrically degenerate — the new check adds nothing there).

**Corrected algorithm (Decision item 4, as amended by this proposed
correction):**

1. For each triangle, check **geometric** degeneracy *before* UV
   degeneracy: compute `e1 = pos(v1) - pos(v0)`, `e2 = pos(v2) -
   pos(v0)`, `e3 = pos(v2) - pos(v1)`, `edgeScale = max(length(e1),
   length(e2), length(e3))`, `area2 = length(cross(e1, e2))`.
2. If `edgeScale == 0` (all three vertices coincide — not observed in
   any of the 5 currently-committed meshes, a defensive case), the
   triangle is geometrically degenerate.
3. Otherwise, if `geometricRatio = area2 / edgeScale² < 1e-12`, the
   triangle is geometrically degenerate — this is the branch that
   catches `pbr_sphere`'s own real 48 pole-closing triangles (both the
   bit-exact-zero north-pole group and the noise-scale south-pole
   group, `1e-12` sitting comfortably above the largest observed
   near-zero value, `≈1.83×10⁻¹⁶`, and comfortably below the smallest
   observed valid value, `≈0.2276`).
4. A geometrically-degenerate triangle contributes **no** `T_face`,
   `B_face`, or `h_face` to any of its own three vertices — identical
   in kind to how a UV-degenerate triangle already contributes nothing
   (item 4's existing rule), just checked first.
5. Only a triangle that is **not** geometrically degenerate proceeds to
   the existing, unchanged UV-degeneracy check (`|det| < 1e-12`,
   item 3) — this check's own epsilon and formula are unaltered by
   this correction.
6. After excluding both geometrically- and UV-degenerate triangles, a
   vertex with zero remaining (non-degenerate) contribution uses the
   already-Accepted deterministic fallback tangent (item 4a) —
   unchanged in formula, only its trigger condition now also covers a
   vertex whose only would-be contributions were geometrically
   degenerate (`pbr_sphere` vertices 24 and 400 — the two seam-closure
   duplicates that, under the corrected algorithm, are referenced only
   by pole-closing triangles now excluded as geometrically degenerate,
   confirmed by re-audit).
7. **No new `CookError` enumerator is introduced.** A geometrically-
   degenerate triangle is a non-contributing triangle, exactly like a
   UV-degenerate one — both are silently excluded from the
   accumulator, never a per-triangle error.
8. `CookError::TangentHandednessConflict` (item 5, unchanged) now fires
   only when two **valid** (geometrically- and UV-non-degenerate)
   contributions at the same vertex genuinely disagree in `h_face`
   sign — a real chirality conflict, never an artifact of a degenerate
   triangle's own arbitrary tie-broken sign.

**Corrected existing-mesh audit (supersedes Decision item 9's own
`pbr_sphere` migration requirement in full):**

| Mesh | Triangles | Geometric-degenerate | UV-degenerate | Handedness conflicts | Fallback vertices | Action |
|---|---|---|---|---|---|---|
| `ground_plane.mesh.txt` | 2 | 0 | 0 | 0/4 | 0 | None |
| `textured_quad_left.mesh.txt` | 2 | 0 | 0 | 0/4 | 0 | None |
| `textured_quad_right.mesh.txt` | 2 | 0 | 0 | 0/4 | 0 | None |
| `minimal_cube.mesh.txt` | 12 | 0 | 12/12 | 0/8 | 8 (all) | None — cook-time fallback (unchanged) |
| `pbr_sphere.mesh.txt` | 768 | **48** | 0/720 | **0/425** | 2 (vertices 24, 400) | **None — no source edit, no migration** |

`pbr_sphere.mesh.txt` needs **no** re-authoring, no vertex split, and
no vertex/index count change under the corrected algorithm — it stays
**425 vertices, 768 triangles, 2304 indices**, byte-for-byte unchanged.
This removes Decision item 9's own `pbr_sphere` migration requirement
entirely, and with it every downstream `425→473`/sign-split
consequence this ADR's own first Accepted Correction (above) had
introduced. `minimal_cube` is unaffected — its own fallback path and
zero-source-change conclusion are unchanged in kind, only now also
covering `pbr_sphere`'s two seam vertices for a different, geometric
(not UV) reason.

**Required verification (added to Plan 0029's own scope once this
correction is accepted):**

- A triangle with an exactly-zero-length edge, or a real `area2` of
  exactly `0.0`, is excluded from contributing (the north-pole group).
- A triangle whose `area2` is nonzero but whose `geometricRatio` falls
  below `1e-12` is excluded identically (the south-pole, floating-
  point-noise group) — the two groups are handled by the same rule,
  not two different code paths.
- Uniformly scaling a triangle's own three vertex positions leaves its
  geometric-degeneracy classification unchanged (`geometricRatio` is
  scale-invariant by construction: `area2` scales as length², `edgeScale²`
  scales identically).
- A triangle that is geometrically degenerate but **not** UV-degenerate
  (`pbr_sphere`'s own real case) produces no `h_face`/`T_face`/`B_face`
  contribution at all — confirmed distinct from a UV-degenerate
  triangle, which this codebase's pre-existing check already excludes
  for an unrelated reason.
- `pbr_sphere`'s own real, corrected audit reports exactly `48`
  geometric-degenerate triangles, `0` UV-degenerate, `0` handedness
  conflicts, out of 768 total.
- `pbr_sphere` vertices 24 and 400 land on the deterministic fallback
  path (item 4a), confirmed by re-audit, not by assumption.
- Two genuinely valid, non-degenerate triangles with real, opposite
  `h_face` signs at a shared vertex still trigger
  `CookError::TangentHandednessConflict` — the conflict-rejection
  requirement itself (item 5) is unweakened; this correction only
  removes a false-positive source, never a true-positive one.

**Deciders:** Pending Human Review — not yet approved. This section
proposes, and does not itself accept, the geometric-degeneracy check
above, the corrected `pbr_sphere` audit (0 conflicts, no migration
needed), and the corresponding removal of the `425→473`/sign-split
consequence this ADR's own first Accepted Correction had introduced.
