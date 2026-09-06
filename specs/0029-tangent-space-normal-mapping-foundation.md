# Spec: Tangent-Space Normal Mapping Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-09-06
- **Related Plan(s):** None yet — Plan 0029 drafting starts only once
  [PR #129](https://github.com/slmao/Atlantis/pull/129) merges to
  `main`.
- **Human Review Approval (2026-09-06):** Approved by the repository
  maintainer against [PR #129](https://github.com/slmao/Atlantis/pull/129).
  Accepts this Spec's design as written, [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
  and [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
  (both `Accepted`), and the Amendments filed against
  [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md#accepted-amendment--2026-09-06),
  [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md#accepted-amendment--2026-09-06),
  and [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md#accepted-amendment--2026-09-06)
  (all three `Accepted`), in the same pass. Explicitly accepts the
  mirrored/negative-determinant tangent-space handedness limitation
  (Q1, Risks & Open Questions) as a disclosed, deferred limitation —
  not solved by this Spec. **This approval authorizes drafting Plan
  0029 only, once PR #129 merges to `main` — not any Implementation,
  asset migration, or golden capture.**
- **Related ADR(s):** [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (`Accepted` — cooked mesh tangent representation, cook-time fallback,
  and existing-mesh migration), [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
  (`Accepted` — material/Renderer-API/descriptor/shader contract, both
  direct-lit and IBL paths), [ADR-0045 Accepted Amendment — 2026-09-06](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md#accepted-amendment--2026-09-06)
  (mesh format-scope sentence gains tangent), [ADR-0058 Accepted Amendment — 2026-09-06](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md#accepted-amendment--2026-09-06)
  (the "one, single vertex layout" closed attribute-count/byte-size
  Decision gains tangent as a fifth attribute — a genuine conflict
  found by direct inspection, mirroring ADR-0063's own identical
  precedent for the normal attribute; ADR-0063 itself needs no
  amendment), [ADR-0072 Accepted Amendment — 2026-09-06](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md#accepted-amendment--2026-09-06)
  (D-7's own 4-sampler ceiling, `4 * maxSets` pool sizing, and 5-slot
  `textureDescriptorMemos_` array each widen by one — the real
  authoritative source ADR-0074 builds on, not ADR-0064)

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
size, new fields) — every mesh gets re-cooked, and exactly one existing
mesh (`pbr_sphere.mesh.txt`) needs a real source edit (below); a second
mesh (`minimal_cube.mesh.txt`) needed a real fix but not a source
edit — a cook-time fallback resolves it with zero authored bytes
changed. What stays byte-identical is **runtime rendering behavior,
shader selection, and descriptor-binding results for every material
that has no normal map**, and all 9 existing committed goldens.

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
- A real, bounded migration of the one existing mesh source
  (`pbr_sphere.mesh.txt`) that cannot currently produce an unambiguous
  tangent (below), preserving its own existing rendered output. A
  second real problem (`minimal_cube.mesh.txt`, zero non-degenerate UV
  contribution at every vertex) is resolved entirely by a cook-time
  fallback with zero source change (below) — it required a fix, but
  not a migration.
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
- **FR1a (Deterministic fallback for zero UV contribution).** A vertex
  with zero non-degenerate contributing triangles (every adjacent
  triangle UV-degenerate, or zero triangles reference it) gets a
  deterministic tangent derived solely from its own already-validated
  unit normal — the fixed Cartesian axis least parallel to that normal,
  Gram-Schmidt-orthogonalized and normalized, with handedness fixed to
  `+1.0` (ADR-0073 item 4a). This is not an error path: it produces a
  valid, decode-time-conformant tangent with zero source change, and is
  `minimal_cube.mesh.txt`'s own resolution (Existing-mesh audit below)
  — explicitly disclosed as carrying no UV-derived directional meaning
  for that vertex, never claimed as standard tangent quality.
- **FR2 (Mesh artifact schema).** `kMeshArtifactSchemaVersion` becomes
  `4`; per-vertex stride becomes 60 bytes (tangent xyzw appended at
  offset 44); a new `kMeshArtifactTangentOffsetBytes = 44` constant is
  added. Schema versions 1–3 are rejected outright — no migration
  reader (ADR-0073).
- **FR3 (Handedness-conflict rejection and the one remaining
  degenerate-basis failure).** A mesh source whose own UV/index/normal
  data cannot yield a well-defined, unambiguous tangent for at least
  one vertex fails cooking outright with a distinct error, no partial
  artifact ever written — after FR1a's fallback already resolves the
  "zero contribution" case with no error at all:
  - `CookError::DegenerateTangentBasis` — a vertex *has* at least one
    non-degenerate contributing triangle (so FR1a's fallback does not
    apply), yet its accumulated tangent is still orthogonalization-
    degenerate (`< 1e-6`, reusing `kDegenerateLengthEpsilon`) — a real
    safety net for a pathological geometric case, not observed in any
    of the 5 currently-committed meshes.
  - `CookError::TangentHandednessConflict` — two or more of a shared
    vertex's own contributing triangles compute opposite-sign
    handedness (a genuine tangent discontinuity that would require a
    vertex split to resolve). **Never silently averaged** — this is a
    real, audit-proven failure mode (`pbr_sphere`, below), not a
    hypothetical one.
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
- **FR5 (Existing-mesh migration, audit-driven, bounded to one mesh).**
  A real probe of all 5 committed mesh sources (below) proves 3 need no
  change, 1 (`minimal_cube.mesh.txt`) is resolved entirely by FR1a's
  cook-time fallback with no source change, and exactly 1
  (`pbr_sphere.mesh.txt`) needs a real source edit before this
  feature's own Implementation. The migrated mesh keeps its existing
  position/color/normal data and existing rendered output unchanged;
  only vertex-duplication topology changes at its own pole regions to
  make every vertex's tangent unambiguous (ADR-0073 Decision item 9).
- **FR6 (Material schema, three legal grammar shapes).**
  `MaterialAssetData`/`DecodedMaterialArtifact`/`MaterialMetadata` each
  gain one optional `AssetId normalMapTexture` field (`0` = none);
  `ParsedMaterialSource` gains `std::string normalMapLogicalPath`
  (empty = none), mirroring `textureLogicalPath`'s own identical
  pre-cook/post-cook type split. `atlantis_material_source_version`
  becomes `3`, with exactly three legal, line-count-exact shapes: 5
  lines (any `kind`, no normal map, unchanged from today); 8 lines
  (adds `base_color_factor`/`metallic_factor`/`roughness_factor`, any
  `kind`, no normal map, unchanged from today); 9 lines (the 8-line
  form plus a trailing `normal_map: <path>` line, **`kind` must be
  `pbr_direct_lit`** — a 9-line file naming `unlit_textured`/
  `lit_textured` is rejected with a new, distinct
  `MaterialSourceParseError::NormalMapNotSupportedForKind`; an empty
  `normal_map:` value is rejected with the existing `MissingField`).
  Every existing `.material.txt` file needs exactly its own
  version-marker line changed, nothing else.
  `kMaterialArtifactSchemaVersion` becomes `3` (56 → 64 bytes).
  Versions 1–2 rejected outright (ADR-0074 item 1).
- **FR7 (Normal-map color space, cross-validated, and shared sampler,
  closed).** The normal-map texture asset is cooked with the existing
  `TextureColorSpace::Unorm` enumerator (directional data, never
  sRGB-decoded) — no new enumerator; this is a fixed decision, not a
  Plan-stage question. Mirroring ADR-0066 item 6's own base-color
  `Rgba8Srgb` requirement exactly, a normal map resolved to `Srgb`
  fails scene load with a new `RuntimeInitError::PbrNormalMapTextureNotUnorm`
  sub-code, checked at Runtime's existing Phase 1
  scene-dependency-resolution point (ADR-0060 Decision 6) — not a
  cook-time check (`cookMaterial()` never resolves its own texture
  reference, ADR-0059 Decision 7). The normal map is sampled through
  the material's own existing sampler (same `filter`/`addressMode` as
  base color) — no second sampler is introduced (ADR-0074 item 1).
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
- **FR10 (Descriptor contract and capacity — three real limits,
  disclosed and correctly attributed).** `sampledTextureBindingCountFor()`
  returns `3` for `PbrDirectLit` + normal map with no environment
  (base-color@1, shadow-map@2, normal-map@3), and `5` with environment
  enabled (base-color@1, environment@2, DFG LUT@3, shadow-map@4,
  normal-map@5). Three separate limits, all fixed by ADR-0072 D-7 and
  each widened by one via that ADR's own Accepted Amendment: the
  per-Pipeline `sampledTextureBindingCount` ceiling (`ATLANTIS_CHECK`
  allowed-set `{0..4}` → `{0..5}`), the pool's own sampler-type sizing
  (`4 * maxSets` → `5 * maxSets`), and
  `VulkanCommandList::textureDescriptorMemos_` (`std::array<..., 5>` →
  `std::array<..., 6>` — without this, `bindTexture(5, ...)` fails its
  own `ATLANTIS_CHECK(binding < textureDescriptorMemos_.size())`
  regardless of the first two being fixed). New descriptor contracts:
  `pbrDirectLitNormalMapExpectedDescriptorContract()` (5 entries) and
  `pbrIblNormalMapExpectedDescriptorContract()` (7 entries) —
  confirmed by direct reading of `descriptor_contract.cpp`'s own
  existing 4-entry/6-entry baselines, not estimated. The existing
  descriptor-**set**-count formula (`N+4`/`N+5`) is unaffected — this
  is a per-pool sampler-type **capacity** change plus one
  `CommandList`-internal array bump, a different axis (ADR-0074 item
  3/4).
- **FR11 (Renderer public-API extension, one pointer, no second
  sampler, contract fully closed).** `Material` gains exactly one new,
  optional, borrowed, non-owning `normalMapTexture_` pointer — never a
  second sampler, since FR7 already fixes the normal map to reuse the
  material's existing `sampler_`. Two mechanically-checked
  preconditions in the constructor:
  `ATLANTIS_CHECK(normalMapTexture_ == nullptr || sampledTexture_ != nullptr)`
  (the base-color pair must already be present) and
  `ATLANTIS_CHECK(normalMapTexture_ == nullptr || pushConstantLayout_ == MaterialPushConstantLayout::PbrDirectLit)`
  (a normal map may only be set on a `PbrDirectLit`-layout Material —
  the only layout the two normal-map shaders use; without this, a
  non-PBR Material could carry a normal map that binds against a
  Pipeline whose real shader never declared that binding, a real
  Vulkan mismatch). A third precondition — the caller's own Pipeline
  must actually be built from the matching normal-map shader variant
  for that Material's own `environmentBinding()` — is documented, not
  mechanically checked (`Pipeline` has no descriptor-layout
  introspection API; adding one is out of scope). **Runtime's own real
  path satisfies all three by construction:** FR6's own grammar
  restriction means `normalMapTexture != 0` implies `kind ==
  PbrDirectLit`, which `pushConstantLayoutFor()` always maps to
  `PbrDirectLit`, and `selectShaderPair()`'s own extension (FR8) always
  selects the matching normal-map shader variant for that same
  material — no call site on Runtime's own real path can violate any
  of the three. `createMaterial()` gains the matching trailing
  parameter, defaulting to `nullptr` — every existing call site is
  unaffected. `Renderer::drawFrame()` binds the normal map at
  `environmentBinding() == Ibl ? 5U : 3U`, reusing `material.sampler()`
  — the same `VkSampler` already bound for base color — mirroring the
  shadow-map binding's own existing `? 4U : 2U` conditional-index
  pattern verbatim. `RealizedMaterialCandidate` gains three new,
  explicitly-named fields (`normalMapTextureAssetId`,
  `newNormalMapTexture`, `normalMapStagingBuffer`), mirroring the
  existing base-color fields exactly in kind, realized into the same,
  existing `sampledTextureResourceMap_` (no new resource-map type). A
  failure at any point during realization is rolled back by the
  existing, unmodified mechanism — the whole candidate is one local,
  RAII-owned variable, destroyed on any early `Err` return before
  anything is recorded into a RenderGraph or submitted (ADR-0074
  Section 2/2a).
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

A real, executed probe (per-triangle UV-Jacobian determinant and
per-vertex handedness-conflict computation, run against all 5
committed `.mesh.txt` sources) found:

| Mesh | Triangles | UV-degenerate triangles | Vertices with handedness conflict | Verdict |
|---|---|---|---|---|
| `ground_plane.mesh.txt` | 2 | 0/2 | 0/4 | Clean — no change needed |
| `textured_quad_left.mesh.txt` | 2 | 0/2 | 0/4 | Clean — no change needed |
| `textured_quad_right.mesh.txt` | 2 | 0/2 | 0/4 | Clean — no change needed |
| `minimal_cube.mesh.txt` | 12 | **12/12 (100%)** | 0/8 | **Zero valid UV contribution at every vertex — resolved entirely by FR1a's cook-time fallback; no source change** |
| `pbr_sphere.mesh.txt` | 768 | 0/768 | **96/425 (22.6%)**, pole rings | **Fails — must migrate (this is the only mesh requiring a source edit)** |

**Correction to this Spec's own earlier drafting:** `minimal_cube`'s UV
is genuinely sampled today — `lighting_demo.scene.txt` places it under
`lit_textured_quad.material.txt` (`kind: lit_textured`), and
`lit_textured.slang` samples `input.uv` for real, confirmed by direct
read. A UV re-unwrap would therefore have changed `lighting_demo`'s own
golden. FR1a's fallback (derived solely from the vertex's own normal)
avoids this entirely by leaving every byte of `minimal_cube`'s
authored content untouched.

Only `pbr_sphere`'s handedness conflict is a real, audit-confirmed
problem this Spec's own Goals require resolved before Implementation
(ADR-0073 Decision item 9):

- **`pbr_sphere.mesh.txt`:** needs further pole-vertex duplication
  (beyond whatever duplication already exists at the poles) so that no
  single vertex index is shared by triangles whose own UV winding
  produces opposite-sign handedness. Position, color, normal, and UV
  values for every already-distinct vertex are unchanged; only the
  pole regions gain additional duplicate vertices, exactly as the
  existing per-longitude pole duplication already establishes the
  precedent for. FR1a's fallback cannot resolve this case — it applies
  only to a vertex with *zero* contributing triangles, and every
  `pbr_sphere` pole vertex has multiple, genuinely disagreeing ones.
- **Acceptance gate:** re-running the same audit against the migrated
  `pbr_sphere` source must report zero handedness conflicts before
  ADR-0073's own Implementation is considered complete.
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

**Yes — two ADRs, both `Accepted`, one per real architectural surface
this Spec touches, confirmed by direct code inspection (mirroring how
Spec 0023's own PBR addition split into ADR-0066 asset-side / ADR-0067
shader-side):**

1. [ADR-0073](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md)
   — the mesh artifact schema/version bump, the cook-time tangent-
   generation and handedness-conflict-rejection algorithm, and the
   bounded existing-mesh migration (Asset System's own module
   boundary). Required Accepted Amendments to both
   [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md#accepted-amendment--2026-09-06)
   (format-scope sentence) and
   [ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md#accepted-amendment--2026-09-06)
   ("one, single vertex layout" closed attribute-count Decision) —
   both real, disclosed conflicts confirmed by direct inspection, not
   mechanically filed; ADR-0063 needs none.
2. [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
   — the material schema/version bump, the `Renderer` public-API
   extension (`Material`/`createMaterial()`'s one new borrowed
   normal-map texture pointer, reusing the existing sampler), the new
   per-material shader-selection axis, two new shader files, and the
   descriptor-pool capacity widening. Required an Accepted Amendment to
   [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md#accepted-amendment--2026-09-06)
   D-7 (the real, authoritative source of the sampler ceiling, pool
   sizing, and `textureDescriptorMemos_` array this ADR widens by one)
   — not ADR-0064, whose own descriptor-set-count/growth model this
   ADR does not touch (Asset System + Shader System + Renderer +
   Runtime's own material realization).

Also touched, as small, additive, already-designated-extensible
surfaces: `VertexAttributeFormat`/`VertexAttributeType` each gain one
`Float4` value (ADR-0073); `Renderer`'s own public `Material`/
`createMaterial()` signatures gain one trailing-default parameter each
(ADR-0074); three `VulkanCommandList`/`VulkanDevice`-internal capacity
limits each widen by one (ADR-0072's own Amendment). No RenderGraph
change; no new module boundary; no new dependency.

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
  committed golden) must both continue passing unchanged. `minimal_cube.mesh.txt`
  gains zero source bytes of any kind (FR1a's fallback reads only its
  already-existing normal); `pbr_sphere.mesh.txt` gains new pole-region
  vertex/index data but every already-distinct vertex's own position/
  color/normal/UV values are unchanged — every one of these goldens
  uses a non-normal-mapped material, so no new shader path is ever
  selected for them.
- **Existing-mesh migration acceptance gate:** re-run the same
  tangent-generatability audit against the migrated `pbr_sphere`
  source; zero handedness conflicts required before Implementation is
  considered complete.
- **New, independent golden:** exactly one, for this Spec's own new
  combined normal-map+IBL+shadow+sky demo scene, captured and human-
  reviewed through ADR-0042's existing two-phase candidate-generate →
  review process — never auto-accepted.
- **GPU-independent unit tests** (new): the tangent-generation
  algorithm against hand-computable inputs (a single flat-UV triangle
  with a known expected tangent/handedness); FR1a's fallback against a
  hand-computable zero-contribution vertex (known normal → known,
  hand-verified fallback tangent, no error); a constructed shared-
  vertex case with deliberately opposite-sign handedness proving
  `TangentHandednessConflict` fires and no partial artifact is
  written; the narrowed `DegenerateTangentBasis` path (a contrived
  vertex with a valid contribution whose orthogonalization still
  degenerates) plus `NonUnitTangent`/`NonOrthogonalTangent`/
  `InvalidTangentHandedness`; `sampledTextureBindingCountFor()`'s new
  `3`/`5`-result cases (mirroring the existing 4-case lock-down test's
  own style); the material grammar's new 9-line-form cases —
  `NormalMapNotSupportedForKind` (9 lines, `kind: lit_textured`) and
  `MissingField` (9 lines, empty `normal_map:` value); and `Material`'s
  own two new constructor preconditions (FR11), using this codebase's
  existing `ScopedFailureHandler` pattern
  (`tests/renderer/renderer_ownership_tests.cpp`) — never a process-
  death test, since `ATLANTIS_CHECK` reports through a replaceable
  handler rather than aborting (`tests/core/assert_tests.cpp`):
  constructing `Material` with a non-null `normalMapTexture` and no
  base-color pair must capture exactly one failure; constructing it
  with a valid base-color pair but `pushConstantLayout =
  ObjectToWorldOnly` must capture exactly one failure; constructing it
  with a valid base-color pair and `pushConstantLayout = PbrDirectLit`,
  for both `MaterialEnvironmentBinding::None` and `Ibl`, must capture
  zero failures.
- **GPU-required tests** (new): the new fixture's own non-degenerate-
  frame proof (mirrors `pbr_material_demo_gpu_tests.cpp`'s own first
  `TEST_CASE`); a discriminative pixel check confirming the normal map
  visibly changes shading versus the same material rendered without
  one (two renders, one fixture, mirroring Spec 0028's own R1/R2
  differential methodology rather than an exact predicted RGB value);
  and a **new, dedicated 5-sampler Pipeline/descriptor-capacity test
  that specifically exercises `bindTexture(5, ...)`** — a real Vulkan
  Device creating a Pipeline with `sampledTextureBindingCount == 5`, a
  real descriptor set with all 5 sampler bindings bound, and a real
  `cmd.bindTexture(5, ...)` call (the normal-map binding under
  `pbr_ibl`) before a real draw is submitted, confirmed under Vulkan
  Validation Layers. Actually calling `bindTexture()` at binding 5 is
  required, not optional — it is the one call that exercises
  `textureDescriptorMemos_`'s own widened size (ADR-0074 Section 3).
  This is a distinct requirement from, and is not satisfied by, the
  existing `N+4`/`N+5` descriptor-**set**-count test.
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

**Closed, not deferred:** the second-texture staging/rollback contract
(`RealizedMaterialCandidate`'s own `normalMapStagingBuffer` lifetime
under a mid-frame failure) is resolved by direct inspection of the
existing `realizeOneMaterialCandidate()`/`realizePendingMaterials()`
mechanism (ADR-0074 Section 2a) — the same local-variable-RAII rollback
already governing the base-color texture, extended with zero new
machinery. This was an earlier draft's own Open Question; it is not
one any longer.

## Proposed Correction — 2026-09-06 (`pbr_sphere` handedness-conflict count)

**Status:** Proposed. Pending Human Review. Does not rewrite this
Spec's own Goals, Requirements, or Decisions above; supersedes only
the `96/425 (22.6%)`, "pole rings" (plural) figures in the
Existing-mesh tangent-generatability audit table and its own
surrounding prose.

A temporary, uncommitted re-audit, run strictly to ADR-0073's own
already-Accepted handedness formula, found the real figure is **48 of
425 vertices (11.3%)**, located precisely at the north-pole ring
(24/25 vertices) and the one ring immediately adjacent to it (24/25
vertices) — **not** "pole rings" generally, and **not** the south
pole, which has zero conflicts under this mesh's own real
triangulation. See
[ADR-0073's own Proposed Correction — 2026-09-06](../adr/0073-static-mesh-tangent-attribute-generation-and-schema.md#proposed-correction--2026-09-06-pbr_sphere-handedness-conflict-count-and-location)
for the full derivation and the simulated, re-verified migration
result (425→473 vertices, 768 triangles/2304 indices unchanged, 0
remaining conflicts) — restated here only to the depth this Spec's own
role (what/scope) requires, not duplicated in full.

This changes no Goal, Requirement, or Decision — `pbr_sphere` remains
the one mesh needing real migration, `minimal_cube` remains fully
resolved by the cook-time fallback with zero source change, and every
existing golden's own byte-identity requirement is unaffected.
[Plan 0029](../plans/0029-tangent-space-normal-mapping-foundation.md)
depends on this correction and uses its corrected figures throughout.

## Out of Scope / Future Work

- A full MikkTSpace-equivalent tangent-generation algorithm.
- A determinant-sign correction for mirrored transforms (Q1) — its
  own future, disclosed decision if a real scene ever needs it.
- glTF import, a second UV set, parallax/relief mapping, anisotropy,
  clear coat, skinning/animation interaction, post-processing, and a
  second graphics backend all remain explicitly out of scope and are
  not unblocked by this Spec.
