# Spec: Mesh Normal Attribute Foundation

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-29
- **Related Plan(s):** none yet — this Spec must reach `Approved`, and its
  own future Plan must reach `Approved` and be **fully implemented and
  merged**, before Plan 0019 ("Lighting Foundation") may be drafted —
  see [Spec 0019](0019-lighting-foundation.md)'s own D1 and its
  header's own governance-gate text, which this Spec exists to satisfy.
- **Related ADR(s):** [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md) (`Proposed`), plus Proposed Amendments to [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md) and [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)

## Summary

[Spec 0019](0019-lighting-foundation.md)'s own D1 named this Spec as a
hard, non-negotiable prerequisite: Atlantis has no real, asset-sourced
per-vertex normal anywhere in its own static mesh pipeline today, and no
genuine lighting math can run without one. This Spec adds exactly that —
nothing more. It widens the one, single, fixed static mesh vertex layout
(position + color + UV0, Spec 0017/ADR-0058) with a fourth, mandatory
attribute, object-space `normal` (3 floats), following the identical
mechanical pattern ADR-0058 already used to add UV0: a new authoring
grammar version, a new runtime artifact schema version, both old
versions rejected outright with no migration reader, and every existing
mesh source re-authored with real, non-guessed normal values.

This Spec implements **only** the data contract: mesh authoring → cook →
portable artifact → load → a composition root's own ability to read a
real, asset-sourced object-space normal. It renders nothing new, adds no
shader, no Lighting, no Lit Material, and no new golden. Its own
Implementation PR merging is the literal gate Spec 0019's Plan is
blocked on.

## Motivation

Confirmed directly against real, current source (this Spec's own
Pre-draft verification, below, not summarized here): `MeshSourceVertex`
is exactly `{positionX/Y/Z, colorR/G/B, uvU/V}` — eight `float` fields,
no ninth. The runtime mesh artifact's per-vertex stride is a single,
global, compile-time constant, `kMeshArtifactVertexStrideBytes = 32`.
Nothing in this codebase — no `.slang` shader, no C++ composition root,
no test fixture — declares or consumes a per-vertex normal anywhere.
Spec 0019's own D1 already rejected every faked alternative (position-
derived, derivative-derived, or a shader-hardcoded constant normal) on
the merits, not merely by drafting-brief instruction — a faked normal
would make "lighting" pixels not actually driven by asset-authored
geometry data, undermining the entire point of a scene-driven lighting
proof. A real normal attribute is therefore a genuine, structural gap,
not a convenience.

## Goals

1. Widen the one, single, fixed static mesh vertex layout with a fourth,
   mandatory attribute: object-space `normal` (3 floats), following
   ADR-0058's own exact "one fixed shape, no per-asset variability"
   precedent — no optional/variant vertex layout is introduced.
2. A new authoring source version and a new runtime artifact schema
   version, both old versions rejected outright, no migration reader —
   matching ADR-0058's own version-1-to-2 precedent exactly.
3. A precise, enforced numeric contract for the normal value itself
   (finite, unit-length within a stated tolerance) — checked
   independently at cook time and decode time, never silently
   normalized, generated, or guessed.
4. Every currently-authored mesh source file in this repository
   (`minimal_cube.mesh.txt`, `textured_quad_left.mesh.txt`,
   `textured_quad_right.mesh.txt`) re-authored with a real, deliberately
   chosen, non-guessed object-space normal per vertex — none left on the
   old grammar, none silently defaulted.
5. Every real composition-root call site that hardcodes a local
   `Vertex` struct matching the mesh artifact's own byte layout widened
   in lockstep — freshly, exhaustively enumerated against current
   source (see Pre-draft verification), not assumed from Spec 0017's
   own now-stale file count.
6. Zero change to any existing, committed golden (`minimal_cube`,
   `world_scene`, `textured_quad`, `material_demo`) — every existing
   shader continues to not declare a normal input, so the new attribute
   occupies an unread region of a wider stride, exactly matching how
   `minimal_mesh.slang`'s own unread UV0 region already works today.
7. Zero new RHI, RenderGraph, or Renderer public API — `VertexAttributeFormat::Float3`
   already exists and is directly reused.

## Non-Goals

- Lighting of any kind, or any lighting math — entirely [Spec 0019](0019-lighting-foundation.md)'s
  own scope, consuming this Spec's own output only after this Spec's
  own Implementation PR merges.
- A Lit Material, a lit shader, or any new rendered output whatsoever —
  this Spec adds no `.slang` file and touches no existing one.
- Tangent or bitangent vertex attributes, or any normal-mapping support.
- A normal map or any texture-driven normal perturbation.
- Automatic normal generation of any kind — no face-average, no
  smoothing-group, no importer-computed normal. Every normal value in
  this Spec's own re-authored mesh sources is a real, deliberately
  chosen, hand-computed value, disclosed in this document (D5, D6), not
  produced by a tool this Spec builds.
- A hard-edge/smoothing-group importer, or any authoring-tool support
  beyond this format's own existing plain-text grammar.
- A second, optional, or per-asset-variable vertex layout — the one
  fixed shape widens; no selection mechanism is introduced (ADR-0058's
  own precedent, unchanged in kind).
- Skeletal or animated normals, or any per-frame normal recomputation.
- A non-uniform-scale-correct (inverse-transpose) normal *transform* —
  that is Spec 0019's own D7 concern, operating on this Spec's own
  static, object-space output; this Spec ships the raw attribute only.
- PBR, shadows, IBL, or post-processing of any kind.
- Any new third-party dependency or new top-level module.
- Android, iOS, or Linux implementation.

## Requirements

### Functional

- **Authoring grammar (version 3):** each `vertex:` line gains three
  trailing space-separated float tokens (`nx ny nz`), extending the
  existing exactly-8-field line to exactly 11. `MeshSourceVertex` gains
  `normalX`/`normalY`/`normalZ` fields, matching its own existing
  `positionX`/`colorR`/`uvU`-style naming convention exactly (D1). The
  grammar's own version marker becomes
  `atlantis_static_mesh_source_version: 3`; a source file declaring any
  other value is rejected with the existing
  `SourceParseError::UnknownSourceVersion` — no dual-version reader.
- **Runtime artifact (schema version 3):** per-vertex bytes gain a
  fourth region, normal xyz at offset 32 — 44 bytes total.
  `kMeshArtifactVertexStrideBytes` becomes `44`;
  `kMeshArtifactSchemaVersion` becomes `3`. An artifact whose header
  declares schema version 1 or 2, or any stride other than 44, is
  rejected with the existing `ArtifactDecodeError::UnknownSchemaVersion`
  / `UnsupportedVertexStride` — no migration reader. The three new
  normal floats are serialized with the exact same
  `appendFloatLE`/`readFloatLE` explicit little-endian routine every
  existing float already uses.
- **Numeric contract (D3):** each normal component must be finite (the
  existing per-vertex `NonFiniteFloat` check, extended from 8 to 11
  floats per vertex, unchanged in kind); the normal vector's own length
  must fall within `[0.99, 1.01]` of unit length, checked independently
  at cook time and decode time — a new
  `SourceParseError::NonUnitNormal` / `ArtifactDecodeError::NonUnitNormal`
  pair (genuinely new failure kinds; no existing enumerator covers
  "correct magnitude"). Neither the cooker nor the decoder ever rewrites
  a normal's own authored bit pattern to force it onto the unit sphere —
  a non-conforming value is rejected outright, never silently corrected.
- **No migration mechanism, no back-compat reader, no silent default**
  — identical in kind to ADR-0058's own Decision item 3. Every
  currently-authored mesh source file is re-authored with real,
  disclosed normal values as part of this Spec's own Implementation;
  none is left on the old 8-field grammar, and none is silently
  defaulted to `(0, 0, 0)` (which would also fail the new unit-length
  check, structurally preventing this particular silent default).
- **Every real composition-root call site widened in lockstep** — see
  Pre-draft verification for the freshly-confirmed, exhaustive list;
  this Spec's own future Plan may not rely on Spec 0017/ADR-0058's own
  now-stale four-file count.
- **Zero new RHI/RenderGraph/Renderer API.** `atlantis::rhi::VertexAttributeFormat::Float3`
  and `atlantis::shader_system::rhi_integration::VertexAttributeType::Float3`
  both already exist and are reused verbatim for the normal attribute;
  `atlantis::renderer::createMesh()` requires no change (its own
  `layout` parameter is already, unconditionally unread — confirmed by
  direct inspection, unchanged since ADR-0058).

### Non-functional

- **Performance:** not a goal. The per-vertex byte cost grows from 32 to
  44 bytes (+37.5%) for every static mesh in this codebase, including
  ones whose own shader never reads the normal region — an accepted,
  disclosed, permanent trade-off, identical in kind to ADR-0058's own
  already-accepted UV0 cost.
- **Portability:** the three new normal floats follow the exact same
  unconditional little-endian, `std::bit_cast`-based serialization
  discipline ([ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md))
  every existing vertex float already uses.
- **Other:** new error enumerators added only where no existing one
  covers a genuinely new failure mode (the unit-length check); every
  other new rejection path (wrong field count, wrong version, wrong
  stride) reuses an existing enumerator, matching ADR-0058's own "no new
  error enumerator anywhere" precedent exactly.

## Pre-draft verification against real, current source

Confirmed directly against `main` at Spec-drafting time (2026-08-29,
immediately following [Spec 0019](0019-lighting-foundation.md)'s own
Human Review Approval and PR #90's merge), by reading full files, not
from memory or by reusing ADR-0058's own now-two-Specs-old file counts:

- **`MeshSourceVertex`/`mesh_source.cpp`:** exactly eight `float`
  fields (`positionX/Y/Z, colorR/G/B, uvU/V`), no ninth.
  `parseMeshSource()` hard-requires `fields.size() != 8` as a parse
  error and hard-matches the version line
  (`atlantis_static_mesh_source_version: 2`) as an exact string.
  `kMaxVertexCount = 65535` (the `std::uint16_t` index domain cap,
  attribute-width-independent, unaffected by this Spec).
- **`mesh_artifact.h`/`.cpp`:** `kMeshArtifactSchemaVersion = 2`,
  `kMeshArtifactVertexStrideBytes = 32`, `kMeshArtifactHeaderSizeBytes = 40`.
  Per-vertex layout, byte-exact: position X/Y/Z at offsets 0/4/8, color
  R/G/B at offsets 12/16/20, UV0 U/V at offsets 24/28 — no padding, no
  gap. `decodeMeshArtifact()`'s own per-vertex finiteness check loops
  over exactly 8 floats (`floatIndex < 8`), hardcoded. `encodeMeshArtifact()`/
  `decodeMeshArtifact()` both use the identical `appendFloatLE`/`appendU32LE`/
  `readFloatLE`/`readU32LE` explicit shift/mask little-endian
  primitives for every field — directly reusable for the three new
  normal floats, no new serialization primitive needed.
- **`StaticMeshAssetData`:** already layout-agnostic — a tightly-packed
  `std::vector<std::byte> vertexBytes_` plus a runtime
  `vertexStrideBytes_` field, not a hardcoded struct. Requires zero
  source change, exactly as ADR-0058 already established for UV0 —
  confirmed unchanged. `vertexBytes().size() / vertexStrideBytes()` is
  its own `vertexCount()`'s existing implementation; unaffected by a
  wider stride value.
- **`atlantis::renderer::createMesh()` (`src/renderer/src/mesh.cpp`):**
  its own `layout` parameter (renamed from ADR-0058's own citation, but
  functionally identical) is unconditionally unread —
  `static_cast<void>(layout);`, confirmed by direct inspection of the
  current file, unchanged since ADR-0058. Zero change required.
- **`toVertexInputLayout()` (`vertex_input_mapping.cpp`):** matches a
  caller's `MeshVertexAttributeSchema` against a shader's own reflected
  `vertexInputAttributes` by `location`, requiring an exact *count*
  match (`vertexInputAttributes.size() != schema.size()`) in both
  directions — never inspecting total vertex stride versus
  attribute-referenced bytes. `toRhiFormat()` maps
  `VertexAttributeType::Float3`/`Float2` to
  `atlantis::rhi::VertexAttributeFormat::Float3`/`Float2` — **no other
  enumerator value exists in either enum**; a normal attribute (a
  `float3`) requires zero new enumerator value anywhere in this
  mapping. Confirms directly, not by inference: a shader that does not
  declare a normal input is structurally unaffected by the new,
  unreferenced stride region — the identical mechanism that already
  makes `minimal_mesh.slang`'s own unread UV0 region safe.
- **Current shader-declared locations, both real `.slang` files, read in
  full:** `minimal_mesh.slang`: `[[vk::location(0)]] float3 position;
  [[vk::location(1)]] float3 color;` — no UV0, no normal.
  `textured_quad.slang`: `[[vk::location(0)]] float3 position;
  [[vk::location(1)]] float2 uv;` — no color, no normal. **Neither
  shader declares a normal input; this Spec adds none.**
- **The three real, currently-authored `.mesh.txt` authoring sources**
  (`assets/meshes/`): `minimal_cube.mesh.txt` — 8 vertices, 36 indices
  (12 triangles), a real cube whose 8 corners are each shared by 3
  mutually-perpendicular faces (confirmed by direct inspection of its
  own index list — each of the 8 vertex indices appears in exactly 3 of
  the 12 triangles). `textured_quad_left.mesh.txt` /
  `textured_quad_right.mesh.txt` — 4 vertices, 2 triangles each, both
  flat quads in the XY plane at `z = 0`; both use the identical winding
  (`index: 0 1 2` then `index: 2 3 0`), confirmed by direct
  cross-product computation of triangle `0-1-2`'s own edge vectors to
  produce a face normal of exactly `(0, 0, +1)` for both quads (D6).
- **Every `struct Vertex` in this tree that actually consumes the real
  mesh artifact's own byte layout, freshly enumerated (not reused from
  ADR-0058's own 2026-08-25 count, which predates both Spec 0018's
  `material_demo_fixture.cpp` and this session's own new
  `tests/runtime/material_realization_gpu_tests.cpp`):**
  - `src/runtime/src/runtime_application.cpp` — real: builds the
    `VertexInputLayout` passed into `loadAndInstantiateScene()`, which
    feeds real mesh bytes to `createMesh()`.
  - `tests/image_regression/fixture/material_demo_fixture.cpp` — real,
    same shape (calls `loadAndInstantiateScene()` directly, Spec 0018
    D12's own precedent) — **not present at ADR-0058's own drafting
    time**, confirming the review's own concern that reusing the old
    count would have missed a real file.
  - `tests/image_regression/fixture/minimal_cube_fixture.cpp` — real,
    calls `loadStaticMeshAsset()` directly.
  - `tests/image_regression/fixture/textured_quad_fixture.cpp` — real,
    calls `loadStaticMeshAsset()` directly.
  - `tests/image_regression/fixture/world_scene_fixture.cpp` — real,
    calls `loadStaticMeshAsset()` directly.
  - `tests/image_regression/fixture/world_scene_loaded_fixture.cpp` —
    real, calls `loadStaticMeshAsset()` directly.
  - **Confirmed NOT a required touch point, despite matching name/shape:**
    `tests/runtime/material_realization_gpu_tests.cpp`'s own local
    `Vertex` struct (added this session, alongside the Plan 0018 PR #88
    final-review round) never loads or consumes any real mesh artifact
    data — it exists solely to satisfy `toVertexInputLayout()`'s own
    signature for building a Pipeline's vertex-input description in a
    test that never draws geometry. Its own `sizeof(Vertex)` happens to
    match the *current* 32-byte stride by copy-paste convention, not by
    a real dependency on `StaticMeshAssetData::vertexStrideBytes()` —
    left untouched by this Spec's own Plan unless a Plan-time reviewer
    finds an actual reason otherwise.
  - `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` and
    `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` — confirmed
    **not** touch points: both declare a `{position, color}`-only
    (6-float, pre-Spec-0017) `Vertex`, hand-authoring raw vertex data
    directly into a `Buffer` with no `loadStaticMeshAsset()` call
    anywhere in either file — entirely disconnected from the Asset
    System mesh artifact pipeline this Spec widens.
  - **Nine test files carry embedded, literal mesh-source text**
    (a `vertex:`-line count per file, confirmed by direct grep, not
    estimated): `tests/asset_system/load_tests.cpp` (7),
    `tests/asset_system/mesh_source_tests.cpp` (26),
    `tests/asset_system/mesh_uv_round_trip_tests.cpp` (3),
    `tests/runtime/material_realization_gpu_tests.cpp` (3),
    `tests/runtime/scene_load_tests.cpp` (3),
    `tests/runtime/scene_manifest_tests.cpp` (3),
    `tests/tools/asset_cooker/cook_command_tests.cpp` (10),
    `tests/tools/asset_cooker/cooker_determinism_tests.cpp` (3) — each
    literal 8-field `vertex:` line in every one of these needs its own
    three new trailing floats (or a deliberate negative-test case
    left at the old field count, to prove the new `CountMismatch`
    rejection). This Spec's own future Plan must enumerate every one of
    these by name, not merely this Spec's own rough per-file count.
- **`ADR-0045`/`ADR-0058`, read in full, both re-checked for a closed
  fixed-schema declaration (D10):** confirmed both close the schema —
  ADR-0045's own already-`Accepted` Decision (as amended 2026-08-25)
  states the runtime artifact/authoring source formats are scoped to "a
  static triangle mesh with per-vertex **position, color, and UV0**" —
  a closed enumeration that must be amended again to include normal.
  ADR-0058's own already-`Accepted` Decision states "UV0 becomes a
  mandatory third attribute of the **one, single** static mesh vertex
  layout — position + color + UV0, 8 floats/32 bytes per vertex" — also
  a closed statement (implicitly fixing the layout at exactly three
  attributes) that must be amended to admit a fourth. **Neither ADR is
  silently rewritten** — each gets its own dated, separately-labeled
  Proposed Amendment (Architectural Impact, below), following
  ADR-0045's own existing "Accepted Amendment — 2026-08-25" section as
  the direct, in-repository precedent for the amendment's own shape.
- **Spec 0019's own already-`Approved` normal-consumption/transform
  contract, re-read in full:** D7 consumes "a real, asset-sourced
  vertex normal" via the object-to-world matrix's own upper-left 3×3,
  checked against a conformal-transform condition; D8 states the
  `lit_textured` vertex shader needs a third attribute,
  `normal`, whose exact location/byte offset is deferred explicitly to
  "Spec 0020's own final field order" — confirming this Spec's own D1
  (below) fixes exactly the value this deferral names, and nothing
  about transform math, Lighting, or shader contract belongs in this
  Spec (D9).

## Proposed Design

```
mesh authoring (.mesh.txt, version 3)
  vertex: px py pz  cr cg cb  uu uv  nx ny nz
        |
        v
parseMeshSource() -- version-line exact match; per-line field count
  (11, not 8); per-float finiteness (extended to 11); NEW: per-vertex
  normal-length check in [0.99, 1.01] (D3) -- rejects outright, never
  normalizes or generates
        |
        v
encodeMeshArtifact() -- same explicit little-endian primitives,
  extended to write 3 more floats per vertex; stride constant becomes
  44; schema version becomes 3
        |
        v
mesh artifact (.amesh, schema version 3, 44-byte stride) -- portable,
  little-endian, independent of host/platform
        |
        v
decodeMeshArtifact() -- independently re-validates schema version,
  stride, per-float finiteness (11 floats), and the identical
  normal-length check -- never trusts the cooker
        |
        v
StaticMeshAssetData (unchanged type, wider vertexStrideBytes() value) --
  zero Asset System source change beyond the two functions above
        |
        v
a composition root's own local Vertex struct (position[3]+color[3]+
  uv0[2]+normal[3], 44 bytes) + createMesh() (unchanged, still
  layout-blind) -- the normal is now real, GPU-buffer-resident bytes a
  future shader (Spec 0019's own lit_textured, not this Spec) can
  declare a location for and read
```

## Decisions for Human Review

### D1. Fixed schema, exact field order/offsets/stride — confirmed against real code, not merely proposed

**Decision:** continue the single, fixed-layout precedent (no optional
vertex layout) — `position[3] + color[3] + uv0[2] + normal[3]`, normal
**appended** after the existing three attributes (not inserted between
them), matching ADR-0058's own "extend, do not reorder" precedent for
UV0's own addition. Confirmed against real, current
`kMeshArtifactVertexStrideBytes`/per-vertex-layout comment (Pre-draft
verification): **position offset 0, color offset 12, UV0 offset 24,
normal offset 32, stride 44 bytes** — exactly the candidate values this
Spec's own drafting brief proposed, verified rather than assumed. No
padding, no gap, matching the existing format's own "no padding"
discipline verbatim.

### D2. Authoring/artifact version — a real, distinguishable rejection path for each failure kind

**Decision:** authoring source version `2 → 3`
(`atlantis_static_mesh_source_version: 3`); artifact schema version
`2 → 3`. Both old versions rejected outright — no dual-version reader,
no migration mechanism, identical in kind to ADR-0058's own version-1-
to-2 precedent. **Missing normal and an old version are already,
structurally distinguishable rejection paths, with no new code
required to keep them so:** a source file still declaring
`atlantis_static_mesh_source_version: 2` fails at the version-line
check (`SourceParseError::UnknownSourceVersion`), evaluated **first**,
before any per-vertex field is ever read; a source file whose version
line correctly reads `3` but whose `vertex:` lines still carry only 8
fields (normal genuinely omitted) fails at the separate,
already-existing per-line field-count check
(`SourceParseError::CountMismatch`), evaluated per vertex, only after
the version check has already passed. The artifact layer has the
identical two-step structure (`UnknownSchemaVersion` first,
`UnsupportedVertexStride` second) — confirmed directly against
`decodeMeshArtifact()`'s own real, current code, not merely asserted.

### D3. Normal numeric contract — required unit length, checked, never auto-corrected

**Decision:** each normal component must be finite (the existing
`NonFiniteFloat` check, extended in kind from 8 to 11 floats — no new
enumerator). The normal vector's own length must fall within `[0.99,
1.01]` of unit length — a real, stated tolerance (not an unstated "close
enough"), chosen to tolerate ordinary hand-typed decimal precision
(e.g. `0.577350` for a cube-corner direction, D5) while still rejecting
a genuinely unnormalized author error (e.g. `1 1 1`, length ≈ `1.732`,
well outside tolerance). Checked independently at cook time
(`parseMeshSource()`) and decode time (`decodeMeshArtifact()`), each via
a new, shared-in-kind pair of enumerators,
`SourceParseError::NonUnitNormal` / `ArtifactDecodeError::NonUnitNormal`
— genuinely new failure kinds; no existing enumerator covers "correct
magnitude." **Neither layer ever rewrites, rescales, or auto-normalizes
a normal's own authored bit pattern** — a value outside tolerance is
rejected outright, matching this Spec's own explicit drafting-brief
instruction to avoid any transformation of author input that could
introduce platform floating-point differences. A zero vector (length
exactly `0`) is rejected by this identical check — no separate
zero-length special case is needed. `NaN`/`Inf` are rejected by the
existing, extended `NonFiniteFloat` check, evaluated before the
length check (matching the existing per-float-then-per-vertex
validation order).

### D4. Coordinate convention — object-space, right-handed, author-responsible, never auto-flipped

**Decision:** every normal is authored and stored in **object space**
(the mesh's own local coordinate frame, before any world transform is
applied) — Spec 0019's own D7 is the one place a normal is ever
transformed, and it consumes exactly this object-space value. **Right-
handed**, matching this codebase's own already-established convention
(`Camera`'s own forward extraction, `-column2` of a world matrix,
already implies a right-handed, -Z-forward frame — this Spec invents no
new handedness rule, it inherits the one already in force). A
triangle's own face normal (for reference, not enforcement — see below)
follows the standard right-hand rule from its own index winding order:
`cross(v1 - v0, v2 - v0)`, confirmed by direct computation to already
equal `(0, 0, +1)` for both existing textured quads (D6).

**The cooker performs no winding-consistency validation, no
auto-flip, and no auto-generation of any kind — restated as a firm
Decision, not merely a Non-Goal.** A vertex's own authored normal is
not required to equal, or even be consistent with, any single adjacent
triangle's own winding-computed face normal — this is a deliberate,
necessary property, not an oversight: a smooth-shaded vertex normal
(D5's own cube choice) is, by construction, the *average* of multiple
adjacent faces' own directions, and will not equal any one of them
individually. Building a "does this normal roughly agree with its own
triangle's winding" check would therefore either reject every
legitimate smooth-normal mesh, or need to special-case smooth vs. hard
shading — real, unjustified complexity this Spec's own minimal scope
does not need. The author (a human, or this Spec's own disclosed,
hand-computed re-authoring of the three existing mesh sources) is fully
responsible for a correctly-facing normal; the cooker's own
responsibility is limited to the numeric contract (D3) alone.

### D5. The existing shared-vertex cube — smooth, deterministic, honestly-labeled normals; the mesh itself untouched

**Decision:** `minimal_cube.mesh.txt` keeps its own existing 8 shared
vertices, 12 triangles, and 36-index list completely unchanged — this
Spec adds **smooth (vertex-averaged) normals**, not hard-face ones,
computed as each vertex's own position, normalized (the cube's 8
corners at `(±0.5, ±0.5, ±0.5)` are already symmetric around the
origin, so the vertex-to-center direction is exactly the vertex
position itself — no separate centroid computation is needed). The
exact eight values, hand-computed and disclosed here, not left to
Implementation's own judgment: each component is `±1/√3 ≈ ±0.57735`,
sign-matched to that corner's own `(±0.5, ±0.5, ±0.5)` position.

**This is explicitly, unambiguously labeled a *smooth-shaded* cube, not
a hard-face one** — this Spec's own Implementation, comments, and any
future consumer (Spec 0019's own eventual Plan, or any later work) must
never describe `minimal_cube`'s own normals as representing distinct,
faceted cube faces. A future, separate decision to split this asset
into 24 non-shared, hard-face-normal vertices remains fully available
(Alternatives Considered) but is not made here — this Spec's own
priority, per its own drafting brief, is preserving the existing
triangle/index list and every existing golden's own pixel content
unchanged (`minimal_mesh.slang` does not read the new normal region at
all, so this choice has **zero** effect on any currently-rendered
pixel), not pre-solving a hard-face-normal need no current consumer
has.

### D6. The existing textured quads — real, winding-verified object-space normals

**Decision:** `textured_quad_left.mesh.txt` and
`textured_quad_right.mesh.txt` each receive the identical uniform
normal, `(0, 0, 1)`, for all four of their own vertices — confirmed by
direct cross-product computation of each quad's own real index winding
(`0 1 2`, then `2 3 0`) against its own real vertex positions (both
flat quads lie exactly in the `z = 0` plane, D6's own Pre-draft
verification), not assumed from the quads' own visual orientation. No
future Lighting shader is required to determine this value — it is
fixed, disclosed, and testable via a hand-computed CPU unit test now
(Testing & Verification Plan), independent of Spec 0019's own eventual
shader work.

### D7. Public API and composition-root exposure — zero new RHI/RenderGraph API, comment-documented byte offset, exhaustive touch-point list

**Decision:** zero new RHI, RenderGraph, or Renderer public API —
`atlantis::rhi::VertexAttributeFormat::Float3` and
`atlantis::shader_system::rhi_integration::VertexAttributeType::Float3`
already exist and are reused verbatim (Pre-draft verification). A
shader that does not declare a normal input remains structurally
unaffected — `toVertexInputLayout()`'s own real, count-only matching
logic and `Device::createPipeline()`'s own per-attribute
(never per-stride) `VkVertexInputAttributeDescription` construction
(ADR-0058's own already-confirmed mechanism, unchanged by this Spec)
together guarantee the new stride region is simply never read by a
pipeline whose own `VertexInputLayout` does not name it.

**`StaticMeshAssetData` remains a wholly untyped `std::vector<std::byte>`
payload — the normal's own byte offset is documented, not exposed as a
new named C++ constant.** This follows the *existing* precedent exactly:
position/color/UV0's own offsets (0/12/24) are today documented only in
`mesh_artifact.h`'s own top-of-file comment, never as a
`kMeshArtifactPositionOffsetBytes`-shaped symbol — only the vertex
*stride* and *schema version* are real, named constants. Adding a new,
asymmetric `kMeshArtifactNormalOffsetBytes` constant while position/
color/UV0 remain comment-only would be an unjustified, inconsistent
special case; this Spec instead extends the *existing* header comment
to document the normal's own offset (32) alongside the other three,
keeping exactly the same public-surface shape this format has always
had.

**Every real composition-root touch point is the freshly-enumerated
list in Pre-draft verification above — six files, not the four
ADR-0058 named two Specs ago** (`runtime_application.cpp`,
`material_demo_fixture.cpp`, `minimal_cube_fixture.cpp`,
`textured_quad_fixture.cpp`, `world_scene_fixture.cpp`,
`world_scene_loaded_fixture.cpp`), each widening its own local `Vertex`
struct by exactly one field (`float normal[3];`) and its own
`MeshVertexAttributeSchema` unchanged otherwise (no shader referenced
by any of these six currently declares a normal input, so no
`.slang` file changes as a consequence of this widening alone).
`tests/runtime/material_realization_gpu_tests.cpp` is confirmed **not**
a required touch point (Pre-draft verification's own disclosed
reasoning) — this Spec's own future Plan states this explicitly rather
than silently omitting the file from consideration.

### D8. Verification boundary — a CPU/GPU-independent data closed loop; no new shader or golden

**Decision:** this Spec's own verification is **CPU/GPU-independent
data-contract proof**, not a rendered one: authoring fixed bytes,
cook/load round-trip, the artifact's own little-endian byte pattern
(a pinned-byte-vector test, matching `mesh_artifact_tests.cpp`'s
existing UV0-era precedent), version 2/1 rejection, missing-normal
rejection (D2's own distinguishable path), zero-vector/NaN/Inf/non-unit
rejection (D3), truncated input, wrong stride, a deterministic
double-cook (`cookStaticMesh()` called twice against the identical
source produces byte-identical artifacts), a real re-cook/re-import
trigger (this format's own existing content-driven re-cook mechanism,
unaffected in kind), and every existing composition root (the six real
touch points, D7) confirmed to still build and run, producing
**byte-for-byte and pixel-for-pixel identical output** against all four
existing goldens.

**No new normal-visualization shader, no new golden, is proposed.**
This Spec adds no rendered capability whatsoever (Non-Goals) — a
byte-level round-trip proof that the correct 44-byte-stride, correctly-
positioned, correctly-signed normal value reaches
`StaticMeshAssetData`'s own vertex bytes is already a complete,
sufficient closure of this Spec's own stated scope. Building a
visualization shader here would be exactly the "expand rendered-output
scope to look complete" pattern this Spec's own drafting brief warns
against — that proof (a real shader reading this Spec's own real
output, producing a real, human-reviewable pixel difference) is
[Spec 0019](0019-lighting-foundation.md)'s own job, using its own new
fixture and golden, once this Spec's own Implementation has merged.

### D9. Boundary with Spec 0019 — restated, symmetric with that Spec's own D1/D9

**Decision:** this Spec provides **only** the normal data contract —
authoring, artifact, load, and a comment-documented byte offset a
composition root can build a schema against. Normal *transform* math,
`Light`, `LitTextured` Material, the frame lighting uniform, the
`lit_textured` shader, and any lighting golden are **entirely** Spec
0019's own domain — this Spec neither implements nor pre-decides any
of them (matching Spec 0019's own D1, which states the identical
boundary from its own side). Only once this Spec's own Implementation
PR has merged does Spec 0019's own Plan gate lift.

### D10. ADR governance — one new Proposed ADR, two Proposed Amendments, no silent rewrite of either Accepted ADR

**Decision:** one new, minimal, single-responsibility ADR,
[ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)
(`Proposed`), recording the normal attribute's own schema/offset/
version/numeric-contract/coordinate-convention decision (D1–D6 above).
Both [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
and [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md),
confirmed by direct re-reading (Pre-draft verification) to each carry
their own closed, `Accepted` fixed-schema declaration, each receive
their own dated, separately-labeled Proposed Amendment (Architectural
Impact, below) — **neither ADR's own original `Accepted` Decision text
is rewritten by so much as one word**; each amendment is appended,
following ADR-0045's own existing "Accepted Amendment — 2026-08-25"
section as this repository's own direct, in-file precedent for the
amendment's own shape. All three documents (the new ADR-0063 plus both
amendments) await Human Review together, in the same pass as this
Spec's own approval — matching ADR-0058/ADR-0045's own precedent of
landing together for Spec 0017's own approval exactly.

## Architectural Impact

- **New:** [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)
  (`Proposed`) — the normal attribute's own schema, byte offset, version
  bump, numeric contract, and coordinate convention (D1–D6).
- **Amended (Proposed, not yet Accepted):** [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  own format-scope sentence, narrowed by its 2026-08-25 Amendment to
  "position, color, and UV0," gains a second, separately-dated
  Proposed Amendment widening it to "position, color, UV0, and normal."
- **Amended (Proposed, not yet Accepted):** [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
  own Decision, which currently closes the vertex layout at exactly
  three attributes ("the one, single static mesh vertex layout —
  position + color + UV0"), gains a Proposed Amendment admitting the
  fourth.

Neither amendment alters, narrows, or reinterprets either ADR's own
original `Accepted` Decision text as written — each amendment states
only the additional, narrower scope this Spec's own Decision requires,
following this repository's own established "an Accepted ADR's Decision
is never silently rewritten" discipline exactly.

## Alternatives Considered

- **A per-asset optional/variant vertex layout, to let normal-less
  meshes stay narrower.** Rejected — identical reasoning to ADR-0058's
  own rejection of the same alternative for UV0: no second concrete
  vertex-schema need exists anywhere in this codebase to justify a real,
  new schema-selection mechanism; `VertexAttribute`/`VertexInputLayout`/
  `MeshVertexAttributeSchema` already provide the general RHI-level
  mechanism any such future system would sit on top of.
- **Automatic (cooker-computed) normal generation** — face-average,
  angle-weighted, or smoothing-group-based. Rejected on the merits
  (Spec 0019's own D1 already rejected this class of approach for the
  identical reason): a tool-computed normal is not a real,
  asset-authored value, and would undermine this Spec's own entire
  purpose of proving a genuine scene-driven lighting closed loop later.
  D5's own smooth cube normals are a real, disclosed, hand-computed
  authoring choice — not an automated cooker feature.
- **Cooker-side auto-normalization of a non-unit authored normal.**
  Rejected — this Spec's own drafting brief's explicit instruction, and
  a real correctness concern in its own right: rescaling an author's own
  input is a floating-point computation whose exact result could differ
  across platforms/compilers, exactly the kind of format-portability
  risk [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  own unconditional-little-endian, `std::bit_cast`-only discipline
  already exists to avoid for every other field.
- **Splitting `minimal_cube` into 24 non-shared, hard-face-normal
  vertices, instead of smooth vertex-averaged normals.** A real,
  available alternative, not foreclosed by this Spec — would give
  `minimal_cube` genuinely faceted shading once a future Lit shader
  reads it, at the cost of changing an existing, golden-backed asset's
  own vertex/index structure (even though the resulting *rendered*
  pixels are provably unaffected today, since no current shader reads
  the normal region at all). Deferred in favor of the smaller, zero-
  structural-change option, per this Spec's own explicit priority
  (preserve existing triangle/index/rasterization and golden content
  unchanged) — revisit if a future Lit-shaded `minimal_cube` render is
  ever actually wanted with hard-face shading.
- **A placeholder/zero-magnitude normal on `minimal_cube`, explicitly
  marked "not for lighting."** Rejected: would fail this Spec's own new
  unit-length numeric contract (D3) by construction, and — more
  fundamentally — would ship a real, cook-time-accepted mesh artifact
  whose own normal data is known-meaningless, contradicting this Spec's
  own basic purpose (a real, usable normal attribute) for the one asset
  most likely to be reused as a placeholder lit-scene test subject
  later.
- **A `kMeshArtifactNormalOffsetBytes`-shaped new named constant**
  (D7). Rejected as inconsistent with this format's own existing,
  comment-only documentation of the other three attributes' offsets —
  revisit only if a future Spec decides to expose *all four* offsets as
  named constants together, not as a one-off exception for normal alone.

## Testing & Verification Plan

- **CPU-level, GPU-independent (the primary proof, per D8):**
  `parseMeshSource()`/`serializeMeshSource()` round-trip with real
  normal values; version-3 acceptance, version-2/version-1 rejection
  (`UnknownSourceVersion`); 11-field acceptance, 8-field rejection
  (`CountMismatch`, the "missing normal, correct version" case, D2);
  `NonFiniteFloat` extended to the three new floats; `NonUnitNormal`
  for a zero vector, a `(1,1,1)`-shaped unnormalized vector, and a
  value just outside `[0.99, 1.01]`; a pinned-expected-byte-vector test
  for `encodeMeshArtifact()` against a known small mesh (matching
  `mesh_artifact_tests.cpp`'s own existing UV0-era pinning-test
  precedent); `decodeMeshArtifact()` rejecting schema version 1/2,
  wrong stride (32 or any value other than 44), truncated input, and
  the identical `NonUnitNormal` cases, independently of the cook-time
  checks; a deterministic double-cook (`cookStaticMesh()` invoked twice
  against identical source text produces byte-identical artifacts); a
  real content-driven re-cook trigger, unaffected in mechanism.
- **Hand-computed math tests (D6):** a CPU-only unit test asserting the
  exact `(0, 0, 1)` face-normal result for both existing textured
  quads' own real index winding, independent of any shader.
- **Composition-root build/run verification:** all six real touch
  points (D7) confirmed to compile against the widened 44-byte
  `Vertex` struct and successfully build/run their own existing test
  suite.
- **The four existing goldens** (`minimal_cube`, `world_scene`,
  `textured_quad`, `material_demo`) confirmed byte-for-byte (PNG/
  sidecar) and pixel-for-pixel unchanged — no shader this Spec touches
  or that any of the six composition roots use declares a normal input,
  so this Spec's own widening is provably inert for every currently-
  rendered pixel.
- **No new golden, no new shader** — see D8's own full reasoning.

## Risks & Open Questions

- **The exact, final per-file edit list for the nine embedded-mesh-
  source test files** (Pre-draft verification's own vertex-line counts)
  is a real, disclosed Plan-time enumeration task — this Spec fixes the
  grammar/byte contract each edit must produce, not each individual
  file's own new literal values.
- **Whether a future Spec ever wants `minimal_cube`'s own hard-face
  normals** (Alternatives Considered) is explicitly left open, not
  decided here — this Spec's own D5 choice does not foreclose it.

## Out of Scope / Future Work

Repeating this Spec's own Non-Goals for visibility, plus this Spec's
own direct successor:

- **Lighting Foundation ([Spec 0019](0019-lighting-foundation.md),
  `Approved`, `Blocked by Spec 0020 implementation`)** — this Spec's
  own direct, named successor; its Plan may begin only once this
  Spec's own Implementation PR merges.
- Tangent/bitangent attributes, normal mapping, and any hard-edge/
  smoothing-group importer — independent, unblocked future work, not
  designed or scaffolded here.
- A hard-face-normal `minimal_cube` variant (Alternatives Considered) —
  real, available, not decided here.
- PBR, shadows, IBL, post-processing — each remains its own,
  independent, unblocked future Spec, named already by Spec 0019's own
  D12; this Spec adds nothing toward any of them.
