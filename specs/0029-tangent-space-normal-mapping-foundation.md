# Spec: Tangent-Space Normal Mapping Foundation

- **Status:** Draft
- **Author:** slmao
- **Created:** 2026-09-06
- **Related Plan(s):** None yet — Plan follows once this Spec is Approved.
- **Related ADR(s):** [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (`Proposed` — cooked mesh tangent representation), [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
  (`Proposed` — PBR normal-map material/descriptor/shader contract)

## Summary

Adds tangent-space normal mapping to the `PbrDirectLit` material's own
no-environment path: a new, cooker-generated mesh tangent attribute
(ADR-0073), a new, optional per-material normal-map texture reference
and a new shader variant (ADR-0074), and one new, independent demo
scene/fixture/golden. Every existing material, mesh, and golden is
unaffected — no normal-mapped material exists until a scene explicitly
authors one.

## Motivation / Problem Statement

The engine's PBR path (Spec 0023) shades every surface using only its
own per-vertex geometric normal — a flat sphere or a flat ground plane
can never show surface micro-detail (bumps, grooves, weave patterns)
without subdividing real geometry. Tangent-space normal mapping is the
standard, minimal way to add that detail using an existing texture-
sampling path this engine already has (Spec 0016). No tangent
attribute, no second material texture slot, and no normal-map shader
variant exist anywhere in the current codebase (confirmed directly
against `mesh_artifact.h`, `material_types.h`, and every `.slang` file
in `shaders/` — see ADR-0073/ADR-0074's own Context sections).

## Goals

- One new, mandatory, cooker-generated tangent attribute on the
  static mesh vertex format (ADR-0073).
- One new, optional, per-material tangent-space normal-map texture
  reference, consumed only by `PbrDirectLit` materials with no
  environment configured (ADR-0074).
- Every existing mesh, material, shader, and golden remains byte-
  identical/behavior-identical when no normal map is authored.
- One new, independent, Human-Review-approved golden demonstrating
  the feature, added the same way Spec 0028's own showcase golden was
  (a new fixture, never touching any existing scene).

## Non-Goals

- glTF import or any external mesh-format interoperability.
- Runtime- or shader-generated tangents (screen-space derivatives or
  any other generation happening after cook time) — ADR-0073's own
  explicit Decision.
- A second UV set, parallax/relief mapping, anisotropy, clear coat, or
  any other advanced material term beyond one tangent-space normal
  texture.
- Skinning, animation, or any vertex-deformation interaction with
  tangents.
- Any post-processing pass.
- A second graphics backend.
- Combining a normal map with `pbr_ibl.slang` (the environment-enabled
  PBR path) — ADR-0074's own explicit scope boundary; see Open
  Questions for the recommended behavior when both are configured for
  the same material.
- Widening the descriptor pool's own sampler-capacity ceiling (fixed
  at 4 per Pipeline, confirmed in `vulkan_device.cpp` — ADR-0074 stays
  within it) or adding a new descriptor-peak test.
- Changing `atlantis_runtime`'s own default scene (`integrated_showcase_demo`,
  Spec 0028) — this Spec's own demo is independent and additive.

## Requirements

### Functional

- **FR1 (Tangent generation, offline, deterministic).** The Asset
  Cooker computes a per-vertex tangent (object-space xyz + handedness
  w) from each mesh's own existing position/UV0/normal/index data,
  using the algorithm ADR-0073 fixes exactly (per-triangle UV-Jacobian
  accumulation, Gram-Schmidt orthogonalization against the vertex's
  own normal, sign-based handedness). Re-cooking the same source
  produces byte-identical tangent output. No tangent is ever
  hand-authored in the mesh source grammar, and no Runtime or shader
  code ever computes one.
- **FR2 (Mesh artifact schema).** `kMeshArtifactSchemaVersion` becomes
  `4`; per-vertex stride becomes 60 bytes (tangent xyzw appended at
  offset 44); a new `kMeshArtifactTangentOffsetBytes = 44` constant is
  added. Schema versions 1–3 are rejected outright — no migration
  reader (ADR-0073).
- **FR3 (Degenerate-input rejection).** A mesh source whose own
  UV/index/normal data cannot yield a well-defined tangent for at
  least one vertex (degenerate/zero-area UV triangles covering that
  vertex entirely, an orthogonalization-degenerate accumulated
  tangent, or a vertex referenced by zero triangles) fails cooking
  outright with a new, distinct error — no partial artifact is ever
  written (ADR-0073 item 5).
- **FR4 (Decode-time re-validation).** `decodeMeshArtifact()`
  independently re-validates the tangent's own unit length (the same
  double-precision, `[0.9801, 1.0201]`-tolerance method
  `NonUnitNormal` already established) and that handedness decodes to
  exactly `+1.0` or `-1.0` — two new, distinct `ArtifactDecodeError`
  enumerators (ADR-0073 item 6).
- **FR5 (Material schema).** `MaterialAssetData`/
  `DecodedMaterialArtifact`/`ParsedMaterialSource`/`MaterialMetadata`
  each gain one optional `AssetId normalMapTexture` field (`0` = none).
  `atlantis_material_source_version` becomes `3`: the existing 8-line
  form remains valid unchanged (no normal map); a 9th, optional line
  (`normal_map: <path>`) adds one. Every existing `.material.txt` file
  needs exactly its own version-marker line changed, nothing else.
  `kMaterialArtifactSchemaVersion` becomes `3` (56 → 64 bytes).
  Versions 1–2 rejected outright (ADR-0074 item 1).
- **FR6 (Per-material shader selection).** `selectShaderPair()` gains
  a per-material parameter (whether the realizing material's own
  `normalMapTexture != 0`). For `PbrDirectLit` with no environment
  configured: `pbr_direct_lit.slang` (unchanged) or the new
  `pbr_direct_lit_normal_map.slang`. The existing scene-wide
  `environmentEnabled` dispatch to `pbr_ibl.slang` is unaffected in
  either direction (ADR-0074 item 2; see Open Questions for the one
  combination this leaves for Human Review).
- **FR7 (Shader integration).** The new shader consumes the tangent
  attribute at vertex location 3, transforms it via the *same*
  `(float3x3)objectToWorld` submatrix and the *same*
  `checkConformalTransform()` gate `pbr_direct_lit.slang`'s own normal
  transform already relies on (never a new or different transform
  path), reconstructs an orthonormal TBN basis in the fragment stage,
  samples the normal map (OpenGL/DirectX tangent-space convention,
  `[0,1]` RGB8 → `[-1,1]`), and substitutes the perturbed normal for
  the geometric one in the existing, otherwise-unmodified BRDF
  accumulation (ADR-0074 item 3).
- **FR8 (Descriptor contract, no capacity change).** A new
  `pbrDirectLitNormalMapExpectedDescriptorContract()` (4 bindings:
  camera@0, base-color@1, shadow-map@2, normal-map@3).
  `sampledTextureBindingCountFor()` returns `3` for this combination —
  confirmed within the existing, unmodified `{0,1,2,3,4}` per-Pipeline
  ceiling and the descriptor pool's own fixed `4 × maxSets` sizing.
  No new descriptor-peak test; the existing `N=6`
  sky+shadow-cast test already proves the ceiling holds at a higher
  Pipeline count than this feature adds (ADR-0074 item 4/5).
- **FR9 (Existing behavior unaffected).** Every mesh, material,
  shader, and Pipeline that does not reference a normal map keeps its
  exact current byte layout, shader, descriptor contract, and
  `sampledTextureBindingCount` result. Every existing image-regression
  golden must remain byte-identical (no recapture) — see Testing &
  Verification Plan.
- **FR10 (New, independent demo).** One new mesh/material/scene using
  a normal map, one new fixture (mirroring `pbr_material_demo_fixture.{h,cpp}`'s
  own skeleton), and one new, independent golden, captured and
  reviewed through ADR-0042's existing two-phase process. Does not
  touch `integrated_showcase_demo` or any other existing scene.

### Non-functional

- **Performance:** one additional texture sample and a small,
  fixed amount of fragment-stage vector math per normal-mapped
  fragment; no new per-frame CPU cost beyond existing per-material
  bookkeeping.
- **Memory:** each affected mesh's own vertex buffer grows ~36%
  (44→60 bytes/vertex); one additional GPU texture per normal-mapped
  material, sharing the exact resource-ownership/dedup model
  `textureAsset` already uses.
- **Portability:** cook-time tangent generation is pure CPU
  arithmetic, portable to Android exactly as every other cook-time
  step already is; the new shader is ordinary Slang/SPIR-V, no
  platform-specific code.
- **Other:** none.

## Proposed Design

See ADR-0073 (mesh/cooker side) and ADR-0074 (material/descriptor/
shader side) for the complete, byte-exact decisions. Summary of the
new demo:

- **New mesh:** reuses `pbr_sphere.mesh.txt` (already UV-mapped,
  already has smooth normals) — no new geometry needed; the cooker's
  own new tangent-generation step applies to it automatically once
  ADR-0073 lands, requiring no change to the mesh source file itself.
- **New material:** one new `.material.txt` (e.g.
  `pbr_normal_mapped_demo`), `kind: pbr_direct_lit`, reusing the
  existing `textured_quad_srgb` base-color texture, with a new
  `normal_map: <path>` line naming one new, small, authored normal-map
  texture asset (a real gap — no normal-map texture exists in
  `assets/textures/` today; one is added as part of Implementation,
  following the exact same `atlantis_add_texture_asset()` cook
  declaration every existing texture already uses, `TextureColorSpace`
  set to the existing non-sRGB/linear-data variant already used for
  non-color data, if one exists, or confirmed against real code at
  Plan time rather than assumed here).
- **New scene:** one new, small scene (camera + one directional light
  + one or two spheres using the new material) — independent of
  `integrated_showcase_demo`, `pbr_material_demo`, and every other
  existing scene, following the exact same "verification-only,
  not wired into `atlantis_runtime`'s own `BootstrapConfig`" precedent
  `pbr_material_demo_scene`/`hdr_roll_off_demo_scene` already
  establish.
- **New fixture/golden_generator/gpu_tests trio:** mirrors
  `pbr_material_demo_fixture.{h,cpp}`/`pbr_material_demo_main.cpp`/
  `pbr_material_demo_gpu_tests.cpp`'s own exact shape, pointed at the
  new scene, using the new
  `pbrDirectLitNormalMapVertexInputLayout()`-style vertex-layout
  function (location 0/1/2/3 = position/uv/normal/tangent) alongside
  the existing `pbrDirectLitVertexLayout()` (still needed for any
  non-normal-mapped material the same fixture might also load).

## Architectural Impact

**Yes — two Proposed ADRs, one per real architectural surface this
Spec touches, confirmed by direct code inspection to be genuinely
separate decisions (mirroring how Spec 0023's own PBR addition split
into ADR-0066 asset-side / ADR-0067 shader-side):**

1. [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
   — the mesh artifact schema/version bump and the new cook-time
   tangent-generation algorithm (Asset System's own module boundary).
2. [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
   — the material schema/version bump, the new per-material shader-
   selection axis, the new shader file, and the new descriptor
   contract (Asset System + Shader System + Runtime's own material
   realization).

No other architectural surface is touched: no RHI public API beyond
one additive enum value each in two existing, already-designated-
extensible enums (`VertexAttributeFormat`, `VertexAttributeType`,
both ADR-0073 item 8); no RenderGraph change; no new module boundary;
no new dependency; no descriptor-pool-capacity/growth-model change
(confirmed within the existing ceiling, ADR-0074).

## Alternatives Considered

See ADR-0073's and ADR-0074's own "Alternatives Considered" sections
for the complete, disclosed reasoning (hand-authored tangents,
runtime/shader-side generation, per-face non-shared tangents, a
determinant-sign mirroring correction, a shader specialization-
constant toggle, widening the sampler ceiling to 5, and a single
branching shader) — not restated here to avoid duplicating one
argument across three documents.

## Testing & Verification Plan

- **Byte-unchanged, no recapture:** all 9 existing committed goldens
  (`minimal_cube`, `world_scene`, `textured_quad`, `material_demo`,
  `lighting_demo`, `ibl_material_demo`, `pbr_material_demo`,
  `hdr_roll_off_demo`, `integrated_showcase_demo`) must remain byte-
  identical. `world_scene_loaded` (reuses `world_scene`'s own golden)
  and `sky_background` (a live-render discriminative comparison, no
  committed golden) must both continue passing unchanged.
- **New, independent golden:** exactly one, for this Spec's own new
  normal-mapped demo scene, captured and human-reviewed through
  ADR-0042's existing two-phase candidate-generate → review process —
  never auto-accepted.
- **GPU-independent unit tests** (new): the tangent-generation
  algorithm against hand-computable inputs (a single flat-UV triangle
  with a known expected tangent/handedness), each new
  `DegenerateTangentBasis`/`NonUnitTangent`/`InvalidTangentHandedness`
  failure path, and `sampledTextureBindingCountFor()`'s new `3`-result
  case (mirroring the existing 4-case lock-down test's own style).
- **GPU-required tests** (new): the new fixture's own non-degenerate-
  frame proof (mirrors `pbr_material_demo_gpu_tests.cpp`'s own first
  TEST_CASE) and a discriminative pixel check confirming the normal
  map visibly changes shading versus the same material rendered
  without one (two renders, one fixture, mirroring Spec 0028's own
  R1/R2 differential methodology rather than an exact predicted RGB
  value).
- **No new descriptor-peak test** — the existing `N=6` sky+shadow-cast
  test already proves the ceiling holds at a Pipeline count and
  sampler-binding shape (4) this feature does not exceed; re-run
  unchanged.
- Full `ctest -LE gpu`/`ctest -L gpu`, Debug and Release; Vulkan
  Validation Layers clean; `git diff --check`; module/link/`Vk*`
  isolation; fresh `ATLANTIS_BUILD_TESTS=OFF` build.

## Risks & Open Questions

- **Q1 (environment + normal map combination).** ADR-0074 leaves
  undecided what happens when a `PbrDirectLit` material with a normal
  map is realized in a scene that also configures an environment
  (would need a 5th sampler binding, outside the current ceiling).
  **Recommendation:** at Runtime material-realization time, treat this
  exactly like an unresolved mesh/material reference already is
  (`runtime_application.cpp`'s own established "recoverable, per-
  entity, log, skip" precedent) — log once and skip realizing that
  material's own affected entities for the frame, rather than
  silently dropping the normal map and falling back to `pbr_ibl.slang`
  (which would render *something* plausible but silently discard an
  authored feature) or hard-failing the whole scene load. This never
  worsens an existing scene (no current scene authors this
  combination) and never invents a new fallback shader nobody asked
  for.
- **Q2 (normal-map texture color space).** `TextureColorSpace`
  (Spec 0016) already distinguishes sRGB (color) from linear
  (non-color) data for the base-color/environment paths; a tangent-
  space normal map's own RGB channels are directional data, not
  color, and must never be sRGB-decoded. **Recommendation:** confirm
  at Plan time which existing `TextureColorSpace` enumerator already
  means "linear, no hardware decode" (used today for the DFG LUT or
  an equivalent non-color texture) and reuse it verbatim for the new
  normal-map texture asset — no new `TextureColorSpace` value.
- **Q3 (mirrored/negative-determinant transform limitation).**
  ADR-0073 documents, but does not correct, that a negative-
  determinant conformal transform (passes `checkConformalTransform()`
  today) flips true tangent-frame chirality, producing a visually
  inverted normal-map result for that one entity. **Recommendation:**
  accept this as a disclosed limitation for this Spec (no current
  scene authors a negative-determinant transform); revisit only if a
  future scene genuinely needs one, as its own separate, disclosed
  decision — not solved speculatively here.

## Out of Scope / Future Work

- Combining normal mapping with `pbr_ibl.slang` (widening the sampler
  ceiling to 5) — its own future Spec/ADR if ever needed.
- glTF import, a second UV set, parallax/relief mapping, anisotropy,
  clear coat, skinning/animation interaction, post-processing, and a
  second graphics backend all remain explicitly out of scope and are
  not unblocked by this Spec.
- A determinant-sign correction for mirrored transforms (Q3) — its
  own future, disclosed decision if a real scene ever needs it.
