# Plan: Mesh UV Attribute Foundation

- **Spec:** [specs/0017-mesh-uv-attribute-foundation.md](../specs/0017-mesh-uv-attribute-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-25 — see that Spec's own approval note for the full 14-item accepted record)
- **Status:** In Review
- **Author:** slmao

This Plan implements Spec 0017 and
[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
(`Accepted`) exactly as approved. It does not redesign, reopen, or
narrow any decision either document already settled — every "D" section
below cites the specific Spec Decision item and/or ADR-0058 Decision
item it implements, and stops at *how*, never revisiting *what* or
*why*. [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
own "Accepted Amendment — 2026-08-25" is the governing record for the
widened mesh format scope this Plan implements; nothing here amends any
ADR further.

## Objective

Make UV0 a real, mandatory attribute of Asset System's one static mesh
vertex layout — authoring source → cook → runtime artifact → load →
composition-root GPU `Mesh` — and prove, on real GPU hardware, that a
genuinely asset-sourced mesh's UV0 data drives real texture sampling
through Spec 0016's already-`Accepted`, already-implemented
`SampledTexture`/`Sampler`/`Material` surface, replacing
`textured_quad_fixture.cpp`'s own hand-authored UV backdoor.

## Pre-draft verification against real, current source

Re-confirmed directly against `main` at Plan-drafting time (2026-08-26),
not carried over from Spec 0017's own citations without re-checking:

- `src/asset_system/include/atlantis/asset_system/mesh_source.h`:
  `MeshSourceVertex{positionX,Y,Z,colorR,G,B}` — six `float` fields.
- `src/asset_system/src/mesh_source.cpp`: `kVersionLine =
  "atlantis_static_mesh_source_version: 1"` (exact-string match, not a
  numeric check); `kVertexPrefix = "vertex: "`; `fields.size() != 6` is
  `SourceParseError::CountMismatch`; each of the six tokens is
  finite-checked (`NonFiniteFloat`) via `parseFloatToken()`/
  `std::isfinite()`.
- `src/asset_system/include/atlantis/asset_system/mesh_artifact.h`:
  `kMeshArtifactSchemaVersion = 1`; `kMeshArtifactVertexStrideBytes =
  24`; `kMeshArtifactHeaderSizeBytes = 40`.
- `src/asset_system/src/mesh_artifact.cpp`: `encodeMeshArtifact()`
  writes six `appendFloatLE()` calls per vertex, no padding;
  `decodeMeshArtifact()` rejects `schemaVersion != kMeshArtifactSchemaVersion`
  (`UnknownSchemaVersion`) and `vertexStrideBytes !=
  kMeshArtifactVertexStrideBytes` (`UnsupportedVertexStride`) against
  **one single global constant** — there is no per-asset variable
  layout anywhere in this decoder. Every offset/size computation
  (`expectedVertexBytesOffset`, `expectedIndexBytesOffset`,
  `expectedTotalSize`) is carried in `std::uint64_t` specifically so a
  crafted 32-bit-overflowing header cannot drive an undersized
  allocation — the existing checked-arithmetic pattern this Plan's own
  new arithmetic must match, not invent a new one for.
- `src/asset_system/include/atlantis/asset_system/static_mesh_asset_data.h`:
  `StaticMeshAssetData{vertexBytes_, indices_, vertexStrideBytes_}` — a
  tightly-packed byte buffer plus a runtime stride field, no fixed
  struct shape. `src/asset_system/src/load.cpp`'s `loadStaticMeshAsset()`
  passes `artifact.vertexStrideBytes` straight through with no
  transformation. Both require **zero source change** for this Plan.
- `src/renderer/src/mesh.cpp`: `createMesh()`'s own `VertexInputLayout
  layout` parameter is explicitly unread
  (`static_cast<void>(layout);`) — it only `memcpy`s `vertexData`/
  `vertexDataSizeBytes` into a GPU buffer. **Zero source change.**
- `src/vulkan_backend/src/vulkan_device.cpp`, `Device::createPipeline()`
  (lines ~939–960): exactly one `VkVertexInputBindingDescription`
  (`stride = params.vertexInputLayout.strideBytes`) and one
  `VkVertexInputAttributeDescription` per entry in
  `params.vertexInputLayout.attributes` — **only** those entries,
  each with its own independent `location`/`format`/`offset`. This is
  the real, code-level proof (not RHI-abstraction assertion) that an
  offset with no attribute description is never read by that pipeline.
- `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp`,
  `toVertexInputLayout()`: requires
  `vertexMetadata.vertexInputAttributes.size() == schema.size()` and a
  1:1 `location` match in both directions — never inspects total
  vertex stride versus attribute-referenced bytes.
- `shaders/minimal_renderer/minimal_mesh.slang`: `VertexInput {
  [[vk::location(0)]] float3 position; [[vk::location(1)]] float3
  color; }` — no UV input declared.
- `shaders/textured_quad/textured_quad.slang`: `VertexInput {
  [[vk::location(0)]] float3 position; [[vk::location(1)]] float2 uv;
  }` — no color input declared.
- `assets/meshes/minimal_cube.mesh.txt`: exactly 8 vertices (one per
  cube corner), 36 indices (12 triangles) — each corner vertex shared
  by the 3 faces meeting there. No UV value can give this topology a
  correct per-face unwrap.
- `assets/CMakeLists.txt`: `atlantis_add_static_mesh_asset(NAME
  minimal_cube SOURCE meshes/minimal_cube.mesh.txt)`, immediately
  followed by three `PARENT_SCOPE` propagations
  (`ARTIFACT_PATH`/`METADATA_PATH`/`TARGET`) plus a fourth,
  `LOGICAL_PATH` (Plan 0015 Section D7).
  `atlantis_add_scene_asset(NAME world_scene ... MESH_DEPENDENCIES
  minimal_cube)` follows, depending on it.
- `src/asset_system/CMakeLists.txt`, `atlantis_add_static_mesh_asset()`:
  output basename is derived from `${ARG_SOURCE}` (strip `.mesh.txt`),
  **not** from `${ARG_NAME}` — `NAME` only drives the CMake target name
  and the exported `ATLANTIS_${NAME}_*` variable prefix. Two mesh
  assets with different `SOURCE` basenames never collide regardless of
  `NAME`.
- Exactly four files hardcode a local `struct Vertex { float
  position[3]; float color[3]; };` and pass `sizeof(Vertex)` as
  `VertexInputLayout::strideBytes` for an Asset-System-sourced mesh,
  confirmed both by presence of the struct and by consumption of
  `StaticMeshAssetData`/`loadStaticMeshAsset()` (directly or, for
  Runtime, via `loadAndInstantiateScene()`):
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_loaded_fixture.cpp`,
  `src/runtime/src/runtime_application.cpp`. Each one's own
  `MeshVertexAttributeSchema` is exactly `{location=0:
  offsetof(Vertex,position)}, {location=1: offsetof(Vertex,color)}` —
  two entries, matching `minimal_mesh.slang`'s own two reflected
  inputs.
  `src/runtime/src/scene_load.cpp`/`.h` only receive an
  already-built `VertexInputLayout` parameter (no local `Vertex`
  struct); `tests/runtime/scene_load_tests.cpp` passes a
  default-constructed, empty `VertexInputLayout{}` with a null
  `Device*` at every call site (failure-path tests only); and
  `tests/shader_system/rhi_integration/vertex_input_mapping_tests.cpp`
  unit-tests `toVertexInputLayout()` with synthetic offsets/strides,
  independent of the real mesh artifact format. **None of these three
  needs any change.**
- `tests/image_regression/fixture/textured_quad_fixture.cpp`: `struct
  Vertex { float position[3]; float uv[2]; };` (20 bytes, no color);
  `kLeftQuadVertices[4]`/`kRightQuadVertices[4]` (positions
  `{-0.9,-0.5,0}..{-0.9,0.5,0}` / `{0.1,-0.5,0}..{0.1,0.5,0}`, UVs
  `{0,1},{1,1},{1,0},{0,0}` for both, per-vertex, matching "v=0 at the
  top row, v=1 at the bottom row"); `kQuadIndices[6] = {0,1,2,2,3,0}`;
  `createMesh(*fixture.device, *vertexInputLayout, kLeftQuadVertices,
  sizeof(kLeftQuadVertices), kQuadIndices, ...)` (and the `kRight...`
  equivalent) are the two call sites this Plan's own Milestone 3
  replaces.
- `tests/image_regression/CMakeLists.txt` and
  `tests/image_regression/golden_generator/CMakeLists.txt`: both
  already declare `ATLANTIS_textured_quad_{unorm,srgb}_{ARTIFACT,METADATA}_PATH`
  compile definitions and `add_dependencies(... ${ATLANTIS_textured_quad_unorm_TARGET}
  ${ATLANTIS_textured_quad_srgb_TARGET})` for the two **texture**
  assets — the exact pattern this Plan's own Milestone 3 extends with
  two new **mesh** asset path definitions/dependencies.
- `src/tools/asset_cooker/cook_command.cpp`, `runCookMeshMode()`: calls
  `cookStaticMesh()` generically — no hardcoded vertex-shape assumption
  at the CLI layer. `cookErrorMessage(CookError)` (line ~80) is the one
  `/w14062`-relevant switch in this file relevant to mesh cooking;
  `CookError` itself gains no new enumerator under this Plan.
- `src/asset_system/include/atlantis/asset_system/errors.h`:
  `SourceParseError`, `ArtifactDecodeError`, `CookError`,
  `AssetLoadError` — confirmed exhaustively, every rejection this
  Plan's own Implementation needs (malformed UV token, non-finite UV,
  wrong field count, wrong version, wrong stride, truncated/oversized
  artifact) already has a matching, existing enumerator once loop
  bounds move from six fields/floats to eight. **No new enumerator is
  added anywhere by this Plan.**

## Plan-level decisions (fixed here, not left to Implementation)

### D1. Authoring source grammar — version 2, 8-field `vertex:` line

Implements Spec 0017 FR1/FR2, ADR-0058 Decision items 1/4.

- `mesh_source.h`: `kVersionLine` becomes
  `"atlantis_static_mesh_source_version: 2"`. `MeshSourceVertex` gains
  two trailing `float` fields, in this exact order:
  ```cpp
  struct MeshSourceVertex {
    float positionX = 0.0f;
    float positionY = 0.0f;
    float positionZ = 0.0f;
    float colorR = 0.0f;
    float colorG = 0.0f;
    float colorB = 0.0f;
    float uvU = 0.0f;
    float uvV = 0.0f;
  };
  ```
- `mesh_source.cpp`: the per-vertex parse loop's `components[6]` array
  and `fields.size() != 6` check both become `[8]`/`!= 8`, adding
  `&vertex.uvU, &vertex.uvV` after the existing six pointers — same
  `parseFloatToken()`/`std::isfinite()` check applied uniformly across
  all eight, not a special-cased ninth branch for UV. `serializeMeshSource()`
  gains the matching two `std::to_string()` appends, same order.
- A source file whose version line is not exactly the new string is
  rejected by the pre-existing `SourceParseError::UnknownSourceVersion`
  check — unconditionally, no dual-version branch. A version-2-labeled
  file whose `vertex:` line still has 6 or 7 fields is rejected by the
  pre-existing `SourceParseError::CountMismatch` check. Neither case
  gains a new enumerator or an implicit `(0, 0)` UV fallback — both are
  hard parse failures, matching Spec 0017 Decision items 2/3 exactly.

### D2. Runtime artifact layout — 32 bytes, offsets 0/12/24, checked arithmetic

Implements Spec 0017 FR3, ADR-0058 Decision items 1/2/7.

- `mesh_artifact.h`: `kMeshArtifactSchemaVersion` becomes `2`;
  `kMeshArtifactVertexStrideBytes` becomes `32`. Per-vertex byte layout,
  unconditionally little-endian, identical encode/decode symmetry to
  today: position X/Y/Z at byte offsets 0/4/8, color R/G/B at offsets
  12/16/20, UV0 U/V at offsets 24/28 — 32 bytes total, no padding.
- `mesh_artifact.cpp`, `encodeMeshArtifact()`: two additional
  `appendFloatLE(out, v.uvU); appendFloatLE(out, v.uvV);` calls per
  vertex, after the existing six — same function, same discipline, no
  new helper.
- `mesh_artifact.cpp`, `decodeMeshArtifact()`: the per-vertex
  finiteness-check loop's inner bound moves from `floatIndex < 6` to
  `floatIndex < 8` — the two new UV floats are checked by the exact
  same `NonFiniteFloat`-returning loop, not a separate pass. The
  existing `UnknownSchemaVersion`/`UnsupportedVertexStride` checks
  reject anything but schema version 2 / stride 32 unconditionally — no
  dual-version decode path. Every existing `std::uint64_t`-carried
  offset/size computation (`expectedVertexBytesOffset`,
  `expectedIndexBytesOffset`, `expectedTotalSize`) is reused verbatim
  with the new constant substituted in — this Plan introduces no new
  arithmetic expression, only a changed constant, so the existing
  overflow-safety property (Pre-draft verification, above) carries over
  unchanged, not by inspection of new code but by construction.
- `cook.cpp`'s `cookStaticMesh()`: `metadata.vertexStrideBytes =
  kMeshArtifactVertexStrideBytes` already reads the constant
  generically — **zero change**, confirmed by direct inspection.

### D3. Cooker/loader error semantics — no new enumerator, confirmed exhaustively

Implements Spec 0017 FR5, ADR-0058 Decision item 7.

No enumerator is added to `SourceParseError`, `ArtifactDecodeError`,
`CookError`, or `AssetLoadError`. The following table is this Plan's own
exhaustive mapping from malformed input to existing rejection —
Implementation must not introduce any case not already listed here:

| Malformed input | Rejected by (existing) |
|---|---|
| Version line reads `... version: 1` (or anything but `2`) | `SourceParseError::UnknownSourceVersion` |
| `vertex:` line has fewer/more than 8 fields (including a source correctly labeled version 2 but missing UV columns) | `SourceParseError::CountMismatch` |
| A UV (or any) token is not a valid float | `SourceParseError::MalformedNumber` |
| A UV (or any) float is non-finite (`NaN`/`Inf`) | `SourceParseError::NonFiniteFloat` |
| Artifact header declares schema version other than 2 | `ArtifactDecodeError::UnknownSchemaVersion` |
| Artifact header declares stride other than 32 | `ArtifactDecodeError::UnsupportedVertexStride` |
| Artifact truncated / offsets inconsistent / size mismatch | `ArtifactDecodeError::TooSmallForHeader`/`InconsistentOffsets`/`SizeMismatch` (unchanged) |
| A UV (or any) float decoded from the artifact is non-finite | `ArtifactDecodeError::NonFiniteFloat` |
| Cooker cannot read the source file | `CookError::SourceFileUnreadable` (unchanged) |
| Cooker's own parse of the source fails (any of the above) | `CookError::SourceParseFailed` (unchanged) |
| Loader's artifact/metadata cross-check disagrees (now including stride 32 vs. metadata's own recorded value) | `AssetLoadError::MetadataArtifactMismatch` (unchanged) |

### D4. Existing no-UV mesh assets — explicit rejection, no migration reader

Implements Spec 0017 Decision item 2, ADR-0058 Decision item 3.

There is exactly one currently-authored mesh source in this repository,
`assets/meshes/minimal_cube.mesh.txt` (confirmed by directory listing).
No mesh artifact is independently shipped — every `.amesh` is a
deterministic CMake build output, already re-cooked automatically
whenever its source file or the `atlantis_asset_cooker` binary itself
changes (`DEPENDS ${source_path} atlantis_asset_cooker`, already
present). This Plan builds **no migration tool, no compatibility
reader, and no implicit `(0, 0)` UV default** — `minimal_cube.mesh.txt`
is edited directly (D7 below), and CMake's own existing dependency
tracking re-cooks it the next time anyone configures/builds.

### D5. Four composition-root call sites — mechanical stride widening, schema unchanged

Implements Spec 0017 FR8, ADR-0058 Decision item 6.

`minimal_cube_fixture.cpp`, `world_scene_fixture.cpp`,
`world_scene_loaded_fixture.cpp`, and `runtime_application.cpp` each
gain a trailing `float uv[2];` on their own local `Vertex` struct:

```cpp
struct Vertex {
  float position[3];
  float color[3];
  float uv[2];
};
```

`sizeof(Vertex)` becomes 32, matching D2's own new stride exactly.
**`MeshVertexAttributeSchema` in all four is left completely
unchanged** — still exactly `{location=0: offsetof(Vertex,position)},
{location=1: offsetof(Vertex,color)}`, two entries, matching
`minimal_mesh.slang`'s own unchanged, two-attribute reflected shape.
The new UV bytes occupy real space in every vertex these four files
upload to the GPU but are never named by any `VkVertexInputAttributeDescription`
for these pipelines — safe by the real Vulkan pipeline construction
confirmed in Pre-draft verification, not by assumption.

### D6. `minimal_cube.mesh.txt` — deterministic placeholder UV, explicitly not a texture asset

Implements Spec 0017 Decision items 2/11, ADR-0058 Decision item 3.

Every one of `minimal_cube.mesh.txt`'s 8 `vertex:` lines gains two
trailing UV tokens, both literally `0.0`, and the version line becomes
`atlantis_static_mesh_source_version: 2`. `0.0 0.0` is chosen
deliberately as the simplest possible **explicit, present, finite**
value — never an implicit default the cooker silently injects, since it
is written directly in the checked-in source text — and deliberately
**not** an attempted per-face unwrap, since the cube's own 8-vertex,
shared-corner topology cannot express one correctly (Pre-draft
verification, above). `minimal_cube` continues to be drawn exclusively
by `minimal_mesh.slang`, which declares no UV input and therefore never
reads these values. This Plan adds no comment or code implying
`minimal_cube` is texture-ready — precisely the opposite is stated
directly in the source file's own header comment as part of
Implementation.

### D7. New quad mesh assets — real UV topology, transcribed from the existing fixture literals

Implements Spec 0017 FR9, Decision items 9/11, ADR-0058 Decision item 5's own "Alternatives Considered" rejection of a shared-vertex approach for this proof.

Two new mesh authoring sources, mirroring `textured_quad_fixture.cpp`'s
own two already-hand-authored quads exactly — same positions, same UVs,
transcribed verbatim, never re-derived — plus an explicit, uniform
placeholder color (`1.0 1.0 1.0`, unread by `textured_quad.slang`, same
"explicit, not implicit" reasoning as D6):

- `assets/meshes/textured_quad_left.mesh.txt` — 4 vertices (not
  shared with any other mesh), transcribed from
  `kLeftQuadVertices[4]`'s own position/UV values; 2 triangles (6
  indices), transcribed from `kQuadIndices[6] = {0,1,2,2,3,0}`.
- `assets/meshes/textured_quad_right.mesh.txt` — same shape, transcribed
  from `kRightQuadVertices[4]`.

Each is declared via the existing, unmodified
`atlantis_add_static_mesh_asset()` mechanism —
```
atlantis_add_static_mesh_asset(NAME textured_quad_left  SOURCE meshes/textured_quad_left.mesh.txt)
atlantis_add_static_mesh_asset(NAME textured_quad_right SOURCE meshes/textured_quad_right.mesh.txt)
```
— in `assets/CMakeLists.txt`, alongside `minimal_cube`, each with its
own distinct `SOURCE` and therefore its own distinct logical
path/AssetId (no identity concern of the kind Plan 0016's own
Correction addressed — this mechanism was never shared here). Four
`PARENT_SCOPE` propagations each (`ARTIFACT_PATH`/`METADATA_PATH`/
`TARGET`/`LOGICAL_PATH`), matching `minimal_cube`'s own exact four-line
pattern.

Two separate mesh assets — not one shared mesh repositioned by a
non-identity transform — because `textured_quad_fixture.cpp`'s own
existing camera/projection/`objectToWorld` matrices are all identity
(Pre-draft verification) and this Plan does not introduce a transform
model the Spec never authorized; two independent, absolute-position
meshes is the minimal change that keeps every other part of the fixture
untouched, per Spec 0017's own Decision item 11.

### D8. `textured_quad_fixture.cpp` — load the two new mesh assets, replacing the hand-authored arrays

Implements Spec 0017 FR9, Decision items 9/10/11/12.

- `kLeftQuadVertices[4]`, `kRightQuadVertices[4]`, `kQuadIndices[6]`,
  and the local `struct Vertex { float position[3]; float uv[2]; };`
  are all removed.
- `setUpTexturedQuadFixture()` gains two new parameters (artifact/metadata
  path pairs for the two new mesh assets, mirroring the existing four
  texture artifact/metadata parameters exactly) and calls
  `atlantis::asset_system::loadStaticMeshAsset()` twice, once per quad —
  the same call `minimal_cube_fixture.cpp` already makes.
- The fixture's own `MeshVertexAttributeSchema` for this shader becomes
  `{location=0: offsetBytes=0}, {location=1: offsetBytes=24}` — position
  at byte 0, UV0 at byte 24 (D2's own fixed layout) — **skipping over**
  the color region at bytes 12–23, still exactly two entries, matching
  `textured_quad.slang`'s own unchanged, two-attribute reflected shape.
  `strideBytes` passed to `toVertexInputLayout()` becomes the real,
  loaded `StaticMeshAssetData::vertexStrideBytes()` value (32) — **not**
  a hardcoded `sizeof(Vertex)` of a now-deleted local struct, since no
  local `Vertex` struct remains in this file for this shader's own mesh
  data.
- `meshLeftResult`/`meshRightResult` become
  `createMesh(*fixture.device, *vertexInputLayout,
  leftMeshData.vertexBytes().data(), leftMeshData.vertexBytes().size(),
  leftMeshData.indices().data(),
  static_cast<std::uint32_t>(leftMeshData.indices().size()))` (and the
  `right...` equivalent) — the exact call shape
  `minimal_cube_fixture.cpp` already uses.
- `tests/image_regression/CMakeLists.txt` and
  `tests/image_regression/golden_generator/CMakeLists.txt` each gain
  two new `target_compile_definitions()` path pairs
  (`ATLANTIS_textured_quad_left_{ARTIFACT,METADATA}_PATH`,
  `ATLANTIS_textured_quad_right_{ARTIFACT,METADATA}_PATH`) and add
  `${ATLANTIS_textured_quad_left_TARGET}`/`${ATLANTIS_textured_quad_right_TARGET}`
  to their own existing `add_dependencies()` call, alongside the two
  texture targets already there.
- **Golden reuse, not replacement:** the existing
  `tests/image_regression/goldens/textured_quad/` PNG and sidecar are
  **not modified by this Plan**. Implementation's own acceptance gate
  for this milestone is that the fixture's own captured pixels continue
  to match that unmodified golden with **zero** difference. If a real
  run finds any difference, Implementation stops and returns to Human
  Review — it must not update the golden, adjust tolerance, or reinterpret
  the mismatch as expected, per Spec 0017 Decision item 11 and
  [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  own golden-update-reason rule (a real pixel change here would not fit
  any of that rule's existing categories without a fresh Human Review
  decision).

### D9. Verification-only claims — what this Plan proves and does not prove

Implements Spec 0017 Decision items 10/12 (explicit, not silently narrowed).

- CPU-side tests (D1/D2's own new/extended unit tests, Milestone 1)
  prove authoring → artifact → loaded `StaticMeshAssetData` preserves
  UV values exactly — same bit pattern in, same bit pattern out, no
  clamping, no flipping, checked by direct byte/float comparison, not
  by rendering.
- The real-GPU proof (Milestone 3, reusing the existing `textured_quad`
  golden) proves the loaded UV0 data genuinely reaches the sampler and
  participates in real texture sampling — it does **not**, by itself or
  in combination with the CPU tests above, independently prove the
  UV-origin/`V`-direction convention's own absolute correctness
  (Spec 0017 Decision item 11's own disclosed, accepted limitation). No
  asymmetric texture or new golden is added by this Plan to close that
  question further — Human Review already accepted this as a known
  limitation for this round.
- `World::Renderable`, any Scene Asset material/texture field, any
  Runtime scene-to-material binding, and every `Atlantis::Renderer`/
  `Atlantis::RHI`/`Atlantis::RenderGraph` public API are **untouched**
  by this Plan — confirmed by the Files/Modules Touched list below
  containing no file under `src/world/`, `src/renderer/include/`,
  `src/rhi/include/`, or `src/render_graph/include/`.

## Milestones / Task Breakdown

Three milestones, strictly sequential. Milestone 1 is drafted as **one
atomic step**, not several smaller ones, for a concrete, disclosed
reason: `kMeshArtifactVertexStrideBytes`/`kVersionLine` are each a
single global constant shared by every static mesh asset (Pre-draft
verification). Landing the format change without also updating
`minimal_cube.mesh.txt` in the same step would make the cooker reject
the repository's own only authored mesh outright, breaking the default
build; landing the format change and the source update without also
widening all four composition-root `Vertex` structs in the same step
would leave those four reading a 24-byte stride against real 32-byte
vertex data — a silent, runtime memory-layout mismatch, not a compile
error. This mirrors Plan 0012's own established "Step 4b, atomic"
precedent for the same class of reason (a step with no safe smaller
subdivision), not a new pattern invented here.

1. **Mesh format extension (atomic).** D1 (`mesh_source.h`/`.cpp`), D2
   (`mesh_artifact.h`/`.cpp`), D6 (`minimal_cube.mesh.txt`), D5 (all
   four composition-root `Vertex` structs), plus:
   - New/extended unit tests: `mesh_source_tests.cpp` (8-field
     parse/serialize round-trip; version-2 acceptance and version-1/
     malformed-count rejection; UV `MalformedNumber`/`NonFiniteFloat`
     cases), `mesh_artifact_tests.cpp` (32-byte encode/decode
     round-trip at the new offsets; schema-version-2/stride-32
     acceptance and version-1/stride-24 rejection; UV `NonFiniteFloat`
     at decode time), `static_mesh_asset_data_tests.cpp` (confirmed
     unmodified — existing tests must continue to pass unchanged
     against the wider stride, itself a regression assertion).
   - A new, dedicated CPU round-trip test (D9) proving a UV value
     written in an authoring source reaches `StaticMeshAssetData`
     unchanged, bit-for-bit, through the real `cookStaticMesh()` →
     `loadStaticMeshAsset()` path (not the encode/decode unit tests in
     isolation) — including at least one UV value outside `[0, 1]`,
     confirming no clamp is applied.
   - Extend the cooker's own existing determinism test
     (`tests/tools/asset_cooker/cooker_determinism_tests.cpp`) to cook
     a UV-bearing mesh source twice and compare artifact/metadata
     bytes — the same test, wider fixture, no new mechanism.
   - Extend `tests/tools/asset_cooker/cook_command_tests.cpp` with a
     real-CLI case cooking a version-1-labeled source and asserting
     rejection (non-zero exit, no artifact written) — mirrors this
     file's own existing "malformed source"/"escaping path" case shape.
   - **Acceptance gate for this Milestone:** `minimal_cube` and
     `world_scene`'s own existing image-regression goldens match with
     zero pixel difference (Debug and Release), and all four
     composition-root executables build and their own existing GPU
     tests pass unmodified in outcome.
2. **New, independently-verifiable quad mesh assets.** D7
   (`textured_quad_left.mesh.txt`/`textured_quad_right.mesh.txt`,
   CMake declarations). New test in `tests/asset_system/` (or extending
   `mesh_source_tests.cpp`/a new `textured_quad_mesh_tests.cpp`)
   confirming both cook and load successfully via the real, declared
   CMake targets, with the expected vertex count/positions/UVs present
   in the loaded `StaticMeshAssetData` — proves the new assets are
   real and correct *before* the fixture depends on them, keeping this
   step reviewable in isolation from Milestone 3's own GPU work.
3. **`textured_quad_fixture.cpp` conversion and golden verification
   (atomic with its own CMake wiring).** D8 — fixture source changes,
   `tests/image_regression/CMakeLists.txt` and
   `tests/image_regression/golden_generator/CMakeLists.txt` updates,
   all landing together (the fixture will not link/run correctly with
   only one side updated). **Acceptance gate:** the existing
   `textured_quad` golden (unmodified on disk — D8's own explicit
   requirement) matches the converted fixture's own captured pixels
   with zero difference, Debug and Release, real GPU hardware,
   Validation Layers clean. If this gate fails on a real run,
   Implementation stops here and returns to Human Review per D8's own
   stated escalation path — this Milestone is not "done" by force-fixing
   the mismatch.

## Files / Modules Touched (expected)

- `src/asset_system/include/atlantis/asset_system/mesh_source.h`,
  `src/asset_system/src/mesh_source.cpp` (D1)
- `src/asset_system/include/atlantis/asset_system/mesh_artifact.h`,
  `src/asset_system/src/mesh_artifact.cpp` (D2)
- `assets/meshes/minimal_cube.mesh.txt` (D6)
- `assets/meshes/textured_quad_left.mesh.txt`,
  `assets/meshes/textured_quad_right.mesh.txt` (new, D7)
- `assets/CMakeLists.txt` (D7 — two new `atlantis_add_static_mesh_asset()`
  declarations)
- `tests/image_regression/fixture/minimal_cube_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_fixture.cpp`,
  `tests/image_regression/fixture/world_scene_loaded_fixture.cpp`,
  `src/runtime/src/runtime_application.cpp` (D5 — `Vertex` struct only)
- `tests/image_regression/fixture/textured_quad_fixture.h`,
  `tests/image_regression/fixture/textured_quad_fixture.cpp` (D8)
- `tests/image_regression/CMakeLists.txt`,
  `tests/image_regression/golden_generator/CMakeLists.txt` (D8 — new
  compile definitions and `add_dependencies()` entries only)
- `tests/asset_system/mesh_source_tests.cpp`,
  `tests/asset_system/mesh_artifact_tests.cpp`,
  `tests/asset_system/static_mesh_asset_data_tests.cpp` (Milestone 1)
- A new `tests/asset_system/` test file for D9's own CPU round-trip
  proof (exact filename an Implementation-time detail, e.g.
  `mesh_uv_round_trip_tests.cpp`)
- `tests/tools/asset_cooker/cooker_determinism_tests.cpp`,
  `tests/tools/asset_cooker/cook_command_tests.cpp` (Milestone 1)
- A new `tests/asset_system/` test file for Milestone 2's own
  quad-asset cook/load proof (exact filename an Implementation-time
  detail, e.g. `textured_quad_mesh_tests.cpp`), and its own
  `tests/asset_system/CMakeLists.txt` registration plus the two new
  targets' `add_dependencies()` entry there.

**Not touched by this Plan** (confirmed by the list above containing no
entry under any of these paths): `src/world/`,
`src/renderer/include/atlantis/renderer/` (public headers),
`src/rhi/include/atlantis/rhi/` (public headers),
`src/render_graph/include/atlantis/render_graph/` (public headers),
`shaders/minimal_renderer/minimal_mesh.slang`,
`shaders/textured_quad/textured_quad.slang`,
`tests/image_regression/goldens/` (any file).

## Sequencing & Dependencies

Milestone 1 → Milestone 2 → Milestone 3, strictly. Milestone 2's own
authoring sources are written under the *new* (version 2) grammar from
the start — there is no reason to author them under the old grammar
first — so Milestone 1 must land first. Milestone 3 depends on
Milestone 2's own cooked, loadable mesh assets. No milestone here can
run in parallel with another; each depends on the previous one's own
real, checked-in output.

Depends on Spec 0012 (Asset System Foundation, `Approved`, implemented
— the cooker/loader/CMake mechanisms this Plan extends) and Spec 0016
(Texture & Sampler Foundation, `Approved`, implemented — the
`SampledTexture`/`Sampler`/`Material`/RenderGraph surface this Plan's
own Milestone 3 reuses unmodified), both already satisfied on `main`.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | `parseMeshSource()`/`serializeMeshSource()` round-trip an 8-field vertex line correctly, including a UV pair outside `[0,1]` | `mesh_source_tests.cpp` | GPU-independent |
| V2 | A source with the old (`... version: 1`) or any non-`2` version line is rejected with `SourceParseError::UnknownSourceVersion` | `mesh_source_tests.cpp` | GPU-independent |
| V3 | A version-2 source whose `vertex:` line has 6 or 7 fields (UV omitted or partially given) is rejected with `SourceParseError::CountMismatch` | `mesh_source_tests.cpp` | GPU-independent |
| V4 | A malformed UV token is rejected with `SourceParseError::MalformedNumber`; a non-finite UV value is rejected with `SourceParseError::NonFiniteFloat` | `mesh_source_tests.cpp` | GPU-independent |
| V5 | `encodeMeshArtifact()`/`decodeMeshArtifact()` round-trip the new 32-byte, offset-0/12/24 layout correctly | `mesh_artifact_tests.cpp` | GPU-independent |
| V6 | An artifact header declaring schema version 1 (or any non-2 value) is rejected with `ArtifactDecodeError::UnknownSchemaVersion`; a header declaring stride other than 32 is rejected with `UnsupportedVertexStride` | `mesh_artifact_tests.cpp` | GPU-independent |
| V7 | A non-finite UV float in an otherwise well-formed artifact is rejected with `ArtifactDecodeError::NonFiniteFloat` | `mesh_artifact_tests.cpp` | GPU-independent |
| V8 | `StaticMeshAssetData`'s own existing tests pass unmodified against the new 32-byte stride, confirming zero source change was needed | `static_mesh_asset_data_tests.cpp` | GPU-independent |
| V9 | A UV value (including one outside `[0,1]`) written in a real authoring source reaches `loadStaticMeshAsset()`'s own returned `StaticMeshAssetData` bit-for-bit — no clamp, no flip — through the real `cookStaticMesh()` → `loadStaticMeshAsset()` path | New `tests/asset_system/` test (D9) | GPU-independent |
| V10 | Cooking a UV-bearing mesh source twice via the real `atlantis_asset_cooker` executable produces byte-identical artifact and metadata output | `cooker_determinism_tests.cpp` (extended) | `tool` |
| V11 | A real, version-1-labeled source file is rejected by the real CLI (non-zero exit, no artifact/stamp written) | `cook_command_tests.cpp` (extended) | GPU-independent |
| V12 | A real, truncated or size-inconsistent artifact is rejected by `loadStaticMeshAsset()` with `AssetLoadError::ArtifactDecodeFailed`/`MetadataArtifactMismatch` as appropriate (regression of existing coverage against the new stride) | `mesh_artifact_tests.cpp`/`load` tests | GPU-independent |
| V13 | `minimal_cube.mesh.txt` cooks successfully under the new grammar via the real CMake build; its own cooked artifact records stride 32 and 8 vertices with UV `(0.0, 0.0)` throughout | Manual/CMake-level, recorded | Manual |
| V14 | Touching `minimal_cube.mesh.txt` (or either new quad source) triggers an automatic re-cook on the next build, with no manual `--force`/reconfigure step — regression of Plan 0012 Section D7's own established re-import-triggering procedure | Manual, recorded | Manual |
| V15 | `minimal_cube` and `world_scene`'s own existing image-regression goldens match the post-migration build with **zero** pixel difference, Debug | `image_regression_gpu_tests.cpp`/`world_scene_gpu_tests.cpp`/`world_scene_loaded_gpu_tests.cpp` | GPU-required |
| V16 | Same as V15, Release | same | GPU-required |
| V17 | Every real binary linking `Atlantis::ImageRegressionFixture` (`atlantis_image_regression_gpu_tests` and all three `atlantis_image_regression_*golden_generator` executables, which together compile all three widened fixture files) or containing `runtime_application.cpp`'s own widened `Vertex` struct (`atlantis_runtime`/`atlantis_runtime_gpu_tests`/`atlantis_runtime_tests`) builds and runs against the new 32-byte stride with no crash or misaligned-geometry symptom | Full build + existing GPU test suites | GPU-required |
| V18 | Both new mesh assets (`textured_quad_left`, `textured_quad_right`) cook successfully via the real CMake build and their own cooked artifacts round-trip through `loadStaticMeshAsset()` with the expected vertex count, positions, and UVs | New `tests/asset_system/` test (Milestone 2) | GPU-independent |
| V19 | `textured_quad_fixture.cpp`'s own converted `MeshVertexAttributeSchema` (`{position@0, offset 0}, {uv@1, offset 24}`) satisfies `toVertexInputLayout()`'s own existing cross-validation against `textured_quad.slang`'s real reflected shape, with no `AttributeCountMismatch`/`LocationNotFoundInSchema` | `textured_quad_gpu_tests.cpp` (existing setup path, exercised) | GPU-required |
| V20 | The existing, unmodified `textured_quad` golden matches the converted fixture's own captured pixels with **zero** difference, Debug | `textured_quad_gpu_tests.cpp` | GPU-required |
| V21 | Same as V20, Release | same | GPU-required |
| V22 | `minimal_mesh.slang`'s own reflected vertex-input shape is unchanged (still exactly `position@0, color@1`) after the stride widening — confirmed by re-running the shader's own existing reflection-consuming tests unmodified | `reflection_metadata_tests.cpp`/existing shader tests | GPU-independent |
| V23 | `cookErrorMessage()`'s own switch over `CookError` still compiles clean (`/w14062` positive proof) — regression, since `CookError` itself gains no new case under this Plan | Build-level, both configurations | GPU-independent |
| V24 | Temporarily removing one existing `CookError` case from `cookErrorMessage()`'s switch fails the build naming that exact enumerator (`/w14062` negative proof); restoring it builds clean again | Manual, build-log evidence | Manual |
| V25 | Module-boundary scan: `atlantis_asset_system`'s own `target_link_libraries` closure is unchanged — still `Atlantis::Core` only, no RHI/Renderer/RenderGraph/`atlantis::world`/`Stb::Stb` reach | `module_boundary_tests.cpp` (existing) | GPU-independent |
| V26 | No third-party dependency, forbidden `#include`, or new top-level module is introduced — repository-wide grep for a new `FetchContent_Declare`/new `add_subdirectory` under `src/` beyond this Plan's own listed files | Manual, recorded | Manual |
| V27 | `ATLANTIS_BUILD_TESTS=OFF`: a from-scratch configure succeeds and both new mesh assets (declared unconditionally in `assets/CMakeLists.txt`, matching `minimal_cube`'s own precedent) cook successfully | Manual, recorded | Manual |
| V28 | Fresh Debug build clean, full repository | Build-level | GPU-independent |
| V29 | Fresh Release build clean, full repository | Build-level | GPU-independent |
| V30 | `ctest -LE gpu` 100% pass, Debug | Full suite | GPU-independent |
| V31 | `ctest -LE gpu` 100% pass, Release | Full suite | GPU-independent |
| V32 | `ctest -L gpu` 100% pass, Debug, real GPU hardware | Full suite | GPU-required |
| V33 | `ctest -L gpu` 100% pass, Release, real GPU hardware | Full suite | GPU-required |
| V34 | Vulkan Validation Layers grepped clean (zero `VUID`/`Validation Error`/`Validation Warning`) across full verbose GPU test output, both configurations | Manual, recorded | Manual |
| V35 | Every existing golden file (`minimal_cube`, `world_scene`, `textured_quad` — both PNG and sidecar) is byte-for-byte unchanged on disk versus `main` before this Plan's own Implementation began — an explicit `git diff`/checksum check, not inferred from "tests passed" | Manual, `git diff` evidence | Manual |
| V36 | `git diff --check` clean across every commit in this Plan's own Implementation; no AI/Claude/Anthropic/Co-Authored-By attribution in any commit or PR content | Manual, recorded | Manual |

## Rollback Plan

Every step is a real, reviewable Git commit. If a defect is found after
merge, revert the specific commit(s) in reverse dependency order
(Milestone 3, then 2, then 1) — Milestone 1's own atomic step reverts as
a single unit for the same reason it lands as one (D2/D5/D6 are
mutually load-bearing). Reverting Milestone 1 restores the 24-byte
format and the four composition roots' own prior `Vertex` shape
together; `minimal_cube.mesh.txt` reverts to its own pre-Plan 6-field
content in the same revert. No data migration is needed in either
direction, since no artifact is ever shipped independently of its own
regenerated-at-build-time source.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No delta beyond: this Plan's own explicit "stop and return to Human
Review" escalation path (D8/D9) applies in place of the general
Definition of Done's own assumption that a failing verification is
always fixed forward within Implementation — a real pixel difference
against the existing, unmodified `textured_quad` golden is treated as a
Human Review question, not an Implementation bug to silently absorb.
