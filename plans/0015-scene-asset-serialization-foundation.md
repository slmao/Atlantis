# Plan: Scene Asset & Serialization Foundation

- **Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)
  (`Approved`, Human Review Approval recorded 2026-08-23, accepting all
  16 items of that Spec's own Human Review Decision Table — see that
  Spec's own approval note for the full record)
- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path.

## Objective

Implement Spec 0015 in full: replace Runtime's hard-coded
`buildValidationScene()` with a real load pipeline — a human-editable
scene authored once (`assets/scenes/world_scene.scene.txt`), cooked at
build time into a versioned, unconditionally little-endian runtime
artifact plus a metadata sidecar (`Atlantis::AssetSystem`), loaded by
`Atlantis Runtime` through an unforgeable, encapsulated
`ValidatedSceneData` value into a fresh `Atlantis::World` via a new,
infallible `World` entry point — reproducing Spec 0014's own five-cube
validation scene byte-for-byte, proved by reusing the existing
`world_scene` image-regression golden with zero difference. No Plan
content here decides anything ADR-0052–0054 or Spec 0015's own Human
Review Decision Table already fixed; this Plan only supplies the
concrete C++/CMake/file-format shapes those documents deliberately left
open.

## Plan-level decisions (fixed here, not left to Implementation)

### D1. Targets, namespaces, directories — no new module

Every new file extends an already-`Accepted` module or target — no new
CMake target category, no new top-level module, matching Spec 0015's
own Non-Goals and ADR-0052's own Decision:

| New file | Extends | Namespace |
|---|---|---|
| `src/asset_system/include/atlantis/asset_system/scene_types.h`, `src/asset_system/src/scene_types.cpp` | `atlantis_asset_system` (existing target) | `atlantis::asset_system` |
| `src/asset_system/include/atlantis/asset_system/scene_source.h`, `src/asset_system/src/scene_source.cpp` | same | same |
| `src/asset_system/include/atlantis/asset_system/scene_artifact.h`, `src/asset_system/src/scene_artifact.cpp` | same | same |
| `src/asset_system/include/atlantis/asset_system/cook_scene.h` (declares `cookScene()`, added to existing `cook.h`'s own translation unit split — see D4) | same | same |
| `src/asset_system/include/atlantis/asset_system/decode_scene.h` (declares `decodeScene()`) | same | same |
| `src/asset_system/include/atlantis/asset_system/errors.h` (extended in place — `SceneCookError`, `SceneArtifactDecodeError` added, nothing existing removed or renumbered) | same | same |
| `src/world/include/atlantis/world/scene_instantiation.h`, `src/world/src/scene_instantiation.cpp` | `atlantis_world` (existing target) | `atlantis::world` |
| `src/runtime/include/atlantis/runtime/scene_manifest.h`, `src/runtime/src/scene_manifest.cpp` | `atlantis_runtime_host` (existing target) | `atlantis::runtime` |
| `src/runtime/include/atlantis/runtime/init_error.h` (extended in place) | same | same |
| `src/runtime/include/atlantis/runtime/bootstrap_config.h` (extended in place) | same | same |
| `src/tools/asset_cooker/cook_command.h`/`.cpp` (extended in place — a third mode) | `atlantis_asset_cooker_lib` (existing target) | `atlantis::tools::asset_cooker` |
| `src/asset_system/CMakeLists.txt` (extended in place — `atlantis_add_scene_asset()`, and one additive `ATLANTIS_<name>_LOGICAL_PATH` export line inside the existing `atlantis_add_static_mesh_asset()`) | — | CMake |
| `assets/scenes/world_scene.scene.txt`, `assets/CMakeLists.txt` (extended) | — | — |

`Atlantis::AssetSystem` gains no new dependency (`Atlantis::Core` only,
unchanged — `scene_types.h`'s own DTOs are plain value types).
`Atlantis::World` gains no new dependency (already links
`Atlantis::AssetSystem` `PUBLIC`, per `src/world/CMakeLists.txt`, since
`renderable.h` already names `AssetId` publicly — `scene_instantiation.h`
naming `atlantis::asset_system::ValidatedSceneData` needs no
`target_link_libraries()` change). `Atlantis Runtime` gains no new
dependency (already depends on both `Atlantis::World` and
`Atlantis::AssetSystem`). `tests/world/module_boundary_tests.cpp`'s own
existing scan (V26) confirms `src/world/` still names no RHI/Renderer/
RenderGraph/ShaderSystem/Platform/VulkanBackend/Runtime header;
`tests/asset_system/module_boundary_tests.cpp`'s own existing scan
confirms `src/asset_system/` still names no `atlantis/world/` header —
**this is the direct, automated proof that ADR-0052's own cycle-
avoidance Decision actually holds in the real source tree, not merely
in this Plan's own prose.**

### D2. Public value types — exact C++ shapes

```cpp
// src/asset_system/include/atlantis/asset_system/scene_types.h
namespace atlantis::asset_system {

// Plain, flat, AssetSystem-owned DTOs -- never atlantis::world's own
// Transform/Camera/Renderable (ADR-0053's own hard constraint: naming
// one here would give AssetSystem a compile-time dependency on World).
struct DecodedTransform {
  float positionX = 0.0f, positionY = 0.0f, positionZ = 0.0f;
  float eulerXRadians = 0.0f, eulerYRadians = 0.0f, eulerZRadians = 0.0f;
  float scaleX = 1.0f, scaleY = 1.0f, scaleZ = 1.0f;
};

struct DecodedCamera {
  float fovYRadians = 0.0f;
  float nearZ = 0.0f;
  float farZ = 0.0f;
};

struct DecodedRenderable {
  atlantis::asset_system::AssetId meshAsset = 0;
};

}  // namespace atlantis::asset_system

// src/asset_system/include/atlantis/asset_system/validated_scene_data.h
namespace atlantis::asset_system {

struct ValidatedSceneNode {
  DecodedTransform transform;
  std::optional<DecodedCamera> camera;
  std::optional<DecodedRenderable> renderable;
};

// ADR-0053 D4 / Spec 0015 Human Review Approval item 3: encapsulated so
// "fully validated" is a type-level guarantee. Every structural field
// private; the only non-default constructor private, callable only by
// decodeScene() (a named friend function, matching EntityId's own
// friend class World). Public surface is read-only accessors only.
class ValidatedSceneData {
 public:
  ValidatedSceneData() = default;  // empty (zero-node) scene -- vacuously valid

  [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }
  [[nodiscard]] const ValidatedSceneNode& node(std::size_t index) const noexcept { return nodes_[index]; }
  [[nodiscard]] std::optional<std::size_t> parentOf(std::size_t index) const noexcept { return parents_[index]; }
  [[nodiscard]] std::optional<std::size_t> activeCameraIndex() const noexcept { return activeCameraIndex_; }

  // Copy/move: defaulted -- nothing on the public surface can mutate an
  // instance, so a copy or a moved-from/to pair are each independently
  // valid by construction; no special handling is needed or written.
  ValidatedSceneData(const ValidatedSceneData&) = default;
  ValidatedSceneData(ValidatedSceneData&&) noexcept = default;
  ValidatedSceneData& operator=(const ValidatedSceneData&) = default;
  ValidatedSceneData& operator=(ValidatedSceneData&&) noexcept = default;

 private:
  friend atlantis::Result<ValidatedSceneData, SceneArtifactDecodeError> decodeScene(
      const std::string& artifactPath, const std::string& metadataPath);

  ValidatedSceneData(std::vector<ValidatedSceneNode> nodes, std::vector<std::optional<std::size_t>> parents,
                      std::optional<std::size_t> activeCameraIndex)
      : nodes_(std::move(nodes)), parents_(std::move(parents)), activeCameraIndex_(activeCameraIndex) {}

  std::vector<ValidatedSceneNode> nodes_;
  std::vector<std::optional<std::size_t>> parents_;
  std::optional<std::size_t> activeCameraIndex_;
};

}  // namespace atlantis::asset_system
```

- **`errors.h` additions** (appended after the existing five enums; none
  renumbered, none removed):

  ```cpp
  enum class SceneCookError {
    SourceFileUnreadable,
    SourceParseFailed,
    DuplicateNodeId,
    UndeclaredParentReference,
    ParentCycle,
    UndeclaredActiveCameraReference,
    ActiveCameraMissingCamera,
    NonFiniteValue,
    ArtifactWriteFailed,
    MetadataWriteFailed,
  };

  enum class SceneArtifactDecodeError {
    ArtifactUnreadable,
    MetadataUnreadable,
    TooSmallForHeader,
    BadMagic,
    UnknownSchemaVersion,
    InconsistentOffsets,
    SizeMismatch,
    NodeCountOutOfRange,
    OutOfRangeParentIndex,
    CyclicParent,
    OutOfRangeActiveCameraIndex,
    ActiveCameraMissingCamera,
    NonFiniteValue,
    MetadataParseFailed,
    MetadataArtifactMismatch,
  };
  ```

  `SceneCookError` folds `SourceFileUnreadable`/`SourceParseFailed`
  (I/O and grammar-level failures — `SourceParseFailed` covers every
  strict-grammar rejection the authoring parser itself reports, D3) and
  `ArtifactWriteFailed`/`MetadataWriteFailed` (matching `CookError`'s
  own precedent) alongside the five structural/semantic conditions D3's
  own validation algorithm produces. `SceneArtifactDecodeError` combines
  `ArtifactDecodeError`'s own binary-decode granularity with
  `AssetLoadError`'s own I/O/metadata-cross-check granularity into one
  enum, because `decodeScene()` — per Spec 0015's own Requirements — is
  a single function playing both roles at once (unlike the mesh
  pipeline's separate `decodeMeshArtifact()`/`loadStaticMeshAsset()`
  split); `MetadataParseFailed` wraps a `MetadataParseError` for
  diagnostic detail (reusing that existing, already-generic-shaped enum
  for the scene metadata sidecar's own strict-text parsing — no new
  parse-error enum needed, D6).
- **`atlantis::world::fromValidatedSceneData()`** — declared in the new
  `scene_instantiation.h`, not a `World` member function (keeps
  `world.h` itself untouched; matches
  `world_error.h`/`entity_id.h` already being separate headers from
  `world.h`):

  ```cpp
  // src/world/include/atlantis/world/scene_instantiation.h
  namespace atlantis::world {
  [[nodiscard]] World fromValidatedSceneData(const atlantis::asset_system::ValidatedSceneData& scene);
  }  // namespace atlantis::world
  ```

  Infallible — returns `World` by value (ADR-0054 D4; Spec 0015 Human
  Review Approval item 4). No new `WorldError` enumerator.
- **`RuntimeInitError` additions** (`src/runtime/include/atlantis/runtime/init_error.h`,
  appended, none renumbered):

  ```cpp
  enum class RuntimeInitError {
    // ...existing enumerators unchanged (PlatformInitFailed, ShaderLoadFailed,
    // DeviceCreateFailed, AssetLoadFailed, AssetMetadataParseFailed,
    // MeshCreateFailed, CameraBufferCreateFailed, SceneConstructionFailed)...
    SceneManifestLoadFailed,      // manifest missing, malformed, or fails its own validation (D8)
    SceneArtifactLoadFailed,      // decodeScene() returned Err
    SceneDependencyUnresolved,    // a referenced AssetId has no resolver entry
    SceneDependencyLoadFailed,    // a resolved AssetId's own mesh load failed
  };
  ```

  Each wraps the underlying `SceneManifestError`/`SceneArtifactDecodeError`/
  etc. only for a logged diagnostic string — the enumerator itself is
  Runtime's own classification, per ADR-0054's own explicit "never
  `WorldError`/`SceneCookError`/`SceneArtifactDecodeError`/`AssetLoadError`
  directly" requirement.
- **`SceneManifestError`** (Runtime-private, `scene_manifest.h`):

  ```cpp
  // src/runtime/include/atlantis/runtime/scene_manifest.h
  namespace atlantis::runtime {

  enum class SceneManifestError {
    ManifestUnreadable,
    MalformedEntry,
    DuplicateLogicalPath,
    AssetIdCollision,
    MetadataArtifactMismatch,
  };

  struct SceneDependencyResolver {
    // Deterministic-iteration container, not std::unordered_map -- see
    // D8's own rationale. A sorted std::vector<std::pair<AssetId, Entry>>,
    // looked up via std::lower_bound(), is sufficient at this Plan's own
    // scale and needs no third-party dependency.
    struct Entry {
      std::string artifactPath;
      std::string metadataPath;
    };
    std::vector<std::pair<atlantis::asset_system::AssetId, Entry>> entries;  // kept sorted by AssetId

    [[nodiscard]] const Entry* find(atlantis::asset_system::AssetId id) const noexcept;
  };

  [[nodiscard]] atlantis::Result<SceneDependencyResolver, SceneManifestError> loadSceneDependencyManifest(
      const std::string& manifestPath);

  }  // namespace atlantis::runtime
  ```
- **`BootstrapConfig` additions** (`src/runtime/include/atlantis/runtime/bootstrap_config.h`,
  appended):

  ```cpp
  struct BootstrapConfig {
    // ...existing fields unchanged...
    std::string sceneArtifactPath;
    std::string sceneMetadataPath;
    std::string sceneDependencyManifestPath;
  };
  ```

### D3. Authoring source grammar — exact fields

Extends `mesh_source.h`'s own strict, fixed-order, `std::from_chars`-
parsed, plain-text discipline. File extension `.scene.txt`
(`kSceneAuthoringExtension`, mirroring `kAuthoringExtension = ".mesh.txt"`
in `cook_command.cpp`).

```
atlantis_scene_source_version: 1
node_count: 6
active_camera: 6
node: node_id=1 parent=none position=-2.5 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube.mesh.txt
node: node_id=2 parent=none position=-1.0 0.0 0.0 rotation=0.0 0.5236 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube.mesh.txt
node: node_id=3 parent=none position=1.0 -0.5 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube.mesh.txt
node: node_id=4 parent=3 position=0.0 1.3 0.0 rotation=0.0 0.7854 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube.mesh.txt
node: node_id=5 parent=none position=2.5 0.0 0.0 rotation=0.2618 0.3491 0.0 scale=1.0 1.0 1.0 mesh=meshes/minimal_cube.mesh.txt
node: node_id=6 parent=none position=0.0 2.2 7.0 rotation=-0.3054 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0
```

- `node_id`: `std::uint32_t`, author-assigned, unique within the file.
- `parent`: `none` or another node's own `node_id`.
- `position`/`rotation`/`scale`: three `std::from_chars`-parsed floats
  each (matching `MeshSourceVertex`'s own field style) — always
  present (a node with no meaningful transform authors identity
  values, matching `Transform`'s own default).
- `mesh=<logical path>`: optional; the referenced mesh's own **exact**
  CMake `SOURCE` string (e.g. `meshes/minimal_cube.mesh.txt`, extension
  included — confirmed against `cook_command.cpp`'s own
  `computeRelativePathString()`, which does **not** strip the
  authoring extension before it is used as `cookStaticMesh()`'s own
  `logicalPathInput`; only the separate `base` variable, used for
  output *file naming*, has it stripped). Mutually exclusive with the
  three `camera_*` fields on the same line (a node is a `Renderable`
  or a `Camera`, matching this Spec's own fixed six-node validation
  scene; a future scene could still author neither on a given node,
  e.g. a pure hierarchy pivot).
- `camera_fov_y`/`camera_near_z`/`camera_far_z`: optional, present only
  on a node meant to carry a `Camera`.
- `active_camera: <node_id>`: a scene-level field (not per-node),
  parsed once, before any `node:` line — the grammar has exactly one
  slot for it, so "more than one node claims the active-camera role"
  is not a reachable authoring mistake this grammar can even express
  (Spec 0015's own Human Review Approval item 9 — `UndeclaredActiveCameraReference`
  and `ActiveCameraMissingCamera` are the two reachable active-camera
  mistakes, both checked, D4).

`parseSceneSource()`/`serializeSceneSource()` (`scene_source.h`) mirror
`parseMeshSource()`/`serializeMeshSource()`'s own exact discipline:
anchored-prefix field matching, fixed field order, `node_count`
range-checked before any `node:` line is read (mirroring
`mesh_source.cpp`'s own already-fixed unbounded-allocation bug from
Plan 0012's own post-merge review — this Plan does not repeat that
mistake). Produces a plain, authoring-shaped `ParsedSceneSource`
(node array with `node_id`, parent-as-`node_id`, `DecodedTransform`,
optional `DecodedCamera`, optional mesh logical-path string,
scene-level active-camera `node_id`) — **not** `ValidatedSceneData`;
that type does not exist until cook-time validation (D4) passes.

### D4. Scene cooker — validation algorithm, remapping, atomic write

`cookScene(sourceFilePath, artifactOutputPath, metadataOutputPath) ->
Result<std::monostate, SceneCookError>` (`cook_scene.h`; no separate
"logical path for self" parameter — a scene has no `AssetId` of its
own, only its `Renderable` references do, D2):

1. Read `sourceFilePath` (`SourceFileUnreadable` on failure); parse via
   `parseSceneSource()` (`SourceParseFailed` on failure).
2. **Duplicate `node_id`**: a `std::vector<std::uint32_t>` of seen IDs,
   sorted and checked for adjacency after `std::sort` — `O(n log n)`,
   sufficient at this Plan's own scale (`DuplicateNodeId`).
3. **Undeclared parent**: every non-`none` parent `node_id` must appear
   in the declared set (`UndeclaredParentReference`).
4. **Parent cycle**: walk each node's own ancestor chain (via the
   authoring-time `node_id`-keyed parent map, not an array index — this
   is genuinely a second, small, independent implementation of the same
   *algorithm* `World::setParent()`'s own ancestor walk already uses,
   applied to authoring-time data rather than a live slot map,
   consistent with this repository's own "duplicated, not shared"
   precedent) (`ParentCycle`).
5. **Active camera reference**: `active_camera`'s own `node_id` must be
   declared (`UndeclaredActiveCameraReference`) and that node must
   carry a `camera_*` triple (`ActiveCameraMissingCamera`).
6. **Non-finite values**: every authored float (`position`/`rotation`/
   `scale`/`camera_*`) checked via `std::isfinite()` (`NonFiniteValue`).
7. **Mesh reference resolution**: for each node with a `mesh=` field,
   `normalizeLogicalPath()` + `computeAssetId()` over the authored
   string — the *existing*, already-`Accepted` mesh pipeline's own
   pure functions, unchanged. (A malformed logical path here surfaces
   through `normalizeLogicalPath()`'s own existing `LogicalPathError`,
   folded into `SourceParseFailed` for this enum's own granularity —
   matching how `cookStaticMesh()`'s own `CookError::LogicalPathInvalid`
   is already a single, coarse case.)
8. **Dense remapping**: build a `node_id → array index` map in
   declaration order (the order `parseSceneSource()`'s own node array
   already has); rewrite every parent/active-camera reference from
   `node_id` to array index.
9. **Encode** (D5) and **write atomically** — write-to-temp-then-rename
   in `artifactOutputPath`'s/`metadataOutputPath`'s own directory,
   identical to `cookStaticMesh()`'s own established pattern
   (`ArtifactWriteFailed`/`MetadataWriteFailed`).

**Determinism**: no filesystem timestamp, no non-deterministic
iteration (steps 2–3 sort explicitly; step 8's remapping is declaration-
order, not hash-order) reaches output bytes — two cooks of an unchanged
source produce byte-identical artifact + metadata (V12). A hand-bumped
`kSceneCookerVersion` constant (`cook_scene.h`, independently versioned
from `kImporterVersion`) is recorded in the metadata sidecar.

### D5. Scene artifact binary layout

Unconditionally little-endian, explicit shift/mask assembly — never a
struct `memcpy`, matching `mesh_artifact.h`'s own discipline exactly
(`scene_artifact.h`'s own `encodeSceneArtifact()`/`decodeSceneArtifact()`,
called by `cookScene()`/`decodeScene()` respectively):

```
Header (24 bytes):
  magic            4 bytes  ("ASCN")
  schema_version   4 bytes  (uint32, little-endian)
  node_count       4 bytes  (uint32, little-endian)
  has_active_camera 4 bytes (uint32, 0 or 1)
  active_camera_index 4 bytes (uint32, meaningful only if has_active_camera)
  reserved         4 bytes  (0, 8-byte header alignment for the record array below)

Per-node record (fixed size, node_count repetitions):
  position          3 x float32 (12 bytes)
  rotation          3 x float32 (12 bytes)
  scale             3 x float32 (12 bytes)
  has_camera        uint32 (4 bytes)
  fov_y/near_z/far_z 3 x float32 (12 bytes, meaningful only if has_camera)
  has_renderable    uint32 (4 bytes)
  mesh_asset_id     uint64, little-endian (8 bytes, meaningful only if has_renderable)
  has_parent        uint32 (4 bytes)
  parent_index      uint32 (4 bytes, meaningful only if has_parent)
```

Every `float`/`uint64` field is reinterpreted via `std::bit_cast` before
shift/mask serialization, matching `mesh_artifact.h`'s own comment
("vertex floats are first reinterpreted via `std::bit_cast<std::uint32_t>`...
this format never `memcpy`'s a C++ struct"). `kSceneArtifactSchemaVersion = 1`.
Metadata sidecar (`scene_metadata.h`, text, mirroring `AssetMetadata`'s
own shape minus the mesh-specific fields): `schema_version`, `node_count`
— cross-checked against the artifact's own header at decode time
(`MetadataArtifactMismatch`).

### D6. Scene decoder — independent re-validation

`decodeScene(artifactPath, metadataPath) -> Result<ValidatedSceneData,
SceneArtifactDecodeError>` (`decode_scene.h`) — **never assumes a
well-formed cooker output**, re-derives every D4 condition from the
artifact's own bytes:

1. Read both files (`ArtifactUnreadable`/`MetadataUnreadable`).
2. Decode header: size ≥ 24 bytes (`TooSmallForHeader`), magic
   (`BadMagic`), `schema_version` known (`UnknownSchemaVersion`),
   declared byte size vs. `node_count`-implied size consistent
   (`InconsistentOffsets`, `SizeMismatch`).
3. **`node_count` bound check** before allocating the node vector
   (`NodeCountOutOfRange`) — mirroring `ArtifactDecodeError::VertexCountOutOfRange`'s
   own precedent exactly: never trust a declared count enough to
   allocate on its word alone.
4. Decode every per-node record; for each: `has_parent` → `parent_index
   < node_count` (`OutOfRangeParentIndex`); every `float` field
   `std::isfinite()` (`NonFiniteValue`).
5. **Cycle re-check**: walk each node's own parent-index chain (array-
   index-based this time, not `node_id`-based — a second, independent
   algorithm instance from D4's own, per this repository's own
   "duplicated, not shared" precedent) (`CyclicParent`).
6. `has_active_camera` → `active_camera_index < node_count`
   (`OutOfRangeActiveCameraIndex`) and that node's own `has_camera`
   flag is set (`ActiveCameraMissingCamera`).
7. Parse the metadata sidecar via the existing `parseAssetMetadata()`-
   sibling strict-text discipline, reusing `MetadataParseError`
   (`MetadataParseFailed`); cross-check its own `node_count` against
   the artifact's own header (`MetadataArtifactMismatch`).
8. On success: construct `ValidatedSceneData` via its own `private`
   constructor (this is the **only** call site in the entire codebase
   permitted to do so, enforced by the `friend` declaration, D2).

By this point every structural and semantic precondition D9 (`World`
instantiation) depends on is proven — nothing downstream re-checks any
of it (V21).

### D7. CMake — `atlantis_add_scene_asset()`, manifest generation

One minimal, additive line inside the *existing*
`atlantis_add_static_mesh_asset()` (`src/asset_system/CMakeLists.txt`):

```cmake
set(ATLANTIS_${ARG_NAME}_LOGICAL_PATH "${ARG_SOURCE}" PARENT_SCOPE)
```

(Exposes the exact string `cookStaticMesh()` was already given as its
own `logicalPathInput` — no behavior change to any existing caller;
existing callers simply gain one more unused-by-them exported
variable.) New function, same file:

```cmake
function(atlantis_add_scene_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE)
  set(multiValueArgs MESH_DEPENDENCIES)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")

  string(REGEX REPLACE "\\.scene\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.ascene")
  set(metadata_path "${output_dir}/${base}.ascene.meta.txt")
  set(manifest_path "${output_dir}/${base}.ascene.manifest.txt")
  set(stamp "${output_dir}/${base}.stamp")

  set(dependency_targets "")
  set(manifest_lines "")
  foreach(dep_name ${ARG_MESH_DEPENDENCIES})
    if(NOT DEFINED ATLANTIS_${dep_name}_ARTIFACT_PATH)
      message(FATAL_ERROR
        "atlantis_add_scene_asset(${ARG_NAME}): MESH_DEPENDENCIES entry "
        "'${dep_name}' is not a previously declared asset -- declare it "
        "via atlantis_add_static_mesh_asset() first.")
    endif()
    list(APPEND dependency_targets "${ATLANTIS_${dep_name}_TARGET}")
    string(APPEND manifest_lines
      "${ATLANTIS_${dep_name}_LOGICAL_PATH}\t${ATLANTIS_${dep_name}_ARTIFACT_PATH}\t${ATLANTIS_${dep_name}_METADATA_PATH}\n")
  endforeach()

  # file(GENERATE) runs at CMake generate time, independent of which
  # config a multi-config (Visual Studio) generator later builds --
  # this manifest's own content (paths, not compiled binaries) does not
  # vary by config, matching atlantis_add_static_mesh_asset()'s own
  # ARTIFACT_PATH/METADATA_PATH (also config-independent, both already
  # proven working under this project's own real Visual Studio
  # generator). No $<CONFIG> generator expression is needed or used.
  file(GENERATE OUTPUT "${manifest_path}" CONTENT "${manifest_lines}")

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}"
    COMMAND atlantis_asset_cooker
      --kind=scene --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp}
    DEPENDS ${source_path} atlantis_asset_cooker ${dependency_targets}
    COMMENT "Cooking scene asset: ${ARG_SOURCE}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")

  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_MANIFEST_PATH "${manifest_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()
```

**Rebuild scoping**: `DEPENDS` names exactly `${source_path}`,
`atlantis_asset_cooker`, and each declared dependency's own `_TARGET` —
nothing else. Editing the scene source, the cooker, or a declared
mesh dependency re-triggers this custom command (ordinary CMake
staleness checking, identical to `atlantis_add_static_mesh_asset()`'s
own already-working mechanism); an unrelated asset never appears in
this list and never triggers it (V13). A `MESH_DEPENDENCIES` entry the
scene's own authoring source never actually references still
contributes a manifest line and a `DEPENDS` edge (harmless — matching
Spec 0015's own explicit "not an error" decision, item 9) but costs
nothing beyond one unused resolver-map entry.

`assets/CMakeLists.txt` gains:

```cmake
atlantis_add_scene_asset(
  NAME world_scene
  SOURCE scenes/world_scene.scene.txt
  MESH_DEPENDENCIES minimal_cube
)
set(ATLANTIS_world_scene_ARTIFACT_PATH "${ATLANTIS_world_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_METADATA_PATH "${ATLANTIS_world_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_MANIFEST_PATH "${ATLANTIS_world_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_TARGET "${ATLANTIS_world_scene_TARGET}" PARENT_SCOPE)
```

(Second `PARENT_SCOPE` propagation up to root scope — the same
mechanical, already-documented `minimal_cube`/shader precedent
`assets/CMakeLists.txt` already uses, D1's own table.)

`atlantis_asset_cooker`'s own CLI (`main.cpp`) gains `--kind=scene`
(default, absent flag, remains today's mesh-cook behavior — no
behavior change for any existing invocation); `cook_command.h`'s own
`CookCommandRequest` gains an `AssetKind kind = AssetKind::StaticMesh;`
field (`enum class AssetKind { StaticMesh, Scene };`); `cook_command.cpp`'s
own `runCookCommand()` dispatches to a new `runCookSceneMode()`
alongside the existing (renamed) `runCookMeshMode()`, calling
`cookScene()` with a scene-specific `kSceneAuthoringExtension = ".scene.txt"`
constant mirroring `kAuthoringExtension` exactly.

### D8. Manifest format and Runtime-side validation

Text, one line per entry, tab-separated (matching this Plan's own
`file(GENERATE)` output above): `<logical path>\t<artifact
path>\t<metadata path>`. `loadSceneDependencyManifest()`
(`scene_manifest.cpp`):

1. Read the file (`ManifestUnreadable`); split into lines; a line with
   not exactly two tabs, or an empty field, is `MalformedEntry`.
2. For each well-formed line: `normalizeLogicalPath()` +
   `computeAssetId()` over the logical-path field (the exact same
   already-public functions the cooker itself uses) — a
   `LogicalPathError` here also folds into `MalformedEntry`.
3. **Duplicate logical path**: sort-and-adjacency-check, mirroring D4's
   own step 2 (`DuplicateLogicalPath`).
4. **`AssetId` collision**: after computing every entry's own `AssetId`,
   sort by `AssetId` and check adjacency for a *different* logical path
   producing the *same* `AssetId` (`AssetIdCollision`) — mirroring
   `AssetSetError::AssetIdCollision`'s own precedent, scoped per-scene
   rather than repository-global.
5. **Metadata cross-check**: for each entry, read its own metadata
   sidecar via the existing `parseAssetMetadata()`, compare its own
   `assetId` field against the `AssetId` just computed from the
   manifest's own logical-path field (`MetadataArtifactMismatch`).
6. Build `SceneDependencyResolver::entries` as a `AssetId`-sorted
   `std::vector`, never a `std::unordered_map` (D2's own type; the
   *storage* choice, not merely the *iteration* choice, is what makes
   D9's own load order genuinely deterministic — an
   `unordered_map`-backed resolver would make "ascending first-
   reference order" meaningless downstream even if the *caller's* own
   loop order were otherwise deterministic).

A declared-but-unreferenced manifest entry is not detected or rejected
here — this function has no visibility into which entries the scene
actually references; that check happens in D10's own resolve phase,
which simply never looks up an unreferenced entry (matching Spec
0015's own explicit "not an error" decision).

### D9. `World::fromValidatedSceneData()` — two-pass instantiation

```cpp
World fromValidatedSceneData(const ValidatedSceneData& scene) {
  World world;
  std::vector<EntityId> byIndex;
  byIndex.reserve(scene.nodeCount());

  // Pass 1: every node exists before any parent link is set.
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    const ValidatedSceneNode& n = scene.node(i);
    const EntityId id = world.createEntity();

    Transform t;
    t.localPosition = {n.transform.positionX, n.transform.positionY, n.transform.positionZ};
    t.localEulerAnglesRadians = {n.transform.eulerXRadians, n.transform.eulerYRadians, n.transform.eulerZRadians};
    t.localScale = {n.transform.scaleX, n.transform.scaleY, n.transform.scaleZ};
    ATLANTIS_CHECK_MSG(world.setLocalTransform(id, t).isOk(), "fromValidatedSceneData(): setLocalTransform() failed for a freshly-created entity");

    if (n.camera.has_value()) {
      ATLANTIS_CHECK_MSG(world.setCamera(id, Camera{n.camera->fovYRadians, n.camera->nearZ, n.camera->farZ}).isOk(),
                          "fromValidatedSceneData(): setCamera() failed for a freshly-created entity");
    }
    if (n.renderable.has_value()) {
      ATLANTIS_CHECK_MSG(world.setRenderable(id, Renderable{n.renderable->meshAsset}).isOk(),
                          "fromValidatedSceneData(): setRenderable() failed for a freshly-created entity");
    }
    byIndex.push_back(id);
  }

  // Pass 2: parent links, using the pass-1 mapping -- discarded when this function returns.
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (const auto parentIndex = scene.parentOf(i); parentIndex.has_value()) {
      ATLANTIS_CHECK_MSG(world.setParent(byIndex[i], byIndex[*parentIndex]).isOk(),
                          "fromValidatedSceneData(): setParent() failed for an already-validated, acyclic hierarchy");
    }
  }

  if (const auto activeCameraIndex = scene.activeCameraIndex(); activeCameraIndex.has_value()) {
    ATLANTIS_CHECK_MSG(world.setActiveCamera(byIndex[*activeCameraIndex]).isOk(),
                        "fromValidatedSceneData(): setActiveCamera() failed for a node ValidatedSceneData already guarantees has a Camera");
  }

  return world;
}
```

Every `ATLANTIS_CHECK_MSG` here is a "should never happen" guard, not a
`Result` path — `ValidatedSceneData`'s own construction already
guarantees each precondition (D6, D2). `byIndex` is the transient
node-index-to-`EntityId` mapping — a local `std::vector`, never
returned, never a `World` member, gone the instant this function
returns (Spec 0015 Human Review Approval item 6).

### D10. Runtime — `initializeSteps()` resequencing

Replaces the existing `buildValidationScene()` call
(`runtime_application.cpp`) with:

```cpp
// (a) Read and validate the dependency manifest -- local, immutable resolver.
auto manifestResult = loadSceneDependencyManifest(config.sceneDependencyManifestPath);
if (manifestResult.isErr()) { /* log, markFailed(), Err(SceneManifestLoadFailed) */ }
const SceneDependencyResolver resolver = std::move(manifestResult.value());

// (b) Decode the scene artifact -- fully validated ValidatedSceneData.
auto sceneResult = decodeScene(config.sceneArtifactPath, config.sceneMetadataPath);
if (sceneResult.isErr()) { /* log, markFailed(), Err(SceneArtifactLoadFailed) */ }
const ValidatedSceneData scene = std::move(sceneResult.value());

// (c) Collect distinct AssetIds, ascending first-reference order (D2's
//     own AssetId-sorted resolver is irrelevant to *this* order --
//     this loop's own visitation order over scene's own nodes is what
//     the "first reference" guarantee actually depends on).
std::vector<AssetId> distinctIds;  // first-reference order, no duplicates
for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
  if (const auto& node = scene.node(i); node.renderable.has_value()) {
    const AssetId id = node.renderable->meshAsset;
    if (std::find(distinctIds.begin(), distinctIds.end(), id) == distinctIds.end()) distinctIds.push_back(id);
  }
}

// (d) Phase 1: resolve every one -- no I/O, no Entity yet.
std::vector<const SceneDependencyResolver::Entry*> resolved;
for (AssetId id : distinctIds) {
  const auto* entry = resolver.find(id);
  if (!entry) { /* log, markFailed(), Err(SceneDependencyUnresolved) */ }
  resolved.push_back(entry);
}

// (e) Phase 2: load, same order.
std::unordered_map<AssetId, atlantis::renderer::Mesh> meshResourceMap;  // keyed access only, never iterated in a load-order-sensitive way
for (std::size_t i = 0; i < distinctIds.size(); ++i) {
  auto meshResult = loadStaticMeshAsset(resolved[i]->artifactPath, resolved[i]->metadataPath);
  if (meshResult.isErr()) { /* log, markFailed(), Err(SceneDependencyLoadFailed) */ }
  auto createResult = createMesh(*device_, vertexInputLayout_, /* ... from meshResult.value() ... */);
  if (createResult.isErr()) { /* log, markFailed(), Err(SceneDependencyLoadFailed) */ }
  meshResourceMap.emplace(distinctIds[i], std::move(createResult.value()));
}

// (f) Instantiate -- infallible.
World world = atlantis::world::fromValidatedSceneData(scene);

// (g) Publish -- only now, both fully built.
world_ = std::move(world);
meshResourceMap_ = std::move(meshResourceMap);
```

**Every early-return branch above happens before (f)/(g)** — no
partial `world_`/`meshResourceMap_` assignment on any failure path; the
locally-scoped `world`/`meshResourceMap` values are simply destroyed by
ordinary C++ scope exit on return, no explicit rollback code (D9's own
`fromValidatedSceneData()` itself never observes a failure, per D6/D9).
`meshResourceMap_` (a new `RuntimeApplication` member, `std::unordered_map<AssetId,
Mesh>` — keyed lookup only, its own iteration order is never load-
order-relevant since loading already finished before this map exists)
replaces the single `mesh_` member; `runFrame()`'s own per-`DrawItem`
loop resolves each `Renderable`'s `AssetId` against it (`resolveMeshAsset()`'s
existing per-`AssetId` check, `scene_extraction.h`, now querying a map
instead of one hard-coded comparison — this is the one small, disclosed
edit to already-`Accepted` per-frame code this Plan makes, not a new
translation layer).

### D11. First scene asset — exact authored content

`assets/scenes/world_scene.scene.txt` reproduces
`buildValidationScene()`'s own six entities byte-for-byte (D3's own
literal example above *is* this content — Entities A–E map to
`node_id` 1–5 in declaration order, camera to `node_id` 6, D attached to
C via `parent=3`, matching `runtime_application.cpp`'s own
`world.setParent(*d, *c)` exactly): positions `(-2.5,0,0)`,
`(-1.0,0,0)`, `(1.0,-0.5,0)`, `(0,1.3,0)`, `(2.5,0,0)`; rotations
`(0,0,0)`, `(0,0.5236,0)`, `(0,0,0)`, `(0,0.7854,0)`,
`(0.2618,0.3491,0)`; camera at `(0,2.2,7.0)`, rotation `(-0.3054,0,0)`,
`fov_y=1.0472` (60° in radians), `near_z=0.1`, `far_z=100.0` — every
value transcribed directly from `runtime_application.cpp`'s own
`buildValidationScene()`, not re-derived.

## Milestones / Task Breakdown

Each step leaves the repository configuring, building, and testing
cleanly (Debug and Release). Steps are ordered so every later step can
build directly on real, already-compiled, already-tested code from
earlier ones — matching Plan 0014's own established sequencing
convention.

1. **Asset System scene types and error enums** (D2's `scene_types.h`,
   `errors.h` additions, `validated_scene_data.h` — `ValidatedSceneData`'s
   own encapsulation contract, no cook/decode logic yet).
   `tests/asset_system/validated_scene_data_tests.cpp`: V11 (compile-fail
   negative test), construction/copy/move round-trip.
2. **Authoring parser** (D3's `scene_source.h`/`.cpp` —
   `parseSceneSource()`/`serializeSceneSource()`, `ParsedSceneSource`).
   `tests/asset_system/scene_source_tests.cpp`: V1 (round-trip),
   grammar acceptance/rejection.
3. **Scene cooker** (D4/D5's `cook_scene.h`/`.cpp`, `scene_artifact.h`/`.cpp`'s
   own `encodeSceneArtifact()`, `scene_metadata.h`/`.cpp`).
   `tests/asset_system/cook_scene_tests.cpp`: V2–V7, V9 (encode-side),
   V12 (determinism, atomic write).
4. **Scene decoder** (D6's `decode_scene.h`/`.cpp`, `scene_artifact.h`'s
   own `decodeSceneArtifact()`). `tests/asset_system/decode_scene_tests.cpp`:
   V8, V9 (decode-side), V10.
5. **CMake scene asset declaration** (D7 — `atlantis_add_scene_asset()`,
   the additive `LOGICAL_PATH` export line, `atlantis_asset_cooker`'s
   own `--kind=scene` mode). No new scene asset declared yet (that is
   Step 8) — verified via a test-only scene fixture declared in
   `tests/asset_system/CMakeLists.txt` alone. V13.
6. **`World::fromValidatedSceneData()`** (D9,
   `src/world/include/atlantis/world/scene_instantiation.h`/`.cpp`).
   `tests/world/scene_instantiation_tests.cpp`: V21.
7. **Runtime manifest loading** (D8's `scene_manifest.h`/`.cpp`,
   `RuntimeInitError`/`BootstrapConfig` additions).
   `tests/runtime/scene_manifest_tests.cpp`: V14–V19.
8. **Runtime `initializeSteps()` resequencing** (D10 — replaces
   `buildValidationScene()`; `meshResourceMap_` member;
   `resolveMeshAsset()`'s own small edit). `tests/runtime/`: V20, V22
   (GPU-independent portions).
9. **First scene asset authored, declared, and wired end to end** (D11;
   `assets/scenes/world_scene.scene.txt`; `assets/CMakeLists.txt`'s own
   `atlantis_add_scene_asset()` call; `main.cpp`'s own
   `BootstrapConfig` population; a new GPU-required headless test
   reusing the existing `world_scene` golden;
   `runtime_smoke_gpu_tests.cpp` extended for the loaded-scene path).
   V22 (GPU-required), V23, V24, V25.
10. **Full verification and documentation/registry closeout** — clean
    Debug and Release builds; `ctest -LE gpu` and `ctest -L gpu` both
    green; Vulkan Validation Layers grepped clean; module-boundary scan
    (V26); manual windowed verification (V24, genuine human — matching
    Spec 0014's own established, and only recently satisfied, standard);
    `specs/README.md`/`docs/project-blueprint.md`/`docs/architecture/module_boundaries.md`
    updated to record delivery.

Step 9 is this Plan's own **mandatory, separate-commit** step, matching
ADR-0042's "Initial baseline bootstrap" precedent — **but this Plan
does not capture a new golden**: the new headless test's own commit
still lands separately from Steps 1–8's own code, so a reviewer can
independently confirm "zero difference against the existing golden"
without that evidence being entangled with unrelated code changes in
the same diff.

## Files / Modules Touched (expected)

- **New**: `src/asset_system/include/atlantis/asset_system/{scene_types,validated_scene_data,scene_source,scene_artifact,scene_metadata,cook_scene,decode_scene}.h`
  and matching `.cpp` files under `src/asset_system/src/`.
- **New**: `src/world/include/atlantis/world/scene_instantiation.h`,
  `src/world/src/scene_instantiation.cpp`.
- **New**: `src/runtime/include/atlantis/runtime/scene_manifest.h`,
  `src/runtime/src/scene_manifest.cpp`.
- **New**: `assets/scenes/world_scene.scene.txt`.
- **New**: `tests/asset_system/{validated_scene_data,scene_source,cook_scene,decode_scene,scene_manifest}_tests.cpp`
  (the last one may instead live under `tests/runtime/` — Plan-level
  naming detail, resolved during Step 7 without changing this Plan's
  own scope).
- **New**: `tests/world/scene_instantiation_tests.cpp`.
- **New**: `tests/image_regression/world_scene_loaded_gpu_tests.cpp`
  (Step 9's own GPU-required test reusing the existing golden — exact
  file name a Plan-level detail).
- **Modified**: `src/asset_system/include/atlantis/asset_system/errors.h`
  (additive), `src/asset_system/CMakeLists.txt` (additive),
  `src/tools/asset_cooker/{cook_command.h,cook_command.cpp,main.cpp}`
  (additive third mode), `src/runtime/include/atlantis/runtime/{init_error.h,bootstrap_config.h}`
  (additive), `src/runtime/src/runtime_application.cpp` (D10's own
  resequencing — `buildValidationScene()` and its own call site
  removed, replaced), `src/runtime/src/main.cpp` (new `BootstrapConfig`
  fields populated from new CMake compile definitions),
  `src/runtime/include/atlantis/runtime/scene_extraction.h`/`.cpp`
  (`resolveMeshAsset()`'s own small map-lookup edit), `assets/CMakeLists.txt`
  (additive), `tests/asset_system/CMakeLists.txt`, `tests/world/CMakeLists.txt`,
  `tests/runtime/CMakeLists.txt`, `tests/image_regression/CMakeLists.txt`
  (each additive — new test files registered).
- **Explicitly untouched**: `src/rhi/`, `src/renderer/`, `src/render_graph/`,
  `src/vulkan_backend/`, `src/platform/`, `src/shader_system/`, every
  existing `src/asset_system/` file beyond the two additive edits above,
  every existing `src/world/` file (D1's own table — `scene_instantiation.h`
  is new, `world.h`/`world.cpp`/`world_error.h`/`entity_id.h` unmodified),
  `tests/image_regression/goldens/world_scene/` (no new or modified
  golden — Spec 0015's own explicit requirement), `tests/image_regression/fixture/world_scene_fixture.cpp`
  (Spec 0015 Human Review Decision Table item 11 — the existing hand-
  authored-fixture path is re-run unmodified, not migrated).

## Sequencing & Dependencies

Steps 1→2→3 are strictly sequential (types before parser before
cooker, since the cooker calls the parser). Step 4 depends only on
Step 1 (the decoder does not call the parser or the cooker — it is an
independent re-implementation, D6's own explicit design) and may be
developed in parallel with Steps 2–3 if Implementation prefers,
though this Plan does not require parallelism. Step 5 depends on Step 3
(the cooker binary's own `--kind=scene` mode must exist before CMake
can invoke it) and on Step 1 (the `LOGICAL_PATH` export is independent
mechanical CMake work, no code dependency). Step 6 depends only on
Step 1 (`ValidatedSceneData`'s own public accessor surface). Step 7
depends on Step 1 (`AssetId`, already available) — genuinely
independent of Steps 2–6. Step 8 depends on Steps 4, 6, 7 (decoder,
`World` instantiation, manifest resolver — all three are composed
here). Step 9 depends on **all** of Steps 1–8 (the first real scene
asset exercises the complete pipeline end to end) and is this Plan's
own mandatory separate commit. Step 10 depends on Step 9.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | Authoring-source round-trip: parse → serialize → parse reproduces the exact same `ParsedSceneSource`; strict grammar rejects a malformed line the same way `mesh_source.h`'s own tests already establish. | `scene_source_tests.cpp` | GPU-independent |
| V2 | Cook-time: a duplicate `node_id` is rejected with `Err(SceneCookError::DuplicateNodeId)`, no artifact written. | `cook_scene_tests.cpp` | GPU-independent |
| V3 | Cook-time: a parent naming an undeclared `node_id` is rejected with `Err(UndeclaredParentReference)`. | `cook_scene_tests.cpp` | GPU-independent |
| V4 | Cook-time: a direct self-parent and a multi-hop cycle are each rejected with `Err(ParentCycle)` — matching Spec 0014's own V5 self/2-hop/4-hop coverage shape. | `cook_scene_tests.cpp` | GPU-independent |
| V5 | Cook-time: `active_camera` naming an undeclared `node_id` is rejected with `Err(UndeclaredActiveCameraReference)`. | `cook_scene_tests.cpp` | GPU-independent |
| V6 | Cook-time: `active_camera` naming a node with no `camera_*` fields is rejected with `Err(ActiveCameraMissingCamera)`. | `cook_scene_tests.cpp` | GPU-independent |
| V7 | Cook-time: a non-finite (`NaN`/`Inf`) authored float in any field is rejected with `Err(NonFiniteValue)`. | `cook_scene_tests.cpp` | GPU-independent |
| V8 | Artifact encode/decode round-trip: cooking a known-good scene then decoding it reproduces the exact same node data (position/rotation/scale/camera/renderable/parent/active-camera, every field). | `decode_scene_tests.cpp` | GPU-independent |
| V9 | Every `SceneArtifactDecodeError` condition individually triggered and correctly, distinctly reported: bad magic, unknown schema version, truncated (`TooSmallForHeader`), a corrupted/inconsistent size, an out-of-range parent index, a decode-time-injected cycle, an out-of-range active-camera index, a decode-time-injected active-camera-missing-Camera case, a non-finite value, a metadata parse failure, a metadata/artifact mismatch. | `decode_scene_tests.cpp` | GPU-independent |
| V10 | An artifact whose own declared `node_count` is implausibly large (mirroring `VertexCountOutOfRange`'s own precedent) is rejected via `NodeCountOutOfRange` before any allocation proportional to it. | `decode_scene_tests.cpp` | GPU-independent |
| V11 | `ValidatedSceneData`'s own unforgeability: a compile-fail negative test (documented, not built, matching `EntityId`'s own V27 precedent) confirming no external code can name the non-default constructor, assign to any field, or obtain a mutable reference; copy/move preserve every accessor's own observed value. | `validated_scene_data_tests.cpp` | GPU-independent (mix of runtime and compile-time) |
| V12 | Cooker determinism and atomic writes: cooking the same source twice produces byte-identical artifact and metadata bytes; a forced mid-write failure leaves no partial output file and does not disturb a pre-existing valid one — mirroring `cookStaticMesh()`'s own already-`Accepted` V11 (Spec 0012/Plan 0012) test shape exactly. | `cook_scene_tests.cpp` | GPU-independent |
| V13 | CMake re-import triggering: editing the scene source, the cooker, or a declared `MESH_DEPENDENCIES` target re-cooks the scene on the next build; editing an unrelated, undeclared asset does not. | Manual, recorded (matching Plan 0012 Section D7's own established procedure) | Manual |
| V14 | Manifest: a duplicate logical-path entry is rejected via `Err(SceneManifestError::DuplicateLogicalPath)`. | `scene_manifest_tests.cpp` | GPU-independent |
| V15 | Manifest: two distinct logical paths engineered (in the test) to hash to the same `AssetId` are rejected via `Err(AssetIdCollision)`. | `scene_manifest_tests.cpp` | GPU-independent |
| V16 | Manifest: an entry whose own metadata sidecar's `assetId` does not match the manifest-computed `AssetId` is rejected via `Err(MetadataArtifactMismatch)`. | `scene_manifest_tests.cpp` | GPU-independent |
| V17 | Manifest: a scene reference naming an `AssetId` with no manifest entry fails via `Err(RuntimeInitError::SceneDependencyUnresolved)`, confirmed to occur **before** any `Entity` exists. | `scene_manifest_tests.cpp` or a dedicated Runtime-level test | GPU-independent |
| V18 | Manifest: a declared `MESH_DEPENDENCIES` entry the scene never references does **not** fail the load — the resolver builds successfully, and only referenced entries are ever resolved/loaded. | `scene_manifest_tests.cpp` | GPU-independent |
| V19 | Deterministic load order: repeated runs against the same scene produce an identical mesh-load sequence, confirmed via an instrumented resolver counting call order — never dependent on `std::unordered_map` iteration (the resolver itself is `AssetId`-sorted-vector-backed, D8). | A dedicated Runtime-level test | GPU-independent |
| V20 | Transactional failure at every stage: a scene that fails cook-time validation never produces an artifact; a corrupted artifact never produces a `ValidatedSceneData`; an unresolved or failed-to-load mesh dependency never reaches `World::fromValidatedSceneData()` and leaves `RuntimeApplication` with neither a populated `world_` nor a populated `meshResourceMap_` — confirmed directly by inspecting both members, not inferred from a process exit code. | A dedicated Runtime-level test (GPU-independent, injecting a bad manifest/artifact path) | GPU-independent |
| V21 | `World::fromValidatedSceneData()`: a hand-constructed `ValidatedSceneData` (via the test's own `friend`-equivalent access, matching `EntityLifecycleTestAccess`'s own established pattern) produces a `World` whose entity count, hierarchy (`getParent()`), `Transform`, `Camera`, `Renderable`, and `activeCamera()` all match exactly; deterministic instantiation order confirmed via repeated runs producing identical `EntityId` sequences (matching Spec 0014's own V14 shape); `static_assert` confirming the function's own return type is `World`, not a `Result`. | `scene_instantiation_tests.cpp` | GPU-independent (mix of runtime and compile-time) |
| V22 | The loaded scene's CPU-side `World` state (entity count, every `Transform`/`Camera`/`Renderable` value, hierarchy, active camera) is identical, field-for-field, to `buildValidationScene()`'s own hand-built `World` state — confirmed by a direct comparison test, GPU-independent portion first (structural equivalence), then re-confirmed by the GPU-required golden compare (V23) as the rendering-level proof. | A dedicated Runtime-level test, then `world_scene_loaded_gpu_tests.cpp` | GPU-independent + `gpu`-labeled |
| V23 | Real GPU, headless: the loaded scene asset, rendered through the existing, unmodified extraction/`Renderer` pipeline, matches the existing, already checked-in `world_scene` golden with **zero** channel difference. The existing golden's own hand-authored-fixture test (`world_scene_gpu_tests.cpp`) is re-run unmodified in the same suite, proving this Plan's own new path did not disturb it — the same cross-check `minimal_cube`'s own Asset-System-sourced precedent already established. | `world_scene_loaded_gpu_tests.cpp`; `world_scene_gpu_tests.cpp` (existing, re-run) | `gpu`-labeled |
| V24 | Manual windowed: the real `atlantis_runtime` executable, launched with the new scene-asset configuration, shows the same five distinct, correctly-shaded, depth-ordered cubes at their D9 positions as Spec 0014's own already-verified windowed check; interactive resize/minimize/restore/close all behave correctly — an actual human, using a real graphical session, confirms this directly (matching Spec 0014's own V20 requirement and its own recorded 2026-08-23 PASS precedent exactly — never satisfied by programmatic Win32 automation alone). | Manual | Manual |
| V25 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green on both configurations; Vulkan Validation Layers grepped clean (not merely inferred from exit status) on every GPU-touching path, including the new loaded-scene test. | Both configurations | Manual, recorded |
| V26 | Module boundary / forbidden-dependency scan: `tests/asset_system/module_boundary_tests.cpp`'s own existing scan confirms `src/asset_system/` still names no `atlantis/world/` header (direct, automated proof ADR-0052's own cycle-avoidance Decision holds); `tests/world/module_boundary_tests.cpp`'s own existing scan confirms `src/world/` still names no RHI/Renderer/RenderGraph/ShaderSystem/Platform/VulkanBackend/Runtime header; `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, `src/shader_system/`, `shaders/`, or `tests/image_regression/goldens/` was modified; `CMakeLists.txt`/`vcpkg`-equivalent dependency list confirms no new third-party dependency was added. | `module_boundary_tests.cpp` (both), manual `git diff --stat` review | GPU-independent + Manual |

## Rollback Plan

Every file this Plan touches is either new (delete on revert) or
additively extended (the two `src/asset_system/CMakeLists.txt` edits,
`errors.h`, `init_error.h`, `bootstrap_config.h`, `scene_extraction.h`/`.cpp`'s
own small map-lookup edit, and `runtime_application.cpp`'s own
`initializeSteps()` resequencing). Reverting this Plan's own merge
commit cleanly restores `buildValidationScene()` and every prior
behavior — no other module's public API is touched, so no other
module needs a coordinated revert. The existing `world_scene` golden is
never modified by this Plan, so no golden-provenance rollback concern
exists (unlike a Plan that captured a new golden).

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- V1–V26 all executed and recorded; V13, V24, V25 (partially) recorded
  as manual verification in the Implementation PR — V24 specifically
  requires genuine human observation through a real graphical session,
  matching Spec 0014's own established, hard-won standard; programmatic
  automation is not, and must never be recorded as, a substitute.
- The existing `minimal_cube` and `world_scene` goldens under
  `tests/image_regression/goldens/` are confirmed **unchanged** in the
  final diff — no new golden is captured by this Plan at all.
- `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`,
  `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, or
  `src/shader_system/` was modified.
- `tests/asset_system/module_boundary_tests.cpp` and
  `tests/world/module_boundary_tests.cpp` both pass unmodified,
  confirming no new forbidden dependency edge exists.

## Independent Review (self-review, 2026-08-23)

Performed during drafting, against `main`'s actual, current source tree
(not the Spec/ADRs' own illustrative prose alone), for every file this
Plan touches or extends:

- **`Result<T, E>` shape confirmed** (`src/core/include/atlantis/result.h`):
  `Ok(T)`/`Err(E)`, `isOk()`/`isErr()`, `value()`/`error()` — every
  `Result`-returning signature in this Plan's own D-sections matches
  this exactly; no `Result<void, E>` anywhere (this codebase's own
  established `Result<std::monostate, E>` convention, per Spec 0014's
  own disclosed deviation, applies identically here where a `Result`
  carries no success payload).
- **`cook_command.cpp`'s own real implementation read in full**, not
  assumed from `cook_command.h`'s own comments alone — confirmed the
  logical path passed to `cookStaticMesh()` retains the `.mesh.txt`
  authoring extension (only the *output-filename* `base` variable has
  it stripped); D3's own authoring-grammar example and D7's own
  manifest design are written to match this exactly, not a plausible-
  looking guess.
- **`Atlantis::World`'s own existing `PUBLIC` link to `Atlantis::AssetSystem`
  confirmed** (`src/world/CMakeLists.txt`) — `scene_instantiation.h`
  naming `ValidatedSceneData` needs no `target_link_libraries()` change,
  stated as fact in D1, not assumed.
- **`AssetMetadata`'s own real field shape confirmed** — mesh-specific
  (`vertexCount`/`indexCount`/`vertexStrideBytes`); the new
  `SceneMetadata` (D5) is a distinct, scene-specific struct, not a
  forced reuse of a mesh-shaped one, while still reusing
  `MetadataParseError`'s own generic-shaped enum for the parse
  discipline itself (D2's own explicit reasoning for why one is reused
  and the other is not).
- **No Spec/ADR content re-litigated.** Every D-section above supplies
  a concrete shape for something Spec 0015/ADR-0052–0054 explicitly
  left as "a Plan-level detail" — none contradicts, narrows, or
  silently reinterprets a Human-Review-Approved decision. No
  architectural blocker was found; nothing here required stopping to
  raise an objection.

## Deviations, objections, and open mechanical details

**No `Accepted`/`Approved` decision in Spec 0015 or ADR-0052–0054 was
found to be unimplementable against the real, current source tree.**
Two genuinely open mechanical details, appropriately left to
Implementation, neither architectural:

1. **The exact scene metadata sidecar's own extra fields** (beyond
   `schema_version`/`node_count`) — D5 fixes the minimum; Implementation
   may add a content hash if it proves useful during Step 3, without
   revisiting this Plan.
2. **Whether `scene_manifest_tests.cpp` (V17) lives under
   `tests/asset_system/` or `tests/runtime/`** — a file-location detail
   with no design content (`Files / Modules Touched` already discloses
   this as undecided).

This Plan's own status remains `In Review` — the mechanical details
above, and any remaining C++/CMake naming choice not already fixed in
the D-sections, are the only items left for Human Review; no
architectural question is open.
