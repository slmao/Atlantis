# Plan: Tangent-Space Normal Mapping Foundation

- **Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Approved`)
- **Status:** Approved / **Implementation complete in [PR #135](https://github.com/slmao/Atlantis/pull/135); pending merge** (not yet merged — this Plan is not "done" until a human merges that PR)
- **Author:** slmao
- **Human Review Approval (2026-09-06):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-09-06, against
  [PR #130](https://github.com/slmao/Atlantis/pull/130), naming all
  three items this approval covers individually, per this Plan's own
  explicit, itemized gate (below) — no blanket, single-statement
  approval: **(1)** the
  [Spec 0029 Human Review Correction](../specs/0029-tangent-space-normal-mapping-foundation.md#human-review-correction--2026-09-06-pbr_sphere-handedness-conflict-count)
  — approved (the real `48/425` conflict count, its location at the
  north-pole ring 0 and adjacent ring 1, and zero conflicts at the
  south pole or any other ring); **(2)** the
  [ADR-0073 Accepted Correction](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md#accepted-correction--2026-09-06-pbr_sphere-handedness-conflict-count-and-location)
  — approved and moved from `Proposed` to `Accepted` (the deterministic
  sign-split method, one new vertex per conflicting vertex, 425→473
  vertices, 768 triangles/2304 indices unchanged, and the `0`
  UV-degenerate/`0` handedness-conflict acceptance gate); **(3)** this
  Plan itself — approved in full: the seven Milestones and their
  atomic ordering, the mesh/material schema and migration tables, the
  descriptor/shader/`Material`/Runtime contracts, the fixed 4×4 RGB8
  normal-map texture and its `stb_image_write`-based temporary
  generation method, the fixture's own `DrawItem`-material-pointer
  A/B substitution, the fixed demo scene/camera/light/shadow-receiver
  geometry, both discriminative pixels and thresholds (normal-map
  `(256,256)` `>50`; shadow `(198,273)` `>15` — both explicitly
  accepted as **not yet measured on real GPU hardware for this exact
  scene**, each carrying its own non-negotiable "stop and request
  Human Review, never silently retune" rule), the two-phase golden
  gate (untracked candidate, then human-approved commit), the
  byte-identical requirement on all 9 existing goldens, and the
  mirrored/negative-determinant handedness limitation staying an
  accepted, deferred limitation, not solved by this Plan. **This
  approval authorizes Implementation of this Plan only once
  [PR #130](https://github.com/slmao/Atlantis/pull/130) itself has
  merged to `main` — not before.**
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  Batch 8 PR (pending). Original scope, all seven Milestones, and the
  full Verification Checklist retained; every chained "Human Review
  Approved Plan Correction"/"Human-Approved Implementation Deviation"/
  "Milestone 7 Closeout"/"Hash-Citation Correction" section is preserved
  in full, unedited.

## Objective

Implement Spec 0029 in full: a cooker-generated mesh tangent attribute
(schema 3→4, 44→60 bytes, ADR-0073), a cook-time fallback tangent for
`minimal_cube` (zero source change) and a real, sign-grouped split
migration for `pbr_sphere` (the only mesh needing one — corrected
count and location below, accepted by Human Review), a per-material
optional normal-map texture on both the direct-lit and IBL
`PbrDirectLit` paths (material schema 2→3, ADR-0074), a single-pointer
`Material` API extension with two mechanically-checked preconditions,
three widened Vulkan-Backend capacity limits (ADR-0072's own Accepted
Amendment), two new shaders, and one new, independent, combined
normal-map+IBL+sky+shadow demo with fixed, computed discriminative
pixels/thresholds for both the normal-map effect and the shadow
effect. Every no-normal-map material's runtime behavior, shader
selection, and descriptor-binding results stay unchanged; all 9
existing goldens stay byte-identical.

**This Plan depended on two corrections, filed alongside it — both now
`Accepted`/Human-Review-approved, named individually in this Plan's
own Human Review Approval record above** — see "Corrections this Plan
depends on" below for the full derivation.

## Corrections this Plan depends on (accepted 2026-09-06)

A temporary, uncommitted re-audit of `pbr_sphere.mesh.txt`, run
strictly to ADR-0073's own already-Accepted handedness formula
(`h_face = sign(dot(cross(vertexNormal, T_face), B_face))`, raw
`T_face`/`B_face`, no orthogonalization before the sign check), found
the real conflict count is **48 of 425 vertices (11.3%)**, not
`96/425 (22.6%)` as ADR-0073/Spec 0029 originally stated, and the real
location is the north-pole ring (ring 0) plus its one immediately-
adjacent ring (ring 1) — not "pole rings" (plural) and not the south
pole, which has zero conflicts. This Plan did not silently use the
corrected figures without disclosure: a concise
[`Accepted Correction — 2026-09-06`](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md#accepted-correction--2026-09-06-pbr_sphere-handedness-conflict-count-and-location)
is appended to ADR-0073, and a matching
[`Human Review Correction`](../specs/0029-tangent-space-normal-mapping-foundation.md#human-review-correction--2026-09-06-pbr_sphere-handedness-conflict-count)
to Spec 0029 — both accepted by Human Review on 2026-09-06 (named
individually in this Plan's own header, above), neither rewriting any
existing Decision/Requirement. **This Plan uses the corrected figures
(48/425, the exact ring locations, the simpler 2-way sign-split
method) throughout — P4 below is built on the accepted correction, not
on the original `96/425` estimate.**

**Governance record — no implied approval was relied upon.** The
approval of this Plan did **not**, by itself, stand in for approving
either correction. Three separate items each received their own
explicit approval, recorded together in one pass but named
individually (this Plan's own header, above): (1) the Spec 0029
correction, (2) the ADR-0073 correction, (3) this Plan itself.

## Pre-draft verification against real, current source

Re-confirmed directly against `main` at Plan-drafting time (2026-09-06)
via `rg` across the whole repository — not reused from the Spec/ADRs'
own citations without independently re-checking.

- `mesh_source.h`/`.cpp`: `MeshSourceVertex` is 11 fields (position/
  color/uv/normal); `kVersionLine = "atlantis_static_mesh_source_version: 3"`;
  `parseMeshSource()` requires exactly 11 fields per `vertex:` line,
  finiteness-then-`detail::isNormalLengthSquaredInTolerance()` order.
  `cookStaticMesh()` (`cook.cpp:82-104`) is `parseMeshSource()` →
  `encodeMeshArtifact()`, nothing between them today.
- `mesh_artifact.h`: `kMeshArtifactSchemaVersion = 3`,
  `kMeshArtifactVertexStrideBytes = 44`, four named offset constants
  (position 0, color 12, UV0 24, normal 32), `kMeshArtifactHeaderSizeBytes = 40`.
  `encodeMeshArtifact(AssetId, const ParsedMeshSource&)` — no tangent
  parameter today. `decodeMeshArtifact()` re-validates finiteness then
  `NonUnitNormal` per vertex.
- `errors.h`: `CookError` has exactly 5 enumerators; `ArtifactDecodeError`
  has 11; `MaterialSourceParseError` (`material_source.h`, not
  `errors.h`) has 8; `MaterialCookError` has 6;
  `MaterialArtifactDecodeError` has 9; `RuntimeInitError`
  (`runtime/init_error.h`) ends at `ShadowLightSpaceBufferCreateFailed`,
  27 enumerators.
- `atlantis::rhi::VertexAttributeFormat` (`rhi/types.h:95-98`) and
  `atlantis::shader_system::VertexAttributeType`
  (`reflection_metadata.h:16-19`) each declare only `Float3, Float2`.
  **Two further real, `rg`-found touch points ADR-0073 named only in
  the abstract ("RHI/reflection addition"), not by file:**
  `slang_json_transform.cpp:109-121`'s own
  `vertexAttributeTypeFromTypeNode()` hardcodes
  `elementCount != 3 && elementCount != 2` as its own rejection
  condition (raw slangc JSON → `VertexAttributeType`, the actual
  authoritative parse point); `reflection_loader.cpp:61-74`'s own
  `parseVertexAttributeType()`/`vertexAttributeTypeToString()` hardcode
  the `"float3"`/`"float2"` string pair (this tool's own normalized
  `.refl.json` sidecar format). `vertex_input_mapping.cpp:10-18`'s own
  `toRhiFormat()` switches on the same two values. All four need a
  `Float4`/`"float4"`/`elementCount==4` case — five files total, not
  the two the ADR names by type alone.
- **Material side**, all read in full: `material_types.h`,
  `material_source.h`/`.cpp` (`kMinLineCount=5`/`kMaxLineCount=8`, both
  confirmed real today), `material_artifact.h`/`.cpp`
  (`kMaterialArtifactSchemaVersion = 2`, 56-byte fixed record),
  `material_metadata.h`/`.cpp` (`kExpectedLineCount = 8`,
  unconditional), `cook_material.cpp`, `load_material.cpp` (all real
  bodies read in full).
- `scene_load.cpp:130-175` (`loadAndInstantiateScene()`'s own Phase 2
  material/texture loop) is the exact, real insertion point for the
  new Unorm cross-validation, mirroring `PbrBaseColorTextureNotSrgb`'s
  own existing block verbatim in shape. **Confirmed: Phase 1's own
  `distinctMaterialIds` collection (`scene_load.cpp:53-69`) walks only
  `node.renderable->materialAsset` — a material declared in the scene's
  own manifest but referenced by zero nodes is never looked up here**,
  the real mechanism P17's own control-material handling below relies
  on.
- `material.h`/`material.cpp`, `renderer.cpp:92-131`,
  `material_realization.h`/`.cpp`, `descriptor_contract.cpp:45-63`,
  `vulkan_device.cpp:437-438,1000-1002`, `vulkan_command_list.h:177`,
  `compile_and_validate.cpp:136-213,366-374`,
  `runtime_application.cpp:372-389,1184-1185`, `bootstrap_config.h:54-85`,
  `shaders/pbr_ibl/CMakeLists.txt` — all read in full, unchanged from
  the previous round's own citations (see that round's own findings,
  restated in P6/P8/P10/P11/P14/P15/P16 below).
- **All real `struct Vertex { ... }` sites** (`rg 'struct Vertex \{'`,
  25 files, 14 doc/plan/ADR matches excluded) and **all real
  `createMaterial(` call sites** (`rg`, 14 real, non-doc files) —
  unchanged from the previous round, see P12/P13.
- `pbr_render_gpu_tests.cpp:97-134`, `assets/materials/*.material.txt`
  (all 6 read), `atlantis_add_material_asset()`
  (`src/asset_system/CMakeLists.txt:345-391`, full function read) —
  unchanged from the previous round, see P7/P9/P12/P13/P18.
- `pbr_direct_lit.slang`/`pbr_ibl.slang`/`output_transform_srgb.slang`
  (all read in full) — the exact BRDF/tonemap formulas P17's own
  discriminative-pixel computations below transcribe verbatim.
- **New this round:** `src/world/src/world.cpp:38-95` (full section
  read) — `Mat4` is **column-major, index `col*4+row`**
  (`multiply()`'s own header comment, ADR-0050); `rotationX(theta)`
  gives column2 `(0, -sin(theta), cos(theta))`; `rotationY(theta)`
  gives column2 `(sin(theta), 0, cos(theta))`; Euler order is
  `R = Ry(yaw) * Rx(pitch) * Rz(roll)` (`eulerRotation()`, ADR-0050's
  own fixed order) — the exact, real basis for every camera/light
  rotation-to-direction computation in P17 below, not assumed from the
  `-column2` identity-rotation case alone.
- `src/renderer/include/atlantis/renderer/draw_item.h:17-21` (full
  struct read): `DrawItem` is exactly `{const Mesh* mesh, const
  Material* material, std::array<float,16> objectToWorld}` — plain,
  borrowed, non-owning pointers, trivially copyable — the exact,
  confirmed basis for P17's own "copy the DrawItem list, swap one
  `material` pointer" mechanism (no new type, no new field).
- `assets/meshes/ground_plane.mesh.txt` (full file read): a `10×10`
  flat quad at `y=0`, `x,z ∈ [-5,5]`, normal `(0,1,0)` — the real
  receiver geometry P17's own shadow test places under the sphere.
- `assets/scenes/integrated_showcase_demo.scene.txt` (full file read):
  confirms this repository's own real, already-golden-backed
  convention for combining a ground plane, spheres, a directional
  light, and an elevated/pitched camera in one scene — consulted for
  precedent shape, not copied verbatim (its own camera/light numbers
  are specific to its own five-sphere layout and do not transfer to
  this Plan's own single-sphere, hand-computed-pixel demo).
- ADR-0042's own "Initial baseline bootstrap" amendment (read in full,
  `adr/0042-...md`, item 4): **"the golden PNG and sidecar are added
  via their own separate, subsequent commit"** — the real, established
  basis for this Plan's own split between generating an untracked
  candidate (Milestone 5) and committing the reviewed golden
  (Milestone 6), not an invented gate.

### Existing mesh sources needing a real audit re-run (ADR-0073 Decision item 9, as corrected)

Re-run at Plan-drafting time with the identical algorithm (per-triangle
UV-Jacobian determinant + per-vertex handedness, `h_face` on raw
`T_face`, matching ADR-0073's own Decision text exactly):

| Mesh | Triangles | UV-degenerate | Handedness conflicts | Action |
|---|---|---|---|---|
| `ground_plane.mesh.txt` | 2 | 0/2 | 0/4 | None |
| `textured_quad_left.mesh.txt` | 2 | 0/2 | 0/4 | None |
| `textured_quad_right.mesh.txt` | 2 | 0/2 | 0/4 | None |
| `minimal_cube.mesh.txt` | 12 | 12/12 | 0/8 | None — cook-time fallback (Milestone 1) |
| `pbr_sphere.mesh.txt` | 768 | 0/768 | **48/425** (corrected) | **Sign-split migration required** (Milestone 1) |

**Precise conflict location** (17 latitude rings of 25 vertices each,
`y` from `-1` to `1`; vertex indices 0-24 are the north-pole ring,
25-49 the adjacent ring): 24 of 25 vertices conflict at `y=1.0`
(indices 0-23; index 24, the seam-closure duplicate, does not), and 24
of 25 at `y=0.980785` (indices 26-49; index 25 does not). **Zero
conflicts at the south pole (`y=-1.0`) or any other ring** — the two
poles are not symmetric under this mesh's own real triangulation
winding. Triangles 0-95 (of 768) touch a conflicting vertex.

**A real, non-pole vertex used for the new demo's discriminative
sample point (P17) was independently identified and computed:** vertex
index 200 — position `(0, ~0, 1)`, normal `(0, ~0, 1)`, UV `(0, 0.5)` —
has exactly 3 contributing triangles, all agreeing at handedness
`-1.0` (no conflict — it is at latitude ring 8, `y=0`, nowhere near
either affected ring). Its own accumulated-and-orthogonalized tangent:
`T = (1, 0, 0)` (to 1e-16), `tw = -1.0`, `B = cross(N,T) * tw = (0,
-1, 0)`. Unaffected by the migration (only ring 0, the north-pole ring,
and ring 1, its adjacent ring — using the same vertex-index-order
numbering as "latitude ring 8" above, ring 0 = indices 0-24 — change);
this basis is stable across Implementation.

## P1. Mesh tangent-generation module — exact shape

New file pair: `src/asset_system/include/atlantis/asset_system/mesh_tangent_generation.h`,
`src/asset_system/src/mesh_tangent_generation.cpp`.

```cpp
// mesh_tangent_generation.h
struct VertexTangent { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };

[[nodiscard]] atlantis::Result<std::vector<VertexTangent>, CookError> generateTangents(
    const ParsedMeshSource& source);
```

`generateTangents()` returns `CookError` directly (the same enum
`cookStaticMesh()` already returns) — no wrapper enum, since
`CookError::DegenerateTangentBasis`/`TangentHandednessConflict` are
themselves `CookError` values per ADR-0073's own Decision.

`encodeMeshArtifact()`'s signature widens:

```cpp
[[nodiscard]] std::vector<std::byte> encodeMeshArtifact(
    AssetId assetId, const ParsedMeshSource& source, const std::vector<VertexTangent>& tangents);
```

with `ATLANTIS_CHECK(tangents.size() == source.vertices.size())` as
the function's own precondition.

`cookStaticMesh()` (`cook.cpp`) gains exactly one new step, between the
existing `parseMeshSource()` call and `encodeMeshArtifact()`:

```cpp
const auto tangentsResult = generateTangents(parsed);
if (tangentsResult.isErr()) return ResultT::Err(tangentsResult.error());
const std::vector<byte> artifactBytes = encodeMeshArtifact(assetId, parsed, tangentsResult.value());
```

**Fallback (item 4a) and handedness-conflict (item 4/5) are both
implemented inside `generateTangents()` itself**, per ADR-0073's own
Decision — one pass over all triangles builds the shared per-vertex
accumulator state both need.

## P2. `errors.h` — five new enumerators, exact placement

```cpp
enum class CookError {
  SourceFileUnreadable, SourceParseFailed, LogicalPathInvalid,
  ArtifactWriteFailed, MetadataWriteFailed,
  DegenerateTangentBasis, TangentHandednessConflict,  // new
};

enum class ArtifactDecodeError {
  TooSmallForHeader, BadMagic, UnknownSchemaVersion, UnsupportedVertexStride,
  InconsistentOffsets, SizeMismatch, VertexCountOutOfRange,
  IndexCountNotMultipleOfThree, IndexOutOfRange, NonFiniteFloat, NonUnitNormal,
  NonUnitTangent, NonOrthogonalTangent, InvalidTangentHandedness,  // new
};
```

`material_source.h`'s own `MaterialSourceParseError` gains one
(`NormalMapNotSupportedForKind`); `runtime/init_error.h`'s own
`RuntimeInitError` gains one, appended after
`ShadowLightSpaceBufferCreateFailed` (`PbrNormalMapTextureNotUnorm`);
`init_error.cpp`'s own `toString()` gains the matching `case`.

## P3. Fixed epsilons and fallback-axis tie-break (restated from ADR-0073, not re-derived)

- UV-degeneracy: `|det| < 1e-12`.
- Orthogonalization-degeneracy: `< 1e-6` (`kDegenerateLengthEpsilon`).
- Handedness zero-tiebreak: `< 1e-9` → `+1.0`.
- Fallback axis selection (item 4a): smallest `|dot(N,axis)|` among
  `X,Y,Z`; tie-break order `X, Y, Z`.
- Decode-time orthogonality: `|dot(N,T)| < 1e-3`.

## P4. `pbr_sphere.mesh.txt` migration — corrected method, real simulated result, audit command, acceptance gate

**Method, corrected from this Plan's own prior draft (2-way sign
split, not "one copy per triangle wedge"):** `h_face` is binary
(`±1`), so each of the 48 real conflicting vertices needs **exactly
one** additional copy, not one per wedge. For each conflicting vertex:
group its own contributing triangle corners by their own recorded
`h_face` sign (exactly two groups, by construction — no vertex has
more than two distinct sign values, since there are only two possible
values); keep the original vertex index for one sign's group; append
one new vertex (position/color/UV/normal copied verbatim from the
original) and repoint the other sign group's own triangle corners to
it.

**Simulated on a temporary, uncommitted in-memory copy of the real,
current `pbr_sphere.mesh.txt`, using this exact method and re-audited
with the identical, unmodified algorithm:**

| | Vertices | Triangles | Indices | UV-degenerate | Handedness conflicts |
|---|---|---|---|---|---|
| Before | 425 | 768 | 2304 | 0/768 | 48/425 |
| After | **473** (425 + 48) | 768 (unchanged) | 2304 (unchanged) | 0/768 (unchanged) | **0/473** |

Only the vertex count grows, by exactly 48 — the triangle and index
counts are completely unaffected (each triangle still names 3
vertices; only which index it names changes for the 48 repointed
corners).

**Audit command (temporary, uncommitted, run by Implementation):** the
identical Python-shaped probe used at Plan-drafting time — parse the
mesh, compute per-triangle UV-Jacobian determinant and per-triangle-
per-vertex `h_face = sign(dot(cross(vertexNormal, T_face), B_face)))`
(raw `T_face`/`B_face`), flag any vertex whose own recorded `h_face`
values are not all equal. Re-run against the candidate (migrated) file
after applying the split above.

**Acceptance gate (mechanical, not subjective):** the re-run audit
must report `0/768` UV-degenerate triangles (unchanged) **and**
`0/473` handedness conflicts. If either does not hold, Implementation
stops and requests Human Review rather than inventing a different
splitting rule.

**Explicitly unaffected, confirmed by the migration's own scope:**
every one of the 377 already-non-conflicting vertices (425 − 48) keeps
its exact position/color/UV/normal values and its exact vertex index
— including vertex 200, the demo's own discriminative sample point,
which is at latitude ring 8 (`y=0`), nowhere near ring 0 (north pole)
or ring 1 (its adjacent ring).

**Dependency, satisfied:** this method and these figures depended on
the two corrections (see "Corrections this Plan depends on" above),
both accepted by Human Review on 2026-09-06 alongside this Plan
itself — Implementation of this Milestone may proceed using the
corrected figures throughout.

## P5. Material grammar — exact new parse/serialize shape

`material_source.cpp`'s `kMaxLineCount` becomes `9` (from `8`).
`parseMaterialSource()` gains, after the existing 5-or-8-line block:

```cpp
std::string normalMapLogicalPath;  // ParsedMaterialSource's new field
if (lines.size() == 9) {
  if (parsed.kind != MaterialKind::PbrDirectLit) {
    return ResultT::Err(MaterialSourceParseError::NormalMapNotSupportedForKind);
  }
  if (!matchField(lines[8], "normal_map: ", value)) return ResultT::Err(MaterialSourceParseError::FieldOrderMismatch);
  if (value.empty()) return ResultT::Err(MaterialSourceParseError::MissingField);
  parsed.normalMapLogicalPath = std::string(value);
}
```

Line-count acceptance widens from `{5, 8}` to `{5, 8, 9}`; anything
else keeps `MissingField` (`< 5`) or `TrailingContent` (`> 9`).
`serializeMaterialSource()` appends the 9th line only when
`normalMapLogicalPath` is non-empty. `ParsedMaterialSource` gains
`std::string normalMapLogicalPath;` (default empty).

## P6. Material artifact/metadata — exact byte/line changes

`material_artifact.h`: `kMaterialArtifactSchemaVersion = 3`;
`kMaterialArtifactHeaderSizeBytes = 64`. `encodeMaterialArtifact()`
gains a trailing `AssetId normalMapTexture` parameter, serialized via
the existing `appendU64LE()` at offset 56. `decodeMaterialArtifact()`
gains, after the existing `roughnessFactor` decode:
`decoded.normalMapTexture = readU64LE(bytes.data() + 56);` — no range
check (`0` = none, the established convention).
`DecodedMaterialArtifact`/`MaterialAssetData` each gain `AssetId
normalMapTexture = 0;`.

`material_metadata.h`: `MaterialMetadata` gains `AssetId
normalMapTexture = 0;`. `material_metadata.cpp`: `kExpectedLineCount`
becomes `9`; a new, **unconditional** 9th line,
`normal_map_texture: <16-hex-digit AssetId>` (matching
`texture_asset:`'s own hex encoding, `0000000000000000` when absent).

`cookMaterial()` gains, after the existing Step 3 (texture identity):
normalize `parsed.normalMapLogicalPath` via the identical
`normalizeLogicalPath()`/`computeAssetId()` pair (only when non-empty;
`0` otherwise); thread the result into both `encodeMaterialArtifact()`'s
new parameter and `metadata.normalMapTexture`.

`load_material.cpp`'s `loadMaterialAsset()` gains one more field in
its existing cross-validation `if`: `artifact.normalMapTexture !=
metadata.normalMapTexture`, and `data.normalMapTexture =
artifact.normalMapTexture;` in the final build.

## P7. Existing `.material.txt` migration — exhaustive, real list

All 6 real, committed material sources need exactly one line changed
(`atlantis_material_source_version: 2` → `3`), nothing else:

| File | Current lines | Kind | New normal map? |
|---|---|---|---|
| `unlit_textured_quad.material.txt` | 5 | UnlitTextured | No |
| `lit_textured_quad.material.txt` | 5 | LitTextured | No |
| `pbr_dielectric_rough.material.txt` | 8 | PbrDirectLit | No |
| `pbr_dielectric_smooth.material.txt` | 8 | PbrDirectLit | No |
| `pbr_metallic_rough.material.txt` | 8 | PbrDirectLit | No |
| `pbr_metallic_smooth.material.txt` | 8 | PbrDirectLit | No |

Two new material sources are added (Milestone 5): `pbr_normal_mapped.material.txt`
(9 lines, `kind: pbr_direct_lit`, `normal_map:` present) and
`pbr_normal_mapped_control.material.txt` (8 lines, identical in every
other field, no `normal_map:` line) — the discriminative test's own
A/B twin pair (P17).

## P8. Scene dependency loading — exact `scene_load.cpp` change

Inserted immediately after the existing `PbrBaseColorTextureNotSrgb`
block (`scene_load.cpp:156-172`), inside the same per-material loop:

```cpp
if (materialAssetData.normalMapTexture != 0 && !textureDataMap.contains(materialAssetData.normalMapTexture)) {
  const auto* normalMapEntry = resolver.find(materialAssetData.normalMapTexture);
  if (!normalMapEntry) {
    ATLANTIS_LOG_ERROR("a material's own referenced normal-map texture AssetId has no manifest entry");
    return ResultT::Err(RuntimeInitError::SceneDependencyUnresolved);
  }
  auto normalMapResult = atlantis::asset_system::loadTextureAsset(normalMapEntry->artifactPath, normalMapEntry->metadataPath);
  if (normalMapResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadTextureAsset() failed for a material's own referenced normal-map texture");
    return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
  }
  textureDataMap.emplace(materialAssetData.normalMapTexture, std::move(normalMapResult.value()));
}
if (materialAssetData.normalMapTexture != 0) {
  const auto& normalMapData = textureDataMap.at(materialAssetData.normalMapTexture);
  if (normalMapData.colorSpace != atlantis::asset_system::TextureColorSpace::Unorm) {
    ATLANTIS_LOG_ERROR("PbrDirectLit material's own normal-map texture is not Unorm");
    return ResultT::Err(RuntimeInitError::PbrNormalMapTextureNotUnorm);
  }
}
```

Reuses the **same** `textureDataMap` the base-color texture already
populates — no new map, no new cache.

## P9. `atlantis_add_material_asset()` — exact CMake widening

```cmake
function(atlantis_add_material_asset)
  set(oneValueArgs NAME SOURCE TEXTURE NORMAL_MAP)  # NORMAL_MAP is new, optional
  ...
  if(ARG_NORMAL_MAP)
    if(NOT DEFINED ATLANTIS_${ARG_NORMAL_MAP}_ARTIFACT_PATH)
      message(FATAL_ERROR "atlantis_add_material_asset(${ARG_NAME}): NORMAL_MAP '${ARG_NORMAL_MAP}' is not a previously declared texture asset.")
    endif()
  endif()
  ...
  add_dependencies(${ARG_NAME}_asset ${ATLANTIS_${ARG_TEXTURE}_TARGET})
  if(ARG_NORMAL_MAP)
    add_dependencies(${ARG_NAME}_asset ${ATLANTIS_${ARG_NORMAL_MAP}_TARGET})
  endif()
endfunction()
```

Every existing call site (6, per P7's own table) omits `NORMAL_MAP` and
is unaffected.

## P10. Descriptor capacity — exact three-line diff (ADR-0072's own Accepted Amendment)

```cpp
// vulkan_device.cpp:438
poolSizes[1].descriptorCount = 5U * maxSets;  // was 4U

// vulkan_device.cpp:1000-1002
ATLANTIS_CHECK(params.sampledTextureBindingCount == 0 || params.sampledTextureBindingCount == 1 ||
               params.sampledTextureBindingCount == 2 || params.sampledTextureBindingCount == 3 ||
               params.sampledTextureBindingCount == 4 || params.sampledTextureBindingCount == 5);  // added == 5
```

```cpp
// vulkan_command_list.h:177
std::array<TextureDescriptorMemo, 6> textureDescriptorMemos_{};  // was 5
```

## P11. New descriptor contracts and shader-compiler contract wiring — exact diff

`descriptor_contract.h`/`.cpp` gain two functions:

```cpp
std::vector<DescriptorBinding> pbrDirectLitNormalMapExpectedDescriptorContract() {
  return {{0,0,UniformBuffer,Vertex}, {0,0,UniformBuffer,Fragment},
          {0,1,Sampler,Fragment}, {0,2,Sampler,Fragment}, {0,3,Sampler,Fragment}};  // 5 entries
}
std::vector<DescriptorBinding> pbrIblNormalMapExpectedDescriptorContract() {
  return {{0,0,UniformBuffer,Vertex}, {0,0,UniformBuffer,Fragment},
          {0,1,Sampler,Fragment}, {0,2,Sampler,Fragment}, {0,3,Sampler,Fragment},
          {0,4,Sampler,Fragment}, {0,5,Sampler,Fragment}};  // 7 entries
}
```

`compile_and_validate.cpp` (the real, `rg`-found touch point, not named
by either ADR): `validateDescriptorContractForStage()` gains two more
`else if` branches (`"pbr-direct-lit-normal-map"`,
`"pbr-ibl-normal-map"`); `validatePushConstantsForVertexStage()`'s own
`isPbr` widens to include both new strings; the fragment-stage
push-constant validation guard (line 372) widens identically.

## P12. Two new shader files — exact structure

`shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang` and
`shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.slang`, each with its
own `CMakeLists.txt` (mirroring `pbr_ibl/CMakeLists.txt` verbatim,
`EXPECTED_CONTRACT pbr-direct-lit-normal-map`/`pbr-ibl-normal-map`),
and one new `add_subdirectory()` line each in the root `CMakeLists.txt`.

Content (per ADR-0074 Section 5, exact diff from each shader's own
real, existing twin):

- `VertexInput` gains `[[vk::location(3)]] float4 tangent;`.
- A `normalMapSampler` `Sampler2D` binding, `[[vk::binding(3,0)]]`
  (direct) / `[[vk::binding(5,0)]]` (IBL).
- `vertexMain()` gains `output.worldTangent = mul((float3x3)pushConstants.objectToWorld,
  input.tangent.xyz); output.tangentHandedness = input.tangent.w;`,
  using the same submatrix/gate the existing `worldNormal` line uses.
- `fragmentMain()` gains, before the existing BRDF loops: `float3 N_geo
  = normalize(input.worldNormal); float3 T = normalize(input.worldTangent -
  N_geo * dot(N_geo, input.worldTangent)); float3 B = cross(N_geo, T) *
  input.tangentHandedness; float3 texelN = normalMapSampler.Sample(input.uv).rgb *
  2.0 - 1.0; float3 N = normalize(texelN.x * T + texelN.y * B + texelN.z *
  N_geo);` — `N` (not `N_geo`) is then used everywhere `N` already
  appears in the existing BRDF/IBL body, unchanged otherwise.
- No push-constant, uniform-buffer, or existing-binding change.

This Milestone's own new GPU test (P18) exercises these two shaders'
own widened capacity **directly**, via hand-fed vertex buffers
(`pbr_render_gpu_tests.cpp`'s own established pattern) — it does
**not** depend on `Material`/`RealizedMaterialCandidate`'s own API
extension (P14/P15, a later Milestone), since that file already
bypasses `Renderer::drawFrame()`'s own automatic binding logic and
calls `cmd.bindTexture()` directly.

## P13. New 5-sampler/`bindTexture(5, ...)` GPU test — exact location and shape

Extends `tests/runtime/pbr_render_gpu_tests.cpp`: after this file's own
existing `litOrPbrLayout()`, a new `pbrIblNormalMapLayout()` (location
0/1/2/3 = position/uv/normal/tangent, appended to this file's own
`Vertex` struct as `float tangent[4]`) and a new `TEST_CASE` that:
creates a Pipeline with `sampledTextureBindingCount == 5` and
`EXPECTED_CONTRACT "pbr-ibl-normal-map"` (via the existing
`createMaterial()` call, base-color texture/sampler only — no normal
map argument exists yet at this Milestone, and none is needed, since
this test binds texture 5 directly with `cmd.bindTexture()`, not
through `Material`'s own field); allocates a real descriptor set;
calls `cmd.bindUniformBuffer(...)`, `cmd.bindTexture(1, ...)`,
`cmd.bindTexture(2, ...)`, `cmd.bindTexture(3, ...)`,
`cmd.bindTexture(4, ...)`, and **`cmd.bindTexture(5, ...)`** (the one
call that exercises `textureDescriptorMemos_`'s own widened size);
submits a real draw of `kTriangleVertices` (widened with a fixed
`tangent` value, `(1,0,0,1)`); confirms success and zero Vulkan
Validation Layers hits.

## P14. `Material`/`createMaterial()` — exact diff

`material.h`:

```cpp
class Material {
 public:
  explicit Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline, MaterialPushConstantLayout pushConstantLayout,
                     const atlantis::rhi::SampledTexture* sampledTexture = nullptr,
                     const atlantis::rhi::Sampler* sampler = nullptr,
                     std::array<float, 4> baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f}, float metallicFactor = 1.0f,
                     float roughnessFactor = 1.0f,
                     MaterialEnvironmentBinding environmentBinding = MaterialEnvironmentBinding::None,
                     const atlantis::rhi::SampledTexture* normalMapTexture = nullptr) noexcept;  // new, trailing

  [[nodiscard]] const atlantis::rhi::SampledTexture* normalMapTexture() const noexcept { return normalMapTexture_; }
  // no normalMapSampler() -- sampler() already covers it (Section 1 item 5, ADR-0074)

 private:
  const atlantis::rhi::SampledTexture* normalMapTexture_ = nullptr;  // borrowed, never owned
};
```

`material.cpp`'s constructor body gains, after the existing
both-or-neither `ATLANTIS_CHECK`:

```cpp
ATLANTIS_CHECK(normalMapTexture_ == nullptr || sampledTexture_ != nullptr);
ATLANTIS_CHECK(normalMapTexture_ == nullptr || pushConstantLayout_ == MaterialPushConstantLayout::PbrDirectLit);
```

`createMaterial()` gains the identical trailing parameter.
`renderer.cpp`'s draw loop gains, after the existing shadow binding
block:

```cpp
if (item.material->normalMapTexture() != nullptr) {
  const std::uint32_t normalMapBinding = item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 5U : 3U;
  cmd.bindTexture(normalMapBinding, *item.material->normalMapTexture(), *item.material->sampler());
}
```

## P15. `RealizedMaterialCandidate`/`realizeOneMaterialCandidate()`/runtime plumbing — exact diff

`material_realization.h`:

```cpp
struct RealizedMaterialCandidate {
  AssetId materialAssetId = 0;
  AssetId textureAssetId = 0;
  std::unique_ptr<SampledTexture> newSampledTexture;
  std::optional<std::unique_ptr<Buffer>> stagingBuffer;
  AssetId normalMapTextureAssetId = 0;                                    // new
  std::unique_ptr<SampledTexture> newNormalMapTexture;                    // new
  std::optional<std::unique_ptr<Buffer>> normalMapStagingBuffer;          // new
  std::unique_ptr<Sampler> sampler;
  std::unique_ptr<Material> material;
};
```

`realizeOneMaterialCandidate()` gains, immediately after the existing
base-color texture/staging block and before `device.createSampler(...)`:
the identical dedup-then-create sequence, keyed by
`materialData.normalMapTexture` (skipped, leaving `newNormalMapTexture`
null, when that value is `0`). The `createMaterial()` call gains the
new trailing `normalMapTexturePtr` argument.
`realizePendingMaterials()` publishes `newNormalMapTexture` into the
**same** `sampledTextureResourceMap_` the base-color texture already
uses — no new map.

`selectShaderPair()`/`realizeOneMaterialCandidate()`/`realizePendingMaterials()`
each gain two more shader-pair trios as trailing parameters
(`pbrDirectLitNormalMap*`, `pbrIblNormalMap*`), inserted immediately
after the existing `pbrIbl*` trio. `selectShaderPair()`'s own
`PbrDirectLit` case becomes:

```cpp
case MaterialKind::PbrDirectLit:
  if (hasNormalMap) return environmentEnabled ? pbrIblNormalMapTriple : pbrDirectLitNormalMapTriple;
  return environmentEnabled ? pbrIblTriple : pbrDirectLitTriple;
```

`hasNormalMap` is a new `bool` parameter, threaded from
`materialData.normalMapTexture != 0` at the one real call site.
`sampledTextureBindingCountFor()` gains a `bool hasNormalMap`
parameter: `PbrDirectLit` returns `environmentEnabled ? (hasNormalMap
? 5U : 4U) : (hasNormalMap ? 3U : 2U)`.

`BootstrapConfig` gains 6 new `std::string` fields (3 per new shader
pair, mirroring `pbrIblVertexShaderSpirvPath`'s own shape).
`runtime_application.cpp` gains the matching load/store block (mirrors
lines 372-389) and threads both new trios into its own
`realizePendingMaterials()` call.

## P16. `createMaterial()` call-site migration — exhaustive, all 14 real sites

Every site compiles and behaves **unchanged** except the one marked
"Edited" — the new parameter defaults to `nullptr`.

| File | Change |
|---|---|
| `src/runtime/src/material_realization.cpp` | **Edited** — threads the new normal-map pointer (P15) |
| `src/runtime/src/runtime_application.cpp` | None directly (calls `realizePendingMaterials()`; gains the two new shader-trio parameters, P15) |
| `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` | None |
| `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp` | None |
| `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` | None |
| `tests/runtime/pbr_render_gpu_tests.cpp` | None for existing calls; new calls added for P13's own test (Milestone 3) |
| `tests/image_regression/shadow_gpu_tests.cpp` | None |
| `tests/image_regression/sky_background_gpu_tests.cpp` | None |
| `tests/image_regression/fixture/world_scene_loaded_fixture.cpp` | None |
| `tests/image_regression/fixture/textured_quad_fixture.cpp` | None |
| `tests/image_regression/fixture/world_scene_fixture.cpp` | None |
| `tests/image_regression/fixture/minimal_cube_fixture.cpp` | None |
| `examples/minimal_renderer_demo/main.cpp` | None |
| `examples/headless_rendering_demo/main.cpp` | None |

New fixture `pbr_normal_map_demo_fixture.cpp` (Milestone 5) is a
**new** call site, not a migration.

## P17. Vertex-layout/stride migration — exhaustive, all real `struct Vertex` sites

**Category A — real 44-byte mesh-consuming `Vertex` structs, widen to
60 bytes (append `float tangent[4];`) plus a 6th `static_assert`,
Milestone 1:**

| File | Line |
|---|---|
| `src/runtime/src/runtime_application.cpp` | 64 |
| `tests/image_regression/fixture/world_scene_loaded_fixture.cpp` | 86 |
| `tests/image_regression/fixture/world_scene_fixture.cpp` | 84 |
| `tests/image_regression/fixture/textured_quad_fixture.cpp` | 82 |
| `tests/image_regression/fixture/pbr_material_demo_fixture.cpp` | 82 |
| `tests/image_regression/fixture/minimal_cube_fixture.cpp` | 70 |
| `tests/image_regression/fixture/material_demo_fixture.cpp` | 71 |
| `tests/image_regression/fixture/integrated_showcase_demo_fixture.cpp` | 89 |
| `tests/image_regression/fixture/lighting_demo_fixture.cpp` | 75 |
| `tests/image_regression/shadow_gpu_tests.cpp` | 100 |

Each gains, after the existing five `static_assert`s:
`static_assert(offsetof(Vertex, tangent) ==
atlantis::asset_system::kMeshArtifactTangentOffsetBytes);`, and the
existing `sizeof(Vertex)` assertion now checks against `60`.

**Category B — no `static_assert`s today (own comment discloses these
files never upload real vertex data), widened only where a normal-map
vertex layout needs a real offset to name:**

| File | Line | Change | Milestone |
|---|---|---|---|
| `tests/runtime/pbr_render_gpu_tests.cpp` | 106 | append `float tangent[4];`, new `pbrIblNormalMapLayout()` (P13) | 3 |
| `tests/runtime/material_realization_gpu_tests.cpp` | 82 | append `float tangent[4];`, new `pbrDirectLitNormalMapVertexLayout()`-style helper | 4 |

**Category C — hand-fed, position(+color)-only geometry, never tied to
`mesh_artifact`'s schema, confirmed NOT to draw through any
mesh-loading path — genuinely unaffected:**

`examples/minimal_renderer_demo/main.cpp`,
`examples/headless_rendering_demo/main.cpp`,
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
`tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp`,
`tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp`,
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`,
`tests/vulkan_backend/shadow_map_render_gpu_tests.cpp`,
`tests/image_regression/sky_background_gpu_tests.cpp`.

New file `tests/image_regression/fixture/pbr_normal_map_demo_fixture.cpp`
(Milestone 5) starts directly at the 60-byte shape — not a migration.

## P18. `Material` constructor guard tests — exact location and shape

New `TEST_CASE`s in `tests/renderer/renderer_ownership_tests.cpp`
(already owns `ScopedFailureHandler`, reused verbatim):

1. `ScopedFailureHandler` installed; construct `Material` with
   `normalMapTexture` non-null, `sampledTexture`/`sampler` both
   `nullptr`; `REQUIRE(failures.size() == 1)`.
2. `ScopedFailureHandler` installed; construct `Material` with a valid
   base-color pair, `normalMapTexture` non-null, `pushConstantLayout =
   ObjectToWorldOnly`; `REQUIRE(failures.size() == 1)`.
3. No handler needed; construct `Material` with a valid base-color
   pair, `normalMapTexture` non-null, `pushConstantLayout =
   PbrDirectLit`, once for `MaterialEnvironmentBinding::None` and once
   for `Ibl` — both must construct without any captured failure.

## P19. New demo — fixed scene, camera, light, receiver, materials

**Assets (new), fixed textures:**
- `assets/textures/normal_map_tilted_source_unorm.png` — a **fixed
  4×4 RGB8 PNG, all 16 pixels exactly `(204, 128, 230)`** (no
  implementation-time size choice). **Generation method — the
  repository's own already-approved, already-vendored `stb_image_write`
  (`Stb::Stb`, `cmake/AtlantisStb.cmake`, the same library
  `tests/image_regression/support/png_codec.cpp` already links), never
  ImageMagick or any other new tool dependency:** a small, temporary,
  **uncommitted** C++ helper, compiled ad hoc against the already-
  fetched `stb_image_write.h` (no new CMake target, no permanent
  asset-generation tool added to this repository):

  ```cpp
  #define STB_IMAGE_WRITE_IMPLEMENTATION
  #include <stb_image_write.h>
  #include <cstdint>

  int main() {
    std::uint8_t pixels[4 * 4 * 3];
    for (int i = 0; i < 16; ++i) {
      pixels[i * 3 + 0] = 204;
      pixels[i * 3 + 1] = 128;
      pixels[i * 3 + 2] = 230;
    }
    stbi_write_png("normal_map_tilted_source_unorm.png", 4, 4, 3, pixels, 4 * 3);
    return 0;
  }
  ```

  Implementation compiles and runs this helper once (e.g. a single
  ad hoc invocation against the CMake-fetched `stb` source directory's
  own include path), copies the resulting PNG into
  `assets/textures/`, computes its SHA-256 (any standard tool --
  `Get-FileHash -Algorithm SHA256` on Windows, `sha256sum` elsewhere),
  and cites that hash in the Implementation PR description for
  reviewer verification. **The helper `.cpp` file itself is never
  committed to this repository** -- it is scratch tooling, deleted
  after use, exactly matching this Plan's own "temporary, uncommitted"
  audit-script precedent (P4 above) rather than a new, permanent
  asset-generation mechanism. Decoded tangent-space normal (before
  shader normalization): `(0.6, 0.00392, 0.80392)`. Cooked via
  `atlantis_add_texture_asset(NAME normal_map_tilted COLOR_SPACE Unorm)`.
- `assets/materials/pbr_normal_mapped.material.txt` — `kind:
  pbr_direct_lit`, `texture: textures/textured_quad_source_srgb.png`
  (reused), `filter: linear`, `address_mode: repeat`,
  `base_color_factor: 1.0 1.0 1.0 1.0`, `metallic_factor: 0.0`,
  `roughness_factor: 0.5`, `normal_map:
  textures/normal_map_tilted_source_unorm.png` (9 lines).
- `assets/materials/pbr_normal_mapped_control.material.txt` —
  identical, minus the `normal_map:` line (8 lines) — the
  discriminative test's own "B" twin, never referenced by any scene
  node (see the fixture mechanism below).

**Scene geometry (real, computed, disclosed — a real departure from
this Plan's own prior draft, whose camera/light/pixel/threshold could
not prove a visible shadow; recomputed in full this round):**

- Sphere: `pbr_sphere` (post-migration), `position=0 0 0 rotation=0 0 0
  scale=1 1 1`, `material=materials/pbr_normal_mapped.material.txt`.
  Vertex 200's own world position stays exactly `(0,0,1)` (identity
  transform).
- Ground plane (new, the shadow receiver): `ground_plane`,
  `position=0 -1 0 rotation=0 0 0 scale=1 1 1`,
  `material=materials/pbr_dielectric_rough.material.txt` (existing,
  reused, no normal map) — its own `y=0`-local, `10×10` extent
  (Pre-draft verification) becomes world `y=-1`, tangent to the unit
  sphere's own bottom without moving the sphere.
- Camera: `position=0 3 9 rotation=-0.3587707 0 0 fov_y=1.0472
  near=0.1 far=100.0`. Derivation: `forward = normalize((0,0,1) -
  (0,3,9)) = (0, -0.3511, -0.9363)`; solving `(0, sin(theta), -cos(theta))
  = forward` (this Plan's own confirmed `rotationX` column2 formula,
  Pre-draft verification) gives `theta = atan2(-0.3511, 0.9363) =
  -0.3587707` rad. Independently re-verified by a full, script-built
  view/projection (standard symmetric perspective, `fov_y=1.0472`,
  `512×512`) confirming vertex 200 projects to **exactly pixel
  `(256, 256)`** — the center-pixel property is preserved under this
  elevated, pitched camera by construction (the camera's own forward
  axis passes exactly through the target point; this holds for any
  camera position/orientation with zero roll, not only the previous
  drafts' axis-aligned case).
- Directional light: `position=0 0 0 rotation=-0.6 1.2 0.0
  color=1.0 1.0 1.0 intensity=3.0`. A real, two-axis (pitch+yaw)
  rotation, computed via this Plan's own confirmed `R = Ry(yaw) *
  Rx(pitch) * Rz(roll)` order (Pre-draft verification) to give
  `direction ≈ (-0.7692, -0.5646, -0.2991)` (`L = -direction ≈
  (0.7692, 0.5646, 0.2991)`) — chosen (not the identity-rotation light
  this Plan's own prior draft used) specifically so the sphere's own
  cast shadow lands beside it on the ground plane, not directly behind
  it in the same screen column (which the prior, camera-aligned light
  would have self-occluded).
- Environment: `ibl_studio` (existing, reused) — activates
  `environmentEnabled = true`, selecting `pbr_ibl_normal_map.slang`.
- Extent: `512×512`.

**Shadow-caster proof:** the sphere is the scene's own one real,
non-degenerate occluder; `shadowCasterDrawItems` is non-empty whenever
a directional light is configured (the existing, established
contract) — this demo is the first of this Spec's own scope to combine
a real shadow-casting occluder with a normal-mapped, IBL-lit material
in one frame, proving the combined path Spec 0029's own Goals require.

**Discriminative pixel test 1 — normal-map A/B, real computed BRDF
values at the new camera/light geometry (recomputed this round; the
sign and magnitude both changed from the prior draft because `L` and
`V` are no longer both `(0,0,1)`):** with `N = (0,0,1)` (no normal
map), `V = normalize((0,3,8)) = (0, 0.3511, 0.9363)`, `L ≈ (0.7692,
0.5646, 0.2991)`, `baseColorFactor=(1,1,1,1)`, `metallic=0`,
`roughness=0.5`, `radiance=(3,3,3)`: final 8-bit `rgbSum = 384` (`128×3`,
white base color, upper bound). With the perturbed, decoded-and-
normalized tangent-space normal `(0.598, 0.0039, 0.801)` substituted
for `N` (`dot(N',L) ≈ 0.702`, substantially higher than `dot(N,L) ≈
0.299`, since the perturbation tilts toward `+X` and `L` itself has a
`+X` component): `rgbSum = 504` (`168×3`). **Delta = +120** (with
normal map brighter than without, the opposite sign from this Plan's
own prior draft, because the light/view geometry itself changed) —
independently re-verified at `roughness ∈ {0.3, 0.9}` and `baseColor ∈
{white, mid-gray, dark}` (a 3×3 sweep): the delta never drops below
**+90** across that whole range. Vertex 200 is confirmed unshadowed
(`dot(N,L) > 0` and `dot(N',L) > 0` — never self-shadowed by a convex
sphere on its own light-facing side, and no other occluder sits
between the light and this point). Neither the base computation nor
the sweep accounts for the scene's own real IBL ambient term or the
real, sub-white base-color texel — both real, unmodeled, additive
uncertainty, same disclosure as before.

**Threshold 1:** `rgbSum(pixelAt(withNormalMapRender, 256, 256)) -
rgbSum(pixelAt(controlRender, 256, 256)) > 50` — comfortably below the
computed 90-120 range. **Not measured on real GPU hardware for this
exact scene.** If Implementation's own real capture shows this bound
does not hold, stop and request Human Review before changing it.

**Discriminative pixel test 2 — shadow on/off, real computed receiver
pixel, borrowed conservative threshold — world coordinate corrected
this round; see "Mechanical float32 recomputation" below for the full,
re-verified derivation:** the sphere's own shadow, cast along the
light direction above onto the `y=-1` ground plane, lands (using the
sphere's own center as the reference cast point) at world `(-1.3624,
-1, -0.5297)`, projecting under the same camera to pixel **`(198,
273)`** (unchanged from this Plan's own prior draft — the earlier
world coordinate was a transcription error, copied from a different,
rejected candidate light rotation; the pixel itself was already
computed from the correct, chosen rotation and is re-confirmed below)
— confirmed outside the sphere's own on-screen silhouette (`x ∈ [209,
303]` at this camera), so the ground, not the sphere, is the visible
surface there, and confirmed within the ground plane's own `10×10`
extent (`x=-1.36, z=-0.53`, well inside `[-5,5]`), not at its edge.

**Mechanical float32 recomputation (this round, matching production
precision — `float`, not Python's default `double`):** using this
codebase's own real, confirmed `Mat4` layout (column-major,
`rotationX`/`rotationY`'s own exact column formulas, `R = Ry(yaw) *
Rx(pitch) * Rz(roll)`, all from `world.cpp`, Pre-draft verification),
recomputed end to end in `float32`:

- Light direction (`pitch=-0.6, yaw=1.2, roll=0`): `(-0.769245,
  -0.5646425, -0.29906675)` (float32) — matches the value already
  used for the normal-map BRDF computation above, to displayed
  precision.
- `t = -1 / direction.y = 1.7710321`.
- Shadow footprint: `(0 + t*direction.x, -1, 0 + t*direction.z) =
  (-1.3623576, -1.0, -0.5296568)` — the corrected value used above,
  **not** the `(-0.761, -1, -0.604)` this Plan's own prior draft
  stated (that value belonged to a different, rejected `(pitch=-0.8,
  yaw=-0.9)` candidate, carried over by a transcription error, not a
  computation error in the chosen `(-0.6, 1.2)` candidate itself).
- Camera pitch (aiming at vertex 200's world position `(0,0,1)` from
  `(0,3,9)`): `theta = -0.35877067` (float32) — unchanged, re-verified.
- Vertex 200 projects to pixel `(256.0, 256.0)` exactly (float32
  recompute) — the center-pixel property holds under full float32
  precision, not only Python's own default double precision.
- Shadow footprint projects to pixel `(197.50752, 273.14087)` →
  rounds to **`(198, 273)`** — **unchanged from this Plan's own prior
  draft.** The pixel itself was already computed from the correct,
  chosen light rotation in the prior round; only the accompanying
  world-coordinate prose was wrong. Sphere silhouette x-range at this
  camera: `[209.23, 302.77]` (float32) — `197.5` remains safely
  outside it, confirming the footprint is unoccluded.

**R1 (shadowed):** the real,
production `shadowCasterDrawItems` (the sphere's own `DrawItem`,
non-empty). **R2 (unshadowed control):** identical scene/camera/light,
`shadowCasterDrawItems` = an empty span — mirrors Plan 0028's own
exact same-pixel differential methodology (never a second scene, never
a light toggled off). **Threshold:** `rgbSum(pixelAt(R2, 198, 273)) -
rgbSum(pixelAt(R1, 198, 273)) > 15` — this exact number is
`shadow_gpu_tests.cpp`'s own already-real-GPU-measured floor
(confirmed precedent, reused verbatim as Plan 0028 itself already
established the practice of doing), carried over as a conservative
Plan-stage starting point for this new, different scene/material
configuration — **not measured for this exact scene.** If
Implementation's own real capture shows this bound does not hold,
stop and request Human Review before changing it, never retune it
silently.

**Fixture A/B mechanism (real, corrected from this Plan's own prior
draft, which incorrectly described "pointing the scene loader at
whichever material" — a real scene artifact's own node data is fixed
at cook time and cannot be rewritten by a loader-side parameter):**

1. `pbr_normal_map_demo_scene`'s own CMake declaration lists **both**
   `pbr_normal_mapped` and `pbr_normal_mapped_control` under
   `MATERIAL_DEPENDENCIES` (and both textures under
   `TEXTURE_DEPENDENCIES`) — the control material is declared,
   manifested, and cooked, but referenced by **zero** scene nodes,
   the exact "declared but only transitively used" tolerance this
   codebase's own `TEXTURE_DEPENDENCIES` mechanism already establishes
   (Pre-draft verification, `assets/CMakeLists.txt`'s own header
   comment).
2. `loadAndInstantiateScene()` runs unmodified: its own Phase 1
   `distinctMaterialIds` collection walks only
   `node.renderable->materialAsset` (confirmed, Pre-draft
   verification) — the control material, referenced by no node, is
   never resolved or loaded by this call. The scene's own one real
   sphere `DrawItem` (R1) uses the real, normal-mapped `Material`.
3. The new fixture's own setup phase, **after** the normal scene-load
   sequence, independently resolves and realizes the control material:
   using the `ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH`/
   `_METADATA_PATH` compile definitions (the exact same mechanism
   `tests/image_regression/CMakeLists.txt` already uses for every
   scene's own artifact/metadata path — Pre-draft verification), it
   calls `loadMaterialAsset()` directly (no manifest lookup needed —
   the path is already known at compile time, exactly like a scene's
   own path), then the same `realizeOneMaterialCandidate()`-shaped
   sequence the scene's own material already went through — reusing
   the **same**, already-realized base-color texture (both materials
   reference `textured_quad_srgb`, deduped by `AssetId` through the
   same `effectiveSampledTextures` map, confirmed by direct reading of
   `realizeOneMaterialCandidate()`'s own dedup logic) and building its
   own new `Sampler`/`Material` (a real, second, fixture-owned
   `Material` object, kept alive alongside the scene's own resources).
4. **R1 (normal-mapped, the golden's own render):** the scene's real
   `DrawItem` list, used unmodified.
5. **R2 (control, discriminative-test-only, never captured as a
   golden):** a **copy** of the same `DrawItem` vector (`DrawItem` is
   a plain, trivially-copyable `{const Mesh*, const Material*,
   std::array<float,16>}`, Pre-draft verification) with the one
   sphere entry's own `.material` pointer reassigned to `&controlMaterial`
   — mesh, transform, camera, lighting, environment, and shadow
   resources are all identical to R1; only this one borrowed pointer
   differs.

**Golden:** exactly one, `pbr_normal_map_demo`, captured against **R1
only** (the real, normal-mapped material) via ADR-0042's existing
two-phase candidate-generate → human-review process. R2 is never
captured or committed.

**Fixture/golden_generator/gpu_tests trio** (new): mirrors
`integrated_showcase_demo_fixture.cpp`'s own combined PBR+IBL+sky+
shadow wiring shape (explicit sky Pipeline construction from the
shared `sky.slang`, no new sky shader) plus `pbr_material_demo_fixture.cpp`'s
own PBR-realization shape, using the new
`pbrDirectLitNormalMapVertexLayout()`/`pbrIblNormalMapVertexLayout()`
functions (location 0/1/2/3 = position/uv/normal/tangent):
`tests/image_regression/fixture/pbr_normal_map_demo_fixture.h/.cpp`,
`tests/image_regression/golden_generator/pbr_normal_map_demo_main.cpp`,
`tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp`.

## Milestones / Task Breakdown

**Seven milestones, strictly ordered.** Each Implementation milestone
(1-6) must independently compile and pass its own applicable tests
before the next begins — no milestone requires a later one to already
exist in order to build (corrected from this Plan's own prior draft,
which incorrectly proposed implementing Milestones 3/4 "together in
source" — a real, disclosed self-contradiction against this same
document's own "each Milestone is atomic" claim, now removed; no
placeholder shader-trio argument or "verified as two diffs" framing
survives this round).

1. **Mesh format extension.** P1-P4, P17 Category A, the RHI/
   reflection `Float4` five-file diff, `errors.h`'s new `CookError`/
   `ArtifactDecodeError` values, `pbr_sphere.mesh.txt`'s own real
   sign-split migration (P4, depending on the two pending Proposed
   Corrections), `minimal_cube.mesh.txt` left byte-for-byte unchanged,
   new GPU-independent unit tests (tangent generation, the fallback,
   handedness-conflict rejection, the narrowed `DegenerateTangentBasis`,
   `NonUnitTangent`/`NonOrthogonalTangent`/`InvalidTangentHandedness`).
   **Independently buildable and testable: yes** — no dependency on
   any later milestone. **Acceptance gate:** all 9 existing goldens
   byte-identical; every composition root builds; `pbr_sphere`'s own
   re-audit reports `0/473` conflicts.
2. **Material schema and dependency chain.** P5-P9, the one new
   `MaterialSourceParseError` enumerator, the one new
   `RuntimeInitError` enumerator, all 6 existing `.material.txt`
   version-marker edits, new unit tests (`NormalMapNotSupportedForKind`,
   `MissingField` on empty `normal_map:`, artifact/metadata
   cross-validation, cook/decode round-trip). **Independently
   buildable and testable: yes** — depends only on Milestone 1's own
   tangent attribute existing in the mesh schema (referenced by no
   material yet). **Acceptance gate:** all 9 goldens byte-identical;
   every existing material still cooks and loads unchanged.
3. **Descriptor capacity, two new shaders, reflection/contract wiring,
   real binding-5 test.** P10-P13, P17 Category B's
   `pbr_render_gpu_tests.cpp` entry. New shader files compile and pass
   their own `EXPECTED_CONTRACT` validation. **Independently buildable
   and testable: yes** — this milestone's own new GPU test (P13) binds
   texture 5 directly via hand-fed vertex buffers and raw
   `cmd.bindTexture()` calls, entirely bypassing `Material`/
   `RealizedMaterialCandidate` (Milestone 4's own scope) — confirmed
   by direct reading of `pbr_render_gpu_tests.cpp`'s own existing
   shape, which already does this for `UnlitTextured`/`LitTextured`/
   `PbrDirectLit`. **Real GPU verification required this milestone:**
   P13's own binding-5 test; the existing `N+4`/`N+5` set-count test
   re-run unmodified. **Acceptance gate:** all 9 goldens byte-
   identical; zero Vulkan Validation Layers hits; P13 passes.
4. **`Material` API and Runtime realization/`BootstrapConfig`
   integration.** P14-P16, P18's own guard tests, P17 Category B's
   `material_realization_gpu_tests.cpp` entry. `selectShaderPair()`/
   `sampledTextureBindingCountFor()`/`realizeOneMaterialCandidate()`/
   `realizePendingMaterials()` widened and given the two real shader
   trios Milestone 3 already compiled — no placeholder trio, since
   Milestone 3 already produced the real thing. **Independently
   buildable and testable: yes** — depends on Milestone 3's own two
   real shaders and Milestone 2's own `normalMapTexture` field
   existing; does not depend on any demo asset. **Acceptance gate:**
   all 9 goldens byte-identical; the 13 unaffected `createMaterial()`
   call sites confirmed to still compile unchanged; P18 passes.
5. **New demo assets/fixture/discriminative tests — candidate only, no
   golden commit.** P19 in full (assets, scene, fixture, golden_generator
   executable, both discriminative-test `TEST_CASE`s), **except** the
   final golden-comparison assertion, which does not yet exist because
   no golden is committed yet. Running the golden_generator executable
   produces an **untracked**, uncommitted candidate PNG under a local
   output path — never added to `tests/image_regression/goldens/` in
   this milestone. **Independently buildable and testable: yes** —
   depends on Milestone 4's own two real shaders and widened capacity;
   both discriminative tests (P19's own pixel/threshold pairs) run and
   assert against **live renders only**, needing no committed golden.
   **Acceptance gate:** both discriminative tests pass against real
   GPU output; the candidate PNG is generated and stops here, pending
   human review — **no commit of any golden file happens in this
   milestone.**
6. **Human-approved golden commit and capture-compare test.** Only
   after a human has visually reviewed Milestone 5's own candidate PNG
   (confirming a correctly-rendered, non-degenerate frame — not black,
   not garbage) and approved it: commit the golden PNG and its own
   sidecar under `tests/image_regression/goldens/pbr_normal_map_demo/`,
   via ADR-0042's own "Initial baseline bootstrap" category (its own
   four sub-requirements: source revision on a clean tree, full
   sidecar provenance, the golden PNG/sidecar added via their own
   separate, subsequent commit — never folded into Milestone 5's own
   commit — and the required evidence: human visual inspection,
   zero-diff self-reproduction, a clean real-GPU/Validation-Layers run,
   citation of the channel-tolerance-0 calibration evidence), and add
   the new capture-compare `TEST_CASE` to
   `pbr_normal_map_demo_gpu_tests.cpp` that reads this now-real golden
   and asserts zero difference. **Independently buildable and
   testable: yes** — depends only on Milestone 5's own candidate
   existing and being approved; no other source changes in this
   milestone. **Acceptance gate:** the new capture-compare test passes;
   all 9 existing goldens re-confirmed byte-identical (this is the
   milestone most likely to accidentally touch a shared asset).
7. **Full verification pass and registry closeout.** No new source
   change. Runs the complete Verification Checklist below end to end,
   both `ATLANTIS_BUILD_TESTS` configurations, both build
   configurations, records the real, final numbers directly in this
   Plan document (not as a promised future edit). `specs/README.md`'s
   Spec 0029 row updated to reflect this Plan's own real
   Implementation state, stated as **"Implemented in
   [PR #&lt;this PR's own number&gt;]; pending merge"** — never
   "merged via PR #N," which only a human's own later merge action
   makes true; the actual "Implemented and merged" registry wording is
   recorded by a **separate, later** commit or PR, after a human has
   actually merged this Implementation PR, not by this PR itself
   (corrected from this Plan's own prior draft, which incorrectly
   proposed adding a "Post-Merge Status Update" section "at merge
   time" — an action this PR cannot itself perform once it is already
   merged). If any gate fails, Implementation returns to the relevant
   earlier milestone, never forward.

## Files / Modules Touched (expected)

- `src/asset_system/include/atlantis/asset_system/mesh_tangent_generation.h` (new), `src/asset_system/src/mesh_tangent_generation.cpp` (new) — Milestone 1
- `src/asset_system/include/atlantis/asset_system/mesh_artifact.h`, `src/asset_system/src/mesh_artifact.cpp` — Milestone 1
- `src/asset_system/src/cook.cpp` — Milestone 1
- `src/asset_system/include/atlantis/asset_system/errors.h` — Milestones 1-2
- `src/rhi/include/atlantis/rhi/types.h`, `src/shader_system/include/atlantis/shader_system/reflection_metadata.h`, `src/shader_system/src/slang_json_transform.cpp`, `src/shader_system/src/reflection_loader.cpp`, `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp` — Milestone 1 (`Float4`)
- `assets/meshes/pbr_sphere.mesh.txt` — Milestone 1 (sign-split migration; `minimal_cube.mesh.txt` NOT touched)
- P17 Category A's 10 files — Milestone 1
- New `tests/asset_system/mesh_tangent_generation_tests.cpp`, extended `tests/asset_system/mesh_artifact_tests.cpp` — Milestone 1
- `src/asset_system/include/atlantis/asset_system/material_types.h`, `material_source.h`/`.cpp`, `material_artifact.h`/`.cpp`, `material_metadata.h`/`.cpp` — Milestone 2
- `src/asset_system/src/cook_material.cpp`, `src/asset_system/src/load_material.cpp` — Milestone 2
- `src/runtime/include/atlantis/runtime/init_error.h`, `src/runtime/src/init_error.cpp`, `src/runtime/src/scene_load.cpp` — Milestone 2
- `src/asset_system/CMakeLists.txt` (`atlantis_add_material_asset()`), `assets/CMakeLists.txt` (6 version-marker edits) — Milestone 2
- New/extended material/scene-load unit tests — Milestone 2
- `src/vulkan_backend/src/vulkan_device.cpp`, `src/vulkan_backend/src/vulkan_command_list.h` — Milestone 3
- `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`, `src/shader_system/src/descriptor_contract.cpp` — Milestone 3
- `src/tools/shader_compiler/compile_and_validate.cpp` — Milestone 3
- New `shaders/pbr_direct_lit_normal_map/*`, `shaders/pbr_ibl_normal_map/*` — Milestone 3
- `CMakeLists.txt` (root, two new `add_subdirectory()`) — Milestone 3
- `tests/runtime/pbr_render_gpu_tests.cpp` (P13, P17 Category B) — Milestone 3
- `src/renderer/include/atlantis/renderer/material.h`, `src/renderer/src/material.cpp` — Milestone 4
- `src/renderer/src/renderer.cpp` — Milestone 4
- `src/runtime/include/atlantis/runtime/material_realization.h`, `src/runtime/src/material_realization.cpp` — Milestone 4
- `src/runtime/include/atlantis/runtime/bootstrap_config.h`, `src/runtime/src/runtime_application.cpp` — Milestone 4
- `tests/renderer/renderer_ownership_tests.cpp` (P18) — Milestone 4
- `tests/runtime/material_realization_gpu_tests.cpp` (P17 Category B) — Milestone 4
- New `assets/textures/normal_map_tilted_source_unorm.png`, `assets/materials/pbr_normal_mapped.material.txt`, `assets/materials/pbr_normal_mapped_control.material.txt`, `assets/scenes/pbr_normal_map_demo.scene.txt` — Milestone 5
- `assets/CMakeLists.txt` (new asset declarations) — Milestone 5
- New `tests/image_regression/fixture/pbr_normal_map_demo_fixture.h`/`.cpp`, `tests/image_regression/golden_generator/pbr_normal_map_demo_main.cpp`, `tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp` (both discriminative tests, no golden-compare test yet) — Milestone 5
- `tests/image_regression/CMakeLists.txt` — Milestone 5
- New `tests/image_regression/goldens/pbr_normal_map_demo/*`, one new `TEST_CASE` in `pbr_normal_map_demo_gpu_tests.cpp` (golden-compare) — Milestone 6
- `specs/README.md` — Milestone 7
- [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Related Plan(s)` field) — a separate, later commit/PR, after human merge, not this Implementation PR (Milestone 7's own note)

**Not touched by this Plan:** `src/world/`, `src/render_graph/`,
`src/rhi/` beyond the one `Float4` enumerator, `pbr_direct_lit.slang`/
`pbr_ibl.slang` themselves (zero edits), `integrated_showcase_demo`'s
own scene/fixture/golden, any `adr/` file beyond the two corrections
(both now `Accepted`) already filed and approved alongside this Plan
(ADR-0073/0074 and the three Amendments remain `Accepted`, unmodified
in Decision content), any of the 9 existing image-regression goldens'
own PNG bytes.

## Sequencing & Dependencies

Milestone 1 → 2 → 3 → 4 → 5 → 6 → 7, strictly, **and each of
Milestones 1-6 independently compiles and passes its own applicable
tests before the next begins** — no milestone's own source depends on
a later milestone's own not-yet-written code (the real correction this
round makes: Milestone 3's new shaders/capacity/binding-5 test come
**before** Milestone 4's `Material` API integration, since Milestone
3's own new GPU test reaches the new shaders directly, bypassing
`Material` entirely, confirmed by direct reading of
`pbr_render_gpu_tests.cpp`'s own existing, hand-fed-vertex-buffer
shape). Milestone 5 depends on Milestone 4's own real `Material`/
runtime-realization plumbing (the demo scene is loaded through the
real, production path, not a hand-fed test buffer). Milestone 6
depends on Milestone 5's own candidate being generated and a human
approving it — this is a real, external gate this Plan cannot
schedule around; Implementation pauses at the end of Milestone 5 until
that approval exists. Milestone 7 depends on Milestone 6's own golden
and capture-compare test both passing.

**No unavoidable intermediate compile-break or real-GPU red window**
in this corrected ordering — each milestone is independently buildable
by construction, not merely "reviewable as a separate diff" within one
larger, jointly-implemented change (the prior draft's own
contradiction). If real Implementation finds an unavoidable
intermediate break this Plan did not anticipate, stop and disclose it
in the PR rather than silently working around it (AGENTS.md).

## Verification Checklist

- [ ] GPU-independent unit tests: tangent generation (hand-computable
      flat-UV triangle), the fallback (hand-computable zero-contribution
      vertex, P3's axis rule), handedness-conflict rejection (a
      constructed opposite-sign case), `DegenerateTangentBasis`'s
      narrowed trigger, `NonUnitTangent`/`NonOrthogonalTangent`/
      `InvalidTangentHandedness`, `NormalMapNotSupportedForKind`,
      `MissingField` on an empty `normal_map:` value, material
      artifact/metadata cross-validation of the new field, the P18
      `Material` constructor guard tests, `sampledTextureBindingCountFor()`'s
      new `3`/`5` results.
- [ ] Real-GPU tests: the P13 binding-5/`bindTexture(5, ...)` test; the
      existing `N+4`/`N+5` descriptor-set-count test re-run unmodified;
      the new fixture's own non-degenerate-frame proof; the P19
      normal-map discriminative test (`rgbSum` delta `> 50` at
      `(256,256)`); the P19 shadow discriminative test (`rgbSum` delta
      `> 15` at `(198,273)`).
- [ ] Image regression: all 9 existing goldens byte-identical after
      every milestone that could plausibly affect them (1-6); exactly
      one new golden (`pbr_normal_map_demo`), captured against R1 only,
      via ADR-0042's own "Initial baseline bootstrap" category, in its
      own separate commit (Milestone 6), never folded into Milestone
      5's own commit, and never auto-accepted.
- [ ] Vulkan Validation Layers: zero `VUID`/Validation Error/Warning
      hits across the full verbose GPU test output, Debug and Release.
- [ ] `ctest -LE gpu` and `ctest -L gpu`, Debug and Release.
- [ ] Fresh `-DATLANTIS_BUILD_TESTS=OFF` configure+build: produces a
      working `atlantis_runtime.exe`, zero test executables, and
      successfully re-cooks every mesh/material/texture asset under
      the new schema versions with no gap.
- [ ] Module/link graph: `Atlantis::AssetSystem` still links
      `Atlantis::Core` only; `Atlantis::RHI`'s public API confirmed
      unchanged beyond the one `Float4` enumerator.
- [ ] `Vk*` isolation: only the Vulkan Backend module references any
      `Vk*` type.
- [ ] `git diff --check` clean.
- [ ] Every existing `createMaterial()` call site (P16's own table)
      confirmed to still compile with zero source edit, except
      `material_realization.cpp`.

**Governance gate, satisfied before Implementation begins (recorded in
this Plan's own header, not a Milestone-time checklist item):** the
Spec 0029 correction, the ADR-0073 correction, and this Plan itself
were each individually named and approved by Human Review on
2026-09-06, against PR #130 — Milestone 1's own acceptance gate
(`0/473` conflicts) relies on the corrected figures those approvals
cover.

## Rollback Plan

Revert the Implementation PR's own commit(s) on `main`. No migration
mechanism exists for any of the widened formats (mesh artifact schema
4, material artifact schema 3, material source version 3) — a revert
returns every build-output artifact to the prior schema on the next
clean configure. The new, real, checked-in mesh/material/scene/texture
source files (P19) and `pbr_sphere.mesh.txt`'s own migrated content
are removed by the same revert; `minimal_cube.mesh.txt` stays
byte-for-byte unchanged throughout, so nothing there needs reverting.
If Milestone 6 has already landed (a real golden committed) before a
revert is needed, the golden's own removal is part of the same
revert — no golden survives independently of the feature it proves.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No delta beyond the Verification Checklist above.

## Human Review Approved Plan Correction — 2026-09-06 (tangent-generation algorithm omits a geometric-degenerate-triangle check)

**Status:** Approved by Human Review, 2026-09-06, against
[PR #131](https://github.com/slmao/Atlantis/pull/131), named
individually alongside the matching corrections to
[Spec 0029](../specs/0029-tangent-space-normal-mapping-foundation.md#human-review-approved-correction--2026-09-06-tangent-generation-algorithm-omits-a-geometric-degenerate-triangle-check)
and [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md#accepted-correction--2026-09-06-tangent-generation-algorithm-omits-a-geometric-degenerate-triangle-check),
in the same review pass — not implied by a blanket approval of one
correction alone. Does not rewrite this Plan's own header (its existing
Human Review Approval record above is preserved verbatim as historical
record — it approved this Plan against the `48/425`/sign-split figures
that this correction now supersedes), Milestones, schema/migration
tables, or Verification Checklist above. **Implementation of this Plan
resumes only once [PR #131](https://github.com/slmao/Atlantis/pull/131)
itself has merged to `main` — not before.**

ADR-0073's own Accepted Correction is the single authoritative source
for the algorithm, threshold, root-cause derivation (including the
corrected historical attribution — regardless of any pre-
orthogonalization difference between earlier probes, neither checked
geometric degeneracy, and re-auditing with ADR-0073's own current,
literal algorithm still reproduces `96/425`, superseding the first
Accepted Correction's own `48/425` conclusion), and the complete
five-mesh audit table; see
[ADR-0073's own Accepted Correction — 2026-09-06](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md#accepted-correction--2026-09-06-tangent-generation-algorithm-omits-a-geometric-degenerate-triangle-check).
Not restated here.

**Effect on Milestone 1, approved:**

- P4's own `425→473` sign-split migration method, its simulated
  result table, its audit command, and its acceptance gate are removed
  in full — `pbr_sphere.mesh.txt` needs **no source edit or vertex
  split**, staying 425 vertices/768 triangles/2304 indices.
- Milestone 1's own acceptance gate changes from "`0/473` conflicts"
  to "`48` geometric-degenerate, `0` UV-degenerate, `0` handedness
  conflicts, out of 768; vertices 24 and 400 on the fallback path."
- Every other `425→473`-based reference in this Plan (Pre-draft
  verification's own audit table, P17's own vertex-200 note, the
  Files/Modules Touched table's `pbr_sphere.mesh.txt` row, the
  Rollback Plan's own migrated-content clause) is now stale prose,
  approved to be mechanically updated — not re-decided — during
  Milestone 1's own resumed Implementation.
- `pbr_sphere` is no longer "the only mesh requiring a source edit" —
  under the corrected algorithm, **no** committed mesh needs one;
  `minimal_cube` is unaffected.
- New unit tests for the geometric-degeneracy check (threshold,
  exclusion behavior, scale-invariance) are approved, added to
  Milestone 1's own scope, supplementing the existing tangent-
  generation unit-test checklist item; full test list is ADR-0073's
  own Accepted Correction, not restated here.
- P1–P3's fixed epsilons, P5–P19's other contracts, the 7-Milestone
  structure, the demo's fixed values, and the byte-identical
  requirement on all 9 existing goldens are all unaffected.

**Governance record — no implied approval was relied upon.** This Plan
Correction, the Spec 0029 correction, and the ADR-0073 correction were
filed together in one pass but each required, and received, its own
separate, individually-named Human Review approval — approving one did
not, by itself, approve the other two. Implementation stays paused
only on the remaining, external gate: a human merging
[PR #131](https://github.com/slmao/Atlantis/pull/131) to `main`.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-06, accepting this correction in full, as
drafted, with no change, as part of the same review pass that approved
the matching Spec 0029 and ADR-0073 corrections.

## Human Review Approved Plan Correction — 2026-09-06 (BootstrapConfig shader-pair path count)

**Status:** Approved by Human Review, 2026-09-06, against
[PR #132](https://github.com/slmao/Atlantis/pull/132). Does not
rewrite this Plan's own header, P15's own text, or either of the two
corrections above (all preserved verbatim as historical record) —
this correction supersedes only P15's own `3 fields per pair / 6 new
fields` claim, quoted and corrected below.

**Origin.** Discovered during resumed Milestone 4 Implementation
(chat, 2026-09-06), while widening `RealizedMaterialCandidate`/
`realizeOneMaterialCandidate()`/`realizePendingMaterials()` per P15.
Does not rewrite P15's own text above (preserved verbatim as
historical record — it is superseded by this correction, not edited
in place, matching this Plan's own established precedent for the two
prior corrections above).

**The error.** P15 states:

> `BootstrapConfig` gains 6 new `std::string` fields (3 per new shader
> pair, mirroring `pbrIblVertexShaderSpirvPath`'s own shape).

This is a factual error about the real, current shape of
`BootstrapConfig` ([src/runtime/include/atlantis/runtime/bootstrap_config.h](../src/runtime/include/atlantis/runtime/bootstrap_config.h)),
not a new architectural choice — the shader-pair loading contract,
the two new shaders themselves, descriptor bindings, and Runtime
integration design are all unaffected.

**Real source fact.** Every existing shader pair `BootstrapConfig`
declares — from the original `vertexShaderSpirvPath` group through
the most recent `outputTransformSrgb*` group (Plan 0024) — is exactly
**4** `std::string` fields, with no exception across the struct's
entire history: `...VertexShaderSpirvPath`, `...VertexShaderReflectionPath`,
`...FragmentShaderSpirvPath`, `...FragmentShaderReflectionPath`. This
includes the very pair P15 cites as the mirrored shape — `pbrIbl` is
itself a 4-field group (`pbrIblVertexShaderSpirvPath`,
`pbrIblVertexShaderReflectionPath`, `pbrIblFragmentShaderSpirvPath`,
`pbrIblFragmentShaderReflectionPath`), not 3.

This 4-field shape is not merely declared — it is populated at every
real call site that constructs a `BootstrapConfig`, with zero
exceptions, confirmed via `rg 'FragmentShaderReflectionPath'` across
the repository: [src/runtime/main.cpp](../src/runtime/main.cpp), all
six `tests/image_regression/golden_generator/*_main.cpp` files, nine
`tests/image_regression/*_gpu_tests.cpp` files,
[tests/runtime/runtime_smoke_gpu_tests.cpp](../tests/runtime/runtime_smoke_gpu_tests.cpp),
and
[tests/runtime/bootstrap_config_tests.cpp](../tests/runtime/bootstrap_config_tests.cpp)
— roughly 15 real files, each assigning all four fields for every
shader pair it configures, unbroken across five-plus prior Milestones
(Plan 0018 through Plan 0024). Some individual `...FragmentShaderReflectionPath`
fields are not read back by every load routine that consumes their
own pair (e.g. `runtime_application.cpp`'s `pbrDirectLit` load block
only calls `loadReflectionMetadata()` on the vertex path) — but the
field is still declared and still populated at every call site,
without exception, matching the struct's own uniform 4-field shape.

**Correction.** `BootstrapConfig` gains **8** new `std::string`
fields (4 per new shader pair), not 6 (3 per pair):

- `pbrDirectLitNormalMapVertexShaderSpirvPath`
- `pbrDirectLitNormalMapVertexShaderReflectionPath`
- `pbrDirectLitNormalMapFragmentShaderSpirvPath`
- `pbrDirectLitNormalMapFragmentShaderReflectionPath`
- `pbrIblNormalMapVertexShaderSpirvPath`
- `pbrIblNormalMapVertexShaderReflectionPath`
- `pbrIblNormalMapFragmentShaderSpirvPath`
- `pbrIblNormalMapFragmentShaderReflectionPath`

`runtime_application.cpp`'s own matching load/store block (P15's own
"mirrors lines 372-389" reference is unaffected — only the field
count per pair changes, not the load shape or which lines it mirrors)
reads all four fields per pair, mirroring the existing `pbrDirectLit`/
`pbrIbl` load blocks exactly, including a fragment-reflection field
that is declared and populated but not read back by
`runtime_application.cpp` itself — the same, already-established
asymmetry the real `pbrDirectLit`/`unlitTextured`/`litTextured`/
`outputTransform*` pairs already exhibit.

**Scope check — no other Plan text depends on this count.** A search
of this entire Plan document for every other statement that could
depend on the wrong number (Milestone 4's own description, the Files/
Modules Touched table, P16's `createMaterial()` call-site migration
table, P17's vertex-layout migration table, and the Verification
Checklist) found no other occurrence of "6 new fields," "3 per pair,"
or any of the 8 field names above — P15 is the only place this Plan
ever states a specific count or shape for these fields. No other
Milestone, file-scope, call-site-migration, or verification statement
in this Plan requires updating.

**No Spec or ADR correction needed.** This is a pure Plan-level
implementation-detail correction: the shader pair, its descriptor
contract, `RealizedMaterialCandidate`/`realizeOneMaterialCandidate()`/
`realizePendingMaterials()`'s own widened function shapes, and every
other part of the Runtime-integration design in Spec 0029/ADR-0073/
ADR-0074 are unchanged. Only the field *count* naming a pre-existing,
already-`Accepted` `BootstrapConfig` shape is corrected — not a new
module boundary, public API shape, dependency, or ownership model
requiring its own ADR (AGENTS.md's Golden Rule).

**Effect on Milestone 4.** P15's own remaining prose (the
`RealizedMaterialCandidate` diff, `selectShaderPair()`'s widened
`PbrDirectLit` case, `sampledTextureBindingCountFor()`'s `hasNormalMap`
parameter, and the `realizeOneMaterialCandidate()`/`realizePendingMaterials()`
trio-widening) is unaffected and stays exactly as approved. Milestone
4's own Acceptance Gate (all 9 goldens byte-identical; the 13
unaffected `createMaterial()` call sites still compile; P18 passes)
is unaffected.

**Human Review Approval, recorded 2026-09-06 (chat).** slmao
(`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
maintainer) approved this correction in full, as drafted, with no
change: the two new normal-map shader pairs (`pbrDirectLitNormalMap`,
`pbrIblNormalMap`) each gain exactly 4 path fields, not 3 — vertex
SPIR-V, vertex reflection, fragment SPIR-V, and fragment reflection,
none of the four omissible — for 8 new `std::string` fields on
`BootstrapConfig` in total, listed in full above. This approval
supersedes P15's own original `3 fields per pair / 6 new fields`
text (preserved above, unedited, as historical record). It does not
change any other numeric value, schema, milestone ordering, or the
shader/descriptor/`Material`/Runtime design elsewhere in this Plan —
those stay exactly as already approved.

**Governance record — no implied approval was relied upon.** This
correction's own approval stands alone; it does not rely on, and was
not implied by, the approval of either of the two corrections above,
or by any other prior approval in this Plan's history. **Implementation
of this Plan remains paused** — approving this correction is not
itself authorization to resume Implementation. Implementation resumes
only once [PR #132](https://github.com/slmao/Atlantis/pull/132)
itself has merged to `main` — not before.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-06, accepting this correction in full, as
drafted, with no change.

## Human-Approved Implementation Deviation — 2026-09-07

**Status:** Approved by Human Review, 2026-09-07 (chat), against this
Plan's own Implementation branch (`feature/0029-tangent-space-normal-mapping-foundation`).
This section does not change this Plan's own `Approved / Ready for
Implementation` status; it records an explicit, human-directed
deviation from this Plan's own Milestone 6 gate, per AGENTS.md's own
"if reality forces a deviation from the plan, stop and call it out
explicitly ... rather than silently drifting."

**The deviation, precisely.** Milestone 5 produced a real, GPU-captured
`pbr_normal_map_demo` candidate whose sphere shows a visible field of
near-black granular artifacts along its own grazing-angle/terminator
region. A dedicated investigation (Spec 0030, ADR-0072's own 2026-09-07
Amendment, and a follow-up strategy-correction probe covering caster-
side Vulkan raster depth bias, geometric-normal world-position offset,
and their combination) traced this artifact, via a controlled four-way
A/B/C/D capture, to `computeShadowFactor()`'s own fixed
`kShadowBias = 0.0015` literal (ADR-0072 D-5, unchanged since Spec
0027) — **not** to this Plan's own tangent-generation algorithm, TBN
construction, or normal-map sampling, all three of which the same
investigation independently ruled out (the identical artifact reproduces
on the sphere's own **control** material, which never builds a TBN
basis or samples a normal map at all). Every real-GPU alternative bias
mechanism investigated (Spec 0030's own originally-approved slope-aware
receiver-depth bias, caster-side raster depth bias, geometric-normal
position offset, and a combination of the latter two) was found,
empirically, to erase a real, physically valid nearby occluder's own
shadow whenever tuned strongly enough to suppress this artifact — see
[Spec 0030](../specs/0030-directional-shadow-bias-stability.md)'s own
Human Review Deferral Record and
[ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)'s
own empirical deferral record for the full evidence.

**User's explicit direction (chat, 2026-09-07):** defer shadow-bias
remediation work entirely and continue Spec 0029 to completion using
the existing Milestone 5 candidate as-is. Concretely:

1. **The current, untracked Milestone 5 candidate is approved to
   proceed to Milestone 6** — no re-capture, no parameter change, no
   camera/light/geometry/material edit of any kind. Its own fixed,
   verified content hashes (SHA-256, recomputed and cross-checked
   immediately before this approval, both from disk and from the
   staged git blob at commit time):
   - PNG: `6fb4339782da365fb09bbd25412793c6fde92a9ec0807fc4d9521106ae5ffe0`
     (see the 2026-09-07 hash-citation correction below — this string
     is one hex digit short of a complete SHA-256)
   - sidecar: `45c8d3d0d0a4fe468b5c433b541bf4b9b6733eff50b354fb9ba522a2ed14ceb`
     (same correction applies)
2. **The visible black granular artifact is accepted, explicitly, as a
   disclosed, pre-existing limitation of Spec 0027/ADR-0072's own
   shared, fixed `kShadowBias` mechanism** — a limitation this Plan's
   own tangent-space normal-mapping work neither introduces nor is
   responsible for fixing, confirmed by the A/B/C/D control-material
   isolation above. It is **not** classified as a tangent-generation,
   TBN-construction, or normal-map-sampling defect of this Plan's own
   Implementation.
3. **No camera, light, geometry, bias, normal-map texture, or
   discriminative-pixel/threshold value changes** as part of this
   deviation — Milestone 5's own already-approved fixed scene and
   Milestone 5/P19's own fixed `(256,256)`/`>50` (normal-map) and
   `(198,273)`/`>15` (shadow) discriminative pixels and thresholds
   remain exactly as this Plan already fixed them, unmodified, and
   both remain real, measured-passing discriminators on this exact
   candidate (see this Plan's own Milestone 6/7 real measurements
   below).
4. **A future shadow-bias remediation, once a real, GPU-verified fix
   passes Spec 0030's own full acceptance gate, may change this exact
   golden's own pixel content** — when that happens, the resulting
   diff goes through its own, independent ADR-0042 Human Review
   re-capture (golden-update-reason category 1, "Rendering change"),
   exactly like any other real rendering change; this deviation record
   is not itself standing authorization for that future re-capture.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-07, directing this exact deviation, as
described above, with no further condition.

## Milestone 7 Closeout — 2026-09-07

Milestone 7's full Verification Checklist was executed end-to-end on
`feature/0029-tangent-space-normal-mapping-foundation` at
`f22a30d` (Milestone 6's own commit), with real results:

- Debug build: succeeded. Release build: succeeded.
- `ctest -C Debug -LE gpu`: 917/917 passed. `ctest -C Release -LE gpu`:
  916/916 passed (one fewer is the pre-existing, documented Debug-only
  `ATLANTIS_ASSERT` test gap, not a regression).
- `ctest -C Debug -L gpu`: 99/99 passed. `ctest -C Release -L gpu`:
  99/99 passed. Vulkan Validation Layers output (`ctest -L gpu -V`,
  both configs) is clean — zero `VUID`/Validation Error/Validation
  Warning.
- All 9 pre-existing goldens confirmed byte-identical. The new
  `pbr_normal_map_demo` capture-compare `TEST_CASE` passes, run twice,
  byte-identical both times. `world_scene_loaded` passes individually
  (7 assertions). `sky_background` passes individually (69 assertions).
  The normal-map discriminative pixel `(256,256)` measures
  `delta = 103` (gate `>50`, pass). The shadow discriminative pixel
  `(198,273)` measures `delta = 160` (gate `>15`, pass).
- A separate `ATLANTIS_BUILD_TESTS=OFF` build was verified: the Runtime
  builds and runs, assets cook correctly (262 files, including the new
  normal-map assets), and zero test executables are produced.
- Module/link boundaries, `Vk*` isolation, RHI public-API diff, and
  `/w14062` exhaustiveness were all re-checked and remain within this
  Plan's own established constraints; no violation found.

Implementation is complete on this branch. It is carried by
[PR #135](https://github.com/slmao/Atlantis/pull/135)
(`feat: implement tangent-space normal mapping foundation`), which is
**not yet merged** — this Plan's own status line above reflects that
honestly; it will not say "merged" until a human actually merges that
PR.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Milestone 7
closeout recorded 2026-09-07.

## Hash-Citation Correction — 2026-09-07

The SHA-256 values quoted verbatim in the
[Human-Approved Implementation Deviation](#human-approved-implementation-deviation--2026-09-07)
section above (as given in chat, 2026-09-07) are each **63 hex
characters, one short of a complete 64-character SHA-256** — almost
certainly a transcription artifact from when the reference values were
pasted into chat, not a different hash. This does **not** indicate the
golden file was regenerated, modified, or is otherwise in question: it
is the same, untouched Milestone 5 candidate throughout, and its real,
complete, independently-recomputed digests (from disk, matching the
staged git blob at commit `f22a30d`) are:

- PNG: `6fb4339782da365fb09bbd25412793c6fde92a9ec0807fc4d9521106ae5ffe0d`
- sidecar: `45c8d3d0d0a4fe468b5c433b541bf4b9b6733eff50b354fb9ba522a2ed14cebe`

Each begins with exactly the 63-character string quoted in chat, with
one additional trailing hex digit. No other property of the golden
(pixel content, sidecar fields, git blob) changed. This correction is
disclosed for record accuracy only; it changes no Decision, gate, or
approval already recorded above.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — correction
recorded 2026-09-07 by the implementing agent; disclosed for Human
Review's own awareness, not requiring re-approval since no content or
decision changed.
