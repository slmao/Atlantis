# Spec: Tangent-Space Normal Mapping Foundation

- **Status:** Draft
- **Author:** slmao
- **Created:** 2026-09-06
- **Related Plan(s):** None yet — Plan follows once this Spec is Approved.
- **Related ADR(s):** [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (`Proposed` — cooked mesh tangent representation and existing-mesh
  migration), [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
  (`Proposed` — material/Renderer-API/descriptor/shader contract, both
  direct-lit and IBL paths), [ADR-0045 Proposed Amendment — 2026-09-06](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (mesh format-scope sentence gains tangent), [ADR-0058 Proposed Amendment — 2026-09-06](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (the "one, single vertex layout" closed attribute-count/byte-size
  Decision gains tangent as a fifth attribute — a genuine conflict
  found by direct inspection, mirroring ADR-0063's own identical
  precedent for the normal attribute; ADR-0063 itself needs no
  amendment)

## Summary

Adds tangent-space normal mapping to `PbrDirectLit` materials on
**both** the no-environment and IBL-enabled paths: a new, cooker-
generated mesh tangent attribute (ADR-0073), a new, optional per-
material normal-map texture reference, two new shader variants and a
`Renderer` public-API extension (ADR-0074), and one new, independent
demo scene/fixture/golden combining PBR + normal map + IBL + sky +
directional shadow.

**Compatibility is precise, not blanket:** mesh and material **source
and artifact formats change** (new schema versions, new stride/record
size, new fields) — every mesh gets re-cooked and two existing meshes
need real source edits (below). What stays byte-identical is **runtime
rendering behavior, shader selection, and descriptor-binding results
for every material that has no normal map**, and all 9 existing
committed goldens.

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
in `shaders/`).

## Goals

- One new, mandatory, cooker-generated tangent attribute on the
  static mesh vertex format (ADR-0073).
- One new, optional, per-material tangent-space normal-map texture
  reference, consumed by `PbrDirectLit` materials on **either** the
  no-environment path or the IBL-enabled path (ADR-0074).
- Precise compatibility: every no-normal-map material's runtime
  behavior, shader selection, and descriptor-binding results, and all
  9 existing goldens, stay identical; mesh/material source and
  artifact bytes do not (new schema versions).
- A real, bounded migration of the two existing mesh sources that
  cannot currently produce a well-defined tangent (below), preserving
  each one's own existing rendered output.
- One new, independent, Human-Review-approved golden demonstrating the
  feature with normal map + IBL + sky + directional shadow combined,
  added the same way Spec 0028's own showcase golden was (a new
  fixture, never touching any existing scene).

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
- A full MikkTSpace-equivalent tangent-generation algorithm (per-
  vertex angle-weighting, per-triangle-instance unsharing) — ADR-0073
  uses a simpler, exact, disclosed algorithm and never claims
  MikkTSpace compatibility.
- A determinant-sign (mirrored-transform) chirality correction —
  disclosed, deferred limitation (Open Questions).
- A generic bindless/material-graph texture mechanism — exactly one
  new, named, optional texture slot (ADR-0074).
- Changing `atlantis_runtime`'s own default scene
  (`integrated_showcase_demo`, Spec 0028) — this Spec's own demo is
  independent and additive.

## Requirements

### Functional

- **FR1 (Tangent generation, offline, deterministic).** The Asset
  Cooker computes a per-vertex tangent (object-space xyz + handedness
  w) from each mesh's own existing position/UV0/normal/index data,
  using the exact algorithm ADR-0073 fixes (per-triangle UV-Jacobian
  accumulation with per-face handedness tracked at accumulation time,
  Gram-Schmidt orthogonalization against the vertex's own normal,
  sign-based handedness). This is not an implementation of MikkTSpace.
  Re-cooking the same source produces byte-identical tangent output.
  No tangent is ever hand-authored in the mesh source grammar, and no
  Runtime or shader code ever computes one.
- **FR2 (Mesh artifact schema).** `kMeshArtifactSchemaVersion` becomes
  `4`; per-vertex stride becomes 60 bytes (tangent xyzw appended at
  offset 44); a new `kMeshArtifactTangentOffsetBytes = 44` constant is
  added. Schema versions 1–3 are rejected outright — no migration
  reader (ADR-0073).
- **FR3 (Degenerate-input and handedness-conflict rejection).** A mesh
  source whose own UV/index/normal data cannot yield a well-defined,
  unambiguous tangent for at least one vertex fails cooking outright
  with a distinct error, no partial artifact ever written:
  - `CookError::DegenerateTangentBasis` — every triangle touching a
    vertex is UV-degenerate (`|det| < 1e-12`), the vertex is
    referenced by zero triangles, or its accumulated tangent is
    orthogonalization-degenerate (`< 1e-6`, reusing
    `kDegenerateLengthEpsilon`).
  - `CookError::TangentHandednessConflict` — two or more of a shared
    vertex's own contributing triangles compute opposite-sign
    handedness (a genuine tangent discontinuity that would require a
    vertex split to resolve). **Never silently averaged** — this is a
    real, audit-proven failure mode (below), not a hypothetical one.
- **FR4 (Decode-time re-validation).** `decodeMeshArtifact()`
  independently re-validates: the tangent's own unit length (the same
  double-precision, `[0.9801, 1.0201]`-tolerance method
  `NonUnitNormal` already established — `NonUnitTangent`); its
  orthogonality to the vertex's own normal (`|dot(N,T)| < 1e-3`, one
  order of magnitude looser than the cook-time epsilon to tolerate
  float32 round-trip — new `NonOrthogonalTangent`); and that
  handedness decodes to exactly `+1.0` or `-1.0` (exact-equality check
  — `InvalidTangentHandedness`). Three new, distinct
  `ArtifactDecodeError` enumerators (ADR-0073).
- **FR5 (Existing-mesh migration, audit-driven, bounded).** A real
  probe of all 5 committed mesh sources (below) proves 3 need no
  change and 2 need a real source edit before this feature's own
  Implementation. Every migrated mesh keeps its existing position/
  color/normal data and existing rendered output unchanged; only UV
  and/or vertex-duplication topology changes to make a tangent
  computable (ADR-0073 Decision item 9).
- **FR6 (Material schema).** `MaterialAssetData`/
  `DecodedMaterialArtifact`/`MaterialMetadata` each gain one optional
  `AssetId normalMapTexture` field (`0` = none); `ParsedMaterialSource`
  gains `std::string normalMapLogicalPath` (empty = none), mirroring
  `textureLogicalPath`'s own identical pre-cook/post-cook type split.
  `atlantis_material_source_version` becomes `3`: the existing 8-line
  form remains valid unchanged (no normal map); a 9th, optional line
  (`normal_map: <path>`) adds one. Every existing `.material.txt` file
  needs exactly its own version-marker line changed, nothing else.
  `kMaterialArtifactSchemaVersion` becomes `3` (56 → 64 bytes).
  Versions 1–2 rejected outright (ADR-0074 item 1).
- **FR7 (Normal-map color space and sampler, closed).** The normal-map
  texture asset is cooked with the existing `TextureColorSpace::Unorm`
  enumerator (directional data, never sRGB-decoded) — no new
  enumerator; this is a fixed decision, not a Plan-stage question. The
  normal map is sampled through the material's own existing sampler
  (same `filter`/`addressMode` as base color) — no second sampler is
  introduced (ADR-0074 item 1).
- **FR8 (Per-material shader selection, both environment states).**
  `selectShaderPair()` gains a per-material parameter (whether the
  realizing material's own `normalMapTexture != 0`), independent of
  the existing scene-wide `environmentEnabled` dispatch. For
  `PbrDirectLit`: no environment → `pbr_direct_lit.slang` (unchanged)
  or new `pbr_direct_lit_normal_map.slang`; environment enabled →
  `pbr_ibl.slang` (unchanged) or new `pbr_ibl_normal_map.slang`
  (ADR-0074 item 2).
- **FR9 (Shader integration, both variants).** Both new shaders
  consume the tangent attribute at vertex location 3, transform it via
  the *same* `(float3x3)objectToWorld` submatrix and the *same*
  `checkConformalTransform()` gate the existing normal transform
  already relies on (never a new or different transform path),
  reconstruct an orthonormal TBN basis in the fragment stage, sample
  the normal map under the fixed convention (FR-Convention below), and
  substitute the perturbed normal for the geometric one in the
  existing, otherwise-unmodified BRDF/IBL accumulation. Neither
  existing shader file (`pbr_direct_lit.slang`, `pbr_ibl.slang`) is
  edited (ADR-0074 item 5).
- **FR-Convention (tangent-space convention, fixed to one).**
  OpenGL-style `+Y` tangent-space normal map; direct linear mapping
  `n = texel * 2.0 - 1.0` per RGB8 Unorm channel, no channel ever
  flipped by the shader; `B = cross(N, T) * tangent.w` (decoded
  handedness, never re-derived). A mirrored (negative-determinant)
  object transform still flips chirality under this convention — a
  disclosed, deferred limitation (Open Questions), not solved here.
- **FR10 (Descriptor contract and capacity, real, disclosed change).**
  `sampledTextureBindingCountFor()` returns `3` for `PbrDirectLit` +
  normal map with no environment (base-color@1, shadow-map@2,
  normal-map@3), and `5` with environment enabled (base-color@1,
  environment@2, DFG LUT@3, shadow-map@4, normal-map@5). The
  descriptor pool's own per-Pipeline sampler ceiling widens from 4 to
  5 (`ATLANTIS_CHECK` allowed-set gains `5`; pool sizing becomes
  `5 * maxSets`, up from `4 * maxSets`) — a real capacity change to
  ADR-0064's own model, confirmed necessary only for the 5-sampler
  IBL+normal-map combination. New descriptor contracts:
  `pbrDirectLitNormalMapExpectedDescriptorContract()` (5 entries) and
  `pbrIblNormalMapExpectedDescriptorContract()` (7 entries) —
  confirmed by direct reading of `descriptor_contract.cpp`'s own
  existing 4-entry/6-entry baselines, not estimated. The existing
  descriptor-**set**-count formula (`N+4`/`N+5`) is unaffected — this
  is a per-pool sampler-type **capacity** change, a different axis
  (ADR-0074 item 3/4).
- **FR11 (Renderer public-API extension, disclosed).** `Material`
  gains a second, optional, borrowed, non-owning normal-map texture+
  sampler pair, both-or-neither, constructor-only, mirroring
  `sampledTexture_`/`sampler_`'s own existing contract and ownership/
  destruction-order rules exactly. `createMaterial()` gains matching
  trailing parameters defaulting to `nullptr` — every existing call
  site is unaffected. `Renderer::drawFrame()` binds the normal map at
  `environmentBinding() == Ibl ? 5U : 3U`, mirroring the shadow-map
  binding's own existing `? 4U : 2U` conditional-index pattern
  verbatim. `RealizedMaterialCandidate` gains a second, optional
  `newNormalMapTexture` field, mirroring `newSampledTexture`'s own
  dedup contract (ADR-0074 item 2).
- **FR12 (Existing behavior unaffected).** Every mesh, material,
  shader, and Pipeline that does not reference a normal map keeps its
  exact current runtime rendering behavior, shader selection, and
  descriptor-binding results. Every existing image-regression golden
  must remain byte-identical (no recapture) — see Testing &
  Verification Plan.
- **FR13 (New, independent combined demo).** One new mesh/material/
  scene combining PBR + normal map + IBL + sky + directional shadow
  (proving the combined path this Spec adds, not a direct-only
  fallback), one new fixture (mirroring
  `pbr_material_demo_fixture.{h,cpp}`'s own skeleton), and one new,
  independent golden, captured and reviewed through ADR-0042's
  existing two-phase process. Does not touch
  `integrated_showcase_demo` or any other existing scene.

### Non-functional

- **Performance:** one additional texture sample and a small,
  fixed amount of fragment-stage vector math per normal-mapped
  fragment; no new per-frame CPU cost beyond existing per-material
  bookkeeping.
- **Memory:** each affected mesh's own vertex buffer grows ~36%
  (44→60 bytes/vertex); one additional GPU texture per normal-mapped
  material, sharing the exact resource-ownership/dedup model
  `textureAsset` already uses; one additional sampler-type descriptor
  slot per Pipeline in the pool's own capacity reservation (not a new
  sampler object — the same sampler is reused, FR7).
- **Portability:** cook-time tangent generation is pure CPU
  arithmetic, portable to Android exactly as every other cook-time
  step already is; both new shaders are ordinary Slang/SPIR-V, no
  platform-specific code.
- **Other:** none.

## Existing-mesh tangent-generatability audit

A real probe (per-triangle UV-Jacobian determinant and per-vertex
handedness-conflict computation, run against all 5 committed
`.mesh.txt` sources) found:

| Mesh | UV-degenerate triangles | Handedness conflicts | Verdict |
|---|---|---|---|
| `ground_plane.mesh.txt` | 0 | 0 | Clean — no change needed |
| `textured_quad_left.mesh.txt` | 0 | 0 | Clean — no change needed |
| `textured_quad_right.mesh.txt` | 0 | 0 | Clean — no change needed |
| `minimal_cube.mesh.txt` | 12/12 (100%) | n/a (no tangent computable at all) | **Fails — all 8 vertices share the literal identical UV `(0,0)`; must migrate** |
| `pbr_sphere.mesh.txt` | 0/many | 96/425 vertices (22.6%), pole rings | **Fails — must migrate** |

Both failures are real, audit-confirmed data problems, not
hypothetical edge cases; this Spec's own Goals require both resolved
before Implementation (ADR-0073 Decision item 9):

- **`minimal_cube.mesh.txt`:** needs a real per-face UV unwrap (e.g.
  the standard 6-face cube unwrap, each face's own 4 corners getting
  distinct `(u,v)` in `[0,1]`) so every triangle has a non-degenerate
  UV Jacobian. Position, color, and normal data — and this mesh's own
  existing rendered output in every non-normal-mapped golden — are
  unchanged; only UV values change.
- **`pbr_sphere.mesh.txt`:** needs further pole-vertex duplication
  (beyond whatever duplication already exists at the poles) so that no
  single vertex index is shared by triangles whose own UV winding
  produces opposite-sign handedness. Position, color, normal, and UV
  values for every already-distinct vertex are unchanged; only the
  pole regions gain additional duplicate vertices, exactly as the
  existing per-longitude pole duplication already establishes the
  precedent for.
- **Acceptance gate:** re-running the same audit against both migrated
  sources must report zero UV-degenerate triangles and zero handedness
  conflicts before ADR-0073's own Implementation is considered
  complete.
- If a future mesh cannot be bounded this way, Implementation must
  stop and report back for Human Review rather than introducing an
  optional vertex layout or a per-mesh Pipeline split — no such case
  is known to exist today among the 5 committed meshes.

## Proposed Design

See ADR-0073 (mesh/cooker side) and ADR-0074 (material/Renderer-API/
descriptor/shader side) for the complete, byte-exact decisions.
Summary of the new demo:

- **New mesh:** reuses `pbr_sphere.mesh.txt`, migrated per the audit
  above.
- **New material:** one new `.material.txt` (e.g.
  `pbr_normal_mapped_demo`), `kind: pbr_direct_lit`, reusing the
  existing `textured_quad_srgb` base-color texture, with a new
  `normal_map: <path>` line naming one new, small, authored normal-map
  texture asset (a real gap — no normal-map texture exists in
  `assets/textures/` today; one is added as part of Implementation,
  cooked with `TextureColorSpace::Unorm` per FR7).
- **New scene:** camera + directional light (shadow-casting) + sky +
  environment (IBL) + one or two spheres using the new normal-mapped
  material — proving the combined direct+IBL+shadow+sky+normal-map
  path this Spec's own Goals require, independent of
  `integrated_showcase_demo`, `pbr_material_demo`, and every other
  existing scene, following the same "verification-only, not wired
  into `atlantis_runtime`'s own `BootstrapConfig`" precedent
  `pbr_material_demo_scene`/`hdr_roll_off_demo_scene` already
  establish.
- **New fixture/golden_generator/gpu_tests trio:** mirrors
  `pbr_material_demo_fixture.{h,cpp}`/`pbr_material_demo_main.cpp`/
  `pbr_material_demo_gpu_tests.cpp`'s own exact shape, pointed at the
  new scene, using a new `pbrIblNormalMapVertexInputLayout()`-style
  vertex-layout function (location 0/1/2/3 = position/uv/normal/
  tangent).

## Architectural Impact

**Yes — two Proposed ADRs, one per real architectural surface this
Spec touches, confirmed by direct code inspection (mirroring how Spec
0023's own PBR addition split into ADR-0066 asset-side / ADR-0067
shader-side):**

1. [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
   — the mesh artifact schema/version bump, the cook-time tangent-
   generation and handedness-conflict-rejection algorithm, and the
   bounded existing-mesh migration (Asset System's own module
   boundary). Requires Proposed Amendments to both
   [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
   (format-scope sentence) and
   [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
   ("one, single vertex layout" closed attribute-count Decision) —
   both real, disclosed conflicts confirmed by direct inspection, not
   mechanically filed; ADR-0063 needs none.
2. [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
   — the material schema/version bump, the `Renderer` public-API
   extension (`Material`/`createMaterial()`'s second borrowed texture
   pair), the new per-material shader-selection axis, two new shader
   files, and the descriptor-pool capacity widening (Asset System +
   Shader System + Renderer + Runtime's own material realization).

Also touched, as small, additive, already-designated-extensible
surfaces: `VertexAttributeFormat`/`VertexAttributeType` each gain one
`Float4` value (ADR-0073); `Renderer`'s own public `Material`/
`createMaterial()` signatures gain trailing-default parameters
(ADR-0074); the descriptor pool's own capacity constant and one
`ATLANTIS_CHECK` allowed-value set change from 4 to 5 (ADR-0074). No
RenderGraph change; no new module boundary; no new dependency.

## Alternatives Considered

See ADR-0073's and ADR-0074's own "Alternatives Considered" sections
for the complete, disclosed reasoning (hand-authored tangents,
runtime/shader-side generation, silently averaging conflicting
handedness, a determinant-sign mirroring correction, scoping to the
no-environment path only, a shader specialization-constant toggle, a
combined shader with a dummy texture, and a bindless/material-graph
abstraction) — not restated here to avoid duplicating one argument
across three documents.

## Testing & Verification Plan

- **Byte-unchanged, no recapture:** all 9 existing committed goldens
  (`minimal_cube`, `world_scene`, `textured_quad`, `material_demo`,
  `lighting_demo`, `ibl_material_demo`, `pbr_material_demo`,
  `hdr_roll_off_demo`, `integrated_showcase_demo`) must remain byte-
  identical. `world_scene_loaded` (reuses `world_scene`'s own golden)
  and `sky_background` (a live-render discriminative comparison, no
  committed golden) must both continue passing unchanged. This holds
  despite `minimal_cube.mesh.txt` and `pbr_sphere.mesh.txt` gaining
  new UV/topology data — the audit above requires their existing
  position/color/normal-driven rendered output to be unchanged, and
  every one of these goldens uses a non-normal-mapped material, so no
  new shader path is ever selected for them.
- **Existing-mesh migration acceptance gate:** re-run the same
  tangent-generatability audit against the migrated `minimal_cube`/
  `pbr_sphere` sources; zero degenerate triangles, zero handedness
  conflicts, required before Implementation is considered complete.
- **New, independent golden:** exactly one, for this Spec's own new
  combined normal-map+IBL+shadow+sky demo scene, captured and human-
  reviewed through ADR-0042's existing two-phase candidate-generate →
  review process — never auto-accepted.
- **GPU-independent unit tests** (new): the tangent-generation
  algorithm against hand-computable inputs (a single flat-UV triangle
  with a known expected tangent/handedness); a constructed shared-
  vertex case with deliberately opposite-sign handedness proving
  `TangentHandednessConflict` fires and no partial artifact is
  written; each new `DegenerateTangentBasis`/`NonUnitTangent`/
  `NonOrthogonalTangent`/`InvalidTangentHandedness` failure path; and
  `sampledTextureBindingCountFor()`'s new `3`/`5`-result cases
  (mirroring the existing 4-case lock-down test's own style).
- **GPU-required tests** (new): the new fixture's own non-degenerate-
  frame proof (mirrors `pbr_material_demo_gpu_tests.cpp`'s own first
  `TEST_CASE`); a discriminative pixel check confirming the normal map
  visibly changes shading versus the same material rendered without
  one (two renders, one fixture, mirroring Spec 0028's own R1/R2
  differential methodology rather than an exact predicted RGB value);
  and a **new, dedicated 5-sampler Pipeline/descriptor-capacity test**
  — a real Vulkan Device creating a Pipeline with
  `sampledTextureBindingCount == 5`, a real descriptor set with all 5
  sampler bindings bound, and a real draw submitted under Vulkan
  Validation Layers. This is a distinct requirement from, and is not
  satisfied by, the existing `N+4`/`N+5` descriptor-**set**-count test
  (ADR-0074 item 3).
- Full `ctest -LE gpu`/`ctest -L gpu`, Debug and Release; Vulkan
  Validation Layers clean; `git diff --check`; module/link/`Vk*`
  isolation; fresh `ATLANTIS_BUILD_TESTS=OFF` build.

## Risks & Open Questions

- **Q1 (mirrored/negative-determinant transform limitation).**
  `checkConformalTransform()` (confirmed by direct code reading) never
  checks the transform's determinant sign, so a negative-determinant
  conformal transform passes today and would flip true tangent-frame
  chirality, producing a visually inverted normal-map result for that
  one entity. **Recommendation:** accept this as a disclosed
  limitation for this Spec (no current scene authors a negative-
  determinant transform); revisit only if a future scene genuinely
  needs one, as its own separate, disclosed decision — not solved
  speculatively here.
- **Q2 (`RealizedMaterialCandidate`/staging ownership for a second
  texture under load/error paths).** The dedup and first-realization
  contract mirrors `newSampledTexture`'s own exactly (ADR-0074), but
  the exact staging-buffer lifetime interaction when a normal-map
  upload fails mid-frame is not exhaustively enumerated in this Spec —
  **Recommendation:** confirmed at Plan time against
  `material_realization.cpp`'s own existing error-path handling for
  the base-color texture, mirrored verbatim rather than designed anew.

## Out of Scope / Future Work

- A full MikkTSpace-equivalent tangent-generation algorithm.
- A determinant-sign correction for mirrored transforms (Q1) — its
  own future, disclosed decision if a real scene ever needs it.
- glTF import, a second UV set, parallax/relief mapping, anisotropy,
  clear coat, skinning/animation interaction, post-processing, and a
  second graphics backend all remain explicitly out of scope and are
  not unblocked by this Spec.
