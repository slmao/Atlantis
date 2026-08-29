# Spec: Mesh Normal Attribute Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-08-29
- **Related Plan(s):** [plans/0020-mesh-normal-attribute-foundation.md](../plans/0020-mesh-normal-attribute-foundation.md)
  (`Approved / Ready for Implementation`, Human Review Approval recorded
  2026-08-29). Implementation is code-complete on
  [PR #93](https://github.com/slmao/Atlantis/pull/93) — **OPEN, not yet
  merged.** **This Spec's own approval authorized drafting Plan 0020
  only — not Implementation.** Once Plan 0020's own Implementation PR
  merges, Spec 0019's own Plan-drafting gate lifts — see
  [Spec 0019](0019-lighting-foundation.md)'s own D1 and header, and this
  Spec's own D9, both of which remain completely unchanged: reaching
  `Approved` on this Spec, or on Plan 0020 itself, was not, and was
  never claimed to be, sufficient to unblock Plan 0019 — only this PR's
  own merge is.
- **Related ADR(s):** [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md) (`Accepted`), plus Accepted Amendments to [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md) and [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
- **Human Review Approval (2026-08-29):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-29, accepting this document's
  own "Decisions for Human Review" section in full, including
  `version 3`, the fixed 44-byte layout, the object-space normal
  convention, the finite/non-zero/unit-length-squared numeric contract,
  `minimal_cube`'s smooth normals, both quads' `(0, 0, 1)` normals, old-
  version rejection, no automatic generation/normalization anywhere,
  and every stated Non-Goal — per this document's own final,
  corrected recommendations produced during one final, targeted review
  round (below), and accepting [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)
  (`Proposed` → `Accepted`) plus the Proposed Amendments to
  [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)/[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (both → `Accepted Amendment`) in the same pass. **This approval
  authorizes drafting Plan 0020 only. It does not authorize
  Implementation.** [Spec 0019](0019-lighting-foundation.md) remains
  Plan-blocked until this Spec's own Implementation PR has merged —
  unchanged, unshortened by this approval.

## Final Review Round (2026-08-29) — closed findings, recorded before approval

A single, targeted final review round examined ten specific areas of
this Spec's own numeric determinism, error-domain precision, and
layout-contract testability. Every item was closed at the Spec level;
two produced a real, disclosed *design change* from this Spec's own
first draft, not merely clarified wording — recorded here so the
change is visible, not silently folded in:

1. **The normal length check is restated on length-*squared*, in
   double precision, never `std::sqrt` — a real design change, not a
   wording fix.** The first draft stated its `[0.99, 1.01]` tolerance
   on `length` directly, implying a `std::sqrt` call whose own
   preceding `x·x + y·y + z·z` sum has a real, if small, ISA/compiler-
   dependent rounding-path variability (fused-multiply-add availability
   differs between this codebase's own two Phase 1 targets, x86-64
   Windows and ARM/AArch64 Android) — a genuine determinism risk for a
   value compared against a fixed boundary, never actually addressed by
   the first draft's own text. Corrected: the check is now stated and
   computed on `length`-*squared* (`[0.9801, 1.0201]`, the identical
   real tolerance restated on the squared quantity), computed via
   explicit double-precision arithmetic after an exact float-to-double
   promotion of each already-finite-checked component — `std::sqrt`
   appears nowhere in this Spec's own numeric contract anymore (D3).
2. **The layout's own four per-attribute byte offsets become real,
   named, `constexpr` constants — a real reversal, not a wording fix.**
   The first draft kept every offset (including the new normal one)
   documented only in `mesh_artifact.h`'s own prose comment, reasoning
   from today's own precedent (only stride/schema-version are named).
   This round's own review correctly identified that a comment is not
   compile-time-checkable — a future consumer could still hand-write
   the wrong magic offset with nothing to catch the mistake. Corrected:
   `kMeshArtifactPositionOffsetBytes`/`ColorOffsetBytes`/
   `Uv0OffsetBytes`/`NormalOffsetBytes` are added to `mesh_artifact.h`
   as real, public constants — all four attributes, not normal alone,
   closing the asymmetry a normal-only constant would have created —
   and each of the six real composition-root touch points gains four
   `static_assert`s tying its own local `Vertex` struct to them, a
   compiler-enforced synchronization guarantee (D7).

Eight further findings closed with real evidence, none requiring a
further design change: the complete six-scenario error-domain matrix
confirmed against `errors.h`'s own real, current enumerator lists, with
a direct-search-confirmed finding that neither `SourceParseError` nor
`ArtifactDecodeError` is consumed by any exhaustive `switch` anywhere in
this codebase today, so this Spec's own two new `NonUnitNormal`
enumerators require no C4062 protection (D2); explicit required test
cases for the length-squared boundary, `-0.0`, an extremely small
non-zero vector, and `NaN`/`Inf` ordering (D3); the cube's own smooth
normals pinned to an exact, `from_chars`-recoverable nine-significant-
digit decimal literal (`0.577350269`) rather than an approximate value,
with the resulting bit pattern locked by a pinned-byte test once a real
build exists, never hand-derived in this Spec's own text (D5); both
quads' `(0, 0, 1)` normal reconfirmed via **both** triangles of **each**
quad independently (four total computations, all agreeing), not
inferred from one triangle (D6); a fresh, explicit re-confirmation that
no artifact-distribution mechanism exists in this codebase today (not
silently inherited from ADR-0045's 2026-08-19 statement) and that all
three real mesh assets are confirmed unconditionally declared, re-
cookable under `ATLANTIS_BUILD_TESTS=OFF` (Requirements); confirmed
`specs/README.md`'s own Spec 0019 row already states the real
Implementation-merged governance gate, never loose "after `Approved`"
language; confirmed no other `Accepted` ADR beyond ADR-0045/ADR-0058
closes the mesh vertex schema (a repository-wide search of every ADR
mentioning "position and color"/vertex layout found only a contextual,
non-authoritative reference in ADR-0059); and the Testing & Verification
Plan expanded to an explicit, complete checklist matching every item
this round's own review named.

No unresolvable architectural conflict was found. Every finding above
was closed with a real, evidenced fix within this Spec's own existing
scope.

## Human Review Correction — 2026-08-29

**A mechanical, non-design correction, found during [Plan 0020](../plans/0020-mesh-normal-attribute-foundation.md)'s
own Plan Review, directed by Human Review to be corrected here rather
than silently reinterpreted at Plan level.** This Spec's own Pre-draft
verification (above) states "**Nine** test files carry embedded,
literal mesh-source text," then lists exactly **eight**
(`tests/asset_system/load_tests.cpp`,
`tests/asset_system/mesh_source_tests.cpp`,
`tests/asset_system/mesh_uv_round_trip_tests.cpp`,
`tests/runtime/material_realization_gpu_tests.cpp`,
`tests/runtime/scene_load_tests.cpp`,
`tests/runtime/scene_manifest_tests.cpp`,
`tests/tools/asset_cooker/cook_command_tests.cpp`,
`tests/tools/asset_cooker/cooker_determinism_tests.cpp`). The word
"nine" was correct in total count, wrong in category: a
repository-wide search for the string `atlantis_static_mesh_source_version`
(this Spec's own original search term) genuinely returns only these
eight files — it cannot and does not find
`tests/asset_system/mesh_artifact_tests.cpp`, which is a real, necessary
ninth touch point of the identical kind (a test literal directly tied to
`MeshSourceVertex`'s own eight-then-eleven-field shape, requiring the
identical class of edit) but reaches that shape by constructing a
`MeshSourceVertex` via C++ aggregate initialization
(`{1.0f, 2.0f, ..., 8.0f}`) rather than by embedding parseable mesh-
source *text* — so it is invisible to a text-string search for the
version-line marker, while being every bit as real a required edit as
the other eight.

**Corrected statement, replacing "Nine test files carry embedded,
literal mesh-source text" above:** this Spec's own real touch-point
count is **nine test files in total, in two distinct categories** —
**eight** files carrying embedded, literal mesh-source *text*
(the original list above, unchanged, still accurate as a list) plus
**one** file (`tests/asset_system/mesh_artifact_tests.cpp`) that
constructs a `MeshSourceVertex` directly via aggregate initialization,
never through parsed text, and additionally hardcodes byte offsets
against the artifact's own binary layout (its own pinned-byte-vector
test and six byte-corruption decode-error cases). Both categories
require the identical class of edit (widening from eight fields/values
to eleven) for the identical reason (the one, single vertex layout
this Spec itself widens) — the distinction is only in *how* each test
file expresses its own mesh-vertex data, not in whether it must change.

This correction **does not alter, narrow, or reinterpret** this Spec's
own Decisions for Human Review (D1–D10), its own `Approved` status, or
any part of its own Human Review Approval recorded above — it corrects
one summary word and appends the one file category that word's own
imprecision had left uncounted-by-name. [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)
and the [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)/[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
Accepted Amendments are unaffected — none of the three names or
depends on this specific file count. [Plan 0020](../plans/0020-mesh-normal-attribute-foundation.md)'s
own traceability and Verification Checklist use this corrected,
two-category accounting throughout.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — directed and
approved as part of Plan 0020's own Human Review Approval, 2026-08-29.

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
  floats per vertex, unchanged in kind); the normal vector's own
  length-*squared* (computed in double precision, never via
  `std::sqrt`) must fall within `[0.9801, 1.0201]`, checked
  independently at cook time and decode time — a new
  `SourceParseError::NonUnitNormal` / `ArtifactDecodeError::NonUnitNormal`
  pair (genuinely new failure kinds; no existing enumerator covers
  "correct magnitude"). Neither the cooker nor the decoder ever rewrites
  a normal's own authored bit pattern to force it onto the unit sphere —
  a non-conforming value is rejected outright, never silently corrected.
- **No migration mechanism, no back-compat reader, no silent default**
  — identical in kind to ADR-0058's own Decision item 3, grounded in the
  identical reasoning: "there is no independently-distributed copy of a
  mesh artifact anywhere — every artifact is a build output,
  deterministically regenerated from its own checked-in authoring
  source at build time" ([ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)).
  **Re-confirmed fresh at this Spec's own drafting time, not silently
  inherited from ADR-0045's 2026-08-19 statement:** no Android (or any
  other) build/packaging pipeline exists anywhere in this repository
  today that distributes a *built* mesh artifact independently of its
  own checked-in source — confirmed by direct inspection, current as of
  this Spec's own drafting, not assumed to remain true forever. Every
  currently-authored mesh source file is re-authored with real,
  disclosed normal values as part of this Spec's own Implementation;
  none is left on the old 8-field grammar, and none is silently
  defaulted to `(0, 0, 0)` (which would also fail the new unit-length
  check, structurally preventing this particular silent default).
  **Confirmed re-cookable under `ATLANTIS_BUILD_TESTS=OFF`:** all three
  real mesh assets' own `atlantis_add_static_mesh_asset()` declarations
  (`assets/CMakeLists.txt`) are unconditional — `minimal_cube` and both
  `textured_quad_left`/`textured_quad_right` are declared outside any
  `ATLANTIS_BUILD_TESTS` guard (the latter two's own declaration
  comment states this explicitly: "Declared here, unconditionally
  (like `minimal_cube`/`world_scene` above)") — so a fresh
  `-DATLANTIS_BUILD_TESTS=OFF` configure/build re-cooks all three
  assets against this Spec's own new version-3 grammar with no
  production-tree gap.
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
  normal length-squared check in [0.9801, 1.0201], double precision,
  no sqrt (D3) -- rejects outright, never normalizes or generates
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

### D2. Authoring/artifact version and the complete error-domain matrix — six independently-distinguishable failure kinds, confirmed against real enumerators, zero new C4062 exposure (expanded this round — see the Final Review Round note above)

**Version decision, unchanged:** authoring source version `2 → 3`
(`atlantis_static_mesh_source_version: 3`); artifact schema version
`2 → 3`. Both old versions rejected outright — no dual-version reader,
no migration mechanism, identical in kind to ADR-0058's own version-1-
to-2 precedent.

**The complete error-domain matrix, six named scenarios, confirmed
each maps to its own distinguishable rejection path against
`errors.h`'s own real, current enumerator lists (read in full this
round) — none conflated with another:**

| # | Scenario | Layer | Enumerator (real, confirmed) |
|---|---|---|---|
| 1 | An 8-field `vertex:` line under the old `atlantis_static_mesh_source_version: 2` marker | cook (`parseMeshSource()`) | `SourceParseError::UnknownSourceVersion` — the version-line check runs **first**, before any per-vertex field is ever read, so this path is reached before field count is even examined |
| 2 | A correctly-versioned-3 `vertex:` line carrying only 10 fields (one normal component genuinely omitted) | cook | `SourceParseError::CountMismatch` — the existing per-line field-count check (`fields.size() != 11`), reached only *after* the version check already passed |
| 3 | A finite-count-correct line whose normal component is `NaN`/`Inf` | cook | `SourceParseError::NonFiniteFloat` — the existing per-float finiteness check, extended in kind from 8 to 11 floats, evaluated *before* any magnitude check (D3) |
| 4 | A finite, correctly-counted normal whose own length-squared falls outside `[0.9801, 1.0201]` | cook | `SourceParseError::NonUnitNormal` — **new** (below); confirmed no existing `SourceParseError` enumerator (`UnknownSourceVersion, MissingField, FieldOrderMismatch, MalformedNumber, NonFiniteFloat, CountMismatch, IndexOutOfRange, IndexCountNotMultipleOfThree, VertexCountOutOfRange, TrailingContent` — the complete, current list) represents "finite but wrong magnitude"; deliberately **not** `NonFiniteFloat` (that enumerator's own name and every existing use of it means non-finite, never finite-but-out-of-range) |
| 5 | An artifact header declaring the wrong schema version or the wrong stride | decode (`decodeMeshArtifact()`) | `ArtifactDecodeError::UnknownSchemaVersion` / `UnsupportedVertexStride` — both already existing, unchanged in kind |
| 6 | A hand-corrupted artifact byte buffer whose normal bytes decode to a finite value with an out-of-tolerance length-squared | decode | `ArtifactDecodeError::NonUnitNormal` — **new** (below), the decode-time twin of scenario 4, independently re-derived from the artifact's own bytes, never trusting a well-formed cooker (matching every other artifact-layer check's own "never trust the cooker" discipline) |

**New enumerators, exactly two, added where — and only where — no
existing one fits:**

```cpp
enum class SourceParseError {
  // ... existing enumerators, unchanged ...
  NonUnitNormal,  // new
};

enum class ArtifactDecodeError {
  // ... existing enumerators, unchanged ...
  NonUnitNormal,  // new
};
```

**C4062 requirement, confirmed by direct search, not assumed:** neither
`SourceParseError` nor `ArtifactDecodeError` is consumed by any
exhaustive `switch` statement, or any `toString()`-shaped function,
anywhere in `src/` or `tests/` today (confirmed by a repository-wide
grep for `switch.*SourceParseError`/`switch.*ArtifactDecodeError` and
for a `toString(SourceParseError`/`toString(ArtifactDecodeError`
signature — zero matches for either). Every existing consumer only ever
compares a returned error value with `==` in a test assertion. **This
Spec's own two new enumerators therefore require no `/w14062` C4062
positive/negative probe** — there is no exhaustive switch anywhere that
a missing case could silently fall through. This is stated as a
confirmed finding, not a gap: unlike `WorldError`/`RuntimeInitError`-
shaped enums elsewhere in this codebase (which *do* have a real
`toString()` switch and *do* need C4062 protection when widened), these
two error types simply have no such consumer today.

### D3. Normal numeric contract — required unit length-squared, checked in double precision, never auto-corrected (revised this round — see the Final Review Round note above)

**Decision (revised from this Spec's own first draft, which stated the
tolerance on `length`, implying `std::sqrt`):** each normal component
must be finite first (the existing `NonFiniteFloat` check, extended in
kind from 8 to 11 floats — no new enumerator, evaluated before any
magnitude check, matching the existing per-float-then-per-vertex
validation order). **The check is then stated and computed on
length-*squared*, never on `length` itself, and never via
`std::sqrt`:**

```cpp
// x, y, z are the three already-finite-checked normal components
// (float). Promoted to double before every arithmetic operation --
// float-to-double promotion is always exact (zero rounding, unlike
// float-to-float multiplication), so this computation carries no
// platform-dependent rounding risk from the promotion step itself,
// and IEEE 754 double arithmetic is required to be correctly rounded,
// per operation, on every conforming target (x86-64 Windows today;
// ARM/AArch64 Android, Phase 1's own other primary target, tomorrow)
// -- eliminating the float-level concern (compiler/ISA-dependent FMA
// fusion changing which single- or double-rounding path a plain
// float `x*x + y*y + z*z` takes) this Spec's own first draft did not
// address. No std::sqrt anywhere in this check.
const double lengthSquared =
    static_cast<double>(x) * static_cast<double>(x) +
    static_cast<double>(y) * static_cast<double>(y) +
    static_cast<double>(z) * static_cast<double>(z);
const bool unitLength = lengthSquared >= 0.9801 && lengthSquared <= 1.0201;
```

**Tolerance, exact:** `lengthSquared ∈ [0.9801, 1.0201]` inclusive —
`0.99²` and `1.01²` respectively, the identical `±1%`-of-unit-length
tolerance this Spec's own first draft already chose and justified
(tolerates ordinary hand-typed decimal precision, e.g. `0.577350269`
for a cube-corner direction, D5, while rejecting a genuinely
unnormalized author error, e.g. `1 1 1`, `lengthSquared = 3.0`, far
outside tolerance) — restated here on the squared quantity, not a new
tolerance decision.

**Checked independently at cook time (`parseMeshSource()`) and decode
time (`decodeMeshArtifact()`), using the identical formula and
tolerance — the decoder never trusts the cooker's own result, exactly
matching this format's own established discipline for every other
independently-re-validated condition** (schema version, stride,
finiteness). A new, shared-in-kind pair of enumerators
(`SourceParseError::NonUnitNormal` / `ArtifactDecodeError::NonUnitNormal`
— see D2's own complete error-domain matrix) — genuinely new failure
kinds; no existing enumerator covers "finite but wrong magnitude."
**Neither layer ever rewrites, rescales,
or auto-normalizes a normal's own authored bit pattern** — a value
outside tolerance is rejected outright, matching this Spec's own
explicit drafting-brief instruction to avoid any transformation of
author input.

**Required test cases, explicit, not left to Implementation's own
judgment:**

- **Boundary, both sides:** `lengthSquared` exactly `0.9801` and
  exactly `1.0201` are both **accepted** (inclusive bounds); a value
  the smallest representable step below `0.9801`, or above `1.0201`,
  is **rejected**.
- **`-0.0` on one or more components:** `std::isfinite(-0.0)` is
  `true` (accepted by the finiteness check), and `(-0.0)² == +0.0`
  exactly under IEEE 754 — a normal with a `-0.0` component behaves
  identically, for this check's own purposes, to the same normal with
  `+0.0` in that position; a test confirms this explicitly rather than
  leaving it as an unverified assumption about IEEE 754 semantics.
- **Extremely small non-zero components** (e.g. all three components
  `~1e-20`): finite, but `lengthSquared` is effectively `0`, far below
  `0.9801` — rejected by this same check; no separate "near-zero"
  special case exists or is needed (this Spec's own first draft already
  made this claim; this revision keeps it true under the new
  length-squared formulation).
- **`NaN`/`Inf` on any component:** rejected by the existing, extended
  `NonFiniteFloat` check, confirmed to run **before** the length-squared
  check ever executes (so a `NaN` component never reaches the
  length-squared arithmetic at all, avoiding any question of `NaN`
  propagation through the tolerance comparison).

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

### D5. The existing shared-vertex cube — smooth, deterministic, honestly-labeled normals, an exact `from_chars`-recoverable decimal literal; the mesh itself untouched

**Decision:** `minimal_cube.mesh.txt` keeps its own existing 8 shared
vertices, 12 triangles, and 36-index list, and its own existing
position/color/UV0 values, completely unchanged — only a fourth,
appended normal field is added per `vertex:` line. This Spec adds
**smooth (vertex-averaged) normals**, not hard-face ones, computed as
each vertex's own position, normalized (the cube's 8 corners at
`(±0.5, ±0.5, ±0.5)` are already symmetric around the origin, so the
vertex-to-center direction is exactly the vertex position itself — no
separate centroid computation is needed, and none is performed by the
cooker at build time — see below).

**The exact decimal literal, chosen so `std::from_chars` (this format's
own existing float-parsing routine, `std::chars_format::general`)
recovers a specific, unambiguous `binary32` value, not left to
Implementation's own rounding choice:** `0.577350269` — nine
significant decimal digits, more than `binary32`'s own ~7.2-decimal-
digit precision needs to pin the *nearest representable* `float` to
`1/√3` unambiguously (`1/√3 = 0.5773502691896258…`, rounded to nine
decimal places). Each of the eight corners' three normal components is
this exact literal, individually sign-matched to that corner's own
`(±0.5, ±0.5, ±0.5)` position component (e.g. the corner at
`(-0.5, -0.5, -0.5)` gets normal `-0.577350269 -0.577350269 -0.577350269`;
the corner at `(0.5, -0.5, -0.5)` gets `0.577350269 -0.577350269
-0.577350269`; and so on for all eight, by the identical sign-copy
rule) — a purely mechanical, Implementation-time transcription this
Spec's own text already fully determines, not a judgment call.

**The exact resulting little-endian bit pattern is what a pinned-byte
artifact test locks, once a real build exists — not hand-derived in
this Spec's own text.** This matches `mesh_artifact_tests.cpp`'s own
already-established UV0-era precedent exactly: that existing test pins
whatever `encodeMeshArtifact()` actually produces for a real, disclosed
source value, rather than a hex literal hand-computed in a Spec
document — the same discipline applies here, now for eleven floats per
vertex instead of eight.

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

**The cooker never computes this (or any) normal from position at
build time — restated as a firm constraint on this Decision, not left
implicit:** the eight `0.577350269`-based values above are written
directly into `minimal_cube.mesh.txt`'s own checked-in authoring-source
text, exactly like every other field on that line — `parseMeshSource()`
reads them as plain, disclosed literals, identically to how it already
reads position/color/UV0, with no vertex-to-center or any other
geometric computation anywhere in `mesh_source.cpp`/`mesh_artifact.cpp`.
This Spec's own D4 (no cooker-side generation of any kind) and this
sentence are the same rule, stated twice for emphasis at the one asset
where it would be easiest to "helpfully" implement as a convenience.

### D6. The existing textured quads — real, per-triangle-verified, winding-consistent object-space normals

**Decision:** `textured_quad_left.mesh.txt` and
`textured_quad_right.mesh.txt` each receive the identical uniform
normal, `(0, 0, 1)`, for all four of their own vertices — confirmed by
direct cross-product computation of **both** triangles of **each**
quad independently (not inferred from one triangle and assumed to hold
for the other), against each quad's own real index winding and real
vertex positions (D6's own Pre-draft verification):

- **Left quad, triangle `0-1-2`** (`v0=(-0.9,-0.5,0)`,
  `v1=(-0.1,-0.5,0)`, `v2=(-0.1,0.5,0)`):
  `cross(v1-v0, v2-v0) = cross((0.8,0,0), (0.8,1.0,0)) = (0, 0, 0.8)`
  → `(0, 0, +1)` normalized.
- **Left quad, triangle `2-3-0`** (`v2=(-0.1,0.5,0)`, `v3=(-0.9,0.5,0)`,
  `v0=(-0.9,-0.5,0)`): `cross(v3-v2, v0-v2) = cross((-0.8,0,0),
  (-0.8,-1.0,0)) = (0, 0, 0.8)` → `(0, 0, +1)` normalized, **confirmed
  consistent** with the first triangle, not merely assumed from
  planarity.
- **Right quad:** an identical shape translated by `+1.0` on `X` only
  — since the cross product depends solely on edge *vectors*, never
  absolute position, translation invariance makes both of the right
  quad's own triangles produce the identical `(0, 0, +1)` result by the
  same arithmetic, without needing to re-run the raw numbers a second
  time.

**Had these four independent computations disagreed** (a genuinely
non-planar quad, or an inconsistent winding across its own two
triangles), this Decision would have surfaced that as a real problem to
resolve — a bad-quality mesh, or a Spec-level modeling question — rather
than silently writing one "close enough" uniform value; they agree, so
a single uniform per-quad normal is the direct, verified consequence of
this real geometry, not an assumption. A CPU-only unit test asserts
each of these four triangle-level computations independently
(Testing & Verification Plan) — no future Lighting shader is required
to determine or confirm this value.

### D7. Public API and composition-root exposure — zero new RHI/RenderGraph API; layout offsets become real, named, testable constants (reversed this round — see the Final Review Round note above)

**Decision, RHI/RenderGraph/Renderer surface unchanged:** zero new RHI,
RenderGraph, or Renderer public API — `atlantis::rhi::VertexAttributeFormat::Float3`
and `atlantis::shader_system::rhi_integration::VertexAttributeType::Float3`
already exist and are reused verbatim (Pre-draft verification). A
shader that does not declare a normal input remains structurally
unaffected — `toVertexInputLayout()`'s own real, count-only matching
logic and `Device::createPipeline()`'s own per-attribute
(never per-stride) `VkVertexInputAttributeDescription` construction
(ADR-0058's own already-confirmed mechanism, unchanged by this Spec)
together guarantee the new stride region is simply never read by a
pipeline whose own `VertexInputLayout` does not name it.

**Reversed from this Spec's own first draft: the layout's own four
per-attribute byte offsets become real, named, public
`constexpr` constants in `mesh_artifact.h` — not comment-only.**
This Spec's own first draft kept offsets comment-only, reasoning from
today's precedent (only stride and schema version are named; offsets
are prose). This round's own review correctly identified a real gap
that precedent papers over: a comment is not a compile-time-checkable
single source of truth — a future composition root (or this Spec's own
six real touch points, re-widened) could still hand-write the wrong
magic offset with nothing to catch the mistake at build time, exactly
the risk this Spec's own numeric-contract rigor (D3) exists to avoid
for the *value* layer. **Decision:** add

```cpp
inline constexpr std::size_t kMeshArtifactPositionOffsetBytes = 0;
inline constexpr std::size_t kMeshArtifactColorOffsetBytes = 12;
inline constexpr std::size_t kMeshArtifactUv0OffsetBytes = 24;
inline constexpr std::size_t kMeshArtifactNormalOffsetBytes = 32;
```

to `mesh_artifact.h`, alongside the existing `kMeshArtifactVertexStrideBytes`/
`kMeshArtifactSchemaVersion` — **all four attributes**, not normal
alone, closing the asymmetry a normal-only constant would have created
(every existing offset becomes an equally real symbol, not merely the
new one). `StaticMeshAssetData` itself stays exactly as it is today (a
wholly untyped `std::vector<std::byte>` payload, no C++ change to that
type) — these four constants live in `mesh_artifact.h`, the format's
own existing home for `kMeshArtifactVertexStrideBytes`, not on
`StaticMeshAssetData`. This introduces no general vertex-schema system
(still explicitly rejected, Non-Goals) — four `constexpr std::size_t`
values are primitive data, not a mechanism.

**The single authoritative source and the exact mechanical
synchronization each of the six real composition roots uses, stated
explicitly (closing this round's own "how do six consumers stay in
sync" question):** `mesh_artifact.h`'s own four constants above are the
one, single authoritative source. Each of the six composition roots
(below) keeps its own local `Vertex` struct exactly as ADR-0058's own
precedent already established (a composition root does not dynamically
read `StaticMeshAssetData::vertexStrideBytes()` at runtime to build its
`VertexInputLayout`) — but each now additionally carries four
`static_assert`s, checked at every compile, tying its own local
`offsetof()` values to Asset System's own real constants:

```cpp
static_assert(offsetof(Vertex, position) == atlantis::asset_system::kMeshArtifactPositionOffsetBytes);
static_assert(offsetof(Vertex, color) == atlantis::asset_system::kMeshArtifactColorOffsetBytes);
static_assert(offsetof(Vertex, uv) == atlantis::asset_system::kMeshArtifactUv0OffsetBytes);
static_assert(offsetof(Vertex, normal) == atlantis::asset_system::kMeshArtifactNormalOffsetBytes);
```

A future change to any offset that is not mirrored in a given
composition root's own local struct now fails that root's own build
immediately, at the exact point of the mismatch — a real, compiler-
enforced guarantee, not a documentation convention a future consumer
could silently drift from. This uses only standard `constexpr`/
`static_assert`/`offsetof` — zero new RHI/RenderGraph/Renderer/Asset
System *runtime* API, matching this Decision's own first sentence.

**Every real composition-root touch point is the freshly-enumerated
list in Pre-draft verification above — six files, not the four
ADR-0058 named two Specs ago** (`runtime_application.cpp`,
`material_demo_fixture.cpp`, `minimal_cube_fixture.cpp`,
`textured_quad_fixture.cpp`, `world_scene_fixture.cpp`,
`world_scene_loaded_fixture.cpp`), each widening its own local `Vertex`
struct by exactly one field (`float normal[3];`), adding the four
`static_assert`s above, with its own `MeshVertexAttributeSchema`
unchanged otherwise (no shader referenced by any of these six currently
declares a normal input, so no `.slang` file changes as a consequence
of this widening alone). `tests/runtime/material_realization_gpu_tests.cpp`
is confirmed **not** a required touch point (Pre-draft verification's
own disclosed reasoning) — this Spec's own future Plan states this
explicitly rather than silently omitting the file from consideration.

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
authoring, artifact, load, and a real, named byte-offset constant
(D7) a composition root can build a schema against. Normal *transform* math,
`Light`, `LitTextured` Material, the frame lighting uniform, the
`lit_textured` shader, and any lighting golden are **entirely** Spec
0019's own domain — this Spec neither implements nor pre-decides any
of them (matching Spec 0019's own D1, which states the identical
boundary from its own side). Only once this Spec's own Implementation
PR has merged does Spec 0019's own Plan gate lift.

### D10. ADR governance — one new ADR, two amendments, no silent rewrite of either Accepted ADR, and no other Accepted ADR left closing the schema unamended

**Decision:** one new, minimal, single-responsibility ADR,
[ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)
(`Accepted`), recording the normal attribute's own schema/offset/
version/numeric-contract/coordinate-convention decision (D1–D6 above).
Both [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
and [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md),
confirmed by direct re-reading (Pre-draft verification) to each carry
their own closed, `Accepted` fixed-schema declaration, each receive
their own dated, separately-labeled Accepted Amendment (Architectural
Impact, below) — **neither ADR's own original `Accepted` Decision text
is rewritten by so much as one word**; each amendment is appended,
following ADR-0045's own existing "Accepted Amendment — 2026-08-25"
section as this repository's own direct, in-file precedent for the
amendment's own shape. All three documents (the new ADR-0063 plus both
amendments) were accepted together, in the same Human Review pass as
this Spec's own approval — matching ADR-0058/ADR-0045's own precedent
of landing together for Spec 0017's own approval exactly. **Confirmed
by a repository-wide search of every ADR mentioning "position and
color"/"position, color"/"vertex layout"/"static mesh vertex":** no
`Accepted` ADR beyond these two closes the mesh vertex schema without
now being amended — the one other match, [ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md),
mentions "the one static mesh vertex layout" only as contextual
narrative about Spec 0017's own past work, never as an independent,
authoritative closure of the schema itself, and needs no amendment.

## Architectural Impact

- **New:** [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)
  (`Accepted`) — the normal attribute's own schema, byte offset, version
  bump, numeric contract, and coordinate convention (D1–D6).
- **Amended (Accepted Amendment):** [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  own format-scope sentence, narrowed by its 2026-08-25 Amendment to
  "position, color, and UV0," gains a second, separately-dated Accepted
  Amendment widening it to "position, color, UV0, and normal."
- **Amended (Accepted Amendment):** [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
  own Decision, which previously closed the vertex layout at exactly
  three attributes ("the one, single static mesh vertex layout —
  position + color + UV0"), gains an Accepted Amendment admitting the
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
- **A `std::sqrt`-based check on `length` directly, instead of a
  `length`-*squared* check in double precision.** This Spec's own first
  draft's own choice — reversed this round (D3): `std::sqrt`'s own
  exact rounding behavior, while required to be correctly rounded by
  IEEE 754 for a *single* operation, still leaves the preceding
  `x·x + y·y + z·z` sum's own rounding path open to real ISA/compiler
  differences (fused-multiply-add availability differs between x86-64
  SSE2 and ARM/AArch64 NEON, this codebase's own two Phase 1 targets) —
  a genuine, if small, determinism risk for a value compared against a
  fixed tolerance boundary. Comparing `length`-squared in double
  precision instead avoids `std::sqrt` entirely and, via exact
  float-to-double promotion, removes the float-level rounding-path
  question altogether — strictly safer, at zero added complexity.
- **A placeholder/zero-magnitude normal on `minimal_cube`, explicitly
  marked "not for lighting."** Rejected: would fail this Spec's own new
  unit-length numeric contract (D3) by construction, and — more
  fundamentally — would ship a real, cook-time-accepted mesh artifact
  whose own normal data is known-meaningless, contradicting this Spec's
  own basic purpose (a real, usable normal attribute) for the one asset
  most likely to be reused as a placeholder lit-scene test subject
  later.
- **A `kMeshArtifactNormalOffsetBytes`-shaped constant for normal
  alone, leaving position/color/UV0's own offsets comment-only.**
  This Spec's own first-draft choice — reversed this round (D7):
  adding a named constant for normal alone, while position/color/UV0
  remained prose-only, would have been an unjustified asymmetric
  special case with no real testability gain for three of the four
  attributes; this Spec now exposes **all four** offsets as named
  constants together (D7), closing the gap for the whole layout, not
  one attribute in isolation.

## Testing & Verification Plan

- **Source parse/round-trip:** `parseMeshSource()`/`serializeMeshSource()`
  round-trip with real normal values, including all eight
  `minimal_cube` corner values (D5) and both quads' `(0, 0, 1)` values
  (D6).
- **Fixed little-endian bytes:** a pinned-expected-byte-vector test for
  `encodeMeshArtifact()` against a known small mesh, extended to eleven
  floats per vertex (matching `mesh_artifact_tests.cpp`'s own existing
  UV0-era pinning-test precedent, D5) — the bit pattern is locked from
  a real build's own output, not hand-derived in this Spec.
- **Cook/decode independent normal validation:** every scenario in D2's
  own six-row error-domain matrix, each asserted against its own real,
  named enumerator — including `NonUnitNormal` at **both** layers,
  independently (decode-time driven by a real, hand-corrupted artifact
  byte buffer, never merely re-running the cook-time case).
- **D3's own explicit boundary/edge-case set:** `lengthSquared` exactly
  `0.9801` and `1.0201` (both accepted); the smallest representable
  step outside each bound (both rejected); a `-0.0` component
  (accepted, behaves identically to `+0.0` for this check); an
  extremely small non-zero vector (`~1e-20` per component, rejected);
  `NaN`/`Inf` on one component (rejected by `NonFiniteFloat`, confirmed
  to run before the length-squared check ever executes).
- **Version/field-count/stride rejection:** version 2 and version 1
  source/artifact rejection; a correctly-versioned 10-field line
  (missing one normal component); truncated artifact input; every
  stride value other than 44 (including the old `32`).
- **Deterministic double-cook:** `cookStaticMesh()` invoked twice
  against identical source text produces byte-identical artifacts.
- **Re-import triggering:** the existing content-driven re-cook
  mechanism, unaffected in mechanism, re-verified against all three
  real mesh assets under the new grammar.
- **Hand-computed math tests (D6):** a CPU-only unit test asserting
  each of the four independently-computed triangle-level face-normal
  results (both triangles, both quads) equals `(0, 0, 1)`, matching
  D6's own exact, disclosed arithmetic — independent of any shader.
- **All six real composition-root touch points (D7):** each confirmed
  to compile (including its own four new `static_assert`s against
  `mesh_artifact.h`'s own named offset constants) and successfully
  build/run its own existing test suite against the widened 44-byte
  `Vertex` struct.
- **All nine embedded-mesh-source-text test files** (Pre-draft
  verification's own exact, counted list) confirmed updated and
  passing — including at least one deliberately-left-at-the-old-
  field-count case, to prove `CountMismatch` still fires correctly
  post-widening.
- **Both `ATLANTIS_BUILD_TESTS` configurations:** a normal `ON` build
  (the full test suite, above) and a fresh `ATLANTIS_BUILD_TESTS=OFF`
  configure/build, confirming all three real mesh assets re-cook
  successfully against the new grammar with zero `tests/` dependency
  in that build tree (Requirements' own confirmed-unconditional-
  declaration finding, above).
- **Both Debug and Release configurations**, matching every prior
  Spec's own established verification baseline.
- **The four existing goldens** (`minimal_cube`, `world_scene`,
  `textured_quad`, `material_demo`) confirmed byte-for-byte (PNG/
  sidecar) and pixel-for-pixel unchanged — no shader this Spec touches
  or that any of the six composition roots use declares a normal input,
  so this Spec's own widening is provably inert for every currently-
  rendered pixel.
- **Module boundary:** a fresh include-scan confirming `Atlantis::AssetSystem`
  still depends on `Atlantis::Core` only — this Spec adds no new
  dependency to Asset System's own module boundary.
- **C4062:** confirmed, and stated as a confirmed finding rather than a
  probe to run, that neither `SourceParseError` nor `ArtifactDecodeError`
  is consumed by any exhaustive `switch`/`toString()` anywhere in this
  codebase today (D2) — no `/w14062` positive/negative probe is
  applicable to this Spec's own two new enumerators. (A C4062 probe
  remains relevant to any *existing* switch this Spec's Implementation
  might separately touch, but this Spec's own new enumerators introduce
  none.)
- **No new golden, no new shader, no normal-visualization capability of
  any kind** — see D8's own full reasoning; this Spec's verification is
  CPU/GPU-independent data-contract proof, in full.

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
