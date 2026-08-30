# Plan: PBR Material Foundation (Direct Lighting)

- **Spec:** [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)
  (`Approved`, carrying its own "Accepted Correction — 2026-08-30"
  for D9's own push-constant-layout discriminator, implemented in
  Milestone 5 below)
- **Status:** Approved / Ready for Implementation
- **Author:** slmao
- **Human Review Approval (2026-08-30):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-30, following one final,
  targeted review round that closed a real implementability gap in
  this Plan's own original Milestone 5 (its own "keyed off the bound
  `Material`'s own `pipeline().pushConstantSizeBytes()`" language named
  a mechanism that does not exist, per Spec 0023's own Accepted
  Correction), corrected Milestone 3's own RHI/Vulkan-Backend boundary
  wording and added a required fragment-stage push-constant check
  specific to `pbr-direct-lit`, moved `PbrBaseColorTextureNotSrgb`'s
  own error domain from Milestone 1 (Asset System) to Milestone 5
  (Runtime), and closed real touch-point gaps in Milestone 6 (reusing
  the already-declared `textured_quad_srgb` texture asset, confirming
  `AssetId` uniqueness, and completing the scene's own
  `TEXTURE_DEPENDENCIES` declaration) and Milestones 7–9 (mutation
  tests as temporary, reverted probes; the golden bootstrap's own
  clean-commit — not `main`-merge — requirement; an explicit stop
  condition restated in Milestone 9 itself). **This approval authorizes
  Implementation of this Plan only once its own Implementation PR has
  merged — not before.**

## Objective

Implement `MaterialKind::PbrDirectLit` exactly as Spec 0023, ADR-0066,
ADR-0067, and ADR-0062's own Accepted Amendment define it — a
metallic-roughness Cook-Torrance BRDF for Directional/Point lights,
sharing the existing single-texture Material architecture, the
extended 96-byte push constant, and the extended 320-byte Camera/
Lighting/CameraWorldPosition uniform buffer. This Plan maps those
Approved contracts to concrete files, atomic commit boundaries, and
verification — it does not redesign anything Spec 0023's own D1–D25
already settled.

## Milestones / Task Breakdown

### Milestone 1 — Material schema/artifact 56-byte bump, full asset/test migration

`MaterialAssetData`/`ParsedMaterialSource`/`DecodedMaterialArtifact`/
`MaterialMetadata` each gain `baseColorFactor`/`metallicFactor`/
`roughnessFactor` (ADR-0066 items 1–4); `MaterialKind::PbrDirectLit`
added to the closed enum; artifact schema version bumped, decode-
rejects any size other than 56 (ADR-0066 item 3's own byte table);
cook/decode dual range validation, new `BaseColorFactorOutOfRange`/
`MaterialFactorOutOfRange`-shaped error enumerators (item 5). **Error
domain stays strictly Asset-System-scoped:** every enumerator this
Milestone adds belongs to `CookError`/`TextureLoadError`-shaped
enumerations already owned by `atlantis_asset_system` and covers only
this module's own parameter/source/artifact failure modes — it names no
Runtime or RHI type and depends on neither.
`PbrBaseColorTextureNotSrgb` is **not** part of this Milestone — it is
a Runtime-side scene-load error, added in Milestone 5 (below), never an
Asset System concept.

- `src/asset_system/include/atlantis/asset_system/material_types.h`,
  `material_source.h`, `material_artifact.h`, `material_metadata.h`
- `src/asset_system/src/material_source.cpp`, `material_artifact.cpp`,
  `material_metadata.cpp`, `cook_material.cpp`, `load_material.cpp`
- Every currently-checked-in `.material.txt` re-authored to the
  version-2 grammar's own version line only (ADR-0066 item 2):
  `assets/materials/unlit_textured_quad.material.txt`,
  `assets/materials/lit_textured_quad.material.txt`,
  `assets/_test_fixtures/cmake_material_declaration_test.material.txt`
- Every existing Asset System material test touching the 32-byte
  layout, the 4-field DTO, or a version-1 source literal (fixed-byte
  round-trip, cook/decode, artifact-size, CMake-declaration tests under
  `tests/asset_system/`)

**Atomic:** schema, DTO, artifact encode/decode, and every existing
asset/test that assumes the old shape land in one commit — a
half-migrated 32/56-byte state is never checked in.

### Milestone 2 — Camera/Lighting/CameraWorldPosition 320-byte layout

Per ADR-0062's own Accepted Amendment: `CameraMatrices` (128B) and
`FrameLightingData` (176B) stay byte-for-byte unchanged, at their
existing offsets; a new `CameraWorldPositionData` (16B,
`alignas(16)`, explicit tail pad) is appended after both, at offset
304. A new `extractCameraWorldPosition(const Mat4& cameraWorldMatrix)`
free function (columns 12/13/14, mirroring `extractCameraMatrices()`'s
own existing `eye` derivation, `scene_extraction.cpp:107`) is added
alongside `extractCameraMatrices()`/`extractFrameLightingData()` —
`extractCameraMatrices()`'s own signature/return type is not widened.

- `src/runtime/include/atlantis/runtime/scene_extraction.h` (new
  struct + function declaration), `src/runtime/src/scene_extraction.cpp`
  (new function definition)
- `src/runtime/src/runtime_application.cpp`: `createBuffer()` call
  site (`:288-289`) widens from `sizeof(float)*32 +
  sizeof(FrameLightingData)` to also add `sizeof(CameraWorldPositionData)`;
  a new write call at the tail (`cameraData + 76` floats / byte offset
  304), placed alongside the existing camera-matrix write (`:539-541`)
  since both are per-frame, unconditional values — never gated on
  whether the current scene uses `PbrDirectLit`
- `tests/runtime/runtime_smoke_gpu_tests.cpp:76-79`: existing
  `static_assert`s (`kLightingByteOffset == 128`, `... + sizeof(FrameLightingData)
  == 304`) are **confirmed to need no change** — neither `CameraMatrices`
  nor `FrameLightingData` moved; add one new `constexpr`/`static_assert`
  pair for the tail (`kCameraWorldPositionByteOffset == 304`,
  `... + sizeof(CameraWorldPositionData) == 320`), independently
  derived, never copied from production's own literal
- Test-only fixtures that create their own, separate camera buffer for
  a non-PBR golden (`tests/image_regression/fixture/lighting_demo_fixture.cpp:164`
  and siblings) are **confirmed out of scope** — each stays at its own
  existing size; only `RuntimeApplication`'s own real, shared buffer
  widens

**Atomic:** the struct, the extraction function, the buffer-size
change, and the independent test assertion land in one commit — the
buffer is never checked in at a size neither Slang reflection nor the
C++ layout probe (Spec 0023's own real-code evidence) agrees with.

### Milestone 3 — 96-byte push constant, uniform stage visibility, shader-compiler contract

Per ADR-0067 D-3/D-4: a new, `alignas(16)` `PbrPushConstants` (96
bytes, explicit tail pad at offset 88); every Pipeline's own
`VkPushConstantRange::stageFlags` uniformly widened to `VERTEX |
FRAGMENT` — one, unconditional change, not gated by `MaterialKind`.
**This `stageFlags` change is a Vulkan-Backend-*private* implementation
change, not an RHI API widening** — it lives entirely inside
`vulkan_device.cpp`/`vulkan_command_list.cpp` (Vulkan Backend), never
touching `src/rhi/include/`'s own public headers; `CommandList::
pushConstant(const void*, std::size_t)`'s own public signature,
`Pipeline`'s own opaque public shape, and every other RHI public type
are byte-for-byte unchanged — confirmed by Verification Checklist item
9 below, which this Milestone must keep passing.

- `src/vulkan_backend/src/vulkan_device.cpp:1035-1045`
  (`createPipeline()`'s push-constant-range `stageFlags`)
- `src/vulkan_backend/src/vulkan_command_list.cpp:320-324`
  (`pushConstant()`'s `vkCmdPushConstants` call)
- `src/runtime/src/material_realization.cpp:176, 301, 324`: `PbrDirectLit`'s
  own future call site (Milestone 5) will pass `pushConstantSizeBytes =
  96`; the three existing call sites are confirmed unchanged at `64`
- `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`/
  `.cpp`: new `pbrDirectLitExpectedDescriptorContract()`, mirroring
  `litTexturedExpectedDescriptorContract()`'s own two-binding shape
  (uniform + combined sampler)
- `src/tools/shader_compiler/compile_and_validate.cpp`: a new
  `"pbr-direct-lit"` branch in `validateDescriptorContractForStage()`
  (`:134-143`) — the existing `"minimal-renderer"`/`"textured-material"`/
  `"lit-textured"` branches, and their own `64`-byte descriptor shapes,
  are unmodified. `validatePushConstantsForVertexStage()`
  (`:168-178`, today hardcoded to `{offset:0, size:64}`, vertex-stage
  only) becomes contract-aware for its own existing vertex-only check —
  `pbr-direct-lit` expects `{offset:0, size:96}` there, the other three
  contracts keep `64`, unchanged. **A second, new check, specific to
  `pbr-direct-lit` only:** because `PbrDirectLit`'s own fragment stage
  genuinely reads push-constant data (unlike `UnlitTextured`/
  `LitTextured`, where a fragment-stage `pushConstantBuffer` reflection
  entry is the already-documented "stray, harmless, unread" case,
  ADR-0067 D-4), this Milestone adds a fragment-stage push-constant
  check exercised only for `pbr-direct-lit` — confirming the fragment
  stage's own reflected range is *also* `{offset:0, size:96}`, matching
  the vertex stage's own value exactly. The three existing contracts are
  not given this second check (their own fragment-stage push-constant
  reflection stays the documented, deliberately-unvalidated stray case).
- `src/tools/shader_compiler/main.cpp:67`: usage string's own
  `--expected-contract=<name>` documentation updated to name the new
  value

**Atomic:** the Vulkan-Backend-private `stageFlags` widening, the new
push-constant struct, and the shader-compiler's own updated validation
(both the vertex-stage size branch and the new `pbr-direct-lit`-only
fragment-stage check) land together — a build where the compiler
accepts a 96-byte block on one stage but not the other, or where the
Vulkan Backend still creates a 64-byte range, is never checked in.

### Milestone 4 — `pbr_direct_lit.slang` shader source and CMake wiring

Implements ADR-0067 D-1/D-2 exactly — no formula deviation. Vertex
input `position@0/uv@1/normal@2` (matching `lit_textured.slang`);
`CameraUniform` declares the full 320-byte struct (Milestone 2's own
field names); `[[vk::push_constant]] PbrPushConstants` (Milestone 3's
own 96-byte layout); one combined-image-sampler binding, unchanged
shape.

- `shaders/pbr_direct_lit/pbr_direct_lit.slang` (new)
- `shaders/pbr_direct_lit/CMakeLists.txt` (new, mirroring
  `shaders/lit_textured/CMakeLists.txt`'s own
  `atlantis_add_slang_shader_pair()` call, `EXPECTED_CONTRACT
  pbr-direct-lit`)
- `CMakeLists.txt` (root): unconditional `add_subdirectory(shaders/pbr_direct_lit)`,
  matching `lit_textured`'s own unconditional placement (`:95`) — this
  shader is production, built-in content, not test-only

### Milestone 5 — `MaterialKind::PbrDirectLit` dispatch, Runtime realization/rebuild, `Material`'s new PBR fields and layout discriminator

Per Spec 0023 D9 (as revised by its own **Accepted Correction —
2026-08-30**) and D11: `selectShaderPair()` gains a third, closed-switch
arm; `Material` gains a `MaterialPushConstantLayout` discriminator plus
three new PBR parameter values; `DrawItem` unchanged; PBR params flow
from `MaterialAssetData` through `createMaterial()` into `Material`'s
new fields and are read by `Renderer::drawFrame()`'s existing
per-`DrawItem` loop when building the push-constant payload. This
Milestone implements the Accepted Correction's own recommendation
exactly — no further design choice is left to Implementation:

- `src/runtime/include/atlantis/runtime/bootstrap_config.h`: new
  `pbrDirectLitVertexShaderReflectionPath`/`...FragmentShaderReflectionPath`,
  mirroring `litTexturedVertex.../FragmentShaderReflectionPath`
  (`:44-46`)
- `src/runtime/include/atlantis/runtime/runtime_application.h`,
  `src/runtime/src/runtime_application.cpp`: new
  `pbrDirectLitVertexSpirv_`/`...FragmentSpirv_`/
  `...VertexInputLayout_` members, loaded via the same
  `loadReflectionMetadata()` pattern as the existing pair
  (`:257-271`); `rebuildMaterialsForFormatChange()`/
  `realizePendingMaterials()` call sites (`:440-442`, `:654-656`)
  widened to also pass the PBR triple
- `src/runtime/include/atlantis/runtime/material_realization.h`,
  `src/runtime/src/material_realization.cpp`: `selectShaderPair()`
  (`:100-115`) gains the `PbrDirectLit` arm, no `default:` label
  (C4062-protected, `/w14062`/`/WX`);
  `realizeOneMaterialCandidate()` (`:119-183`) passes
  `pushConstantSizeBytes = 96` and
  `MaterialPushConstantLayout::PbrDirectLit` only for this kind (every
  other kind passes `pushConstantSizeBytes = 64` and
  `MaterialPushConstantLayout::ObjectToWorldOnly`, unchanged from
  today), forwards `materialData`'s three PBR fields into
  `createMaterial()`, and — for this kind only — resolves and checks
  the material's own texture `colorSpace == Srgb`, failing with a new
  `RuntimeInitError::PbrBaseColorTextureNotSrgb` sub-code on mismatch
  (ADR-0066 item 6). **Error domain, precisely:** this new enumerator
  belongs to `RuntimeInitError` (Runtime's own existing error type,
  `src/runtime/include/atlantis/runtime/runtime_application.h`'s own
  enum), added as one new arm to Runtime's own existing error-handling
  switch(es)/tests — never an Asset System type, and never depended on
  by Asset System (Milestone 1's own scope stays untouched).
- `src/renderer/include/atlantis/renderer/material.h`,
  `src/renderer/src/material.cpp`: implements the Accepted Correction's
  own exact shape —
  ```cpp
  enum class MaterialPushConstantLayout { ObjectToWorldOnly, PbrDirectLit };
  ```
  a new, required (non-defaulted) constructor parameter of this type;
  four new `[[nodiscard]] ... const noexcept` accessors
  (`pushConstantLayout()`, `baseColorFactor()`, `metallicFactor()`,
  `roughnessFactor()`); four new **plain, non-`const`** private value
  members (`MaterialPushConstantLayout pushConstantLayout_;
  std::array<float, 4> baseColorFactor_{1,1,1,1}; float
  metallicFactor_ = 1.0f; float roughnessFactor_ = 1.0f;`), each set
  exactly once in the constructor's own initializer list and never
  reassigned anywhere else — "immutable" by encapsulation (no setter),
  never by a `const`-qualified member, preserving `Material`'s own
  existing `= default` move-constructor/move-assignment operator
  exactly as today (a `const` member would silently delete it — the
  real defect this Correction closed). Every existing call site
  (`UnlitTextured`/`LitTextured`'s own two `createMaterial()` calls in
  `material_realization.cpp`) passes
  `MaterialPushConstantLayout::ObjectToWorldOnly` explicitly — never
  inferred, matching the Correction's own explicit prohibition on
  value-based inference.
- `src/renderer/src/renderer.cpp:27-41`: the existing per-`DrawItem`
  loop's own single `pushConstant()` call site is replaced by an
  exhaustive `switch (item.material->pushConstantLayout())` with **no
  `default:` label** (this repository's own `/w14062`/`/WX` already
  makes a missed case a compile error, exercised here exactly as
  `selectShaderPair()`'s own switch already exercises it) —
  `MaterialPushConstantLayout::ObjectToWorldOnly` builds and pushes the
  existing, byte-identical 64-byte `objectToWorld`-only payload;
  `MaterialPushConstantLayout::PbrDirectLit` builds and pushes the new
  96-byte payload (`objectToWorld` + `baseColorFactor()` +
  `metallicFactor()` + `roughnessFactor()` + explicit tail pad,
  ADR-0067 D-3's own exact layout) — both calling the existing,
  unmodified `CommandList::pushConstant(const void*, std::size_t)`.
  Never a new `DrawItem` field (D10, reaffirmed).

**Atomic:** the dispatch arm, the Runtime loading/plumbing, `Material`'s
new discriminator/fields, and `Renderer::drawFrame()`'s own new
exhaustive switch land together — `selectShaderPair()` gaining a case
with no corresponding Runtime member to load from, `Material` gaining
fields with no Renderer-side switch arm to consume them, or vice versa,
is never checked in.

### Milestone 6 — Sphere mesh, PBR material assets, validation scene

Per Spec 0023 D16/D17: one non-shared-vertex UV-sphere `.mesh.txt`,
offline-generated and checked in as an ordinary hand-authored text
source (never engine/tooling code); four `PbrDirectLit` material
assets spanning dielectric/metallic × rough/smooth; one new,
asymmetric four-node scene reusing the one sphere mesh via four
transforms.

**Real texture dependency, confirmed, not assumed:** `assets/CMakeLists.txt:87-94`
already declares `textured_quad_srgb` (`SOURCE
textures/textured_quad_source_srgb.png`, `COLOR_SPACE Srgb`) — an
already-cooked, already-`Rgba8Srgb` texture asset, today consumed only
by Spec 0016's own isolated color-space GPU test fixture, never by a
production Material. This Milestone reuses it for all four new PBR
materials — **no new texture PNG and no new `atlantis_add_texture_asset()`
call** — each material's own `atlantis_add_material_asset(...)`
declaration passes `TEXTURE textured_quad_srgb` (the exact single-value
argument name `unlit_textured_quad`'s own real declaration already
uses, `assets/CMakeLists.txt:131-139`); Runtime's own existing
`textureResourceMap_` (`AssetId`-keyed, ADR-0060 Decision 10) dedups the
one real GPU `SampledTexture` across all four materials automatically —
no new dedup logic needed.

**`AssetId` uniqueness, confirmed, not assumed:** every Asset System
`AssetId` is computed from an asset's own normalized *logical path*
(`computeAssetId(normalizeLogicalPath(...))`, unchanged by this Plan) —
never from content. The one new mesh (`pbr_sphere`) and each of the
four new materials (`pbr_dielectric_rough`/`pbr_dielectric_smooth`/
`pbr_metallic_rough`/`pbr_metallic_smooth`) each get their own distinct
`NAME`/`SOURCE` logical path, so each gets a distinct `AssetId` by
construction — no collision is possible under the existing mechanism,
confirmed by this same, already-relied-upon property every other
multi-asset scene in this codebase (`world_scene`, `material_demo`)
already depends on.

- `assets/meshes/pbr_sphere.mesh.txt` (new — exact topology/attribute/
  winding/pole requirements per Spec 0023 D16 detail)
- `assets/materials/pbr_dielectric_rough.material.txt`,
  `pbr_dielectric_smooth.material.txt`, `pbr_metallic_rough.material.txt`,
  `pbr_metallic_smooth.material.txt` (new — `kind: pbr_direct_lit`,
  `texture: textures/textured_quad_source_srgb.png`, each authoring its
  own distinct `metallic_factor`/`roughness_factor` pair spanning the
  four corners; `base_color_factor` left at its own default unless a
  real, disclosed reason to vary it is found during Implementation)
- `assets/scenes/pbr_material_demo.scene.txt` (new — four sphere
  nodes, one Directional + one Point light, asymmetric layout per D17;
  never wired into `atlantis_runtime`'s own `BootstrapConfig`, matching
  `material_demo_scene`'s/`lighting_demo_scene`'s own identical
  precedent — verification-only, consumed by Milestone 8's own new
  fixture)
- `assets/CMakeLists.txt`: `atlantis_add_static_mesh_asset()` for the
  sphere (mirroring `:17-20`'s own `minimal_cube` call);
  `atlantis_add_material_asset(... TEXTURE textured_quad_srgb)` ×4
  (mirroring `:131-139`'s own exact pattern); one
  `atlantis_add_scene_asset()` for the new scene, with
  `MESH_DEPENDENCIES pbr_sphere`, `MATERIAL_DEPENDENCIES` naming all
  four materials, and `TEXTURE_DEPENDENCIES textured_quad_srgb`
  (mirroring `material_demo_scene`'s/`lighting_demo_scene`'s own
  complete three-argument shape, `:150-155`) — no dependency argument
  omitted

### Milestone 7 — New tests

- **Asset System (GPU-independent):** fixed-byte round-trip for the
  56-byte artifact; version-bump rejection; per-parameter range-error
  tests at cook and decode time (Milestone 1); the new
  `Rgba8Srgb`-required scene-load rejection test (Runtime-level,
  reusing the existing Phase 1 dependency-resolution test harness)
- **CPU BRDF reference (GPU-independent, independently implemented —
  never calling `pbr_direct_lit.slang`'s own compiled output or sharing
  any helper with it):** dielectric/metallic at low/high roughness;
  Directional at several `N·L` including grazing; Point at/beyond
  `range`; multi-light accumulation; the `roughness=0`/`kMinAlpha` and
  `NdotL≤0` guard paths, asserted NaN/Inf-free
- **Shader reflection vs. C++ layout (GPU-independent):**
  `static_assert`/runtime cross-check of `PbrPushConstants` and the
  Camera/Lighting/CameraWorldPosition buffer against real Slang
  reflection JSON, matching Plan 0022's own independent-cross-check
  precedent — never a shared literal trusted from one side alone
- **Real GPU:** parameter-transmission test (two draws differing only
  in one PBR factor produce different pixels); initial realization;
  format-change rebuild mixing `PbrDirectLit` with another kind; a
  mixed `UnlitTextured`+`LitTextured`+`PbrDirectLit` scene; Spec 0022
  regression (a runtime `Light`/Transform change reflected next frame
  for a `PbrDirectLit` material); the required negative/mutation tests
  (Spec 0023 D17 detail — `metallicFactor` forced to 0,
  `roughnessFactor` forced constant, camera-position sign flipped,
  Fresnel blend disabled, Point attenuation bypassed — each confirmed
  to make its own test assertion or golden comparison actually fail).
  **Each mutation is a temporary, reversible verification probe, never
  committed code:** applied locally, its own failure confirmed, then
  fully reverted before the corresponding real test/fixture is
  committed — matching Plan 0022's own "three temporary mutation
  probes... reverted, working tree left clean" precedent exactly; the
  Implementation PR's own final diff contains no trace of any mutation.
- **Spec 0021 regression:** existing N=2/N=6 tests
  (`tests/runtime/material_realization_gpu_tests.cpp`) re-run
  unmodified; one new case substituting a `PbrDirectLit` material into
  an existing N-material format-change scenario
- **Existing five goldens:** re-run unmodified, confirmed byte-for-
  byte/pixel-for-pixel identical — required specifically because
  Milestone 3's `stageFlags` widening is a real, uniform Vulkan object
  change

### Milestone 8 — PBR fixture and golden-generator implementation (no golden yet)

- `tests/image_regression/fixture/pbr_material_demo_fixture.h`/`.cpp`
  (new, mirroring `lighting_demo_fixture.h`/`.cpp`'s own shape — an
  offscreen, `OffscreenTarget`-based, multi-cycle-safe harness — but
  creating its own 320-byte camera buffer, since this fixture is a
  from-scratch composition root, not `RuntimeApplication`)
- `tests/image_regression/golden_generator/pbr_material_demo_main.cpp`
  (new, mirroring `lighting_demo_main.cpp`)
- `tests/image_regression/CMakeLists.txt`,
  `tests/image_regression/golden_generator/CMakeLists.txt`,
  `tests/image_regression/fixture/CMakeLists.txt`: new target
  registrations

This milestone lands and passes its own real-pixel comparison-cycle
self-consistency check (ADR-0042's own bootstrap evidence requirement)
**before** Milestone 9's own golden files exist — no golden PNG/sidecar
is part of this commit. **Does not require first merging into
`main`:** ADR-0042's own "Source revision, precisely" requirement is a
*clean, already-committed working tree* at capture time — an
Implementation branch's own real, clean commit satisfies this exactly
as a `main` commit would; the golden's own sidecar records whatever
commit hash was actually clean at capture time, per ADR-0042 verbatim.

### Milestone 9 — New PBR golden (separate commit, ADR-0042 bootstrap)

`tests/image_regression/goldens/pbr_material_demo/pbr_material_demo_512x512_rgba8unorm.png`
+ sidecar. Captured against the clean commit Milestone 8 lands on
(Implementation's own branch, not necessarily `main` yet — see
Milestone 8 above); human-reviewed per Spec 0023 D17/ADR-0067 D-6's own
binding visual-distinguishability condition; landed in its own commit,
never folded into Milestone 8's. **Explicit stop condition, restated
here, not only in Risks below:** if the four corner cases
(dielectric/metallic × rough/smooth) are not visually distinguishable
from one another under the existing clamp-only output contract, this
Milestone does not land — no golden is captured, accepted, cropped,
re-lit, or otherwise adjusted to mask the problem. Implementation stops
and reports back; per Spec 0023's own D14/Risks, the correct next step
is a dedicated Output Transfer Function Spec, never a local,
`PbrDirectLit`-only patch.

## Files / Modules Touched (expected)

`src/asset_system/**material*`, `assets/materials/**`,
`assets/meshes/pbr_sphere.mesh.txt`, `assets/scenes/pbr_material_demo.scene.txt`,
`assets/CMakeLists.txt`, `src/runtime/**scene_extraction**`,
`src/runtime/**runtime_application**`, `src/runtime/**material_realization**`,
`src/runtime/**bootstrap_config**`, `src/renderer/**material**`,
`src/renderer/src/renderer.cpp`, `src/vulkan_backend/src/vulkan_device.cpp`,
`src/vulkan_backend/src/vulkan_command_list.cpp`,
`src/shader_system/**descriptor_contract**`,
`src/tools/shader_compiler/**`, `shaders/pbr_direct_lit/**`,
`CMakeLists.txt` (root), `tests/asset_system/**material*`,
`tests/runtime/runtime_smoke_gpu_tests.cpp`,
`tests/runtime/material_realization_gpu_tests.cpp`,
`tests/image_regression/fixture/pbr_material_demo_fixture.*`,
`tests/image_regression/golden_generator/pbr_material_demo_main.cpp`,
new CPU BRDF reference test file(s), new shader-reflection cross-check
test file(s), `tests/image_regression/goldens/pbr_material_demo/**`.

## Sequencing & Dependencies

M1 → M2/M3 (independent of each other) → M4 (needs M2's field names,
M3's contract) → M5 (needs M1's schema, M3's push-constant size, M4's
compiled shader) → M6 (needs M1's material grammar, M5's kind) → M7
(needs M1–M6 all in place) → M8 (needs M5/M6) → M9 (needs M8 on a
clean, already-committed commit — not necessarily merged into `main`,
per Milestone 8's own note — but never the same commit as M8's own).

The five atomic groupings this Plan does not split across commits:
M1 (schema + full migration); M2 (buffer layout + its own independent
assertion); M3 (push-constant struct + Vulkan-Backend-private
`stageFlags` widening + shader-compiler contract — no RHI public API
change); M5 (dispatch + Runtime plumbing + `Material`'s new
discriminator/fields + `Renderer`'s new exhaustive switch); M8/M9
(fixture-then-golden, strictly separate commits, never combined).

## Verification Checklist

1. [ ] Unit/GPU-independent tests: Material artifact/cook/decode
   fixed-byte and round-trip (M1); CPU BRDF reference, independently
   implemented (M7); shader-reflection-vs-C++-layout cross-checks for
   both `PbrPushConstants` and the 320-byte camera buffer (M7);
   push-constant-classification and descriptor-contract unit coverage
   for the new `"pbr-direct-lit"` contract (M3).
2. [ ] Headless/GPU integration tests: initial realization;
   format-change rebuild mixing `PbrDirectLit` with another kind;
   mixed-`MaterialKind` scene; Spec 0022 dynamic-Lighting regression
   for `PbrDirectLit`; Spec 0021 N=2/N=6 regression plus one new
   `PbrDirectLit`-inclusive case (M7).
3. [ ] Negative/mutation tests (D17): `metallicFactor`→0,
   `roughnessFactor`→constant, camera-position sign flip, Fresnel
   blend disabled, Point attenuation bypassed — each confirmed to make
   its own test/golden comparison fail before being accepted (M7).
4. [ ] Image regression tests: existing five goldens byte-for-byte/
   pixel-for-pixel unchanged (re-run, not assumed); new
   `pbr_material_demo` golden captured per ADR-0042's bootstrap
   process, human-reviewed against the binding visual-
   distinguishability condition (M8/M9).
5. [ ] Vulkan Validation Layers clean across the full `ctest -L gpu`
   run, both Debug and Release.
6. [ ] `ctest -LE gpu` and `ctest -L gpu`, both configurations.
7. [ ] `ATLANTIS_BUILD_TESTS=OFF` configure+build produces a working
   `atlantis_runtime.exe` with zero test executables, re-cooking every
   asset (including the new sphere/materials/scene) successfully.
8. [ ] C4062 (`/w14062`/`/WX`): both `selectShaderPair()`'s own
   `MaterialKind` switch and `Renderer::drawFrame()`'s own new
   `MaterialPushConstantLayout` switch (Milestone 5) confirmed to fail
   to compile if a case is omitted from either.
9. [ ] Module/link graph: `Atlantis::AssetSystem` still links
   `Atlantis::Core` only; `Atlantis::RHI`'s public API confirmed
   byte-for-byte unchanged (no new type/field/method — the Milestone 3
   `stageFlags` widening is Vulkan-Backend-private, not an RHI change);
   `Atlantis::Renderer`'s own public API gains exactly four new
   `Material` accessors (`pushConstantLayout()`, `baseColorFactor()`,
   `metallicFactor()`, `roughnessFactor()` — corrected from an earlier
   count of three, Spec 0023's own Accepted Correction).
10. [ ] `git diff --check` clean on the final Implementation diff.

## Rollback Plan

Each Milestone is independently revertible in reverse order (M9 before
M8 before M7 …) since later milestones only add new call sites/files
and never rewrite an earlier milestone's own shape. Reverting M2/M3
alone (buffer/push-constant widening) also requires reverting every
milestone that depends on their new field names/sizes (M4 onward) —
called out explicitly if a partial rollback is ever needed, never
attempted silently.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No deltas beyond this Plan's own Verification Checklist above.

## Risks (Plan-level, not previously identified by the Spec/ADRs)

- **M3's shader-compiler contract-awareness for push-constant size is a
  real, concrete implementation requirement this Plan is the first to
  spell out precisely** — `validatePushConstantsForVertexStage()`
  today takes no `expectedContract` parameter at all (unlike its
  descriptor-contract sibling); Implementation must add one without
  changing the fixed `64`-byte expectation for the three existing
  contracts.
- **Milestone 8's fixture needs its own, independent 320-byte camera
  buffer** — it is not `RuntimeApplication` and does not share that
  class's own buffer-creation code path; Implementation must apply
  Milestone 2's layout there too, independently, not by refactoring
  `RuntimeApplication` and the fixture onto shared code (out of this
  Plan's own scope).
- **D17's own binding visual-distinguishability condition (Spec 0023,
  ADR-0067 D-6) is a real stop condition for Milestone 9, not merely a
  checklist item** — if the human reviewer capturing the golden finds
  the four corner cases indistinguishable under the existing hard-clip
  output contract, Implementation must stop before landing that
  golden and report back rather than proceeding to a dedicated Output
  Transfer Function Spec unilaterally.
