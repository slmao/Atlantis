# Spec: Lighting Foundation

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-29
- **Related Plan(s):** none yet — this Spec must reach `Approved` before a
  Plan is drafted, per [AGENTS.md](../AGENTS.md)'s Golden Rule.
- **Related ADR(s):** [ADR-0061](../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md) (`Proposed`), [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md) (`Proposed`)

## Readiness for Human Review

One centralized self-review pass was performed while drafting this
Spec, before presenting it — mirroring Spec 0018's own two "Readiness
for Human Review" rounds, scaled to a single, first-draft pass rather
than a re-review of an already-circulated document. Every real,
concrete architectural or RHI-boundary claim this Spec makes (the mesh
format's own real byte layout; `World`'s own real component/dependency
shape; the scene grammar's own real per-node dispatch mechanism;
`MaterialKind`'s own real closed shape and disclosed growth path; the
RHI's own real, single, vertex-only uniform binding; `ShaderStage`'s own
real single-value shape; the camera buffer's own real per-frame update
pattern) was verified by reading the actual, current source file in
full, not assumed from memory of Spec 0018's own drafting or from this
codebase's own general conventions — see "Pre-draft verification"
below for the complete, itemized record.

**The one real, load-bearing finding this pass made, not merely a
nuance:** this codebase has no vertex normal attribute anywhere, and
genuine diffuse lighting cannot be built without one, without either
faking it (explicitly forbidden by this Spec's own drafting brief) or
solving it here (a real, substantial scope addition this Spec's own
self-review judged too large to bundle cleanly into one delivery
alongside the rest of this Spec's own scope). D1 recommends resolving
this as a separate, prerequisite Spec rather than silently assuming
either answer — stated as this Spec's own most consequential, disclosed
scheduling dependency, not buried in a footnote.

**A second, real finding, load-bearing for D5 specifically:** the RHI's
own existing uniform-buffer descriptor binding is hardcoded,
Vulkan-side, to vertex-stage visibility only — a fragment shader cannot
read it today regardless of what any higher-level API or Shader-System
reflection contract might suggest. This is not a hypothetical
constraint invented to justify an ADR; it was found by reading
`vulkan_device.cpp`'s own real `createPipeline()` implementation line by
line, and is the one real, disclosed RHI change (ADR-0062) this Spec's
own design requires.

No unresolvable architectural conflict was found. Every one of this
Spec's own twelve Decision areas was closed with a real, evidenced
recommendation grounded in current source — none required inventing an
unverifiable claim or silently picking an answer AGENTS.md's own Golden
Rule would call an uncontrolled architectural decision.

## Summary

Close the loop Spec 0018 named as its own successor: a `World`-loaded
scene can bind a real, asset-sourced textured `Material` per entity
(Spec 0018), but nothing in this codebase computes or applies any actual
*lighting* — every drawn pixel today is either a raw vertex color or an
unmodified sampled texel, with no light source, no surface normal, and
no lighting math anywhere in the render path. This Spec adds the
smallest real, scene-driven lighting closed loop: `World` gains a third
optional per-entity component, `Light` (Directional or Point, one
closed, tagged shape — not a variant type, not a generic ECS registry);
the Scene Asset format gains an optional light node; Material gains one
new, minimal kind, `LitTextured`, whose fragment shader reads a
Runtime-computed, per-frame array of active lights and applies minimal
Lambertian diffuse shading against a real, asset-sourced vertex normal.
No PBR, no shadows, no IBL, no post-processing — a closed, honest,
minimal proof that a scene-authored light actually changes what a
Runtime-rendered pixel looks like, end to end.

**A load-bearing, evidence-grounded finding this Spec's own drafting
made, stated up front rather than buried:** this codebase has **no
vertex normal attribute anywhere** — Asset System's static mesh format
(schema version 2, Spec 0017) is position(3)+color(3)+UV0(2), 32 bytes,
full stop (confirmed by direct inspection of every mesh-related file
under `src/asset_system/`, not assumed). Genuine, non-faked diffuse
lighting is mathematically impossible without a real per-vertex normal.
This Spec's own Decision D1 addresses this directly and recommends
**not** solving it inside this Spec — see D1 for the full, evidenced
reasoning and the recommended prerequisite.

## Motivation / Problem Statement

Confirmed directly against real, current source (see "Pre-draft
verification" below):

- `atlantis::renderer::Material` owns exactly one `Pipeline`, plus an
  optional, non-owning `SampledTexture`/`Sampler` pair (Spec 0016). It
  has no light-related field, no normal-related field, no concept of
  "how many lights are active."
- `atlantis::renderer::DrawItem` is `{ mesh, material, objectToWorld }`
  — no per-object normal matrix, no light index/count, nothing beyond
  what Spec 0013/0018 already established.
- `atlantis::rhi::PipelineCreateParams` describes exactly one uniform
  buffer binding (hardcoded, always present, vertex-stage-only) and one
  optional combined-image-sampler binding (Spec 0016's
  `hasSampledTextureBinding`). There is no mechanism to add a second
  uniform buffer binding, and the existing one's own Vulkan
  `stageFlags` is hardcoded to `VK_SHADER_STAGE_VERTEX_BIT` only
  (`vulkan_device.cpp`, `createPipeline()`) — a fragment shader cannot
  read it today, at all, regardless of what any higher-level API claims.
- `atlantis::world::Camera`/`Renderable` are `World`'s only two optional
  per-entity components (`world.h`); a third, `Light`, does not exist.
  Camera's own direction (`extractCameraMatrices()`,
  `scene_extraction.cpp`) is derived entirely from the owning entity's
  own world matrix (`-column2` = forward, `column3` = eye) — it stores
  no redundant direction/position field of its own. This is the direct,
  real precedent this Spec's own Light component design follows.
- The scene authoring grammar's per-node trailing token group is
  currently exactly one of three mutually exclusive shapes — mesh-only
  (12 tokens), mesh+material (13 tokens), or camera (14 tokens) — a
  node cannot carry two trailing groups at once under today's real
  parsing mechanism (`scene_source.cpp`, confirmed by reading the full
  parse function). A fourth, light-shaped group is a real, disclosed
  grammar-extension question this Spec must resolve (see D3).
- `MaterialKind` is a closed, one-enumerator (`UnlitTextured`) enum by
  explicit design (ADR-0059 D2/D15), with its own disclosed consequence
  already on record: "adding a second kind later means adding a second
  Runtime-side hardcoded shader-pair mapping." This Spec is that
  anticipated second kind.
- `RuntimeApplication`'s existing camera uniform `Buffer` is created
  once (`BufferPurpose::Uniform`, host-visible/persistently-mapped) and
  overwritten every frame via a direct `memcpy`-shaped loop into
  `mappedData()` — no RHI "update" call exists or is needed; this is
  the direct, already-proven precedent for a per-frame-updated frame
  lighting buffer (see "Pre-draft verification").

The result: today, no amount of scene authoring can make a
Runtime-loaded scene look *lit* — a scene can carry a real camera, a
real textured mesh, and a real material, but every pixel's own color is
either raw vertex color or an unmodified sampled texel, with no light
source affecting it in any way. This Spec builds the smallest real path
that changes that, through the same real authoring → cook → artifact →
load → Runtime-resolve → GPU-realize → `DrawItem` chain Spec 0018
already established — not a fixture shortcut, not a shader-faked light.

## Goals

1. `World` gains a third optional per-entity component, `Light` — one
   closed, tagged shape (Directional or Point), following the exact
   flat-struct precedent `Camera`/`Renderable` already establish; no
   generic ECS registry.
2. The Scene Asset authoring/artifact format gains an optional light
   node, following the exact version-bump/no-dual-version-reader
   discipline every prior Scene Asset extension already used.
3. Material gains exactly one new, minimal kind, `MaterialKind::LitTextured`
   — a closed, Runtime-resolved shader-pair mapping, matching
   `UnlitTextured`'s own exact precedent, reusing the existing Material
   DTO shape with zero new fields.
4. Runtime computes one, real, per-frame array of active lights
   (deterministically ordered, deduplicated by nothing — every light
   entity contributes independently, unlike Spec 0018's texture/material
   dedup, since two distinct light entities are never "the same light")
   and makes it available to a `LitTextured` Material's own fragment
   shader, applying minimal Lambertian diffuse shading against a real
   vertex normal.
5. A real, new, minimal scene+light+material image-regression fixture
   proves the whole path end to end, distinguishing a Directional
   light's own contribution from a Point light's own contribution in
   the captured pixels.
6. Zero change to any existing, committed golden (`minimal_cube`,
   `world_scene`, `textured_quad`, `material_demo`).

## Non-Goals

- PBR, metallic/roughness, or any physically-based shading model.
- Shadows or shadow mapping of any kind.
- Image-based lighting (IBL) or environment maps.
- Normal mapping or a tangent vertex attribute — see D1; even a plain
  vertex normal is a real, disclosed open question this Spec does not
  silently assume, and tangent-space normal mapping is explicitly a
  later phase regardless of D1's own outcome.
- Emissive or transparent materials, or any second `LitColored`
  (untextured) material kind — see D4.
- Clustered, Forward+, or deferred rendering, or any light-culling
  strategy beyond a fixed, small, CPU-computed active-light array.
- GPU-driven light culling of any kind.
- Tone mapping, gamma-encode, HDR intermediate targets, or any other
  post-processing — see D8.
- Animation of any kind (a light's own color/intensity/range and every
  entity's own transform are static per frame, exactly as every existing
  scene already is).
- A material graph, shader graph, or any user-composable shading system.
- Hot-reload of any kind, an editor, or runtime asset mutation.
- A distributable, cross-session Asset Catalog/Registry, or any
  rename-stable identity beyond the existing path-derived `AssetId`.
- Android, iOS, or Linux implementation — Phase 1 remains Windows-only
  for real hardware verification (per AGENTS.md).
- A new third-party dependency or a new top-level module.
- Multiple lights sharing one GPU resource "dedup" concept — unlike a
  shared texture/material (Spec 0018 D10), two light entities are never
  interchangeable, so there is nothing to deduplicate here.
- Prewiring any interface, field, or abstraction for Shadow Foundation,
  PBR Material, IBL, or Post-processing — each remains its own,
  independent, unblocked future Spec (see D12).

## Requirements

### Functional

- `atlantis::world::LightKind` (`Directional`, `Point`) and
  `atlantis::world::Light` — a flat struct, matching `Camera`'s own
  shape exactly (see D2 for the exact field list); `World::setLight()`/
  `removeLight()`/`getLight()`, and a `lightEntities()` deterministic
  accessor mirroring `renderableEntities()` exactly (ascending
  slot-index order, a fresh snapshot per call).
- A scene node may optionally declare a light (see D3 for the exact
  grammar shape and its own real, evidenced constraint against
  co-locating a light with a mesh/camera on the same node this round).
  `cookScene()`/`decodeScene()` resolve and validate it with the same
  discipline every other node-level field already has (non-finite
  rejection, independent decode-time re-validation, never trusting the
  cooker).
- `atlantis::asset_system::MaterialKind::LitTextured` — a new
  enumerator on the existing closed `MaterialKind` enum, requiring **no
  new `MaterialAssetData` field** (see D4) — Runtime maps it to a new,
  fixed, built-in `Lit` shader pair, following `UnlitTextured`'s own
  exact CMake-unconditional-production-shader precedent (Spec 0018 D3).
- A new Runtime-private extraction function (mirroring
  `extractCameraMatrices()`/`resolveMaterialAsset()`'s own exact
  precedent) computes, once per frame, a fixed-size, deterministically-
  ordered array of active lights' own world-space direction/position,
  color, intensity, and (Point only) range, from `World`'s own
  `lightEntities()` and each entity's own current world matrix — never
  a redundant, separately-stored direction/position field.
- This per-frame light array is packed into the *same* existing
  uniform buffer binding the camera view/projection matrices already
  occupy (see D5) — no new descriptor binding, no new `Buffer`, no new
  RHI resource-creation call — updated via the same unconditional,
  per-frame `memcpy`-into-`mappedData()` pattern the camera buffer
  already uses.
- The one RHI-level change this Spec requires (see D5/ADR-0062): the
  existing, single uniform-buffer descriptor binding's own Vulkan
  `stageFlags` widens from vertex-only to vertex-and-fragment,
  unconditionally, for every `Pipeline` — a disclosed, minimal, one-line
  `vulkan_device.cpp` change; `PipelineCreateParams`'s own public shape
  is unchanged.
- A new `Lit` built-in shader pair (`.slang`) samples the material's own
  texture (unchanged from `UnlitTextured`), reads the per-frame light
  array from the same uniform binding, computes minimal Lambertian
  diffuse shading per active light using a real, asset-sourced vertex
  normal (transformed per D7), and writes the summed result as the
  final fragment color — no tone-mapping, no gamma-encode (D8).
- A new, minimal, independent scene + light + material image-regression
  fixture and its own first golden (ADR-0042's "Initial baseline
  bootstrap" category), proving the real end-to-end path, with a
  captured-pixel-level distinction between the Directional light's own
  contribution and the Point light's own contribution (D10).

### Non-functional

- **Performance:** not a goal of this round, matching Spec 0018's own
  identical discipline — the per-frame light-array `memcpy` runs
  unconditionally every frame (no dirty-tracking), matching the camera
  buffer's own existing, already-accepted per-frame update cost exactly.
- **Memory:** the frame lighting data adds a small, fixed number of
  bytes to the existing camera uniform buffer (exact size fixed by D5's
  own maximum-light-count decision) — no unbounded growth, no
  per-light heap allocation.
- **Portability (within the Vulkan-only Phase 1 constraint):** the
  frame lighting struct's own CPU-side layout is unconditionally
  little-endian/std140-compatible (see D5), matching every other
  Asset-System/Runtime binary contract's own discipline.
- **Other:** zero new error enumerator reused where an existing one
  already fits (mirroring Spec 0017/0018's own discipline); new
  enumerators are added only where no existing one covers a genuinely
  new failure mode.

## Pre-draft verification against real, current source

Confirmed directly against `main` at Spec-drafting time (2026-08-29),
by reading full files, not from memory of Spec 0018's own drafting:

- **Mesh vertex layout (Spec 0017, `scene_artifact.h`/`mesh_artifact.h`/
  `material_realization.cpp`'s own `Vertex` struct, all read in full):**
  `position[3] + color[3] + uv[2]`, 32 bytes, offsets 0/12/24. No
  `normal` field anywhere in `StaticMeshAssetData`, the mesh source
  grammar, the mesh artifact, or any composition root's own local
  `Vertex` struct (`runtime_application.cpp`, `material_demo_fixture.cpp`,
  `minimal_cube_fixture.cpp`, `textured_quad_fixture.cpp` — all four
  checked). Confirmed via a targeted `normal`/`Normal` grep across
  `src/asset_system/` returning only unrelated `normalizeLogicalPath()`
  matches.
- **`World`'s real component model (`world.h`, `camera.h`, `renderable.h`,
  `transform.h`, all read in full):** exactly two optional per-entity
  components today, `Camera` (`{fovYRadians; nearZ; farZ;}` — no
  direction field) and `Renderable` (`{meshAsset; materialAsset;}`);
  each has its own `set*()`/`remove*()`/`get*()` triplet; `Renderable`
  alone has a dedicated deterministic accessor, `renderableEntities()`
  (doc comment: "Ascending slot-index order — a fresh `std::vector`
  snapshot each call, valid as of the call, not a live iterator held
  across a subsequent `World` mutation"). `World`'s own public API has
  no matrix-inverse function anywhere — `updateTransforms()`/
  `getWorldMatrix()` only ever compose `parentWorld · T · R · S`
  forward, never invert (relevant to D7).
- **Camera direction/eye extraction (`scene_extraction.cpp`, read in
  full):** `extractCameraMatrices()` derives `forward = normalize(-column2
  of cameraWorldMatrix)` and `eye = column3 (translation)` — confirmed,
  not assumed, that a component's own "facing" data is *never* stored
  redundantly on the component itself; it is always re-derived from the
  owning entity's own current world matrix, every frame. This is the
  exact precedent D2's own Light-direction/position design follows.
- **Scene authoring grammar's real per-node dispatch (`scene_source.cpp`,
  full parse function read):** `tokens.size()` is checked against
  exactly `11` (transform only), `12` (+`mesh=`), `13` (+`mesh=`
  +`material=`, Spec 0018), or `14` (+three `camera_*=` fields) — a
  hard `if (tokens.size() == 12 || 13) {...} else if (tokens.size() ==
  14) {...}` branch. Confirmed directly: there is no existing mechanism
  for a node to carry two of {mesh[+material], camera, light} at once;
  each node's own single trailing token group is mutually exclusive by
  construction, not merely by convention. This fixes the real
  constraint D3 must resolve.
- **`MaterialKind`'s own closed shape and disclosed growth path
  (`material_types.h`, ADR-0059, both read in full):** `enum class
  MaterialKind { UnlitTextured };` and ADR-0059's own already-`Accepted`
  "Consequence, disclosed" text: "adding a second kind later means
  adding a second Runtime-side hardcoded shader-pair mapping and, if
  that shader is not yet unconditionally built, the same CMake-placement
  fix this decision already makes once." `MaterialAssetData` is
  `{kind; textureAsset; filter; addressMode;}` — confirmed a
  `LitTextured` kind needs no new field here (D4), since it names the
  same one texture/sampler pair `UnlitTextured` already does; the only
  per-material difference a `LitTextured` Material needs is which
  built-in shader pair Runtime maps its `kind` to.
- **RHI's real, single uniform-buffer descriptor binding
  (`types.h`'s `PipelineCreateParams`, `vulkan_device.cpp`'s
  `createPipeline()`, both read in full):** `PipelineCreateParams` has
  exactly one push-constant range, one hardcoded uniform-buffer binding
  (always present, no flag controls it), and one *optional*
  combined-image-sampler binding (`hasSampledTextureBinding`). The
  uniform binding's own `VkDescriptorSetLayoutBinding.stageFlags` is
  hardcoded to `VK_SHADER_STAGE_VERTEX_BIT` — confirmed by reading the
  exact line, not inferred from a comment — meaning a fragment shader
  cannot read this binding today, full stop, regardless of any Shader
  System reflection contract claiming otherwise. This is the one real,
  concrete RHI-boundary blocker D5 must resolve — not a hypothetical
  one.
- **`ShaderStage`/`DescriptorBinding` reflection shape (`reflection_metadata.h`,
  read in full):** `enum class ShaderStage { Vertex, Fragment };` — a
  single value, not a bitmask; `DescriptorBinding{set; binding; type;
  stage;}` names exactly one stage per entry. Confirmed: a shader whose
  vertex *and* fragment stages both reference `{set 0, binding 0}`
  produces two separate `DescriptorBinding` reflection entries (one per
  stage's own separately-loaded reflection JSON,
  `vertexShaderReflectionPath`/`fragmentShaderReflectionPath`), each
  correctly tagged with its own stage — the Shader-System/Tools-side
  descriptor-contract-validation mechanism (`descriptor_contract.h`,
  `texturedMaterialExpectedDescriptorContract()`'s own precedent) can
  already express this as two expected-contract entries; the *only*
  real blocker is the RHI-side hardcoded `stageFlags` above, confirmed
  by reading both files, not assumed from one alone.
- **`VertexAttributeFormat` (`types.h`):** `{ Float3, Float2 }` — `Float3`
  already exists and is exactly what a normal attribute would need; no
  new RHI vertex-format enumerator would be required if D1's own
  recommendation is later accepted and a future Spec adds one.
- **Per-frame uniform buffer update, already proven
  (`runtime_application.cpp`'s `cameraBuffer_` creation and its own
  `runFrame()` write loop, read in full):** `device_->createBuffer({.purpose
  = BufferPurpose::Uniform, .sizeBytes = sizeof(float) * 32})`, then
  every frame, unconditionally: `auto* cameraData =
  static_cast<float*>(cameraBuffer_->mappedData()); for (...) cameraData[i]
  = ...;` — no RHI "update" call, no re-creation, no synchronization
  concern beyond the engine's own existing single-frame-in-flight model.
  Confirms D5/D9's own "reuse this exact pattern for light data" claim
  directly, not by inference.
- **Runtime's own `MaterialKind::UnlitTextured` shader-pair wiring
  (`bootstrap_config.h`, `main.cpp`, `runtime_application.cpp`,
  `material_realization.cpp`, all re-read): confirms the exact,
  precedented shape a `LitTextured` shader-pair addition would mirror
  — four new `BootstrapConfig` path fields, a second
  `VertexInputLayout`/SPIR-V pair resolved once at `initializeSteps()`,
  consumed by `realizeOneMaterialCandidate()`/`rebuildMaterialsForFormatChange()`'s
  own already-generic, kind-parameterized call shape (both functions
  already take the shader pair as explicit parameters, never hardcoding
  `MaterialKind::UnlitTextured` internally — confirmed by re-reading
  `material_realization.h`'s own function signatures in full — so a
  `LitTextured` kind is a *new caller-side* shader-pair argument set,
  not a change to either function's own internals).
- **Current image-regression fixtures/goldens
  (`tests/image_regression/`, directory listing plus each fixture's own
  top-of-file comment read):** four goldens exist today —
  `minimal_cube`, `world_scene`, `textured_quad`, `material_demo` — the
  last two each directly link `Atlantis::RuntimeHost` (Spec 0016/0018's
  own precedent) rather than duplicating Runtime logic; `minimal_cube`/
  `world_scene` duplicate it, a disclosed, accepted precedent for
  simpler, stable logic only (`world_scene_loaded_fixture.cpp`'s own
  top-of-file comment).
- **Runtime's default bootstrap scene (`main.cpp`, re-read):**
  `config.sceneArtifactPath` etc. still resolve to `world_scene`,
  unchanged since Spec 0014 — `material_demo_scene` (Spec 0018) is
  cooked but never wired in, per that Spec's own explicit P15 decision.
  This is the direct precedent D10's own bootstrap-scene question
  follows.

## Proposed Design

### The minimal closed loop

```
scene authoring (.scene.txt)
  node: ... light=directional color=<r,g,b> intensity=<f>
  node: ... light=point color=<r,g,b> intensity=<f> range=<f>
        |
        v
cookScene() -- validates finite/non-negative, resolves LightKind token
        |
        v
scene artifact (version 3) -- new light slot, decode-time re-validated
independently of the cooker
        |
        v
World::Light (via fromValidatedSceneData(), infallible, unchanged in
  kind) -- direction/position NOT stored, re-derived from the owning
  entity's own world matrix every frame, exactly like Camera already
  does
        |
        v
Runtime: a new, Runtime-private light-extraction function, called once
  per frame from runFrame() (mirroring the existing camera-matrix
  extraction call site exactly) -- walks World::lightEntities() in its
  own deterministic order, reads each light's own current world matrix
  + Light component, produces a fixed-size, capped array of active
  lights' own {direction-or-position, color, intensity, range}
        |
        v
packed into the SAME existing camera uniform Buffer, appended after the
  existing view/projection floats -- one Buffer, one binding, one
  per-frame memcpy, exactly like the camera data already is
        |
        v
MaterialKind::LitTextured's own built-in Lit shader pair (fragment
  stage now ALSO declared against binding (0,0), per ADR-0062's own
  RHI stage-visibility widening) -- samples the material's own texture
  (unchanged), reads the light array, computes per-light Lambertian
  diffuse against a real, asset-sourced vertex normal (D1), sums,
  writes final color -- no tone-mapping, no gamma-encode
        |
        v
headless golden (new lighting_demo fixture, directly linking
  Atlantis::RuntimeHost per Spec 0018's own D12 precedent) + windowed
  Runtime (bootstrap-scene switch itself left to Human Review, D10)
```

### Where each new piece lives

| Piece | Module | Mirrors |
|---|---|---|
| `LightKind`, `Light` component, `set/remove/getLight()`, `lightEntities()` | `Atlantis::World` | `Camera`'s own exact shape/accessor pattern |
| Scene grammar `light=` node, artifact light slot | `Atlantis::AssetSystem` | the existing `camera_*=` node/slot, the most structurally similar precedent (also a component-shaped, non-Renderable trailing group) |
| `MaterialKind::LitTextured` | `Atlantis::AssetSystem` (`material_types.h`) | `MaterialKind::UnlitTextured`, verbatim |
| Runtime-private light extraction | `Atlantis::Runtime` (new `light_extraction.h`/`.cpp`, or an addition to `scene_extraction.h`/`.cpp` — a Plan-time file-layout choice, not an architectural one) | `extractCameraMatrices()`/`resolveMaterialAsset()` |
| Frame lighting data packed into the existing camera `Buffer` | `Atlantis::Runtime` (`runtime_application.cpp`) | the existing per-frame `cameraData[i] = ...` write loop |
| RHI uniform-binding `stageFlags` widening | `Atlantis::RHI`/`Atlantis::VulkanBackend` (`vulkan_device.cpp`) | the existing, unconditional single-binding shape — widened in place, not replaced |
| `Lit` built-in shader pair | `shaders/lit_textured/` (new directory, mirroring `shaders/textured_quad/`'s own unconditional-production-shader placement, Spec 0018 D3) | `shaders/textured_quad/textured_quad.slang` |

No new top-level module. `Atlantis::World`'s link closure is unchanged
(`Atlantis::Core` + `Atlantis::AssetSystem`). `Atlantis::AssetSystem`'s
link closure is unchanged (`Atlantis::Core` only).

## Decisions for Human Review

Numbered to match this Spec's own governing questions one to one — the
twelve areas named in this Spec's own drafting brief.

### D1. Mesh normal prerequisite — recommendation: a separate, prerequisite Spec, not bundled here

**Decision needed:** how this Spec obtains a real, asset-sourced vertex
normal, given the confirmed fact that none exists anywhere in this
codebase today (Pre-draft verification, above), and the explicit
instruction that this Spec must never fake one (no position-derived
normal, no shader-hardcoded/assumed-flat normal standing in for a real
asset attribute).

**Three options, evaluated honestly:**

**(a) Bundle a real normal attribute into this Spec's own scope.**
Would require: a new authoring grammar field (`nx ny nz` per vertex,
mirroring `MeshSourceVertex`'s own "vertex: x y z r g b" style, widened
to `x y z r g b nx ny nz`), a new `StaticMeshAssetData` field, a mesh
artifact schema version bump (3, 44-byte stride at offsets 0/12/24/32),
outright rejection of version 2, a full repository-wide sweep of every
existing `.mesh.txt` source and every embedded mesh-source-literal test
string (mirroring Spec 0017's own Milestone 1 sweep, which was itself
a dedicated, non-trivial piece of work), a disclosed migration decision
for `minimal_cube`'s own topology (does it get real per-face normals,
or another placeholder — and if a placeholder, is a *placeholder
normal* materially different from the *forbidden* fake/derived normal
this Spec must not use for its own lighting proof? It is not used for
lighting there, so it is not the same category of fake, but it must be
disclosed identically to Spec 0017's own UV0 placeholder disclosure),
and — the explicit trigger named in this Spec's own drafting brief — a
Proposed Amendment to both ADR-0045 (data-format/versioning policy,
already amended once for UV0) and ADR-0058 (static mesh vertex layout,
already scoped to "position, color, UV0" by its own Accepted Decision
text).

**(b) A separate, prerequisite Spec — "Mesh Normal Attribute
Foundation" — drafted and approved before this Spec's own Plan begins.**
Mirrors Spec 0017's own real, direct precedent almost exactly: Spec
0016 (Texture & Sampler Foundation) named UV0 as its own disclosed,
blocking follow-up rather than bundling it in; Spec 0017 was drafted,
approved, planned, and implemented as its own, independent, single-
responsibility unit, closing that gap cleanly before Spec 0018 (which
depended on it) was drafted. A vertex normal is, in every structural
respect, the same *kind* of gap: a new, mandatory, asset-sourced vertex
attribute requiring the identical grammar/artifact/version/migration/
ADR-amendment machinery Spec 0017 already built once. This Spec would
then depend on that prerequisite Spec being `Approved`/implemented,
exactly as Spec 0018 depended on Spec 0017.

**(c) A temporary, disclosed stand-in** (derivative normals from
position, or a shader-hardcoded flat normal) **— explicitly rejected**
by this Spec's own drafting brief, and independently rejected here on
the merits: either approach would make the resulting "lighting" pixels
*not actually driven by asset-authored geometry data*, undermining the
one property this Spec's own Summary states as its central claim (a
real, scene-authored light changes what a Runtime-rendered pixel looks
like, through the real asset pipeline) — the exact same reasoning
ADR-0059/Spec 0018 already used to reject a fixture-hardcoded material
bypassing Asset System.

**Recommendation: (b).** Evidence-based, not merely by analogy: this
Spec's own remaining scope (World `Light` component, Scene grammar/
artifact light node, `MaterialKind::LitTextured`, the RHI uniform-binding
stage-visibility widening, Runtime light extraction, the new
image-regression fixture/golden) is already a full, real Spec-sized
unit of work in its own right — comparable in shape to Spec 0018's own
17-Milestone scope. Adding a full mesh-format version bump, migration
sweep, and two ADR amendments on top would make this Spec's own single
delivery meaningfully harder to review, plan, and verify as one
coherent unit, for a component (the normal attribute) that has its own
clean, independent, already-precedented shape and no dependency on any
lighting-specific design decision in this Spec. **Consequence, disclosed
plainly:** if this recommendation is accepted, this Spec's own Plan
cannot begin until "Mesh Normal Attribute Foundation" is itself
`Approved` and implemented — this Spec documents the full lighting
design now so Human Review can evaluate the whole shape at once, but
its own Implementation is gated on that prerequisite landing first,
exactly as Spec 0018's own Plan was gated on Spec 0017.

### D2. World `Light` component shape — recommendation: one closed, flat, tagged struct, not a variant, not two components

**Decision:** `enum class LightKind { Directional, Point };` and one
flat `Light` struct:

```cpp
struct Light {
  LightKind kind = LightKind::Directional;
  Vec3 color{1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = 0.0f;  // Point only; ignored (and decode/cook-time
                        // rejected as invalid) for Directional -- see D3
};
```

`World::setLight(EntityId, Light)`/`removeLight(EntityId)`/
`getLight(EntityId) const`, and `lightEntities() const` — a fresh
`std::vector<EntityId>` snapshot in ascending slot-index order,
verbatim-mirroring `renderableEntities()`'s own exact doc comment and
implementation shape.

**Why one component, not two (`DirectionalLight`/`PointLight`):** would
double `World`'s own component-slot count for no real benefit — an
entity is never both a directional and a point light simultaneously (a
closed, mutually-exclusive `kind` tag is the correct shape for "exactly
one of a small, fixed set," matching `MaterialKind`'s own precedent
exactly, not `Camera`+`Renderable`'s own precedent of "two genuinely
independent, freely-combinable components").

**Why a flat struct with a `kind` tag, not `std::variant<DirectionalLight,
PointLight>`:** matches every other closed-kind DTO in this codebase
(`MaterialAssetData`, `TextureAssetData`) — none use `std::variant` for
"one of a small, fixed set of shapes"; all use a plain enum plus flat
fields, with fields irrelevant to the current `kind` simply unread. A
`std::variant` would be the first of its kind anywhere in `World` or
Asset System, introducing a new pattern for no functional gain over the
established one.

**Why no direction/position field:** direct precedent, not invention —
`Camera` stores no direction of its own; `extractCameraMatrices()`
derives it fresh from the owning entity's own world matrix every frame.
`Light`'s own direction (Directional) or position (Point) is derived the
identical way in Runtime's own extraction step (D9) — `-column2`
(normalized) for direction, `column3` for position — never stored
redundantly on the component, never capable of drifting out of sync
with the entity's own `Transform`.

**Entity destruction/parent-transform/deterministic-traversal
semantics:** unchanged in kind from `Camera`/`Renderable` — a light
entity's own slot is retired and its `Light` component destroyed
exactly like any other component on `destroyEntity()`; a light attached
under a moving parent inherits that parent's own transform through the
existing, unmodified `updateTransforms()`/`getWorldMatrix()` composition,
with no light-specific hierarchy logic added anywhere. No generic ECS
registry — `Light` is a third fixed-type optional slot, exactly like the
two that already exist.

### D3. Scene authoring/artifact — recommendation: light is its own, standalone node this round; co-location with mesh/camera achieved via the existing parent/child hierarchy, not a same-node grammar extension

**Decision:** `atlantis_scene_source_version: 2 → 3`. A node's own
trailing token group gains a fourth, mutually-exclusive shape,
`light=<directional|point> color=<r> <g> <b> intensity=<f> [range=<f>]`
— present only when the node carries no `mesh=`/`camera_*=` group,
matching the real, confirmed structural shape of today's parser
exactly (a new `tokens.size()` case, disjoint from 11/12/13/14, added
as a fourth `else if` branch — mechanically identical to how Spec
0018's own 13-token case was added alongside the pre-existing 12/14
cases). Version 2 sources/artifacts are rejected outright — no
dual-version reader, matching every prior Scene Asset version bump.

**Why light cannot share a trailing group with mesh/material or camera
this round, stated as a real constraint, not a stylistic choice:**
confirmed directly (Pre-draft verification) that today's parser
dispatches on a single, exclusive `tokens.size()` value per node — there
is no existing mechanism for "two independent, self-describing trailing
groups on one line." Building one would mean replacing the entire
grammar's own position-count-based dispatch with a self-describing,
prefix-tagged multi-group parser — a real, disclosed, out-of-proportion
grammar redesign for this Spec's own minimal scope, touching every
existing node shape's own parsing code, not merely adding a fourth case
to it.

**How a "visible light fixture" (a lit bulb mesh at the same visual
location as its own light) is still achievable with zero grammar
change:** the existing parent/child hierarchy already composes
transforms across entities — a `Renderable`-only node and a `Light`-only
node, one parented to the other (or both parented to a shared, empty
transform-only node), already produce the correct composed world
position for both, through mechanism this Spec adds nothing to. This is
the recommended, disclosed pattern for that composition, not a gap.

**Validation, cook-time and decode-time, independent of each other**
(matching `MaterialWithoutRenderable`'s own "never trust the cooker"
precedent exactly): `color` components and `intensity` must be finite
and non-negative; `range` must be finite and strictly positive for
`Point`, and is rejected outright (not merely ignored) if present on a
`directional` light line — an explicit, distinct grammar-level error,
not a silently-tolerated-then-ignored field, so a malformed source never
silently produces a technically-valid-but-wrong artifact. `intensity ==
0` is accepted (a light contributing nothing is not itself invalid — an
authoring convenience for temporarily disabling a light without
deleting its node) but is a real, disclosed edge case worth an explicit
test, not an assumed-fine one.

**Renderable/Camera/Light co-existence on one Entity, precisely
stated:** at the `World` level, yes — nothing in D2's own component
model prevents an entity from carrying `Renderable` and `Light`
simultaneously (they are independent optional slots, like `Camera`/
`Renderable` already are); at the *scene authoring grammar* level, no,
not this round, per the constraint above — a scene wanting both on
"the same visual object" authors two nodes and a parent/child
relationship instead. This is a disclosed, real gap between what
`World` can represent and what this round's own grammar can author
directly — future work, not silently pretended away.

### D4. Material — recommendation: exactly one new kind, `LitTextured`, no `LitColored`, no material artifact schema change

**Decision:** `MaterialKind::LitTextured` — one new enumerator on the
existing closed enum, reusing `MaterialAssetData`'s own exact, unchanged
shape (`{kind; textureAsset; filter; addressMode;}`) — a `LitTextured`
material still names exactly one texture/sampler pair, identically to
`UnlitTextured`; the only difference is which built-in shader pair
Runtime's own closed mapping resolves `kind` to, and that shader's own
use of the newly-widened, fragment-visible uniform binding (D5).
**No material artifact schema-version bump is required** — the 32-byte
record's own `kind` field is already a plain `u32` whose *decoded,
valid* value set widens (from `{0}` to `{0, 1}`); every existing,
already-cooked `kind=0` artifact continues decoding identically, byte
for byte; `decodeMaterialArtifact()`'s own existing `UnknownMaterialKind`
rejection path is unaffected in shape, only in which raw values it now
accepts.

**Why no `LitColored` (untextured lit) kind this round:** this Spec's
own "priority coverage" (one Directional, one Point, minimal diffuse,
multi-entity shared light data) names exactly one real consumer —
`LitTextured`. Adding a second, parallel kind now would be exactly the
kind of speculative, no-real-consumer-yet addition ADR-0059 D2 already
rejected once for a color-tint field, and this Spec's own Non-Goals
explicitly name "emissive/transparent materials" as out of scope, which
an untextured-lit kind's own natural next use case would edge toward.
If a genuine untextured-lit consumer appears later, it is a small,
additive follow-up — matching `MaterialKind`'s own already-disclosed
growth path exactly (ADR-0059's own "Consequence, disclosed").

**Existing `UnlitTextured` output must be byte-unchanged:** its own
shader, artifact shape, and Runtime-side mapping are untouched by this
Spec — confirmed structurally, not merely asserted, since `LitTextured`
is a wholly additive enumerator value and a wholly separate,
newly-added Runtime-side shader-pair mapping entry, touching no existing
code path `UnlitTextured` itself runs through.

### D5. Frame lighting data — recommendation: reuse and widen the existing single uniform buffer binding; the one real, disclosed RHI decision is widening its Vulkan stage visibility, not adding a new binding

**Decision:** the frame lighting array is packed into the *same*
uniform buffer the camera view/projection matrices already occupy —
appended after the existing 32 floats, at a fixed offset, in a fixed,
`std140`-compatible layout (exact per-light struct size/alignment and
maximum count fixed at Plan time, informed by this Decision's own
analysis below). **No new descriptor binding, no `PipelineCreateParams`
shape change, no new `Device` resource-creation call.**

**Why this is sufficient, not merely convenient — grounded in the real
constraint found during this Spec's own drafting (Pre-draft
verification):** `PipelineCreateParams` has no mechanism for a second
uniform buffer binding today, and `Device::createPipeline()`'s own
internal descriptor-set-layout construction is hardcoded to at most two
bindings (the uniform buffer, always; the combined-image-sampler,
conditionally). Adding a genuinely *second*, independent uniform buffer
binding would mean widening `PipelineCreateParams` with a new field,
extending `createPipeline()`'s own binding-count/pool-size logic to a
three-binding case, and extending the Shader-System-side expected-
descriptor-contract mechanism with a new binding-count shape — real,
disclosed, but avoidable: since this Spec's own lighting data is
inherently *per-frame*, exactly like the camera data it would sit
beside, and both are written by the exact same composition root at the
exact same point in `runFrame()`, there is no structural reason for them
to occupy two separate buffers or two separate bindings. Reusing the
one that already exists is the direct, minimal-diff answer, not an
avoided harder problem.

**The one real RHI change this decision does require, disclosed
precisely, not smoothed over (see ADR-0062):** the existing uniform
binding's own Vulkan `stageFlags` is hardcoded to
`VK_SHADER_STAGE_VERTEX_BIT` — a `LitTextured` shader's own fragment
stage cannot read the light data (or, for that matter, the camera data
already there) without this changing. The recommended fix widens it,
unconditionally, to `VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT`
for every `Pipeline` this engine creates, not merely `LitTextured`
ones — legal, zero-cost Vulkan (a pipeline layout permitting broader
shader-stage visibility than a given shader module actually uses is
valid and already exercised elsewhere in this codebase's own Vulkan
usage patterns); every existing shader (`minimal_mesh`, `textured_quad`)
simply continues not referencing this binding from its own fragment
stage, unaffected. This is the one, minimal, disclosed RHI-boundary
decision this Spec surfaces explicitly for Human Review rather than
silently assuming — see ADR-0062.

**Fixed maximum count — a real number, not left open:** recommend
**1 Directional + 4 Point** lights (5 total light "slots" in the fixed
array), chosen to comfortably exceed this Spec's own stated minimum
coverage (one of each) with modest headroom for the verification
scene's own two-light-kind-distinguishing proof (D10), without
speculatively over-provisioning for a use case no consumer this round
needs. This exact count is a Plan-time-adjustable constant, not an
architectural commitment — Human Review may pick a different number;
what is fixed here is that it *is* a small, fixed compile-time constant,
never a dynamically-sized array.

**Over-limit lights — recommendation: deterministic truncation, logged,
never a scene-load failure:** if a scene declares more lights of a
given kind than the fixed maximum, the first N in `lightEntities()`'s
own deterministic (ascending slot-index) order are used, and the rest
are silently-to-the-render-but-not-silently-to-the-log skipped — logged
once at scene-load time, not per-frame. **Why not a hard error:** a
scene that happens to exceed the cap should still load and render
something correct-looking with its own first N lights, matching this
codebase's own general "recoverable condition, not fatal" philosophy for
per-entity issues (Spec 0018's own present-but-unresolvable-material
skip is the closest precedent, though that case is genuinely
unrecoverable per-entity while this one is a scene-wide, at-cook-or-load-
time-knowable condition) — Human Review may instead prefer a hard
cook-time rejection (simpler to reason about, catches the condition
before it ever reaches Runtime); both are real, defensible options, and
this Spec states its own recommendation without foreclosing the
alternative.

### D6. Point-light attenuation — recommendation: a documented, explicitly non-physical linear-range falloff

**Decision:** `attenuation = clamp(1.0 - distance / range, 0.0, 1.0)`,
multiplied directly into that light's own diffuse contribution;
`distance` is clamped to a small positive epsilon before division
(`max(distance, 1e-4)`) to guard the zero-distance case without a branch
that could itself introduce a discontinuity. **Explicitly and
permanently non-physical** — real inverse-square attenuation
(`1 / distance²`), any energy-conserving or physically-calibrated
unit system, and any softer, smoothstep-shaped falloff curve are all
named, disclosed, deliberately deferred to a future, real PBR-adjacent
Spec, not silently approximated here.

**Why this shape, not inverse-square, evidence/reasoning stated
plainly:** inverse-square attenuation has no natural "range" concept at
all (it asymptotically approaches, never reaches, zero) — this Spec's
own D3 already requires an authored `range` field per Point light
specifically so a light's own influence is bounded and testable (the
verification scene, D10, needs a light whose contribution is visibly
different at different distances within a known, finite falloff
region). A simple linear-in-distance falloff is the smallest function
that is bounded, monotonic, zero at `range`, one at `distance = 0`, and
trivially unit-testable without any transcendental function or
division-by-a-value-that-can-legitimately-be-zero.

**Units:** Phase 1's own linear scalar convention throughout (matching
`intensity`'s own unitless-multiplier treatment) — `range`/`distance`
share whatever world-unit scale a scene's own `Transform` values
already use (this codebase's own existing scenes operate at a roughly
1–10 world-unit scale, per `scene_extraction.cpp`'s own documented
epsilon-choice rationale); no physical unit (lumens, candela, meters)
is claimed or implied.

### D7. Normal transform — recommendation: the object-to-world matrix's own upper-left 3×3, with an explicit, non-silent uniform-scale requirement for any `LitTextured`-bound entity

**Decision:** a vertex normal transforms by the object-to-world
matrix's own upper-left 3×3 submatrix directly — **not** an
inverse-transpose normal matrix. This is exactly correct for pure
rotation and uniform scale, and silently *wrong* (a skewed, non-unit-
length, non-perpendicular-to-the-surface result) under non-uniform
scale — a real, disclosed limitation, not an unstated one.

**Why this is not left as a silent gap, per this Spec's own explicit
"must not silently produce a mathematically wrong result" instruction:**
`World` has no matrix-inverse function anywhere in its own public API
today (Pre-draft verification) — computing a correct inverse-transpose
normal matrix would require introducing new math capability this
codebase does not yet have, a real, additional, disclosed cost this
Spec's own minimal scope does not currently justify. Instead, Runtime's
own per-entity `DrawItem`-build step (already the exact point Spec 0018
resolves each entity's own Material) additionally checks whether a
`LitTextured`-bound entity's own current `Transform.localScale` is
uniform (`scaleX == scaleY == scaleZ`, within a small epsilon, checked
per entity, per frame, cheaply) — a non-uniform-scale entity bound to a
`LitTextured` material is a real, explicit, logged, recoverable
per-entity condition (mirroring the existing present-but-unresolvable-
material skip's own severity exactly, D8/D9 of Spec 0018), never a
silently-wrong-looking render passed off as correct.

**Alternative, disclosed, not chosen:** implementing a real
inverse-transpose normal matrix (would require a new, small 3×3
inverse function — itself a small, real, addable piece of Core/World
math, not a large undertaking, but a genuinely new capability this
Spec's own drafting did not find an existing precedent for anywhere in
this codebase). Human Review may prefer this over the detect-and-skip
approach above if non-uniform scale on a lit entity is expected to be
common; this Spec's own recommendation optimizes for "ship the smallest
correct thing, disclose the real limitation explicitly" over
"implement more math than this round's own verification scene needs."

### D8. Lighting color space — recommendation: sample-time linearization is already free; no tone-mapping or gamma-encode is added, and this is a stated, permanent-for-Phase-1 limitation

**Decision:** an `Rgba8Srgb`-format texture sampled by the `Lit` shader
already receives real, hardware-decoded linear color values at the
point of sampling — this is existing, already-proven GPU behavior
(Spec 0016's own `textured_quad` golden proof), unchanged and
unaffected by this Spec. Lighting math (D6's own attenuation, the
Lambertian dot-product term) operates on these already-linear sampled
values and the light's own authored `color`/`intensity` (themselves
treated as already being in the same linear space, by convention, not
by any conversion this Spec performs). **The final summed color is
written directly to the target's own `Rgba8Unorm`/`Rgba8Srgb` color
attachment with no tone-mapping curve and no gamma-encode step of any
kind** — the exact same "no HDR intermediate target, no post-processing
pass" constraint every prior Spec in this codebase already operates
under, unchanged.

**Consequence, disclosed, not smoothed over:** a `LitTextured` scene
whose combined light contribution exceeds 1.0 in any channel simply
clips at the display format's own maximum representable value — no
highlight rolloff, no exposure control. This is an accepted, Phase 1-
appropriate limitation, not a defect this Spec attempts to solve;
real tone-mapping is explicitly named future Post-processing-Spec
territory (Non-Goals).

### D9. Runtime integration — recommendation: a new Runtime-private extraction function, deterministically ordered, updated unconditionally every frame, no format-rebuild interaction

**Decision:** light extraction is a new, Runtime-private, GPU-independent,
independently-testable function (mirroring `extractCameraMatrices()`/
`resolveMaterialAsset()`'s own exact precedent — explicit parameters,
no hidden `RuntimeApplication` state access), called once per frame
from `runFrame()`, at the same point camera-matrix extraction already
runs. `World` gains zero new dependency (still `Core` + `AssetSystem`
only) — this function lives in Runtime, reading `World`'s own already-
public `lightEntities()`/`getLight()`/`getWorldMatrix()` API, exactly
like every other Runtime-side `World` consumer already does.

**Ordering:** derived exclusively from `World::lightEntities()`'s own
deterministic (ascending slot-index) iteration — never an
`std::unordered_map`, matching this codebase's own repeatedly-stated
"never hash-bucket-order-dependent" discipline (Spec 0018's own
identical requirement for `computePendingMaterialIds()`'s own upload
ordering).

**Format change / material rebuild / shared frame uniform lifetime:**
the frame lighting data's own byte layout is entirely format-
independent (matching the camera data it sits beside) — it requires no
rebuild on a color-format change, unlike a `Pipeline`/`Material` itself.
It is re-derived and re-written into the shared uniform `Buffer`
unconditionally every frame (matching the camera data's own identical,
already-accepted per-frame-unconditional-write cost) — no dirty-
tracking, no "did any light actually change" optimization, matching
this Spec's own stated Non-functional "performance is not a goal this
round." This introduces no new synchronization concern beyond the
engine's own existing single-frame-in-flight model, for the identical
reason the camera data's own existing per-frame write already doesn't:
the buffer is host-visible and persistently mapped, and this engine's
own `submit()`-drains-the-previous-frame contract already guarantees
the GPU is done reading last frame's own values before this frame's own
CPU-side write happens.

### D10. Verification scene — recommendation: a new, independent lighting scene; Runtime's default bootstrap scene stays unswitched, matching Spec 0018's own precedent, but is named here as its own explicit Human Review choice, not silently assumed

**Decision:** a new, minimal scene authored specifically for this Spec
(not a reuse of `material_demo_scene` or `world_scene`) — real,
asset-sourced UV0-and-normal-carrying mesh geometry (gated on D1's own
prerequisite landing), one Directional light and one Point light, an
**asymmetric layout** (the Point light positioned close to one distinct
part of the scene's own geometry, the Directional light illuminating
the whole scene roughly uniformly from a fixed world direction) so the
two lights' own distinct contributions are visually separable in the
captured frame — never a symmetric setup where a self-consistent-but-
directionally-wrong render could coincidentally still look plausible. A
new image-regression golden, captured per ADR-0042's own "Initial
baseline bootstrap" procedure (direct human visual review plus the
build-time comparator's own self-consistency check plus a real
GPU/Validation-Layers run, since no prior golden exists to diff
against). The four existing goldens (`minimal_cube`, `world_scene`,
`textured_quad`, `material_demo`) are asserted byte-for-byte unchanged
throughout Implementation.

**Runtime's default bootstrap scene — explicit decision, not an
inherited default:** recommend **no**, matching Spec 0018's own P15
precedent exactly — `atlantis_runtime.exe` keeps loading `world_scene`;
the new lighting scene is verification-only, exercised through the new
fixture/golden, never wired into `main.cpp`'s own `BootstrapConfig`
population. **Why stated as its own decision rather than silently
inherited:** the previous Spec's own reasoning (keep the shipped
Runtime's own windowed output stable; prove the new path exclusively
through a golden) is sound but was itself an explicit Human-Review-
facing choice there (Spec 0018 D15's own Requirements text), not an
automatic default — this Spec asks the same question fresh rather than
assuming the prior answer carries forward unexamined.

**Fixture shape:** directly links and calls `Atlantis::RuntimeHost`'s
real Phase 1/Phase 2-equivalent functions (the new light-extraction
function, the widened per-frame uniform-buffer write, the `LitTextured`
material-realization call), following Spec 0018 D12's own precedent
exactly — not a fixture-private reimplementation.

### D11. Error domain, module boundaries, threading, ownership, C4062

- **Error domain:** new `SceneSourceParseError`/`SceneArtifactDecodeError`
  enumerators only where the light node introduces a genuinely new
  failure mode (an invalid `LightKind` token, a `range` on a
  `directional` light, non-finite/negative `color`/`intensity`/`range`)
  — reusing an existing enumerator wherever the failure mode is
  identical in kind to an existing one (a malformed token count, a
  missing field), matching Spec 0017/0018's own discipline exactly.
  `World`'s own `WorldError` gains no new enumerator (`setLight`/
  `getLight`/`removeLight` reuse the identical validate-then-mutate
  shape `setCamera`/`getCamera`/`removeCamera` already use, including
  reusing `WorldError::NoCameraComponent`'s own sibling-naming pattern
  for a new `NoLightComponent`).
- **Module boundaries:** `World` remains `Core` + `AssetSystem`-only,
  verified by the existing include-scanning test, unmodified in
  mechanism. `Atlantis::AssetSystem` remains `Core`-only. Light
  extraction stays Runtime-private, never a public Runtime API.
- **Threading:** unchanged — Phase 1's single-logical-thread baseline
  (ADR-0004) applies identically; the new extraction function documents
  "not thread-safe" exactly like every sibling function already does.
- **Ownership:** the frame lighting data introduces **no new GPU
  resource** — it is CPU-side data written into the already-owned
  camera `Buffer`; no new destruction-order concern is introduced
  anywhere in `RuntimeApplication`'s own member layout.
- **C4062:** every new closed `switch` this Spec's own Implementation
  introduces (`LightKind` translation in the extraction function,
  `MaterialKind::LitTextured` in Runtime's shader-pair mapping) gets its
  own `/w14062` positive-and-negative build probe, matching every prior
  Spec's own identical discipline.

### D12. Boundary with future lighting/material work

**Decision:** Shadow Foundation, PBR Material, IBL, and Post-processing
all remain independent, unblocked future Specs — nothing in this
Spec's own design presumes or forecloses any of their own future
shape, and no interface, field, or abstraction is pre-wired for any of
them ahead of its own approved spec, per AGENTS.md's own "no speculative
abstraction" principle.

## Architectural Impact

This Spec introduces real architectural decisions, filed as two
`Proposed` ADRs, mirroring Spec 0018's own module-boundary/artifact-
format-and-RHI-boundary split:

- [ADR-0061](../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md)
  — `World`'s own `Light` component shape and boundary (D2), the Scene
  Asset format's light-node extension and versioning contract (D3), and
  Material's new `LitTextured` kind (D4).
- [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
  — the frame lighting data's own layout and Runtime integration
  contract (D5, D9), the one real RHI decision this Spec requires (the
  uniform-binding Vulkan stage-visibility widening, D5), Point-light
  attenuation (D6), normal transform (D7), and the lighting color-space
  boundary (D8).

Neither ADR proposes a change to any already-`Accepted` ADR's own
Decision text. If D1's own recommended prerequisite ("Mesh Normal
Attribute Foundation") is drafted and approved, *that* Spec — not this
one — would carry its own Proposed Amendment to ADR-0045 and ADR-0058;
this Spec introduces no such amendment itself, since it does not itself
add a mesh vertex attribute.

## Alternatives Considered

- **Bundle the normal attribute into this Spec (D1 option (a)).**
  Considered directly, evidence-based — see D1's own full analysis.
  Rejected: makes this Spec's own single delivery meaningfully larger
  and harder to review/plan/verify as one coherent unit, for a
  component with no dependency on any lighting-specific decision this
  Spec makes.
- **A second uniform buffer binding for lighting data, instead of
  widening the existing one (D5).** Considered directly — see D5's own
  full analysis. Rejected as the *default* recommendation (a real,
  larger RHI change: a new `PipelineCreateParams` field, a third
  descriptor binding, a new pool-size entry, a new expected-contract
  shape) when reusing the existing single binding is structurally
  sufficient and requires only one, disclosed, minimal stage-visibility
  widening.
- **Two Light components (`DirectionalLight`/`PointLight`) instead of
  one tagged `Light` (D2).** Considered directly — rejected: doubles
  `World`'s own component-slot count for a mutually-exclusive property
  a single closed-enum tag already expresses correctly, matching
  `MaterialKind`'s own precedent over `Camera`+`Renderable`'s own
  "genuinely independent components" precedent.
- **A fourth manifest-style "kind" column or a general multi-group
  scene-node grammar (D3).** Considered directly and rejected on the
  same grounds Spec 0018's own D7 already rejected an equivalent
  manifest-kind-column proposal — a real grammar redesign this Spec's
  own minimal scope does not need, when the existing parent/child
  hierarchy already achieves the one real use case (a lit, visible
  fixture) a same-node combination would have provided.
- **Physically-based inverse-square Point-light attenuation (D6).**
  Considered directly — deferred, not chosen, since it introduces an
  unbounded-range light with no natural cutoff, in direct tension with
  this Spec's own authored, testable `range` field; named explicitly as
  real future PBR-adjacent work, not silently approximated.

## Testing & Verification Plan

- Unit tests (GPU-independent): `Light`/`LightKind` round-trip through
  `World`'s own `set/remove/getLight()`; `lightEntities()` deterministic
  ordering; the scene grammar's own new `light=` token-count case,
  every new `SceneSourceParseError`/`SceneArtifactDecodeError`
  enumerator driven by a real malformed input, not a mocked one
  (matching every prior Spec's own discipline); the light-extraction
  function's own direction/position derivation and attenuation formula,
  tested against hand-computed expected values for known transforms/
  distances.
- GPU-required tests: the new `LitTextured` material realizes correctly
  through Runtime's existing Phase 2 pipeline (reusing, not duplicating,
  Spec 0018's own `realizePendingMaterials()`); the widened uniform
  binding's own fragment-stage visibility is Validation-Layers-clean;
  the new fixture's own capture-compare test against its own new golden;
  a real, isolated proof that removing either light from the scene
  changes the captured frame from the golden (mirroring Spec 0018's own
  D12 negative-proof precedent exactly) — demonstrating each light's
  own contribution is real, not coincidental.
- The four existing goldens confirmed byte-for-byte unchanged (SHA-256/
  `git diff` evidence) throughout Implementation, not inferred from
  "tests passed."
- Vulkan Validation Layers clean across the full GPU test suite,
  including specifically the widened descriptor-binding-stage-visibility
  change (a real, GPU-observable proof this change is legal, not merely
  argued from the Vulkan spec's own text).
- `/w14062` C4062 positive-and-negative probes for every new closed
  `switch` this Spec's own Implementation introduces.
- Manual, human-performed Runtime windowed verification (Debug and
  Release), scoped identically to every prior Spec's own D13-shaped
  requirement — programmatic lifecycle smoke test plus a genuine human
  visual confirmation, no fabricated automated pixel readback.

## Risks & Open Questions

- **D1's own prerequisite is a real, disclosed scheduling risk, not a
  hidden one:** if "Mesh Normal Attribute Foundation" is not drafted or
  does not reach `Approved`, this Spec's own Plan cannot begin at all —
  stated plainly here so Human Review can weigh it explicitly rather
  than discover it during Plan drafting.
- **D5's own maximum-light-count constant is a real, disclosed
  trade-off** (buffer size and shader-loop cost scale with it; too low
  a cap makes D5's own truncation behavior more likely to matter for a
  realistic future scene) — Plan-time-adjustable, not fixed forever by
  this Spec.
- **D7's own uniform-scale restriction is a real, disclosed correctness
  boundary**, not merely a performance one — a scene author who
  attaches a `LitTextured` material to a non-uniformly-scaled entity
  gets a detected, logged skip (this Spec's own recommendation) rather
  than a silently-wrong render, but still does not get correct lighting
  on that entity without a future Spec adding a real normal-matrix
  inverse.
- **The exact file location for the new Runtime-private extraction
  function** (a new `light_extraction.h`/`.cpp`, or an addition to the
  existing `scene_extraction.h`/`.cpp`) is left open for Plan-time
  closure — a file-organization choice, not an architectural one.

## Out of Scope / Future Work

Repeating this Spec's own Non-Goals for visibility, plus the named
successor candidates this Spec's own closure unblocks:

- PBR/metallic-roughness material model — **"PBR Material"**, a real,
  independent future Spec, unblocked but not started here.
- Shadow mapping of any kind — **"Shadow Foundation"**, likewise.
- Image-based lighting / environment maps — **"IBL Foundation"**,
  likewise.
- Tone mapping, gamma-encode, HDR intermediate targets, or any other
  post-processing — **"Post-processing Foundation"**, likewise.
- Normal mapping / a tangent vertex attribute — blocked on, but a
  distinct, later extension beyond, D1's own recommended "Mesh Normal
  Attribute Foundation" prerequisite.
- A material graph, shader graph, hot-reload, editor, runtime asset
  mutation, a distributable Asset Catalog, or Android/iOS/Linux
  implementation — all remain exactly as out of scope as every prior
  Spec in this codebase already states them to be.

None of the above is designed, scaffolded, or interface-reserved for by
this Spec, per AGENTS.md's own "no speculative abstraction" principle.
