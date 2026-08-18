# Plan: Asset System Foundation

- **Spec:** [specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-19)
- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Related ADR(s):**
  [ADR-0043](../adr/0043-asset-system-module-boundary.md) (module
  boundary), [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md)
  (identity, provenance, import methodology), and
  [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format, versioning, dependency policy) — all three `Accepted`
  2026-08-19.

## Objective

Implement Spec 0012 in full: a new tenth top-level module, Atlantis
Asset System, depending on Atlantis Core alone, that turns a checked-in
authoring mesh source into a deterministic, versioned, little-endian
runtime artifact plus a metadata sidecar, and loads that artifact back
into CPU-side `StaticMeshAssetData`. A test composition root — never
Asset System itself — hands that CPU data to the existing, unmodified
`atlantis::renderer::createMesh()` and renders it through the existing
stack, proving the loop against Spec 0011's already-committed golden
with zero channel difference.

## Plan-level decisions (fixed here, not left to Implementation)

These are the details Spec 0012 and the three ADRs explicitly delegate
to Plan stage. Each is decided here so Implementation has nothing
architectural left to choose.

### D1. Asset source root, extensions, output location, check-in policy

| Question | Decision |
|---|---|
| Asset source root | `assets/` at the repository root. The one asset this Plan ships lives at `assets/meshes/minimal_cube.mesh.txt`. |
| Logical path | Path relative to `assets/`, i.e. `meshes/minimal_cube.mesh.txt` — this exact string is what ADR-0044's normalization and hash consume. |
| Authoring source extension | `.mesh.txt` — human-readable and diffable, and `.txt` keeps editors/`git` treating it as text on a Windows checkout. |
| Runtime artifact extension | `.amesh` (binary). |
| Metadata sidecar extension | `.amesh.meta.txt`, alongside the artifact. |
| Generated output location | Build tree only: `${CMAKE_BINARY_DIR}/assets/<logical-path-with-extension-replaced>`. |
| **Are runtime artifacts checked in?** | **No.** Cooked artifacts and sidecars are build outputs, never committed — matching Shader System's `.spv`/reflection-JSON precedent ([ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md), Spec 0008), which this repository has already lived with successfully. **Consequence:** ADR-0044's "Checked-in imported artifacts — human-reviewed, never silently regenerated" clause is satisfied vacuously and no golden-image-style artifact review workflow is introduced. `.gitignore` is not touched, because the artifacts live under `build/`, which is already ignored. |

### D2. CMake targets, namespaces, directories, dependency direction

| Target | Kind | Location | Links | Notes |
|---|---|---|---|---|
| `atlantis_asset_system` (alias `Atlantis::AssetSystem`) | STATIC | `src/asset_system/` | **PUBLIC `Atlantis::Core`**; PRIVATE `atlantis_compiler_warnings` | The only module target this Plan adds. Links nothing else — no RHI, Renderer, Shader System, Vulkan Backend, RenderGraph, Platform, Runtime, or Tools. |
| `atlantis_asset_cooker_lib` | STATIC | `src/tools/asset_cooker/` | PUBLIC `Atlantis::AssetSystem`, `Atlantis::Core`; PRIVATE `atlantis_compiler_warnings` | Exists for the same mechanical reason `atlantis_shader_compiler_lib` does: CMake cannot link a test executable against another executable's objects, and the CLI's argument handling must be unit-testable. |
| `atlantis_asset_cooker` | executable | `src/tools/asset_cooker/` | PRIVATE `atlantis_asset_cooker_lib`, `atlantis_compiler_warnings` | The Tools-hosted CLI. **Tools → Asset System, never the reverse.** |
| `atlantis_asset_system_tests` | executable | `tests/asset_system/` | `Atlantis::AssetSystem`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | GPU-independent. Links no RHI/Renderer target — that is itself part of the verification (see V9). |
| `atlantis_asset_cooker_tests` | executable | `tests/tools/asset_cooker/` | `atlantis_asset_cooker_lib`, `Catch2::Catch2WithMain`, `atlantis_compiler_warnings` | GPU-independent; the determinism test is `tool`-labeled because it launches the real cooker executable. |

- **Namespace:** `atlantis::asset_system`, matching the module list in
  [AGENTS.md](../AGENTS.md)'s C++ coding conventions.
- **Public header root:** `src/asset_system/include/atlantis/asset_system/`,
  matching every existing module's own layout.
- **Root `CMakeLists.txt` ordering:** `add_subdirectory(src/asset_system)`
  is inserted after `src/core` and before `src/tools/asset_cooker`;
  `src/tools/asset_cooker` is added after `src/tools/shader_compiler`.
  `tests/asset_system` and `tests/tools/asset_cooker` join the existing
  `ATLANTIS_BUILD_TESTS` block.
- **The asset-declaration CMake function** (`atlantis_add_static_mesh_asset()`)
  is defined in `src/asset_system/CMakeLists.txt`, mirroring exactly how
  `atlantis_add_slang_shader_pair()` is defined in
  `src/shader_system/CMakeLists.txt` — so any directory processed after
  it can call it. The existing ordering comment convention in the root
  `CMakeLists.txt` is extended with the same kind of note.

### D3. Format layouts

**Authoring source** — `assets/meshes/minimal_cube.mesh.txt`, ASCII,
LF-terminated (a single optional trailing `\r` per line is tolerated so
a Windows `core.autocrlf` checkout parses identically; every other
deviation is rejected):

```
atlantis_static_mesh_source_version: 1
vertex_count: 8
index_count: 36
vertex: -0.5 -0.5 -0.5 0.0 0.0 0.0
...                                   (exactly vertex_count vertex lines)
index: 0 1 2
...                                   (exactly index_count/3 index lines)
```

Strict parsing rules: line 1 must match the version anchor exactly and
carry a recognized version (`1`); `vertex_count`/`index_count` follow in
that fixed order; then exactly `vertex_count` `vertex:` lines, each with
exactly 6 finite floats (position xyz, colour rgb); then exactly
`index_count / 3` `index:` lines, each with exactly 3 integers, each
`< vertex_count`. Anchored-prefix matching, never a delimiter scan.
`index_count` must be a positive multiple of 3; `vertex_count` must be
positive and `<= 65535` (the `std::uint16_t` index domain). No trailing
content after the final index line other than one optional newline.

**Metadata sidecar** — exactly 8 lines, fixed order, anchored-prefix
parsed, mirroring ADR-0042's own sidecar discipline (same *pattern*, no
shared code):

```
atlantis_asset_metadata_version: 1
asset_id: 0123456789abcdef
source_logical_path: meshes/minimal_cube.mesh.txt
importer_version: atlantis-asset-cooker/1
asset_type: static_mesh
vertex_count: 8
index_count: 36
vertex_stride_bytes: 24
```

`asset_id` is the fixed-width, lowercase, 16-hex-digit form ADR-0044
fixes. An unrecognized `atlantis_asset_metadata_version` is rejected
outright, never guessed at.

**Runtime artifact** — `.amesh`, **unconditionally little-endian**
(ADR-0045), 40-byte header then payload:

| Offset | Size | Field | Value / constraint |
|---|---|---|---|
| 0 | 8 | `magic` | bytes `A T L M E S H \0` |
| 8 | 4 | `schema_version` (u32) | `1` |
| 12 | 4 | `vertex_stride_bytes` (u32) | `24` for this asset type |
| 16 | 8 | `asset_id` (u64) | ADR-0044's value |
| 24 | 4 | `vertex_count` (u32) | `> 0`, `<= 65535` |
| 28 | 4 | `index_count` (u32) | `> 0`, multiple of 3 |
| 32 | 4 | `vertex_bytes_offset` (u32) | `40` |
| 36 | 4 | `index_bytes_offset` (u32) | `40 + vertex_count * vertex_stride_bytes` |
| 40 | … | vertex bytes | `vertex_count * vertex_stride_bytes` |
| … | … | index bytes | `index_count * 2` (`std::uint16_t` each) |

`asset_id` sits at offset 16 so the `u64` is naturally 8-byte aligned
within the header. Total file size must equal
`index_bytes_offset + index_count * 2` exactly — no trailing bytes.
Every write and every read performs explicit byte-by-byte
little-endian assembly (shift/mask), never a `memcpy` of a host
integer — so the format contract holds independently of host endianness,
as ADR-0045 requires.

### D4. Cooker entry-point shape and how every declared asset reaches the collision check

- **The cooker CLI cooks exactly one asset per invocation**
  (`--source=<path> --asset-root=<dir> --output-dir=<dir>`), mirroring
  `atlantis_shader_compiler`'s own one-shader-per-invocation model. This
  keeps CMake's per-asset `DEPENDS` edge precise: a change to one asset
  re-cooks only that asset.
- **A second, separate mode performs set-level validation**
  (`--validate-set --asset-list=<file>`), consuming a newline-separated
  list of every declared logical path and running ADR-0044's
  invocation-scoped checks — Asset ID collision, case-only-differing
  logical paths, duplicate logical paths — across the whole set at once.
- **How the set is guaranteed complete:**
  `atlantis_add_static_mesh_asset()` appends each declared asset's
  logical path to a global CMake property
  (`ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS`). A single
  `atlantis_finalize_asset_validation()` call, invoked once from the root
  `CMakeLists.txt` **after every** `add_subdirectory()` that could declare
  an asset, writes that accumulated list to
  `${CMAKE_BINARY_DIR}/assets/declared_assets.txt` via `file(GENERATE)`
  and declares one `atlantis_asset_validation` custom target (`ALL`) that
  runs `--validate-set` over it. Because the list is written at generate
  time from a global property, an asset declared anywhere in the build
  necessarily appears in it. A validation failure fails the build.
- **Disclosed scope, matching ADR-0044 exactly:** this establishes
  uniqueness across everything one configured build tree declares. It is
  *not* repository-global detection across independent build
  invocations, and nothing in this Plan claims otherwise.

### D5. Importer build identity / provenance

`importer_version` is a **compile-time constant string in the Asset
System library**, `atlantis-asset-cooker/1`, bumped by hand whenever
import logic changes in a way that could alter output bytes.

Rationale, stated because the obvious alternative is wrong here: a git
source-revision anchor (Shader System's own style) would change the
metadata sidecar's bytes on *every* commit, including commits that touch
nothing the importer reads. That would defeat this Plan's own
byte-identical-determinism verification (V5) and would make re-import
triggering fire on unrelated history. A manually-bumped version string
keeps determinism meaningful and keeps the provenance anchor honest
about what it actually identifies: importer behaviour, not checkout
state.

### D6. Error taxonomy and mapping

All recoverable errors are `atlantis::Result<T, E>`; none throws.
Programmer errors (a null pointer into a parsing entry point) remain
`ATLANTIS_CHECK`, per [AGENTS.md](../AGENTS.md).

| Enum | Enumerators | Raised by |
|---|---|---|
| `LogicalPathError` | `EmptyPath`, `AbsolutePathRejected`, `DriveLetterRejected`, `EscapesAssetRoot`, `DisallowedCharacter` | Path normalization (Step 2) |
| `AssetSetError` | `AssetIdCollision`, `CaseOnlyPathConflict`, `DuplicateLogicalPath`, `InvalidLogicalPath` | Set validation (Step 2) |
| `SourceParseError` | `UnknownSourceVersion`, `MissingField`, `FieldOrderMismatch`, `MalformedNumber`, `NonFiniteFloat`, `CountMismatch`, `IndexOutOfRange`, `IndexCountNotMultipleOfThree`, `VertexCountOutOfRange`, `TrailingContent` | Authoring parser (Step 3) |
| `MetadataParseError` | `UnknownMetadataVersion`, `WrongLineCount`, `FieldNameMismatch`, `MalformedValue` | Sidecar parser (Step 3) |
| `ArtifactDecodeError` | `TooSmallForHeader`, `BadMagic`, `UnknownSchemaVersion`, `UnsupportedVertexStride`, `InconsistentOffsets`, `SizeMismatch`, `VertexCountOutOfRange`, `IndexCountNotMultipleOfThree`, `IndexOutOfRange`, `NonFiniteFloat` | Artifact decoder (Step 3) |
| `AssetLoadError` | `ArtifactFileUnreadable`, `MetadataFileUnreadable`, `ArtifactDecodeFailed`, `MetadataParseFailed`, `MetadataArtifactMismatch` | File-level loader (Step 5) |
| `CookError` | `SourceFileUnreadable`, `SourceParseFailed`, `LogicalPathInvalid`, `ArtifactWriteFailed`, `MetadataWriteFailed` | Cooker (Step 4) |

Mapping notes: **I/O failure is always distinct from content failure**
(`*FileUnreadable`/`*WriteFailed` never collapse into a parse error).
**Version incompatibility is always its own enumerator**
(`UnknownSourceVersion`, `UnknownMetadataVersion`,
`UnknownSchemaVersion`) and is never guessed past. **Overflow** is
handled before allocation: `vertex_count * vertex_stride_bytes` and
`index_count * 2` are computed in `std::uint64_t` and range-checked
against the actual file size before any buffer is sized, so a malicious
or truncated header cannot drive a huge allocation.
**Missing dependency** for this Plan's one asset type means the metadata
sidecar next to a runtime artifact is absent or unreadable —
`MetadataFileUnreadable` — and the loader rejects the pair rather than
loading an artifact whose provenance it cannot confirm.

### D7. Determinism and re-import triggering verification

- **Determinism — automated** (`tool`-labeled ctest, Step 4): run the
  real `atlantis_asset_cooker` executable twice against the same source
  into two distinct temp directories, then byte-compare both the
  `.amesh` and the `.amesh.meta.txt`. Byte-identical or the test fails.
- **Fixed-bytes contract — automated** (Step 3): encode a small,
  hand-known mesh and compare against a checked-in expected byte vector
  written out in the test itself. This is what actually pins the
  little-endian contract; a determinism test alone would pass even if the
  writer emitted host-endian bytes.
- **Re-import triggering — scripted manual procedure, honestly labeled**
  (Step 4). CMake's own `DEPENDS` staleness behaviour is a property of
  the build system, not of code this Plan writes, and this repository has
  no build-system-level test harness. The Plan therefore specifies an
  exact, repeatable command sequence that a human or agent runs and
  records, matching how Plan 0008 recorded its own observed multi-config
  rebuild behaviour:
  1. Configure and build. Record the artifact's mtime and bytes.
  2. Rebuild with nothing changed → the cooker does not re-run
     (artifact mtime unchanged).
  3. Touch `assets/meshes/minimal_cube.mesh.txt`, rebuild → the cooker
     re-runs (mtime changes; bytes identical, since content did not).
  4. Edit one vertex value, rebuild → the cooker re-runs and bytes
     change.
  5. Touch an unrelated file (e.g. `README.md`), rebuild → the cooker
     does **not** re-run.
  6. Rebuild after touching the cooker's own source → the cooker re-runs
     (the executable is a `DEPENDS` input, matching ADR-0029's model).

  **This is re-import triggering, not a cache.** Nothing is retained or
  reused across clean builds, separate build trees, or machines, and no
  derived-data cache is built (ADR-0044).

### D8. GPU test reuse of the existing golden

The new GPU test renders the **asset-sourced** cube and compares it
against the **already-committed**
`tests/image_regression/goldens/minimal_cube/` golden using the existing
`atlantis::image_regression::compareBuffers()`, requiring **zero**
channel difference.

- **The golden does not change, and no code in this Plan writes to
  `tests/image_regression/goldens/`.** The golden generator is not
  invoked, extended, or re-run.
- If the comparison fails, the correct response is to fix the importer
  or the authoring source so the imported bytes match the hand-authored
  bytes — **never** to regenerate the golden. Any golden change would be
  a separate, human-reviewed decision under ADR-0042's own
  golden-update-reason rule, and this Plan asserts in advance that it
  needs none.
- A cheaper GPU-independent guard (V4) catches drift long before the GPU
  test does: the cooked artifact's vertex and index bytes are asserted
  byte-identical to the hand-authored `kCubeVertices`/`kCubeIndices`
  values. If that passes, the render is pixel-identical by construction.

## Milestones / Task Breakdown

Each step leaves the repository building and its own tests passing.
Steps marked **atomic** must land as a single commit because a partial
landing does not compile or does not configure.

### Step 1 — Module skeleton, CPU data type, contracts (**atomic**)

Atomic because CMake rejects a STATIC library with no sources, and the
root `CMakeLists.txt` edit references a directory that must already
exist.

- `src/asset_system/CMakeLists.txt` — `atlantis_asset_system` +
  `Atlantis::AssetSystem` alias, PUBLIC `Atlantis::Core` only.
- `src/asset_system/include/atlantis/asset_system/static_mesh_asset_data.h`
  — `StaticMeshAssetData` (owning `std::vector<std::byte> vertexBytes`,
  `std::vector<std::uint16_t> indices`, `std::uint32_t
  vertexStrideBytes`, plus accessors for vertex/index counts). Move-only
  by virtue of its members; documents at the type, per AGENTS.md's
  required contracts: **not thread-safe, caller-thread-only**;
  **single owner, RAII, no manual cleanup**; **recoverable errors are
  `Result`, never exceptions**.
- `src/asset_system/include/atlantis/asset_system/errors.h` — the full
  D6 enum set.
- `src/asset_system/src/static_mesh_asset_data.cpp` — the one
  translation unit needed to make the library non-empty (accessor
  definitions).
- `tests/asset_system/CMakeLists.txt`,
  `tests/asset_system/static_mesh_asset_data_tests.cpp` — construction,
  move, accessor arithmetic.
- Root `CMakeLists.txt` — `add_subdirectory(src/asset_system)` and
  `add_subdirectory(tests/asset_system)`.

### Step 2 — Logical path normalization, Asset ID, set validation

- `.../asset_system/logical_path.h` / `src/logical_path.cpp` —
  normalization implementing ADR-0044 exactly: `\` → `/`; reject
  absolute paths and drive prefixes; `lexically_normal()`-equivalent
  `.`/`..`/empty-segment collapsing; reject any residual leading `..`;
  case preserved; ASCII-only character-set validation
  (`a-z A-Z 0-9 _ - . /`).
- `.../asset_system/asset_id.h` / `src/asset_id.cpp` — 64-bit FNV-1a
  over the normalized path bytes (offset basis `0xcbf29ce484222325`,
  prime `0x100000001b3`); `toHexString()` producing the 16-hex-digit
  lowercase form; explicit little-endian 8-byte serialization helpers.
- `.../asset_system/asset_set_validation.h` / `src/asset_set_validation.cpp`
  — given a list of logical paths, return `Ok` or the first
  `AssetSetError` with the offending entries.
- `tests/asset_system/logical_path_tests.cpp`,
  `asset_id_tests.cpp`, `asset_set_validation_tests.cpp`.

### Step 3 — Strict parsing and writing of the three formats

- `.../asset_system/mesh_source.h` / `src/mesh_source.cpp` — authoring
  parser per D3.
- `.../asset_system/asset_metadata.h` / `src/asset_metadata.cpp` —
  sidecar parse + serialize per D3.
- `.../asset_system/mesh_artifact.h` / `src/mesh_artifact.cpp` —
  in-memory encode/decode of the little-endian binary layout per D3,
  with explicit shift/mask byte handling and pre-allocation overflow
  checks.
- `tests/asset_system/mesh_source_tests.cpp`,
  `asset_metadata_tests.cpp`, `mesh_artifact_tests.cpp`.

### Step 4 — Deterministic cooker, Tools CLI, declared-set validation, CMake wiring

Two sub-steps; **4b is atomic** (the CMake function, the finalize call,
the asset declaration, and the validation target must configure
together).

**4a — cooker library + CLI + tests**

- `.../asset_system/cook.h` / `src/cook.cpp` — `cookStaticMesh()`:
  read source, parse, compute Asset ID from the logical path, encode
  artifact, serialize metadata, write both. Pure `Result`, no exception.
- `src/tools/asset_cooker/{cook_command.h,cook_command.cpp,main.cpp,CMakeLists.txt}`
  — argument parsing, both modes (single-asset cook, `--validate-set`),
  exit-code propagation.
- `tests/tools/asset_cooker/{CMakeLists.txt,cook_command_tests.cpp,cooker_determinism_tests.cpp}`
  — argument handling; the `tool`-labeled double-run byte-comparison
  (D7).

**4b — CMake integration and the one real asset**

- `assets/meshes/minimal_cube.mesh.txt` — the 8 vertices / 36 indices
  transcribed exactly from
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`.
- `src/asset_system/CMakeLists.txt` — `atlantis_add_static_mesh_asset()`
  and `atlantis_finalize_asset_validation()`.
- `assets/CMakeLists.txt` — declares the one asset.
- Root `CMakeLists.txt` — `add_subdirectory(assets)` after
  `src/asset_system`, and the single `atlantis_finalize_asset_validation()`
  call placed after every asset-declaring directory.

### Step 5 — File-level runtime artifact loader

- `.../asset_system/load.h` / `src/load.cpp` — `loadStaticMeshAsset()`:
  read artifact and sidecar, decode/parse both, cross-check that
  `asset_id`, `vertex_count`, `index_count`, and `vertex_stride_bytes`
  agree between them (`MetadataArtifactMismatch` if not), return
  `StaticMeshAssetData`. **Returns CPU data only — no RHI type is named,
  included, or constructed.**
- `tests/asset_system/load_tests.cpp` — success, each failure mode,
  and the deliberate artifact/metadata mismatch case.

### Step 6 — Composition root and the GPU closed loop

- `tests/image_regression/fixture/minimal_cube_fixture.h/.cpp` — add
  `setUpMinimalCubeFixtureFromAsset(const char* artifactPath, const char*
  metadataPath)`, differing from the existing
  `setUpMinimalCubeFixture()` *only* in where the vertex/index bytes come
  from. It calls `loadStaticMeshAsset()`, then — as the composition
  root — resolves the `VertexInputLayout` via the existing
  `shader_system::rhi_integration::toVertexInputLayout()` and calls the
  existing, unmodified `renderer::createMesh()`. The existing function is
  left byte-for-byte unchanged so the hand-authored path stays available
  as the comparison baseline.
- `tests/image_regression/fixture/CMakeLists.txt` — the fixture target
  gains `Atlantis::AssetSystem`. This is the composition root taking on
  the dependency, exactly as ADR-0043 prescribes.
- `tests/image_regression/image_regression_gpu_tests.cpp` — one new
  `gpu`-labeled case: render via the asset path, compare against the
  existing golden, require zero difference.
- `tests/image_regression/CMakeLists.txt` — `ATLANTIS_ASSET_ARTIFACT_DIR`
  compile definition (absolute build-tree path, matching the existing
  `ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR` convention, so it resolves
  regardless of working directory), plus `add_dependencies()` on the
  asset target so the artifact exists before the test runs.

### Step 7 — Full verification and documentation closeout

- Debug and Release configure + build from a clean tree.
- `ctest -LE gpu` and `ctest -L gpu`, both configurations.
- The D7 re-import triggering procedure, run and recorded.
- Validation Layers grepped clean on the GPU path.
- `AGENTS.md` — add Atlantis Asset System to the top-level module list
  (nine → ten) and its one-line boundary summary.
- `docs/architecture/module_boundaries.md` — add an Atlantis Asset
  System section; narrow Tools' existing generic "asset processing"
  responsibility to the cooker CLI entry point, as ADR-0043's Decision
  anticipated.
- `README.md`, `docs/project-blueprint.md`, `tests/README.md`,
  `specs/README.md`, `src/README.md` — minimal current-state updates.

## Files / Modules Touched (expected)

**New — Asset System module**

- `src/asset_system/CMakeLists.txt`
- `src/asset_system/include/atlantis/asset_system/{static_mesh_asset_data,errors,logical_path,asset_id,asset_set_validation,mesh_source,asset_metadata,mesh_artifact,cook,load}.h`
- `src/asset_system/src/{static_mesh_asset_data,logical_path,asset_id,asset_set_validation,mesh_source,asset_metadata,mesh_artifact,cook,load}.cpp`

**New — Tools CLI**

- `src/tools/asset_cooker/{CMakeLists.txt,cook_command.h,cook_command.cpp,main.cpp}`

**New — assets**

- `assets/CMakeLists.txt`
- `assets/meshes/minimal_cube.mesh.txt`

**New — tests**

- `tests/asset_system/{CMakeLists.txt,static_mesh_asset_data_tests.cpp,logical_path_tests.cpp,asset_id_tests.cpp,asset_set_validation_tests.cpp,mesh_source_tests.cpp,asset_metadata_tests.cpp,mesh_artifact_tests.cpp,load_tests.cpp,module_boundary_tests.cpp}`
- `tests/tools/asset_cooker/{CMakeLists.txt,cook_command_tests.cpp,cooker_determinism_tests.cpp}`

**Modified**

- `CMakeLists.txt` (root) — five `add_subdirectory()` lines
  (`src/asset_system`, `src/tools/asset_cooker`, `assets`,
  `tests/asset_system`, `tests/tools/asset_cooker`) plus one
  `atlantis_finalize_asset_validation()` call.
- `tests/image_regression/fixture/{minimal_cube_fixture.h,minimal_cube_fixture.cpp,CMakeLists.txt}` — additive only.
- `tests/image_regression/{image_regression_gpu_tests.cpp,CMakeLists.txt}` — additive only.
- `AGENTS.md`, `docs/architecture/module_boundaries.md`, `README.md`,
  `docs/project-blueprint.md`, `tests/README.md`, `src/README.md`,
  `specs/README.md` (Step 7).

**Explicitly not touched:** `src/rhi/`, `src/renderer/`,
`src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
`src/shader_system/`, `src/tools/shader_compiler/`, `shaders/`,
`tests/image_regression/goldens/`,
`tests/image_regression/golden_generator/`, `cmake/`, and every existing
ADR.

## Sequencing & Dependencies

```
Step 1 (skeleton, types, errors)
  └─> Step 2 (logical path, Asset ID, set validation)
        └─> Step 3 (three format codecs)
              ├─> Step 4a (cook() + CLI)  ──> Step 4b (CMake + real asset)
              └─> Step 5 (file-level loader)
                        └─> Step 6 (composition root + GPU loop)  [needs 4b's artifact]
                              └─> Step 7 (full verification + docs)
```

- Step 3 needs Step 2 because the cooked metadata carries the Asset ID.
- Step 5 could technically precede Step 4, but is sequenced after so the
  loader's tests can consume real cooker output rather than only
  hand-built byte vectors.
- Step 6 needs **4b**, not merely 4a: the GPU test consumes the artifact
  the build actually produces.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | Logical path normalization: every legal form (nested, `.`, redundant separators, `\` input) normalizes as specified; every illegal form (absolute, drive prefix, root-escaping `..`, non-ASCII byte, empty) returns its own distinct `LogicalPathError`. Windows/Android neutrality asserted by construction: the same input string yields the same output with no platform conditional in the code path. | `logical_path_tests.cpp` | GPU-independent |
| V2 | FNV-1a known-answer vectors (including the canonical empty-string and `"a"` basis checks); the same logical path hashes identically across repeated calls; fixed 8-byte little-endian serialization asserted against explicit expected bytes; `toHexString()` fixed-width lowercase. | `asset_id_tests.cpp` | GPU-independent |
| V3 | Within one declared set: an Asset ID collision returns `AssetIdCollision`; a case-only-differing pair returns `CaseOnlyPathConflict`; an exact duplicate returns `DuplicateLogicalPath` — three distinct errors, never merged. A valid set returns `Ok`. | `asset_set_validation_tests.cpp` | GPU-independent |
| V4 | All three formats: round-trip; each malformed/truncated/unknown-version/out-of-range/non-finite-float case returns its own distinct error; **the cooked artifact's vertex and index bytes are byte-identical to the hand-authored `kCubeVertices`/`kCubeIndices` values.** | `mesh_source_tests.cpp`, `asset_metadata_tests.cpp`, `mesh_artifact_tests.cpp` | GPU-independent |
| V5 | Cooking the same source twice produces byte-identical `.amesh` **and** `.amesh.meta.txt`. | `cooker_determinism_tests.cpp` | `tool`-labeled |
| V6 | Re-import triggering: source change and cooker change each re-run the cooker; an unrelated file change does not; a no-op rebuild does not. | D7 scripted procedure | **Manual/scripted, recorded in the PR** |
| V7 | The CPU loader touches no GPU/Vulkan type: `atlantis_asset_system_tests` links no RHI/Renderer/Vulkan target and passes with no Vulkan device present. | `tests/asset_system/` | GPU-independent |
| V8 | Real GPU: load the imported cube, render through the existing unmodified path, compare against the existing golden — **zero** channel difference. Golden unmodified. | `image_regression_gpu_tests.cpp` | `gpu`-labeled |
| V9 | Module boundary static check: no source under `src/asset_system/` includes `atlantis/rhi/`, `atlantis/renderer/`, `atlantis/render_graph/`, `atlantis/shader_system/`, `atlantis/platform/`, `atlantis/vulkan_backend/`, or any `vulkan` header. Implemented as a test that scans the source directory (path supplied by `target_compile_definitions`), mirroring the grep-verifiable boundary pattern ADR-0029 already established. | `module_boundary_tests.cpp` | GPU-independent |
| V10 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green; Vulkan Validation Layers zero warnings/errors on every GPU-touching path (grepped, not merely inferred from exit status). | Both configurations | Manual, recorded |

## Traceability — Spec / ADR → Plan

| Source requirement | Where satisfied |
|---|---|
| Spec 0012 — Asset identity, deterministic, non-rename-durable | Step 2; V1, V2 |
| Spec 0012 — Path normalization contract, platform-invariant | D3/Step 2; V1 |
| Spec 0012 — Collision + case conflict, scoped to declared set | D4/Step 2, Step 4b; V3 |
| Spec 0012 — Metadata schema, strict, versioned | D3/Step 3; V4 |
| Spec 0012 — Authoring/runtime separation with named transformation step | Steps 3–5 (source → `cook()` → artifact → `load()`) |
| Spec 0012 — Deterministic import, proven empirically | D7/Step 4a; V5 |
| Spec 0012 — Re-import triggering, not a cache | D7/Step 4b; V6 |
| Spec 0012 — Module ownership, Core-only, no reverse dependency | D2/Step 1; V7, V9 |
| Spec 0012 — Composition root owns GPU handoff | Step 6 |
| Spec 0012 — First closed loop, zero channel difference | D8/Step 6; V8 |
| Spec 0012 — Windows-now / Android-later artifact principle | D3 (fixed little-endian, ASCII paths, no platform conditional); V1, V2 |
| Spec 0012 — No global mutable asset database | `StaticMeshAssetData` is a caller-owned value; no static/singleton state anywhere in the module |
| ADR-0043 — Tenth module, Core-only, Tools → Asset System | D2; V9 |
| ADR-0043 — CPU-side `StaticMeshAssetData`, no GPU mesh creation | Step 1, Step 5; V7 |
| ADR-0043 — No Renderer-integration submodule | D2 (no such target exists in the target table) |
| ADR-0044 — Logical path rules, ASCII Phase 1, FNV-1a 64-bit | Step 2; V1, V2 |
| ADR-0044 — Asset ID serialization (binary + hex text) | D3/Step 2; V2 |
| ADR-0044 — Collision detection scoped to one invocation | D4; V3 |
| ADR-0044 — Metadata field semantics | D3/Step 3 |
| ADR-0044 — Importer provenance anchor | D5 |
| ADR-0044 — No derived-data cache | D7 (explicitly stated) |
| ADR-0044 — Checked-in artifact discipline | D1 (not checked in — clause vacuous) |
| ADR-0045 — Three hand-rolled formats, mandatory `schema_version` | D3 |
| ADR-0045 — Unconditional little-endian on disk | D3 (shift/mask encoding); V2, V4 |
| ADR-0045 — No new third-party dependency | No `FetchContent`, `find_package`, or `cmake/AtlantisDependencies.cmake` edit appears anywhere in this Plan's file list |

## Rollback Plan

Every step is additive. Reverting the Implementation PR removes
`src/asset_system/`, `src/tools/asset_cooker/`, `assets/`,
`tests/asset_system/`, and `tests/tools/asset_cooker/` wholesale, and
reverts the additive-only edits to the root `CMakeLists.txt` and to
`tests/image_regression/`. Because no existing module's source is
modified and the golden is untouched, revert restores the exact
pre-Plan build and test behaviour with no migration step. Individual
steps can also be reverted in reverse order, with the one constraint
that 4b must be reverted before 4a (4b's CMake wiring references the
cooker target).

## Deviations, objections, and open mechanical details

**No objection to Spec 0012 or ADR-0043/0044/0045 was found while
drafting this Plan.** Every Accepted decision proved implementable
against the real source tree as written; nothing required a new
architectural decision, and no Accepted boundary needed relaxing.

Two honest limitations, disclosed rather than papered over:

1. **V6 (re-import triggering) is a scripted manual procedure, not a
   ctest.** CMake `DEPENDS` staleness is build-system behaviour and this
   repository has no build-system-level test harness; inventing one is
   outside this Plan's scope. The procedure is exact and repeatable, and
   its result is recorded in the Implementation PR.
2. **V8 runs on one GPU vendor/driver.** Same disclosed single-vendor
   limitation every prior GPU-touching spec in this repository carries;
   not cross-vendor coverage.

Mechanical details deliberately left for Plan Review to confirm or
redirect — none changes a boundary, format, or algorithm:

- Exact target/CLI names (`atlantis_asset_cooker`, `Atlantis::AssetSystem`).
- Exact file extensions (`.mesh.txt`, `.amesh`, `.amesh.meta.txt`).
- Whether `assets/` or `assets/meshes/` is the more natural home for the
  one shipped asset.
- Whether the new GPU case joins the existing
  `atlantis_image_regression_gpu_tests` executable (this Plan's choice,
  for minimal new CMake surface) or gets its own.
- The exact wording of the `AGENTS.md` and `module_boundaries.md` Step 7
  edits.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- V1–V10 all executed and recorded, with V6 and V10 recorded as manual
  verification in the Implementation PR.
- The golden under `tests/image_regression/goldens/` is confirmed
  **unchanged** in the final diff.
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`,
  `src/shader_system/`, or `shaders/` was modified.
- No new third-party dependency appears in `cmake/` or any
  `CMakeLists.txt`.
