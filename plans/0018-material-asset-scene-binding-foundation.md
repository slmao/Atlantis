# Plan: Material Asset & Scene Binding Foundation

- **Spec:** [specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md) (`Approved`, Human Review Approval recorded 2026-08-27 — authorizes drafting this Plan only, not Implementation)
- **Status:** `In Review`. Not self-approved — see [AGENTS.md](../AGENTS.md)'s Golden Rule. Implementation must not start until a human records approval here, matching Plan 0017's own precedent exactly.
- **Author:** slmao
- **Related ADR(s):** [ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md) (`Accepted`), [ADR-0060](../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md) (`Accepted`)

This Plan implements Spec 0018 and ADR-0059/ADR-0060 exactly as approved.
It does not redesign, reopen, or narrow any decision those documents
already settled — every "P" (Plan-level decision) section below cites
the specific Spec Decision item and/or ADR item it implements, and stops
at *how*, never revisiting *what* or *why*. Where a Spec Decision
explicitly left a mechanical choice open for Plan-time closure (D6's
`AssetId`-embedding question, D6's error-enumerator naming, D9's exact
member order), this Plan closes it here, citing the precedent it
mirrors.

## Plan Review (2026-08-28, pre-Human-Review self-check)

One centralized review pass performed while drafting, not a separate
round after the fact — mirroring Spec 0018's own two "Readiness for
Human Review" rounds, scaled to Plan-level mechanical detail. Findings,
each fixed in the draft below before this Plan was presented:

1. **`atlantis_add_texture_asset()` is missing a `LOGICAL_PATH` export
   that this Plan's own manifest wiring needs.** Confirmed directly
   (`src/asset_system/CMakeLists.txt:195-233`): unlike
   `atlantis_add_static_mesh_asset()` (which exports
   `ATLANTIS_<NAME>_LOGICAL_PATH`, consumed by
   `atlantis_add_scene_asset()`'s own `MESH_DEPENDENCIES` manifest-line
   construction), `atlantis_add_texture_asset()` exports only
   `_ARTIFACT_PATH`/`_METADATA_PATH`/`_TARGET` — three variables, not
   four. Spec 0018 Requirements calls for a scene-level
   `TEXTURE_DEPENDENCIES` argument that must build a manifest line the
   same shape as `MESH_DEPENDENCIES` does, which is structurally
   impossible without a texture's own logical path being exported. Fixed
   by adding this Plan's own Milestone 8 as a small, additive, one-line
   CMake fix to `atlantis_add_texture_asset()` (mirroring the exact
   `ATLANTIS_${ARG_NAME}_LOGICAL_PATH` line
   `atlantis_add_static_mesh_asset()` already has), called out explicitly
   rather than silently assumed to already work.
2. **D6's open `AssetId`-embedding question is closed here as "texture
   precedent" (no self-`AssetId` embedded in the artifact), not "mesh
   precedent."** See P3 below for the justification — not deferred to
   Implementation.
3. **RuntimeInitError's naming choice (D6's other open item) is closed
   as "reuse the existing `SceneDependencyUnresolved`/
   `SceneDependencyLoadFailed` enumerators generically," not "add a new
   Material-named pair."** See P9 below — confirmed both existing
   enumerators are already kind-agnostic in their real call sites
   (`resolver.find(id)` and a load-call failure are structurally
   identical regardless of which asset kind's `AssetId` is being
   resolved), so adding a duplicate pair would be exactly the kind of
   enumerator growth Spec 0018's own D6 discipline directs against.
4. **The scene grammar's new `material=` token requires a fourth
   node-line token-count case (13), not a reuse of the existing 12 or 14
   case.** Verified directly against the real grammar
   (`scene_source.cpp:145`, `tokens.size() != 11 && tokens.size() != 12
   && tokens.size() != 14`) — confirmed no existing case already
   accommodates "mesh + material" (12 is mesh-only; 14 is camera-only,
   mutually exclusive with mesh today). See P6.
5. **Format-change rebuild failure must force the WHOLE frame's
   `DrawItem` list empty, not merely skip newly-pending materials** —
   re-derived directly from Spec 0018 D9's own text ("This frame draws
   nothing (an empty `DrawItem` list)") and cross-checked against the
   real per-entity fallback logic this Plan adds (M14): an entity whose
   `materialAsset` is absent would otherwise still draw with
   `fallbackMaterial_`, which after a failed rebuild is still built for
   the *old* format — exactly the mismatched-attachment-format condition
   D9 exists to close. Milestone 13 makes this an explicit, unconditional
   override, not an emergent property of the per-entity loop.
6. **Six implementability claims verified against real, current code
   this Plan-drafting session, not asserted from the ADR's own prior
   proof alone** — see "Pre-draft verification," items marked
   `[Claim a]`–`[Claim f]`. All six confirmed feasible with today's real
   APIs; none required a design change from ADR-0060's own contract.

No unresolvable conflict with the Approved Spec/Accepted ADRs was found.
Every item above is a mechanical closure of a question the Spec itself
left open for the Plan, not a reopening of a settled decision.

## Objective

Implement Spec 0018's minimal closed loop end to end: a fourth Asset
System asset type (Material), a Scene Asset/`World` schema widened with
an optional material reference, and Runtime's two-phase (CPU-transaction,
then deferred-per-frame-GPU-realization) resolution pipeline — so that a
real, Runtime-loaded scene can bind a real, asset-sourced textured
Material per entity, with zero visual change to any scene that does not
opt in.

## Pre-draft verification against real, current source

Re-confirmed directly against `origin/main` at the tip this Plan branch
was created from (`6b6e56d`, both PR #85 and PR #86 merged), by reading
full files and exact line ranges — not from memory of Spec/ADR drafting:

**Gate check (performed before creating this Plan's own branch):**
Spec 0018 `Status: Approved` with its exact "This approval authorizes
drafting Plan 0018 only. It does not authorize Implementation" sentence
present verbatim; ADR-0059 and ADR-0060 both `Status: Accepted` — all
confirmed via `git show origin/main:<path>`, not merely the GitHub PR
API.

**Runtime composition root (all read in full):**
- `src/runtime/include/atlantis/runtime/runtime_application.h` —
  `RuntimeApplication`'s exact private member declaration order:
  `platformSession_, device_, presentation_, meshResourceMap_
  (unordered_map<AssetId, Mesh>), cameraBuffer_, depthTexture_,
  material_ (optional<Material>), renderer_, world_ (optional<World>),
  activeCameraEntity_, lifecycle_, lastExitReason_, closeRequested_,
  lastSeenFormat_ (optional<Format>), lastSeenExtent_
  (optional<Extent2D>), vertexInputLayout_, vertexSpirv_,
  fragmentSpirv_.` This fixes exactly where new material/texture/sampler
  resource maps must be inserted to get the destruction order D9
  requires (M11).
- `src/runtime/include/atlantis/runtime/scene_load.h` —
  `SceneLoadOutcome{World world; unordered_map<AssetId, Mesh>
  meshResourceMap;}`; `loadAndInstantiateScene(const BootstrapConfig&,
  Device*, const VertexInputLayout&)` already takes a **nullable**
  `Device*`, documented as enabling GPU-independent tests of the
  manifest/decode/dependency-unresolved paths — the direct precedent
  this Plan's own material/texture CPU-loading extension (M11) reuses
  without needing a new testability mechanism.
- `src/runtime/src/scene_load.cpp` (full file, 103 lines) — the real
  steps (a)–(g) `loadAndInstantiateScene()` runs today: (a) load
  manifest, (b) `decodeScene()`, (c) collect distinct mesh `AssetId`s in
  first-reference order (a `std::vector` + linear `std::find`, not a
  set), (d) resolve every one via `resolver.find()`, (e) load +
  `createMesh()` per distinct id, (f) `fromValidatedSceneData()`
  (infallible), (g) return `SceneLoadOutcome` by value (the transactional
  boundary is the return itself — nothing is written to caller state
  until the caller consumes `Ok()`). This exact shape is what M11
  widens, not replaces.
- `src/runtime/include/atlantis/runtime/bootstrap_config.h` — current
  fields: `applicationName; vertexShaderSpirvPath;
  vertexShaderReflectionPath; fragmentShaderSpirvPath;
  fragmentShaderReflectionPath; assetArtifactPath; assetMetadataPath;
  sceneArtifactPath; sceneMetadataPath; sceneDependencyManifestPath;
  enableValidationLayers;` — a plain value struct, no config-file/env-var
  reading. Confirms one shader-pair path set exists today
  (`minimal_mesh`); M10 adds a second, mirrored set for
  `MaterialKind::UnlitTextured`'s shader.
- `src/runtime/include/atlantis/runtime/scene_manifest.h` — `enum class
  SceneManifestError {ManifestUnreadable, MalformedEntry,
  DuplicateLogicalPath, AssetIdCollision, MetadataArtifactMismatch};`
  `SceneDependencyResolver{entries: sorted vector<pair<AssetId, Entry>>;
  find(AssetId) -> const Entry*}` — a **kind-agnostic** point lookup
  (`std::lower_bound`), confirming Spec 0018 D7's own claim that a
  material or texture `AssetId` resolves through this exact,
  unmodified mechanism with zero widening.
- `src/runtime/main.cpp` (full file) — exact current `BootstrapConfig`
  population: every path field is `std::string(ATLANTIS_RUNTIME_*) +
  "/<literal filename>"`, all four `ATLANTIS_RUNTIME_*` macros coming
  from `src/runtime/CMakeLists.txt`'s `target_compile_definitions()`.
  This is the exact pattern M10's new `unlitTextured*` fields/macros
  mirror.
- `src/runtime/CMakeLists.txt` (full file) — `atlantis_runtime_host`'s
  real source list (`lifecycle_state.cpp, exit_reason.cpp,
  init_error.cpp, error_classification.cpp, platform_session.cpp,
  scene_extraction.cpp, scene_manifest.cpp, scene_load.cpp,
  runtime_application.cpp`) and link closure (`Core, Platform, RHI,
  VulkanBackend, Renderer, ShaderSystem,
  ShaderSystemRhiIntegration, AssetSystem, World`) — `Atlantis::AssetSystem`
  is already linked, so no new link dependency is needed for Material's
  CPU types; the exact `target_compile_definitions()`/
  `add_dependencies()` pattern this Plan's M10 extends.
- `src/runtime/src/runtime_application.cpp` (full file read; exact line
  citations below reconfirmed against real, current content, not the
  ADR's own prior citations alone):
  - Lines 83–89: the two `static_assert`s proving `world_.emplace()`/
    `meshResourceMap_ = ...`'s publish step is genuinely `noexcept` — the
    direct template for M11's own widened publish `static_assert`s.
  - Lines 293–313: the real format-change block — `createMaterial()`
    called with `.colorFormat = currentFormat`; on failure,
    `lastSeenFormat_` is deliberately **not** updated and the code falls
    through with the *old* `material_` still set (today's latent gap D9
    fixes); on success, `material_ = std::move(...)` (create-before-
    destroy).
  - Line 330: `if (!material_ || !depthTexture_) { return; }` — the
    existing "nothing valid to draw yet" early-return shape M13's own
    all-or-nothing rebuild failure path reuses conceptually (empty
    `DrawItem` list, not a fatal error).
  - Lines 397–420: the real per-entity `DrawItem` loop —
    `resolveMeshAsset()` failure is a `continue` (recoverable, logged,
    per-entity skip); `item.material = &*material_;` unconditionally
    today, the exact line M14 makes conditional on `materialAsset`.
  - Lines 422–433: `createCommandList()` → `renderer_.drawFrame(*commandList,
    ...)` → `device_->submit(std::move(commandList), *target)` — **one**
    `CommandList`, **one** `submit()` call, confirming `[Claim a]` below.
  - Lines 450–479 (`shutdown()`): `device_->waitIdle()` called only if
    `lifecycle_.hasEverRun()`, then an explicit ordered
    `material_.reset(); depthTexture_.reset(); cameraBuffer_.reset();
    meshResourceMap_.clear(); presentation_.reset(); device_.reset();`
    — the exact sequence M13 widens to clear the three new resource maps
    in the texture/sampler-before-material order D9 requires.
- `src/runtime/include/atlantis/runtime/scene_extraction.h`/`.cpp` (full
  files) — `SceneExtractionError{NoActiveCamera, DegenerateCameraForward,
  DegenerateCameraBasis, UnresolvedMeshAsset}`; `resolveMeshAsset(AssetId
  requested, const vector<AssetId>& knownIds) -> Result<monostate,
  SceneExtractionError>` — a trivial, GPU-independent, already
  Runtime-private and already `tests/runtime/`-consumed boundary. This
  is where M9 adds `resolveMaterialAsset()`, verbatim-shaped, plus one
  new sibling enumerator `UnresolvedMaterialAsset` (same failure class,
  same severity, distinct name for log clarity — not a new enum family).
- `src/runtime/include/atlantis/runtime/init_error.h` — real current
  `RuntimeInitError` list, including `SceneDependencyUnresolved` ("a
  referenced AssetId has no resolver entry") and
  `SceneDependencyLoadFailed` ("a resolved AssetId's own mesh load
  failed") — both already documented generically enough (the first names
  no asset kind at all) that P9 below reuses them for material/texture
  dependency failures with only a doc-comment widening, not a new
  enumerator.

**Asset System precedent files (all read in full):**
- `src/asset_system/include/atlantis/asset_system/errors.h` (full file,
  160 lines) — the complete, current enum inventory:
  `LogicalPathError, AssetSetError, SourceParseError, MetadataParseError,
  ArtifactDecodeError, AssetLoadError, CookError, SceneCookError,
  SceneArtifactDecodeError, TextureCookError, TextureArtifactDecodeError,
  TextureLoadError`. `TextureCookError{ZeroDimension,
  DimensionExceedsMaximum, SourceOverflow, LogicalPathInvalid,
  AtomicWriteFailed}`, `TextureArtifactDecodeError{BadMagic,
  UnsupportedSchemaVersion, TruncatedHeader, InconsistentPixelDataSize,
  DimensionExceedsMaximum, UnknownFormat, UnsupportedMipCount}`,
  `TextureLoadError{ArtifactDecodeFailed, MetadataParseFailed,
  MetadataArtifactMismatch, MetadataReadFailed}` are the direct,
  letter-for-letter naming precedent M1/M2/M3's new `MaterialCookError`/
  `MaterialArtifactDecodeError`/`MaterialLoadError` mirror.
- `src/asset_system/include/atlantis/asset_system/texture_types.h`,
  `cook_texture.h`, `load_texture.h`, `texture_artifact.h`/`.cpp`,
  `texture_metadata.h` (all read in full) — confirmed the texture
  artifact embeds **no self-`AssetId`** (`kTextureArtifactHeaderSizeBytes
  = 36`: magic(8) + schema_version(4) + width(4) + height(4) + format(4)
  + mip_count(4) + pixel_data_offset(4) + pixel_data_size(4), no id
  field), with self-consistency entirely metadata-side
  (`TextureMetadata{assetId; sourceLogicalPath; width; height; format;
  channelsInFile;}`, cross-checked at load time). This is the precedent
  P3 below picks for Material, over the mesh artifact's own
  self-embedding precedent — Material, like a texture, is a single,
  non-indexed, whole-file-is-the-asset format with no internal
  cross-referencing that benefits from a redundant embedded id.
  `cookTexture(pixelBytes, width, height, channelsInFile, colorSpace,
  logicalPathInput, artifactOutputPath, metadataOutputPath)` and
  `loadTextureAsset(artifactPath, metadataPath)` are the exact function-
  signature shapes `cookMaterial()`/`loadMaterialAsset()` mirror (M3),
  adapted for a text-authored, not raw-pixel-authored, source (closer to
  `cookScene()`'s own "reads the source file itself" shape than
  `cookTexture()`'s "caller already decoded it" shape, since Material's
  own authoring source is plain text, not a binary asset needing an
  external decoder).
- `src/asset_system/src/scene_source.cpp` (full file, 275 lines) — the
  real grammar: `atlantis_scene_source_version: 1`; a `node:` line's
  token count is exactly 11 (transform only), 12 (+ `mesh=<path>`), or
  14 (+ three `camera_*=` fields) — `tokens.size() != 11 && != 12 && !=
  14` rejects anything else with `InvalidComponentGroup`. Mesh and
  camera are mutually exclusive in today's grammar (`if (tokens.size()
  == 12) {...} else if (tokens.size() == 14) {...}`). This fixes the
  exact insertion point and the exact new token-count value (13) M6
  needs.
- `src/asset_system/src/scene_artifact.cpp` (full file, 235 lines) — the
  real, exact per-node byte layout: transform `[0..35]` (9×f32),
  camera-slot `[36..51]` (has_camera u32 @36, 3×f32 @40/44/48),
  renderable-slot `[52..63]` (has_renderable u32 @52, mesh_asset_id u64
  @56), parent-slot `[64..71]` (has_parent u32 @64, parent_index u32
  @68) — total 72 bytes
  (`kSceneArtifactNodeRecordSizeBytes`), header 24 bytes
  (`kSceneArtifactHeaderSizeBytes`: magic 4 + schema_version 4 +
  node_count 4 + has_active_camera 4 + active_camera_index 4 + reserved
  4). `decodeSceneArtifact()`'s own independent, artifact-side
  `hasCycleByIndex()` re-check (never trusts the cooker) is the direct
  precedent for the new decode-time cross-check M6 adds (a `has_material`
  flag set without `has_renderable` set is a new, independently-checked
  corruption condition, not merely trusted-safe because `cookScene()`
  itself would never produce it).
- `src/asset_system/src/cook_scene.cpp` (full file, 232 lines) — the
  real end-to-end resolution: `normalizeLogicalPath()` +
  `computeAssetId()` resolve a mesh's logical-path string into an
  `AssetId` at cook time (line 194-198), with **no existence check** —
  the exact, already-`Accepted` "value-level-only reference validation"
  precedent (ADR-0059 D6/D7) M6's own material-reference resolution
  reuses verbatim, just for a second optional field.
- `src/asset_system/include/atlantis/asset_system/scene_types.h`,
  `validated_scene_data.h` (both read in full) — confirmed
  `DecodedRenderable{AssetId meshAsset = 0;}` is exactly one field today
  (line 29-31); `ValidatedSceneData`'s own privileged-constructor,
  friend-only-`decodeScene()`, no-public-default-constructor invariant
  is unaffected by widening `DecodedRenderable`'s own field count.
- `src/asset_system/CMakeLists.txt` (the three relevant functions read
  in full): `atlantis_add_static_mesh_asset()` (lines 51-98, exports 4
  `PARENT_SCOPE` vars including `_LOGICAL_PATH`),
  `atlantis_add_scene_asset()` (lines 111-178, `MESH_DEPENDENCIES`
  multi-value arg, per-entry `FATAL_ERROR` if not previously declared,
  `file(GENERATE)`-written 3-column manifest, `add_dependencies()` for
  build-ordering only — explicitly NOT part of the stamp's own
  `DEPENDS`, since a mesh's own content edit must not re-cook the
  scene), `atlantis_add_texture_asset()` (lines 195-233, exports only 3
  vars — the M8 gap fixed above), `atlantis_finalize_asset_validation()`
  (lines 245-257). These four function bodies are the exact, complete
  precedent M4/M8's own new/extended CMake functions mirror line-for-
  line.
- `src/asset_system/asset_id.h` — `using AssetId = std::uint64_t;`
  confirmed directly (not assumed).
- `src/tools/shader_compiler/compile_and_validate.cpp` (full file) —
  confirmed `texturedMaterialExpectedDescriptorContract()` (Spec
  0016/D6) is **already implemented and already wired end to end**:
  `--expected-contract=textured-material` is a real, already-functioning
  CLI value this file's own `validateDescriptorContractForStage()`
  already dispatches on (line 130-152). `shaders/textured_quad/
  CMakeLists.txt`'s own `atlantis_add_slang_shader_pair(... EXPECTED_CONTRACT
  textured-material)` call (confirmed directly, line 9-16) already uses
  it. **This means M10's shader-promotion work needs zero new descriptor-
  contract authoring** — the contract this Material kind's shader must
  match already exists, is already validated at shader-compile time, and
  is already proven correct by the existing `textured_quad` golden.
- `src/shader_system/CMakeLists.txt`'s `atlantis_add_slang_shader_pair()`
  (lines 62-86, full function read) — confirmed the real custom target
  name is `${NAME}_shaders` (so `textured_quad_shaders`, already
  existing) and it exports exactly one `PARENT_SCOPE` var
  (`ATLANTIS_${NAME}_SHADER_OUTPUT_DIR`).
- `shaders/textured_quad/CMakeLists.txt` (full file) — confirmed its own
  `atlantis_add_slang_shader_pair(NAME textured_quad ... EXPECTED_CONTRACT
  textured-material)` call already exists, unmodified by this Plan; only
  its own **placement** (which directory's `CMakeLists.txt` calls
  `add_subdirectory()` on it, and under what condition) needs to change.
- Root `CMakeLists.txt` (lines 55-119, full relevant range read) —
  confirmed the exact current structure: `shaders/minimal_renderer` is
  added unconditionally at line 79, immediately followed by
  `src/runtime` at line 88 (with an explicit comment: `atlantis_runtime`'s
  own `add_dependencies()` requires the named shader/asset targets to
  already be declared at the point that call is parsed); `shaders/
  textured_quad` is today added at line 111, **inside** the
  `if(ATLANTIS_BUILD_TESTS)` block, which starts at line 99 — i.e.,
  strictly after `src/runtime` has already been processed. This fixes
  both the fact of the gap (`ATLANTIS_BUILD_TESTS=OFF` truly builds no
  `textured_quad_shaders` target at all, confirmed structurally, not
  just by the sub-CMakeLists' own comment) and the exact mechanical fix:
  M10 moves `add_subdirectory(shaders/textured_quad)` to immediately
  after line 79's `shaders/minimal_renderer` call (before line 88's
  `src/runtime`), mirroring `minimal_renderer`'s own unconditional
  placement and its own stated reason (`atlantis_runtime` needs it)
  exactly.
- `src/asset_system/include/atlantis/asset_system/descriptor_contract.h`
  — re-confirmed `texturedMaterialExpectedDescriptorContract()`'s own
  doc comment: "two bindings, {set 0, binding 0, UniformBuffer, Vertex}
  ... plus {set 0, binding 1, Sampler, Fragment}" — matching the camera
  uniform + combined sampler contract Spec 0018 already asserts
  compatible.

**Vulkan submit/waitIdle/present chain — `[Claim d]`, fresh, line-level
verification this session (not reused from the ADR's own prior citation
alone):**
- `src/vulkan_backend/src/vulkan_device.cpp`: `submit()` (around line
  520) begins by calling `waitAndReleaseRetainedSubmission()` (draining
  any *previous* frame's retained submission first) before its own
  `vkQueueSubmit()` call (line 557, signaling `submissionFence_` and the
  target's own semaphore); `waitIdle()` (around line 571-580) calls the
  same `waitAndReleaseRetainedSubmission()` (line 465:
  `vkWaitForFences(...)`, line 469: `vkResetFences(...)`) and then
  `vkDeviceWaitIdle()` (line 580). Confirmed: `waitIdle()` called after a
  `submit()` that already signaled its own fence blocks until that exact
  submission's GPU work has completed.
- `src/vulkan_backend/src/vulkan_presentation.cpp`: `present()`'s own
  `VkPresentInfoKHR.pWaitSemaphores` (line 616) is set to
  `renderFinishedSemaphores_[imageIndex]` — the **exact same semaphore**
  `submit()` signals via `pSignalSemaphores` for that target. This
  confirms `[Claim d]`: `submit()` → `waitIdle()` → `present()`, called
  in that order on a realization frame, is legal — by the time
  `present()` runs, `waitIdle()` has already blocked until the GPU work
  `submit()` queued (including any upload pass) is done, so the
  semaphore `present()` waits on is already signaled; this is a strictly
  *stronger* guarantee than `present()` normally requires (it normally
  only needs the semaphore signaled by the time the *presentation
  engine* gets to it, not the CPU) — never a race, only extra,
  intentional CPU-side conservatism paid for exactly on realization
  frames (Spec 0018 D8 item 5).

**`[Claim a]`/`[Claim b]`/`[Claim c]` — upload-before-draw, one
`submit()`, and move-safety:** confirmed via
`runtime_application.cpp:422-433` (cited above) that today's draw path
is already exactly "one `CommandList`, `renderer_.drawFrame()` records
into it, one `submit()`" — M12's own upload-pass recording call is
inserted between `createCommandList()` and `renderer_.drawFrame()`,
changing nothing about the number of `CommandList`s or `submit()` calls.
`renderer::DrawItem::material` is a raw, non-owning `const Material*`
(confirmed via its existing use, `item.material = &*material_;`) — a
frame's own `DrawItem`s are built, consumed by
`Renderer::drawFrame()`'s command recording, and destroyed, all within
one `runFrame()` call, before this Plan's own candidate-to-map move (M12
step 6) ever runs; `Renderer::drawFrame()`'s own command recording
(`cmd.bindPipeline()`/`cmd.bindTexture()`) embeds real Vulkan handle
values into the `VkCommandBuffer` at record time, never retaining the
C++ `Material*`/`DrawItem*` itself past that call — so the candidate's
own C++ object address changing when it moves into
`materialResourceMap_` afterward is never observed by anything that
already recorded against it this frame.

**`[Claim f]` — headless realization testability, confirmed with a
concrete, already-existing mechanism:** `tests/image_regression/fixture/
textured_quad_fixture.cpp` already obtains a real `RenderTarget` with no
Platform window via `Device::createOffscreenTarget(OffscreenTargetCreateParams{extent,
format})` (confirmed directly, lines 248-249) — the exact mechanism
M16's new fixture reuses to call the same Phase 2 realization function
Runtime's own `runFrame()` calls, with a real `colorFormat` and a real
`RenderTarget`, no window required. This directly resolves D12's own
disclosed open question ("if Plan-time drafting finds a real, structural
reason [sharing logic] is not achievable... that is itself a
Human-Review-relevant finding") — no such reason was found; sharing is
achievable, confirmed by an already-proven, already-used precedent.

## Plan-level decisions (fixed here, not left to Implementation)

### P1. Material CPU types and file layout — implements Spec D1/D2, ADR-0059 Decision items 1/2

New files, `Atlantis::AssetSystem`, Core-only (no new link dependency —
confirmed the module's own `target_link_libraries` stays `Atlantis::Core`
only):

- `src/asset_system/include/atlantis/asset_system/material_types.h`:
  ```cpp
  enum class MaterialSamplerFilter { Nearest, Linear };
  enum class MaterialSamplerAddressMode { Repeat, ClampToEdge };
  enum class MaterialKind { UnlitTextured };
  struct MaterialAssetData {
    MaterialKind kind = MaterialKind::UnlitTextured;
    AssetId textureAsset = 0;
    MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
    MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
  };
  ```
  Names no RHI type — matching `TextureAssetData`'s own discipline
  exactly (ADR-0059's own already-fixed shape, reproduced verbatim here
  for Plan-level completeness, not redecided).

### P2. Material error domain — implements Spec D6's error-enumerator accounting

New enumerators added to `src/asset_system/include/atlantis/asset_system/errors.h`,
naming and shape mirroring `TextureCookError`/`TextureArtifactDecodeError`/
`TextureLoadError` exactly:

```cpp
enum class MaterialCookError {
  SourceFileUnreadable,
  SourceParseFailed,
  UnknownMaterialKind,   // genuinely new: no existing enumerator covers an unrecognized enum tag
  LogicalPathInvalid,    // reused name, matching CookError/TextureCookError's own precedent
  AtomicWriteFailed,
};

enum class MaterialArtifactDecodeError {
  BadMagic,
  UnsupportedSchemaVersion,
  TruncatedHeader,
  UnexpectedSize,        // NEW: Material's record is fixed-size (32 bytes, no variable
                          // payload unlike texture's pixel data) -- any size other than
                          // exactly 32 bytes is corruption, not merely "too small."
  UnknownMaterialKind,
  UnknownFilter,
  UnknownAddressMode,
};

enum class MaterialLoadError {
  ArtifactFileUnreadable,
  MetadataFileUnreadable,
  ArtifactDecodeFailed,
  MetadataParseFailed,
  MetadataArtifactMismatch,
};
```

`MaterialSourceParseError` lives beside its own grammar header
(`material_source.h`), matching where `SceneSourceParseError` lives
(`scene_source.h`), **not** in `errors.h` — the established convention
is that a grammar's own parse-error enum lives with the grammar, while
cook/decode/load enums (coarser, artifact-facing) live in `errors.h`:

```cpp
enum class MaterialSourceParseError {
  UnknownSourceVersion,
  MissingField,
  FieldOrderMismatch,
  UnknownKind,
  UnknownFilter,
  UnknownAddressMode,
  TrailingContent,
};
```

**Tools cooker dispatch:** `src/tools/asset_cooker/cook_command.h`/`.cpp`
gains `AssetKind::Material` in the existing `enum class AssetKind` and a
new `case` in `cook_command.cpp`'s `switch (request.kind)` (no `default`
today — confirmed directly — so this is a build error by construction
until the `case` is added, exactly like Spec 0018 D6 states) plus
`--kind=material` support and a new `runCookMaterialMode()` mirroring
`runCookSceneMode()`'s own shape (reads the source file path directly,
no external decoder needed — unlike `runCookTextureMode()`, which
decodes a PNG via `stbi_load()` first).

### P3. Material artifact byte layout — closes Spec D6's open `AssetId`-embedding question

**Decision: no self-`AssetId` embedded in the artifact — the texture
precedent, not the mesh precedent.** Justification: Material, like a
texture, is a single, non-indexed, whole-file-is-the-asset format with
no internal cross-referencing of its own that would benefit from a
redundant embedded id; self-consistency is fully covered by the
metadata sidecar's own `assetId` field cross-checked against
`computeAssetId(normalizeLogicalPath(sourceLogicalPath))` at load time
— the exact mechanism `loadTextureAsset()` already uses.

Fixed 32-byte record (magic `"ATLMAT\0\0"`, 8 bytes — distinct from
mesh's `"ATLMESH\0"`, scene's `"ASCN"`, and texture's `"ATLTEX\0\0"`, so
a wrong-artifact-kind reference always fails with `BadMagic`, never
silently decodes, per Spec D7's own "wrong-kind reference" analysis):

| Offset | Bytes | Field | Notes |
|---|---|---|---|
| 0 | 8 | magic | `'A','T','L','M','A','T','\0','\0'` |
| 8 | 4 | schema_version (u32) | `kMaterialArtifactSchemaVersion = 1` |
| 12 | 4 | kind (u32) | 0 = `UnlitTextured`; any other value → `UnknownMaterialKind` |
| 16 | 8 | texture_asset_id (u64) | value-level only, never existence-checked here (D6) |
| 24 | 4 | filter (u32) | 0 = `Nearest`, 1 = `Linear`; else → `UnknownFilter` |
| 28 | 4 | address_mode (u32) | 0 = `Repeat`, 1 = `ClampToEdge`; else → `UnknownAddressMode` |

Total: 32 bytes exactly (`kMaterialArtifactHeaderSizeBytes`), no
variable-length payload. `decodeMaterialArtifact()` rejects `bytes.size()
< 32` as `TruncatedHeader` and `bytes.size() != 32` (i.e., any trailing
bytes) as `UnexpectedSize` — unlike the texture artifact, whose declared
`pixel_data_size` legitimately varies, Material's record size is fixed
by schema version alone, so any size other than exactly 32 is corruption.
Every multi-byte field assembled by explicit shift/mask (`appendU32LE`/
`appendU64LE`/`readU32LE`/`readU64LE`, duplicated file-locally, matching
every other artifact file's own "duplicated, not shared" precedent for
these exact four helpers).

Metadata sidecar (`src/asset_system/include/atlantis/asset_system/material_metadata.h`),
mirroring `TextureMetadata`'s flat, versioned, anchored-prefix, 5-line
text grammar:

```
atlantis_material_metadata_version: 1
asset_id: <u64>
source_logical_path: <path>
kind: unlit_textured
texture_asset: <u64>
```

```cpp
struct MaterialMetadata {
  AssetId assetId = 0;
  std::string sourceLogicalPath;
  MaterialKind kind = MaterialKind::UnlitTextured;
  AssetId textureAsset = 0;
};
[[nodiscard]] atlantis::Result<MaterialMetadata, MetadataParseError> parseMaterialMetadata(std::string_view);
[[nodiscard]] std::string serializeMaterialMetadata(const MaterialMetadata&);
```

`loadMaterialAsset()` cross-checks: metadata's re-derived `AssetId`
(`computeAssetId(normalizeLogicalPath(sourceLogicalPath))`) equals its
own stored `assetId`; metadata's `kind`/`textureAsset` equal the
artifact's own decoded `kind`/`texture_asset_id` — any disagreement is
`MetadataArtifactMismatch`, exactly `loadTextureAsset()`'s own
discipline.

### P4. Material authoring grammar, cook, and load — implements Spec D1/D2/D6

`src/asset_system/include/atlantis/asset_system/material_source.h`
(new), a flat, fixed-line-order, versioned text grammar (mirroring
`mesh_source.cpp`/`scene_source.cpp`'s own anchored-prefix style, no
general parser library) — a `.material.txt` authoring file, exactly 5
lines:

```
atlantis_material_source_version: 1
kind: unlit_textured
texture: <logical path>
filter: linear
address_mode: repeat
```

```cpp
struct ParsedMaterialSource {
  MaterialKind kind = MaterialKind::UnlitTextured;
  std::string textureLogicalPath;
  MaterialSamplerFilter filter = MaterialSamplerFilter::Linear;
  MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::Repeat;
};
[[nodiscard]] atlantis::Result<ParsedMaterialSource, MaterialSourceParseError> parseMaterialSource(std::string_view);
[[nodiscard]] std::string serializeMaterialSource(const ParsedMaterialSource&);
```

`cookMaterial()` (`src/asset_system/include/atlantis/asset_system/cook_material.h`/`.cpp`),
mirroring `cookScene()`'s own "reads the source file itself" shape
(closer precedent than `cookTexture()`, since this source is
plain text, not pre-decoded binary pixels):

```cpp
[[nodiscard]] atlantis::Result<std::monostate, MaterialCookError> cookMaterial(
    const std::string& sourceFilePath, const std::string& artifactOutputPath,
    const std::string& metadataOutputPath);
```

Steps: read + `parseMaterialSource()` (→ `SourceParseFailed` on any
grammar error); normalize the texture logical path via
`normalizeLogicalPath()` (→ `LogicalPathInvalid` on failure, matching
`cookTexture()`'s own Human-Review-corrected precedent exactly);
`computeAssetId()` on the normalized path (never an existence check,
D6); reject an unrecognized `kind`/`filter`/`address_mode` token
(`UnknownMaterialKind` — the grammar-level `MaterialSourceParseError`
variants already reject `filter`/`address_mode` typos before this point,
so this specific enumerator is reserved for `kind` only, the one field
whose vocabulary this Spec's own D2/D15 boundary keeps deliberately
closed and likely to grow); encode + atomic write (temp-then-`rename()`,
duplicated `writeBytesAtomically()`/`writeTextAtomically()` helpers,
matching `cook_scene.cpp`'s own file-local, not-shared precedent).

`loadMaterialAsset()` (`src/asset_system/include/atlantis/asset_system/load_material.h`/`.cpp`):

```cpp
[[nodiscard]] atlantis::Result<MaterialAssetData, MaterialLoadError> loadMaterialAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath);
```

Mirrors `loadTextureAsset()`'s own shape exactly: read both files (→
`ArtifactFileUnreadable`/`MetadataFileUnreadable`), decode + parse (→
`ArtifactDecodeFailed`/`MetadataParseFailed`), cross-check (→
`MetadataArtifactMismatch`), return `MaterialAssetData`. Names no RHI
type.

**Determinism:** a new `material` case in the existing
`cooker_determinism_tests.cpp` (cook the same source twice, byte-compare
both outputs), matching mesh/scene/texture's own existing coverage.

### P5. `atlantis_add_material_asset()` and the `atlantis_add_texture_asset()` fix — implements Spec Requirements ("a new `atlantis_add_material_asset()` CMake function")

New CMake function, `src/asset_system/CMakeLists.txt`, mirroring
`atlantis_add_scene_asset()`'s shape (single dependency kind instead of
a list, since a Material references exactly one texture — ADR-0059's
own closed DTO shape):

```cmake
function(atlantis_add_material_asset)
  set(options "")
  set(oneValueArgs NAME SOURCE TEXTURE)
  set(multiValueArgs "")
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  set(asset_root "${CMAKE_SOURCE_DIR}/assets")
  set(source_path "${asset_root}/${ARG_SOURCE}")
  set(output_dir "${CMAKE_BINARY_DIR}/assets")
  string(REGEX REPLACE "\\.material\\.txt$" "" base "${ARG_SOURCE}")
  set(artifact_path "${output_dir}/${base}.amaterial")
  set(metadata_path "${output_dir}/${base}.amaterial.meta.txt")
  set(stamp "${output_dir}/${base}.stamp")

  if(NOT DEFINED ATLANTIS_${ARG_TEXTURE}_ARTIFACT_PATH)
    message(FATAL_ERROR
      "atlantis_add_material_asset(${ARG_NAME}): TEXTURE '${ARG_TEXTURE}' is not a "
      "previously declared texture asset -- declare it via atlantis_add_texture_asset() first.")
  endif()

  add_custom_command(
    OUTPUT "${stamp}"
    BYPRODUCTS "${artifact_path}" "${metadata_path}"
    COMMAND atlantis_asset_cooker
      --kind=material --source=${source_path} --asset-root=${asset_root}
      --output-dir=${output_dir} --stamp=${stamp}
    DEPENDS ${source_path} atlantis_asset_cooker
    COMMENT "Cooking material asset: ${ARG_NAME} (${ARG_SOURCE})"
    VERBATIM
  )
  add_custom_target(${ARG_NAME}_asset ALL DEPENDS "${stamp}")
  # Ordering only, matching atlantis_add_scene_asset()'s own
  # MESH_DEPENDENCIES rationale exactly: the texture's own content does
  # not affect this material's own cooked bytes (only its AssetId,
  # resolved from the source file's own texture= logical-path string,
  # already known at configure time) -- this add_dependencies() exists
  # so the full build graph is consistent, not to mark this material's
  # stamp stale on a texture content edit.
  add_dependencies(${ARG_NAME}_asset ${ATLANTIS_${ARG_TEXTURE}_TARGET})

  set_property(GLOBAL APPEND PROPERTY ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS "${ARG_SOURCE}")
  set(ATLANTIS_${ARG_NAME}_ARTIFACT_PATH "${artifact_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_METADATA_PATH "${metadata_path}" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_TARGET "${ARG_NAME}_asset" PARENT_SCOPE)
  set(ATLANTIS_${ARG_NAME}_LOGICAL_PATH "${ARG_SOURCE}" PARENT_SCOPE)
endfunction()
```

**Fix to `atlantis_add_texture_asset()` (Plan Review item 1):** add
`set(ATLANTIS_${ARG_NAME}_LOGICAL_PATH "${ARG_SOURCE}" PARENT_SCOPE)`
immediately after its existing three `PARENT_SCOPE` lines (line 232) —
additive only, no existing caller's behavior changes (every existing
caller simply gains one more unused-by-them exported variable, exactly
the precedent `atlantis_add_static_mesh_asset()`'s own doc comment
already states for its own `_LOGICAL_PATH` addition).

### P6. Scene grammar v2 — implements Spec D5, ADR-0060 Decision item 2

`atlantis_scene_source_version: 1 → 2`. New `node:` line token-count
case, **13** (mesh + material — a case the real grammar does not accept
today, confirmed in Pre-draft verification): tokens[11] = `mesh=<path>`
(unchanged), tokens[12] = `material=<path>` (new). 12-token (mesh only)
and 14-token (camera only) cases are unchanged in shape; `material=` is
never valid without `mesh=` in the same line (grammar-structural, not a
runtime check — there is no token arrangement that produces
"material without mesh"). `ParsedSceneNode` gains
`std::optional<std::string> materialLogicalPath;`. No new
`SceneSourceParseError` enumerator — a malformed 13-token line falls
into the existing `InvalidComponentGroup` (wrong prefix) or
`MissingField` (empty path) cases exactly as the 12-token case already
does, matching Spec D6's own "reuse where the failure mode is identical
in kind" discipline.

`cook_scene.cpp`'s existing per-node loop (line 194-198) extends: when
`parsedNode.materialLogicalPath.has_value()` (only ever set alongside
`meshLogicalPath`, per the grammar), normalize + `computeAssetId()` it
exactly like the mesh reference, setting `node.renderable =
DecodedRenderable{meshAssetId, materialAssetId}`. No existence check
(D6, unchanged).

`SceneCookError`/`SceneArtifactDecodeError` (`errors.h`) — no new
enumerator for cook-time; one new enumerator for decode-time (below,
P7), matching the "reuse the mesh-shaped ones for material's own
identical-in-kind failure, add new only where the failure is genuinely
new" discipline.

**Repository-wide version-1 sweep, mandatory and exhaustively verified —
not merely grepped (Spec D5's own explicit "mirror Spec 0017's own
Milestone 1 five-file lesson"):** every `.scene.txt` file under
`assets/`, and every embedded scene-source-literal string in `tests/`,
must move to `atlantis_scene_source_version: 2` in the same commit that
changes the parser. Milestone 5 performs this sweep by running the full
test suite after the grammar change, not by a static grep alone —
Spec 0017's own `load_tests.cpp` lesson (a hardcoded-version test string
invisible to a naive search of the word "material") applies identically
here to any embedded `atlantis_scene_source_version: 1` literal.

### P7. Scene artifact v2 — implements Spec D5, ADR-0060 Decision item 2

`kSceneArtifactSchemaVersion: 1 → 2`. Per-node record widens from 72 to
84 bytes (`kSceneArtifactNodeRecordSizeBytes`), inserting a new,
12-byte material slot immediately after the existing renderable slot,
before the parent slot:

| Offset | Bytes | Field | Status |
|---|---|---|---|
| 0–35 | 36 | transform (9×f32) | unchanged |
| 36–51 | 16 | camera slot | unchanged |
| 52–55 | 4 | has_renderable (u32) | unchanged |
| 56–63 | 8 | mesh_asset_id (u64) | unchanged |
| **64–67** | **4** | **has_material (u32)** | **new** |
| **68–75** | **8** | **material_asset_id (u64)** | **new** |
| 76–79 | 4 | has_parent (u32) | moved from 64 |
| 80–83 | 4 | parent_index (u32) | moved from 68 |

Total 84 bytes. Header (24 bytes) unchanged. `encodeSceneArtifact()`
writes the new fields unconditionally (0/0 when absent, matching every
other optional-slot field's own "always written, flag-gated" style).
`decodeSceneArtifact()` reads them at the new offsets and sets
`node.renderable = DecodedRenderable{meshAssetId, materialAssetId or
nullopt}`.

**New decode-time check, independent of the cooker (mirroring
`hasCycleByIndex()`'s own "never trust a well-formed producer" ethos):**
`has_material != 0 && has_renderable == 0` is a new, distinct corruption
condition — no existing enumerator covers "a structurally-impossible
combination of two independent flags." New enumerator:
`SceneArtifactDecodeError::MaterialWithoutRenderable`.

`scene_types.h`: `DecodedRenderable{AssetId meshAsset = 0; std::optional<AssetId> materialAsset;}`.
`SceneMetadata`'s own `schemaVersion` field updates automatically (it is
already assigned from `kSceneArtifactSchemaVersion` in `cookScene()`, no
structural change to `SceneMetadata` itself).

### P8. `World::Renderable` widening — implements Spec D4/D11, ADR-0060 Decision item 1

`src/world/include/atlantis/world/renderable.h`:
`Renderable{AssetId meshAsset = 0; std::optional<AssetId> materialAsset;}`.
`src/world/src/scene_instantiation.cpp`'s `fromValidatedSceneData()`
gains one more trivial field copy (`n.renderable->materialAsset`),
matching `meshAsset`'s own copy exactly — still infallible, still no
Renderer/RHI type constructed. `Atlantis::World`'s `target_link_libraries`
(`Atlantis::Core` + `Atlantis::AssetSystem`) is unchanged — `AssetId` is
a type it already depends on `Atlantis::AssetSystem` for.

### P9. Manifest, `RuntimeInitError`, and `scene_extraction.h` — implements Spec D6/D7, ADR-0060 Decision item 5

**`atlantis_add_scene_asset()` gains two new `multiValueArgs`:
`MATERIAL_DEPENDENCIES` and `TEXTURE_DEPENDENCIES`**, each looping
exactly like the existing `MESH_DEPENDENCIES` loop (same
`FATAL_ERROR`-if-undeclared check, same `dependency_targets`
accumulation, same `manifest_lines` append using
`ATLANTIS_<dep>_LOGICAL_PATH/ARTIFACT_PATH/METADATA_PATH`) — appended
into the exact same, unchanged, 3-column `manifest_lines` variable, in
whatever order the three loops run (mesh, then material, then texture;
order is irrelevant since `SceneDependencyResolver` sorts by `AssetId`
for lookup, per ADR-0054, confirmed unmodified). `TEXTURE_DEPENDENCIES`
exists precisely so a material's own transitively-referenced texture
resolves through the *same* manifest a scene's mesh references already
use — required per Spec D7 even when the scene itself never directly
names that texture (an explicitly-permitted "declared but only
transitively used" entry, matching `MESH_DEPENDENCIES`'s own existing
tolerance for an unreferenced declared dependency).

**No `SceneManifestError`/`SceneDependencyResolver` change at all** —
confirmed in Pre-draft verification that `find()` is already
kind-agnostic; this Plan adds zero lines to `scene_manifest.h`/`.cpp`.

**`RuntimeInitError` (`init_error.h`): no new enumerator.** Widen the
existing `SceneDependencyUnresolved`/`SceneDependencyLoadFailed` doc
comments from "a referenced AssetId has no resolver entry" / "a
resolved AssetId's own mesh load failed" to kind-agnostic wording ("...
own mesh, material, or texture load failed") — the real call sites
(`resolver.find(id)` returning `nullptr`; a load call failing) are
already identical regardless of which asset kind's `AssetId` triggered
them (Plan Review item 3). This is the more disciplined choice under
Spec D6's own "add a new enumerator only for a genuinely new failure
mode" rule — a material/texture dependency failure is not a new failure
mode, only a new *source* of an already-covered one.

**`scene_extraction.h`/`.cpp`:** add `UnresolvedMaterialAsset` as a
sibling enumerator on the existing `SceneExtractionError` (same enum,
not a new family — this is the *same* per-entity, per-frame resolution
failure class `UnresolvedMeshAsset` already names, applied to a
material `AssetId` instead of a mesh one) and:

```cpp
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> resolveMaterialAsset(
    atlantis::asset_system::AssetId requested, const std::vector<atlantis::asset_system::AssetId>& knownIds);
```

verbatim-shaped after `resolveMeshAsset()` (same linear `std::find`,
same decoupled-from-the-concrete-resource-type rationale).

### P10. `RuntimeApplication` member layout and `BootstrapConfig` — implements Spec D9/D10, ADR-0060 Decision item 9/10

**`BootstrapConfig` gains four new fields**, named and populated exactly
like the existing four `minimal_mesh` shader-path fields:

```cpp
std::string unlitTexturedVertexShaderSpirvPath;
std::string unlitTexturedVertexShaderReflectionPath;
std::string unlitTexturedFragmentShaderSpirvPath;
std::string unlitTexturedFragmentShaderReflectionPath;
```

`main.cpp` populates them via a new `ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR`
macro + the literal filenames `textured_quad.{vert,frag}.{spv,refl.json}`
— mirroring the existing `ATLANTIS_RUNTIME_SHADER_DIR` + `minimal_mesh.*`
pattern exactly. No new field is needed for material/texture/scene
paths — `sceneDependencyManifestPath` already covers all three
dependency kinds (P9).

**`RuntimeApplication` member declaration order** — the single
`optional<Material> material_` slot is replaced, in place (same
position: immediately after `depthTexture_`, immediately before
`renderer_`), by seven new members, ordered so C++'s reverse-declaration-
order destruction gives exactly the texture/sampler-before-material,
all-before-`device_` sequence D9 requires:

```cpp
// GPU-realized, AssetId-keyed (destroyed in this reverse order: material
// resources first, then samplers, then textures -- all before device_,
// which stays declared first among GPU-owning members, unchanged).
std::unordered_map<atlantis::asset_system::AssetId, atlantis::rhi::SampledTexture> sampledTextureResourceMap_;
std::unordered_map<atlantis::asset_system::AssetId, atlantis::rhi::Sampler> samplerResourceMap_;
std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Material> materialResourceMap_;
std::optional<atlantis::renderer::Material> fallbackMaterial_;  // renamed from material_; same role, same lazy-build-on-first-format-known-frame timing

// CPU-only, populated by Phase 1, consumed/cleared by Phase 2 as each
// entry is realized -- no GPU handle, so relative order versus the four
// members above is not destruction-order-significant.
std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap_;
std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap_;
```

Every other existing member (`platformSession_` through
`fragmentSpirv_`) keeps its exact current relative position — this Plan
is a pure insertion at `material_`'s old slot, not a reordering of
anything else. `shutdown()`'s existing ordered `.reset()`/`.clear()`
sequence (`runtime_application.cpp:469-474`) widens: `fallbackMaterial_.reset();
materialResourceMap_.clear(); samplerResourceMap_.clear();
sampledTextureResourceMap_.clear();` inserted where `material_.reset();`
used to be, in that order, before `depthTexture_.reset();` — clearing
the maps explicitly in `shutdown()` (rather than relying solely on
destruction order) matches `meshResourceMap_.clear()`'s own existing
explicit-clear precedent, keeping `shutdown()`'s own sequence the single
place that visibly documents the teardown order, not merely an implicit
property of declaration order.

### P11. Phase 1 — CPU-only scene-load transaction extension — implements Spec D8 (Phase 1), ADR-0060 Decision item 6

`SceneLoadOutcome` widens:

```cpp
struct SceneLoadOutcome {
  atlantis::world::World world;
  std::unordered_map<AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
};
```

`loadAndInstantiateScene()` (`scene_load.cpp`) widens steps (c)–(g),
keeping the exact same shape (a `std::vector` + linear `std::find` for
distinct-id collection, matching the existing mesh collection loop
verbatim, not a `std::unordered_set` — consistency with the existing
code's own style over a micro-optimization the Spec does not ask for):

- (c): a second `std::vector<AssetId> distinctMaterialIds`, and — for
  each distinct material — its own texture reference is not yet known
  at this point (it is inside the material's own artifact, not the
  scene's), so texture collection happens in (e), immediately after each
  material loads, not in (c).
- (d): resolve every mesh id (unchanged) **and** every material id via
  the same `resolver`; an unresolved material id returns
  `RuntimeInitError::SceneDependencyUnresolved` (P9), exactly like an
  unresolved mesh id today.
- (e): after each distinct material's own `loadMaterialAsset()` call
  succeeds, resolve and load its own `textureAsset` the same way
  (`resolver.find()` then `loadTextureAsset()`), deduplicated by texture
  `AssetId` into a third local map — two materials naming the same
  texture load it once, matching D10's own free-deduplication claim.
  Both loads return CPU-only DTOs; **no** `SampledTexture`, `Sampler`,
  `Pipeline`, or `Material` is constructed anywhere in this function,
  matching Spec D8 Phase 1's own hard constraint (no `RenderTarget`
  exists yet at this point in `initializeSteps()`).
- (f)/(g): unchanged in kind — `fromValidatedSceneData()` stays
  infallible; the function's own `Result` return, by value, remains the
  transactional boundary.

`RuntimeApplication::initializeSteps()`'s own publish step widens from
two moves to four, each covered by its own `static_assert`:

```cpp
static_assert(std::is_nothrow_move_assignable_v<decltype(std::declval<SceneLoadOutcome>().materialDataMap)>, ...);
static_assert(std::is_nothrow_move_assignable_v<decltype(std::declval<SceneLoadOutcome>().textureDataMap)>, ...);
```

alongside the two existing ones — `std::unordered_map<AssetId, T>`'s
move-assignment is `noexcept` under the default allocator/hash/equality
this map instantiates with regardless of `T`, matching the existing
proof's own reasoning exactly (no new argument needed, only two more
instantiations of it). Any failure anywhere in steps (a)–(e) leaves
Runtime in its existing, already-tested failed-init state — no partial
scene, no partial resource map, ever becomes visible.

### P12. Phase 2 — deferred, per-frame GPU realization: a new Runtime-private, testable module — implements Spec D8 (Phase 2), D9, item 9's testable-boundary requirement

**New files**, added to `atlantis_runtime_host`'s own source list
(`src/runtime/CMakeLists.txt`), matching `scene_extraction.cpp`/
`scene_load.cpp`'s own already-established "factored out for
testability" precedent exactly:

- `src/runtime/include/atlantis/runtime/material_realization.h`
- `src/runtime/src/material_realization.cpp`

This is the direct answer to item 9 and `[Claim f]`: every function
below takes its dependencies as explicit parameters (`Device&`,
`RenderTarget`/`colorFormat`, the current resource maps by reference,
the CPU-only data maps by const reference) — never reads
`RuntimeApplication`'s own private state directly, and is called
identically by `runFrame()` and by Milestone 16's fixture. No new
*public* API is introduced outside `Atlantis::RuntimeHost` — this header
lives under `atlantis::runtime`, the same Runtime-private namespace
`scene_extraction.h` already uses, consumed only by `runtime_application.cpp`
and by test/fixture code that already links `Atlantis::RuntimeHost`
(mirroring `tests/runtime/`'s own existing linkage).

```cpp
namespace atlantis::runtime {

enum class MaterialRealizationError {
  SamplerCreateFailed,
  SampledTextureCreateFailed,
  StagingBufferCreateFailed,
  MaterialCreateFailed,
};

// A single successfully-realized material's own new GPU resources --
// entirely function-local/RAII-owned until the caller decides to keep
// them (Spec 0018 D8 step 2's own "candidate bundle").
struct RealizedMaterialCandidate {
  atlantis::asset_system::AssetId textureAssetId = 0;
  atlantis::rhi::SampledTexture sampledTexture;
  atlantis::rhi::Sampler sampler;
  atlantis::rhi::Buffer stagingBuffer;
  atlantis::renderer::Material material;
};

// Step 2 of Spec 0018 D8: attempts one material's own full realization
// as one local, all-or-nothing sequence. Never touches any
// RuntimeApplication-owned map -- purely functional given its inputs.
// On success, the caller (realizePendingMaterials(), below) is
// responsible for recording the returned candidate's own texture-upload
// RenderGraph pass into the shared CommandList (step 3) before this
// candidate is ever moved into a persistent map.
[[nodiscard]] atlantis::Result<RealizedMaterialCandidate, MaterialRealizationError> realizeOneMaterialCandidate(
    atlantis::rhi::Device& device, const atlantis::shader_system::rhi_integration::MeshVertexAttributeSchema& unlitTexturedVertexSchema,
    const std::vector<std::uint32_t>& unlitTexturedVertexSpirv, const std::vector<std::uint32_t>& unlitTexturedFragmentSpirv,
    atlantis::rhi::Format colorFormat, const atlantis::asset_system::MaterialAssetData& materialData,
    const atlantis::asset_system::TextureAssetData& textureData);

// Step 1 of Spec 0018 D8: the pending set is a pure function of current
// state, recomputed every frame -- never a persisted queue. referencedIds
// is every distinct material AssetId any currently-renderable entity
// names (collected by the caller from World, matching how
// knownMeshAssetIds is already collected in runFrame() today);
// alreadyRealizedIds is materialResourceMap_'s own current key set.
[[nodiscard]] std::vector<atlantis::asset_system::AssetId> computePendingMaterialIds(
    const std::vector<atlantis::asset_system::AssetId>& referencedIds,
    const std::vector<atlantis::asset_system::AssetId>& alreadyRealizedIds);

// Steps 2-3 combined, for every pending id this frame: builds every
// candidate that succeeds, recording each one's upload pass into
// commandList (before the caller's own subsequent draw-graph recording)
// -- never partially recording a candidate that itself failed at any
// sub-step. Returns the set of successfully-realized candidates, keyed
// by material AssetId, for the caller to (a) build this frame's own
// DrawItems from directly (Spec 0018 D8 step 3's own same-frame-visible
// guarantee) and (b), only after this frame's submit()+conditional
// waitIdle() both succeed, move into the persistent resource maps (step
// 6) -- this function itself never touches those maps.
[[nodiscard]] std::unordered_map<atlantis::asset_system::AssetId, RealizedMaterialCandidate> realizePendingMaterials(
    atlantis::rhi::Device& device, atlantis::rhi::CommandList& commandList, /* ...shader/schema params... */
    atlantis::rhi::Format colorFormat, const std::vector<atlantis::asset_system::AssetId>& pendingIds,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>& materialDataMap,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData>& textureDataMap);

}  // namespace atlantis::runtime
```

**Wiring into `runFrame()`:** immediately after the existing
extent-change block (`runtime_application.cpp:315-328`) and before the
existing `if (!material_ || !depthTexture_) return;` guard, insert: (1)
collect `referencedMaterialIds` from `world_->renderableEntities()`
(mirroring the existing `knownMeshAssetIds` collection loop exactly, but
reading `Renderable::materialAsset` instead of `meshAsset`, skipping
absent ones); (2) collect `alreadyRealizedIds` from
`materialResourceMap_`'s own keys; (3) `computePendingMaterialIds()`;
(4) if non-empty, `createCommandList()` **once** (the same
`CommandList` the existing draw-graph recording immediately below
already creates — this Plan does not add a second `createCommandList()`
call, only moves the existing one earlier and records into it twice);
(5) `realizePendingMaterials()`, recording upload passes into that same
`commandList`; (6) proceed to build this frame's `DrawItems` (M14),
consulting both `materialResourceMap_` (already-realized) and this
frame's own local realized-candidates map (newly realized this frame);
(7) `renderer_.drawFrame()` records the draw graph into the same
`commandList`, after the upload passes; (8) **one** `submit()`
(unchanged call site, unchanged signature); (9) if this frame realized
at least one new candidate, `device_->waitIdle()` **before** `present()`
— on success, move every local candidate into
`sampledTextureResourceMap_`/`samplerResourceMap_`/`materialResourceMap_`;
on `waitIdle()` failure, treat with the same severity a `submit()`
failure already has (`lifecycle_.markFailed()`), matching the existing
"an unrecoverable wait is itself an unrecoverable condition" precedent
`shutdown()`'s own `waitIdle()` failure handling already establishes;
(10) `present()` (unchanged call site).

### P13. Format-change atomic rebuild — implements Spec D9

Extends `material_realization.h`/`.cpp` with:

```cpp
struct FormatRebuildCandidates {
  atlantis::renderer::Material fallback;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Material> materials;
};

// Spec 0018 D9 steps 2-4: builds a COMPLETE candidate batch (the
// fallback Material plus one rebuilt Material per CURRENT
// materialResourceMap_ entry, each reusing that entry's own already-
// uploaded SampledTexture/Sampler unchanged -- no new upload, no
// CommandList, no submit()) against the new colorFormat. Returns Err on
// the FIRST sub-failure (fallback or any one entry) -- the caller (
// runFrame()) is responsible for discarding the returned partial state
// via ordinary RAII and leaving the EXISTING fallbackMaterial_/
// materialResourceMap_ completely untouched, exactly as D9 requires; no
// partial candidate is ever returned as if it were usable.
[[nodiscard]] atlantis::Result<FormatRebuildCandidates, MaterialRealizationError> rebuildMaterialsForFormatChange(
    atlantis::rhi::Device& device, /* ...shader/schema params for both the fallback and unlit-textured pipelines... */
    atlantis::rhi::Format newColorFormat,
    const std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Material>& currentMaterials);
```

`runFrame()`'s existing format-change block
(`runtime_application.cpp:293-313`) is rewritten to call this instead of
its own inline `createMaterial()` call: on `Ok`, swap
`fallbackMaterial_`/`materialResourceMap_` with the candidate batch
(old objects destroyed only now, create-before-destroy, matching
today's single-Material ordering) and update `lastSeenFormat_`; on
`Err`, **do not** update `lastSeenFormat_` (retry next frame, unchanged
behavior) and set a new, function-scoped `bool formatRebuildFailedThisFrame
= true` flag.

**Unconditional override (Plan Review item 5, the one behavioral
correction D9 makes):** when `formatRebuildFailedThisFrame` is true,
`runFrame()` builds an **empty** `DrawItems` vector for this frame,
regardless of what the normal per-entity loop (M14) would otherwise
produce — including entities whose `materialAsset` is absent, which
would otherwise still draw with the now-stale-format
`fallbackMaterial_`. `Renderer::drawFrame()` still runs (clearing the
target to the new format, per the existing `if (!material_ ||
!depthTexture_) return;` early-return precedent generalized: a rebuild
failure is "nothing valid to draw this frame," not merely "no camera").

`shutdown()`'s existing clear sequence (P10) already covers the final
teardown of both `fallbackMaterial_` and `materialResourceMap_` in the
correct order; no additional shutdown change is needed for rebuild
specifically.

### P14. Per-entity `DrawItem` material binding — implements Spec D4's three-state semantics, D8's per-entity fallback

`runFrame()`'s existing `DrawItem` loop
(`runtime_application.cpp:397-420`) widens, for each renderable entity:

1. `materialAsset` absent → `item.material = &*fallbackMaterial_;`
   (unchanged behavior, the exact current line).
2. `materialAsset` present, and a key in either this frame's own
   newly-realized-candidates map (M12) or in `materialResourceMap_` →
   `item.material` points at that Material (candidate map checked first,
   since a just-realized material is not yet in `materialResourceMap_`
   this same frame, per D8 step 3's "visible on the very frame it
   succeeds" guarantee).
3. `materialAsset` present, but a key in neither → `continue;` (skip
   this entity for this frame only), using `resolveMaterialAsset()`
   (P9) for the membership check, mirroring `resolveMeshAsset()`'s own
   exact call shape and log-then-`continue`, not `markFailed()`,
   severity.

This three-way branch is subordinate to P13's own unconditional
"rebuild failed this frame → empty `DrawItems`" override, which is
checked first and short-circuits all of the above when true.

### P15. First real material scene assets — implements Spec D12, Requirements item "reuses Spec 0016's already-proven texture/shader"

New files under `assets/`:

- `assets/materials/unlit_textured_quad.material.txt` — `kind:
  unlit_textured`, `texture: textures/textured_quad_source_unorm.png`
  (reusing Spec 0016's own already-cooked, already-proven texture; no
  new PNG).
- `assets/scenes/material_demo.scene.txt` — one or two nodes, each with
  a real `mesh=`/`material=` reference; reuses Spec 0017's own
  UV-carrying mesh asset (no new mesh authored). If two nodes reference
  the **same** material, this is the required proof of D10's own
  dedup claim (a single `loadMaterialAsset()`/`loadTextureAsset()` call
  and a single realized GPU resource, shared).

New CMake declarations (`assets/CMakeLists.txt` or a new
`assets/materials/CMakeLists.txt`, matching the existing
`assets/textures/CMakeLists.txt`-style precedent):

```cmake
atlantis_add_material_asset(
  NAME unlit_textured_quad
  SOURCE materials/unlit_textured_quad.material.txt
  TEXTURE textured_quad_source_unorm  # Spec 0016's own already-declared texture asset target name
)
atlantis_add_scene_asset(
  NAME material_demo_scene
  SOURCE scenes/material_demo.scene.txt
  MESH_DEPENDENCIES <the UV mesh asset's own NAME>
  MATERIAL_DEPENDENCIES unlit_textured_quad
  TEXTURE_DEPENDENCIES textured_quad_source_unorm
)
```

**Explicit decision on Runtime's default bootstrap scene (Spec
Requirements' own "state whether Runtime's default bootstrap scene
switches"): No, it does not switch.** `main.cpp`'s
`config.sceneArtifactPath`/`sceneMetadataPath`/`sceneDependencyManifestPath`
continue pointing at `world_scene` (unchanged) — `material_demo_scene`
is a verification-only asset, declared and cooked but never wired into
`atlantis_runtime`'s own `BootstrapConfig` population or its
`add_dependencies()` list. This keeps `atlantis_runtime.exe`'s own
windowed output completely unchanged by this Plan (no new manual
verification item beyond what D13 already requires), while still
exercising the real, production `Atlantis::RuntimeHost` code through
Milestone 16's own fixture. If a future Spec wants the shipped Runtime
itself to boot into a textured scene, that is an explicit, separate,
disclosed decision for that Spec — not an incidental side effect of this
one.

### P16. Verification fixture, golden, and negative proofs — implements Spec D12

New file, `tests/image_regression/fixture/material_demo_fixture.cpp`,
**directly linking and calling** `Atlantis::RuntimeHost`'s real
`loadAndInstantiateScene()` (Phase 1) and the real
`computePendingMaterialIds()`/`realizePendingMaterials()` (Phase 2, P12)
— not a fixture-private reimplementation of either, resolving D12's own
disclosed risk (`[Claim f]`, confirmed feasible via
`Device::createOffscreenTarget()`, already used by
`textured_quad_fixture.cpp` for exactly this "real `RenderTarget`, no
window" purpose). Golden captured per ADR-0042's "Initial baseline
bootstrap" category (Milestone 16 below).

## Milestones / Task Breakdown

Each milestone is independently buildable and independently reviewable;
later milestones depend only on earlier ones, never the reverse.

1. **Material CPU types + error domain (P1, P2).** `material_types.h`;
   `errors.h` additions (`MaterialCookError`, `MaterialArtifactDecodeError`,
   `MaterialLoadError`); `AssetKind::Material` added to the Tools
   cooker's enum (dispatch wired in Milestone 4, not yet here). No
   behavior yet — a pure-declaration milestone, compiles standalone.
2. **Material source grammar (P4).** `material_source.h`/`.cpp`
   (`MaterialSourceParseError`, `parseMaterialSource()`/
   `serializeMaterialSource()`). Unit tests: round-trip, each of the 7
   `MaterialSourceParseError` cases exercised with a real malformed
   input string (mirroring `scene_source_tests.cpp`'s own per-enumerator
   coverage style).
3. **Material artifact codec + metadata sidecar (P3).**
   `material_artifact.h`/`.cpp`, `material_metadata.h`/`.cpp`. Unit
   tests: encode→decode round trip; every `MaterialArtifactDecodeError`
   case (bad magic, wrong version, truncated, oversized/`UnexpectedSize`,
   unknown kind/filter/address_mode) driven by a real corrupted byte
   buffer, not a mocked one; metadata parse/serialize round trip.
4. **`cookMaterial()`/`loadMaterialAsset()` + Tools cooker dispatch
   (P4).** `cook_material.h`/`.cpp`, `load_material.h`/`.cpp`;
   `cook_command.cpp`'s `switch` gains `case AssetKind::Material:` (a
   build error without it, confirmed no `default` exists); new
   `runCookMaterialMode()`. Tests: full cook→load round trip against a
   real texture asset already on disk; determinism (`cooker_determinism_tests.cpp`
   gains a `material` case); every `MaterialCookError` case.
5. **`atlantis_add_material_asset()` + the `atlantis_add_texture_asset()`
   fix (P5).** CMake-only milestone: declare one throwaway test material
   asset (e.g. under `tests/tools/asset_cooker/fixtures/` or reusing an
   existing test texture) purely to prove the CMake plumbing builds
   end to end, including the `TEXTURE` not-previously-declared
   `FATAL_ERROR` path (a deliberate negative CMake-configure test).
6. **Scene grammar v2 + repository-wide version sweep (P6).**
   `scene_source.h`/`.cpp` (13-token case, version bump). Full-repo
   sweep: every `.scene.txt` under `assets/` and every embedded
   `atlantis_scene_source_version: 1` literal across `tests/` updated in
   this same commit — enumerated exhaustively by grep **and** confirmed
   complete by running the full test suite (Spec D5's own explicit
   requirement), not by the grep alone.
7. **Scene artifact v2 (P7).** `scene_artifact.h`/`.cpp` (84-byte
   record, new offsets, `MaterialWithoutRenderable`), `scene_types.h`
   (`DecodedRenderable` widening), `cook_scene.cpp` (material-reference
   resolution extension). Tests: encode→decode round trip at the new
   size; version-1 artifact rejected outright; `MaterialWithoutRenderable`
   driven by a real hand-crafted corrupted byte buffer (has_material=1,
   has_renderable=0) — a case `cookScene()` itself can never produce, so
   this test exists specifically to prove `decodeSceneArtifact()`'s own
   independent check, not merely exercise a reachable path.
8. **`World::Renderable` widening (P8).** `renderable.h`,
   `scene_instantiation.cpp`. Test: a scene with a material reference
   instantiates into a `World` whose `Renderable::materialAsset` is set;
   confirm `Atlantis::World`'s own `target_link_libraries` is unchanged
   (a link-graph boundary check, matching the existing include-scanning
   test's own precedent).
9. **Manifest wiring + `RuntimeInitError`/`scene_extraction.h` (P9).**
   `atlantis_add_scene_asset()`'s `MATERIAL_DEPENDENCIES`/
   `TEXTURE_DEPENDENCIES`; `init_error.h` doc-comment widening (no
   enumerator change); `scene_extraction.h`/`.cpp`'s
   `resolveMaterialAsset()` + `UnresolvedMaterialAsset`. Tests:
   `resolveMaterialAsset()` unit tests mirroring `resolveMeshAsset()`'s
   own exactly; a CMake-configure-time test scene declaring
   `MATERIAL_DEPENDENCIES`/`TEXTURE_DEPENDENCIES` and confirming the
   generated manifest file's own 3-column content.
10. **`BootstrapConfig` + shader production promotion (P10, and Spec
    D3).** Four new `BootstrapConfig` fields; `main.cpp` population;
    move `add_subdirectory(shaders/textured_quad)` from inside
    `if(ATLANTIS_BUILD_TESTS)` (root `CMakeLists.txt` line 111) to
    immediately after `shaders/minimal_renderer` (line 79), before
    `src/runtime` (line 88); `src/runtime/CMakeLists.txt` gains
    `ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR` and
    `add_dependencies(atlantis_runtime ... textured_quad_shaders)`.
    **Verification specific to this milestone:** a clean configure+build
    with `-DATLANTIS_BUILD_TESTS=OFF` succeeds and produces a working
    `atlantis_runtime.exe` — the direct, real proof Spec D3's own gap
    finding is closed, not merely argued.
11. **`RuntimeApplication` member layout + Phase 1 extension (P10,
    P11).** The seven-member replacement of `material_`; `SceneLoadOutcome`
    widening; `loadAndInstantiateScene()`'s steps (c)-(e) extension;
    `initializeSteps()`'s publish-step widening with the two new
    `static_assert`s. Tests: a scene with an unresolvable material
    `AssetId` fails scene load fatally (`SceneDependencyUnresolved`) —
    never a silent fallback (D4 case 2); a scene with a resolvable but
    unloadable material (corrupted artifact) fails scene load fatally
    (`SceneDependencyLoadFailed`); two entities referencing the same
    material `AssetId` produce exactly one `materialDataMap_`/
    `textureDataMap_` entry each (dedup, D10) — all via
    `loadAndInstantiateScene()` called directly with `device = nullptr`
    for the CPU-only failure paths, matching its own existing
    GPU-independent-test precedent.
12. **Phase 2 deferred GPU realization (P12).**
    `material_realization.h`/`.cpp` (`realizeOneMaterialCandidate()`,
    `computePendingMaterialIds()`, `realizePendingMaterials()`);
    `runFrame()` wiring. GPU-dependent tests (tagged `gpu`, matching this
    codebase's existing `ctest -L gpu` convention): a pending material
    realizes exactly once and becomes drawable the same frame (D8 step
    3); a `SamplerCreateFailed`/`SampledTextureCreateFailed`/
    `StagingBufferCreateFailed`/`MaterialCreateFailed` injected failure
    (via a deliberately-invalid parameter, e.g. an oversized dimension)
    leaves the material pending, retried next frame, never fatal; an
    already-realized material is never re-uploaded on a subsequent frame
    (a GPU/Validation-Layers-observable proof, e.g. asserting the
    staging `Buffer`'s own creation call count via a test-only counting
    seam, mirroring how existing GPU tests already assert call counts
    where needed).
13. **Format-change atomic rebuild (P13).**
    `rebuildMaterialsForFormatChange()`; `runFrame()`'s format-change
    block rewritten to call it; the unconditional
    empty-`DrawItems`-on-rebuild-failure override. GPU tests: a
    successful format change rebuilds the fallback and every existing
    map entry together, reusing their own unchanged `SampledTexture`/
    `Sampler` (asserted via the same call-count seam — zero new
    `createSampledTexture()`/`createSampler()` calls during a rebuild);
    an injected rebuild failure (e.g. a deliberately-invalid
    `colorFormat`) leaves the *existing* fallback/map completely
    untouched (object identity/address-stable, not merely
    value-equal) and produces an empty-`DrawItems` frame — the direct,
    GPU-observable proof of the D9 fix (a frame that would, before this
    fix, have drawn with a format-mismatched `Pipeline`, now draws
    nothing instead).
14. **Per-entity `DrawItem` binding (P14).** `runFrame()`'s `DrawItem`
    loop widening. GPU test: a three-entity scene (one absent
    `materialAsset`, one present-and-realized, one present-and-
    deliberately-never-resolvable) produces exactly two `DrawItem`s
    (the third silently skipped, logged) every frame after the second
    entity's own material realizes.
15. **First real material scene assets (P15).** New
    `.material.txt`/`.scene.txt` sources; new
    `atlantis_add_material_asset()`/`atlantis_add_scene_asset()`
    declarations; explicit confirmation (by inspection of `main.cpp`,
    unchanged) that Runtime's own default bootstrap scene stays
    `world_scene`.
16. **New fixture + first golden + negative proofs (P16, Spec D12).**
    `material_demo_fixture.cpp`, directly linking `Atlantis::RuntimeHost`
    and calling the real Phase 1/Phase 2 functions against a real
    `Device::createOffscreenTarget()`-produced `RenderTarget`. Golden
    capture follows ADR-0042's "Initial baseline bootstrap" procedure:
    direct human visual review of the captured PNG (non-black frame,
    correct texture orientation, correct material binding), the build-
    time comparator's own self-consistency check, and a real GPU/
    Validation-Layers-clean run — performed only **after** the full
    Implementation and scene assets are already committed, with the
    golden PNG + sidecar landing in their own, separate, subsequent
    commit whose `source_revision` sidecar field names that prior
    implementation commit's own hash. Required negative proofs, each a
    real, executed test that must fail: removing the scene node's
    `material=` reference changes the captured frame (untextured
    fallback) from the golden; corrupting the cooked material artifact's
    own `texture_asset_id` field changes the captured frame
    (wrong-texture) from the golden; a fixture-local hand-built
    `Material` bypassing the real asset path is confirmed absent from
    this new fixture's own source (a code-review-level check, not a
    runtime one). `minimal_cube`/`world_scene`/`textured_quad`'s own
    existing golden PNG+sidecar files are confirmed byte-for-byte
    unchanged on disk (`git diff` evidence) both before and after this
    milestone.
17. **Full verification matrix + documentation closeout.** Execute the
    complete Verification Checklist below; update
    `specs/README.md`'s Spec 0018 row and this Plan's own status
    language to state "Implementation PR OPEN" (never "merged") until a
    human actually merges it.

## Files / Modules Touched (expected)

**New — `Atlantis::AssetSystem`:**
`src/asset_system/include/atlantis/asset_system/{material_types.h,
material_source.h, material_artifact.h, material_metadata.h,
cook_material.h, load_material.h}`,
`src/asset_system/src/{material_source.cpp, material_artifact.cpp,
material_metadata.cpp, cook_material.cpp, load_material.cpp}`.

**Modified — `Atlantis::AssetSystem`:**
`errors.h` (new enums, P2); `scene_source.h`/`.cpp` (P6);
`scene_artifact.h`/`.cpp` (P7); `scene_types.h` (P7); `cook_scene.cpp`
(P6/P7); `CMakeLists.txt` (`atlantis_add_material_asset()`,
`atlantis_add_texture_asset()`'s `_LOGICAL_PATH` fix,
`atlantis_add_scene_asset()`'s two new dependency args — P5/P9).

**New — `Atlantis::Tools`:** none (extends existing
`src/tools/asset_cooker/cook_command.{h,cpp}` only).

**Modified — `Atlantis::Tools`:** `cook_command.h`/`.cpp`
(`AssetKind::Material`, `runCookMaterialMode()` — P4).

**Modified — `Atlantis::World`:** `renderable.h`,
`scene_instantiation.cpp` (P8).

**New — `Atlantis::RuntimeHost`:**
`src/runtime/include/atlantis/runtime/material_realization.h`,
`src/runtime/src/material_realization.cpp` (P12/P13).

**Modified — `Atlantis::RuntimeHost`:** `runtime_application.h`/`.cpp`
(P10/P12/P13/P14); `scene_load.h`/`.cpp` (P11); `bootstrap_config.h`
(P10); `init_error.h` (doc comments only, P9); `scene_extraction.h`/`.cpp`
(P9); `CMakeLists.txt` (new source file, new `target_compile_definitions`,
new `add_dependencies` — P10/P12).

**Modified — `Atlantis::Runtime` (executable):** `main.cpp` (P10).

**Modified — build/shader:** root `CMakeLists.txt` (P10, D3 shader
promotion); `shaders/textured_quad/CMakeLists.txt` unchanged in content
(only its caller's placement changes).

**New — assets:** `assets/materials/unlit_textured_quad.material.txt`,
`assets/scenes/material_demo.scene.txt` (P15).

**Modified — assets:** every existing `.scene.txt` under `assets/`
(version bump, P6).

**New — tests:** `tests/image_regression/fixture/material_demo_fixture.cpp`
+ its own golden (`tests/image_regression/goldens/material_demo.png` +
sidecar, P16); new unit test files for every new Asset System/Runtime
file above, matching this codebase's existing 1:1
implementation-file-to-test-file convention.

**Modified — tests:** every existing test file identified by
Milestone 6's own repository-wide version-1 sweep (exact list finalized
during Implementation, not guessed here — Spec D5's own explicit
instruction).

No new top-level module. No change to `Atlantis::Core`, `Atlantis::RHI`,
`Atlantis::VulkanBackend`, `Atlantis::Renderer`, `Atlantis::RenderGraph`,
`Atlantis::ShaderSystem`'s own public API (confirmed, Spec D14) — only
`Atlantis::ShaderSystem`'s already-existing
`texturedMaterialExpectedDescriptorContract()` is *reused*, not changed.

## Sequencing & Dependencies

Milestones 1–5 (Material Asset, Asset-System-only) have no dependency on
anything Runtime- or World-facing and can be fully implemented and
tested in isolation first. Milestones 6–8 (Scene/World widening) depend
only on Milestone 1's `AssetId`/types being available for signatures,
not on Materials actually being loadable yet. Milestone 9 depends on
Milestones 5 and 7 (needs both `atlantis_add_material_asset()` and the
widened scene artifact to declare a real test manifest). Milestone 10
(shader promotion + `BootstrapConfig`) is independent of Milestones
6–9 and can proceed in parallel once Milestone 1 exists (it needs
`MaterialKind` for the Runtime-private shader mapping, nothing else).
Milestone 11 depends on Milestones 4, 7, 8, 9, 10 (needs a real,
loadable material asset, a widened scene artifact/`World`, the manifest
wiring, and the new `BootstrapConfig` fields all together). Milestones
12–14 depend strictly on 11, in that order (realization before rebuild
before per-entity binding — each is a real precondition for the next
being GPU-testable). Milestone 15 depends on 5, 7, 9, 10 (needs every
CMake declaration mechanism ready). Milestone 16 depends on everything
through 15. Milestone 17 is last, always.

This ordering means the smallest atomic, independently-compilable step
sequence is: Material Asset (1–5) → Scene/World schema (6–9) → Runtime
plumbing (10) → Runtime Phase 1 (11) → Runtime Phase 2 (12–14) → real
content + proof (15–16) → closeout (17) — each step leaves the
repository in a fully building, fully passing state before the next
begins, matching this Plan's own "smallest atomic, independently
reviewable" requirement.

## Verification Checklist

1. Debug build succeeds, `ATLANTIS_BUILD_TESTS=ON`.
2. Release build succeeds, `ATLANTIS_BUILD_TESTS=ON`.
3. Debug build succeeds, `ATLANTIS_BUILD_TESTS=OFF` — and produces a
   working `atlantis_runtime.exe` (Milestone 10's own direct proof of
   the D3 CMake fix).
4. Release build succeeds, `ATLANTIS_BUILD_TESTS=OFF`.
5. `ctest --test-dir build -C Debug -LE gpu` passes in full.
6. `ctest --test-dir build -C Release -LE gpu` passes in full.
7. `ctest --test-dir build -C Debug -L gpu` passes in full, Vulkan
   Validation Layers enabled, zero validation errors/warnings in the
   log.
8. Material source grammar: round-trip + every `MaterialSourceParseError`
   case (Milestone 2).
9. Material artifact: round-trip + every `MaterialArtifactDecodeError`
   case, including `UnexpectedSize` against both an over- and
   under-sized buffer (Milestone 3).
10. Material metadata: round-trip; a hand-edited sidecar whose `asset_id`
    disagrees with its own `source_logical_path` is rejected
    (`MetadataArtifactMismatch`).
11. `cookMaterial()`/`loadMaterialAsset()` full round trip against a
    real texture asset; determinism (two cooks byte-identical);
    re-import triggering (editing the `.material.txt` re-cooks; editing
    the referenced texture's own content does not, per P5's own
    ordering-only `add_dependencies()`).
12. Every `MaterialCookError` case driven by a real malformed/missing
    input, not a mocked one.
13. `AssetKind::Material` dispatch: a positive `/w14062` probe (the
    `switch` in `cook_command.cpp` still has no `default`, confirming
    exhaustiveness is still compiler-enforced) and a negative probe
    (temporarily remove the new `case`, confirm the build fails with
    C4062, then restore it) — mirroring Spec 0017's own C4062 regression
    discipline exactly.
14. Scene grammar v2: version-1 source rejected outright
    (`UnknownSourceVersion`); the new 13-token case round-trips; every
    malformed 13-token variant hits the expected reused enumerator.
15. Repository-wide version sweep completeness: full test suite green
    after the grammar/artifact bump — not merely a clean grep (Spec D5).
16. Scene artifact v2: round-trip at 84 bytes; version-1 artifact
    rejected outright; `MaterialWithoutRenderable` driven by a
    hand-crafted corrupted buffer.
17. `World::Renderable`/`fromValidatedSceneData()`: a material reference
    survives scene→`World` instantiation intact.
18. `Atlantis::World`'s link closure (`Core` + `AssetSystem` only) —
    unchanged, confirmed by the existing include-scanning boundary test.
19. `Atlantis::AssetSystem`'s link closure (`Core` only) — unchanged.
20. Manifest: a scene declaring `MATERIAL_DEPENDENCIES`/
    `TEXTURE_DEPENDENCIES` produces a correct 3-column manifest; a
    declared-but-unreferenced-by-any-node dependency cooks and validates
    cleanly (explicitly not an error, per D7); an undeclared
    `MATERIAL_DEPENDENCIES`/`TEXTURE_DEPENDENCIES` entry fails CMake
    configure with a clear `FATAL_ERROR` (negative test).
21. `resolveMaterialAsset()`: unit tests mirroring `resolveMeshAsset()`'s
    own coverage exactly (present/absent id).
22. Phase 1 CPU transaction: an unresolvable material `AssetId` fails
    scene load fatally (`SceneDependencyUnresolved`), never a silent
    fallback; an unloadable-but-resolvable material fails scene load
    fatally (`SceneDependencyLoadFailed`); two entities sharing one
    material `AssetId` produce exactly one loaded `MaterialAssetData`/
    `TextureAssetData` entry each; a failure anywhere in the chain
    leaves `world_`/every resource map completely unpublished (existing
    `RuntimeApplication` state unchanged) — all via direct
    `loadAndInstantiateScene()` calls with `device = nullptr` where
    GPU-independent, matching its own existing testability contract.
23. Phase 1 publish atomicity: the two new `static_assert`s compile
    (i.e., `materialDataMap`/`textureDataMap`'s move-assignment is
    genuinely `noexcept`) — a compile-time-enforced regression guard,
    not a runtime test.
24. Phase 2 realization (GPU): a pending material realizes and becomes
    drawable the same frame it is first referenced; each of
    `SamplerCreateFailed`/`SampledTextureCreateFailed`/
    `StagingBufferCreateFailed`/`MaterialCreateFailed` is independently
    driven by a real injected failure and leaves the material pending
    (never fatal, retried next frame); an already-realized material is
    never re-uploaded on a later frame (asserted via a real creation-
    call-count seam).
25. Format-change rebuild (GPU): a successful rebuild swaps the fallback
    and every map entry together, reusing existing
    `SampledTexture`/`Sampler` unchanged (zero new upload calls); an
    injected rebuild failure leaves the *existing* (pre-rebuild)
    fallback/map objects address-identical and untouched, and produces
    an empty-`DrawItems` frame for that one frame only, retried
    successfully the next; this is re-run for **both** the
    old→new-format and new→old-format directions (Spec item 12's own
    "both current and new format" requirement).
26. Per-entity `DrawItem` binding (GPU): the three-state fixture
    (absent/realized/never-resolvable) produces exactly the expected
    `DrawItem` count every frame.
27. `main.cpp`'s own bootstrap scene is confirmed, by inspection,
    unchanged (`world_scene`, not `material_demo_scene`) — Runtime's
    windowed output is bit-for-bit unaffected by this Plan.
28. Windowed smoke test (programmatic resize/minimize/restore/close via
    real Win32 message injection), Debug and Release — unchanged in kind
    from every prior Plan.
29. Manual, human-performed Runtime windowed visual confirmation
    (`atlantis_runtime.exe`, both Debug and Release) — confirms the
    window's own visible output is unchanged (still `world_scene`,
    untextured, exactly as before this Plan), since Milestone 15
    explicitly does not switch the bootstrap scene.
30. New fixture (`material_demo_fixture.cpp`) directly links and calls
    `Atlantis::RuntimeHost`'s real Phase 1/Phase 2 functions — confirmed
    by code inspection, not merely by the fixture passing (a fixture
    could pass while secretly reimplementing the logic; this line item
    is a review gate, not a test assertion).
31. First golden capture, ADR-0042 "Initial baseline bootstrap"
    procedure: implementation + scene assets committed first; golden
    PNG + sidecar in their own, separate, subsequent commit; sidecar's
    `source_revision` names the prior implementation commit's own hash;
    a human visually confirms non-black frame, correct texture
    orientation, correct material binding; a real GPU/Validation-Layers
    run is clean.
32. Negative proof: removing the scene's `material=` reference changes
    the captured frame from the golden (untextured fallback) — executed
    and confirmed to actually fail/differ, not merely asserted possible.
33. Negative proof: corrupting the cooked material artifact's own
    `texture_asset_id` changes the captured frame (wrong texture) from
    the golden — executed and confirmed.
34. `minimal_cube`, `world_scene`, `textured_quad` goldens (PNG +
    sidecar) confirmed byte-for-byte unchanged on disk via `git diff`,
    both immediately before Milestone 16 and again at the end of
    Implementation — evidence, not an inference from "tests passed"
    (Spec 0017's own V35 precedent, reapplied).
35. Module/link-graph boundary checks (existing include-scanning
    mechanism): `Atlantis::AssetSystem` still Core-only; `Atlantis::World`
    still Core+AssetSystem-only; no new dependency edge from either onto
    `Atlantis::Renderer`/`Atlantis::RHI`/`Atlantis::VulkanBackend`.
36. Documentation closeout: `specs/README.md`'s Spec 0018 row and this
    Plan's own status both state "Implementation PR OPEN," never
    "merged," until a human actually merges it.

## Rollback Plan

Every change in this Plan is additive at the format level (a new asset
kind, a new optional field, a version bump with outright rejection of
the old version — never a silent reinterpretation) and isolated at the
module level (no existing public API of `Atlantis::Renderer`/
`Atlantis::RHI`/`Atlantis::RenderGraph`/`Atlantis::ShaderSystem`
changes). If Implementation needs to be reverted after merge:

- Revert the Implementation PR wholesale (`git revert`, not a manual
  unpick) — every new file this Plan adds is new, so a straight revert
  cleanly removes them; every modified file's own change is additive
  (new fields/enumerators/CMake args), so a straight revert cleanly
  restores the prior, already-working state.
- `assets/`'s own version-1 `.scene.txt` sources are never deleted by
  this Plan, only re-authored to version 2 in place — a revert restores
  the version-1 text exactly, and the existing (unmodified by this
  Plan) parser/artifact code already handles it correctly, since that
  code is *also* reverted together.
- The three existing goldens are never touched by this Plan's own
  Implementation commits (only Milestone 16's own new, separate golden
  commit adds a new file) — a revert of Milestone 16's own commit alone,
  independent of the rest, is sufficient if only the new golden is
  found wrong after merge, with no effect on the other three.
- No data migration, no schema requiring a forward-only transform of
  already-shipped user data exists (Non-Goals: no cross-session
  distribution of any artifact this Spec produces) — a revert has no
  external state to reconcile.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan:

- Every item in the Verification Checklist above is executed and its
  result recorded in the Implementation PR description, not merely
  claimed.
- The three pre-existing goldens are confirmed byte-for-byte unchanged
  by `git diff` evidence attached to the PR, not by test-pass inference
  alone.
- The new `material_demo` golden's own negative proofs (V32/V33) are
  each shown to actually fail/differ when triggered, with the
  before/after frames or diff output attached to the PR.
- `specs/README.md` and this Plan's own status line reflect "Implementation
  PR OPEN" only — no document anywhere in the repository is left
  claiming this work is merged before a human has actually merged it.
