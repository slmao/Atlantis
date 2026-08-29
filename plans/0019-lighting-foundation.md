# Plan: Lighting Foundation

- **Spec:** [specs/0019-lighting-foundation.md](../specs/0019-lighting-foundation.md) (`Approved`)
- **Status:** In Review
- **Author:** slmao

## Pre-draft verification gate

Confirmed directly against real, current `main` at drafting time
(2026-08-29), not assumed from the Spec's own text alone:

- Spec 0019 = `Approved`. ADR-0061/ADR-0062 = `Accepted`.
- [PR #93](https://github.com/slmao/Atlantis/pull/93) (Plan 0020,
  "Mesh Normal Attribute Foundation") is merged into `main`
  (`20cd888`). The mesh normal contract this Plan depends on is real,
  not anticipated: `mesh_artifact.h` defines
  `kMeshArtifactSchemaVersion = 3`, `kMeshArtifactVertexStrideBytes = 44`,
  and the four named offset constants
  (`kMeshArtifactPositionOffsetBytes = 0`,
  `kMeshArtifactColorOffsetBytes = 12`,
  `kMeshArtifactUv0OffsetBytes = 24`,
  `kMeshArtifactNormalOffsetBytes = 32`) — read directly from the file,
  not from memory of Spec 0020's own drafting.
- Spec 0019's own D1 governance gate is therefore satisfied: this Plan
  may be drafted. Human Review has not yet authorized Implementation —
  this document's own **Status** stays `In Review` throughout drafting;
  nothing in this Plan schedules or performs Implementation.

## Objective

Implement Spec 0019 in full: `World` gains a third optional
per-entity component (`Light`, Directional or Point); the Scene Asset
format gains an optional, capped light node; Material gains
`MaterialKind::LitTextured`; Runtime computes a one-time-captured array
of active lights and makes it available, via the existing camera
uniform buffer's newly-fragment-visible binding, to a new
`lit_textured` shader pair that applies exact, Spec-fixed Lambertian
diffuse shading against a real, Spec-0020-sourced vertex normal. A new,
independent image-regression fixture and golden prove the whole path
end to end, distinguishing a Directional light's own contribution from
a Point light's own contribution, with zero change to any of the four
existing goldens.

## Pre-draft verification against real, current source

Every claim below was confirmed by reading the named file in full (or
the cited lines) during this Plan's own drafting — not carried over
from Spec 0019's own Pre-draft verification without re-checking, since
that section explicitly predates Spec 0020's own merge.

### World (`src/world/`)

- `World` (`world.h`/`world.cpp`, both read in full) has exactly two
  optional per-entity components today, `Camera` and `Renderable`,
  each a fixed slot on `Slot` (`std::optional<Camera> camera;`/
  `std::optional<Renderable> renderable;`), with a matching
  `set*()`/`remove*()`/`get*()` triplet and, for `Renderable`, a
  deterministic `renderableEntities() const -> std::vector<EntityId>`
  (ascending slot-index order, a fresh `std::vector` snapshot built by
  one loop over `slots_`, `world.cpp:337-346`). `Camera` has no
  `removeCamera()`-adjacent deterministic-list accessor (Runtime only
  ever needs the *one* active camera, via `activeCamera()`); `Light`
  needs its own accessor mirroring `renderableEntities()` exactly, per
  Spec 0019 D2.
- `World::validate(EntityId)` (`world.cpp:110-121`): checks
  `id.worldIdentity_ != nullptr && id.worldIdentity_ != identity_.get()`
  → `WrongWorld` **first**; then `id.index_ >= slots_.size() ||
  !slots_[id.index_].alive || slots_[id.index_].generation !=
  id.generation_` → `InvalidEntity` **second**. Every accessor
  (`getCamera()`, `world.cpp:290-295`; `getRenderable()`,
  `world.cpp:328-335`) calls `validate()` first, returning its error
  verbatim on failure, and only then checks its own
  `std::optional::has_value()` as a third, final step. `getLight()`
  must follow this identical three-step shape.
- `WorldError` (`world_error.h`, read in full): `{InvalidEntity,
  WouldCreateCycle, NoCameraComponent, WrongWorld,
  NoRenderableComponent}`, five enumerators, each with a one-line doc
  comment. `NoLightComponent` is a sixth, added the same way.
- `Camera`/`Renderable` (`camera.h`/`renderable.h`, both four-to-nine-line
  files): each is its own header, `#pragma once`, no logic — a `Light`
  header follows the identical one-type-per-file shape.
- `destroyEntity()` (`world.cpp:148-183`) resets `slot.alive`/advances
  `slot.generation`/pushes to `freeList_` — it does **not** individually
  reset `camera`/`renderable` (those are simply never read again once
  `alive` is false and the slot is later reused by `createEntity()`,
  which explicitly resets both — `world.cpp:139-140`). `createEntity()`
  must reset `light` (`slot.light.reset();`) alongside `camera`/
  `renderable` the same way.
- `EntityId`/`entity_id.h` (referenced, not re-read in full — unchanged
  by this Plan): index+generation+`WorldIdentity*` triple, `World`'s
  own friend.
- `World`'s own dependency closure (`src/world/CMakeLists.txt`,
  confirmed unchanged by this Plan's own file list below):
  `Atlantis::Core` + `Atlantis::AssetSystem` (for `AssetId` only, named
  in `Renderable`). `Light` introduces no new field naming any
  `AssetId`/Asset-System type, so this closure is untouched.

### Runtime scene extraction (`src/runtime/include/atlantis/runtime/scene_extraction.h`, `src/runtime/src/scene_extraction.cpp`, both read in full)

- `SceneExtractionError` (current): `{NoActiveCamera,
  DegenerateCameraForward, DegenerateCameraBasis, UnresolvedMeshAsset,
  UnresolvedMaterialAsset}`. `DegenerateLightDirection` and
  `NonConformalNormalTransform` are new sixth/seventh enumerators —
  confirmed this is the exact enum Spec 0019 D2/D7/D11 name.
- `extractCameraMatrices()` (`scene_extraction.cpp:88-117`) is the
  **exact, cited formula** this Plan's own `Light` direction/position
  extraction must mirror: `Vec3 negColumn2{-m[8], -m[9], -m[10]}`,
  normalized, is the forward direction (`Directional` light's own
  direction, verbatim); `Vec3 eye{m[12], m[13], m[14]}` is the
  translation column (`Point` light's own position, verbatim). The
  degenerate check is `length(negColumn2) < kDegenerateLengthEpsilon`
  (`kDegenerateLengthEpsilon = 1e-6f`, `scene_extraction.cpp:10`) →
  `DegenerateCameraForward`; the direct analog for `Light` is the
  identical comparison → `DegenerateLightDirection`. This constant is
  **reused verbatim**, not re-derived — matching Spec 0019 D2's own
  "identical formula and sign convention" requirement literally, at
  the constant level, not merely the formula-shape level.
- `resolveMeshAsset()`/`resolveMaterialAsset()`
  (`scene_extraction.cpp:119-133`) are the exact shape a new
  `checkConformalTransform()` (D7) should match: a small, pure,
  `Result`-returning function taking exactly the primitive data it
  needs (here, one `const Mat4&`), called once per candidate entity by
  the caller's own loop — never a `World`-consuming function itself.
- `scene_extraction.h` currently has **zero** `#include` of any
  `atlantis::world::*` header (confirmed: `<atlantis/asset_system/asset_id.h>`,
  `<atlantis/result.h>`, `<array>`, `<variant>`, `<vector>` only) — every
  existing function takes raw `Mat4`/`AssetId` values, never a `World&`
  or an `EntityId`. See P8 below for this Plan's own explicit decision
  on whether the new light-extraction function preserves or breaks this
  property.

### Runtime frame loop, camera buffer, Phase 1/Phase 2 boundary (`src/runtime/src/runtime_application.cpp`, key sections read in full)

- Camera uniform `Buffer`: created once, `initializeSteps()` Step 4
  (`runtime_application.cpp:239-247`),
  `device_->createBuffer({.purpose = BufferPurpose::Uniform, .sizeBytes
  = sizeof(float) * 32})` — 32 floats, view (16) + projection (16), no
  other content today. Destroyed via `cameraBuffer_.reset()` in
  `shutdown()` (`runtime_application.cpp:744`) — plain RAII, no special
  ordering relative to any other member (it is bound generically by
  `Renderer::drawFrame()`, never borrowed by any `Material`).
- Every frame, unconditionally, `runFrame()` (`runtime_application.cpp:449`)
  calls `world_->updateTransforms()` **first** — before camera
  extraction, before the material-realization pending-set computation,
  before every per-entity `DrawItem`-building step. Camera view/
  projection are written into `cameraBuffer_->mappedData()` at
  `runtime_application.cpp:491-493`
  (`cameraData[0..15]` = view, `cameraData[16..31]` = projection),
  **every frame**, unconditionally — this write is completely
  unaffected by this Plan (D9's own "camera portion... continues its
  own existing, unrelated per-frame rewrite").
- `lastSeenFormat_` (a `std::optional<Format>` member, checked at
  `runtime_application.cpp:390`) is the real "has a format-dependent
  resource ever been successfully built" flag — `!lastSeenFormat_.has_value()`
  is true only before the very first successful `submit()`
  (`runtime_application.cpp:669` sets it, only after `submit()` returns
  `Ok`). This is a real, existing precedent for a boolean-flag-style
  "exactly once, on the first successful frame" gate, but it is
  **format-change-specific** — this Plan's own light-capture flag must
  be a **separate**, new member (`lightingDataCaptured_`, a plain
  `bool`, default `false`), never conflated with `lastSeenFormat_`,
  since a *later* format change (a second, different color format) must
  **not** retrigger light capture (D9's own "format-change rebuild —
  explicitly unaffected, no new interaction").
- Phase 1 (mesh/material/texture CPU load) and Phase 2 (deferred GPU
  material realization) both already run, in that order, inside
  `runFrame()` today (`realizePendingMaterials()` call confirmed at
  `runtime_application.cpp:526-` range, following the same pattern
  `material_demo_fixture.cpp` mirrors — see below). The one-time light
  capture is placed immediately after the camera buffer write
  (`runtime_application.cpp:493`), **before** Phase 2's own pending-material
  computation — light capture depends on nothing Phase 2 produces (it
  is a pure function of `World`'s own already-`updateTransforms()`'d
  state), so placing it earliest in the frame keeps the one-time write
  as close as possible to the one, single condition that gates it
  (`world_->updateTransforms()` having just run), matching D9's own
  "immediately after `World::updateTransforms()` first runs."

### Material realization (`src/runtime/include/atlantis/runtime/material_realization.h`, `src/runtime/src/material_realization.cpp`, both read in full) — the one real, concrete widening this Plan requires beyond what Spec 0019's own text states explicitly

- **Confirmed, load-bearing finding, not previously stated in Spec
  0019's own text:** `realizeOneMaterialCandidate()`,
  `realizePendingMaterials()`, and `rebuildMaterialsForFormatChange()`
  (the entire Spec 0018-built material-realization surface) are each
  **hardcoded to exactly one shader pair** — every parameter and every
  internal `createMaterial()` call names `unlitTexturedVertexInputLayout`/
  `unlitTexturedVertexSpirv`/`unlitTexturedFragmentSpirv` literally, with
  no `MaterialKind`-based branch anywhere (`material_realization.cpp:83-140`,
  `154-229`, `231-` range, all read in full). Every material realized
  through this pipeline today gets the *same* `Pipeline`, regardless of
  its own `MaterialKind`. **This is a real, necessary widening this
  Plan must design — Spec 0019 D8 states the intended *outcome*
  ("Runtime maps it to a new, fixed, built-in `lit_textured` shader
  pair") but does not itself specify *how* the realization functions
  select between two shader pairs; this is exactly the kind of Plan-time
  mechanical closure AGENTS.md expects, not an open architectural
  question requiring a Spec amendment (the outcome — `MaterialKind`
  determines which of two already-Approved shader pairs a realized
  `Material` uses — is already fully decided; only the C++ signature
  shape is Plan-level).** See P6 below for the exact widened signatures.
- `realizeOneMaterialCandidate()`'s own internal `createMaterial()` call
  (`material_realization.cpp:125-137`) passes `materialData` through
  already (needed for `textureAsset`/`filter`/`addressMode`) — so
  `materialData.kind` is already in scope at exactly the point a
  shader-pair `switch` needs to read it. No new parameter is needed to
  make the *selection decision*; only the two new shader-pair parameter
  groups (`lit_textured` layout/vertex-SPIR-V/fragment-SPIR-V) need to
  be threaded in alongside the existing `unlitTextured*` ones.
- `RealizedMaterialCandidate`, `FormatRebuildCandidates`, `MaterialRealizationError`,
  `computePendingMaterialIds()` are all `MaterialKind`-agnostic already
  (they operate on `AssetId`s and already-loaded `MaterialAssetData`,
  never branching on `kind` themselves) — **zero change** to any of
  these four.
- `rebuildMaterialsForFormatChange()` (`material_realization.cpp:231-`
  onward, signature read in full) already takes **two** separate
  shader-pair parameter groups — `fallback*` (the untextured fallback
  Material) and `unlitTextured*` — confirming the "multiple shader
  pairs, selected per-candidate" shape already exists once in this
  exact file, for a different reason (fallback vs. real material, not
  `MaterialKind`-based) — this Plan's own widening to a third
  (`litTextured*`) group is the same *kind* of parameter-list growth,
  not a new kind of complexity.

### Renderer / RHI (`src/renderer/include/atlantis/renderer/draw_item.h`, `material.h`; `src/rhi/include/atlantis/rhi/types.h`, all read in full)

- `DrawItem { const Mesh* mesh; const Material* material; std::array<float, 16> objectToWorld; }`
  — confirmed unaffected: this Plan adds zero new `DrawItem` field,
  matching D9's "no new push-constant range."
- `Material` (`material.h`) owns exactly one `Pipeline`, plus two
  borrowed, optional `sampledTexture`/`sampler` pointers — confirmed
  **zero** change needed: a `LitTextured` `Material` is constructed via
  the exact same `createMaterial(device, PipelineCreateParams, sampledTexture,
  sampler)` call shape every existing `Material` already uses, just
  with different `vertexShader`/`fragmentShader`/`vertexInputLayout`
  fields inside `PipelineCreateParams` (unchanged struct shape,
  `rhi/types.h:186-201`, read in full).

### Vulkan Backend (`src/vulkan_backend/src/vulkan_device.cpp`, exact line confirmed)

- `uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;` —
  **`vulkan_device.cpp:858`**, inside `createPipeline()`, immediately
  preceded by a comment ("Camera uniform binding... vertex stage only")
  that itself needs updating in the same change, per AGENTS.md's
  "update a stale comment in the same change that makes it stale." This
  is the **entire** RHI-internal change D5/ADR-0062 Decision 2
  requires — one line's own value, one comment. The sibling
  `combinedImageSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;`
  (`vulkan_device.cpp:869`) and the push-constant range's own
  `pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;`
  (`vulkan_device.cpp:908`) are both confirmed **untouched** — the
  former is already fragment-visible (unrelated to this change), the
  latter must stay vertex-only (D9's own "no new push-constant range,"
  and the push-constant data itself — `objectToWorld` — is never read
  in the fragment shader).

### Shader System descriptor contract (`src/shader_system/include/atlantis/shader_system/descriptor_contract.h`, `descriptor_contract.cpp`, `src/tools/shader_compiler/compile_and_validate.cpp`, all read in full) — a real ambiguity investigated and resolved, not assumed safe

- `DescriptorBinding { set; binding; type; stage; }` and
  `ReflectionMetadata` are both genuinely **per-stage** — one
  `ReflectionMetadata` instance per compiled entry point
  (`ReflectionMetadata::stage` is a single `ShaderStage` field, not a
  vector), and its own `descriptorBindings` contains only the bindings
  *that stage's own reflection JSON* reports as used.
- **Investigated and resolved:** `validateDescriptorContract()`
  (`descriptor_contract.cpp:27-53`) matches an `expected` entry to an
  `actual` `metadata.descriptorBindings` entry by `(set, binding)`
  only, **not** also by `stage` — in isolation, this would be unsafe
  for `lit_textured`'s own three-entry contract, which the Spec
  requires to declare **binding (0,0) twice**, once per stage
  (`{0,0,UniformBuffer,Vertex}`, `{0,0,UniformBuffer,Fragment}`).
  Confirmed this is **not** a real defect: the *only* caller,
  `validateDescriptorContractForStage()`
  (`compile_and_validate.cpp:130-152`, read in full), always **filters
  the full expected contract down to the entries whose own `.stage`
  matches the stage currently being validated** (`std::copy_if`,
  `compile_and_validate.cpp:141-143`) *before* ever calling
  `validateDescriptorContract()` — so `validateDescriptorContract()`
  itself is **never** called with two same-`(set, binding)` entries in
  the same `expected` list; the ambiguity that would matter cannot
  actually arise. `litTexturedExpectedDescriptorContract()` (new,
  mirroring `texturedMaterialExpectedDescriptorContract()`'s own exact
  shape) needs **zero** change to either `validateDescriptorContract()`
  or `validateDescriptorContractForStage()` — both already handle a
  repeated-`(set, binding)`-across-stages contract correctly, by
  construction, not by luck.
- `--expected-contract` accepts two string values today,
  `"minimal-renderer"`/`"textured-material"`
  (`compile_and_validate.cpp:133-140`) — a third, `"lit-textured"`,
  is added the same way, matching Spec 0019 D8's own
  `EXPECTED_CONTRACT lit-textured` CMake value exactly.

### Vertex-input mapping (`src/shader_system/rhi_integration/include/atlantis/shader_system/rhi_integration/vertex_input_mapping.h`, read in full)

- `MeshVertexAttributeSchema { location; offsetBytes; }`,
  `toVertexInputLayout(vertexMetadata, schema, strideBytes)` — confirmed
  generic over attribute *count*: `textured_quad_fixture.cpp`'s own
  precedent (Plan 0020) already builds a two-entry schema
  (`position@0`, `uv@24`); `lit_textured`'s own three-entry schema
  (`position@0`, `uv@24`, `normal@32`, `strideBytes = 44`) needs zero
  change to this function — a direct, mechanical application of an
  already-generic mechanism, exactly as Spec 0019 D8 anticipated
  ("deferred only on Spec 0020's own final byte offsets, never on its
  existence").

### `atlantis::world::fromValidatedSceneData()` (`src/world/src/scene_instantiation.cpp`, read in full) — the exact, single insertion point

- The **entire** function is 58 lines: Pass 1 creates every entity and
  sets `Transform`, then conditionally `setCamera()`
  (`if (n.camera.has_value())`, lines 26-29) and conditionally
  `setRenderable()` (`if (n.renderable.has_value())`, lines 30-34);
  Pass 2 links parents; a final step sets the active camera. A third
  conditional, `if (n.light.has_value()) { world.setLight(id, Light{...}); }`,
  inserted immediately after the existing `setRenderable()` block (same
  shape, same `ATLANTIS_CHECK_MSG` pattern — a freshly-created entity's
  own `setLight()` call can only fail via a precondition this function's
  own caller (`decodeScene()`) already guarantees never holds), is the
  **entire** change this file needs.

### Scene Asset — grammar, cook, artifact (`scene_source.h`/`.cpp`, `scene_types.h`, `cook_scene.cpp`, `scene_artifact.h`/`.cpp`, `validated_scene_data.h`, `errors.h`, all read in full)

- Grammar dispatch (`scene_source.cpp:151-234`): a node's own trailing
  token group is one of exactly `{11, 12, 13, 14}` tokens today —
  `11` (no group), `12` (`mesh=`), `13` (`mesh=`+`material=`), `14`
  (three `camera_*=` tokens) — a hard `tokens.size()` check
  (`scene_source.cpp:152-154`) rejects anything else as
  `InvalidComponentGroup`, then an `if`/`else if` chain
  (`scene_source.cpp:207-234`) dispatches on the exact count. A fifth,
  disjoint shape for `light=` is a direct, mechanical extension of this
  exact mechanism — see P3.
- `ParsedSceneNode`/`ParsedSceneSource`
  (`scene_source.h:27-39`), `DecodedTransform`/`DecodedCamera`/
  `DecodedRenderable` (`scene_types.h`, all flat-field DTOs, **never**
  a nested `Vec3`-shaped field — `DecodedCamera` alone is the simplest
  precedent: three bare `float` fields, no grouping struct). A new
  `DecodedLight` DTO (P2) matches this flat-field convention exactly —
  not `world::Vec3`, not a nested color struct.
  `SceneSourceParseError` (`scene_source.h:47-55`): seven enumerators,
  `{UnknownSourceVersion, MissingField, FieldOrderMismatch,
  MalformedNumber, InvalidParentToken, InvalidComponentGroup,
  TrailingContent}` — `TooManyLights` is an eighth (P3).
- `cookScene()` (`cook_scene.cpp:105-243`) folds **every**
  `SceneSourceParseError` into `SceneCookError::SourceParseFailed`
  (`cook_scene.cpp:119`, `parsedResult.isErr()` → one, undifferentiated
  branch) — confirming Spec 0019 D11's own light-count-cap check
  belongs entirely inside `parseSceneSource()` itself (returning
  `SceneSourceParseError::TooManyLights`), never as a new
  `SceneCookError` enumerator; `cookScene()` needs **zero** new
  enumerator of its own. `cookScene()`'s own per-node loop
  (`cook_scene.cpp:173-217`) is the exact site `node.light =
  parsedNode.light;` (a direct copy, `DecodedLight` needs no
  transformation `DecodedRenderable`'s own logical-path-to-`AssetId`
  resolution requires) is added, mirroring the existing
  `if (parsedNode.camera.has_value())` block shape.
- `ValidatedSceneNode { transform; camera; renderable; }`
  (`validated_scene_data.h:14-18`) — a fourth optional field, `light`,
  is added the same way.
- Scene artifact node record (`scene_artifact.h:22-27`,
  `scene_artifact.cpp:84-249`, both read in full): 84 bytes today,
  eleven fixed fields in a fixed order (position/rotation/scale,
  `has_camera`+3 camera floats, `has_renderable`+`mesh_asset_id`,
  `has_material`+`material_asset_id`, `has_parent`+`parent_index`) — the
  *material* slot (Plan 0018) was inserted **immediately after the
  renderable slot, before the parent slot**
  (`scene_artifact.h:17-20`'s own comment, confirmed against the actual
  byte offsets in `scene_artifact.cpp:114-122`) — this Plan's own light
  slot follows the **identical insertion point**: after material,
  before parent (P4).
- `SceneArtifactDecodeError` (`errors.h:111-136`): sixteen
  enumerators today, the most recent being `MaterialWithoutRenderable`
  (Plan 0018) — added specifically because `hasMaterialFlag != 0 &&
  hasRenderableFlag == 0` is a structurally-impossible-from-the-cooker,
  decode-must-still-reject-it condition
  (`scene_artifact.cpp:207-213`), the exact "never trust a well-formed
  producer" precedent this Plan's own `TooManyLights` (decode-time) and
  `NonFiniteValue`-reused light-field-range checks both directly
  mirror. `hasCycleByIndex()`'s own three-state, O(n) marking algorithm
  (`scene_artifact.cpp:58-80`) is unaffected — light nodes carry no
  parent-graph-relevant data.

### Material Asset (`material_types.h`, `material_artifact.cpp`, both read/confirmed)

- `MaterialKind { UnlitTextured }` — one enumerator; `kindToField()`
  (`material_artifact.cpp:31-`, a `switch`) and the decode `if
  (kindField == 0) ... else UnknownMaterialKind` chain
  (`material_artifact.cpp:98-102`) both confirmed to need exactly the
  mechanical widening ADR-0061 Decision 3 describes: field `0` →
  `UnlitTextured` (unchanged), field `1` → `LitTextured` (new), any
  other value → `UnknownMaterialKind` (unchanged in kind, now reachable
  from one more input value). `MaterialAssetData`'s own four fields are
  confirmed unchanged — `kind`'s own valid *value set* is the only
  thing widening.

### CMake (`shaders/textured_quad/CMakeLists.txt`, root `CMakeLists.txt`, `assets/CMakeLists.txt`, all read in full)

- `atlantis_add_slang_shader_pair(NAME textured_quad SOURCE ...
  VERTEX_ENTRY vertexMain FRAGMENT_ENTRY fragmentMain OUTPUT_DIR ...
  EXPECTED_CONTRACT textured-material)` is `shaders/textured_quad/CMakeLists.txt`'s
  own **entire** real content (beyond one `PARENT_SCOPE` propagation
  line) — `shaders/lit_textured/CMakeLists.txt` mirrors this exactly,
  `EXPECTED_CONTRACT lit-textured`.
- Root `CMakeLists.txt`: `add_subdirectory(shaders/textured_quad)` sits
  **unconditionally** at line 89, immediately before
  `add_subdirectory(src/runtime)` at line 99 (both outside any
  `if(ATLANTIS_BUILD_TESTS)` guard) — `add_subdirectory(shaders/lit_textured)`
  is inserted at the identical point, immediately after
  `shaders/textured_quad` and before `src/runtime`, matching Spec 0019
  D8's own exact placement instruction. **Disclosed, out-of-Plan-scope
  finding (not fixed here):** `shaders/textured_quad/CMakeLists.txt`'s
  own header comment still incorrectly claims this target is
  "added inside the `ATLANTIS_BUILD_TESTS` block" — a stale, pre-Plan-0018
  leftover unrelated to this Plan's own scope (root `CMakeLists.txt`'s
  own comment, unlike this file's, was correctly updated at the time);
  flagged separately, not fixed inside this Plan's own diff, per
  AGENTS.md's "a plan's file list is the scope, not a starting point
  for opportunistic cleanup."
- `assets/CMakeLists.txt`'s `atlantis_add_scene_asset(NAME
  material_demo_scene SOURCE scenes/material_demo.scene.txt
  MESH_DEPENDENCIES ... MATERIAL_DEPENDENCIES unlit_textured_quad
  TEXTURE_DEPENDENCIES textured_quad_unorm)` and `atlantis_add_material_asset(NAME
  unlit_textured_quad ...)` (both read in full) are this Plan's own
  exact templates for the new `lighting_demo_scene`/
  `lit_textured_quad` declarations (P10/Milestone 8).

### RuntimeHost fixture precedent (`tests/image_regression/fixture/material_demo_fixture.cpp`, read in full — 312 lines, the direct template for the new fixture)

- `setUpMaterialDemoFixture()`/`renderMaterialDemoFrame()` call
  `loadAndInstantiateScene()`, `computePendingMaterialIds()`,
  `realizePendingMaterials()`, `extractCameraMatrices()`,
  `resolveMeshAsset()`, `resolveMaterialAsset()` — the **real, shared**
  `Atlantis::RuntimeHost` functions, never a fixture-private
  reimplementation of any of them (Spec 0018 D12's own precedent,
  Spec 0019 D10's own named requirement for this Plan). The new
  `lighting_demo_fixture.cpp` follows this file's own shape exactly,
  additionally calling the new, shared `extractFrameLightingData()`
  (P8) and the widened `realizePendingMaterials()` (P6).
  `renderMaterialDemoFrame()`'s own camera-buffer write
  (`material_demo_fixture.cpp:184-186`) is the direct analog of
  `runtime_application.cpp`'s own — the new fixture's own state struct
  needs the identical new `lightingDataCaptured_`-shaped boolean member
  `MaterialDemoFixture` does not have today (that fixture never needed
  one-time-vs-per-frame semantics, since it captures a single frame
  per test case; this Plan's own D10 static-snapshot negative test
  explicitly requires calling the new fixture's own render function
  **twice** on the same fixture object, so the flag is load-bearing
  here in a way it is not for `material_demo_fixture.cpp`).

## Plan-level decisions (fixed here, not left to Implementation)

### P1. `World` `Light` component — exact files, exact shape, transcribed from the Approved Spec

New `src/world/include/atlantis/world/light.h`, mirroring
`camera.h`/`renderable.h`'s own one-type-per-file shape exactly:

```cpp
#pragma once

#include <atlantis/world/vec3.h>

namespace atlantis::world {

enum class LightKind { Directional, Point };

// See specs/0019-lighting-foundation.md D2. No direction/position of
// its own -- both are re-derived from the owning entity's own current
// world matrix, the one time Runtime ever reads them (Spec 0019 D9).
struct Light {
  LightKind kind = LightKind::Directional;
  Vec3 color{1.0f, 1.0f, 1.0f};  // each component in [0, 1]
  float intensity = 1.0f;          // finite, >= 0
  float range = 0.0f;              // Point only; ignored for Directional
};

}  // namespace atlantis::world
```

`world.h`: `#include <atlantis/world/light.h>`; `Slot` (private to
`world.cpp`) gains `std::optional<Light> light;`; `createEntity()`
gains `slot.light.reset();` alongside its existing `camera.reset();`/
`renderable.reset();`. New public methods, inserted after the existing
`Renderable` triplet, mirroring `setCamera()`/`removeCamera()`/
`getCamera()`'s own exact shape (not `setRenderable()`'s — `Light`
needs a `remove*()`, matching `Camera`, since Spec 0019 D2 names one
explicitly):

```cpp
[[nodiscard]] atlantis::Result<std::monostate, WorldError> setLight(EntityId id, Light light);
[[nodiscard]] atlantis::Result<std::monostate, WorldError> removeLight(EntityId id);
[[nodiscard]] atlantis::Result<Light, WorldError> getLight(EntityId id) const;
[[nodiscard]] std::vector<EntityId> lightEntities() const;
```

`world.cpp` implementations are direct, line-for-line transcriptions of
`setCamera()`/`removeCamera()`/`getCamera()`/`renderableEntities()`
(substituting `light`/`Light`/`NoLightComponent` for
`camera`/`Camera`/`NoCameraComponent`; `removeLight()` mirrors
`removeCamera()` **minus** the `activeCamera_` reset clause, which has
no `Light`-equivalent concept). `world_error.h`: `WorldError` gains
`NoLightComponent` as its sixth enumerator, with a one-line doc comment
matching the other five's own style, placed last (this codebase's own
established convention — see `WrongWorld`/`NoRenderableComponent`,
both also appended at the end of their own enum in the order they were
added, not alphabetized or re-sorted).

### P2. Scene DTO — `DecodedLight`, flat fields, `scene_types.h`

```cpp
enum class DecodedLightKind { Directional, Point };

struct DecodedLight {
  DecodedLightKind kind = DecodedLightKind::Directional;
  float colorR = 1.0f, colorG = 1.0f, colorB = 1.0f;
  float intensity = 1.0f;
  float range = 0.0f;
};
```

A deliberately separate, `AssetSystem`-owned enum/DTO shape from
`atlantis::world::LightKind`/`Light` — matching `DecodedCamera`'s own
already-established precedent of never naming a `world::` type
(`scene_types.h`'s own file-level comment, unchanged, re-confirmed
during this Plan's own reading: naming one here would give
`Atlantis::AssetSystem` a compile-time dependency on `Atlantis::World`,
closing the exact cycle ADR-0052 exists to avoid). Flat `colorR/G/B`,
never a nested `Vec3`-shaped field or `atlantis::world::Vec3` — matches
`DecodedTransform`'s own flat-field convention exactly, not
`world::Light`'s own `Vec3 color` field (that nesting is `world::Light`'s
own, separate, already-Approved shape — ADR-0061 Decision 1 — and this
DTO is not required to mirror it structurally, only to carry the
identical *values*). `ValidatedSceneNode` gains `std::optional<DecodedLight> light;`
as a fourth field, and `ParsedSceneNode` (the pre-cook, authoring-facing
shape) gains the identical `std::optional<DecodedLight> light;` field —
one DTO shape serves both the parsed and the validated stage, exactly
like `DecodedCamera` already does today (the same struct, unchanged,
appears in both `ParsedSceneNode::camera` and `ValidatedSceneNode::camera`).

### P3. Scene grammar — token counts, prefixes, validation, the `TooManyLights` cap

New `scene_source.cpp` prefixes: `kLightPrefix = "light="`,
`kColorPrefix = "color="`, `kIntensityPrefix = "intensity="`,
`kRangePrefix = "range="`; token values `"directional"`/`"point"`
(compared via `==`, no new named constant needed — matches how
`kNoneToken = "none"` is the only such named token value today, since
`"directional"`/`"point"` are each compared exactly once, at exactly
one call site, not shared across the file).

Node line shape: `light=<directional|point> color=<r> <g> <b>
intensity=<f> [range=<f>]` — 5 tokens for `directional` (11 base + 5 =
**16** total), 6 tokens for `point` (11 base + 6 = **17** total).
`scene_source.cpp:152`'s own `tokens.size()` guard widens from `!= 11
&& != 12 && != 13 && != 14` to also accept `16`/`17`; a fifth `else if
(tokens.size() == 16 || tokens.size() == 17)` branch (after the
existing camera branch, `scene_source.cpp:221-234`) parses:

```cpp
} else if (tokens.size() == 16 || tokens.size() == 17) {
  if (tokens[11].substr(0, kLightPrefix.size()) != kLightPrefix) {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
  }
  const std::string_view kindToken = tokens[11].substr(kLightPrefix.size());
  DecodedLight light;
  if (kindToken == "directional") {
    light.kind = DecodedLightKind::Directional;
  } else if (kindToken == "point") {
    light.kind = DecodedLightKind::Point;
  } else {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
  }
  if (kindToken == "directional" && tokens.size() != 16) {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);  // range= on a directional line
  }
  if (kindToken == "point" && tokens.size() != 17) {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);  // range= missing on a point line
  }
  if (tokens[12].substr(0, kColorPrefix.size()) != kColorPrefix) {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
  }
  float r = 0.0f, g = 0.0f, b = 0.0f;
  if (!consumePrefixedFloat(tokens[12], kColorPrefix, r) || !parseFloatToken(tokens[13], g) ||
      !parseFloatToken(tokens[14], b)) {
    return ResultT::Err(SceneSourceParseError::MalformedNumber);
  }
  if (!std::isfinite(r) || r < 0.0f || r > 1.0f || !std::isfinite(g) || g < 0.0f || g > 1.0f ||
      !std::isfinite(b) || b < 0.0f || b > 1.0f) {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
  }
  light.colorR = r; light.colorG = g; light.colorB = b;

  float intensity = 0.0f;
  if (!consumePrefixedFloat(tokens[15], kIntensityPrefix, intensity)) {
    return ResultT::Err(SceneSourceParseError::MalformedNumber);
  }
  if (!std::isfinite(intensity) || intensity < 0.0f) {
    return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
  }
  light.intensity = intensity;

  if (kindToken == "point") {
    float range = 0.0f;
    if (!consumePrefixedFloat(tokens[16], kRangePrefix, range)) {
      return ResultT::Err(SceneSourceParseError::MalformedNumber);
    }
    if (!std::isfinite(range) || range <= 0.0f) {
      return ResultT::Err(SceneSourceParseError::InvalidComponentGroup);
    }
    light.range = range;
  }
  node.light = light;
}
```

**Explicit reasoning for reusing `InvalidComponentGroup` for every
value-domain violation (out-of-`[0,1]` color, negative intensity,
non-positive range, `range=` present on `directional`/absent on
`point`), never a new enumerator:** Spec 0019 D3 itself calls the
`range`-on-`directional` case "a distinct grammar-level error," and
D11 explicitly lists `InvalidComponentGroup` among the reused
enumerators for "every other new failure mode" in this exact group —
`InvalidComponentGroup` already means "this node's own trailing group
is structurally well-formed-looking but violates a constraint of its
own kind" (its own only other use today, `scene_source.cpp:209`/`216`,
is exactly this shape: a token present where a differently-prefixed one
was expected). A value-domain violation *within* an otherwise
well-formed `light=` group is the identical kind of defect, not a new
one.

**`std::isfinite` is checked explicitly, separately from the `>= 0`/
`<= 1`/`> 0` comparisons — not implied by them:** `intensity`'s own
`>= 0.0f` check alone would **accept** `+Infinity` (`INFINITY >= 0.0f`
is `true` in IEEE-754) — a real, distinct bug class this Plan's own
explicit `std::isfinite()` check closes; `color`'s own `[0, 1]` check
does not have this gap (`INFINITY > 1.0f` is `true`, so it is already
rejected by the upper bound alone), but the explicit `isfinite` check
is kept for symmetry and because it is what Spec 0019 D3's own text
literally requires ("must each be finite **and** in `[0.0, 1.0]`" — two
stated conditions, not one).

**The `TooManyLights` cap**, checked once, after the main per-node
parse loop (`scene_source.cpp:238`, immediately before the existing
`if (lineIndex != lines.size())` trailing-content check — so a scene
that is *both* over-cap *and* has trailing garbage reports the cap
violation first, matching this file's own existing "earlier checks take
precedence" ordering throughout):

```cpp
std::uint32_t directionalCount = 0, pointCount = 0;
for (const ParsedSceneNode& n : parsed.nodes) {
  if (!n.light.has_value()) continue;
  if (n.light->kind == DecodedLightKind::Directional) ++directionalCount;
  else ++pointCount;
}
if (directionalCount > 1 || pointCount > 4) {
  return ResultT::Err(SceneSourceParseError::TooManyLights);
}
```

`SceneSourceParseError` gains `TooManyLights` as its eighth enumerator.
`serializeSceneSource()` (round-trip testing only) gains the matching
`else if (node.light.has_value())` branch in its own trailing-group
`if`/`else if` chain (`scene_source.cpp:273-285`), serializing the
identical five/six-token shape.

### P4. Scene artifact — the light slot's exact byte layout, schema version 3

`kSceneArtifactSchemaVersion`: `2 → 3`. New light slot, 28 bytes,
inserted **after the material slot, before the parent slot** (the
established insertion point — P4's own citation, Pre-draft verification
above):

| Field | Offset (relative to slot start) | Size |
|---|---|---|
| `has_light` (u32) | 0 | 4 |
| `light_kind` (u32; `0`=Directional, `1`=Point) | 4 | 4 |
| `color_r`/`color_g`/`color_b` (f32 ×3) | 8 | 12 |
| `intensity` (f32) | 20 | 4 |
| `range` (f32) | 24 | 4 |

Total light slot: **28 bytes**. New node record size:
`kSceneArtifactNodeRecordSizeBytes`: `84 → 112` (light slot occupies
relative bytes `84`-`111`; the parent slot, unchanged in shape, moves
from `76`-`83` to `84`-`91`... **correction, stated precisely**: the
light slot is inserted at the *former* parent-slot position (`76`-`103`,
i.e. `76 + 28 = 104` bytes consumed), and `has_parent`/`parent_index`
move to `104`-`111`. Full, final byte map (0-indexed, inclusive):

| Field | Bytes |
|---|---|
| position xyz | 0-11 |
| rotation xyz | 12-23 |
| scale xyz | 24-35 |
| has_camera | 36-39 |
| fov_y/near_z/far_z | 40-51 |
| has_renderable | 52-55 |
| mesh_asset_id | 56-63 |
| has_material | 64-67 |
| material_asset_id | 68-75 |
| **has_light** | **76-79** |
| **light_kind** | **80-83** |
| **color_r/g/b** | **84-95** |
| **intensity** | **96-99** |
| **range** | **100-103** |
| has_parent | 104-107 |
| parent_index | 108-111 |

`encodeSceneArtifact()` (`scene_artifact.cpp:84-126`): the light block
(`appendU32LE(hasLight)`, `appendU32LE(lightKind)`,
`appendFloatLE(colorR/G/B)`, `appendFloatLE(intensity)`,
`appendFloatLE(range)`) is inserted between the existing material block
(ends at the call producing byte 75) and the existing parent block —
mechanically, a five-line insertion mirroring the material block's own
shape exactly (`hasMaterial ? 1U : 0U` → `hasLight ? 1U : 0U`, etc.).
`decodeSceneArtifact()` (`scene_artifact.cpp:128-249`): the equivalent
read block is inserted at the identical point (after reading
`materialAssetId` at record offset 68, before reading `hasParentFlag`
at the *former* offset 76, now 104) — **every one of the six
`readFloatLE`/`readU32LE` calls already reading the parent block must
have its own literal offset updated from `76`/`80` to `104`/`108`**,
matching this codebase's own established "the byte offset moved,
update the literal, do not leave it silently wrong" discipline
(the exact same discipline Plan 0020's own review round enforced for
the mesh artifact's own index-byte-offset literal).

**Decode-time independent re-validation, mirroring `MaterialWithoutRenderable`'s
own "never trust a well-formed producer" precedent:**

```cpp
if (hasLightFlag != 0) {
  if (lightKindRaw != 0 && lightKindRaw != 1) return ResultT::Err(SceneArtifactDecodeError::NonFiniteValue);
  const bool isPoint = lightKindRaw == 1;
  if (!std::isfinite(colorR) || colorR < 0.0f || colorR > 1.0f || /* g, b identical */ ||
      !std::isfinite(intensity) || intensity < 0.0f ||
      (isPoint && (!std::isfinite(range) || range <= 0.0f)) ||
      (!isPoint && range != 0.0f)) {
    return ResultT::Err(SceneArtifactDecodeError::NonFiniteValue);
  }
  node.light = DecodedLight{isPoint ? DecodedLightKind::Point : DecodedLightKind::Directional,
                             colorR, colorG, colorB, intensity, isPoint ? range : 0.0f};
}
```

**Explicit reasoning for reusing `NonFiniteValue`, never a new
decode-time enumerator, for every one of these checks (including the
unrecognized-`light_kind`-raw-value case):** `SceneArtifactDecodeError`
has no source-grammar-level "structurally odd but not clearly
corrupt" family the way `SceneSourceParseError::InvalidComponentGroup`
does — every existing numeric-domain check at this layer
(`transform`/`camera` floats, `scene_artifact.cpp:183-200`) already
funnels through `NonFiniteValue` regardless of *which* specific
condition failed (non-finite, or, for `light_kind`, simply
unrecognized) — matching this file's own already-established
"one enumerator per structural *kind* of defect, not one per specific
condition" discipline. An unrecognized `light_kind` raw value (neither
`0` nor `1`) is treated as the identical *kind* of corruption a
non-finite float already represents at this layer: a value outside its
own valid domain.

**`TooManyLights`, decode-time, independently re-counted** — after the
per-node decode loop (`scene_artifact.cpp:229`, before the existing
`hasCycleByIndex()` call at `232`), mirroring `parseSceneSource()`'s
own count-then-compare shape exactly:

```cpp
std::uint32_t directionalCount = 0, pointCount = 0;
for (const auto& node : decoded.nodes) {
  if (!node.light.has_value()) continue;
  if (node.light->kind == DecodedLightKind::Directional) ++directionalCount;
  else ++pointCount;
}
if (directionalCount > 1 || pointCount > 4) return ResultT::Err(SceneArtifactDecodeError::TooManyLights);
```

`SceneArtifactDecodeError` gains `TooManyLights` as its seventeenth
enumerator (appended last, matching `MaterialWithoutRenderable`'s own
precedent of being appended, not inserted, into this enum's own
declaration order).

`cookScene()` (`cook_scene.cpp`): the per-node loop
(`cook_scene.cpp:173-217`) gains `node.light = parsedNode.light;`
(a direct copy — `parseSceneSource()` has already fully validated every
light field by the time `cookScene()` runs, so no further
transformation or re-check is needed here, mirroring how
`parsedNode.camera` is copied to `node.camera` verbatim today after
only a redundant `isfinite` re-check the camera fields already get —
**this Plan adds the identical redundant `isfinite` re-check for
`color`/`intensity`/`range` inside `cookScene()`'s own per-node loop
too**, matching that existing precedent exactly, even though
`parseSceneSource()` already rejected non-finite values — defense in
depth, not redundant dead code, since `cookScene()`'s own step already
performs the identical redundant check for every other optional
component today).

### P5. Material — `LitTextured`, mechanical widening only

`material_types.h`: `MaterialKind` gains `LitTextured` as its second
enumerator. `material_artifact.cpp`: `kindToField()`'s `switch` gains
`case MaterialKind::LitTextured: return 1U;`; the decode `if/else`
chain becomes `if (kindField == 0) { ... UnlitTextured; } else if
(kindField == 1) { ... LitTextured; } else { ... UnknownMaterialKind;
}`. A `/w14062` positive probe (temporarily removing the new `case`
from `kindToField()`'s `switch`, confirming a build failure) and a
negative probe (restoring it, confirming a clean, empty `git diff`) are
both required — Spec 0019 D11 names this switch explicitly.

### P6. Runtime material realization — the widened shader-pair selection

`realizeOneMaterialCandidate()`, `realizePendingMaterials()`, and
`rebuildMaterialsForFormatChange()` (`material_realization.h`/`.cpp`)
each gain three new parameters — `litTexturedVertexInputLayout`,
`litTexturedVertexSpirv`, `litTexturedFragmentSpirv` — inserted
immediately after their own existing `unlitTexturedFragmentSpirv`/
`colorFormat` parameter (a mechanical, additive signature widening,
matching `rebuildMaterialsForFormatChange()`'s own already-established
precedent of carrying **two** shader-pair parameter groups in one
signature, for a different reason). `realizeOneMaterialCandidate()`'s
own internal `createMaterial()` call site becomes a `switch
(materialData.kind)` selecting which shader-pair's own `spirvWords`/
`wordCount`/`vertexInputLayout` feed `PipelineCreateParams` — a
`/w14062` positive/negative probe on this new `switch`, matching P5's
identical requirement (Spec 0019 D11 names this switch explicitly, as
"`MaterialKind::LitTextured` in Runtime's shader-pair mapping").
`realizePendingMaterials()`'s own single call site
(`material_realization.cpp:194-197`) threads the three new parameters
through unchanged in kind (a pure pass-through, exactly like its
existing `unlitTextured*` parameters already are).
`rebuildMaterialsForFormatChange()`'s own per-candidate rebuild loop
gains the identical `switch (materialData.kind)` — reading each
existing `Material`'s own `kind` from the caller-supplied
`materialDataMap` the same way `realizeOneMaterialCandidate()` already
does (this function does not currently take a `materialDataMap`
parameter — **it must gain one**, since today it rebuilds every
material blind to `MaterialKind` entirely, borrowing only the existing
`SampledTexture*`/`Sampler*` off each current `Material` object; this
is a second, disclosed, necessary signature widening beyond the three
shader-pair parameters, load-bearing because a format change must
rebuild a `LitTextured` material's `Pipeline` with the `lit_textured`
shader pair, never silently falling back to `unlitTextured`).

Every one of these three functions' own call sites
(`runtime_application.cpp`, `material_demo_fixture.cpp`, the new
`lighting_demo_fixture.cpp`) is updated to pass the three new
`litTextured*` arguments (loaded from the new shader pair's own SPIR-V/
reflection files, mirroring exactly how `unlitTextured*` is already
loaded at each of those call sites today) and, for
`rebuildMaterialsForFormatChange()` specifically, the existing
`materialDataMap` member each caller already owns.

### P7. Frame lighting data — exact CPU struct, exact std140 byte layout, buffer sizing

Lives in the existing `scene_extraction.h` (P8 states the file-location
reasoning), immediately after the existing `CameraMatrices` struct:

```cpp
struct FrameLightingData {
  std::uint32_t directionalLightCount = 0;  // offset 0
  std::uint32_t pointLightCount = 0;        // offset 4
  std::uint32_t _pad1[2]{};                 // offset 8 -- pads to a 16-byte
                                             // boundary for the array below
                                             // (std140's own vec3-array rule;
                                             // C++ does not insert this gap
                                             // on its own, since float[3] only
                                             // demands 4-byte alignment)
  struct DirectionalLightGpu {
    float direction[3];    // offset 0 (within this 32-byte element)
    float _pad0 = 0.0f;    // offset 12
    float color[3];        // offset 16
    float intensity = 0.0f;  // offset 28
  } directionalLights[1];  // offset 16, 32 bytes total
  struct PointLightGpu {
    float position[3];     // offset 0
    float range = 0.0f;    // offset 12
    float color[3];        // offset 16
    float intensity = 0.0f;  // offset 28
  } pointLights[4];  // offset 48, 128 bytes total (4 x 32)
};
static_assert(std::is_standard_layout_v<FrameLightingData>);
static_assert(offsetof(FrameLightingData, directionalLightCount) == 0);
static_assert(offsetof(FrameLightingData, pointLightCount) == 4);
static_assert(offsetof(FrameLightingData, directionalLights) == 16);
static_assert(offsetof(FrameLightingData, pointLights) == 48);
static_assert(sizeof(FrameLightingData::DirectionalLightGpu) == 32);
static_assert(sizeof(FrameLightingData::PointLightGpu) == 32);
static_assert(sizeof(FrameLightingData) == 176);
```

Absolute buffer layout: bytes `0`-`127` are the existing camera
view+projection block (unchanged); bytes `128`-`303` are
`FrameLightingData` (176 bytes), appended immediately after. Camera
`Buffer` sizing (`runtime_application.cpp:241`,
`material_demo_fixture.cpp:136`, and every other camera-buffer-creating
composition root this Plan's own Milestones touch) widens from
`sizeof(float) * 32` to `sizeof(float) * 32 + sizeof(atlantis::runtime::FrameLightingData)`
— `128 + 176 = 304` bytes total. The Slang-side `CameraUniform` struct
(P11) is widened to match this exact layout field-for-field — `ConstantBuffer<T>`'s
own std140 (well, Slang's own default constant-buffer layout, which for
this codebase's own already-`std140`-disciplined convention is
confirmed equivalent for this field shape) rules produce the identical
byte offsets a hand-verified Slang reflection JSON confirms at
Implementation time (V-checklist item, below) — this Plan does not
merely assert layout equivalence, it requires Implementation to
confirm it via a real compile.

### P8. Light extraction — file location, function shape, resolving Spec 0019's own explicitly-left-open question

**Decision: added to the existing `scene_extraction.h`/`.cpp`, not a
new `light_extraction.h`/`.cpp`.** Spec 0019's own Risks & Open
Questions section explicitly leaves this choice to this Plan. Reasoning:
both of this Plan's own two new `SceneExtractionError` enumerators
(`DegenerateLightDirection`, `NonConformalNormalTransform`) are already
required, by Spec 0019 D11 itself, to live in the *existing*
`SceneExtractionError` enum (not a new one) — keeping the functions
that raise them in the same file as that enum avoids a header taking a
dependency on another header purely to see an enum it itself extends,
and mirrors `extractCameraMatrices()`'s own immediately-adjacent
"per-entity, per-frame, world-matrix-driven extraction" role exactly.

**A deliberate, disclosed, narrow break from `extractCameraMatrices()`'s
own "raw values only, no `World`/`world::` type" style:** the new
`extractFrameLightingData()` function takes a
`const std::vector<LightExtractionInput>&`, where:

```cpp
struct LightExtractionInput {
  atlantis::world::Light light;  // the entity's own current Light component
  Mat4 worldMatrix;              // the entity's own current world matrix
};

[[nodiscard]] atlantis::Result<FrameLightingData, SceneExtractionError> extractFrameLightingData(
    const std::vector<LightExtractionInput>& lights);

// D7: a per-entity, per-frame check, called only for a LitTextured-bound
// entity's own current world matrix -- unrelated to light extraction
// itself, grouped here only because it shares this file's own
// "world-matrix-driven, SceneExtractionError-returning" shape.
[[nodiscard]] atlantis::Result<std::monostate, SceneExtractionError> checkConformalTransform(const Mat4& worldMatrix);
```

This is the one place this Plan names an `atlantis::world::` type
(`Light`) inside `scene_extraction.h`, which today names none — a
disclosed, minor, justified precedent addition: `atlantis::runtime`
(Runtime) already depends on `Atlantis::World` throughout
`runtime_application.cpp` (it is the composition root that owns the one
real `World` instance), so this introduces no new module-boundary
dependency, only a new dependency *within* an already-fully-dependent
module's own internal header — unlike `extractCameraMatrices()`, which
takes three bare `float`s (`fovYRadians`/`nearZ`/`farZ`) precisely
because `world::Camera` itself is a three-`float` struct with no richer
structure worth naming, `Light`'s own four fields (`kind`, `color`,
`intensity`, `range`) are exactly what this function needs verbatim, so
naming `world::Light` directly avoids inventing a fourth,
purely-duplicative parameter-shape for data that already has one real,
canonical type in a module already fully in scope. `computePendingMaterialIds()`
`std::vector<AssetId>`-taking shape is not a counterexample against this
reasoning — `AssetId` there is `Atlantis::AssetSystem`'s own public
type, not `world::`'s, and this precedent is specifically about
`atlantis::world::` types, not about "raw values only" as a blanket
rule.

**Internal algorithm** (a direct transcription of Spec 0019 D2, using
`extractCameraMatrices()`'s own cited `Vec3`/`length()` helpers,
already `scene_extraction.cpp`-file-local): iterate `lights` once, in
the caller-supplied order (**the caller** is responsible for supplying
this in `World::lightEntities()`'s own ascending-slot-index order — this
function itself performs no reordering, matching `computePendingMaterialIds()`'s
own "order is the caller's responsibility, this function preserves it"
precedent); for each `LightExtractionInput` whose `light.kind ==
LightKind::Directional`, compute `-column2`/normalize/degenerate-check
exactly as `extractCameraMatrices()` does, writing the first such
result into `directionalLights[0]` and setting `directionalLightCount = 1`
(a second `Directional` entry in `lights` is a caller-side programmer
error — `ATLANTIS_CHECK_MSG`-asserted, never a `Result`, since the
cook/decode-time cap already makes this structurally unreachable from
any real, validated scene, matching `realizePendingMaterials()`'s own
`ATLANTIS_CHECK_MSG` precedent for an analogous "the caller already
guarantees this" invariant); for each `Point`-kind entry (up to 4,
identically asserted), compute `column3` directly (no degenerate case —
D2's own "a raw translation column is always well-defined" — into
`pointLights[i]`, incrementing `pointLightCount`. Returns
`Err(DegenerateLightDirection)` on the **first** degenerate
`Directional` light found (there can be at most one, per the cap, so
"first" and "only" coincide) — a real, disclosed, deliberately
simple choice: this Plan does not need a "which of several possibly-
degenerate lights" precedence question, since the cap makes at most one
`Directional` light reachable at all.

### P9. Runtime integration — the one-time capture site, ownership, the new boolean flag

`runtime_application.h`: `RuntimeApplication` gains one new private
member, `bool lightingDataCaptured_ = false;`, declared immediately
after `cameraBuffer_`'s own declaration (ownership-adjacent members
grouped together, matching this file's own existing member-declaration-order
convention). No new GPU resource, no new destructor concern — `cameraBuffer_`'s
own existing `.reset()` in `shutdown()` already covers the tail bytes
this Plan appends into it; nothing new is separately owned.

`runtime_application.cpp`'s `runFrame()`, immediately after the
existing camera-portion write (`runtime_application.cpp:493`):

```cpp
if (!lightingDataCaptured_) {
  std::vector<atlantis::runtime::LightExtractionInput> lightInputs;
  for (const atlantis::world::EntityId& id : world_->lightEntities()) {
    const auto lightResult = world_->getLight(id);
    const auto worldMatrixResult = world_->getWorldMatrix(id);
    ATLANTIS_CHECK_MSG(lightResult.isOk() && worldMatrixResult.isOk(),
                        "runFrame(): getLight()/getWorldMatrix() failed for a handle lightEntities() just returned");
    lightInputs.push_back({lightResult.value(), worldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  if (lightingResult.isErr()) {
    // Spec 0019's own fixed light Transform values (Milestone 8's new
    // scene) are chosen to never be degenerate -- reaching this path is
    // an unrecoverable construction-bug indicator, matching
    // extractCameraMatrices()'s own identical "should never happen"
    // treatment at this same call site's own sibling check above.
    ATLANTIS_LOG_ERROR("runFrame(): extractFrameLightingData() failed");
    lifecycle_.markFailed();
    return;
  }
  auto* lightingData = reinterpret_cast<atlantis::runtime::FrameLightingData*>(cameraData + 32);
  *lightingData = lightingResult.value();
  lightingDataCaptured_ = true;
}
```

**Why `reinterpret_cast` through the existing `float* cameraData`
pointer, not a second, independent `mappedData()` call:** `Buffer::mappedData()`'s
own contract (`buffer.h`, Pre-draft verification, cited in Spec 0019's
own text) is "mapped once, at construction," so a second call returns
the identical pointer — using the arithmetic offset from the
already-obtained `cameraData` pointer (rather than re-deriving a second,
separately-typed pointer from a fresh `mappedData()` call) keeps this
one write visibly, textually anchored to the write immediately above
it, matching this codebase's own preference for locality over
independently-re-deriving an already-available value.

**Ownership/destruction order, restated as its own explicit
Verification Checklist item (below) since AGENTS.md requires ownership
and destruction-order to be a reviewed, stated decision, not an
implicit consequence:** the frame lighting data occupies tail bytes of
`cameraBuffer_`, an already-existing, already-correctly-ordered member;
this Plan adds no new RAII-owned type, so no new destruction-order
constraint exists anywhere in `RuntimeApplication`'s own member layout
— `cameraBuffer_.reset()` (unchanged call site, `shutdown()`) already
covers 100% of the bytes this Plan ever writes.

### P10. Assets — the new scene, material, mesh, textures this Plan's own verification requires

A new, minimal, independent scene (Spec 0019 D10's own explicit
requirement — never a reuse of `material_demo_scene`/`world_scene`):
`assets/scenes/lighting_demo.scene.txt`, declaring (a) one camera node;
(b) at least one `Renderable`+`LitTextured`-`Material` node, using a
**real, Spec-0020-sourced normal** (the existing `minimal_cube` mesh
asset is reused — it already carries real, smooth, sign-matched
per-vertex normals, requiring no new mesh asset); (c) one `Directional`
light node, aimed via its own parent transform to illuminate the scene
roughly uniformly from a fixed, known world direction; (d) one `Point`
light node, positioned close to one distinct part of the scene's own
geometry — an **asymmetric** layout (Spec 0019 D10's own explicit
requirement), so a direction-sign, position, or attenuation-formula
error each produce a visibly different, wrong frame. A new
`lit_textured_quad` material asset (`assets/materials/lit_textured_quad.material.txt`,
`kind: lit_textured`), reusing the existing `textured_quad_unorm`
texture asset (Plan 0018's own existing declaration — no new texture
asset is required; reusing an existing, already-cooked, already-golden-proven
texture keeps this Plan's own new asset surface to exactly the two new
files this section names, matching AGENTS.md's own "no unrelated
addition" discipline applied to assets as much as to code).
`assets/CMakeLists.txt`: `atlantis_add_material_asset(NAME
lit_textured_quad SOURCE materials/lit_textured_quad.material.txt)` and
`atlantis_add_scene_asset(NAME lighting_demo_scene SOURCE
scenes/lighting_demo.scene.txt MESH_DEPENDENCIES minimal_cube
MATERIAL_DEPENDENCIES lit_textured_quad TEXTURE_DEPENDENCIES
textured_quad_unorm)`, both declared unconditionally (test-only
consumer, so — matching `material_demo_scene`'s own placement — inside
the `ATLANTIS_BUILD_TESTS` block, not promoted to production; Runtime's
own default bootstrap scene stays `world_scene`, D10's own explicit,
restated decision).

### P11. `lit_textured.slang` — exact shader source

`shaders/lit_textured/lit_textured.slang`, mirroring
`textured_quad.slang`'s own exact shape/style:

```hlsl
struct Varying {
    float4 position : SV_Position;
    float3 worldPosition;
    float3 worldNormal;
    float2 uv;
};

struct DirectionalLightGpu {
    float3 direction;
    float _pad0;
    float3 color;
    float intensity;
};

struct PointLightGpu {
    float3 position;
    float range;
    float3 color;
    float intensity;
};

struct CameraUniform {
    float4x4 view;
    float4x4 projection;
    uint directionalLightCount;
    uint pointLightCount;
    uint2 _pad1;
    DirectionalLightGpu directionalLights[1];
    PointLightGpu pointLights[4];
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUniform> camera;

struct PushConstants {
    float4x4 objectToWorld;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

struct VertexInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float3 normal;
};

[[vk::binding(1, 0)]]
Sampler2D texturedSampler;

static const float kPointLightDistanceEpsilon = 1e-4;

[shader("vertex")]
Varying vertexMain(VertexInput input) {
    Varying output;
    float4 worldPos4 = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
    output.position = mul(camera.projection, mul(camera.view, worldPos4));
    output.worldPosition = worldPos4.xyz;
    output.worldNormal = mul((float3x3)pushConstants.objectToWorld, input.normal);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragmentMain(Varying input) : SV_Target {
    float4 texColor = texturedSampler.Sample(input.uv);
    float3 N = normalize(input.worldNormal);
    float3 accumulated = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < camera.directionalLightCount; ++i) {
        float3 L = -camera.directionalLights[i].direction;
        float ndotl = max(dot(N, L), 0.0);
        accumulated += camera.directionalLights[i].color * camera.directionalLights[i].intensity * ndotl;
    }

    for (uint j = 0; j < camera.pointLightCount; ++j) {
        float3 toLight = camera.pointLights[j].position - input.worldPosition;
        float dist = max(length(toLight), kPointLightDistanceEpsilon);
        float3 L = toLight / dist;
        float ndotl = max(dot(N, L), 0.0);
        float atten = clamp(1.0 - dist / camera.pointLights[j].range, 0.0, 1.0);
        accumulated += camera.pointLights[j].color * camera.pointLights[j].intensity * ndotl * atten;
    }

    float3 finalRgb = clamp(texColor.rgb * accumulated, 0.0, 1.0);
    return float4(finalRgb, texColor.a);
}
```

`VertexInput` locations `0`/`1`/`2` map to `position`/`uv`/`normal` —
**not** `position`/`normal`/`uv` — a deliberate choice matching
`textured_quad.slang`'s own existing `location 0`/`1` = `position`/`uv`
ordering as far as it goes, with `normal` appended at `location 2`
(never inserted between), so the C++-side `MeshVertexAttributeSchema`
(P-below, composition roots) names the identical location→offset
mapping every reader of this Plan can verify by inspection without
cross-referencing two files' own field orders against each other.
`CameraUniform`'s own field order/padding matches P7's `FrameLightingData`
exactly, field-for-field — Implementation's own real Slang reflection
JSON is the actual verification (Verification Checklist, below), this
source is the intended, disclosed target, not an unverified assumption.

### P12. Vulkan Backend — the one-line change, and its own comment

`vulkan_device.cpp:858`: `uniformBinding.stageFlags =
VK_SHADER_STAGE_VERTEX_BIT;` → `uniformBinding.stageFlags =
VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;`. The
preceding comment block (`vulkan_device.cpp:851-853`, "Camera uniform
binding... vertex stage only") is updated in the same change to state
the real, current, unconditional-for-every-`Pipeline` widening and cite
ADR-0062 Decision 2 — never left stale (AGENTS.md's own comment-currency
rule, applied here to a comment this Plan's own change makes false).

### P13. Shader System descriptor contract — `litTexturedExpectedDescriptorContract()`

```cpp
std::vector<DescriptorBinding> litTexturedExpectedDescriptorContract() {
  return {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex},
          DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
          DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment}};
}
```

`compile_and_validate.cpp`'s own `--expected-contract` dispatch gains
`else if (expectedContract == "lit-textured") { fullContract =
litTexturedExpectedDescriptorContract(); }`.

## Milestones / Task Breakdown

Each Milestone is one commit, independently buildable/testable per
AGENTS.md's own "build and run tests after every implementation step"
rule — this Plan does not fragment a single mechanical widening (e.g.
P4's own encode+decode byte-layout change) across multiple Milestones,
matching this Plan's own explicit "not too many Milestones for a simple
mechanical change" instruction, while still keeping each Milestone
small enough to review in isolation.

1. **`World` `Light` component** (P1): `light.h` (new),
   `world.h`/`world.cpp`/`world_error.h` widened. GPU-independent unit
   tests only (`tests/world/`): create/set/get/remove, error precedence
   (`WrongWorld`/`InvalidEntity`/`NoLightComponent`, all three orders
   probed), `lightEntities()` determinism (ascending slot-index order,
   fresh snapshot per call, matching `renderableEntities()`'s own
   existing test shape), destroy/cascade (a parent's own destroy
   removes a child's own `Light` the same way it already removes
   `Camera`/`Renderable`), at-most-one-per-entity (a second `setLight()`
   call replaces, never errors — matching `setCamera()`'s own identical,
   already-tested behavior). **Acceptance gate:** `tests/world/` passes
   in full, zero regression to any existing `Camera`/`Renderable` test.
2. **Scene grammar + cook-time validation** (P2, P3):
   `scene_types.h`/`scene_source.h`/`.cpp`/`cook_scene.cpp` widened.
   GPU-independent unit tests (`tests/asset_system/`): round-trip
   (`parseSceneSource()`/`serializeSceneSource()`), every new
   `InvalidComponentGroup`/`MalformedNumber`/`TooManyLights` case (D3's
   own value-domain list, each independently probed — out-of-range
   color, negative intensity, non-positive range, `range=` on
   `directional`, missing `range=` on `point`, a 6th light of a kind
   already at cap), `cookScene()`'s own `node.light` propagation.
3. **Scene artifact byte layout + decode-time validation** (P4):
   `scene_artifact.h`/`.cpp`/`errors.h` widened, `validated_scene_data.h`
   widened. Unit tests: pinned-byte-vector encode test (mirroring
   Plan 0020's own "compile-time-constant expected vector, independently
   computed, never generated by the encoder itself" discipline) at the
   new 112-byte stride; every `TooManyLights`/re-validated-value-domain
   decode rejection, each via a real, hand-corrupted artifact byte
   buffer; `has_light`/`light_kind`/color/intensity/range round-trip
   through `encodeSceneArtifact()`→`decodeSceneArtifact()`.
4. **`fromValidatedSceneData()`** (Pre-draft verification's own single
   insertion point): `scene_instantiation.cpp` widened (one new
   conditional block). Unit test: a `ValidatedSceneData` carrying a
   light node produces a `World` whose one entity has a matching
   `Light` component, via the real `cookScene()`→`decodeScene()`→
   `fromValidatedSceneData()` path, not a hand-built `ValidatedSceneData`.
5. **Material `LitTextured`** (P5): `material_types.h`/
   `material_artifact.cpp` widened. Unit tests: encode/decode round-trip
   for `kind=1`, `UnknownMaterialKind` still rejects `kind=2`; `/w14062`
   positive/negative probe on `kindToField()`'s widened `switch`.
6. **`lit_textured` shader pair + descriptor contract** (P8 partial,
   P11, P12, P13): `shaders/lit_textured/` (new), root `CMakeLists.txt`
   widened, `vulkan_device.cpp` one-line change, `descriptor_contract.h`/
   `.cpp` widened, `compile_and_validate.cpp` widened. **GPU-required**
   (shader compile/reflect/validate is a real build-time tool
   invocation): a clean build confirms `lit_textured`'s own real Slang
   reflection matches `litTexturedExpectedDescriptorContract()` exactly
   (three bindings, the repeated `(0,0)` pair each independently
   confirmed against its own stage) — this is also where P7's own
   `CameraUniform`/`FrameLightingData` byte-layout-equivalence claim is
   actually verified, by inspecting the real reflection JSON's own
   reported offsets, not merely asserted in this Plan's own prose.
   `minimal_mesh`/`textured_quad` shaders' own existing descriptor
   contracts and build-time validation are confirmed unaffected (their
   own `EXPECTED_CONTRACT` values are untouched).
7. **Frame lighting data + light extraction** (P7, P8):
   `scene_extraction.h`/`.cpp` widened (`FrameLightingData`,
   `LightExtractionInput`, `extractFrameLightingData()`,
   `checkConformalTransform()`). GPU-independent unit tests: every D6
   formula, hand-computed expected values (directional `ndotl` sign
   convention, point vector/epsilon/attenuation/range, matching Spec
   0019 D10's own explicit requirement) — as pure math tests against
   `extractFrameLightingData()`'s own returned `FrameLightingData`
   values, not yet through the GPU; `DegenerateLightDirection` (a
   near-zero `-column2`, mirroring `DegenerateCameraForward`'s own
   existing test shape exactly); `checkConformalTransform()`'s own
   accept/reject cases (pure rotation accepted, uniform scale of either
   sign accepted, non-uniform scale rejected, shear rejected — each a
   direct, hand-verified 3×3 matrix, matching D7's own proof sketch).
8. **Runtime integration** (P6, P9, P10 partial):
   `runtime_application.h`/`.cpp`, `material_realization.h`/`.cpp`
   widened; the new `lighting_demo_scene`/`lit_textured_quad`
   assets (P10) declared. **GPU-required.** No new golden yet — this
   Milestone's own acceptance gate is the **four existing goldens'
   own zero pixel difference**, confirmed by running their own existing
   capture-compare tests unmodified, plus a real, executed
   `LitTextured` realization through Runtime's Phase 1/Phase 2 pipeline
   (reusing, not duplicating, `realizePendingMaterials()`), the widened
   uniform binding's own fragment-stage visibility confirmed
   Validation-Layers-clean for both `lit_textured` (which now uses it)
   and every existing shader (which continues not to, unaffected).
9. **Static-snapshot boundary + non-conformal-transform skip, negative
   tests** (D9/D10's own explicitly-required negative coverage):
   a dedicated GPU test proving `World::setLight()` called *after* the
   one-time capture does **not** change the next rendered frame's own
   captured pixels; a dedicated GPU test proving a `LitTextured`-bound
   entity given a deliberately non-conformal world transform is skipped
   for that frame's own `DrawItem` list, logged once, never
   scene-load-fatal.
10. **New fixture + first golden** (P10, D10's own two-commit split —
    ADR-0042's own "Initial baseline bootstrap" category): commit (a)
    `lighting_demo_fixture.h`/`.cpp` (mirroring
    `material_demo_fixture.cpp` exactly, calling every real, shared
    function this Plan's own Milestones 6-9 already built/widened — no
    duplication), its own `CMakeLists.txt`/test registration, a real,
    isolated proof that removing either light from the scene changes
    the captured frame (two additional, temporary scene variants or an
    equivalent programmatic light-removal path, mirroring Spec 0018
    D12's own negative-proof precedent) — landed **without** its own
    golden PNG/sidecar yet, so this commit's own diff is reviewable as
    pure implementation; commit (b), separate, adds the golden capture
    itself (`tests/image_regression/goldens/lighting_demo/`), generated
    on a clean tree against commit (a)'s own already-merged
    implementation, human-reviewed before this commit lands (Definition
    of Done's own image-regression gate).
11. **Documentation + registry closeout** (Milestone-internal, not a
    separate PR — matching Plan 0018/0020's own precedent of a final,
    Milestone-numbered documentation step within the same
    Implementation PR): `specs/README.md` Spec 0019 row updated to
    "code complete, OPEN"; this Plan's own eventual Post-Merge Status
    Update is **not** written here (added only at actual merge time,
    matching every recent Plan's own established closeout discipline —
    see Plan 0020's own identical restraint).

## Files / Modules Touched (expected)

- `src/world/include/atlantis/world/light.h` (new), `world.h`,
  `world_error.h`, `src/world/src/world.cpp`
- `src/world/src/scene_instantiation.cpp`
- `src/asset_system/include/atlantis/asset_system/scene_types.h`,
  `scene_source.h`, `scene_artifact.h`, `validated_scene_data.h`,
  `errors.h`, `material_types.h`
- `src/asset_system/src/scene_source.cpp`, `cook_scene.cpp`,
  `scene_artifact.cpp`, `material_artifact.cpp`
- `src/runtime/include/atlantis/runtime/scene_extraction.h`,
  `material_realization.h`, `runtime_application.h`
- `src/runtime/src/scene_extraction.cpp`, `material_realization.cpp`,
  `runtime_application.cpp`
- `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`,
  `src/shader_system/src/descriptor_contract.cpp`
- `src/tools/shader_compiler/compile_and_validate.cpp`
- `src/vulkan_backend/src/vulkan_device.cpp` (one line + one comment)
- `shaders/lit_textured/lit_textured.slang` (new),
  `shaders/lit_textured/CMakeLists.txt` (new)
- `CMakeLists.txt` (one `add_subdirectory` line)
- `assets/scenes/lighting_demo.scene.txt` (new),
  `assets/materials/lit_textured_quad.material.txt` (new),
  `assets/CMakeLists.txt`
- `tests/world/` (new test cases in existing files, following that
  directory's own established per-component file convention — e.g. a
  `light_tests.cpp` mirroring `camera_tests.cpp`/`renderable_tests.cpp`
  if those exist as separate files, confirmed at Implementation time
  against the real, current `tests/world/` directory listing)
- `tests/asset_system/` (new test cases: scene source/artifact light
  coverage)
- `tests/runtime/` (new test cases: light extraction math, conformal-
  transform check, `/w14062` probes)
- `tests/image_regression/fixture/lighting_demo_fixture.h`/`.cpp` (new),
  its own `CMakeLists.txt` registration
- `tests/image_regression/goldens/lighting_demo/` (new — Milestone 10's
  own second commit only)
- `specs/README.md` (Milestone 11)

**Not touched by this Plan** (confirmed by Pre-draft verification's own
explicit per-file reading above, and by this list containing no entry
under any of these paths): `src/render_graph/`, `src/renderer/include/atlantis/renderer/`
(public headers — `DrawItem`/`Material` are confirmed unchanged in
shape), `src/rhi/include/` (public headers — `PipelineCreateParams` is
confirmed unchanged in shape), any `.slang` file other than the one
new `lit_textured.slang`, `shaders/minimal_renderer/`,
`shaders/textured_quad/` (source content — only the unrelated,
disclosed-but-not-fixed `CMakeLists.txt` comment staleness was found
there, left alone), `assets/meshes/` (no new mesh asset — `minimal_cube`
is reused), `tests/image_regression/goldens/minimal_cube/`,
`world_scene/`, `textured_quad/`, `material_demo/` (all four existing
goldens, zero change), `adr/` (no new ADR — ADR-0061/ADR-0062 are
already `Accepted`, nothing further to record).

## Sequencing & Dependencies

Milestones 1-5 are independently orderable relative to each other (each
touches a disjoint file set: World, Scene grammar, Scene artifact,
`fromValidatedSceneData()`, Material) but are listed in this dependency
order since Milestone 2's own tests exercise DTOs Milestone 1 does not
define, and Milestone 4 needs both Milestone 1 (`World::setLight()`)
and Milestone 3 (`ValidatedSceneData::node().light`) to already exist.
Milestone 6 (shader) has no code dependency on 1-5 but is sequenced
after them so its own new descriptor contract can be exercised by a
real `Material` in Milestone 8's own tests without a forward reference.
Milestone 7 depends on Milestone 1 (`World::Light`/`lightEntities()`)
only. Milestone 8 depends on every one of 1-7. Milestone 9 depends on
Milestone 8's own real, running Runtime integration. Milestone 10
depends on Milestone 9 (the negative-removal proof needs the same
scene/fixture machinery). Milestone 11 is strictly last.

Depends on [Spec 0018](../specs/0018-material-asset-scene-binding-foundation.md)
(Material Asset & Scene Binding Foundation, `Approved`, implemented —
the exact `MaterialKind`/Runtime-realization pattern this Plan widens
rather than replaces) and [Spec 0020](../specs/0020-mesh-normal-attribute-foundation.md)
(Mesh Normal Attribute Foundation, `Approved`, implemented and merged
via [PR #93](https://github.com/slmao/Atlantis/pull/93) — this Plan's
own hard prerequisite, satisfied). [ADR-0061](../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md)/
[ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md),
both `Accepted`. This Plan's own Implementation PR merging is the one
event nothing else in this codebase currently waits on (no named
successor Spec exists yet that names this Plan as its own prerequisite,
unlike Spec 0019's own relationship to Spec 0020).

## Verification Checklist

Maps directly to Spec 0019's own D10, made concrete and numbered.

- [ ] V1 — `World` `Light`: create/set/get/remove, at-most-one-per-entity,
      error precedence (`WrongWorld` → `InvalidEntity` →
      `NoLightComponent`, all three orders individually probed),
      `lightEntities()` determinism, destroy/cascade.
- [ ] V2 — Scene grammar: round-trip; every field-count case (16/17
      accepted, every other rejected as before); every value-domain
      rejection (out-of-`[0,1]` color ×3 components, negative
      intensity, non-finite color/intensity/range each independently,
      `range=` present on `directional`, `range=` absent on `point`).
- [ ] V3 — `TooManyLights`: a 2nd `directional` node and a 5th `point`
      node each independently rejected at parse time
      (`SceneSourceParseError::TooManyLights`) and, independently, via
      a hand-corrupted artifact, at decode time
      (`SceneArtifactDecodeError::TooManyLights`).
- [ ] V4 — Scene artifact pinned-byte encode test at the new 112-byte
      node stride — the expected vector is a compile-time constant,
      independently computed, never produced by calling
      `encodeSceneArtifact()` itself.
- [ ] V5 — Scene artifact decode-time re-validation: every
      value-domain violation independently reachable via a real,
      hand-corrupted artifact buffer, never merely re-running the
      parse-time case.
- [ ] V6 — `fromValidatedSceneData()`: a real
      `cookScene()`→`decodeScene()`→`fromValidatedSceneData()` path
      produces a `World` entity with the correct `Light` component.
- [ ] V7 — Material `LitTextured`: encode/decode round-trip;
      `UnknownMaterialKind` still rejects an out-of-widened-range value;
      `/w14062` positive/negative probe on `kindToField()`.
- [ ] V8 — `lit_textured` shader: real build-time compile/reflect/
      validate succeeds; `litTexturedExpectedDescriptorContract()`
      matches the real reflection exactly, including the repeated
      `(0,0)` pair independently confirmed per stage; `minimal_mesh`/
      `textured_quad` shaders' own existing contracts unaffected.
- [ ] V9 — `CameraUniform`/`FrameLightingData` byte-layout equivalence:
      the real Slang reflection JSON's own reported offsets for
      `directionalLights`/`pointLights` match this Plan's own P7 byte
      table exactly — not merely asserted, inspected.
- [ ] V10 — D6 math, hand-computed expected values, each formula
      independently tested: directional `ndotl`/sign convention; point
      vector/epsilon/attenuation/range; the final combine-and-clamp
      step; no-ambient (zero active lights → pure black).
- [ ] V11 — `DegenerateLightDirection`: a near-zero `-column2`
      `Directional` light rejected, mirroring `DegenerateCameraForward`'s
      own existing test shape.
- [ ] V12 — `checkConformalTransform()`: pure rotation accepted;
      uniform scale (both signs) accepted; non-uniform scale rejected;
      shear rejected — each a hand-constructed 3×3 matrix.
- [ ] V13 — RHI stage-visibility widening: `vulkan_device.cpp:858`'s
      own new value confirmed via a real `lit_textured` GPU test
      (fragment stage genuinely reads the uniform); every existing
      shader's own GPU test suite passes unmodified, Validation Layers
      clean throughout, both configurations.
- [ ] V14 — Runtime integration: a real `LitTextured` material
      realizes correctly through Phase 1/Phase 2 (reusing, not
      duplicating, `realizePendingMaterials()`); the one-time light
      capture happens exactly once (a boolean-flag/log-count check
      across multiple simulated frames).
- [ ] V15 — Static-snapshot boundary: `World::setLight()` called after
      the one-time capture does not change the next rendered frame's
      own captured pixels — a real, executed proof, not an inspection
      claim.
- [ ] V16 — Non-conformal-transform skip: a real GPU test confirms a
      deliberately non-conformal `LitTextured`-bound entity is skipped
      for that frame's own `DrawItem` list, logged once, never
      scene-load-fatal.
- [ ] V17 — Format-change rebuild: `rebuildMaterialsForFormatChange()`'s
      own widened, `MaterialKind`-aware rebuild is confirmed to
      reconstruct a `LitTextured` material's `Pipeline` with the
      `lit_textured` shader pair (never silently falling back to
      `unlitTextured`) after a real color-format change, GPU-required.
- [ ] V18 — The four existing goldens (`minimal_cube`, `world_scene`,
      `textured_quad`, `material_demo`) confirmed byte-for-byte
      identical to `main` and pixel-for-pixel zero-difference, both
      configurations, throughout every Milestone — checked after
      Milestone 8 (first Runtime-integration point) and again at final
      verification, not assumed unaffected merely because this Plan's
      own new code is additive.
- [ ] V19 — New `lighting_demo` golden: captured on a clean tree
      against Milestone 10(a)'s own already-merged implementation
      (never against a dirty/uncommitted tree); the Directional light's
      own contribution and the Point light's own contribution are each
      independently, visibly distinguishable in the captured pixels
      (the asymmetric scene layout, P10, is the mechanism); a real,
      isolated negative proof that removing either light changes the
      captured frame from the golden.
- [ ] V20 — Fresh Debug and Release builds clean, both configurations.
- [ ] V21 — `ctest -LE gpu` and `ctest -L gpu`, both configurations,
      real numbers recorded.
- [ ] V22 — `ATLANTIS_BUILD_TESTS=ON`/`OFF`, both fresh configure+build
      — the `OFF` tree confirms zero `tests/` dependency and that
      Runtime's own default bootstrap scene (`world_scene`) still
      builds and runs unaffected (the new `lighting_demo_scene`/
      `lit_textured_quad`/`lit_textured` shader are all test-only
      declarations, per P10, so none of them exist in that tree at
      all — confirmed, not assumed).
- [ ] V23 — Vulkan Validation Layers: zero `VUID`/`Validation Error`/
      `Validation Warning` across the full `ctest -L gpu` verbose log,
      both configurations.
- [ ] V24 — `/w14062` C4062 positive/negative probes: `kindToField()`'s
      widened `switch` (P5); Runtime's shader-pair-selection `switch`
      (P6); the light-extraction function's own `LightKind`-to-GPU-slot
      translation, if implemented as a `switch` (Implementation-time
      confirmation of shape).
- [ ] V25 — Module/link graph: `Atlantis::World` still links
      `Atlantis::Core` + `Atlantis::AssetSystem` only;
      `Atlantis::AssetSystem` still links `Atlantis::Core` only —
      both re-confirmed, not assumed unaffected.
- [ ] V26 — `git diff --check` clean; no stray whitespace.
- [ ] V27 — Manual, human-performed Runtime windowed verification,
      Debug and Release: confirms `world_scene` (the unswitched default
      bootstrap scene) renders identically to its own pre-Plan
      appearance — a human-observed, not merely automated, confirmation
      that this Plan's own RHI-internal stage-visibility widening (V13)
      has zero visible effect on the one scene Runtime actually boots
      by default.

## Rollback Plan

Every one of this Plan's own version bumps (Scene Asset schema 2→3,
`MaterialKind`'s own widened valid-field-value-set) is, by design, not
independently revertible in place once real content depends on it —
identical in kind to Plan 0020's own mesh-normal bump and every prior
Scene Asset extension. If a real defect is found after this Plan's own
PR merges: (1) before any later Plan begins consuming Scene Asset
schema 3 or `MaterialKind::LitTextured`, a clean `git revert` of this
Plan's own PR wholesale remains possible; (2) once later work has begun
consuming either, a straight revert is no longer safe — the fix must be
a forward, disclosed correction, matching this repository's own general
discipline. The one RHI-internal change (P12, the `stageFlags` widening)
is itself trivially, independently revertible at any time (a single
line, with zero shader depending on its own absence — every existing
shader already tolerates fragment-stage visibility being either present
or absent, since none of them reads the binding from that stage), so it
carries no schema-bump-shaped rollback risk of its own. This Plan
touches zero existing golden (V18), so no golden-side rollback concern
exists for any of the four pre-existing goldens; the one new golden
(`lighting_demo`) is deleted wholesale alongside a revert, with no
partial-state concern.

## Explicit Prohibitions

Restated here, as their own named section, from Spec 0019's own
Non-Goals — Implementation may not silently narrow or reinterpret any
of these:

- No mesh normal authoring, DTO, artifact, or loader change of any
  kind — Spec 0020's own final contract is consumed exactly as merged,
  never redecided.
- No position-derived, derivative-derived, or shader-hardcoded/
  assumed-flat normal, at any point, for any reason.
- No runtime reflection of a `Light` component change after its
  scene's own frame lighting data has been captured — the
  static-snapshot boundary (V15) is load-bearing, not incidental.
- No PBR, metallic/roughness, or any physically-based shading model.
- No shadows or shadow mapping of any kind.
- No image-based lighting (IBL) or environment maps.
- No normal mapping or tangent vertex attribute.
- No ambient/fill light term — an unlit-facing surface renders pure
  black, by design.
- No emissive or transparent materials, and no second, untextured
  `LitColored` material kind this round.
- No clustered/Forward+/deferred rendering, and no light-culling
  strategy beyond the fixed, small, one-time-computed active-light
  array.
- No GPU-driven light culling of any kind.
- No tone mapping, gamma-encode, HDR intermediate targets, or any
  other post-processing.
- No animation of any kind.
- No material graph, shader graph, or user-composable shading system.
- No hot-reload, editor, or runtime asset mutation.
- No distributable Asset Catalog/Registry, or rename-stable identity
  beyond the existing path-derived `AssetId`.
- No Android, iOS, or Linux implementation.
- No new third-party dependency or new top-level module.
- No "dedup" concept for multiple lights sharing one GPU resource.
- No prewiring of any interface/field/abstraction for Shadow
  Foundation, PBR Material, IBL, Post-processing, or Dynamic Frame
  Uniform Updates.
- No real inverse-transpose normal matrix — the detect-and-skip
  approach (D7/P8) is this round's own chosen, disclosed alternative;
  Implementation may not silently substitute the rejected alternative
  without a Spec amendment.
- No second, independent uniform buffer binding for lighting data —
  the single, widened existing binding (D5/P7) is the entire RHI-level
  change this Plan authorizes.
- No widening of the fixed 1-Directional/4-Point light-count cap
  without a Spec amendment — Implementation may not "helpfully" enlarge
  the fixed-size GPU arrays.
- No switch of Runtime's own default bootstrap scene away from
  `world_scene` (D10's own explicit, restated decision).

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: the new `lighting_demo` golden requires
genuine human visual review before Milestone 10(b)'s own commit lands
(a real Directional-vs-Point-light-distinguishing frame, not a black or
garbage capture) — this is this Plan's own first *new* rendered
capability since Spec 0018, so, unlike Plan 0020's own negative-only
delta, this Plan's Definition of Done applies in its ordinary, positive
form here.

## Plan Review (self-check before Human Review)

A single, targeted self-review pass, run immediately after this
document's own first full draft above, checking this Plan's own claims
against the real source a second time rather than trusting the first
pass's own transcription:

1. **Re-verified the scene artifact byte-offset table (P4) by
   re-deriving it independently from the insertion rule** ("after
   material, before parent") rather than trusting the first-pass
   arithmetic alone — confirmed `has_light`/`light_kind` land at
   `76`-`83` (the material slot's own end, `75`, plus one byte) and the
   full light block occupies exactly `76`-`103` (28 bytes), moving
   `has_parent`/`parent_index` from their old `76`-`83` to a new
   `104`-`111` — matches the table as drafted; no correction needed.
2. **Re-verified `FrameLightingData`'s own std140 padding requirement**
   by checking whether a plain C++ struct without the explicit
   `_pad1[2]` member would already produce the required 16-byte
   alignment by accident (e.g. via the compiler's own natural alignment
   of the nested `DirectionalLightGpu[1]` array member) — confirmed it
   would **not**: `DirectionalLightGpu`'s own strictest member is a
   4-byte `float`/`std::uint32_t`, so the array's own natural C++
   alignment requirement is 4 bytes, not 16; without the explicit pad,
   `directionalLights` would land at offset 8, not 16, silently
   producing a CPU/GPU layout mismatch no `static_assert` in a naive
   draft would have caught. The explicit `_pad1[2]` member (and its own
   `static_assert(offsetof(..., directionalLights) == 16)`) is
   confirmed load-bearing, not decorative — kept as drafted.
3. **Re-checked whether `validateDescriptorContract()`'s own
   `(set, binding)`-only matching (not also `stage`) is actually safe
   for a three-entry, repeated-`(0,0)` contract** — this was flagged as
   a real, investigated risk during Pre-draft verification, not
   assumed safe; traced the one real caller
   (`validateDescriptorContractForStage()`) and confirmed it always
   pre-filters by stage before calling the shared function, so the
   theoretical ambiguity never actually arises in practice — no fix
   needed to either function, confirmed by tracing the real call path,
   not merely inspecting the function's own signature in isolation.
4. **Re-checked `rebuildMaterialsForFormatChange()`'s own missing
   `materialDataMap` parameter** — the first draft's own P6 already
   named this as a "second, disclosed, necessary signature widening,"
   confirmed correct on this pass: without it, a format-change rebuild
   would have no way to know which shader pair a given existing
   `Material` was built with, silently rebuilding every material as
   `UnlitTextured` regardless of its own real `MaterialKind` — a real,
   would-have-been-silent defect this Plan's own P6 already closes;
   restated here for visibility since it is the least "obvious" of this
   Plan's own several signature-widening decisions.
5. **Re-checked whether `intensity`'s own `>= 0.0f` check alone would
   incorrectly accept `+Infinity`** (P3) — confirmed yes, `+Infinity >=
   0.0f` is `true` under IEEE-754, so the explicit, separate
   `std::isfinite()` check is load-bearing, not redundant with the
   range check the way it might appear to be at a glance — this
   distinction is now stated explicitly in P3's own text (not merely
   implied), matching the mathematical honesty precedent Plan 0020's
   own numeric-contract review established for a structurally identical
   class of "does the range check alone imply finiteness" question.
6. **Confirmed no Approved Spec/ADR text needed correction** —
   every real-code finding this Plan's own drafting surfaced (the
   material-realization hardcoding, the descriptor-contract
   per-stage-scoping investigation, the `rebuildMaterialsForFormatChange()`
   missing parameter, the `lit_textured`-CMakeLists.txt stale-comment
   sibling finding) was resolvable entirely within this Plan's own
   Plan-level-decision authority — none required stopping to raise an
   objection against Spec 0019, ADR-0061, or ADR-0062's own already-
   `Accepted` text.

No unresolved finding remains. This Plan's own Status stays `In
Review` — Human Review, not this self-review pass, is the gate that
authorizes Implementation.
