# ADR 0063: Static Mesh Normal Attribute — Schema, Version, Numeric Contract, and Coordinate Convention

- **Status:** Accepted
- **Date:** 2026-08-29
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-29 as part of [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
  own Human Review Approval, following one final, targeted review round
  that corrected the numeric contract from a `length`-based,
  `std::sqrt`-implying tolerance to a `length`-squared, double-precision
  one, and reversed a comment-only layout-offset decision to four real,
  named, `static_assert`-checked constants — see that Spec's own "Final
  Review Round" section for the full record.
- **Related Spec:** [specs/0020-mesh-normal-attribute-foundation.md](../specs/0020-mesh-normal-attribute-foundation.md) (`Approved`)
- **Related ADR(s):** [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format/versioning policy — directly narrowed by its own second
  Accepted Amendment, accepted in the same Human Review pass as this
  ADR), [ADR-0058](0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (the immediately-prior, structurally-identical precedent for adding a
  mandatory attribute to the one, single static mesh vertex layout —
  also directly narrowed by its own Accepted Amendment, accepted in the
  same pass).

## Context

Confirmed directly against real, current source (Spec 0020's own
Pre-draft verification, not repeated here in full): the static mesh
vertex layout is fixed, end to end, exactly as ADR-0058 already
described for UV0's own addition — `MeshSourceVertex` is
`{positionX/Y/Z, colorR/G/B, uvU/V}`, eight `float` fields;
`kMeshArtifactVertexStrideBytes = 32` is one single, global,
compile-time constant; `StaticMeshAssetData` is already layout-agnostic;
`atlantis::renderer::createMesh()` never inspects vertex layout;
`toVertexInputLayout()` matches by shader-reflected `location` and
attribute *count* only, never by stride. No `.slang` shader in this
codebase declares or consumes a normal input today. Spec 0019
("Lighting Foundation," `Approved`, `Blocked by Spec 0020
implementation`) needs a real, asset-sourced object-space normal and
explicitly defers every part of its own byte layout, version, and
numeric contract to this ADR.

## Decision

**A fourth, mandatory attribute, object-space `normal` (3 floats), is
added to the one, single static mesh vertex layout — position (3
floats) + color (3 floats) + UV0 (2 floats) + normal (3 floats), 11
floats / 44 bytes per vertex, replacing the current 8-float/32-byte
layout. No optional/variant vertex-layout mechanism is introduced.**

1. **Authoring grammar:** each `vertex:` line gains three trailing
   space-separated float tokens (`nx ny nz`), extending the existing
   exactly-8-field line to exactly 11 — `MeshSourceVertex` gains
   `normalX`/`normalY`/`normalZ` fields. The grammar's own version
   marker becomes `atlantis_static_mesh_source_version: 3`; any other
   value is rejected with the existing
   `SourceParseError::UnknownSourceVersion` — no dual-version reader.
2. **Runtime artifact layout:** per-vertex bytes become position xyz
   (offset 0), color rgb (offset 12), UV0 uv (offset 24), normal xyz
   (offset 32) — 44 bytes total, no padding, no gap.
   `kMeshArtifactVertexStrideBytes` becomes `44`;
   `kMeshArtifactSchemaVersion` becomes `3`. An artifact declaring
   schema version 1 or 2, or any stride other than 44, is rejected with
   the existing `ArtifactDecodeError::UnknownSchemaVersion` /
   `UnsupportedVertexStride` — no migration reader. The three new
   floats use the exact same `appendFloatLE`/`readFloatLE` explicit
   little-endian routine every existing float already uses.
3. **No migration mechanism, no back-compat reader, no silent default**
   — identical in kind to ADR-0058's own Decision item 3. Every
   currently-authored mesh source file is re-authored with real,
   disclosed values (Spec 0020's own D5/D6) as part of this ADR's own
   Implementation; none is left on the old 8-field grammar, none is
   silently defaulted.
4. **Numeric contract:** each normal component must be finite first
   (the existing `NonFiniteFloat` check, extended in kind from 8 to 11
   floats — no new enumerator for this part, and evaluated before the
   check below). **The normal vector's own length-*squared* — never
   `length`, never `std::sqrt` — computed via explicit double-precision
   arithmetic (each `float` component promoted to `double`, an exact,
   zero-rounding conversion, before `x·x + y·y + z·z`) must fall within
   `[0.9801, 1.0201]`** (`0.99²`/`1.01²`, the same real ±1%-of-unit-
   length tolerance stated on the squared quantity, chosen precisely to
   avoid `std::sqrt`'s own platform/ISA-dependent rounding-path
   variability across this codebase's own two Phase 1 targets, x86-64
   Windows today and ARM/AArch64 Android tomorrow), checked
   independently at cook time and decode time, via a new pair of
   enumerators, `SourceParseError::NonUnitNormal` /
   `ArtifactDecodeError::NonUnitNormal` — confirmed, by a direct search
   of every current `SourceParseError`/`ArtifactDecodeError` use in this
   codebase, to be the one genuinely new failure kind neither enum's own
   existing list expresses (deliberately never `NonFiniteFloat`, whose
   own name and every existing use already means non-finite, never
   finite-but-wrong-magnitude). Neither layer ever rewrites, rescales,
   or auto-normalizes an authored normal — a value outside tolerance is
   rejected outright. A zero vector, `-0.0` on any component, and an
   extremely small non-zero vector are all correctly rejected by this
   same single check — no separate special case exists or is needed for
   any of them.
5. **Coordinate convention:** object-space (before any world transform;
   Spec 0019's own D7 is the sole consumer of the transform step),
   right-handed (this codebase's own already-established convention,
   inherited from `Camera`'s own `-column2`-forward extraction — not a
   new handedness rule). **The cooker performs no winding-consistency
   validation, no auto-flip, and no auto-generation of any kind** — a
   vertex's own authored normal need not equal, or even agree with, any
   single adjacent triangle's own winding-computed face normal (a
   smooth-shaded normal is, by construction, an average across several
   faces, and legitimately disagrees with each of them individually).
   The author bears full responsibility for a correctly-facing normal.
6. **`minimal_cube.mesh.txt`'s own 8 shared vertices, 12 triangles, and
   36-index list are unchanged** — this ADR adds smooth (vertex-
   averaged) normals, computed as each corner's own normalized position.
   Exact decimal literal, chosen so `std::from_chars` recovers a
   specific, unambiguous `binary32` value: `0.577350269` (nine
   significant digits of `1/√3 = 0.5773502691896258…`), sign-matched
   per component to that corner's own `(±0.5, ±0.5, ±0.5)` position —
   written directly into the checked-in authoring source as a plain
   literal, never computed by the cooker at build time (this ADR's own
   item 5 above, restated for emphasis at the one asset where it would
   be easiest to implement as a convenience). **Explicitly labeled
   smooth-shaded, never described as hard-face**, since no current
   shader reads this region at all and the mesh's own triangle/index
   structure stays byte-for-byte unchanged.
7. **`textured_quad_left.mesh.txt`/`textured_quad_right.mesh.txt`** each
   receive the uniform normal `(0, 0, 1)` for all four vertices —
   confirmed correct by direct cross-product computation of **both**
   triangles of **each** quad independently against each quad's own
   real index winding and real, flat, `z = 0` vertex positions, not
   assumed from one triangle or from visual orientation; all four
   independent computations agree.
8. **No new RHI/RenderGraph/Renderer public API.**
   `atlantis::rhi::VertexAttributeFormat::Float3` and
   `atlantis::shader_system::rhi_integration::VertexAttributeType::Float3`
   already exist and are reused verbatim; `createMesh()`'s own
   `layout` parameter remains unconditionally unread. **All four
   attributes' own byte offsets become real, named, public
   `constexpr std::size_t` constants in `mesh_artifact.h`** —
   `kMeshArtifactPositionOffsetBytes = 0`,
   `kMeshArtifactColorOffsetBytes = 12`, `kMeshArtifactUv0OffsetBytes = 24`,
   `kMeshArtifactNormalOffsetBytes = 32` — alongside the existing
   `kMeshArtifactVertexStrideBytes`/`kMeshArtifactSchemaVersion`, closing
   a real testability gap a comment-only offset would have left open (a
   future consumer hand-writing the wrong magic offset, with nothing to
   catch the mistake at build time). Each of the six real composition-
   root touch points gains four `static_assert`s tying its own local
   `Vertex` struct's `offsetof()` values to these constants — a
   compiler-enforced, always-in-sync guarantee, not a documentation
   convention. This remains zero new *runtime* API anywhere (`constexpr`/
   `static_assert`/`offsetof` are compile-time only) and introduces no
   general vertex-schema system.

## Consequences

### Positive

- Every mechanism this ADR needs already exists and is directly
  reusable — zero new RHI type, zero new serialization primitive, zero
  new vertex-attribute-format enumerator.
- The numeric contract (unit-length, never auto-corrected) gives Spec
  0019's own later shading math a real, trustworthy input — no
  hidden renormalization anywhere upstream of the shader's own
  `normalize()` call.
- `minimal_cube`'s own choice (smooth, disclosed, zero structural
  change) keeps this ADR's own Implementation strictly additive to
  every existing golden — no image-regression risk anywhere.

### Negative / Trade-offs

- Every static mesh's own per-vertex byte cost grows from 32 to 44
  bytes (+37.5%), including meshes whose own shader never reads the
  normal region — a real, permanent, accepted cost, identical in kind
  to ADR-0058's own already-accepted UV0 cost.
- `minimal_cube`'s own smooth normals are not usable as a hard-face-
  shading demonstration without a future, separate, disclosed decision
  to split it into 24 non-shared vertices — a real, named, deferred
  limitation, not a silent one.
- This ADR narrows already-`Accepted` sentences of both ADR-0045 and
  ADR-0058 — two Proposed Amendments, filed alongside this ADR, are
  both required before this ADR itself may reach `Accepted`.

## Alternatives Considered

See [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
own "Alternatives Considered" section for the full, disclosed
reasoning behind every rejected option (a per-asset optional vertex
layout, automatic/cooker-computed normal generation, cooker-side
auto-normalization, `std::sqrt`-based length validation, a 24-vertex
hard-face `minimal_cube`, a placeholder/known-meaningless normal, and a
named offset constant for normal alone while leaving the other three
attributes comment-only) — not restated here to avoid duplicating one
argument in two documents.
