# Plan: Mesh Normal Attribute Foundation

- **Spec:** [specs/0020-mesh-normal-attribute-foundation.md](../specs/0020-mesh-normal-attribute-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** slmao
- **Human Review Approval (2026-08-29):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-29, following the two review
  rounds recorded below — the second directed a mechanical, disclosed
  "Human Review Correction" to [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)
  itself (a one-word file-count summary slip, its own already-`Approved`
  Decisions left completely unmodified), corrected this Plan's own
  numeric-contract implementation shape (a shared, directly-testable
  `detail::` function pair, honest about what floating-point
  determinism is and is not guaranteed, with no new compiler-specific
  flag), split boundary testing into three distinct, each-independently-
  implementable kinds, and added a `std::is_standard_layout_v` guard and
  an explicit fixed-byte/shader-location/error-domain re-confirmation to
  the Verification Checklist — accepting this document's own three
  Milestones, its own Milestone 1 atomic boundary, the `version 3`/
  44-byte fixed layout, the named offset constants, the numeric
  validation implementation, the three-asset/six-consumer/nine-test-file
  migration (in its own two, now-corrected categories), the V1–V22
  Verification Checklist, and every stated Non-Goal, in full. **This
  approval authorizes Implementation of this Plan only once this PR
  itself has merged — not before.** [Spec 0019](../specs/0019-lighting-foundation.md)
  remains Plan-blocked until this Plan's own Implementation PR
  (a separate, later PR) merges — this approval does not itself
  constitute that event.

## Plan Review

Two review rounds, both recorded rather than silently folded in,
matching this repository's own established Plan-review disclosure
precedent (Plan 0017's own identical section).

### Round 1 (2026-08-29, first draft)

1. **A real, mechanical discrepancy found in the then-current Approved
   Spec's own prose, not a design gap** — [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
   own Pre-draft verification said "**Nine** test files carry embedded,
   literal mesh-source text" but its own following list named exactly
   **eight**. A repository-wide re-search independently confirmed eight
   files carry embedded `atlantis_static_mesh_source_version` *text*,
   and a ninth, real touch point
   (`tests/asset_system/mesh_artifact_tests.cpp`) exists but constructs
   `MeshSourceVertex` directly via aggregate initialization, invisible
   to that same text search — so the word "nine" was correct in total
   count, wrong in category. **This round's own finding was directed to
   Human Review rather than silently resolved at Plan level; Human
   Review has since directed, and this Spec now carries, its own
   "Human Review Correction — 2026-08-29" section recording exactly
   this — see Round 2 item 1, below, for this Plan's own consequent
   update.**
2. **Two specific existing negative tests would silently change what
   they test, not merely fail to build, if their own embedded literal
   were left unexamined** — found by reading, not assumed:
   `mesh_source_tests.cpp`'s own "rejects an unrecognized version line"
   test currently asserts `atlantis_static_mesh_source_version: 3` is
   rejected (true today; false after this Plan's own Implementation,
   since version 3 becomes the real, accepted version) —
   `mesh_artifact_tests.cpp`'s own "rejects an unknown schema version"
   test has the identical shape, setting the schema-version byte to
   `0x03`. Both must be updated to name a still-genuinely-unrecognized
   value (this Plan fixes both to `4`) rather than left as `3`, or
   Implementation would silently begin testing the wrong thing while
   still compiling and, worse, still passing (since a real `4` would
   also correctly reject) — a false sense of coverage this Plan closes
   by naming the exact fix here.
3. **`tests/tools/asset_cooker/cook_command_tests.cpp`'s own version-1
   rejection test needs no change** — confirmed by direct reading: it
   asserts a `atlantis_static_mesh_source_version: 1` source is
   rejected by the real CLI, which remains true (version 1 stays
   rejected) under this Plan's own version-3 grammar exactly as it was
   under version 2's. Listed explicitly as **not** requiring an edit,
   rather than left for Implementation to discover.

### Round 2 (2026-08-29, final, pre-approval)

1. **Updated to match [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
   own new "Human Review Correction — 2026-08-29" section:** the real
   touch-point count is **nine test files, in two categories** — eight
   carrying embedded, literal mesh-source text (Round 1's own list,
   unchanged) plus one, `mesh_artifact_tests.cpp`, that constructs
   `MeshSourceVertex` directly via aggregate initialization. This
   Plan's own Pre-draft verification, Milestones, Files/Modules
   Touched, and Verification Checklist below all use this corrected,
   two-category "nine total" accounting throughout — **not** Round 1's
   own provisional "eight, not nine" phrasing, which is superseded by
   the Spec's own now-corrected text, not left standing alongside it.
2. **Double precision alone does not make the `length`-squared
   computation's own intermediate values bit-identical across every
   compiler/target — this round corrects an overclaim risk in the
   Plan's own first-draft framing of D3, not a change to the tolerance
   or the accept/reject contract itself.** Fused-multiply-add (FMA)
   contraction of an `a*b + c`-shaped expression is a compiler/target
   choice orthogonal to operand width — `double` arithmetic does not,
   by itself, disable it. Confirmed by a repository-wide search: no
   `/fp:` flag is set anywhere in this project's CMake configuration
   today, so every target compiles under MSVC's own default
   floating-point model, `/fp:precise`, which does not fuse
   multiply-add into FMA for ordinary scalar code (unlike `/fp:fast`,
   used nowhere in this project) — the real, current build is not at
   risk in practice, but this Plan must not claim a guarantee its own
   design does not actually provide for a hypothetical future
   toolchain (e.g. a Clang/GCC-based Android target, Phase 1's own
   other primary platform, not yet implemented) that might default
   differently. Corrected (P7, below): the exact implementation shape,
   split into two small, directly-testable `detail::` functions,
   precise language about what is and is not guaranteed, and
   confirmation that this Plan adds **no** new compiler-specific FP
   flag to work around this — the tolerance's own width is the real
   mitigation, not a forced FP mode.
3. **The length-squared check moves from "duplicated in each
   translation unit" (this Plan's own first-draft choice) to one
   shared, directly-testable `atlantis::asset_system::detail::`
   function pair, declared in `mesh_source.h`** — matching
   `atlantis::runtime::detail::checkForDuplicatesAndCollisions()`'s own
   already-established, real precedent for exactly this need (a real,
   internal, non-public-contract seam a test can call directly,
   documented as such in the header). This round's own review found
   the first draft's "each `.cpp` gets its own copy, matching
   `splitLines()`-style small-helper duplication" reasoning does not
   actually fit here: `splitLines()` is duplicated because it is
   *trivial* and has no independent test of its own; a numeric
   tolerance boundary is exactly the kind of logic this codebase's own
   `detail::` idiom exists to make independently testable without
   becoming part of a module's stable public contract. `mesh_artifact.h`
   already `#include`s `mesh_source.h` (for `ParsedMeshSource`), so
   this introduces no new header dependency direction.
4. **Boundary tests must be restructured into three distinct kinds, not
   one, or the "exact `0.9801`/`1.0201` boundary" claim is not actually
   testable the way this Plan's own first draft implied** — `0.99f`
   squared and the decimal literal `0.9801` are not guaranteed to be
   the identical `double` value once a real `float` triple is promoted
   and summed; a test that tries to construct "a normal whose three
   float components sum-of-squares to exactly `0.9801`" is relying on
   an unstated, unverified decimal-equality assumption. Corrected (P8,
   below): pure comparator tests (the boundary itself, via `std::nextafter`
   on exact `double` literals, never through float components), a
   separate "real float components, clearly inside/outside" integration
   layer, and the existing full parse/decode-path tests — three
   distinct verification items, not one conflated claim.
5. **`static_assert(std::is_standard_layout_v<Vertex>)`** added to each
   of the six composition roots' own five existing `static_assert`s
   (P4) — a `constexpr`/`static_assert`-only addition, zero new runtime
   API, closing the one remaining implicit assumption `offsetof()`
   itself already requires (`offsetof` is only well-defined for a
   standard-layout type) but that this Plan's own first draft never
   stated as its own explicit, checked precondition.
6. **Explicit confirmation added that no shader gains a new
   `[[vk::location(N)]]` declaration as a consequence of this Plan** —
   `Vertex` gaining a `normal` field changes no composition root's own
   `MeshVertexAttributeSchema` (each still lists only the attributes
   its own real shader reflects, unchanged — Pre-draft verification's
   own already-confirmed finding, restated here as an explicit
   Verification Checklist item rather than left implicit).
7. **Explicit confirmation added that `mesh_artifact_tests.cpp`'s own
   pinned-byte-vector test's *final*, checked-in expected value is a
   compile-time `constexpr`/literal byte vector, computed once by an
   independent tool at Implementation time and transcribed — never a
   runtime call to `encodeMeshArtifact()` used to generate its own
   "expected" comparison value** — this Plan's own first draft already
   implied this (matching the existing test's own real, current shape,
   Pre-draft verification) but did not state it as its own explicit
   constraint; a test that calls the function under test to produce its
   own expected value is not a regression test.

No other finding changed this Plan's own structure. The remainder of
this document reflects both rounds' own corrections already applied,
not tracked as open items.

## Objective

Implement [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)
in full: widen the one, single, fixed static mesh vertex layout with a
fourth, mandatory `normal` (object-space, 3 floats) attribute —
authoring grammar version 3, artifact schema version 3, 44-byte stride,
a `length`-squared/double-precision numeric contract, four new named
`mesh_artifact.h` offset constants with `static_assert`-enforced
composition-root synchronization, all three real mesh sources
re-authored with real, disclosed normal values, and all real touch
points (composition roots and tests) widened in lockstep — with **zero**
change to any rendered output, since no shader in this codebase declares
a normal input yet. This Plan implements the data contract only; no
Lighting, no Lit Material, no shader, no new golden. Landing this
Plan's own Implementation PR is the literal event that lifts
[Spec 0019](../specs/0019-lighting-foundation.md)'s own Plan-drafting
gate — nothing else does.

## Pre-draft verification against real, current source

Re-confirmed directly against `main` at Plan-drafting time (2026-08-29,
immediately following [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
own Human Review Approval and PR #91's merge), by reading full files —
not reused from the Spec's own Pre-draft verification without
independent re-checking, and extended with the concrete file/line/byte
detail an Implementation-ready Plan needs that a Spec correctly leaves
open.

### Format constants and functions (unchanged since Spec 0020's own drafting)

- `src/asset_system/include/atlantis/asset_system/mesh_source.h`:
  `MeshSourceVertex` is exactly `{positionX, positionY, positionZ,
  colorR, colorG, colorB, uvU, uvV}` — eight `float` fields, in this
  exact declaration order.
- `src/asset_system/src/mesh_source.cpp`: `kVersionLine =
  "atlantis_static_mesh_source_version: 2"`; `parseMeshSource()`'s own
  per-vertex-line loop builds `float* const components[8]` pointing at
  the eight `MeshSourceVertex` fields in declaration order and requires
  `fields.size() != 8` to reject; the per-float loop
  (`for (std::size_t f = 0; f < 8; ++f)`) checks `std::isfinite()` on
  each. `kMaxVertexCount = 65535`, unaffected by this Plan.
- `src/asset_system/include/atlantis/asset_system/mesh_artifact.h`:
  `kMeshArtifactSchemaVersion = 2`, `kMeshArtifactVertexStrideBytes = 32`,
  `kMeshArtifactHeaderSizeBytes = 40` — the only three named constants
  this format exposes today; every per-attribute offset (0/12/24) is
  documented only in this header's own top-of-file comment.
- `src/asset_system/src/mesh_artifact.cpp`: `encodeMeshArtifact()`
  writes, per vertex, `positionX/Y/Z`, `colorR/G/B`, `uvU/uvV` via
  `appendFloatLE()`, in that order — no other float. `decodeMeshArtifact()`'s
  own per-vertex finiteness loop is `for (std::size_t floatIndex = 0;
  floatIndex < 8; ++floatIndex)`, hardcoded. The header-field byte
  offsets (all unaffected by this Plan, restated for completeness):
  magic `0`–`7`, `schemaVersion` `8`–`11`, `vertexStrideBytes` `12`–`15`,
  `assetId` `16`–`23`, `vertexCount` `24`–`27`, `indexCount` `28`–`31`,
  `vertexBytesOffset` `32`–`35`, `indexBytesOffset` `36`–`39`.
- `src/asset_system/include/atlantis/asset_system/static_mesh_asset_data.h`/
  `.cpp`: `StaticMeshAssetData` holds `std::vector<std::byte> vertexBytes_`,
  `std::vector<std::uint16_t> indices_`, `std::uint32_t vertexStrideBytes_`
  — confirmed zero source change needed (unchanged since ADR-0058, and
  since Spec 0020's own confirmation).
- `src/renderer/src/mesh.cpp`: `createMesh()`'s own `layout` parameter
  is `static_cast<void>(layout);` — confirmed zero change.
- `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp`:
  `toVertexInputLayout()` matches by `location` and requires an exact
  attribute-*count* match in both directions; `toRhiFormat()` maps only
  `VertexAttributeType::Float3`/`Float2` — confirmed zero change, zero
  new enumerator value needed for a `float3` normal.
- `shaders/minimal_renderer/minimal_mesh.slang`:
  `[[vk::location(0)]] float3 position; [[vk::location(1)]] float3 color;`
  `shaders/textured_quad/textured_quad.slang`:
  `[[vk::location(0)]] float3 position; [[vk::location(1)]] float2 uv;`
  — neither declares a normal input; this Plan touches neither `.slang`
  file.

### The complete error-domain surface (`src/asset_system/include/atlantis/asset_system/errors.h`, read in full)

`SourceParseError`: `UnknownSourceVersion, MissingField,
FieldOrderMismatch, MalformedNumber, NonFiniteFloat, CountMismatch,
IndexOutOfRange, IndexCountNotMultipleOfThree, VertexCountOutOfRange,
TrailingContent`. `ArtifactDecodeError`: `TooSmallForHeader, BadMagic,
UnknownSchemaVersion, UnsupportedVertexStride, InconsistentOffsets,
SizeMismatch, VertexCountOutOfRange, IndexCountNotMultipleOfThree,
IndexOutOfRange, NonFiniteFloat`. Confirmed, by a repository-wide
`switch.*SourceParseError`/`switch.*ArtifactDecodeError` and
`toString(SourceParseError`/`toString(ArtifactDecodeError` search
(zero matches for either), that adding `NonUnitNormal` to each enum
requires no `/w14062` C4062 probe anywhere — restated from Spec 0020's
own D2, independently re-run, same result.

### The three real mesh sources (`assets/meshes/`, full content read)

- `minimal_cube.mesh.txt`: `atlantis_static_mesh_source_version: 2`,
  8 vertices, 36 indices (12 triangles). Vertex order and position
  values, exact: `v0(-0.5,-0.5,-0.5)`, `v1(0.5,-0.5,-0.5)`,
  `v2(0.5,0.5,-0.5)`, `v3(-0.5,0.5,-0.5)`, `v4(-0.5,-0.5,0.5)`,
  `v5(0.5,-0.5,0.5)`, `v6(0.5,0.5,0.5)`, `v7(-0.5,0.5,0.5)`. Each of
  the 8 indices appears in exactly 3 of the 12 triangles (confirmed by
  direct enumeration of the index list) — the real, structural
  confirmation behind Spec 0020 D5's own "8 shared corners, each
  shared by 3 mutually-perpendicular faces" claim.
- `textured_quad_left.mesh.txt` / `textured_quad_right.mesh.txt`: 4
  vertices, 2 triangles each (`index: 0 1 2`, `index: 2 3 0`), both
  flat at `z = 0`. Left: `v0(-0.9,-0.5,0)`, `v1(-0.1,-0.5,0)`,
  `v2(-0.1,0.5,0)`, `v3(-0.9,0.5,0)`. Right: identical shape translated
  `+1.0` on `X`.

### The exact, real content this Plan widens (all six composition roots, `struct Vertex` and its own surrounding context)

Each already `#include <cstddef>` (needed for `offsetof`, already
present — confirmed, zero new include for that alone); **none currently
`#include <atlantis/asset_system/mesh_artifact.h>`** — each needs that
one new include added, for the four new `static_assert`s (D-Plan-3,
below).

| File | `struct Vertex` at line | Local `VertexInputLayout`-building helper (unaffected — schema unchanged) |
|---|---|---|
| `src/runtime/src/runtime_application.cpp` | 58 | (two helpers in this file: `unlitTexturedVertexLayout()` and a `minimal_mesh`-schema one; neither declares a normal, both unaffected) |
| `tests/image_regression/fixture/material_demo_fixture.cpp` | 67 | `unlitTexturedVertexLayout()` |
| `tests/image_regression/fixture/minimal_cube_fixture.cpp` | 65 | `minimalMeshVertexLayout()` |
| `tests/image_regression/fixture/textured_quad_fixture.cpp` | 73 | `texturedQuadVertexLayout()` |
| `tests/image_regression/fixture/world_scene_fixture.cpp` | 79 | `minimalMeshVertexLayout()` |
| `tests/image_regression/fixture/world_scene_loaded_fixture.cpp` | 81 | `minimalMeshVertexLayout()` |

Every one of the six is currently `struct Vertex { float position[3];
float color[3]; float uv[2]; };` — byte-identical shape across all six,
confirmed by direct reading, not assumed from one file.

**`tests/runtime/material_realization_gpu_tests.cpp`'s own local
`Vertex` (line 74, identical shape) is re-confirmed, independently of
Spec 0020's own claim, as genuinely not a required touch point** — this
file never calls `loadStaticMeshAsset()`/`loadAndInstantiateScene()`
anywhere (grepped: zero matches), and its own `Vertex` exists solely to
satisfy `toVertexInputLayout()`'s signature for Pipeline construction in
tests that only exercise material realization, never mesh drawing. Left
untouched.

### The eight real embedded-mesh-source-text test files, exact current shape (re-verified, not estimated)

- `tests/asset_system/load_tests.cpp` — a `kValidTriangleSource`-shaped
  constant plus one further, distinct mesh-source string used by its
  own metadata-mismatch test; both `atlantis_static_mesh_source_version: 2`,
  8-field `vertex:` lines. Both feed real `cookStaticMeshAsset()`/
  `loadStaticMeshAsset()` calls this file's own tests depend on
  succeeding.
- `tests/asset_system/mesh_source_tests.cpp` — the largest (26
  `vertex:` lines across ~19 `TEST_CASE`s). Contains: the accept-path
  cases (well-formed triangle, no-trailing-newline, `\r`-tolerance,
  round-trip) that must move to version 3/11 fields with a real,
  unit-length normal; the version-1-rejection case (line ~87–93,
  **unchanged** — version 1 stays rejected); the
  **version-3-then-"unrecognized"** case (line ~95–99, **must change
  its literal from `3` to `4`** — Plan Review Round 1 item 2, above); every
  other case (wrong field order, malformed numeric/UV token, double-
  space separator, non-finite position/color/UV float, wrong field
  count, the two deliberate "still missing UV0 columns"/"exact old
  pre-UV0 field count" boundary cases, index-line issues, count-range
  issues, trailing content) currently authored against
  `atlantis_static_mesh_source_version: 2` — **each of these moves to
  version 3, with its own vertex line widened to 11 fields (adding a
  real, unit-length normal) unless the specific case under test is
  itself about field *count*, in which case the new field count is
  restated relative to 11, not 8** (e.g. the "still missing UV0
  columns" case's own intent — "one attribute's own columns absent" —
  is preserved by a new, analogous "missing the normal columns"
  variant at 8 fields, i.e. every field present except normal; the
  "exact old pre-UV0 field count" case's own intent is preserved by a
  new "exact old pre-normal field count" variant at exactly 8 fields
  too, distinguishing "no UV0, no normal" from "has UV0, no normal" as
  two, not one, still-independently-meaningful boundary cases — see
  Milestone 1's own itemized list).
- `tests/asset_system/mesh_uv_round_trip_tests.cpp` — one embedded
  source (`atlantis_static_mesh_source_version: 2`, D9's own real
  cook→load round-trip proof this Plan's own new
  `mesh_normal_round_trip_tests.cpp` mirrors for normal, below) — moves
  to version 3/11 fields with a real normal.
- `tests/runtime/material_realization_gpu_tests.cpp` — one embedded
  source, feeding a real material-realization GPU test unrelated to
  normal itself; moves to version 3/11 fields with a real normal so the
  underlying mesh continues to cook/load successfully.
- `tests/runtime/scene_load_tests.cpp`, `tests/runtime/scene_manifest_tests.cpp`
  — each one embedded `kValidTriangleSource`-shaped constant, feeding
  real scene-load/scene-manifest tests unrelated to normal itself;
  each moves to version 3/11 fields with a real normal.
- `tests/tools/asset_cooker/cook_command_tests.cpp` — two accept-path
  constants (`kValidTriangleSource`, `kValidSquareSource`, both version
  2) move to version 3/11 fields; the existing version-1-rejection CLI
  case (Plan Review Round 1 item 3, above) is **unchanged**.
- `tests/tools/asset_cooker/cooker_determinism_tests.cpp` — one
  embedded `kValidTriangleSource`-shaped constant, feeding the real
  double-cook determinism test; moves to version 3/11 fields with a
  real normal.

### `tests/asset_system/mesh_artifact_tests.cpp` (full content read — the ninth real touch point, its own distinct category — Spec 0020's own "Human Review Correction — 2026-08-29")

`makeOneVertexSource()` builds one `MeshSourceVertex` via aggregate
initialization: `{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f}` (no
embedded text, so outside Spec 0020's own "embedded mesh-source text"
search, but a real touch point regardless — it directly constructs
`MeshSourceVertex`, which gains three new fields). The pinned-byte
vector test (`"encodeMeshArtifact matches an independently-computed
expected byte vector"`) hardcodes, at the current 32-byte stride: magic
(offset 0–7), `schema_version = 2` (8–11), `vertex_stride_bytes = 32`
(12–15), `asset_id` (16–23), `vertex_count = 1` (24–27),
`index_count = 3` (28–31), `vertex_bytes_offset = 40` (32–35),
`index_bytes_offset = 72` (36–39, `= 40 + 1×32`), the vertex's own 32
bytes (position/color/UV0), then 6 index bytes — total `78` bytes,
`REQUIRE(expected.size() == 78)`. Six further tests each construct a
one-vertex artifact via `encodeMeshArtifact(1, makeOneVertexSource())`
and corrupt one byte: `"rejects an unknown schema version"` sets byte 8
to `0x03` (**must become `0x04`** — Plan Review Round 1 item 2); `"rejects
the old, pre-UV0 vertex stride"` sets byte 12 to `0x18` (`= 24`, the
pre-UV0 stride — **stays a valid, meaningful "old stride" test
unchanged**, since 24 is still not 44); `"rejects an unsupported vertex
stride"` sets byte 12 to `0x10` (`= 16`, an arbitrary wrong value —
**unchanged**); `"rejects an out-of-range index"` sets bytes at offset
`72`/`73` (**must become `84`/`85`** — the new `index_bytes_offset`);
`"rejects a non-finite position/color vertex float"` sets bytes
`40`–`43` (positionX — **unchanged**, offset 0 within the vertex is
unaffected by the widening); `"rejects a non-finite UV0 float"` sets
bytes `64`–`67` (`= 40 + 24` — **unchanged**, UV0's own relative offset
is unaffected).

### CMake (`assets/CMakeLists.txt`, `src/asset_system/CMakeLists.txt`, full content read)

All three real mesh assets' own `atlantis_add_static_mesh_asset()`
calls (`minimal_cube` line 17, `textured_quad_left` line 110,
`textured_quad_right` line 118) are **unconditional** — none inside an
`ATLANTIS_BUILD_TESTS` guard; the two quad declarations' own comment
states this explicitly ("Declared here, unconditionally ... for the
same `atlantis_finalize_asset_validation()` configure-time-visibility
reason"). Confirmed: a fresh `-DATLANTIS_BUILD_TESTS=OFF` configure/
build re-cooks all three under the new grammar with no gap. No CMake
file needs a new target or dependency edge for this Plan — the format
version bump requires no build-graph change, only re-running the
existing, unconditional cook step (which CMake's own existing
source-file dependency on `SOURCE` already triggers automatically on
any `.mesh.txt` content change).

### Module boundary (unaffected, re-confirmed)

`Atlantis::AssetSystem` links `Atlantis::Core` only
(`src/asset_system/CMakeLists.txt`, unchanged by this Plan) — this
Plan adds no new dependency anywhere.

## Plan-level decisions (fixed here, not left to Implementation)

### P1. Exact normal values for every real, checked-in mesh source

Transcribed directly from [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)
D5/D6 — not re-derived, not left to Implementation's own rounding
choice:

- `minimal_cube.mesh.txt`, all eight vertices, sign-matched to each
  vertex's own existing position: `v0(-0.5,-0.5,-0.5)` → normal
  `-0.577350269 -0.577350269 -0.577350269`; `v1(0.5,-0.5,-0.5)` →
  `0.577350269 -0.577350269 -0.577350269`; `v2(0.5,0.5,-0.5)` →
  `0.577350269 0.577350269 -0.577350269`; `v3(-0.5,0.5,-0.5)` →
  `-0.577350269 0.577350269 -0.577350269`; `v4(-0.5,-0.5,0.5)` →
  `-0.577350269 -0.577350269 0.577350269`; `v5(0.5,-0.5,0.5)` →
  `0.577350269 -0.577350269 0.577350269`; `v6(0.5,0.5,0.5)` →
  `0.577350269 0.577350269 0.577350269`; `v7(-0.5,0.5,0.5)` →
  `-0.577350269 0.577350269 0.577350269`. **Position, color, UV0, and
  index-line bytes on every one of these eight `vertex:` lines are
  byte-for-byte unchanged** — only the trailing three tokens are
  appended.
- `textured_quad_left.mesh.txt` / `textured_quad_right.mesh.txt`, all
  four vertices each: `0 0 1` (exact — no decimal-precision concern,
  representable exactly in `binary32`).

### P2. Test-helper normal values (not authoring-source values, but must also satisfy D3's own contract)

Every hardcoded test literal this Plan touches that needs a "some real,
unit-length normal, value unimportant" placeholder (as opposed to P1's
own geometrically-meaningful authoring values) uses the **identical**
`0.577350269 0.577350269 0.577350269` literal — reusing D5's own
already-`Approved`, already-justified value rather than inventing a new
one, matching this codebase's own "no gratuitous magic numbers"
discipline. This applies to: `mesh_artifact_tests.cpp`'s own
`makeOneVertexSource()` (becomes `{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f,
7.0f, 8.0f, 0.577350269f, 0.577350269f, 0.577350269f}`), and every
accept-path embedded-text literal in the eight files above whose own
test is not specifically about a particular normal *value* (only the
handful of D3-boundary-specific tests below use a different, deliberate
value).

### P3. Exact new byte offsets, `mesh_artifact_tests.cpp`

With stride `44` (not `32`): `vertex_bytes_offset` stays `40`
(unaffected — header size is unchanged); `index_bytes_offset` becomes
`84` (`= 40 + 1×44`); total artifact size becomes `90` (`= 84 + 6`
index bytes). The pinned-byte-vector test's own expected vector is
recomputed **at Implementation time** by an independently-computed tool
(matching this test's own existing disclosed method, ".NET's own
`BitConverter.GetBytes()`, not transcribed from memory") for the new,
wider vertex (position/color/UV0 unchanged, normal
`0.577350269 0.577350269 0.577350269` per P2) — this Plan fixes the
exact input values and the exact computation method, not the resulting
hex bytes, matching Spec 0020 D5's own identical "not hand-derived"
discipline. The out-of-range-index test's own two corrupted bytes move
from `72`/`73` to `84`/`85`. The schema-version/stride/non-finite tests'
own byte offsets are otherwise unchanged (Pre-draft verification,
above) except the "unknown schema version" test's own asserted value,
`0x03` → `0x04` (Plan Review Round 1 item 2).

### P4. Named constants and `static_assert` wiring — exact form

`mesh_artifact.h` gains, immediately after the existing
`kMeshArtifactSchemaVersion`/`kMeshArtifactVertexStrideBytes`/
`kMeshArtifactHeaderSizeBytes` constants:

```cpp
inline constexpr std::size_t kMeshArtifactPositionOffsetBytes = 0;
inline constexpr std::size_t kMeshArtifactColorOffsetBytes = 12;
inline constexpr std::size_t kMeshArtifactUv0OffsetBytes = 24;
inline constexpr std::size_t kMeshArtifactNormalOffsetBytes = 32;
```

Each of the six real composition-root touch points adds, immediately
after its own `struct Vertex { ... };` definition:

```cpp
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(offsetof(Vertex, position) == atlantis::asset_system::kMeshArtifactPositionOffsetBytes);
static_assert(offsetof(Vertex, color) == atlantis::asset_system::kMeshArtifactColorOffsetBytes);
static_assert(offsetof(Vertex, uv) == atlantis::asset_system::kMeshArtifactUv0OffsetBytes);
static_assert(offsetof(Vertex, normal) == atlantis::asset_system::kMeshArtifactNormalOffsetBytes);
static_assert(sizeof(Vertex) == atlantis::asset_system::kMeshArtifactVertexStrideBytes);
```

(The `sizeof(Vertex)` assertion against the stride constant is a
direct, mechanical strengthening this Plan adds beyond Spec 0020's own
four-offset text — same `static_assert`/`constexpr` mechanism, zero new
API, and closes the one remaining gap those four alone would leave
open: a `Vertex` struct with correct offsets but incorrect *trailing
padding* would still pass all four offset checks while failing to
match the real artifact stride. The leading
`std::is_standard_layout_v<Vertex>` assertion, added Plan Review Round
2 item 5, makes explicit the one precondition `offsetof()` itself
already silently requires — `offsetof` is only well-defined for a
standard-layout type — rather than leaving it an unstated assumption;
every one of the six existing `Vertex` structs already satisfies this
trivially, being a plain aggregate of `float[3]`/`float[2]` arrays with
no base class, virtual function, or access-specifier mixing, so this
assertion is expected to pass immediately, not to change behavior — its
own value is guarding against a *future* accidental change to one of
these structs, not fixing a present defect.) Each of the six files
gains one new `#include <atlantis/asset_system/mesh_artifact.h>` (for
the four offset constants) and one new `#include <type_traits>` (for
`std::is_standard_layout_v`) if not already present.

### P5. The two `mesh_source_tests.cpp` field-count boundary cases, exact new shape

- The existing **"rejects a version-2-labeled vertex line still
  missing its UV0 columns (7 fields)"** case is superseded by two
  distinct new cases at version 3: **"rejects a vertex line missing
  only its normal columns (8 fields)"** (all of position/color/UV0
  present, normal entirely absent) and **"rejects a vertex line missing
  its UV0 and normal columns (6 fields)"** (position/color only) — both
  asserting `SourceParseError::CountMismatch`.
- The existing **"rejects a version-2-labeled vertex line with the
  exact old, pre-UV0 field count (6 fields)"** case is superseded by
  **"rejects a vertex line with the exact old, pre-normal field count
  (8 fields)"** at version 3 — the direct analog one version bump
  later, same intent (a well-formed-looking line at exactly the
  *previous* format's own field count), asserting `CountMismatch`.

### P6. New, dedicated round-trip test file

A new `tests/asset_system/mesh_normal_round_trip_tests.cpp` (mirroring
`mesh_uv_round_trip_tests.cpp`'s own exact shape and CMake registration
pattern), proving a real, disclosed normal value written in an
authoring source reaches `StaticMeshAssetData` unchanged, bit-for-bit,
through the real `cookStaticMesh()` → `loadStaticMeshAsset()` path —
using one of `minimal_cube`'s own real P1 values (not a synthetic P2
placeholder), so this test doubles as a real proof that a geometrically
meaningful, non-uniform-across-components value (`±0.577350269` mixed
signs) survives the full pipeline exactly.

### P7. The numeric-contract check: exact implementation shape, honest determinism claim, no new compiler flag (Plan Review Round 2 item 2/3)

**Exact code shape**, placed in `mesh_source.h`'s own new
`atlantis::asset_system::detail` namespace (declaration) and
`mesh_source.cpp` (definition) — mirroring
`atlantis::runtime::detail::checkForDuplicatesAndCollisions()`'s own
already-established "internal seam, not part of this module's own
public contract, directly unit-testable" precedent exactly, not a new
pattern invented here:

```cpp
namespace atlantis::asset_system::detail {

// Internal seam, exposed for direct unit testing only -- not part of
// this module's own public contract (mirrors
// atlantis::runtime::detail::checkForDuplicatesAndCollisions()'s own
// identical precedent). Spec 0020 D3: computes a normal's own
// length-squared from its three already-finite-checked float
// components, via exact float-to-double promotion and a fixed,
// left-to-right summation order. Promotion from float to double is
// always exact (zero rounding); this function does not, on its own,
// guarantee its own returned value is bit-identical across every
// compiler/target for a given input -- fused-multiply-add (FMA)
// contraction of `a*a + b` is a compiler/ISA choice orthogonal to
// operand width, and this function neither forces nor forbids it via
// any pragma or compiler flag. See isNormalLengthSquaredInTolerance()
// below for why this does not matter for this format's own real
// accept/reject contract.
[[nodiscard]] double computeNormalLengthSquared(float x, float y, float z) noexcept {
  const double xd = static_cast<double>(x);
  const double yd = static_cast<double>(y);
  const double zd = static_cast<double>(z);

  double lengthSquared = xd * xd;
  lengthSquared += yd * yd;
  lengthSquared += zd * zd;
  return lengthSquared;
}

// Internal seam, exposed for direct unit testing only -- see above.
// Spec 0020 D3's own exact tolerance: [0.9801, 1.0201] inclusive
// (0.99^2 / 1.01^2), a real +-1%-of-unit-length margin restated on the
// squared quantity. This +-1% margin is chosen to be, and confirmed
// here to remain, vastly wider than the largest possible discrepancy
// between an FMA-contracted and a separately-rounded computation of a
// three-term sum of squares (bounded by a small constant multiple of
// double's own machine epsilon, ~2.22e-16) -- no realistic authored
// normal changes its own accept/reject outcome based on whether a
// given compiler/target contracts multiply-add into an FMA
// instruction. This function does NOT guarantee bit-identical
// intermediate lengthSquared values across toolchains -- only that the
// accept/reject decision computeNormalLengthSquared() feeds into here
// is stable for any input not deliberately engineered to sit within
// about 1e-15 of the exact boundary, which no human-authored normal
// ever will.
[[nodiscard]] bool isNormalLengthSquaredInTolerance(double lengthSquared) noexcept {
  return lengthSquared >= 0.9801 && lengthSquared <= 1.0201;
}

}  // namespace atlantis::asset_system::detail
```

`parseMeshSource()` (`mesh_source.cpp`, same translation unit) and
`decodeMeshArtifact()` (`mesh_artifact.cpp`, via its own existing
`#include <atlantis/asset_system/mesh_source.h>` — already present,
confirmed by Pre-draft verification, needed today only for
`ParsedMeshSource`) both call
`detail::isNormalLengthSquaredInTolerance(detail::computeNormalLengthSquared(x, y, z))`
after their own already-existing finiteness check, returning the new
`NonUnitNormal` enumerator on `false`. **One shared implementation, not
two duplicated copies** — this Plan's own first draft called for
per-translation-unit duplication (matching `splitLines()`'s own
established small-helper precedent); this round's own review found
that reasoning does not actually fit a numeric-tolerance boundary,
which needs to be independently, directly testable, exactly the need
`detail::` namespaces already exist in this codebase to serve.

**What this Plan does and does not claim, stated precisely, not
overclaimed:** input `float`→`double` promotion is exact; the
summation order is fixed by the source code above; the `[0.9801,
1.0201]` tolerance is a real, generous margin, not a claim that hinges
on any operation's own exact last-bit rounding; this Plan does **not**
promise that an arbitrary decimal vector engineered to sit exactly on
the tolerance boundary produces a bit-identical `lengthSquared` value
across every possible compiler/target — only that no such engineered
input exists among this Plan's own real, disclosed authored normals
(P1/P2), and that the accept/reject *decision* is stable regardless.
**Real, checked-in artifact determinism (V10 — a double-cook producing
byte-identical output) comes from a different, independent property:
`encodeMeshArtifact()` always serializes the author's own original,
untouched float bits (`appendFloatLE(std::bit_cast<std::uint32_t>(value))`)
— the numeric-contract check above is a pure accept/reject gate that
never feeds back into what bytes get written; it either lets the
author's own original bits through unchanged or rejects the whole
source outright.** No new compiler-specific floating-point flag (no
`/fp:strict`, no `-ffp-contract=off`, nothing) is added anywhere by
this Plan — confirmed today no `/fp:` flag exists in this project's
CMake configuration at all (Pre-draft verification), and this Plan's
own tolerance is deliberately wide enough that none is needed.

### P8. Boundary-test structure: three distinct kinds, not one (Plan Review Round 2 item 4)

**Kind 1 — pure comparator, exact boundary, via `std::nextafter` on
`double` literals, never through `float` components:** a new,
dedicated unit test calls `detail::isNormalLengthSquaredInTolerance()`
directly with `0.9801` and `1.0201` (both **accepted**), and with
`std::nextafter(0.9801, 0.0)` / `std::nextafter(1.0201, 2.0)` (both
**rejected**) — testing the comparison logic alone, with no float-
promotion or summation-order question involved at all, since the
`double` value under test is supplied directly, not computed.

**Kind 2 — real `float` components, clearly inside/clearly outside:** a
separate unit test calls `detail::computeNormalLengthSquared()` (then
`isNormalLengthSquaredInTolerance()` on its result) with real `float`
triples that are unambiguously far from the boundary either way — P1's
own `0.577350269³`-shaped value (`lengthSquared ≈ 0.999999...`,
comfortably inside) and `1.0f, 1.0f, 1.0f` (`lengthSquared = 3.0`,
comfortably outside), plus a "just barely, unambiguously" pair chosen
with real margin from the exact boundary (e.g. components scaled to
`lengthSquared ≈ 0.99` and `≈ 1.01`) — this layer deliberately makes
**no** claim about landing on the exact `0.9801`/`1.0201` value itself;
that claim belongs to Kind 1 alone.

**Kind 3 — full parse/decode-path integration:** the existing-shaped
tests itemized below (V5c/V8a in the Verification Checklist) —
`parseMeshSource()`/`decodeMeshArtifact()` called with real source
text/artifact bytes, confirming the *whole* path (finiteness check,
then the two `detail::` functions, then the correct enumerator)
produces the correct `Result`, not merely that the isolated comparator
does.

**`-0.0`, extremely small values, exact zero, and `NaN`/`Inf` ordering**
are each exercised at Kind 2 (`computeNormalLengthSquared()` accepts
real `float` inputs, including `-0.0f`) and reconfirmed at Kind 3 (the
full path, proving the finiteness check really does run first) — not
merely asserted once. `NaN`/`Inf` inputs are never passed to Kind 1 or
Kind 2 directly (both are downstream of the finiteness check in the
real call path) — they are exercised exclusively at Kind 3, where the
finiteness check's own ordering is the actual property under test.

## Milestones / Task Breakdown

Two implementation milestones plus a closeout milestone. **Milestone 1
is drafted as one atomic step, not several smaller ones, for the
identical, disclosed reason Plan 0017's own Milestone 1 already
established for the previous (UV0) format bump — strengthened here
since normal touches strictly more files:**
`kMeshArtifactVertexStrideBytes`/`kMeshArtifactSchemaVersion`/`kVersionLine`
are each a single, global, compile-time constant shared by every static
mesh asset in this codebase (Pre-draft verification). Landing the
format/parser/encoder/decoder change without also updating all three
real `.mesh.txt` sources in the same step would make the cooker reject
every one of this repository's own authored meshes outright, breaking
the default build; landing the source updates without also widening
all six composition-root `Vertex` structs (and their own six new
`static_assert`s) in the same step would leave those six either
failing to compile (once the `static_assert`s land) or — if the
`static_assert`s were somehow deferred — silently reading a 44-byte
stride against a 32-byte local struct, a memory-layout mismatch no
compiler catches on its own. There is no safe intermediate tree state
between "everything speaks version 2/32 bytes" and "everything speaks
version 3/44 bytes."

1. **Mesh format extension (atomic).**
   - `src/asset_system/include/atlantis/asset_system/mesh_source.h`/`.cpp`
     (P1's own D1 target): `MeshSourceVertex` gains `normalX`/`normalY`/
     `normalZ`; `kVersionLine` becomes `atlantis_static_mesh_source_version: 3`;
     the per-vertex-line parse widens its own `components[8]` array to
     `components[11]` and its own `fields.size() != 8` check to `!= 11`;
     the new `detail::computeNormalLengthSquared()`/
     `detail::isNormalLengthSquaredInTolerance()` pair (P7 — declared in
     `mesh_source.h`'s own new `detail` namespace, defined in
     `mesh_source.cpp`) runs immediately after the (now 11-wide)
     per-float finiteness loop, returning the new
     `SourceParseError::NonUnitNormal` on failure. `serializeMeshSource()`
     widens its own field-by-field `std::to_string` concatenation to
     eleven values.
   - `mesh_artifact.h`/`.cpp` (D2's own D2 target): four new offset
     constants (P4); `kMeshArtifactVertexStrideBytes` → `44`;
     `kMeshArtifactSchemaVersion` → `3`; `encodeMeshArtifact()` writes
     three more `appendFloatLE()` calls per vertex;
     `decodeMeshArtifact()`'s own per-vertex finiteness loop widens
     `floatIndex < 8` to `< 11`, then calls the identical, **shared**
     `detail::computeNormalLengthSquared()`/
     `detail::isNormalLengthSquaredInTolerance()` pair (P7 — via
     `mesh_artifact.cpp`'s own existing `#include <atlantis/asset_system/mesh_source.h>`,
     already present for `ParsedMeshSource`; no new include needed for
     this specifically) after decode's own finiteness check, returning
     `ArtifactDecodeError::NonUnitNormal` on failure. **One shared
     implementation, not a duplicated one per translation unit** — P7's
     own corrected reasoning, reversing this Plan's own first-draft
     duplication choice.
   - `assets/meshes/minimal_cube.mesh.txt`,
     `assets/meshes/textured_quad_left.mesh.txt`,
     `assets/meshes/textured_quad_right.mesh.txt`: version line → `3`;
     every `vertex:` line gains its own P1 normal.
   - All six composition-root `Vertex` structs (Pre-draft verification's
     own table) gain `float normal[3];`, the new `mesh_artifact.h`/
     `<type_traits>` includes (where not already present), and the six
     P4 `static_assert`s (`is_standard_layout_v` plus four offsets plus
     `sizeof`).
   - `tests/asset_system/mesh_source_tests.cpp`: every accept-path case
     moves to version 3/11 fields with a P2 normal (or, for the
     round-trip test, a value chosen to exercise real, non-trivial
     component signs — reuses P1's own `minimal_cube` v0 value,
     `-0.577350269 -0.577350269 -0.577350269`, so the round-trip test
     is not accidentally uniform-sign-blind); the version-1 case is
     unchanged; the version-"3" case's own literal becomes `4` (Plan
     Review finding 3); the two field-count boundary cases become P5's
     own three; three new cases are added — `NonUnitNormal` for a
     zero-vector normal, for a grossly unnormalized one (`1 1 1`), and
     for a value the smallest representable step outside
     `[0.9801, 1.0201]`; a `-0.0`-component case confirming acceptance.
   - `tests/asset_system/mesh_artifact_tests.cpp`: P3's own exact byte-
     offset changes; the pinned-byte-vector test's own expected vector
     recomputed per P3; two new decode-time `NonUnitNormal` cases
     (mirroring the existing `NonFiniteFloat` pair's own shape —
     corrupt the normal region's own bytes at offset `72`–`83` to a
     finite-but-wrong-magnitude pattern) — independently reachable from
     the source-level cases, never merely re-asserting the same input.
   - `tests/asset_system/static_mesh_asset_data_tests.cpp`: confirmed
     to require **no** change — its own existing tests must continue
     passing unmodified against the wider stride, itself a regression
     assertion (matching Plan 0017's own identical precedent for this
     file).
   - The remaining seven embedded-mesh-source-text files
     (`load_tests.cpp`, `mesh_uv_round_trip_tests.cpp`,
     `material_realization_gpu_tests.cpp`, `scene_load_tests.cpp`,
     `scene_manifest_tests.cpp`, `cook_command_tests.cpp`'s own two
     accept-path constants, `cooker_determinism_tests.cpp`): version
     line → `3`, vertex line(s) widened to 11 fields with a P2 normal;
     `cook_command_tests.cpp`'s own version-1 CLI-rejection case is
     unchanged (Plan Review Round 1 item 3).
   - New `tests/asset_system/mesh_normal_round_trip_tests.cpp` (P6),
     registered in `tests/asset_system/CMakeLists.txt` alongside its
     existing `mesh_uv_round_trip_tests.cpp` entry.
   - Extend `tests/tools/asset_cooker/cooker_determinism_tests.cpp`'s
     own existing double-cook test to a normal-bearing source (same
     test, wider fixture, no new mechanism — mirrors Plan 0017's own
     identical UV0-era extension).
   - Extend `tests/tools/asset_cooker/cook_command_tests.cpp` with a
     real-CLI case cooking a version-2-labeled source (now itself
     rejected, exactly like version 1) and asserting rejection
     (non-zero exit, no artifact written) — the direct successor to
     this file's own existing version-1 case, now that version 2 is
     also superseded.
   - **Acceptance gate for this Milestone:** `minimal_cube`,
     `world_scene`, `textured_quad`, and `material_demo`'s own existing
     image-regression goldens each match with zero pixel difference
     (Debug and Release), and all six composition-root executables
     build and their own existing GPU tests pass unmodified in outcome.
2. **Full verification pass.** No new source change of its own — runs
   the complete Verification Checklist below end to end (both
   `ATLANTIS_BUILD_TESTS` configurations, both build configurations,
   `ctest -LE gpu`/`-L gpu`, Vulkan Validation Layers scan, module/link
   graph, all four golden SHA-256 checks, `git diff --check`) and
   records the real, final numbers. If any gate fails, Implementation
   returns to Milestone 1, not forward.
3. **Documentation/registry closeout.** [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
   own `Related Plan(s)` field updated to link this Plan and record its
   own final status; `specs/README.md`'s Spec 0020 row and Spec 0019
   row both updated to reflect this Plan's own real Implementation
   state (per D10 — the gate that matters for Spec 0019 is this Plan's
   own **Implementation PR merging**, not this Plan reaching `Approved`
   alone); this Plan's own "Post-Merge Status Update" section (added at
   merge time, matching every recent Plan's own established closeout
   pattern) records the real, final verification numbers from
   Milestone 2. No Lighting-related content of any kind is added
   anywhere in this Milestone — Spec 0019's own transform math, `Light`
   components, `LitTextured` Material, frame lighting uniform, shader,
   or golden remain entirely un-started, exactly as this Plan's own
   Non-Goals require.

## Files / Modules Touched (expected)

- `src/asset_system/include/atlantis/asset_system/mesh_source.h`,
  `src/asset_system/src/mesh_source.cpp` (Milestone 1)
- `src/asset_system/include/atlantis/asset_system/mesh_artifact.h`,
  `src/asset_system/src/mesh_artifact.cpp` (Milestone 1)
- `assets/meshes/minimal_cube.mesh.txt`,
  `assets/meshes/textured_quad_left.mesh.txt`,
  `assets/meshes/textured_quad_right.mesh.txt` (Milestone 1)
- `src/runtime/src/runtime_application.cpp`,
  `tests/image_regression/fixture/material_demo_fixture.cpp`,
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`,
  `tests/image_regression/fixture/textured_quad_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_loaded_fixture.cpp`
  (Milestone 1 — `Vertex` struct + six `static_assert`s + up to two new
  includes each (`mesh_artifact.h`, `<type_traits>` where not already
  present), no other change)
- `tests/asset_system/mesh_source_tests.cpp`,
  `tests/asset_system/mesh_artifact_tests.cpp` (Milestone 1)
- New `tests/asset_system/mesh_normal_round_trip_tests.cpp` (Milestone 1, P6)
- `tests/asset_system/CMakeLists.txt` (Milestone 1 — one new test-file
  entry for the file above)
- `tests/asset_system/load_tests.cpp`,
  `tests/asset_system/mesh_uv_round_trip_tests.cpp`,
  `tests/runtime/material_realization_gpu_tests.cpp`,
  `tests/runtime/scene_load_tests.cpp`,
  `tests/runtime/scene_manifest_tests.cpp`,
  `tests/tools/asset_cooker/cook_command_tests.cpp`,
  `tests/tools/asset_cooker/cooker_determinism_tests.cpp` (Milestone 1
  — existing hardcoded mesh-source literals only, updated to the new
  grammar; no change to what any of these files actually tests)
- [specs/0020-mesh-normal-attribute-foundation.md](../specs/0020-mesh-normal-attribute-foundation.md)
  (Milestone 3 — `Related Plan(s)` field only)
- `specs/README.md` (Milestone 3)
- This file's own "Post-Merge Status Update" section (Milestone 3, at
  merge time)

**Not touched by this Plan** (confirmed by the list above containing no
entry under any of these paths, and by Pre-draft verification's own
explicit per-file confirmation above): `src/world/`,
`src/renderer/include/atlantis/renderer/` (public headers),
`src/rhi/`, `src/render_graph/`, `src/shader_system/`, any `.slang`
shader file, `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`,
`tests/runtime/material_realization_gpu_tests.cpp`'s own `Vertex`
struct specifically (the file itself is touched, for its embedded
source text only — Pre-draft verification), `tests/image_regression/goldens/`
(any file), `adr/` (no new ADR — ADR-0063/ADR-0045/ADR-0058 are already
`Accepted`/`Accepted Amendment`, nothing further to record).

## Sequencing & Dependencies

Milestone 1 → Milestone 2 → Milestone 3, strictly — Milestone 1 is a
single atomic step internally (above), so there is no finer sequencing
within it. Milestone 2 depends on Milestone 1's own real, checked-in
output (nothing to verify before it exists). Milestone 3 depends on
Milestone 2's own passing verification (registry/status updates record
a real, confirmed outcome, never an anticipated one).

Depends on [Spec 0017](../specs/0017-mesh-uv-attribute-foundation.md)
(Mesh UV Attribute Foundation, `Approved`, implemented — the exact
mechanical pattern and every constant/function this Plan widens rather
than replaces) and [ADR-0063](../adr/0063-static-mesh-normal-attribute-schema-version-and-convention.md)/
the [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)/[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
Accepted Amendments (all `Accepted`, 2026-08-29), all already satisfied
on `main`. This Plan's own Implementation PR merging is the one event
[Spec 0019](../specs/0019-lighting-foundation.md)'s own Plan gate is
waiting on — no other Plan depends on this one.

## Verification Checklist

Maps directly to [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)'s
own Testing & Verification Plan, made concrete and numbered:

- [ ] V1 — `parseMeshSource()`/`serializeMeshSource()` round-trip with
      every real P1 value (`minimal_cube`'s eight, both quads' `0 0 1`).
- [ ] V2 — Version 3 acceptance; version 2 and version 1 rejection
      (`UnknownSourceVersion`), both at parse time.
- [ ] V3 — 11-field acceptance; P5's own three field-count boundary
      cases (`CountMismatch`).
- [ ] V4 — `NonFiniteFloat` on a normal component (`NaN` and `Inf`
      each), confirmed to run before either `detail::` function (P7) is
      ever called.
- [ ] V5a — **Kind 1 (P8), pure comparator:**
      `detail::isNormalLengthSquaredInTolerance()` called directly with
      `0.9801`/`1.0201` (both **accepted**) and
      `std::nextafter(0.9801, 0.0)`/`std::nextafter(1.0201, 2.0)` (both
      **rejected**) — no `float` component or summation involved.
- [ ] V5b — **Kind 2 (P8), real `float` components, unambiguous
      margin:** `detail::computeNormalLengthSquared()` +
      `isNormalLengthSquaredInTolerance()` with P1's own
      `(0.577350269, 0.577350269, 0.577350269)` (clearly inside), `(1, 1,
      1)` (clearly outside), a `-0.0f` component (accepted, behaves as
      `+0.0f`), and an extremely small non-zero vector (`~1e-20` per
      component, rejected) — this layer makes **no** claim about
      landing on the exact `0.9801`/`1.0201` value; that is V5a's own
      job alone.
- [ ] V5c — **Kind 3 (P8), full parse-time integration:**
      `parseMeshSource()` with real 11-field source text reproducing
      V5b's own cases end to end, confirming the real
      `NonUnitNormal`/acceptance outcome through the whole function, not
      only the isolated `detail::` pair.
- [ ] V6 — A pinned-expected-byte-vector test for `encodeMeshArtifact()`
      at the new 44-byte stride/90-byte total size (P3) — the checked-in
      expected vector is a compile-time constant/literal, computed once
      by an independent tool at Implementation time and transcribed;
      **the test itself never calls `encodeMeshArtifact()` to produce
      its own expected value** (Plan Review Round 2 item 7).
- [ ] V7 — `decodeMeshArtifact()` rejecting schema version 1, 2, and 4
      (not 3 — Plan Review Round 1 item 2); every stride other than 44
      (including 32 and 24); truncated input; inconsistent offsets;
      out-of-range index at the new offset (P3, bytes `84`/`85`, not the
      old `72`/`73` — confirmed the corrected offset targets the
      *index* region, not accidentally the normal region now occupying
      bytes `72`–`83`).
- [ ] V8a — `NonUnitNormal` at decode time (Kind 2-equivalent): a real,
      hand-corrupted artifact byte buffer whose normal region decodes to
      a finite value outside `[0.9801, 1.0201]` — independently
      reachable, never merely re-running the parse-time case.
- [ ] V8b — Confirmed the *same*, shared `detail::` pair (P7) is what
      both V5a–c and V8a exercise — not two independent
      implementations that could silently drift apart.
- [ ] V9 — `mesh_normal_round_trip_tests.cpp` (P6): a real
      `cookStaticMesh()` → `loadStaticMeshAsset()` round-trip for a
      real P1 normal value, bit-for-bit.
- [ ] V10 — Deterministic double-cook: `cookStaticMesh()` invoked twice
      against identical, normal-bearing source text produces
      byte-identical artifacts (`cooker_determinism_tests.cpp`) —
      confirmed this holds because `encodeMeshArtifact()` always
      serializes the author's own original float bits unchanged (P7),
      not because of any property of the numeric-contract check itself.
- [ ] V11 — Re-import triggering: the existing content-driven re-cook
      mechanism, re-verified against all three real mesh assets under
      the new grammar.
- [ ] V12 — Hand-computed math test (D6): all four independently-
      computed triangle-level face-normal results (both triangles, both
      quads) equal `(0, 0, 1)`.
- [ ] V13 — All six real composition-root touch points compile
      (including their own six new `static_assert`s each —
      `is_standard_layout_v` plus four offsets plus `sizeof`, P4) and
      their own existing test suites pass, outcome-unmodified, against
      the widened 44-byte `Vertex`.
- [ ] V13a — Confirmed no composition root's own `MeshVertexAttributeSchema`/
      shader-reflection wiring changed as a consequence of `Vertex`
      gaining `normal` — each of the six still lists only the
      attributes its own real, unmodified shader reflects; **zero** new
      `[[vk::location(N)]]` declaration in any `.slang` file (restates
      V22 below at the composition-root level specifically, Plan Review
      Round 2 item 6).
- [ ] V14 — All nine real touch points confirmed updated and passing, in
      their own two categories (Spec 0020's own "Human Review
      Correction — 2026-08-29"): the eight embedded-mesh-source-text
      files (Pre-draft verification's own exact list — including
      `mesh_source_tests.cpp`'s own version-1-unchanged and
      version-4-not-3 cases, and `cook_command_tests.cpp`'s own
      version-1-unchanged case plus its new version-2-rejection case)
      plus `mesh_artifact_tests.cpp` (its own direct-`MeshSourceVertex`-
      construction category, P3).
- [ ] V15 — Fresh `ATLANTIS_BUILD_TESTS=ON` configure/build, both Debug
      and Release: `ctest -LE gpu` and `ctest -L gpu` both pass, real
      numbers recorded (not merely "passing").
- [ ] V16 — Fresh `ATLANTIS_BUILD_TESTS=OFF` configure/build: all three
      real mesh assets re-cook successfully against the new grammar;
      `atlantis_runtime.exe` links with zero `tests/` dependency in
      that tree.
- [ ] V17 — Vulkan Validation Layers: zero `VUID`/`Validation Error`/
      `Validation Warning` across the full `ctest -L gpu` verbose log,
      both configurations.
- [ ] V18 — The four existing goldens (`minimal_cube`, `world_scene`,
      `textured_quad`, `material_demo`) confirmed **byte-for-byte** —
      PNG and sidecar SHA-256 identical to `main` — and, independently,
      **pixel-for-pixel** zero-difference against a fresh capture, both
      configurations; the golden generator is **not** run.
- [ ] V19 — Module/link graph: `Atlantis::AssetSystem` still links
      `Atlantis::Core` only.
- [ ] V20 — C4062/error-domain re-check, repository-wide, not merely
      re-asserted from the Spec: confirmed neither `SourceParseError`
      nor `ArtifactDecodeError` gained a new exhaustive-switch or
      `toString()`-shaped consumer anywhere in `src/` or `tests/` during
      Implementation — a fresh `switch.*SourceParseError`/
      `switch.*ArtifactDecodeError`/`toString(SourceParseError`/
      `toString(ArtifactDecodeError` search, zero matches expected; no
      `/w14062` probe is applicable if that holds.
- [ ] V21 — `git diff --check` clean; no stray whitespace.
- [ ] V22 — No new `.slang` file, no new golden, no new RHI/RenderGraph/
      Renderer public API symbol anywhere in the real diff (a direct,
      mechanical grep-based confirmation, not an assumption).

## Rollback Plan

Format version bumps in this codebase are, by design, not
independently revertible in place once real content depends on them —
identical in kind to Plan 0017's own UV0 bump and Plan 0012's own
original format. If a real defect is found after this Plan's own PR
merges: (1) before [Spec 0019](../specs/0019-lighting-foundation.md)'s
own Plan begins, revert this Plan's own PR wholesale via a normal `git
revert` (a clean revert is possible as long as no later commit has
built on top of the version-3 format — true immediately after merge,
and remains true for as long as no Spec 0019 work has started, since
this Plan's own Non-Goals guarantee nothing downstream depends on
normal data yet); (2) after Spec 0019 work has begun consuming the
version-3 format, a straight revert is no longer safe — the fix must be
a forward, disclosed correction (a new commit, or a follow-up Spec
Amendment if the defect is a real design gap, not merely a bug),
matching this repository's own general "specs/ADRs are corrected
forward, not silently reverted once real work depends on them"
discipline. No golden is touched by this Plan (V18), so no golden-side
rollback concern exists either way.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: none beyond what the Verification
Checklist above already enumerates in full — this Plan adds no new
rendered capability, so the Definition of Done's own image-regression/
human-visual-review items apply only in their negative form here (V18:
confirm zero change, not review a new capture).
