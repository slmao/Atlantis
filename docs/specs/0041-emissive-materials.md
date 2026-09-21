# Spec: Emissive Materials

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-22
- **Related Plan(s):** none yet — Plan 0041 is drafted only after this Spec
  is Approved.
- **Approval:** pending
- **Related ADR(s):** [ADR-0089](../adr/0089-emissive-material-parameter-range-composition-and-push-constant-placement.md)
  (Proposed, on this branch)

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Add a self-lit colour term to Atlantis's PBR materials: an `emissiveFactor`
(linear RGB) on `MaterialAssetData`, carried through the material source,
artifact and metadata formats (each bumped one version), pushed to the GPU in
each of the four PBR push-constant structs, and added to the final colour of
all ten PBR fragment shaders as `accumulated + emissiveFactor` — after every
lighting term, independent of the BRDF, the lights, the shadow and the
environment, and before the output transform. This is
[Spec 0036](0036-bistro-parity-roadmap.md) workflow ③. It is factor-only: an
emissive *texture* is deferred with its measured cost and a named reopening
condition. The one real surprise the investigation found is that Bistro's
emissive factors are **not** glTF-core `[0, 1]` values — they reach 100 — so
emissive gets a range contract unlike every other material factor, and that
departure is what makes ADR-0089 necessary.

## Motivation / Problem Statement

[Spec 0035](0035-clearcoat-sheen-anisotropy-materials.md)'s Non-Goals
disclosed "no emissive term added to the material schema"; Spec 0036 ③
(`0036:406-429`) records the gap against Bistro's neon signage, lit windows and
the string-light bulbs *as surfaces that appear self-lit* — distinct from
workflow ②'s job of making lights illuminate other surfaces, which
[Spec 0040](0040-multi-light-architecture.md) has now delivered.

Today the engine has no way to express a self-lit surface. The glTF importer
(Spec 0037) finds Bistro's emissive data and throws it away, reporting it as
"no v6 destination, dropped (Ruling 3)"
(`src/tools/gltf_importer/material_import.cpp:394-398`). Every Bistro bulb,
lantern and sign therefore renders as an unlit, dark object.

Spec 0036 left one coordination question for whichever of ② or ③ drafted
first: whether they share one schema bump (`0036:878-880`). That question has
been answered by events: Spec 0040 changed `FrameLightingData` and the shader
uniform block but touched no material format, so there is nothing to share —
③ takes its own bump, consistent with Spec 0035's "one bump per slice" policy.

## Goals

- `emissiveFactor` exists on every material, defaults to `(0, 0, 0)`, and is
  honoured by all four PBR kinds (`PbrDirectLit`, `PbrClearcoat`, `PbrSheen`,
  `PbrAnisotropic`) across all ten of their shader variants.
- The emissive term is **additive and light-independent**: it is not
  multiplied by base colour, N·L, attenuation, shadow, or environment, and it
  is present in a scene with no lights at all.
- Emissive values can carry real HDR energy — Bistro's measured range up to
  100 cooks and renders, not clamped to 1.
- Every existing golden is byte-identical afterwards: a material without
  emissive renders exactly as it does today.
- The push-constant layout of every PBR kind stays within Vulkan's guaranteed
  128-byte minimum, confirmed by real Slang reflection and an MSVC probe (the
  ADR-0067 D-3 method), with the numbers recorded here rather than assumed.

## Non-Goals

- **An emissive texture.** Deferred; see Investigation 4 for the measured
  cost and the reopening condition, and Out of Scope for the condition itself.
- **Bloom, or any interaction with it.** This Spec guarantees only that the
  emissive term reaches the scene-referred linear HDR target. Deciding what is
  "bright" and making it glow is workflow ⑥'s bright-pass.
- **Emissive surfaces as light sources.** An emissive bulb does not illuminate
  its neighbours; that is what the point lights of workflow ② are for, and
  ⑦'s authoring pairs the two by hand.
- **Emissive on `LitTextured` and `UnlitTextured`.** Decided in Investigation
  3: not added, and both kinds reject the field at parse time.
- **`KHR_materials_emissive_strength`.** Bistro does not use it (its
  `extensionsUsed` lists three other extensions only). The unbounded range
  below already expresses what that extension exists to express.
- **Photometric meaning.** What an emissive value of 40 means in physical
  units belongs to the future photometric Spec to which Spec 0040's ruling O5
  reassigned light intensity. This Spec fixes the arithmetic, not the units.

## Requirements

### Functional

1. **Data model.** `MaterialAssetData` gains `float emissiveFactor[3]`,
   default `{0, 0, 0}`, linear-space RGB.
2. **Range.** Each component must be finite and within `[0, 65504]`,
   rejected at cook time with a new, named error. The upper bound is the
   largest finite value of the `Rgba16Float` HDR target
   (`src/rhi/include/atlantis/rhi/types.h:45`); anything above it would write
   `inf` into the target that workflow ⑥'s bright-pass will read. This is
   deliberately **not** `isValidFactor()`'s `[0, 1]`
   (`src/asset_system/src/cook_material.cpp:62`) — see Investigation 4.
3. **Legal kinds.** The field is legal on the four PBR kinds only. A material
   source that sets it on `unlit_textured` or `lit_textured` is rejected with a
   new, named parse error, mirroring `NormalMapNotSupportedForKind`.
4. **Formats.** The three independently versioned material formats bump
   together, with no dual-version reader (the ADR-0066 precedent every prior
   bump followed):
   - source grammar `atlantis_material_source_version` 6 → 7, with an optional
     `emissive_factor: r g b` line (grammar shape: open question O1);
   - artifact schema 6 → 7, header 96 → 108 bytes, `emissive_factor` appended
     at offset 96 (12 bytes, little-endian, byte-by-byte as today);
   - metadata `atlantis_material_metadata_version` 5 → 6, with an
     `emissive_factor:` line, and `loadMaterialAsset()`'s metadata-versus-
     artifact agreement check extended to cover it.
5. **Push constants.** Each of the four PBR push-constant structs gains a
   trailing `float emissiveFactor[3]`, with the offsets and sizes measured in
   Investigation 2 (96/112 for three kinds, 112/128 for `PbrSheen`), each
   confirmed by real Slang reflection **and** an MSVC `static_assert`, and each
   shader's declared push-constant range equal to the C++ `sizeof`.
6. **Shading.** All ten PBR fragment shaders return
   `float4(accumulated + pushConstants.emissiveFactor, alphaOut)` in place of
   `float4(accumulated, alphaOut)` — the same single line in each
   (Investigation 3). Nothing else in their lighting changes. Alpha is
   unaffected.
7. **Plumbing.** `renderer::Material` gains the value as a defaulted trailing
   constructor parameter and an accessor, as every ADR-0081 factor did
   (`src/renderer/include/atlantis/renderer/material.h:88-90`); material
   realization passes it; the draw path pushes it per kind.

### Non-functional

- **Performance:** one vector add per fragment and 12–16 more push-constant
  bytes per draw. Not measurable at this engine's scale.
- **Memory:** 12 bytes more per material artifact (96 → 108).
- **Portability (within the Vulkan-only Phase 1 constraint):** every layout
  stays within the guaranteed `maxPushConstantsSize` of 128. `PbrSheen`
  reaches it exactly — see Risks.
- **Golden neutrality:** with `emissiveFactor = (0, 0, 0)`, `x + 0.0` is exact
  in IEEE-754, so every existing golden must stay byte-identical. Any golden
  that moves means something other than the add changed; stop and report.

## Pre-drafting Investigation (required conclusions, cited against real files)

### Investigation 1 — the material schema as it stands

- `MaterialKind` has six enumerators
  (`src/asset_system/include/atlantis/asset_system/material_types.h:44-51`);
  `MaterialAssetData` (`:65-106`) carries the ADR-0066 base set plus
  `normalMapTexture` and ADR-0081's six per-kind fields. Every field is
  present on every material and inert where unread.
- **The artifact header is 96 bytes at schema 6, not 64.** 64 was schema 3;
  ADR-0074 and ADR-0081's three bumps took it to 96
  (`src/asset_system/include/atlantis/asset_system/material_artifact.h:28-35`,
  `:60-61`). Every older size is rejected outright (`:44-51`).
- **The source parser is a line-count shape machine, not a key-value
  reader.** It accepts exactly 5, 8, 9, 10 or 11 lines
  (`src/asset_system/src/material_source.cpp:141-151`) and decides what each
  line *is* from the count and the kind (`:186-213`, `:325-335`). A new
  optional line collides with existing shapes — a 9-line `pbr_direct_lit`
  could be `normal_map` or emissive — so the grammar shape is a real design
  question (O1), not a transcription.
- **Three version numbers, not one.** Source 6 (`material_source.cpp:11`),
  artifact 6 (`material_artifact.h:60`), metadata 5
  (`src/asset_system/src/material_metadata.cpp:12`). Plan 0035 bumped them
  together at each slice (commit `49b270e` for the last).
- **Blast radius of the bump:** all 51 committed `.material.txt` files change
  their version line, as do 39 inline source strings in 7 test/source files.
  Material artifacts are cooked at build time; none is committed.

### Investigation 2 — the push-constant budget, measured

Four structs, not three: `PbrClearcoatPushConstants` exists alongside the
other three (`src/renderer/src/pbr_clearcoat_push_constants.h`).

| Struct | Today | Internal padding | Headroom to 128 |
|---|---|---|---|
| `PbrPushConstants` | 96 (`pbr_push_constants.h:44`) | 8 B `_pad` at 88 | 32 |
| `PbrClearcoatPushConstants` | 96 (`pbr_clearcoat_push_constants.h:50`) | **none** | 32 |
| `PbrAnisotropicPushConstants` | 96 (`pbr_anisotropic_push_constants.h:31`) | **none** | 32 |
| `PbrSheenPushConstants` | 112 (`pbr_sheen_push_constants.h:34`) | 8 B `_padding0` at 88 | 16 |

A `float3` must start on a 16-byte boundary in both layouts, so neither of the
two 8-byte pads at offset 88 can hold it. Appending `float3 emissiveFactor` was
**measured, not computed**: scratch copies of the five distinct shaders were
compiled with `slangc -reflection-json` (the shipped shaders untouched), and a
scratch C++ probe of the four structs was compiled with the build's own MSVC
(Visual Studio 18, `/std:c++20`):

| Kind | `emissiveFactor` offset | Last byte used | Block size (Slang `elementVarLayout.size`) | MSVC `sizeof` | Left of 128 |
|---|---|---|---|---|---|
| `PbrDirectLit` (both variants, and `pbr_ibl*`) | 96 | 108 | 112 | 112 | 16 |
| `PbrClearcoat` | 96 | 108 | 112 | 112 | 16 |
| `PbrAnisotropic` | 96 | 108 | 112 | 112 | 16 |
| **`PbrSheen`** | **112** | **124** | **128** | **128** | **0** |

**Sheen fits — exactly.** 112 + 12 = 124 bytes of data, rounded by the
16-byte struct alignment to 128, which is precisely Vulkan's guaranteed
`maxPushConstantsSize`. Slang and MSVC agree on every number, so the two sides
cannot disagree about the range. The declared push-constant range is the
Slang block size (`src/shader_system/src/slang_json_transform.cpp:262-274`
reads `elementVarLayout.binding.size`), so the range becomes 128 and equals
the C++ `sizeof` — no 124-versus-128 mismatch at `vkCmdPushConstants`.

What Sheen has left afterwards: no room for another `float3`, but 12 bytes of
scalar room (the 8-byte pad at 88 and 4 bytes at 124). Any future *vector*
parameter on `PbrSheen` forces ADR-0081's deferred uniform-buffer question.
That consequence is recorded in ADR-0089 rather than left to be rediscovered.

No non-conventional placement is needed. Reordering Sheen so that emissive
sits at 96 in all four kinds was considered (ADR-0089 Alternatives): it gains
no bytes, it moves `sheenColor`, and it rewrites a proven layout for cosmetic
uniformity.

### Investigation 3 — the shader consumption surface

- **All ten PBR fragment shaders end in the identical line**
  `return float4(accumulated, alphaOut);`:
  `pbr_direct_lit.slang:243`, `pbr_direct_lit_normal_map.slang:204`,
  `pbr_ibl.slang:234`, `pbr_ibl_normal_map.slang:234`,
  `pbr_clearcoat_ibl.slang:194`, `pbr_clearcoat_ibl_normal_map.slang:183`,
  `pbr_sheen_ibl.slang:184`, `pbr_sheen_ibl_normal_map.slang:171`,
  `pbr_anisotropic_ibl.slang:236`, `pbr_anisotropic_ibl_normal_map.slang:197`.
  `accumulated` already holds every lighting term — direct, shadowed, IBL — in
  scene-referred linear HDR, and exposure and tonemapping happen later in the
  output-transform pass. Adding emissive there is exactly Requirement 6, and it
  makes the term independent of shadow by construction.
- **`lit_textured`: recommend not adding.** Its push-constant block is only
  `float4x4 objectToWorld` (`shaders/lit_textured/lit_textured.slang:46-48`);
  it has never carried a per-material factor — not even `baseColorFactor`.
  Emissive would be that path's first material parameter, a new push-constant
  layout and a new `MaterialPushConstantLayout` arm
  (`src/renderer/include/atlantis/renderer/material.h:28`) for a legacy kind
  used by 1 of 51 material assets and never emitted by the importer, which
  writes only `PbrDirectLit` (`material_import.cpp:317`). Bistro's emissive
  materials are all spec-gloss, which the importer maps to `PbrDirectLit`.
- **`UnlitTextured`: not added.** An unlit surface already shows its texture
  unmodified; "self-lit" has no additional meaning there.

### Investigation 4 — what Bistro actually needs

Measured directly from `content/bistro/bistro.gltf`:

- 254 materials; **21 with a non-zero `emissiveFactor`**, **13 with an
  `emissiveTexture`**, 11 with both. The 2 with a texture but a zero factor
  are inert under glTF's `factor × texture` rule either way.
- **The factors are not `[0, 1]`.** Max component 100; the string lights sit
  at 8–20 (e.g. `Paris_StringLights_01_Orange_Color` = `(20, 0.8, 0)`), bulbs
  at 30–100, signage at 0.05–1. glTF core clamps `emissiveFactor` to `[0, 1]`;
  this asset ignores that and uses no `KHR_materials_emissive_strength`.
  Reusing the `[0, 1]` validator would reject every Bistro light fixture, and
  clamping would cost them up to two orders of magnitude of energy — hence
  Requirement 2.
- All 21 are opaque or masked, and none uses transmission, so workflow ④'s
  translucency does not interact with them.

**Factor-only versus factor + texture, quantified:**

| | Factor-only (recommended) | Factor + texture |
|---|---|---|
| Bistro materials fully correct | **10** (string lights ×6, bare bulbs ×4) | 21 |
| Bistro materials approximated | 11 would glow uniformly over the whole mesh (signs, lanterns, ceiling lamp) | 0 |
| Descriptor capacity | unchanged | +1 sampled image; widest shader goes 6 → 7 of the guaranteed 16 per stage — **not** the constraint |
| Shader variants | 10, unchanged | either 20 (a `hasEmissiveMap` dimension, as `normal_map` doubled them) or 10 with an always-bound slot + default texture |
| Other surfaces | none | every arm of `sampledTextureBindingCountFor()` (`src/runtime/src/material_realization.cpp:655-697`), descriptor-contract tests, a default texture resource, an sRGB texture-usage path in the importer |
| Size | S (Spec 0036's sizing) | M |

Recommendation: **factor-only.** It delivers the half of Bistro's emissive
content that is pure colour, at a fraction of the surface, and it is exactly
Spec 0036's "S". The textured half is better decided by workflow ⑦, when the
real assembled scene shows whether uniformly glowing signage is acceptable,
or whether it is better left dark, than by guessing now.

Importer consequence: whether the importer should *map* the 10 texture-less
factors now is open question O3.

### Investigation 5 — impact on existing goldens

Every committed PBR material omits emissive and so takes the default
`(0, 0, 0)`; `accumulated + 0.0` is bit-exact. The only other change the
goldens see is a wider push-constant block whose extra bytes no existing
shader instruction reads differently. All 20 golden directories must therefore
compare byte-identical at zero tolerance (ADR-0042). This goes into the
verification plan as a gate, not a hope: a moved golden means a change beyond
the add — for example, an unintended FMA contraction or reordering — and is a
stop-and-report finding.

## Proposed Design

Data flows through the path every ADR-0081 field already travels:
`.material.txt` → `parseMaterialSource()` → `cookMaterial()` (range check) →
108-byte artifact plus metadata → `loadMaterialAsset()` (agreement check) →
`MaterialAssetData` → material realization → `renderer::Material` → the per-kind
push-constant payload in the draw path → the fragment shader's final add.

The shader change is one line per file. The C++ change is additive
everywhere: a field, a parse line, a validator, a serializer line, a decoder
field, a constructor parameter, a payload copy. The only removal is the
importer's "emissive dropped" report line, and only if O3 is ruled in.

## Architectural Impact

**Yes — ADR-0089 (Proposed, on this branch).** The field addition and the
push-constant extension would, on their own, be a straight application of the
ADR-0081 precedent (a new material field, a schema bump, per-kind push-constant
structs grown and double-confirmed). Two things are not precedent, and those
are why an ADR is needed rather than a paragraph here:

1. **A range contract that departs from ADR-0066.** Every material factor to
   date is linear-space `[0, 1]`. Emissive is linear-space `[0, 65504]`, and
   that departure is a data-contract decision future importers and ⑥'s
   bright-pass will build on.
2. **A composition-point contract.** Where emissive enters the frame — after
   all lighting, before the output transform — is exactly what workflow ⑥
   (bloom) needs to be able to
   rely on, so it is recorded once rather than inferred from shader code.
   Emissive is not modulated by shadow or by any lighting term, but it *is*
   subject to exposure and tonemapping, like every other scene-referred
   value.

Also recorded in ADR-0089, as a consequence rather than a decision:
`PbrSheen` reaches the 128-byte guaranteed push-constant ceiling. This was
**not** a non-conventional placement — it fits with the ordinary append — but
it exhausts that kind's budget and makes ADR-0081's uniform-buffer Future Work
the forced next step for any further Sheen vector parameter.

Public-API surface: `renderer::Material` gains one defaulted constructor
parameter and one accessor — additive, source-compatible, the ADR-0081
precedent. No module boundary, dependency, threading or ownership change. The
RHI, RenderGraph, Platform and World are untouched.

## Alternatives Considered

- **Factor + texture now.** Measured in Investigation 4: roughly doubles the
  surface (variants or an always-bound slot, every binding-count arm, a
  default texture) to fix 11 materials whose correct appearance ⑦ can judge
  better. Deferred, not rejected.
- **Reuse the `[0, 1]` factor validator.** Rejects every Bistro bulb and
  string light (factors 8–100). Rejected.
- **No upper bound (finite only).** Allows values that overflow the
  `Rgba16Float` target to `inf`. Rejected in favour of 65,504 (O2 asks for the
  ruling).
- **An emissive *strength* scalar alongside a `[0, 1]` colour** (the
  `KHR_materials_emissive_strength` shape). Keeps the ADR-0066 convention for
  the colour and would even fit (4 bytes; Sheen's spare 4 bytes at 124 would
  hold it). But it gives every emissive value two sources of truth, costs a
  further schema field and a further multiply per fragment, and expresses
  nothing one unbounded linear vector does not already express. Rejected.
- **Emissive on `lit_textured` too.** Investigation 3. Rejected.
- **A per-material uniform buffer now** (ADR-0081 Future Work), to avoid
  Sheen's exhaustion. Not needed yet: everything fits. Recorded as the forced
  next step instead.

## Testing & Verification Plan

Mapped to the Requirements:

- **R1–R4, GPU-independent:**
  - source parse — the field present, absent (default zero) and malformed;
    rejected on `lit_textured`/`unlit_textured`; every existing shape still
    accepted;
  - cook — `NaN`, `inf`, a negative component and 65,504.5 rejected with the
    named error; 0, 1, 100 and 65,504 accepted;
  - artifact — v7 round-trip at 108 bytes; a 96-byte v6 artifact rejected;
  - metadata — round-trip, plus the agreement check catching a mismatch;
  - the source serializer round-trips.
- **R5, GPU-independent:** for each of the ten PBR shaders, real Slang
  reflection reports the push-constant block size equal to the C++ `sizeof`
  (112/112/112/128), with `emissiveFactor` at the offset the `static_assert`s
  pin — extending the existing reflection cross-check precedent
  (`tests/runtime/pbr_reflection_cross_check_tests.cpp`) from one shader to
  ten.
- **R6, GPU — the Spec 0036 ③ contract row** (`0036:817`):
  - **A dark-scene golden.** `PbrDirectLit` spheres with distinct emissive
    colours, zero lights, environment disabled, so the frame is pure emissive
    on black. Captured with ADR-0042's Initial-baseline two-commit procedure
    and its four evidence items.
  - **A golden-independent analytic check on that scene.** Each sphere's
    centre pixel equals the tone-mapping reference
    (`tests/image_regression/support/tone_mapping_reference.h`) applied to its
    emissive value at the scene's exposure. This proves the term is additive
    and needs no light, independently of the golden.
  - **Light independence.** A test that changes the scene's lighting
    (adding a light, or moving one) shows the emissive contribution is
    unchanged, by a method the Plan chooses.
  - **Coverage of all ten variants.** Every shader variant is exercised by at
    least one GPU test in which a non-zero emissive visibly changes the frame
    in the expected direction; the IBL-only kinds need an environment, so
    their mechanism is O5.
- **R7:** covered by the GPU tests above, which drive the real realization
  and draw path.
- **Golden neutrality:** all 20 existing golden directories compare
  byte-identical at zero tolerance, in both Debug and Release.
- **Vulkan Validation Layers:** zero warnings or errors in Debug, where they
  are fatal. `vkCmdPushConstants` at 128 bytes on `PbrSheen` is the specific
  call to watch.
- **Android:** `assembleDebug` green.

## Risks & Open Questions

- **O1 — source grammar shape.** The parser counts lines, so an optional line
  collides with existing shapes. (a) An optional `emissive_factor:` line at a
  fixed position — after the kind-specific pair, before `normal_map` —
  identified by its prefix, with the count gate widened to match. (b) A
  mandatory line in every 8+-line PBR form, keeping pure count dispatch but
  editing all 48 PBR sources beyond their version line. **Recommend (a):**
  existing sources change only their version line, and the parser already
  peeks at prefixes (`material_source.cpp:205-212`).
- **O2 — the upper bound.** 65,504 (the `Rgba16Float` finite maximum) versus
  finite-only. **Recommend 65,504**, because ⑥'s bright-pass should never read
  `inf` out of a material.
- **O3 — does this Spec change the importer?** With the field in place, the
  importer could map Bistro's factors instead of dropping them. **Recommend
  mapping only the 10 texture-less materials**, and continuing to drop and
  report the 11 textured ones. Mapping their factor without the texture
  would light whole signs uniformly — a visible regression from "dark" to
  "wrong". This is roughly a ten-line importer change plus its report line;
  the alternative is leaving the importer entirely to ⑦.
- **O4 — accept `PbrSheen` at exactly 128.** It is within the guarantee, and
  Slang and MSVC agree. The cost is that Sheen's next vector parameter forces
  the uniform-buffer decision. **Recommend accept**, with the consequence
  recorded in ADR-0089 — the alternative of moving Sheen's own fields into a
  uniform buffer now is the larger change, made ahead of any need.
- **O5 — GPU coverage for the IBL-only kinds.** A true dark scene needs no
  environment, but Clearcoat, Sheen, Anisotropic and `pbr_ibl*` always sample
  one. Options: (a) a differential test — the same scene with emissive zero
  and non-zero; the emissive frame is brighter exactly where the emissive
  material is, and unchanged elsewhere; (b) a new zero-radiance environment
  asset for a true dark scene on every kind. **Recommend (a):** it needs no new
  asset, and the dark-scene golden already proves additivity on the shared
  code path.
- **Risk — golden movement from compiler behaviour.** Treated as a gate
  (Investigation 5).
- **Risk — the importer's report is stale if O3 is ruled out.** Its "no v6
  destination" wording would become inaccurate once v7 has the field; at
  minimum the wording changes.

## Out of Scope / Future Work

- **Emissive texture**, reopened when workflow ⑦'s assembled Bistro shows the
  11 textured emissive materials matter. By then the choice between a
  variant dimension and an always-bound slot will have a real scene behind it.
- **Bloom** (workflow ⑥) consumes the HDR emissive this Spec produces.
- **Photometric units** for emissive, with the light-intensity question
  already reassigned by Spec 0040 O5.
- **A per-material uniform buffer** — ADR-0081 Future Work, now the forced
  next step for `PbrSheen`.
