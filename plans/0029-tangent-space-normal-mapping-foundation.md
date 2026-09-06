# Plan: Tangent-Space Normal Mapping Foundation

- **Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** slmao

## Objective

Implement Spec 0029 in full: a cooker-generated mesh tangent attribute
(schema 3→4, 44→60 bytes, ADR-0073), a cook-time fallback tangent for
`minimal_cube` (zero source change) and a real pole-split migration for
`pbr_sphere` (the only mesh needing one), a per-material optional
normal-map texture on both the direct-lit and IBL `PbrDirectLit` paths
(material schema 2→3, ADR-0074), a single-pointer `Material` API
extension with two mechanically-checked preconditions, three widened
Vulkan-Backend capacity limits (ADR-0072's own Accepted Amendment), two
new shaders, and one new, independent, combined normal-map+IBL+sky+
shadow demo with a fixed, computed discriminative pixel/threshold.
Every no-normal-map material's runtime behavior, shader selection, and
descriptor-binding results stay unchanged; all 9 existing goldens stay
byte-identical.

## Pre-draft verification against real, current source

Re-confirmed directly against `main` at Plan-drafting time (2026-09-06,
immediately following Spec 0029's own Human Review Approval and PR
#129's merge) via `rg` across the whole repository — not reused from
the Spec/ADRs' own citations without independently re-checking.

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
- `errors.h`: `CookError` has exactly 5 enumerators
  (`SourceFileUnreadable, SourceParseFailed, LogicalPathInvalid,
  ArtifactWriteFailed, MetadataWriteFailed`); `ArtifactDecodeError` has
  11 (ending `NonFiniteFloat, NonUnitNormal`); `MaterialSourceParseError`
  (`material_source.h`, not `errors.h`) has 8; `MaterialCookError` has
  6; `MaterialArtifactDecodeError` has 9; `RuntimeInitError`
  (`runtime/init_error.h`) ends at `ShadowLightSpaceBufferCreateFailed`,
  27 enumerators, `PbrBaseColorTextureNotSrgb` at position 15.
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
- **Material side**, all read in full: `material_types.h`
  (`MaterialKind{UnlitTextured, LitTextured, PbrDirectLit}`,
  `MaterialAssetData` with `textureAsset` only), `material_source.h`
  (`ParsedMaterialSource`, `MaterialSourceParseError`),
  `material_source.cpp` (`kMinLineCount=5`/`kMaxLineCount=8`, both
  confirmed real today via `unlit_textured_quad.material.txt`/
  `lit_textured_quad.material.txt` at 5 lines and every `pbr_*` at 8),
  `material_artifact.h`/`.cpp` (`kMaterialArtifactSchemaVersion = 2`,
  56-byte fixed record, `encodeMaterialArtifact()` takes each field as
  a separate parameter, not a struct), `material_metadata.h`/`.cpp`
  (`kExpectedLineCount = 8`, unconditional — "no optional field
  concept" for this machine-generated sidecar), `cook_material.cpp`
  (the real 4-step `cookMaterial()` body), `load_material.cpp` (the
  real `loadMaterialAsset()` cross-validation body).
- `scene_load.cpp:130-175` (`loadAndInstantiateScene()`'s own Phase 2
  material/texture loop) is the exact, real, already-confirmed
  insertion point for the new Unorm cross-validation (mirrors
  `PbrBaseColorTextureNotSrgb`'s own existing `if (kind ==
  PbrDirectLit) { ... }` block, lines 156-172, verbatim in shape).
- `material.h`/`material.cpp` (full files read): `Material`'s
  constructor takes `sampledTexture`/`sampler` (both-or-neither,
  `ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ ==
  nullptr))`), `pushConstantLayout`, `baseColorFactor`,
  `metallicFactor`, `roughnessFactor`, `environmentBinding`, in that
  order; `createMaterial()` mirrors it as a free function.
- `renderer.cpp:92-131` (`Renderer::drawFrame()`'s own draw loop): base
  color bound at 1 iff `sampledTexture() != nullptr`; IBL bound at 2/3
  iff `environmentBinding() == Ibl`; shadow bound at
  `environmentBinding() == Ibl ? 4U : 2U` — the exact, already-real
  conditional-index precedent this Plan's own normal-map binding
  mirrors.
- `material_realization.h/.cpp` (full files read): `RealizedMaterialCandidate`
  has exactly `materialAssetId, textureAssetId, newSampledTexture,
  stagingBuffer (std::optional<std::unique_ptr<Buffer>>), sampler,
  material`; `realizeOneMaterialCandidate()` builds this in one local
  variable, returning `Err` immediately on any failure — real,
  confirmed RAII rollback, `material_realization.cpp:165-255`.
  `selectShaderPair()` (lines 101-128) and `pushConstantLayoutFor()`
  (137-148) both switch on `MaterialKind` alone, no per-material
  normal-map awareness today. `sampledTextureBindingCountFor()`
  (371-380) returns `1` (`UnlitTextured`/`LitTextured`) or `2`/`4`
  (`PbrDirectLit`, no/with environment).
- `descriptor_contract.cpp:45-63`: `pbrDirectLitExpectedDescriptorContract()`
  = 4 entries, `pbrIblExpectedDescriptorContract()` = 6 — confirmed by
  direct count, matching ADR-0074.
- `vulkan_device.cpp:437-438` (`poolSizes[1].descriptorCount = 4U *
  maxSets`), `vulkan_device.cpp:1000-1002` (`ATLANTIS_CHECK(...
  sampledTextureBindingCount == 0 || ... == 4)`).
- `vulkan_command_list.h:177`: `textureDescriptorMemos_` is
  `std::array<TextureDescriptorMemo, 5>`; `bindTexture()`
  (`vulkan_command_list.cpp:432,529`) asserts `binding <
  textureDescriptorMemos_.size()` before every use.
- `compile_and_validate.cpp:136-213,366-374` (full relevant section
  read): `validateDescriptorContractForStage()` string-dispatches
  `"pbr-direct-lit"`/`"pbr-ibl"` (among others) to the matching
  `descriptor_contract.cpp` function; `validatePushConstantsForVertexStage()`'s
  own `isPbr` flag and the fragment-stage push-constant validation
  guard (line 372) both key off the identical two string literals. A
  real, `rg`-found, ADR-uncited touch point: this file must gain two
  more string branches and widen both boolean checks, or the two new
  shaders' own build-time contract validation either silently skips
  (wrong) or falls through to the `else` "unknown contract" failure
  (build break) the first time either is compiled.
- `runtime_application.cpp:372-389,1184-1185` (the real `pbrIbl*`
  loading/threading pattern) and `bootstrap_config.h:54-85` (the real
  `pbrDirectLitVertexShaderSpirvPath`-shaped field-triple convention)
  — the exact, real precedent the two new shader pairs' own
  loading/threading extension mirrors.
- `shaders/pbr_ibl/CMakeLists.txt` (full file read): one
  `atlantis_add_slang_shader_pair()` call per shader pair, one
  `add_subdirectory()` line in the root `CMakeLists.txt`
  (confirmed at lines 79-135, one call per existing pair).
- **All real `struct Vertex { ... }` sites in this repository**,
  enumerated via `rg 'struct Vertex \{'` (25 files; 14 doc/plan/ADR
  matches excluded) and independently read for their own exact field
  list — the complete, disjoint partition this Plan's own migration
  table (below) is built from; see that table for the two categories
  found (44-byte real-mesh consumers vs. hand-fed, mesh-format-
  independent geometry).
- **All real `createMaterial(` call sites**, enumerated via `rg` (14
  real, non-doc files) — see the call-site table below; exactly one
  (`material_realization.cpp`) needs a real edit, the other 13 compile
  and behave unchanged under the new trailing-default parameter.
- `pbr_render_gpu_tests.cpp:97-134` (full relevant section read): this
  file constructs real, hand-fed vertex buffers (`kTriangleVertices`,
  position/uv/normal, no color) and draws them with real
  `UnlitTextured`/`LitTextured`/`PbrDirectLit` Pipelines directly — the
  established, real, precedented location this Plan's own new
  5-sampler/`bindTexture(5, ...)` GPU test extends, mirroring its own
  existing shape rather than inventing a new file.
- `assets/materials/*.material.txt` (all 6 read): `lit_textured_quad`,
  `pbr_dielectric_rough`, `pbr_dielectric_smooth`, `pbr_metallic_rough`,
  `pbr_metallic_smooth`, `unlit_textured_quad` — the complete,
  exhaustive version-marker migration list (below).
- `assets/CMakeLists.txt`'s `atlantis_add_material_asset()`
  (`src/asset_system/CMakeLists.txt:345-391`, full function read): a
  single, required `TEXTURE` one-value arg, used only for
  `add_dependencies()` build-graph ordering — **no manifest role**
  (a material's own texture reference lives inside its `.material.txt`
  source, resolved to an `AssetId` entirely inside `cookMaterial()`).
  Confirmed real, additive change needed: one new, **optional**
  `NORMAL_MAP` one-value arg, conditionally required-declared and
  conditionally `add_dependencies()`-linked, mirroring `TEXTURE`'s own
  shape exactly.
- `pbr_direct_lit.slang`/`pbr_ibl.slang` (both read in full, prior
  phase): exact `VertexInput`/`CameraUniform`/`PushConstants`/BRDF
  bodies this Plan's own two new shaders copy verbatim, adding only the
  tangent input, the normal-map binding, and the TBN
  reconstruction/normal substitution in `fragmentMain()`.
- `output_transform_srgb.slang` (read in full): the exact, real
  tone-mapping formula this Plan's own discriminative-pixel derivation
  (below) uses — `tonemapped = max(linear,0) / (1 + max(linear,0))`,
  `kBaselineExposure = 1.0`, no manual OETF for the `*_Srgb` variant
  (hardware encodes on store).
- `pbr_material_demo.scene.txt` (read in full): the real camera-at-
  `(0,0,8)`/`fov_y=1.0472`/`near=0.1`/`far=100`, `512×512` extent
  precedent (`pbr_material_demo_fixture.h:134`) this Plan's own new
  scene reuses verbatim for its own camera.
- `scene_extraction.cpp:243-293`: `direction = normalize(-column2)`
  for a directional light — **identical** convention to the camera's
  own `-column2`-forward extraction — confirmed directly, not assumed,
  the basis for this Plan's own exact light-direction derivation
  (below).
- `renderer_ownership_tests.cpp:49-58` (`ScopedFailureHandler`) and
  `assert_tests.cpp:31-43` (confirms `ATLANTIS_CHECK` reports through a
  replaceable handler, never aborts the test process by itself) — the
  real, established pattern this Plan's own new `Material` constructor
  guard tests reuse, not a GoogleTest-style death test (this codebase
  has none).

### Existing mesh sources needing a real audit re-run (ADR-0073 Decision item 9)

Re-run at Plan-drafting time with the identical algorithm (per-triangle
UV-Jacobian determinant + per-vertex handedness), against
`assets/meshes/*.mesh.txt` — same figures the Spec/ADR already cite,
independently reconfirmed:

| Mesh | Triangles | UV-degenerate | Handedness conflicts | Action |
|---|---|---|---|---|
| `ground_plane.mesh.txt` | 2 | 0/2 | 0/4 | None |
| `textured_quad_left.mesh.txt` | 2 | 0/2 | 0/4 | None |
| `textured_quad_right.mesh.txt` | 2 | 0/2 | 0/4 | None |
| `minimal_cube.mesh.txt` | 12 | 12/12 | 0/8 | None — cook-time fallback (Milestone 1) |
| `pbr_sphere.mesh.txt` | 768 | 0/768 | 96/425 | **Pole-split migration required** (Milestone 1) |

**A real, non-pole vertex used for the new demo's discriminative
sample point (below) was independently identified and computed at
Plan-drafting time:** vertex index 200 in the current, unmigrated
`pbr_sphere.mesh.txt` — position `(0, ~0, 1)`, normal `(0, ~0, 1)`, UV
`(0, 0.5)` — has exactly 3 contributing triangles (`(175,200,201)`,
`(200,225,226)`, `(200,226,201)`), all agreeing at handedness `-1.0`
(no conflict — it is not one of the 96 pole vertices). Its own
accumulated-and-orthogonalized tangent, computed via the exact
ADR-0073 algorithm: `T = (1, 0, 0)` (to 1e-16), `tw = -1.0`, giving
`B = cross(N,T) * tw = (0, -1, 0)`. Since this vertex is unaffected by
the pole-split migration (only pole vertices change), this basis is
already correct today and stable across Implementation.

## P1. Mesh tangent-generation module — exact shape

New file pair, mirroring `mesh_source.h`/`.cpp` and
`mesh_artifact.h`/`.cpp`'s own naming convention:
`src/asset_system/include/atlantis/asset_system/mesh_tangent_generation.h`,
`src/asset_system/src/mesh_tangent_generation.cpp`.

```cpp
// mesh_tangent_generation.h
struct VertexTangent { float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f; };

// ADR-0073's own generation algorithm (accumulate, orthogonalize,
// reject on conflict, fall back on zero contribution). One entry per
// source.vertices element, same order. Never returns a partial
// vector on Err.
[[nodiscard]] atlantis::Result<std::vector<VertexTangent>, CookError> generateTangents(
    const ParsedMeshSource& source);
```

`generateTangents()` returns `CookError` directly (the same enum
`cookStaticMesh()` already returns) — no wrapper enum, since
`CookError::DegenerateTangentBasis`/`TangentHandednessConflict` are
themselves `CookError` values per ADR-0073's own Decision, unlike
`SourceParseError` (a distinct enum `cookStaticMesh()` maps through
`CookError::SourceParseFailed`).

`encodeMeshArtifact()`'s signature widens:

```cpp
[[nodiscard]] std::vector<std::byte> encodeMeshArtifact(
    AssetId assetId, const ParsedMeshSource& source, const std::vector<VertexTangent>& tangents);
```

with `ATLANTIS_CHECK(tangents.size() == source.vertices.size())` as
the function's own precondition (a programmer error if violated — the
one and only caller, `cookStaticMesh()`, always satisfies it by
construction, per AGENTS.md's error-handling rules).

`cookStaticMesh()` (`cook.cpp`) gains exactly one new step, between the
existing `parseMeshSource()` call and `encodeMeshArtifact()`:

```cpp
const auto tangentsResult = generateTangents(parsed);
if (tangentsResult.isErr()) return ResultT::Err(tangentsResult.error());
const std::vector<byte> artifactBytes = encodeMeshArtifact(assetId, parsed, tangentsResult.value());
```

**Fallback (item 4a) and handedness-conflict (item 4/5) are both
implemented inside `generateTangents()` itself**, per ADR-0073's own
Decision — not split across two functions — since both operate on the
same per-vertex accumulator state built in one pass over all
triangles.

## P2. `errors.h` — five new enumerators, exact placement

```cpp
enum class CookError {
  SourceFileUnreadable, SourceParseFailed, LogicalPathInvalid,
  ArtifactWriteFailed, MetadataWriteFailed,
  DegenerateTangentBasis,       // new
  TangentHandednessConflict,    // new
};

enum class ArtifactDecodeError {
  TooSmallForHeader, BadMagic, UnknownSchemaVersion, UnsupportedVertexStride,
  InconsistentOffsets, SizeMismatch, VertexCountOutOfRange,
  IndexCountNotMultipleOfThree, IndexOutOfRange, NonFiniteFloat, NonUnitNormal,
  NonUnitTangent,           // new
  NonOrthogonalTangent,     // new
  InvalidTangentHandedness, // new
};
```

`material_source.h`'s own `MaterialSourceParseError` gains one:

```cpp
enum class MaterialSourceParseError {
  UnknownSourceVersion, MissingField, FieldOrderMismatch, UnknownKind,
  UnknownFilter, UnknownAddressMode, TrailingContent, MalformedNumber,
  NormalMapNotSupportedForKind,  // new
};
```

`runtime/init_error.h`'s own `RuntimeInitError` gains one, appended
after `ShadowLightSpaceBufferCreateFailed` (the current last
enumerator):

```cpp
enum class RuntimeInitError {
  /* ...unchanged... */
  ShadowLightSpaceBufferCreateFailed,
  PbrNormalMapTextureNotUnorm,  // new
};
```

`init_error.cpp`'s own `toString()` gains the matching `case`.

## P3. Fixed epsilons and fallback-axis tie-break (restated from ADR-0073, not re-derived)

- UV-degeneracy: `|det| < 1e-12`.
- Orthogonalization-degeneracy: `< 1e-6` (`kDegenerateLengthEpsilon`,
  reused).
- Handedness zero-tiebreak: `< 1e-9` → `+1.0`.
- Fallback axis selection (item 4a): compare `|dot(N,X)|`,
  `|dot(N,Y)|`, `|dot(N,Z)|`; pick the smallest; tie-break order
  `X, Y, Z`.
- Decode-time orthogonality: `|dot(N,T)| < 1e-3`.

No new epsilon is introduced beyond what ADR-0073 already fixed —
listed here only so Milestone 1's own implementation has one place to
read them from without re-opening the ADR.

## P4. `pbr_sphere.mesh.txt` pole-split migration — deterministic method, audit command, acceptance gate

**Method (mechanical, not a judgment call):** for each of the two
poles (north, `y=+1`; south, `y=-1`), the existing per-longitude-
segment duplicate vertices (24 copies each, one per longitude column,
already present — Pre-draft verification's own `head` sample shows
this) are insufficient because a **triangle wedge**, not a longitude
column, is the unit that must agree on handedness. The migration
duplicates each pole vertex **once more, per triangle wedge** rather
than per longitude column: instead of vertex `i` being shared by the 2
triangles of longitude segment `i` (a wedge spans from longitude `i`'s
own column to longitude `i+1`'s own column), each pole gets one
distinct vertex copy per wedge (24 wedges around the pole → 24 vertex
copies, one triangle pair each, sharing no vertex index with its own
neighbor). This is the same "one copy per consumer, no sharing" idea
this mesh's own pole already applies at the *column* granularity,
carried one level further to the *wedge* granularity — not a new
topology concept (ADR-0073's own Decision item 9).

**Concrete script-level procedure** (temporary, uncommitted, run by
Implementation — not part of the shipped cooker):

1. Parse `pbr_sphere.mesh.txt`; identify pole vertices (`|y| == 1`,
   confirmed by the audit's own handedness-conflict output — the same
   96 indices already found).
2. For each pole vertex, find its own conflicting triangle set (the
   audit's own per-face handedness list, already computed above).
3. Replace the single shared pole-vertex index in each triangle with
   a **new**, appended vertex — an exact copy of the original pole
   vertex's own position/color/UV/normal — so no two triangles that
   previously disagreed in handedness still share an index.
4. Renumber `vertex_count`/`index_count` in the file header to match.
5. Re-run the identical audit script (Pre-draft verification's own
   method) against the candidate file.

**Acceptance gate (mechanical, not subjective):** the re-run audit
must report `0/N` UV-degenerate triangles (unchanged — this mesh was
already `0/768`) **and** `0/M` handedness conflicts, where `M` is the
new, larger vertex count. If it does not, Implementation stops and
requests Human Review (per Spec 0029's own "no per-mesh Pipeline
split" instruction) rather than inventing a different splitting rule.

**Explicitly unaffected, confirmed by the migration's own scope:**
every already-non-pole vertex (329 of 425 today) keeps its exact
position/color/UV/normal values and its exact vertex index — including
vertex 200, the demo's own discriminative sample point (above), which
is nowhere near either pole (`y ≈ 0`).

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
gains a trailing `AssetId normalMapTexture` parameter, appended after
`roughnessFactor`, serialized via the existing `appendU64LE()` at
offset 56. `decodeMaterialArtifact()` gains, after the existing
`roughnessFactor` decode: `decoded.normalMapTexture =
readU64LE(bytes.data() + 56);` — no range check (an `AssetId` is an
opaque hash, `0` = none, already the established "unassigned"
convention). `DecodedMaterialArtifact`/`MaterialAssetData` each gain
`AssetId normalMapTexture = 0;`.

`material_metadata.h`: `MaterialMetadata` gains `AssetId
normalMapTexture = 0;`. `material_metadata.cpp`: `kExpectedLineCount`
becomes `9`; a new, **unconditional** 9th line,
`normal_map_texture: <16-hex-digit AssetId>` (matching
`texture_asset:`'s own hex encoding exactly, `0000000000000000` when
absent) — unconditional because this sidecar is machine-generated only
(its own existing comment: "no optional field concept here").

`cookMaterial()` (`cook_material.cpp`) gains, after the existing
Step 3 (texture identity): normalize `parsed.normalMapLogicalPath` via
the identical `normalizeLogicalPath()`/`computeAssetId()` pair (only
when non-empty; `0` otherwise — no `LogicalPathInvalid` check on an
empty string, mirroring how an absent optional never reaches
`normalizeLogicalPath()` elsewhere in this codebase); thread the
result into both `encodeMaterialArtifact()`'s new parameter and
`metadata.normalMapTexture`.

`load_material.cpp`'s `loadMaterialAsset()` gains one more field in
its existing cross-validation `if`:
`artifact.normalMapTexture != metadata.normalMapTexture` (added to the
existing `kind`/`textureAsset` check), and `data.normalMapTexture =
artifact.normalMapTexture;` in the final `MaterialAssetData` build.

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
`pbr_normal_mapped_control.material.txt` (8 lines, identical to the
first in every other field, no `normal_map:` line) — the discriminative
test's own A/B twin pair.

## P8. Scene dependency loading — exact `scene_load.cpp` change

Inserted immediately after the existing `PbrBaseColorTextureNotSrgb`
block (`scene_load.cpp:156-172`), inside the same per-material loop,
mirroring the base-color texture's own dedup-then-validate shape:

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
populates (keyed by `AssetId` regardless of which material field
references it) — no new map, no new cache, matching ADR-0074 item 4's
own "no new resource-map type" contract exactly.

## P9. `atlantis_add_material_asset()` — exact CMake widening

`src/asset_system/CMakeLists.txt:345-391`:

```cmake
function(atlantis_add_material_asset)
  set(options "")
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
  ...
endfunction()
```

Every existing call site (6, per P7's own table) omits `NORMAL_MAP` and
is unaffected.

## P10. `Material`/`createMaterial()` — exact diff

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

`createMaterial()` gains the identical trailing parameter, forwarded
into `Material`'s constructor unchanged.

`renderer.cpp`'s draw loop gains, immediately after the existing shadow
binding block:

```cpp
if (item.material->normalMapTexture() != nullptr) {
  const std::uint32_t normalMapBinding = item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 5U : 3U;
  cmd.bindTexture(normalMapBinding, *item.material->normalMapTexture(), *item.material->sampler());
}
```

## P11. `RealizedMaterialCandidate`/`realizeOneMaterialCandidate()` — exact diff

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
`materialData.normalMapTexture` (skipped entirely, leaving
`newNormalMapTexture` null, when that value is `0`). The call to
`createMaterial()` gains the new trailing `normalMapTexturePtr`
argument (the just-created or dedup-reused pointer, or `nullptr`).
`realizePendingMaterials()` publishes `newNormalMapTexture` into the
**same** `sampledTextureResourceMap_` the base-color texture already
uses (keyed by `normalMapTextureAssetId`) — after the same
`submit()`/`waitIdle()` gate, no new map.

`selectShaderPair()`/`realizeOneMaterialCandidate()`/`realizePendingMaterials()`
each gain two more shader-pair trios as trailing parameters
(`pbrDirectLitNormalMap*`, `pbrIblNormalMap*` — vertex layout, vertex
SPIR-V, fragment SPIR-V each), inserted immediately after the existing
`pbrIbl*` trio, mirroring its own insertion point exactly (Plan 0025's
own precedent for adding the `pbrIbl*` trio itself).
`selectShaderPair()`'s own `PbrDirectLit` case becomes:

```cpp
case MaterialKind::PbrDirectLit:
  if (hasNormalMap) {
    return environmentEnabled ? pbrIblNormalMapTriple : pbrDirectLitNormalMapTriple;
  }
  return environmentEnabled ? pbrIblTriple : pbrDirectLitTriple;
```

where `hasNormalMap` is a new `bool` parameter, threaded from
`materialData.normalMapTexture != 0` at the one real call site
(`realizeOneMaterialCandidate()`). `UnlitTextured`/`LitTextured`'s own
branches never read `hasNormalMap` — per P5's own grammar restriction,
it is always `false` for those kinds.

`sampledTextureBindingCountFor()` gains a `bool hasNormalMap`
parameter: `PbrDirectLit` returns `environmentEnabled ? (hasNormalMap
? 5U : 4U) : (hasNormalMap ? 3U : 2U)`; the other two kinds ignore it
(always `1U`).

## P12. `createMaterial()` call-site migration — exhaustive, all 14 real sites

Every site compiles and behaves **unchanged** except the one marked
"Edited" — the new parameter defaults to `nullptr`.

| File | Change |
|---|---|
| `src/runtime/src/material_realization.cpp` | **Edited** — threads the new normal-map pointer (P11) |
| `src/runtime/src/runtime_application.cpp` | None (calls `realizePendingMaterials()`, not `createMaterial()` directly; gains the two new shader-trio parameters, P11) |
| `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` | None |
| `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp` | None |
| `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` | None |
| `tests/runtime/pbr_render_gpu_tests.cpp` | None for existing calls; **new** calls added for the new 5-sampler test (Milestone 4) |
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

## P13. Vertex-layout/stride migration — exhaustive, all real `struct Vertex` sites

**Category A — real 44-byte mesh-consuming `Vertex` structs, widen to
60 bytes (append `float tangent[4];`) plus a 6th `static_assert`:**

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
atlantis::asset_system::kMeshArtifactTangentOffsetBytes);` and the
existing `sizeof(Vertex) == kMeshArtifactVertexStrideBytes` assertion
now checks against the new `60`.

**Category B — no `static_assert`s today (own comment: "never actually
uploads real vertex data" / "tests Material/Pipeline construction, not
drawing"), widened only where a normal-map vertex layout needs a real
offset to name — no correctness requirement, matching the identical
precedent Plan 0020 already established for `normal`:**

| File | Line | Change |
|---|---|---|
| `tests/runtime/material_realization_gpu_tests.cpp` | 82 | append `float tangent[4];`, add a new `pbrDirectLitNormalMapVertexLayout()`-style helper |
| `tests/runtime/pbr_render_gpu_tests.cpp` | 106 | append `float tangent[4];` (its own comment already discloses "position/uv/normal only, no color" — this file's own minimal, sufficient sub-schema convention continues; tangent is appended for the new binding-5 test, Milestone 4) |

**Category C — hand-fed, position(+color)-only geometry, never tied to
`mesh_artifact`'s schema, confirmed by direct reading NOT to draw
through any mesh-loading path — genuinely unaffected, matching Plan
0020's own identical "not a required touch point" finding for this
exact file set:**

`examples/minimal_renderer_demo/main.cpp`,
`examples/headless_rendering_demo/main.cpp`,
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
`tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp`,
`tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp`,
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp`,
`tests/vulkan_backend/shadow_map_render_gpu_tests.cpp`,
`tests/image_regression/sky_background_gpu_tests.cpp`.

New file: `tests/image_regression/fixture/pbr_normal_map_demo_fixture.cpp`
(Milestone 5) starts directly at the 60-byte, tangent-including shape
— not a migration.

## P14. Descriptor capacity — exact three-line diff (ADR-0072's own Accepted Amendment)

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

## P15. New descriptor contracts and shader-compiler contract wiring — exact diff

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
`isPbr` becomes `expectedContract == "pbr-direct-lit" ||
expectedContract == "pbr-ibl" || expectedContract ==
"pbr-direct-lit-normal-map" || expectedContract ==
"pbr-ibl-normal-map"`; the fragment-stage push-constant validation
guard (line 372) widens identically.

## P16. Two new shader files — exact structure

`shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang` and
`shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.slang`, each with its
own `CMakeLists.txt` (`atlantis_add_slang_shader_pair(NAME
pbr_direct_lit_normal_map ... EXPECTED_CONTRACT
pbr-direct-lit-normal-map)`, mirroring `pbr_ibl/CMakeLists.txt`
verbatim), and one new `add_subdirectory()` line each in the root
`CMakeLists.txt`, alongside the existing `pbr_direct_lit`/`pbr_ibl`
lines.

Content (per ADR-0074 Section 5, restated here as the exact diff from
each shader's own real, existing twin):

- `VertexInput` gains `[[vk::location(3)]] float4 tangent;`.
- A `normalMapSampler` `Sampler2D` binding, `[[vk::binding(3,0)]]`
  (direct) / `[[vk::binding(5,0)]]` (IBL).
- `vertexMain()` gains `output.worldTangent = mul((float3x3)pushConstants.objectToWorld,
  input.tangent.xyz); output.tangentHandedness = input.tangent.w;`
  (two new `Varying` fields), using the exact same submatrix/gate the
  existing `worldNormal` line already uses.
- `fragmentMain()` gains, before the existing BRDF loops: `float3 N_geo
  = normalize(input.worldNormal); float3 T = normalize(input.worldTangent -
  N_geo * dot(N_geo, input.worldTangent)); float3 B = cross(N_geo, T) *
  input.tangentHandedness; float3 texelN = normalMapSampler.Sample(input.uv).rgb *
  2.0 - 1.0; float3 N = normalize(texelN.x * T + texelN.y * B + texelN.z *
  N_geo);` — `N` (not `N_geo`) is then used everywhere `N` already
  appears in the existing BRDF/IBL body, unchanged otherwise.
- No push-constant, uniform-buffer, or existing-binding change of any
  kind.

`BootstrapConfig` gains 6 new `std::string` fields (3 per shader pair,
mirroring `pbrIblVertexShaderSpirvPath`'s own 3-field shape).
`runtime_application.cpp` gains the matching load/store block (mirrors
lines 372-389 exactly) and threads both new trios into its own
`realizePendingMaterials()` call (line ~1184-1185).

## P17. New demo — fixed scene, camera, light, material, pixel, threshold

**Assets (new):**
- `assets/textures/normal_map_tilted_source_unorm.png` — a uniform
  1×1 (or a small, e.g. 4×4, uniform-fill for authoring-tool
  compatibility if 1×1 is rejected by the PNG encoder used — confirmed
  at Implementation time) RGB8 image, pixel value `(204, 128, 230)`
  exactly. Decoded tangent-space normal (before shader normalization):
  `(0.6, 0.00392, 0.80392)` — a real, fixed, ~37° tilt toward `+T`,
  chosen (not arbitrary) to maximize the discriminative delta at the
  sample point below while keeping the source trivially reproducible
  (one uniform color, no procedural generation needed). Cooked via
  `atlantis_add_texture_asset(NAME normal_map_tilted COLOR_SPACE Unorm)`.
- `assets/materials/pbr_normal_mapped.material.txt` — `kind:
  pbr_direct_lit`, `texture: textures/textured_quad_source_srgb.png`
  (reused, no new base-color texture), `filter: linear`,
  `address_mode: repeat`, `base_color_factor: 1.0 1.0 1.0 1.0`,
  `metallic_factor: 0.0`, `roughness_factor: 0.5`, `normal_map:
  textures/normal_map_tilted_source_unorm.png` (9 lines).
- `assets/materials/pbr_normal_mapped_control.material.txt` —
  identical, minus the `normal_map:` line (8 lines) — the
  discriminative test's own "B" twin.
- `assets/scenes/pbr_normal_map_demo.scene.txt` — reuses `pbr_sphere`
  (post-migration). One sphere node, `position=0 0 0 rotation=0 0 0
  scale=1 1 1`, `material=materials/pbr_normal_mapped.material.txt`;
  one camera node, `position=0 0 8 rotation=0 0 0 fov_y=1.0472
  near=0.1 far=100.0` (reusing `pbr_material_demo_scene`'s own real
  camera numbers verbatim); one directional light,
  `position=0 0 0 rotation=0 0 0 color=1.0 1.0 1.0 intensity=3.0`
  (identity rotation, deliberate — see derivation below, a real,
  disclosed departure from other demo scenes' own "never
  axis-aligned" convention, chosen here specifically so the
  discriminative pixel is hand-computable, not to re-test direction/
  axis correctness, which existing goldens already cover).
- Environment: `ibl_studio` (existing, reused, no new asset) —
  activates `environmentEnabled = true`, selecting
  `pbr_ibl_normal_map.slang` for this material.
- Extent: `512×512` (reusing `pbr_material_demo`'s own precedent).

**Derivation of the light/camera/sample-point geometry (real,
computed, not assumed):** `scene_extraction.cpp`'s own confirmed
`direction = normalize(-column2)` convention means identity rotation
(`0,0,0`) on both the camera and the light gives camera-forward
`(0,0,-1)` and light-direction `(0,0,-1)` (so `L = -direction =
(0,0,1)`) — both confirmed against real code, not assumed. Placing the
sphere at the world origin with identity rotation/scale means vertex
200's own object-space position `(0, ~0, 1)` is also its world
position exactly (satisfies `checkConformalTransform()` trivially —
identity is conformal). With the camera at `(0,0,8)`, `V =
normalize((0,0,8)-(0,0,1)) = (0,0,1)`. Vertex 200 therefore sits
exactly on the camera's own optical axis — **its own screen-space
projection is the exact center pixel, `(256, 256)` for a `512×512`
target**, with no perspective-matrix derivation needed (unlike Plan
0028's own off-axis sample point).

**Discriminative pixel test — real, computed BRDF+tonemap+encode
values (script-computed at Plan-drafting time, Cook-Torrance formula
transcribed verbatim from `pbr_direct_lit.slang`, Reinhard/exposure
formula transcribed verbatim from `output_transform_srgb.slang`):**
with `N = (0,0,1)` (no normal map, the control material), `L = V =
(0,0,1)`, `baseColorFactor=(1,1,1,1)`, `metallic=0`, `roughness=0.5`,
`radiance=(3,3,3)`: final 8-bit `rgbSum = 570` (worst case with a pure
white base-color texel; the real `textured_quad_source_srgb.png`
texel at this UV is ≤ white, so this is an upper bound on the "no
normal map" sample). With the perturbed, decoded-and-normalized
tangent-space normal `(0.598, 0.0039, 0.801)` substituted for `N`
(everything else identical): `rgbSum = 522`. **Delta = 48** at
`roughness=0.5`; independently re-verified at `roughness ∈ {0.3, 0.9}`
and `baseColor ∈ {white, mid-gray, dark}` (a 3×3 sweep), the delta
never drops below **27** across that whole range. Neither this
sweep nor the single computed value above accounts for the scene's
own real IBL ambient term (adds signal in the same direction for a
diffuse/rough dielectric material, per the environment's own real
irradiance response to a changed `N` — not modeled exactly here) or
the real, sub-white base-color texel (reduces the diffuse term
proportionally, leaving the metallic-independent specular term's own
delta untouched, since `F0 = 0.04` fixed at `metallic = 0`).

**Threshold, disclosed precisely, mirroring Plan 0028's own exact
"conservative, not-yet-measured, stop-and-ask-if-wrong" methodology:**
`rgbSum(pixelAt(controlRender, 256, 256)) - rgbSum(pixelAt(normalMapRender,
256, 256)) > 20` — comfortably below every value in the 27-48 sweep
above, leaving real margin for the unmodeled IBL/texture effects noted
above. **This has not been measured on real GPU hardware for this
exact scene.** If Implementation's own real capture shows this bound
does not hold, stop and request Human Review before changing it —
never retune it silently, per this repository's own established
non-negotiable rule for exactly this class of Plan-stage estimate.

**Fixture/golden_generator/gpu_tests trio** (new): mirrors
`integrated_showcase_demo_fixture.cpp`'s own combined PBR+IBL+sky+
shadow wiring shape exactly (explicit sky Pipeline construction from
the shared `sky.slang`, no new sky shader) plus
`pbr_material_demo_fixture.cpp`'s own PBR-realization shape, using the
new `pbrDirectLitNormalMapVertexLayout()`/`pbrIblNormalMapVertexLayout()`
functions (location 0/1/2/3 = position/uv/normal/tangent):
`tests/image_regression/fixture/pbr_normal_map_demo_fixture.h/.cpp`,
`tests/image_regression/golden_generator/pbr_normal_map_demo_main.cpp`,
`tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp`. The
fixture's own render function takes the material logical name
(`pbr_normal_mapped` vs. `pbr_normal_mapped_control`) as a parameter,
so the discriminative test can call it twice against the one shared
scene skeleton — never two separate `.scene.txt` files for the A/B
comparison (the sphere's own `material=` line is the only difference,
resolved at the fixture's own call-site by pointing the scene loader
at whichever of the two cooked material assets the caller names).

**Golden:** exactly one, `pbr_normal_map_demo`, captured against the
`pbr_normal_mapped` (non-control) material via ADR-0042's existing
two-phase candidate-generate → human-review process.

## P18. New 5-sampler/`bindTexture(5, ...)` GPU test — exact location and shape

Extends `tests/runtime/pbr_render_gpu_tests.cpp` (P13 Category B):
after this file's own existing `litOrPbrLayout()`, a new
`pbrIblNormalMapLayout()` (location 0/1/2/3 = position/uv/normal/
tangent) and a new `TEST_CASE` that: creates a Pipeline with
`sampledTextureBindingCount == 5` and `EXPECTED_CONTRACT
"pbr-ibl-normal-map"`; allocates a real descriptor set; calls
`cmd.bindUniformBuffer(...)`, `cmd.bindTexture(1, ...)`,
`cmd.bindTexture(2, ...)`, `cmd.bindTexture(3, ...)`,
`cmd.bindTexture(4, ...)`, and **`cmd.bindTexture(5, ...)`** (the one
call that exercises `textureDescriptorMemos_`'s own widened size — a
Pipeline/descriptor-set creation alone does not); submits a real draw
of `kTriangleVertices` (widened with a fixed, arbitrary-but-valid
`tangent` value, e.g. `(1,0,0,1)`); confirms success and zero Vulkan
Validation Layers hits.

## P19. `Material` constructor guard tests — exact location and shape

New `TEST_CASE`s in `tests/renderer/renderer_ownership_tests.cpp`
(already owns `ScopedFailureHandler`, P19 reuses it verbatim, no new
helper):

1. `ScopedFailureHandler` installed; construct `Material` with
   `normalMapTexture` non-null, `sampledTexture`/`sampler` both
   `nullptr`; `REQUIRE(failures.size() == 1)`.
2. `ScopedFailureHandler` installed; construct `Material` with a valid
   base-color pair, `normalMapTexture` non-null,
   `pushConstantLayout = ObjectToWorldOnly`;
   `REQUIRE(failures.size() == 1)`.
3. No handler needed; construct `Material` with a valid base-color
   pair, `normalMapTexture` non-null, `pushConstantLayout =
   PbrDirectLit`, once for `MaterialEnvironmentBinding::None` and once
   for `Ibl` — both must construct without any captured failure.

## Milestones / Task Breakdown

Six milestones, strictly ordered — each is atomic internally for the
identical reason Plan 0020's own Milestone 1 was (a single, global,
compile-time schema constant; no safe intermediate tree state).

1. **Mesh format extension (atomic).** P1-P4, P13 Category A/B's own
   mesh-side entries, the RHI/reflection `Float4` five-file diff
   (Pre-draft verification), `errors.h`'s two new `CookError`/three new
   `ArtifactDecodeError` values (P2), `pbr_sphere.mesh.txt`'s own real
   pole-split (P4) verified against its own acceptance gate,
   `minimal_cube.mesh.txt` left byte-for-byte unchanged, new
   GPU-independent unit tests (tangent generation against a
   hand-computable flat-UV triangle; the fallback against a
   hand-computable zero-contribution vertex; a constructed
   handedness-conflict case; the narrowed `DegenerateTangentBasis`;
   `NonUnitTangent`/`NonOrthogonalTangent`/`InvalidTangentHandedness`).
   **Acceptance gate:** all 9 existing goldens byte-identical; every
   composition root builds; `pbr_sphere`'s own re-audit reports zero
   conflicts.
2. **Material schema and dependency chain (atomic).** P5-P9, the one
   new `MaterialSourceParseError` enumerator, the one new
   `RuntimeInitError` enumerator, all 6 existing `.material.txt`
   version-marker edits (P7), new unit tests (`NormalMapNotSupportedForKind`,
   `MissingField` on empty `normal_map:`, artifact/metadata
   cross-validation of the new field, cook/decode round-trip).
   **Acceptance gate:** all 9 goldens byte-identical (no scene yet
   references a normal map); every existing material still cooks and
   loads unchanged.
3. **Renderer `Material` API and realization plumbing (atomic).**
   P10-P12, P19's own guard tests. `selectShaderPair()`/
   `sampledTextureBindingCountFor()`/`realizeOneMaterialCandidate()`/
   `realizePendingMaterials()` widened to accept (but not yet be
   given, until Milestone 4) the two new shader trios — this
   milestone compiles against the **existing** `pbr_direct_lit.slang`/
   `pbr_ibl.slang` trios passed twice (once for the plain slot, once
   as a placeholder for the not-yet-created normal-map slot) **only if
   Milestone 4 cannot immediately follow in the same commit**; since
   this Plan lands all six milestones as one Implementation PR (no
   intermediate merge), this placeholder is unnecessary — Milestone 3
   and Milestone 4 are implemented together in source but verified as
   two separate, reviewable diffs within the one PR, avoiding any real
   half-built intermediate state. **Acceptance gate:** all 9 goldens
   byte-identical; the 13 unaffected `createMaterial()` call sites
   confirmed to still compile unchanged.
4. **Descriptor capacity, two new shaders, CMake/shader-compiler
   wiring (atomic).** P14-P18. New shader files compile and pass their
   own `EXPECTED_CONTRACT` validation; `BootstrapConfig`/
   `runtime_application.cpp` load and thread both new trios. **Real
   GPU verification required this milestone, not deferred:** the new
   binding-5 test (P18) and the existing `N+4`/`N+5` set-count test
   (re-run unmodified, confirming it still passes — a different axis,
   per ADR-0074 Section 3). **Acceptance gate:** all 9 goldens
   byte-identical; zero Vulkan Validation Layers hits; the new P18
   test passes.
5. **New demo assets/scene/fixture/golden (atomic).** P17. New
   texture/material/scene assets; new fixture/golden_generator/
   gpu_tests trio; the discriminative pixel test (P17's own fixed
   pixel/threshold); ADR-0042's own candidate-generate step, followed
   by human review and commit of the final golden. **Acceptance gate:**
   the discriminative test passes against real GPU output; the new
   golden is human-reviewed and approved; all 9 existing goldens still
   byte-identical (re-confirmed once more, since this is the milestone
   most likely to accidentally touch a shared asset).
6. **Full verification pass and registry/documentation closeout.** No
   new source change. Runs the complete Verification Checklist below
   end to end, both `ATLANTIS_BUILD_TESTS` configurations, both build
   configurations, records the real, final numbers. `specs/README.md`'s
   Spec 0029 row updated to reflect this Plan's own real Implementation
   state; this file's own "Post-Merge Status Update" section added at
   merge time. If any gate fails, Implementation returns to the
   relevant earlier milestone, never forward.

## Files / Modules Touched (expected)

- `src/asset_system/include/atlantis/asset_system/mesh_tangent_generation.h` (new), `src/asset_system/src/mesh_tangent_generation.cpp` (new) — Milestone 1
- `src/asset_system/include/atlantis/asset_system/mesh_artifact.h`, `src/asset_system/src/mesh_artifact.cpp` — Milestone 1
- `src/asset_system/src/cook.cpp` — Milestone 1
- `src/asset_system/include/atlantis/asset_system/errors.h` — Milestones 1-2
- `src/rhi/include/atlantis/rhi/types.h`, `src/shader_system/include/atlantis/shader_system/reflection_metadata.h`, `src/shader_system/src/slang_json_transform.cpp`, `src/shader_system/src/reflection_loader.cpp`, `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp` — Milestone 1 (`Float4`)
- `assets/meshes/pbr_sphere.mesh.txt` — Milestone 1 (pole-split; `minimal_cube.mesh.txt` NOT touched)
- All 10 Category A + 2 Category B files in P13's own tables — Milestone 1 (Category A), Milestone 4 (Category B's `pbr_render_gpu_tests.cpp`/`material_realization_gpu_tests.cpp`)
- New `tests/asset_system/mesh_tangent_generation_tests.cpp`, extended `tests/asset_system/mesh_artifact_tests.cpp` — Milestone 1
- `src/asset_system/include/atlantis/asset_system/material_types.h`, `material_source.h`/`.cpp`, `material_artifact.h`/`.cpp`, `material_metadata.h`/`.cpp` — Milestone 2
- `src/asset_system/src/cook_material.cpp`, `src/asset_system/src/load_material.cpp` — Milestone 2
- `src/runtime/include/atlantis/runtime/init_error.h`, `src/runtime/src/init_error.cpp`, `src/runtime/src/scene_load.cpp` — Milestone 2
- `src/asset_system/CMakeLists.txt` (`atlantis_add_material_asset()`), `assets/CMakeLists.txt` (6 version-marker edits) — Milestone 2
- New `tests/asset_system/material_source_tests.cpp`/`material_artifact_tests.cpp`/`material_metadata_tests.cpp` extensions, new `tests/runtime/scene_load_normal_map_tests.cpp`-shaped additions — Milestone 2
- `src/renderer/include/atlantis/renderer/material.h`, `src/renderer/src/material.cpp` — Milestone 3
- `src/renderer/src/renderer.cpp` — Milestone 3
- `src/runtime/include/atlantis/runtime/material_realization.h`, `src/runtime/src/material_realization.cpp` — Milestone 3
- `tests/renderer/renderer_ownership_tests.cpp` (P19) — Milestone 3
- `src/vulkan_backend/src/vulkan_device.cpp`, `src/vulkan_backend/src/vulkan_command_list.h` — Milestone 4
- `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`, `src/shader_system/src/descriptor_contract.cpp` — Milestone 4
- `src/tools/shader_compiler/compile_and_validate.cpp` — Milestone 4
- New `shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang`+`CMakeLists.txt`, `shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.slang`+`CMakeLists.txt` — Milestone 4
- `CMakeLists.txt` (root, two new `add_subdirectory()`) — Milestone 4
- `src/runtime/include/atlantis/runtime/bootstrap_config.h`, `src/runtime/src/runtime_application.cpp` — Milestone 4
- `tests/runtime/pbr_render_gpu_tests.cpp` (P18) — Milestone 4
- New `assets/textures/normal_map_tilted_source_unorm.png`, `assets/materials/pbr_normal_mapped.material.txt`, `assets/materials/pbr_normal_mapped_control.material.txt`, `assets/scenes/pbr_normal_map_demo.scene.txt` — Milestone 5
- `assets/CMakeLists.txt` (new asset declarations) — Milestone 5
- New `tests/image_regression/fixture/pbr_normal_map_demo_fixture.h`/`.cpp`, `tests/image_regression/golden_generator/pbr_normal_map_demo_main.cpp`, `tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp`, `tests/image_regression/goldens/pbr_normal_map_demo/*` — Milestone 5
- `tests/image_regression/CMakeLists.txt` — Milestone 5
- [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Related Plan(s)` field only), `specs/README.md` — Milestone 6
- This file's own "Post-Merge Status Update" section — Milestone 6

**Not touched by this Plan** (confirmed by the P13 Category C list and
by the call-site table in P12): `src/world/`, `src/render_graph/`,
`src/rhi/` beyond the one `Float4` enumerator, `pbr_direct_lit.slang`/
`pbr_ibl.slang` themselves (zero edits — both new shaders are
additions), `integrated_showcase_demo`'s own scene/fixture/golden, any
`adr/` file (ADR-0073/0074 and the three Amendments are already
`Accepted`), any of the 9 existing image-regression goldens' own PNG
bytes.

## Sequencing & Dependencies

Milestone 1 → 2 → 3 → 4 → 5 → 6, strictly. Milestone 2 depends on
Milestone 1's own tangent attribute existing (the new material grammar
references a texture, not the mesh directly, but the combined demo in
Milestone 5 needs a tangent-bearing mesh to be meaningful). Milestone 3
depends on Milestone 2's own `normalMapTexture` field existing on
`MaterialAssetData`. Milestone 4 depends on Milestone 3's own
`Material`/`RealizedMaterialCandidate` plumbing compiling. Milestone 5
depends on Milestone 4's own two real shaders and widened capacity
existing (the demo cannot render without them). Milestone 6 depends on
Milestone 5's own golden being captured and approved.

**No known unavoidable intermediate compile-break or real-GPU red
window** — Milestones 3/4 are the closest to one (a widened
`selectShaderPair()` signature requires its one real caller,
`realizeOneMaterialCandidate()`, and that function's one real caller,
`runtime_application.cpp`, to update in the same commit); this Plan's
own Milestone boundaries are reviewable-diff boundaries within one
Implementation PR, not separate merged commits to `main` — matching
Plan 0020's own explicit "landed as one atomic step, reviewed as
several" precedent. If real Implementation finds an unavoidable
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
      artifact/metadata cross-validation of the new field, the P19
      `Material` constructor guard tests (P19), `sampledTextureBindingCountFor()`'s
      new `3`/`5` results.
- [ ] Real-GPU tests: the P18 binding-5/`bindTexture(5, ...)` test; the
      existing `N+4`/`N+5` descriptor-set-count test re-run unmodified;
      the new fixture's own non-degenerate-frame proof; the P17
      discriminative pixel test (`rgbSum` delta `> 20` at `(256,256)`).
- [ ] Image regression: all 9 existing goldens byte-identical after
      every milestone that could plausibly affect them (1, 2, 3, 4, 5);
      exactly one new golden (`pbr_normal_map_demo`), captured and
      human-reviewed via ADR-0042's two-phase process, never
      auto-accepted.
- [ ] Vulkan Validation Layers: zero `VUID`/Validation Error/Warning
      hits across the full verbose GPU test output, Debug and Release.
- [ ] `ctest -LE gpu` and `ctest -L gpu`, Debug and Release.
- [ ] Fresh `-DATLANTIS_BUILD_TESTS=OFF` configure+build: produces a
      working `atlantis_runtime.exe`, zero test executables, and
      successfully re-cooks every mesh/material/texture asset under
      the new schema versions with no gap (mirrors Plan 0020's own
      identical gate).
- [ ] Module/link graph: `Atlantis::AssetSystem` still links
      `Atlantis::Core` only; `Atlantis::RHI`'s public API confirmed
      unchanged beyond the one `Float4` enumerator.
- [ ] `Vk*` isolation: only the Vulkan Backend module references any
      `Vk*` type (unaffected by this Plan; re-confirmed, not assumed).
- [ ] `git diff --check` clean.
- [ ] Every existing `createMaterial()` call site (P12's own table)
      confirmed to still compile with zero source edit, except
      `material_realization.cpp`.

## Rollback Plan

Revert the Implementation PR's own commit(s) on `main`. No migration
mechanism exists for any of the widened formats (mesh artifact schema
4, material artifact schema 3, material source version 3) — a revert
returns every build-output artifact to the prior schema on the next
clean configure, since none of this Plan's own artifacts are
independently distributed or hand-edited outside the build. The two
new, real, checked-in mesh/material/scene/texture source files (P17)
and `pbr_sphere.mesh.txt`'s own migrated content are removed by the
same revert; no other checked-in asset is modified by this Plan
(`minimal_cube.mesh.txt` stays byte-for-byte unchanged throughout).

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No delta beyond the Verification Checklist above.
