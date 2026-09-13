# Spec: Clearcoat, Sheen, and Anisotropy PBR Materials

- **Status:** Proposed
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-13
- **Related Plan(s):** None yet
- **Approval:** pending
- **Related ADR(s):** [ADR-0081: PBR Material BRDF Extension — Clearcoat, Sheen, Anisotropy](../adr/0081-pbr-material-brdf-extension-clearcoat-sheen-anisotropy.md) (`Proposed`)

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

Extends Atlantis's PBR material model (Spec 0023, ADR-0066/ADR-0067) with
three additional, independently-verifiable BRDF features — **clearcoat**,
**sheen** (cloth), and **anisotropy** — matching the vocabulary and math
Filament's own documentation defines for the same features, and adds a new
IBL-only showcase scene visually comparable to Filament's own
`example_materials1.jpg` reference image (a fan-shaped array of roughly
30 dimpled test spheres, one distinct material per sphere, on a rough
concrete floor, lit only by a warehouse-interior environment map) to
exercise and demonstrate all three together with the engine's existing
base metallic-roughness materials. No directional light is required or
used — this Spec's own investigation (see Requirements/Non-functional)
confirmed the zero-directional-light, IBL-only rendering path already
exists and is already golden-tested today.

## Motivation / Problem Statement

Atlantis's PBR material system today implements exactly one BRDF: the
standard Cook-Torrance metallic-roughness model (Spec 0023), extended
with tangent-space normal mapping (Spec 0029) and image-based lighting
(Spec 0025). Spec 0025 explicitly listed "clear coat" as a Non-Goal at
the time ("Shadows, ambient occlusion, normal mapping/tangent-space
input, clear coat, ... are out of scope", Spec 0025 line 63) — a
deliberate, disclosed deferral, not an oversight, but one this Spec now
picks up.

A materially large, common class of real-world surfaces cannot be
represented at all today: clear-coated/lacquered surfaces (car paint,
varnished wood, carbon fiber), cloth/fabric (velvet, satin, nylon), and
anisotropically-brushed materials (brushed steel, hair, vinyl records).
Filament — a comparable, well-documented open-source PBR engine — treats
exactly these three as its next tier of material sophistication beyond
base metallic-roughness, with a published, precise BRDF definition for
each. Building the same three lets Atlantis validate its material/shader
extension path against an established, citable reference model rather
than inventing shading math from scratch, and gives the engine a
substantially more capable material showcase than any single-BRDF scene
can demonstrate.

## Goals

- Three new BRDF features, each independently implementable, buildable,
  and verifiable on its own: **Clearcoat**, **Sheen**, **Anisotropy**.
  Each ships as its own new `MaterialKind` (see Architectural Impact and
  ADR-0081) with its own dedicated shader pair and its own golden-image
  test — no feature's implementation or verification depends on either
  of the other two landing first.
- An IBL-only lighting path for the new showcase scene: zero
  `atlantis::world::Light` entities, matching `ibl_material_demo`'s own
  already-shipping, already-golden-tested precedent (Requirements/
  Non-functional below) — not a new capability, a reused one.
- A new showcase scene, visually comparable to Filament's own
  `example_materials1.jpg` (this Spec's own content-source investigation
  below establishes exactly what "comparable" can honestly mean here):
  a fan/arc arrangement of a few dozen dimpled test spheres on a rough
  concrete-like ground plane, each sphere a distinct material (existing
  base PBR materials plus the three new BRDFs), lit only by a
  warehouse-interior HDRI environment map, no directional light, no
  hard shadow.
- A fourth entry in Spec 0032's closed `--scene` whitelist
  (`ATLANTIS_ibl_material_demo_scene_*`-style addition — exact name is
  Plan-stage detail) so the new scene is reachable from the real
  `atlantis_runtime.exe` product binary, matching every existing sample
  scene's own selection path.
- Windows goldens: one per new BRDF (a small, parameterized single- or
  few-sphere scene sweeping that BRDF's own new parameter, mirroring
  `tests/image_regression/`'s existing per-feature pattern — e.g.
  `ibl_material_demo`'s sphere-grid precedent) plus one for the full
  showcase scene.
- Android asset-manifest lock-step: every new packaged shader pair or
  texture this Spec's implementation adds gets a matching entry in both
  `android_main.cpp`'s literal `extractAsset()` list and
  `android/app/build.gradle`'s `namedAssetRelativePaths` list — the
  existing, cross-referencing obligation each file's own comment already
  states (`android_main.cpp:63-72`, `android/app/build.gradle:16-22`),
  applied to this Spec's own new assets exactly as it already applies to
  every asset shipped so far. This Spec does not itself decide whether
  the new showcase scene becomes Android's default boot scene (see
  Non-Goals) — the lock-step obligation is about *packaged assets*, not
  about which scene `android_main.cpp` boots.

## Non-Goals

- **Screen-space ambient occlusion / contact shadows.** Explicit,
  reasoned decision (required by this Spec's own drafting instructions):
  the showcase scene accepts **no contact shadows or grounding cues
  under each sphere** — pure IBL, no compensating weak directional
  light. Reasoning: (1) Atlantis has no ambient-occlusion mechanism of
  any kind today — Spec 0025 itself listed "ambient occlusion" as a
  Non-Goal alongside "clear coat" and it remains unimplemented; adding
  one is a real, separate rendering-pass addition (a new
  RenderGraph pass, a new non-trivial algorithm) wholly disproportionate
  to a Spec about BRDF parameterization. (2) A "weak directional light
  for shadow-casting only" would reopen Spec 0027/ADR's directional
  shadow-map machinery and Spec 0030's shadow-bias-stability concerns
  for a scene whose ~30 objects sit at wildly different heights/radii
  around an arc (unlike `integrated_showcase_demo`'s more uniform
  ground-plane layout) — a materially harder shadow-mapping case this
  Spec's own BRDF focus should not have to solve. (3) This Spec's own
  investigation (Requirements/Non-functional) confirms zero-directional-
  light IBL-only rendering is already a real, working, golden-tested
  path (`ibl_material_demo`) — reusing it exactly, with the visual
  trade-off of no contact shadow disclosed here, keeps this Spec's scope
  to BRDF work only. A future spec may add SSAO/contact shadows against
  any scene, including this one, as a wholly separate concern.
- **Transmission / refraction.** No transmissive/refractive BRDF term
  (Filament's own `refractionMode`) — out of scope; none of the three
  BRDFs this Spec adds require it.
- **Emissive materials.** No emissive term added to the material schema
  by this Spec.
- **Bloom / any new post-processing pass.** Purely a material/BRDF and
  scene-content Spec; the existing HDR/tone-mapping pipeline (Spec 0024)
  is reused unchanged.
- **Per-material shadow casting/receiving toggles.** Moot given the SSAO/
  contact-shadow decision above — no shadow map exists in this scene at
  all.
- **glTF import.** Explicitly not this Spec's concern — the showcase
  scene's content is authored directly against Atlantis's own asset
  pipeline (`.amesh`/`.amat`/`.atex`/`.aenv`/`.ascene`), matching every
  existing sample scene; no glTF loader exists in this engine and none
  is added here.
- **Arbitrary combination of the three new BRDFs on one material** (e.g.
  a single material that is simultaneously clearcoat-over-anisotropic,
  the way real brushed-and-lacquered car paint physically is). See
  ADR-0081's Decision and Consequences — each of the three ships as its
  own mutually-exclusive `MaterialKind`, not a combinable feature-flag
  set, primarily because of the push-constant budget finding below.
  Composability is real future work, not silently foreclosed
  architecturally, just not decided or built now.
- **Changing Android's default boot scene** away from
  `integrated_showcase_demo`. Whether the new showcase scene becomes
  Android's own new default (`android_main.cpp` currently hard-builds
  one fixed `BootstrapConfig`, per Spec 0032's own Non-Goals: "Android/
  iOS entry points ... out of this Spec's own scope") is a separate
  product decision this Spec does not make — see Goals above for what
  this Spec *does* obligate on the Android side (asset lock-step only).

## Requirements

### Functional

1. **Clearcoat BRDF** — a second, energy-aware specular lobe layered over
   the existing base metallic-roughness lobe, parameterized by
   `clearcoatFactor` (0-1 intensity) and `clearcoatRoughness` (typically
   near-zero — a thin, smooth coat), matching Filament's own clear coat
   model (see Filament, *Clear coat model* — link in Non-functional
   below). An optional clearcoat normal map (a texture/descriptor
   binding, not a push-constant field, following normal mapping's own
   established Spec 0029 precedent) lets the coat's own microsurface
   diverge from the base surface's normal (e.g. carbon-fiber weave under
   a smooth coat — directly the look this Spec's own reference-image
   investigation below identifies on at least one sphere).
2. **Sheen BRDF** — a retroreflective, grazing-angle-peaked lobe
   approximating cloth/fabric (velvet, satin), parameterized by
   `sheenColor` (RGB, linear-space, following `baseColorFactor`'s own
   established ADR-0066 convention) and `sheenRoughness`, matching
   Filament's own cloth/sheen model (Filament, *Cloth model* — link
   below).
3. **Anisotropy BRDF** — an elongated, direction-dependent specular
   highlight (brushed metal, hair), parameterized by `anisotropyFactor`
   (-1 to 1 strength/sign) and `anisotropyRotation` (rotates the
   existing per-vertex tangent, Spec 0029, about the normal — no new
   vertex attribute is needed; see ADR-0081's Decision), matching
   Filament's own anisotropic model (Filament, *Anisotropic model* —
   link below).
4. Each of the three is its own new `MaterialKind` enumerator, its own
   dedicated `.slang` shader pair, and its own material-asset schema
   version bump — exactly mirroring `PbrDirectLit`'s own existing
   relationship to `UnlitTextured`/`LitTextured` (ADR-0066/ADR-0067),
   not a modification of the existing `PbrDirectLit` kind's own shader
   or push-constant layout. See ADR-0081.
5. **IBL-only lighting.** The new showcase scene (and each per-BRDF
   golden scene) declares zero `Light` entities; environment/IBL is the
   sole light source, using the existing `MaterialEnvironmentBinding::Ibl`
   mechanism (`ibl_material_demo`'s own precedent) — no new lighting
   capability is required (see Non-functional below for the confirming
   investigation).
6. **Showcase scene content.** A new `.ascene` (name: Plan-stage detail)
   containing on the order of Filament's own reference image's visible
   material count (this Spec's own direct inspection of
   `example_materials1.jpg`, downloaded and viewed at full resolution
   during drafting — see Risks & Open Questions for what could not be
   verified — counted roughly 24 spheres fully or mostly visible within
   that single photo's own frame, with at least one more sphere visibly
   cropped at the frame's left edge, consistent with a fuller underlying
   array the photo itself does not fully capture; this Spec targets
   "on the order of 24-30", not an exact reproduced count, since the
   photo itself does not establish one), arranged in a fan/arc, each on
   its own small pedestal-like base, on a rough, granular gray ground
   plane (visually: weathered concrete/asphalt, matching the "cement
   ground" description), lit by one new warehouse-interior-style HDRI
   environment (content source: see Requirements/Non-functional
   "Content source" below). The majority of spheres use existing base
   PBR materials (metal, wood-grain, stone/marble, tiled/textured
   dielectric — all already representable via `PbrDirectLit`); a
   deliberately identifiable subset uses each of the three new BRDFs
   (at minimum one clearly-clearcoat, one clearly-sheen/fabric, one
   clearly-anisotropic-brushed sphere), so the scene both demonstrates
   parity with the existing material system and exercises all three new
   features together in one real, compiled, golden-tested scene.
7. **Spec 0032 whitelist extension.** A fourth `--scene` value is added
   to the existing closed whitelist (`integrated_showcase_demo`,
   `ibl_material_demo`, `pbr_normal_map_demo`, **+ this Spec's new
   scene**), following Spec 0032's own established `SceneWhitelistEntry`
   pattern exactly — no change to Spec 0032's own CLI grammar, error
   handling, or `cli.h`/`cli.cpp` structure, purely a new table entry
   plus the new scene's own CMake asset declaration.
8. **Android asset lock-step** (Goals above) — functional requirement,
   not merely a goal: this Spec's Plan/Implementation must not add a new
   packaged shader pair or texture without a matching literal-list entry
   in both `android_main.cpp` and `android/app/build.gradle`, reviewed
   against each other by a human exactly as both files' own existing
   comments already require for every prior asset.

### Non-functional

- **Directional-light requirement — investigated, confirmed false.**
  This Spec's own required pre-drafting investigation read Spec 0019
  (`Light` is always an *optional* per-`World`-entity component,
  never mandatory; the "exactly 1 `Directional` slot" cap in the scene
  grammar is an upper bound, not a lower one), ADR-0061/ADR-0062 (no
  scene-load-time or `World`-level invariant requires ≥1 `Light`;
  `FrameLightingData.directionalLightCount` is documented `0 or 1`), and
  the actual runtime code (`scene_extraction.cpp`'s
  `extractFrameLightingData()` returns a well-formed, zero-filled
  `FrameLightingData` for zero lights; every shader-side lighting loop
  in the PBR direct-lit path is bounds-safe over `directionalLightCount`
  and is simply a no-op at zero). Most directly: **this exact
  configuration is already a real, executed, golden-verified GPU test**
  — `tests/image_regression/ibl_material_demo_gpu_tests.cpp` asserts
  `fixture.world->lightEntities().empty()` and renders/golden-compares a
  real frame through the genuine `RuntimeHost`/`Renderer` path. This
  Spec's own showcase scene reuses this exact, already-proven path; no
  runtime or shader change is needed to support "no directional light."
- **Material parameter capacity — investigated, a real, load-bearing
  constraint.** `PbrPushConstants` (the current `PbrDirectLit` push-
  constant layout, `src/renderer/src/pbr_push_constants.h`) is 96 of
  Vulkan's guaranteed-minimum 128-byte push-constant budget — 32 bytes
  of documented headroom, ADR-0067 D-3, which that ADR's own
  Consequences already flagged as thin for even *one* future scalar.
  Clearcoat's own two scalars (~8 bytes) fit inside that headroom
  comfortably; sheen's `sheenColor` (a `vec3`) is the likely tipping
  point once real Slang/SPIR-V alignment padding is accounted for, not
  a bare 12-byte cost. **This is exactly why ADR-0081 puts each new BRDF
  in its own `MaterialKind` with its own independent push-constant
  layout** (each new kind's own struct grows from the *same* 96-byte
  `objectToWorld`+`baseColorFactor`+`metallic`+`roughness` base, not
  from a shared, ever-growing single struct) rather than adding feature
  flags to `PbrDirectLit`'s existing, nearly-exhausted 96-byte layout.
  Exact per-kind byte layouts are explicitly **not** fixed by this Spec
  or ADR-0081 — they require the same real Slang-reflection-plus-MSVC-
  static_assert confirmation ADR-0067 D-3 itself performed, which is
  Plan/Implementation-stage work, not Spec-stage assertion.
- **Anisotropy vertex-attribute impact — investigated, none.** Spec
  0029/ADR-0073's existing mandatory per-vertex tangent (object-space
  `xyz` + handedness `w`) already gives anisotropy everything it needs;
  Filament's own anisotropic model rotates exactly this kind of existing
  tangent, in the fragment stage, by a scalar `anisotropyRotation` — no
  new vertex stream, no mesh-artifact schema change, no geometry-format
  version bump.
- **Descriptor pool capacity — investigated, already solved.** Spec
  0021/ADR-0064 already made the Vulkan Backend's descriptor pool
  growable (no fixed `maxSets` ceiling) specifically to absorb workloads
  larger than what a fixed pool was originally sized for; a ~30-material
  showcase scene, each material needing its own descriptor set(s), is
  exactly the kind of larger, real workload that existing growth
  mechanism already exists to absorb. No new descriptor-pool work is
  required by this Spec.
- **MaterialKind dispatch mechanism — investigated; see ADR-0081.**
  `MaterialKind` is a closed 3-value enum today
  (`UnlitTextured`/`LitTextured`/`PbrDirectLit`), but real shader-pair
  *selection* already nests two independent booleans (`hasNormalMap`,
  `environmentEnabled`) inside the `PbrDirectLit` arm of
  `selectShaderPair()`, picking among 4 real, already-compiled `.slang`
  pairs today. ADR-0081 decides how the three new BRDFs extend this
  existing structure.
- **BRDF physical-correctness reference.** Each of the three BRDFs is
  implemented against Filament's own published, precise shading-model
  definitions — not reinvented: [Filament, *Clear coat model*](https://google.github.io/filament/Filament.md.html#materialsystem/clearcoatmodel),
  [Filament, *Cloth model*](https://google.github.io/filament/Filament.md.html#materialsystem/clothmodel),
  [Filament, *Anisotropic model*](https://google.github.io/filament/Filament.md.html#materialsystem/anisotropicmodel).
  This Spec adopts these as its own normative shading-math reference —
  Plan/Implementation must cite the specific equations used from these
  sections, matching ADR-0067's own established "cite the real math"
  discipline for the existing base BRDF.
- **Content source — investigated; assets are not available to reuse.**
  This Spec's own required pre-drafting investigation fetched and
  visually inspected both `example_materials1.jpg` and
  `example_materials2.jpg` (Filament's `docs/images/samples/`, Apache-
  2.0-licensed repository) at full resolution, and separately inspected
  `google/filament`'s own `samples/materials/` directory. Finding:
  **the specific dimpled-sphere model, the ~30-texture material set, and
  the warehouse-interior HDRI shown in `example_materials1.jpg` are not
  included anywhere in the public `google/filament` repository** —
  `samples/materials/` contains only `.mat` shader-definition files for
  an unrelated interactive `material_sandbox` demo (`sandboxLit.mat`,
  `sandboxCloth.mat`, etc.), with no such geometry or texture set;
  `example_materials1.jpg`/`example_materials2.jpg` are marketing/hero
  images whose own source content was never checked into the repository
  the README embeds them in. "Reuse Filament's own materials1 assets,
  with attribution" is therefore **not an available option** — there is
  nothing to reuse. This Spec instead recommends **separately-sourced
  CC0 content**, following the exact sourcing precedent
  `google/filament`'s own real, redistributed `third_party/environments`
  and `third_party/textures` already set: that directory's own
  `URL.txt` names `https://hdrihaven.com/` (now Poly Haven,
  `polyhaven.com`) as its HDRI source — a CC0 environment-map provider
  with several genuinely warehouse/industrial-interior-styled HDRIs
  available today. Equivalent CC0 PBR texture sets (metal, wood, stone,
  fabric, carbon-fiber-weave) are available from the same class of
  provider (Poly Haven, ambientCG). Plan/Implementation must record a
  `URL.txt`-equivalent attribution file alongside any such imported
  content, matching Filament's own documented practice, whether or not
  Atlantis's license requires it — precedent, not obligation, is the
  reason to do it anyway. A fully procedural (shader-authored, no
  imported texture) alternative was considered and is not recommended
  as the primary path (Alternatives Considered) — real CC0 PBR texture
  sets are more visually comparable to the reference image with
  substantially less new engineering than procedural material
  authoring, which this Spec does not otherwise need.
- **Performance:** ~30 draw calls plus one environment/IBL setup, well
  within every existing scene's own real, already-measured envelope
  (`integrated_showcase_demo` already renders comparably many objects);
  no new performance concern.
- **Memory:** New shader `.slang` pairs, new texture assets (CC0-sourced,
  see above), and a modest ~30-entry scene file — no new large runtime
  allocation pattern; descriptor pool growth already solved (above).
- **Portability (Vulkan-only Phase 1):** No portability-relevant change
  — Windows and Android both already render `PbrDirectLit`/IBL content
  through the same RHI/RenderGraph path; the three new `MaterialKind`s
  add shader pairs to that same, already-cross-platform pipeline. The
  Android on-device verification path itself currently has a disclosed,
  temporary gap — the real Android emulator translation layer this
  repository's CI/manual verification currently runs against cannot
  load `VK_LAYER_KHRONOS_validation` at all (Plan 0034 Milestone 6,
  commits `b7d5caa`/`6c4d8a5`: two independent, confirmed upstream
  translation-layer defects) — so Android verification of this Spec's
  own new shaders inherits that same, already-disclosed "no Validation
  Layer coverage on Android until the upstream defect closes or a real
  device is used" gap. This Spec does not reopen or re-decide that gap;
  it is stated here only so this Spec's own Testing & Verification Plan
  does not silently claim Android Validation coverage it cannot have.
- **Other — compatibility:** No existing scene's rendering, golden
  image, or material asset schema version changes. `MaterialKind`'s
  existing three enumerators (`UnlitTextured`/`LitTextured`/
  `PbrDirectLit`) and `PbrDirectLit`'s own existing 96-byte push-constant
  layout are both unchanged (ADR-0081).

## Proposed Design

Three new `MaterialKind` enumerators — `PbrClearcoat`, `PbrSheen`,
`PbrAnisotropic` (exact names Plan-stage detail) — each following
`PbrDirectLit`'s own existing shape exactly: its own material-asset
schema fields (scalar factors + optional texture references), its own
push-constant struct (the existing 96-byte `objectToWorld`/
`baseColorFactor`/`metallicFactor`/`roughnessFactor` base plus that
kind's own ~2-4 new scalar fields, individually budget-confirmed against
Vulkan's 128-byte guarantee at Plan/Implementation time — see Non-
functional), its own `.slang` shader pair(s) implementing that BRDF
against Filament's cited math, and its own arm in `selectShaderPair()`'s
existing closed switch. At minimum, each new kind gets an IBL-lit
variant (this Spec's own scene is IBL-only); a direct-lit variant of any
new kind is Out of Scope/Future Work unless Plan finds a concrete need.

The showcase scene is a new `.ascene` asset: roughly two dozen-plus
`PbrDirectLit` spheres (reusing existing base-material machinery,
skinned with newly-sourced CC0 PBR textures — metal, wood, stone,
tile/mosaic — matching the reference image's own visible material
variety) arranged in a fan/arc on a ground-plane mesh, plus at least one
sphere each of `PbrClearcoat`, `PbrSheen`, and `PbrAnisotropic`, all
using a single new warehouse-interior CC0 HDRI environment
(`MaterialEnvironmentBinding::Ibl`), zero `Light` entities. The sphere
mesh itself gets a small spherical-cap indentation ("dimpled"), matching
the reference photo's own test-ball geometry — a simple, original mesh
Atlantis authors itself (not imported from anywhere), since a plain UV
sphere with one recessed cap is geometrically trivial and carries no
licensing question at all.

Exact scene name, exact per-kind push-constant field names/order, exact
`.slang` file names, exact sphere/pedestal mesh authoring approach, and
the exact CC0 texture/HDRI sources chosen are Plan-stage detail, not
fixed here — matching this codebase's own repeatedly-established
"exact identifiers are Plan's own freedom" precedent (Spec 0013 item 8,
reaffirmed by Spec 0032's own Risks & Open Questions).

## Architectural Impact

**Yes.** This Spec extends `MaterialKind` (an existing, closed,
already-architecturally-significant enum — ADR-0066) with three new
enumerators, each a new BRDF/shader-model choice, and extends the
material-asset schema (ADR-0066) and the `PbrDirectLit` push-constant
precedent (ADR-0067) with new per-kind parameter layouts. Both are
squarely "a subsystem boundary, a public API, ... or a backend-
abstraction contract" per this template's own Architectural Impact
question. The decision — three new mutually-exclusive `MaterialKind`s
with independent per-kind push-constant budgets, rather than combinable
feature flags on the existing `PbrDirectLit` kind — is recorded in
[ADR-0081](../adr/0081-pbr-material-brdf-extension-clearcoat-sheen-anisotropy.md)
(`Proposed`), which must reach `Accepted` before this Spec is approved
for implementation, per AGENTS.md's own ADR workflow rule. ADR-0081 is
explicitly framed as an **extension** of ADR-0066/ADR-0067's own already-
`Accepted` decisions (same asset-schema-versioning approach, same
linear-space scalar-factor convention, same texture-vs-push-constant
split, same closed-switch `MaterialKind` dispatch pattern), not a
reversal of either.

This Spec was checked against Spec 0032/ADR-0076 (the `--scene`
whitelist this Spec's own Requirement 7 extends — confirmed additive,
no change to that Spec's own CLI grammar or architecture) and against
Spec 0021/ADR-0064 (descriptor pool growth — confirmed this Spec's own
~30-material workload is exactly the class of growth that ADR already
accounts for, no conflict). No other `Accepted` ADR's scope is touched.

## Alternatives Considered

- **Feature flags on the existing `PbrDirectLit` `MaterialKind`**
  (`clearcoatEnabled`/`sheenEnabled`/`anisotropyEnabled` booleans, akin
  to today's `hasNormalMap`/`environmentEnabled`): rejected as this
  Spec's primary design — see ADR-0081's own Alternatives Considered for
  the full reasoning (push-constant budget exhaustion, shader-variant
  combinatorial growth, and this Spec's own "independently verifiable"
  Goal all favor separate kinds).
- **A per-material uniform buffer** replacing/supplementing push
  constants for the new BRDFs (removing the 128-byte ceiling entirely):
  considered, and flagged in ADR-0081 as the natural next step *if* a
  future spec needs true feature combinability — rejected as this
  Spec's own primary design because it is a materially larger
  architectural change (a new descriptor binding, new buffer-lifetime/
  update-frequency decisions) than three independent, already-budget-
  fitting push-constant layouts need.
- **Reusing Filament's own `example_materials1.jpg` source assets, with
  attribution**: rejected — not available; see Requirements/Non-
  functional "Content source" above. There is no asset to attribute.
- **Fully procedural (shader-generated) material textures** instead of
  imported CC0 texture sets: considered as the content source for the
  showcase scene's non-BRDF (existing base-PBR) spheres; not recommended
  as the primary path — real CC0 PBR texture sets already closely match
  the reference image's own visual variety (wood grain, stone, rust,
  tile) at a fraction of the shader-engineering cost procedural texture
  synthesis (e.g. procedural wood-ring noise, procedural marble veining)
  would require, and this Spec does not otherwise need procedural
  texturing as a capability. Not foreclosed for a future spec.
- **A weak directional light purely for ground-contact shadowing**:
  rejected — see Non-Goals' own SSAO/contact-shadow decision above for
  the full reasoning.
- **Exactly reproducing the reference photo's sphere count/arrangement
  pixel-for-pixel**: rejected — the photo itself does not establish an
  exact, complete count (see Requirement 6's own citation of what this
  Spec's direct inspection could and could not determine); "visually
  comparable, on the order of the same scale" is the honest, achievable
  target, not a pixel-exact reproduction of an image whose own full
  content (assets, exact camera, exact sphere count) is not available.

## Testing & Verification Plan

- **Per-BRDF GPU/image-regression goldens** (3 new tests, one per new
  `MaterialKind`), each a small, parameterized scene (mirroring
  `tests/image_regression/`'s existing sphere-grid/parameter-sweep
  pattern, e.g. `ibl_material_demo_gpu_tests.cpp`'s own structure)
  sweeping that BRDF's own defining parameter (`clearcoatRoughness`,
  `sheenRoughness`, `anisotropyRotation`) across a small range, rendered
  through the real `RuntimeHost`/`Renderer` path, golden-compared, with
  Vulkan Validation Layers clean throughout (Windows).
- **Showcase-scene golden** (1 new test): the full ~24-30-sphere scene,
  rendered once, golden-compared on future changes exactly like every
  other scene's own golden; **human visual review against the real
  Filament reference image** for "comparable in spirit" (not a pixel
  diff against `example_materials1.jpg` itself, which is not this
  engine's own output and never will be) — recorded as this Spec's own
  Implementation PR's evidence, per AGENTS.md's existing golden-review
  discipline.
- **`--scene` whitelist GPU-independent tests**: the new fourth entry's
  own name-to-path mapping, following Spec 0032's own existing
  `parseCommandLine()`/whitelist test pattern exactly — no new test
  category, an extension of an existing one.
- **Android on-device verification**: `assembleDebug` → install →
  `am start` → sustained render → `screencap`, on a running emulator or
  real device, following Plan 0034 Milestone 6's own established
  protocol exactly (including its own logcat-summary/crash-absence
  checks) — with the disclosed "no Validation Layer coverage on this
  translation-layer emulator" gap (Non-functional above) applying here
  too, not re-decided by this Spec.
- **Windows regression**: full Debug + Release build, full `ctest`, zero
  real Validation Layer hits — the standard, unconditional AGENTS.md
  requirement, re-run because this Spec's implementation touches shared
  renderer/asset-system code.

## Risks & Open Questions

- **Reference-image analysis is necessarily approximate.** This Spec's
  own direct inspection (both `example_materials1.jpg` and
  `example_materials2.jpg`, downloaded and viewed at full resolution
  during drafting) could establish sphere geometry (a dimpled/recessed-
  cap sphere on a small pedestal), rough material variety (metals,
  wood-grain, stone/marble, tile/mosaic, carbon-fiber weave, and several
  glossy solid-color "lacquer" spheres consistent with a clearcoat
  demonstration), ground appearance (rough gray concrete/asphalt), and
  background character (a blurred warehouse-interior scene — a support
  pillar and a blue waste container visible in `example_materials1.jpg`
  specifically), and a rough, non-exact visible sphere count (≈24
  visible within that one photo's own frame, with evidence of at least
  one more cropped at the edge). It could **not** establish: which
  specific sphere, if any, was Filament's own authors' intended
  demonstration of clearcoat vs. sheen vs. anisotropy specifically (the
  image carries no labels) — Plan/Implementation should treat Filament's
  own written material-model documentation (cited above) as the
  authority for *what each BRDF should look like*, and this Spec's own
  showcase scene's exact sphere-to-material assignment as an original
  composition inspired by, not copied from, the reference photo's
  specific arrangement.
- **Anisotropy shading-math complexity.** Anisotropic GGX is a real step
  up in implementation complexity from the existing isotropic GGX term
  — Plan/Implementation must work from Filament's own cited equations
  directly, not approximate; flagged here as the highest shader-
  correctness risk of the three new BRDFs.
- **Exact per-kind push-constant byte layouts are not yet confirmed.**
  Flagged throughout this Spec as Plan/Implementation-stage work
  requiring real Slang reflection + MSVC layout probes, exactly as
  ADR-0067 D-3 already established as this codebase's own required
  method — not asserted as fact here.
- **CC0 content sourcing is a real, if modest, content-authoring task**
  not previously part of any Atlantis spec's own scope (every existing
  scene's textures were, to this Spec's own drafting knowledge, either
  procedurally trivial or already in-repository) — Plan should size this
  honestly as real content work, not incidental.

## Out of Scope / Future Work

- Arbitrary combination of clearcoat/sheen/anisotropy on one material
  (Non-Goals above; ADR-0081's own Consequences names this as the
  natural next step if ever needed).
- SSAO/contact shadows against this or any other scene (Non-Goals
  above).
- Transmission/refraction, emissive materials, bloom (Non-Goals above).
- Android becoming this scene's default boot target (Non-Goals above).
- A general glTF import path (Non-Goals above) — unrelated to this
  Spec's own narrow, hand-authored-asset scope.
