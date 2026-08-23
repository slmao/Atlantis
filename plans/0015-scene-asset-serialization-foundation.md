# Plan: Scene Asset & Serialization Foundation

- **Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)
  (`Approved`, Human Review Approval recorded 2026-08-23, accepting all
  16 items of that Spec's own Human Review Decision Table — see that
  Spec's own approval note for the full record)
- **Status:** `Approved / Ready for Implementation`. See "Human Review
  Approval" below for the full record.
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Reviewed and approved by a human — see Human
  Review Approval immediately below; the two "Independent Review"
  sections further below are the self-review record that preceded and
  fed that approval, not a substitute for it.
- **Human Review Approval (2026-08-23):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer) on 2026-08-23, accepting this Plan in full as revised
  through both Independent Review rounds below (the second of which
  found and fixed a real compile error, a misleading load-order
  description, an over-triggering CMake dependency, and completed a
  switch-exhaustiveness inventory). This approval explicitly covers:

  1. **The full ten-step Milestones / Task Breakdown**, its own
     Sequencing & Dependencies, and the Files / Modules Touched scope —
     new files under `src/asset_system/`, `src/world/`, `src/runtime/`,
     `assets/scenes/`, and their matching `tests/`; additive-only edits
     to `errors.h`, `init_error.h`, `bootstrap_config.h`,
     `runtime_application.h`/`.cpp`, `main.cpp`, `scene_extraction.h`/`.cpp`,
     `cook_command.h`/`.cpp`/`main.cpp`, and the CMake files listed —
     with `src/rhi/`, `src/renderer/`, `src/render_graph/`,
     `src/vulkan_backend/`, `src/platform/`, `src/shader_system/`, and
     both existing goldens confirmed untouched.
  2. **The authoring/artifact/manifest format in full** — the strict,
     versioned, `.scene.txt` authoring grammar (D3); the versioned,
     unconditionally little-endian binary artifact and its metadata
     sidecar (D5); the per-scene, build-tree-private dependency
     manifest as an explicit (logical path, `AssetId`, artifact
     locator) triple, its own duplicate/collision/metadata-mismatch/
     malformed-entry validation, and its confirmed exclusion from the
     portable scene artifact (D7/D8).
  3. **`ValidatedSceneData`'s own corrected construction contract** —
     no public default constructor of any kind (`= delete`d), the
     private, `decodeScene()`-only non-default constructor, private
     storage, read-only accessors only, copy/move that only ever
     duplicates or relocates an already-validated instance, and a
     zero-node scene rejected as an explicit, named error at both cook
     time and decode time (`EmptyScene`) rather than accepted as a
     vacuously-valid empty instance — matching
     [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)'s
     own Human Review Correction (2026-08-23) exactly (D2, D4, D6; V11,
     V28).
  4. **`World::fromValidatedSceneData()`'s own two-pass, infallible
     instantiation** — no `Result`, no new `WorldError` enumerator, a
     transient decoded-index-to-`EntityId` mapping that exists only for
     the duration of the call, and explicit `ATLANTIS_CHECK_MSG`
     invariant guards (not silent tolerance) on every internal call
     whose own precondition `ValidatedSceneData` already guarantees
     (D9; V21).
  5. **Runtime's own transactional publish and ownership contract** —
     mesh-dependency resolution and loading strictly before any
     `Entity` is created; the resolver as a point-lookup structure only,
     with the sole load-order guarantee coming from
     `ValidatedSceneData`'s own first-reference node order, never
     `AssetId`-numeric or hash-iteration order; `world_` retyped to
     `std::optional<World>` and published via `world_.emplace()` — an
     in-place move-*construction*, never a move-assignment `World`
     itself does not have; `meshResourceMap_` replacing `mesh_` in its
     exact former declaration slot, preserving the file's own existing
     reverse-destruction-order guarantee (every GPU `Mesh` destroyed
     before `Presentation`/`Device`); no partial `world_`/`meshResourceMap_`
     mutation on any failure path, and no explicit rollback code (D10;
     V17–V22).
  6. **CMake trigger semantics, precisely differentiated** — a scene or
     cooker edit re-cooks the scene; a declared mesh dependency's own
     *content* edit does not (ordering via `add_dependencies()`,
     deliberately excluded from the custom command's own `DEPENDS`); a
     `MESH_DEPENDENCIES` list edit regenerates the manifest on the next
     ordinary CMake reconfigure; an unrelated asset never triggers
     anything here (D7; V13).
  7. **The full V1–V28 Verification Checklist**, including V19's own
     deliberate first-reference-vs-`AssetId`-order regression test, V27's
     own switch-exhaustiveness positive/negative build check (with one
     new, disclosed `/w14062` on `atlantis_asset_cooker_lib`), and V28's
     own cook-time/decode-time `EmptyScene` rejection.

  This approval does not authorize Implementation to begin immediately
  — per this repository's own PR-based workflow, this Plan's own PR
  must be merged first; see [specs/README.md](../specs/README.md).

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

// ADR-0053 D4 / its own Human Review Correction (2026-08-23); Spec 0015
// Human Review Approval item 3: encapsulated so "fully validated" is a
// type-level guarantee, not a caller convention. Every structural field
// private; the only non-default constructor private, callable only by
// decodeScene() (a named friend function, matching EntityId's own
// friend class World); public surface is read-only accessors only.
// NO public default constructor -- deleted, not merely omitted, so a
// caller's own attempt to default-construct one is a named compiler
// error, not a puzzling "no viable constructor" message. A zero-node
// scene is not representable by this type at all; decodeScene() itself
// never succeeds with one (D6's own EmptyScene check) -- see this
// Plan's own V11/V28.
class ValidatedSceneData {
 public:
  ValidatedSceneData() = delete;

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
    EmptyScene,               // node_count == 0 -- ADR-0053's own Human Review Correction (2026-08-23)
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
    EmptyScene,               // node_count == 0 -- re-checked independently, never trusting the cooker
    NodeCountOutOfRange,       // node_count implausibly large -- the separate, pre-existing upper-bound guard
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
  directly" requirement. **`RuntimeInitError::toString()`'s own
  existing switch** (`src/runtime/src/init_error.cpp`) already has no
  `default:` case (confirmed by reading the real file: a trailing
  `ATLANTIS_CHECK_MSG(false, ...)` fallback after the switch, matching
  `exit_reason.cpp`'s own identical, already-`Accepted` idiom) and
  already compiles under `atlantis_runtime_host`'s own existing,
  target-scoped `/w14062` (`src/runtime/CMakeLists.txt` line 53–55,
  confirmed by reading the real file — Plan 0013's own 2026-08-21
  amendment). Adding these four cases to that same switch is
  automatically covered by that same, already-present protection — no
  new CMake change is needed for `RuntimeInitError`.
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
    // A sorted std::vector<std::pair<AssetId, Entry>>, looked up via
    // std::lower_bound() -- never std::unordered_map -- so no code
    // path in this Plan ever exposes hash-iteration order anywhere,
    // even by accident. **This choice is about lookup-container
    // hygiene only; it does NOT establish load order.** find() is a
    // point query -- this struct is never iterated end-to-end by any
    // caller. The load-bearing load-order guarantee (Spec 0015 Human
    // Review Approval item 10; ADR-0054's own Decision item 3) comes
    // entirely from D10's own distinctIds collection, built by walking
    // ValidatedSceneData's own node array in ascending index order --
    // a property of D10's own loop, independent of how this resolver
    // itself happens to be stored. Needs no third-party dependency.
    struct Entry {
      std::string artifactPath;
      std::string metadataPath;
    };
    std::vector<std::pair<atlantis::asset_system::AssetId, Entry>> entries;  // sorted by AssetId -- for lookup speed only

    [[nodiscard]] const Entry* find(atlantis::asset_system::AssetId id) const noexcept;
  };

  [[nodiscard]] atlantis::Result<SceneDependencyResolver, SceneManifestError> loadSceneDependencyManifest(
      const std::string& manifestPath);

  // For logging only, matching init_error.h's own toString()
  // precedent exactly (D2's own switch-exhaustiveness inventory,
  // below).
  [[nodiscard]] const char* toString(SceneManifestError error) noexcept;

  }  // namespace atlantis::runtime
  ```
- **Switch-exhaustiveness inventory (every new enum, every consuming
  switch, exact protection)** — enumerated explicitly rather than
  assumed, per this Plan's own Independent Review:

  | Enum | Consuming switch | Target | `/w14062` already present? |
  |---|---|---|---|
  | `RuntimeInitError` (4 new cases) | `toString()`, `init_error.cpp` (existing) | `atlantis_runtime_host` | **Yes** — confirmed real (`src/runtime/CMakeLists.txt` line 53–55). No CMake change needed. |
  | `SceneManifestError` | `toString()`, `scene_manifest.cpp` (new) | `atlantis_runtime_host` | **Yes**, same target as above — this new file compiles under the same, already-present flag. No CMake change needed. |
  | `SceneCookError` | `sceneCookErrorMessage()`, `cook_command.cpp` (new, mirroring the existing `cookErrorMessage()`/`assetSetErrorMessage()` — confirmed real, both already no-`default:` switches, `cook_command.cpp` lines 46–74) | `atlantis_asset_cooker_lib` | **No** — confirmed real (`src/tools/asset_cooker/CMakeLists.txt`, no `/w14062` anywhere). **New, disclosed addition below.** |
  | `SceneArtifactDecodeError` | **None.** Runtime logs `decodeScene()` failure generically (`ATLANTIS_LOG_ERROR("decodeScene() failed")`, no per-case string) — matching `loadStaticMeshAsset()`'s own existing precedent for `AssetLoadError` exactly (confirmed real: `initializeSteps()`'s existing Step 4 logs `"loadStaticMeshAsset() failed"` with no per-`AssetLoadError`-case string anywhere). No switch exists for this enum in production code; nothing to protect. `decode_scene_tests.cpp`'s own assertions compare enumerator values directly (`==`), which needs no exhaustiveness protection at all. | — | N/A |

  **New CMake addition** (`src/tools/asset_cooker/CMakeLists.txt`,
  matching `src/runtime/CMakeLists.txt`'s own exact precedent):

  ```cmake
  # Plan 0015: real, compile-time enum-exhaustiveness protection for
  # sceneCookErrorMessage()'s own no-default switch requires MSVC's
  # C4062, off by default even at /W4 -- matching Plan 0013's own
  # already-established src/runtime/CMakeLists.txt precedent exactly.
  # Scoped to this target only; cmake/CompilerWarnings.cmake is
  # deliberately not touched.
  target_compile_options(atlantis_asset_cooker_lib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/w14062>
  )
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
- **`ValidatedSceneData` has no public default constructor of any
  kind — settled by ADR-0053's own "Human Review Correction
  (2026-08-23)", not merely this Plan's own reading.** An earlier round
  of this Plan's own drafting found a wording gap between ADR-0053's
  own original Decision text (which had briefly kept a trivial `public`
  default constructor for an empty-scene case) and Spec 0015's own
  Human Review Approval summary (item 3, "no public default/arbitrary
  construction"); Human Review has since resolved it directly, in
  ADR-0053 itself, in the "no public default construction" direction —
  see that ADR's own Correction for the full record. This Plan's own
  D2 above (`ValidatedSceneData() = delete;`) already reflects the
  corrected design; a zero-node scene is now an explicit,
  named cook-time and decode-time error (D4/D6, `EmptyScene`), not a
  vacuously-valid empty instance.
- **`RuntimeApplication`'s own real member list — confirmed against
  `src/runtime/include/atlantis/runtime/runtime_application.h`, not
  assumed.** `world_` is declared today as a **bare** `atlantis::world::World
  world_;` (default-constructed for free by `RuntimeApplication() =
  default;`). `World` is move-constructible but **not**
  move-assignable (`World& operator=(World&&) = delete;`,
  ADR-0049/Spec 0014) — a bare member cannot be replaced by a freshly-
  instantiated `World` without move-assignment, which does not exist.
  **This Plan changes `world_`'s own declared type to
  `std::optional<atlantis::world::World>`** — matching the exact
  pattern `mesh_`/`material_` already use for the identical "empty
  until a one-time-successful init step populates it" shape (both
  already `std::optional`, confirmed real) — and publishes via
  `world_.emplace(std::move(world))` (in-place move-*construction*,
  never assignment; see D10 for the full call site and destruction-
  order reasoning). This is a disclosed, necessary, minimal signature
  change to an *already-existing* member — not a new type.

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
2. **Empty scene**: `node_count == 0` is rejected outright
   (`EmptyScene`) — checked immediately after a successful parse, before
   any of the per-node validation below runs, matching
   [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)'s
   own Human Review Correction (2026-08-23): `ValidatedSceneData` has no
   public default constructor, so `decodeScene()` must never succeed
   with zero nodes, and this is where that guarantee originates.
3. **Duplicate `node_id`**: a `std::vector<std::uint32_t>` of seen IDs,
   sorted and checked for adjacency after `std::sort` — `O(n log n)`,
   sufficient at this Plan's own scale (`DuplicateNodeId`).
4. **Undeclared parent**: every non-`none` parent `node_id` must appear
   in the declared set (`UndeclaredParentReference`).
5. **Parent cycle**: walk each node's own ancestor chain (via the
   authoring-time `node_id`-keyed parent map, not an array index — this
   is genuinely a second, small, independent implementation of the same
   *algorithm* `World::setParent()`'s own ancestor walk already uses,
   applied to authoring-time data rather than a live slot map,
   consistent with this repository's own "duplicated, not shared"
   precedent) (`ParentCycle`).
6. **Active camera reference**: `active_camera`'s own `node_id` must be
   declared (`UndeclaredActiveCameraReference`) and that node must
   carry a `camera_*` triple (`ActiveCameraMissingCamera`).
7. **Non-finite values**: every authored float (`position`/`rotation`/
   `scale`/`camera_*`) checked via `std::isfinite()` (`NonFiniteValue`).
8. **Mesh reference resolution**: for each node with a `mesh=` field,
   `normalizeLogicalPath()` + `computeAssetId()` over the authored
   string — the *existing*, already-`Accepted` mesh pipeline's own
   pure functions, unchanged. (A malformed logical path here surfaces
   through `normalizeLogicalPath()`'s own existing `LogicalPathError`,
   folded into `SourceParseFailed` for this enum's own granularity —
   matching how `cookStaticMesh()`'s own `CookError::LogicalPathInvalid`
   is already a single, coarse case.)
9. **Dense remapping**: build a `node_id → array index` map in
   declaration order (the order `parseSceneSource()`'s own node array
   already has); rewrite every parent/active-camera reference from
   `node_id` to array index.
10. **Encode** (D5) and **write atomically** — write-to-temp-then-rename
    in `artifactOutputPath`'s/`metadataOutputPath`'s own directory,
    identical to `cookStaticMesh()`'s own established pattern
    (`ArtifactWriteFailed`/`MetadataWriteFailed`).

**Determinism**: no filesystem timestamp, no non-deterministic
iteration (steps 3–4 sort explicitly; step 9's remapping is declaration-
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
3. **Empty scene, re-checked independently**: `node_count == 0` is
   rejected (`EmptyScene`) — **never trusting that the cooker already
   enforced this** (D4's own step 2), matching this function's own
   stated philosophy for every other condition below. This is the
   check that makes `ValidatedSceneData`'s own lack of a public default
   constructor sound in practice, not merely in the type's own API
   shape: no code path through `decodeScene()` can ever reach the
   private constructor (step 9 below) with an empty node vector.
4. **`node_count` upper-bound check** before allocating the node vector
   (`NodeCountOutOfRange`) — mirroring `ArtifactDecodeError::VertexCountOutOfRange`'s
   own precedent exactly: never trust a declared count enough to
   allocate on its word alone. A distinct condition from step 3 above
   (zero vs. implausibly large), each with its own enumerator, per
   [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)'s
   own Human Review Correction.
5. Decode every per-node record; for each: `has_parent` → `parent_index
   < node_count` (`OutOfRangeParentIndex`); every `float` field
   `std::isfinite()` (`NonFiniteValue`).
6. **Cycle re-check**: walk each node's own parent-index chain (array-
   index-based this time, not `node_id`-based — a second, independent
   algorithm instance from D4's own, per this repository's own
   "duplicated, not shared" precedent) (`CyclicParent`).
7. `has_active_camera` → `active_camera_index < node_count`
   (`OutOfRangeActiveCameraIndex`) and that node's own `has_camera`
   flag is set (`ActiveCameraMissingCamera`).
8. Parse the metadata sidecar via the existing `parseAssetMetadata()`-
   sibling strict-text discipline, reusing `MetadataParseError`
   (`MetadataParseFailed`); cross-check its own `node_count` against
   the artifact's own header (`MetadataArtifactMismatch`).
9. On success: construct `ValidatedSceneData` via its own `private`
   constructor (this is the **only** call site in the entire codebase
   permitted to do so, enforced by the `friend` declaration, D2) —
   reachable only once every check above, including step 3's own
   non-empty guarantee, has already passed.

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
    DEPENDS ${source_path} atlantis_asset_cooker
    COMMENT "Cooking scene asset: ${ARG_SOURCE}"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  # Ordering only, deliberately NOT added to the custom command's own
  # DEPENDS above -- see "Rebuild scoping" immediately below for why
  # these are two different things in CMake and why conflating them
  # would over-trigger.
  if(dependency_targets)
    add_dependencies(${ARG_NAME}_asset ${dependency_targets})
  endif()

  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_MANIFEST_PATH "${manifest_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
endfunction()
```

**Rebuild scoping — trigger semantics, precise, not conflated:**

- **Scene source or the cooker itself changes** → the custom
  *command*'s own `DEPENDS` (`${source_path}`, `atlantis_asset_cooker`
  only) goes stale → the scene re-cooks. Ordinary CMake staleness
  checking, identical to `atlantis_add_static_mesh_asset()`'s own
  already-working mechanism.
- **A declared `MESH_DEPENDENCIES` mesh's own source or cooker
  changes** → that mesh's own, separate `atlantis_add_static_mesh_asset()`-
  declared custom command re-cooks *that mesh* — **the scene's own
  cook step does not re-run**, because the mesh's own target is
  attached via `add_dependencies()` on the *target* (ordering only:
  "build the mesh before/alongside this scene, as part of the same
  `ALL` graph") — never via the custom *command*'s own `DEPENDS` (which
  is what CMake actually checks for staleness). **This is deliberate,
  not an oversight**: the scene artifact only ever stores a mesh's
  `AssetId`, itself a hash of that mesh's own **logical path**, which
  does not change when the mesh's own *content* changes and is already
  known at CMake-declare time — so the scene artifact's own output
  bytes provably cannot change from a mesh content edit, and this
  design avoids the wasted, misleading-if-undisclosed re-cook a
  cruder `DEPENDS`-based edge would have caused. The `add_dependencies()`
  edge still exists so a full build graph is consistent (the mesh
  builds as part of the same `ALL` pass) — it just does not, by
  itself, mark the scene's own stamp stale.
- **A `MESH_DEPENDENCIES` mesh's own logical path changes** (renamed
  in its own `atlantis_add_static_mesh_asset(... SOURCE ...)` call) —
  this edits `assets/CMakeLists.txt` itself, which CMake always
  reconfigures on (a plain, standard CMake guarantee, not a mechanism
  this Plan invents); reconfiguring re-runs `atlantis_add_scene_asset()`
  and therefore regenerates this scene's own manifest (`file(GENERATE)`)
  with the new logical path — the scene's own *cooked artifact* is
  unaffected (it stores an `AssetId`, computed independently at that
  mesh's own cook time from its own then-current logical path; if the
  path changed, that mesh's own `AssetId` changed too, and the *scene
  authoring source*, unchanged here, still names the *old* path unless
  a human also edits it — an already-covered, ordinary "unresolved
  `AssetId`" condition at Runtime load time, not a new case).
- **The `MESH_DEPENDENCIES` list itself changes** (a dependency added
  or removed in `assets/CMakeLists.txt`) — same reconfigure-on-CMakeLists-edit
  guarantee as above; the manifest is regenerated with the new
  dependency set on the next configure, no special mechanism needed.
- **An unrelated, undeclared asset or scene changes** — never appears
  in this scene's own `DEPENDS` or `add_dependencies()` list at all;
  never triggers anything here (V13).

A `MESH_DEPENDENCIES` entry the scene's own authoring source never
actually references still contributes a manifest line and an ordering
edge (harmless — matching Spec 0015's own explicit "not an error"
decision, item 9) but costs nothing beyond one unused resolver-map
entry; it is never resolved or loaded (D10's own resolve phase only
ever looks up references the scene actually has).

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
path>\t<metadata path>`. **Tab-separation is safe by construction, not
merely by convention**: the logical-path field can never itself
contain a tab or newline — `normalizeLogicalPath()`'s own existing,
already-`Accepted` character whitelist
(`LogicalPathError::DisallowedCharacter`, confirmed real in
`errors.h`) already rejects any character outside its own allowed set
before a path is ever considered valid, and a tab/newline is not in
that set; the artifact/metadata-path fields are CMake-computed, never
human-authored, from a fixed build-tree root plus a mechanically
derived filename (`${output_dir}/${base}.amesh`-shaped, D4/D7), never
containing a tab either. `loadSceneDependencyManifest()`
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
6. Build `SceneDependencyResolver::entries` as an `AssetId`-sorted
   `std::vector`, never a `std::unordered_map` (D2's own type) — a
   deliberate lookup-container choice so no code path in this Plan
   ever exposes hash-iteration order, even incidentally. **This
   resolver is a point-lookup structure only; D10's own distinctIds
   collection, not this container's own storage order, is what
   actually establishes "ascending first-reference" load order** — see
   D2's own corrected note on `SceneDependencyResolver` and D10's own
   explicit call-order walkthrough for the single, authoritative
   source of that guarantee.

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

### D10. Runtime — `initializeSteps()` resequencing, ownership, and destruction order

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

// (c) Collect distinct AssetIds in ascending FIRST-REFERENCE order --
//     the sole source of this Plan's own load-order guarantee (Spec
//     0015 Human Review Approval item 10). Walks scene's own node
//     array in index order; the resolver (built in (a), AssetId-sorted
//     for lookup only) is never iterated here or anywhere else.
std::vector<AssetId> distinctIds;
for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
  if (const auto& node = scene.node(i); node.renderable.has_value()) {
    const AssetId id = node.renderable->meshAsset;
    if (std::find(distinctIds.begin(), distinctIds.end(), id) == distinctIds.end()) distinctIds.push_back(id);
  }
}

// (d) Phase 1: resolve every one -- no I/O, no Entity yet, same order as (c).
std::vector<const SceneDependencyResolver::Entry*> resolved;
for (AssetId id : distinctIds) {
  const auto* entry = resolver.find(id);
  if (!entry) { /* log, markFailed(), Err(SceneDependencyUnresolved) */ }
  resolved.push_back(entry);
}

// (e) Phase 2: load, same order as (c)/(d) -- distinctIds' own index
//     order IS the load order; the map below is populated in that
//     order but is a keyed store, never iterated afterward in any
//     order-sensitive way (runFrame()'s own per-DrawItem lookups are
//     by AssetId, one key at a time, order-independent by nature).
std::unordered_map<AssetId, atlantis::renderer::Mesh> meshResourceMap;
for (std::size_t i = 0; i < distinctIds.size(); ++i) {
  auto meshResult = loadStaticMeshAsset(resolved[i]->artifactPath, resolved[i]->metadataPath);
  if (meshResult.isErr()) { /* log, markFailed(), Err(SceneDependencyLoadFailed) */ }
  auto createResult = createMesh(*device_, vertexInputLayout_, /* ... from meshResult.value() ... */);
  if (createResult.isErr()) { /* log, markFailed(), Err(SceneDependencyLoadFailed) */ }
  meshResourceMap.emplace(distinctIds[i], std::move(createResult.value()));
}

// (f) Instantiate -- infallible.
World world = atlantis::world::fromValidatedSceneData(scene);

// (g) Publish -- only now, both fully built. World is move-constructible
//     but NOT move-assignable (ADR-0049/Spec 0014, unchanged) -- world_
//     is std::optional<World> (D2's own corrected member type) and this
//     is emplace(), i.e. in-place move-CONSTRUCTION, never assignment.
//     meshResourceMap_ is a plain std::unordered_map, whose own move-
//     assignment is not deleted, so plain assignment is correct there.
world_.emplace(std::move(world));
meshResourceMap_ = std::move(meshResourceMap);
```

**Every early-return branch above happens before (f)/(g)** — no
partial `world_`/`meshResourceMap_` mutation on any failure path; the
locally-scoped `resolver`/`scene`/`resolved`/`meshResourceMap`/`world`
values are simply destroyed by ordinary C++ scope exit on return, no
explicit rollback code (D9's own `fromValidatedSceneData()` itself
never observes a failure, per D6/D9). If `initializeSteps()` fails
before (g), `world_` remains `std::nullopt` and `meshResourceMap_`
remains empty — both their own default, harmless states — and
`RuntimeApplication` never reaches `Running` (Spec 0013's own existing
lifecycle contract, unchanged), so `runFrame()` is never called against
a half-published instance.

**Ownership and destruction order — confirmed against the real
`RuntimeApplication` member list
(`src/runtime/include/atlantis/runtime/runtime_application.h`), not
assumed:**

```cpp
// runtime_application.h -- existing member list, with this Plan's own changes marked
PlatformSession platformSession_;
std::unique_ptr<atlantis::rhi::Device> device_;
std::unique_ptr<atlantis::rhi::Presentation> presentation_;
std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap_;  // CHANGED: replaces the old std::optional<Mesh> mesh_, same declaration slot
std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer_;
std::unique_ptr<atlantis::rhi::Texture> depthTexture_;
std::optional<atlantis::renderer::Material> material_;

atlantis::renderer::Renderer renderer_;
std::optional<atlantis::world::World> world_;  // CHANGED: was a bare World, D2's own note
atlantis::asset_system::AssetId knownMinimalCubeAssetId_ = 0;  // REMOVED -- see below
// ...remaining members unchanged...
```

C++ destroys non-static members in the **reverse** of their declaration
order — this file's own existing comment (`runtime_application.h`
lines 91–98, confirmed real) already states the resulting destruction
sequence explicitly: **Material, Texture, Buffer, Mesh, Presentation,
Device** (declaration order is the exact reverse). `meshResourceMap_`
**replaces `mesh_` in that exact same declaration slot** — immediately
after `presentation_`, before `cameraBuffer_` — so it destructs at
exactly the position the comment already documents for "Mesh": after
Material/Texture/Buffer, but **before Presentation and Device**,
satisfying "every GPU `Mesh` destroyed before `Device`" by construction,
not by a new ordering rule this Plan invents — the existing rule
already covers it once `meshResourceMap_` occupies `mesh_`'s own slot.
Every `Mesh` value inside the map is destroyed when the map itself is
(the map owns its own values; nothing here needs its own destructor).
`world_`'s own position (after `material_`/`renderer_`, outside the
GPU-resource block) is unchanged from today — it owns no GPU resource
and has no ordering relationship to Device/Presentation/Mesh, exactly
as the file's own existing comment already states; only its *type*
changes (D2). `knownMinimalCubeAssetId_` — today's single-mesh
identity field — is removed outright: `meshResourceMap_`'s own keys
are the (plural) equivalent, and nothing else reads this field once
D10 lands.

**`runFrame()`'s own `DrawItem` loop borrows only already-published
mesh data, by construction, not by a new check this Plan adds**:
`item.mesh = &meshResourceMap_.at(renderable.meshAsset);` (a keyed
lookup — matching `resolveMeshAsset()`'s own existing per-`AssetId`
check, `scene_extraction.h`, now querying a map instead of one
hard-coded comparison; this is the one small, disclosed edit to
already-`Accepted` per-frame code this Plan makes, not a new
translation layer) — `runFrame()` is only ever called once
`RuntimeApplication` has reached `Running`, which per the paragraph
above only happens after step (g) has already published a fully-formed
`meshResourceMap_`; there is no code path where `runFrame()` observes
a partially-populated map, so no defensive re-check is added at the
`DrawItem`-construction call site itself (matching how `mesh_`/`material_`
are already dereferenced unconditionally, `&*mesh_`/`&*material_`, in
this exact file today).

**`shutdown()`'s own existing body** (`runtime_application.cpp`,
confirmed real: `material_.reset(); depthTexture_.reset();
cameraBuffer_.reset(); mesh_.reset(); presentation_.reset();
device_.reset();`, called once, idempotent, guarded by the existing
`ShutDown`-state check) gains exactly one substitution — `mesh_.reset()`
becomes `meshResourceMap_.clear()` — in the identical position,
preserving the identical explicit-early-teardown order the existing
five other calls already establish; `world_` is not reset here (it
owns no GPU resource, matching today's behavior — `world_` was never
reset in `shutdown()` before this Plan either). Idempotency is
unaffected: `meshResourceMap_.clear()` on an already-empty map (a
second `shutdown()` call, or a call after `initializeSteps()` never
reached step (g)) is a safe no-op, exactly as `mesh_.reset()` on an
already-empty `optional` already is today. No double-destruction risk:
every value is owned by exactly one container, cleared/reset exactly
once per `shutdown()` invocation, and `shutdown()` itself is guarded
against re-entry by the existing `RuntimeLifecycleState::ShutDown`
early-return.

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
   own encapsulation contract, no public default constructor, no
   cook/decode logic yet).
   `tests/asset_system/validated_scene_data_tests.cpp`: V11 (compile-fail
   negative test and `static_assert`), construction/copy/move round-trip.
2. **Authoring parser** (D3's `scene_source.h`/`.cpp` —
   `parseSceneSource()`/`serializeSceneSource()`, `ParsedSceneSource`).
   `tests/asset_system/scene_source_tests.cpp`: V1 (round-trip),
   grammar acceptance/rejection.
3. **Scene cooker** (D4/D5's `cook_scene.h`/`.cpp`, `scene_artifact.h`/`.cpp`'s
   own `encodeSceneArtifact()`, `scene_metadata.h`/`.cpp`).
   `tests/asset_system/cook_scene_tests.cpp`: V2–V7, V9 (encode-side),
   V12 (determinism, atomic write), V28 (cook-side `EmptyScene`).
4. **Scene decoder** (D6's `decode_scene.h`/`.cpp`, `scene_artifact.h`'s
   own `decodeSceneArtifact()`). `tests/asset_system/decode_scene_tests.cpp`:
   V8, V9 (decode-side), V10, V28 (decode-side `EmptyScene`).
5. **CMake scene asset declaration** (D7 — `atlantis_add_scene_asset()`,
   the additive `LOGICAL_PATH` export line, `atlantis_asset_cooker`'s
   own `--kind=scene` mode and `sceneCookErrorMessage()`, the new,
   disclosed `/w14062` on `atlantis_asset_cooker_lib`). No new scene
   asset declared yet (that is Step 9) — verified via a test-only scene
   fixture declared in `tests/asset_system/CMakeLists.txt` alone. V13,
   V27 (`SceneCookError`'s own portion).
6. **`World::fromValidatedSceneData()`** (D9,
   `src/world/include/atlantis/world/scene_instantiation.h`/`.cpp`).
   `tests/world/scene_instantiation_tests.cpp`: V21.
7. **Runtime manifest loading** (D8's `scene_manifest.h`/`.cpp`,
   `RuntimeInitError`/`BootstrapConfig` additions, `SceneManifestError`'s
   own `toString()`). `tests/runtime/scene_manifest_tests.cpp`: V14–V19,
   V27 (`RuntimeInitError`/`SceneManifestError` portion — both already
   covered by `atlantis_runtime_host`'s own existing `/w14062`, D2).
8. **Runtime `initializeSteps()`/ownership resequencing** (D10 — replaces
   `buildValidationScene()`; `runtime_application.h`'s own `world_`
   retyped to `std::optional<World>`, `meshResourceMap_` added in
   `mesh_`'s former declaration slot (same reverse-destruction-order
   guarantee), `mesh_`/`knownMinimalCubeAssetId_` removed;
   `shutdown()`'s own `mesh_.reset()` becomes `meshResourceMap_.clear()`;
   `resolveMeshAsset()`'s own small map-lookup edit). `tests/runtime/`:
   V20, V22 (GPU-independent portions).
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
    (V26); switch-exhaustiveness positive/negative build check (V27);
    manual windowed verification (V24, genuine human — matching Spec
    0014's own established, and only recently satisfied, standard);
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
  (additive), `src/asset_system/CMakeLists.txt` (additive —
  `atlantis_add_scene_asset()`, the `LOGICAL_PATH` export line),
  `src/tools/asset_cooker/{cook_command.h,cook_command.cpp,main.cpp}`
  (additive third mode), `src/tools/asset_cooker/CMakeLists.txt`
  (additive — new, disclosed `/w14062`, D2's own switch-exhaustiveness
  inventory), `src/runtime/include/atlantis/runtime/{init_error.h,bootstrap_config.h}`
  (additive), `src/runtime/include/atlantis/runtime/runtime_application.h`
  (`world_`'s own type change to `std::optional<World>`;
  `meshResourceMap_` added in `mesh_`'s own former declaration slot;
  `mesh_`/`knownMinimalCubeAssetId_` removed — D2/D10), `src/runtime/src/runtime_application.cpp`
  (D10's own resequencing — `buildValidationScene()` and its own call
  site removed, replaced; `shutdown()`'s own `mesh_.reset()` becomes
  `meshResourceMap_.clear()`), `src/runtime/src/main.cpp` (new
  `BootstrapConfig` fields populated from new CMake compile
  definitions), `src/runtime/include/atlantis/runtime/scene_extraction.h`/`.cpp`
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
- **Checked in vs. build-tree-only — explicit, per Independent Review.**
  Checked in: `assets/scenes/world_scene.scene.txt` (the authoring
  source — matches `assets/meshes/minimal_cube.mesh.txt`'s own
  precedent exactly: authoring sources are committed, cooked output is
  not). **Never checked in, build-tree-only, generated fresh by every
  build** (matches `minimal_cube`'s own `.amesh`/`.amesh.meta.txt`,
  neither of which is committed, confirmed real by their absence from
  `assets/` and their presence only under `${CMAKE_BINARY_DIR}/assets/`):
  the cooked scene artifact (`.ascene`), its metadata sidecar
  (`.ascene.meta.txt`), and the per-scene dependency manifest
  (`.ascene.manifest.txt`) — none of the three is ever committed, and
  none contains a path that would be meaningful outside the machine
  that produced it (the artifact/metadata contain no path at all, only
  `AssetId`s; the manifest's own build-tree paths are exactly as
  ephemeral as `minimal_cube`'s own already-uncommitted artifact
  path). `.gitignore` coverage for `${CMAKE_BINARY_DIR}` already
  excludes all three, matching how it already excludes every other
  build-tree asset output today — no new ignore rule is needed.

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
| V11 | `ValidatedSceneData`'s own unforgeability: `static_assert(!std::is_default_constructible_v<ValidatedSceneData>)` (ADR-0053's own Human Review Correction, 2026-08-23 — confirms no caller can default-construct one at all, not merely that construction with arbitrary data is blocked); a compile-fail negative test (documented, not built, matching `EntityId`'s own V27 precedent) confirming no external code can name the private non-default constructor, assign to any field, or obtain a mutable reference; copy/move preserve every accessor's own observed value, confirmed only against an instance a real `decodeScene()` call produced (there is no other kind to test against). | `validated_scene_data_tests.cpp` | GPU-independent (mix of runtime and compile-time) |
| V12 | Cooker determinism and atomic writes: cooking the same source twice produces byte-identical artifact and metadata bytes; a forced mid-write failure leaves no partial output file and does not disturb a pre-existing valid one — mirroring `cookStaticMesh()`'s own already-`Accepted` V11 (Spec 0012/Plan 0012) test shape exactly. | `cook_scene_tests.cpp` | GPU-independent |
| V13 | CMake re-import triggering: editing the scene source, the cooker, or a declared `MESH_DEPENDENCIES` target re-cooks the scene on the next build; editing an unrelated, undeclared asset does not. | Manual, recorded (matching Plan 0012 Section D7's own established procedure) | Manual |
| V14 | Manifest: a duplicate logical-path entry is rejected via `Err(SceneManifestError::DuplicateLogicalPath)`. | `scene_manifest_tests.cpp` | GPU-independent |
| V15 | Manifest: two distinct logical paths engineered (in the test) to hash to the same `AssetId` are rejected via `Err(AssetIdCollision)`. | `scene_manifest_tests.cpp` | GPU-independent |
| V16 | Manifest: an entry whose own metadata sidecar's `assetId` does not match the manifest-computed `AssetId` is rejected via `Err(MetadataArtifactMismatch)`. | `scene_manifest_tests.cpp` | GPU-independent |
| V17 | Manifest: a scene reference naming an `AssetId` with no manifest entry fails via `Err(RuntimeInitError::SceneDependencyUnresolved)`, confirmed to occur **before** any `Entity` exists. | `scene_manifest_tests.cpp` or a dedicated Runtime-level test | GPU-independent |
| V18 | Manifest: a declared `MESH_DEPENDENCIES` entry the scene never references does **not** fail the load — the resolver builds successfully, and only referenced entries are ever resolved/loaded. | `scene_manifest_tests.cpp` | GPU-independent |
| V19 | Deterministic load order matches **first-reference**, not `AssetId`-numeric, order — the specific regression this item exists to prevent. Test scene deliberately engineered so a node referencing a **numerically larger** `AssetId` appears **before** a node referencing a numerically smaller one (i.e., first-reference order and `AssetId`-sorted order are guaranteed to disagree); the observed load sequence (instrumented at the `loadStaticMeshAsset()` call site in D10's own step (e)) matches first-reference order exactly, proving the implementation does not secretly sort by `AssetId` and does not depend on the resolver's own internal storage order or on `std::unordered_map` iteration. Repeated runs against the same scene additionally produce an identical sequence (bare determinism, not merely "some deterministic-looking order"). | A dedicated Runtime-level test | GPU-independent |
| V20 | Transactional failure at every stage: a scene that fails cook-time validation never produces an artifact; a corrupted artifact never produces a `ValidatedSceneData`; an unresolved or failed-to-load mesh dependency never reaches `World::fromValidatedSceneData()` and leaves `RuntimeApplication` with neither a populated `world_` nor a populated `meshResourceMap_` — confirmed directly by inspecting both members, not inferred from a process exit code. | A dedicated Runtime-level test (GPU-independent, injecting a bad manifest/artifact path) | GPU-independent |
| V21 | `World::fromValidatedSceneData()`: a hand-constructed `ValidatedSceneData` (via the test's own `friend`-equivalent access, matching `EntityLifecycleTestAccess`'s own established pattern) produces a `World` whose entity count, hierarchy (`getParent()`), `Transform`, `Camera`, `Renderable`, and `activeCamera()` all match exactly; deterministic instantiation order confirmed via repeated runs producing identical `EntityId` sequences (matching Spec 0014's own V14 shape); `static_assert` confirming the function's own return type is `World`, not a `Result`. | `scene_instantiation_tests.cpp` | GPU-independent (mix of runtime and compile-time) |
| V22 | The loaded scene's CPU-side `World` state (entity count, every `Transform`/`Camera`/`Renderable` value, hierarchy, active camera) is identical, field-for-field, to `buildValidationScene()`'s own hand-built `World` state — confirmed by a direct comparison test, GPU-independent portion first (structural equivalence), then re-confirmed by the GPU-required golden compare (V23) as the rendering-level proof. | A dedicated Runtime-level test, then `world_scene_loaded_gpu_tests.cpp` | GPU-independent + `gpu`-labeled |
| V23 | Real GPU, headless: the loaded scene asset, rendered through the existing, unmodified extraction/`Renderer` pipeline, matches the existing, already checked-in `world_scene` golden with **zero** channel difference. The existing golden's own hand-authored-fixture test (`world_scene_gpu_tests.cpp`) is re-run unmodified in the same suite, proving this Plan's own new path did not disturb it — the same cross-check `minimal_cube`'s own Asset-System-sourced precedent already established. | `world_scene_loaded_gpu_tests.cpp`; `world_scene_gpu_tests.cpp` (existing, re-run) | `gpu`-labeled |
| V24 | Manual windowed: the real `atlantis_runtime` executable, launched with the new scene-asset configuration, shows the same five distinct, correctly-shaded, depth-ordered cubes at their D9 positions as Spec 0014's own already-verified windowed check; interactive resize/minimize/restore/close all behave correctly — an actual human, using a real graphical session, confirms this directly (matching Spec 0014's own V20 requirement and its own recorded 2026-08-23 PASS precedent exactly — never satisfied by programmatic Win32 automation alone). | Manual | Manual |
| V25 | Debug **and** Release: clean configure + build; `ctest -LE gpu` and `ctest -L gpu` both green on both configurations; Vulkan Validation Layers grepped clean (not merely inferred from exit status) on every GPU-touching path, including the new loaded-scene test. | Both configurations | Manual, recorded |
| V26 | Module boundary / forbidden-dependency scan: `tests/asset_system/module_boundary_tests.cpp`'s own existing scan confirms `src/asset_system/` still names no `atlantis/world/` header (direct, automated proof ADR-0052's own cycle-avoidance Decision holds); `tests/world/module_boundary_tests.cpp`'s own existing scan confirms `src/world/` still names no RHI/Renderer/RenderGraph/ShaderSystem/Platform/VulkanBackend/Runtime header; `git diff --stat` confirms no file under `src/rhi/`, `src/renderer/`, `src/render_graph/`, `src/vulkan_backend/`, `src/platform/`, `src/shader_system/`, `shaders/`, or `tests/image_regression/goldens/` was modified; `CMakeLists.txt`/`vcpkg`-equivalent dependency list confirms no new third-party dependency was added. | `module_boundary_tests.cpp` (both), manual `git diff --stat` review | GPU-independent + Manual |
| V27 | Every new enum's own consuming switch is exhaustive, no `default:` case, and — where a switch genuinely exists in production code — compile-time protected: `RuntimeInitError::toString()`'s four new cases and `SceneManifestError::toString()` both compile under `atlantis_runtime_host`'s own existing, real `/w14062` (temporarily removing a case and confirming the build fails naming that exact enumerator, then restoring it, matching Spec 0013's own already-`Accepted` `C4062` positive/negative re-verification precedent exactly); `SceneCookError`'s own `sceneCookErrorMessage()` compiles under `atlantis_asset_cooker_lib`'s own new, disclosed `/w14062` the same way. Confirmed, not merely asserted, that `SceneArtifactDecodeError` has no production switch anywhere (D2's own switch-exhaustiveness inventory) — grepped for `switch` against that type across `src/`, matching zero production call sites. | Manual, recorded (positive/negative build check); a `grep`-based inventory check | GPU-independent + Manual |
| V28 | A zero-node scene is rejected as `Err(SceneCookError::EmptyScene)` at cook time (no artifact written) and, independently, as `Err(SceneArtifactDecodeError::EmptyScene)` at decode time (a hand-crafted, artificially-empty artifact, bypassing the cooker entirely, still correctly rejected — proving the decoder does not merely trust a well-behaved cooker); confirms, together with V11's own `static_assert`, that no `ValidatedSceneData` instance — empty or otherwise malformed — can ever exist outside a successful, non-empty `decodeScene()` result (ADR-0053's own Human Review Correction, 2026-08-23). | `cook_scene_tests.cpp`, `decode_scene_tests.cpp` | GPU-independent |

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

- V1–V28 all executed and recorded; V13, V24, V25, V27 (partially)
  recorded as manual verification in the Implementation PR — V24
  specifically requires genuine human observation through a real
  graphical session, matching Spec 0014's own established, hard-won
  standard; programmatic automation is not, and must never be recorded
  as, a substitute.
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

## Independent Review — Round 2 (2026-08-23): targeted final fix, not a broad re-review

Six concrete findings, each corrected directly in the D-sections above
rather than left as a note — all mechanical (this Plan's own C++/CMake
shape choices), none touching Spec 0015 or ADR-0052–0054's own already-
Approved/Accepted content:

1. **A real compile error, found and fixed.** `RuntimeApplication::world_`
   is declared today as a bare `World` (confirmed real,
   `runtime_application.h`); `World` is not move-assignable. The prior
   draft's own `world_ = std::move(world);` would not compile. Fixed:
   `world_`'s own type changes to `std::optional<World>` (matching
   `mesh_`/`material_`'s own already-existing pattern for the identical
   shape), published via `world_.emplace(std::move(world))` — in-place
   move-construction, never assignment (D2, D10).
2. **A misleading internal description, found and fixed.** D8's own
   prior text claimed the resolver's `AssetId`-sorted storage was "what
   makes load order deterministic" — factually wrong relative to D10's
   own code, which was already correct (first-reference order from
   walking `ValidatedSceneData`'s own node array) but risked being
   misread as license to iterate the resolver directly, which would
   silently produce `AssetId`-numeric order instead of the Human-
   Review-Approved first-reference order. Fixed: D2/D8/D10 now state,
   consistently, that the resolver is a point-lookup structure only;
   V19 rewritten to deliberately test a scene whose first-reference and
   `AssetId`-numeric orders disagree, specifically to catch this class
   of regression rather than merely confirming "some" deterministic
   order.
3. **An over-triggering CMake dependency, found and fixed.** The prior
   draft's own `add_custom_command(... DEPENDS ... ${dependency_targets})`
   would re-cook the scene on every mesh *content* edit, even though
   the scene artifact's own bytes (which store only a path-derived
   `AssetId`, unaffected by mesh content) never actually change as a
   result. Fixed: mesh targets move to `add_dependencies()` on the
   scene's own target (ordering only), out of the custom command's own
   `DEPENDS` (staleness) — D7's own "Rebuild scoping" now states each
   distinct trigger condition explicitly, including the case this fix
   addresses.
4. **A bare assertion, replaced with cited real evidence.** "multi-
   config-safe" was previously asserted from `file(GENERATE)`'s own
   general behavior alone. D7 now cites the exact real file/lines
   (`src/runtime/CMakeLists.txt` lines 69–80, whose own comment
   literally reads "absolute, configuration-independent build-tree
   paths") as the concrete, already-`Accepted`, already-working
   precedent this Plan's own manifest mechanism matches.
5. **An incomplete exhaustiveness inventory, completed.** Every new
   enum's own consuming switch (or explicit absence of one) is now
   listed with its exact target and exact protection status (D2's own
   new table); one new, disclosed `/w14062` addition
   (`atlantis_asset_cooker_lib`) was found necessary and added; two
   enums' own switches were confirmed to already fall under
   `atlantis_runtime_host`'s own existing protection, requiring no new
   CMake change; new V27 records the positive/negative build check.
6. **A wording-precision finding, disclosed, not silently resolved.**
   Spec 0015's own Human Review Approval note (item 3) summarizes
   `ValidatedSceneData`'s own construction contract as "no public
   default/arbitrary construction"; ADR-0053's own more detailed,
   Accepted Decision (item 4) explicitly keeps a trivial `public`
   default constructor for the empty-scene case. This Plan follows the
   ADR's own more detailed, reasoned text (D2) and flags the
   discrepancy for Human Review's own awareness — it is a summary-
   wording precision question, not a design disagreement (no caller can
   construct a non-empty, malformed instance either way), and is
   explicitly **not** resolved here by editing either already-approved
   document.

## Deviations, objections, and open mechanical details

**No `Accepted`/`Approved` decision in Spec 0015 or ADR-0052–0054 was
found to be unimplementable against the real, current source tree.**
Two genuinely open mechanical details, appropriately left to
Implementation, neither architectural — both already noted for Human
Review's own awareness and neither blocked this Plan's own approval:

1. **The exact scene metadata sidecar's own extra fields** (beyond
   `schema_version`/`node_count`) — D5 fixes the minimum; Implementation
   may add a content hash if it proves useful during Step 3, without
   revisiting this Plan.
2. **Whether `scene_manifest_tests.cpp` (V17) lives under
   `tests/asset_system/` or `tests/runtime/`** — a file-location detail
   with no design content (`Files / Modules Touched` already discloses
   this as undecided).

**Resolved since the prior round:** the `ValidatedSceneData` default-
constructor wording discrepancy between Spec 0015's own condensed
Human Review Approval summary and ADR-0053's own Decision (Independent
Review Round 2, item 6 above) is settled — ADR-0053 now carries its
own "Human Review Correction (2026-08-23)" resolving it in the "no
public default construction" direction; this Plan's own D2/D4/D6 and
V11/V28 already reflect the corrected design. See
[ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)'s
own Correction and Spec 0015's own updated Human Review Approval item 3
cross-reference for the full record.

This Plan's own status is `Approved / Ready for Implementation` — see
"Human Review Approval" at the top of this document for the full
record. The two items above, and any remaining C++/CMake naming choice
not already fixed in the D-sections, are mechanical details for
Implementation to resolve without reopening this Plan; no architectural
question remains open.
