# Spec: PBR Material Foundation (Direct Lighting)

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-08-30
- **Related Plan(s):** none yet — this Spec's own Human Review Approval
  authorizes drafting a Plan only, not any Implementation.
- **Related ADR(s):**
  [ADR-0066](../adr/0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Accepted` — Material asset parameter set, artifact schema,
  base-color texture color-space contract) and
  [ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (`Accepted` — exact BRDF, push-constant extension, rendering
  contract), deliberately two single-responsibility ADRs. A third,
  real piece — extending the shared Camera/Lighting uniform buffer
  with a camera world-space position — is recorded as an **Accepted
  Amendment to
  [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)**
  (the ADR that already owns that buffer's layout), not a third ADR
  and not folded into ADR-0067.
- **Human Review Approval (2026-08-30):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-30, accepting this document's
  own "Decisions for Human Review" (D1–D25) in full, as recorded in
  commit `0fc6a14`. Accepting ADR-0066 and ADR-0067 (`Proposed` →
  `Accepted`) and ADR-0062's own Amendment (`Proposed Amendment` →
  `Accepted Amendment`, that ADR's own top-level Status unchanged at
  `Accepted`) in the same pass. **This approval authorizes drafting
  Plan 0023 only, once this PR merges — not any Implementation.**

## Summary

Proposes `MaterialKind::PbrDirectLit` — a metallic-roughness
Cook-Torrance BRDF (Lambertian diffuse + GGX/Smith-Schlick specular)
against the Directional/Point light data this codebase already computes
and uploads every frame (Spec 0022). Continues the existing
single-base-color-texture-plus-scalar-parameters Material architecture
— no second texture, no normal map, no IBL, no shadows — specifically
to avoid reopening Spec 0021's descriptor-pool-capacity proof.

## Motivation / Problem Statement

`LitTextured` (Spec 0019) has no specular term, no roughness/metallic
concept, and no physically-motivated Fresnel or energy conservation
(confirmed: `lit_textured.slang` is pure N·L diffuse). Directed by the
human maintainer as the next drafting priority, ahead of Android
Platform, Shadow Foundation, IBL, and Post-processing.

## Goals

1. A new, closed `MaterialKind::PbrDirectLit` rendering a precisely
   specified BRDF for Directional and Point lights only, against the
   existing, unmodified `FrameLightingData`.
2. Continue the single-base-color-texture architecture — one texture
   reference (existing `textureAsset` field) plus three new scalar
   parameters, never a second texture binding — keeping Spec 0021's
   descriptor-pool-capacity proof intact without re-derivation.
3. Real mesh normal (Spec 0020) and UV0 (Spec 0017) are the only new
   geometric inputs — no tangent, no normal map.
4. `UnlitTextured`/`LitTextured` remain byte-for-byte/pixel-for-pixel
   unchanged — their `.slang` source and all five existing goldens are
   unmodified.
5. Every new parameter travels through the existing Asset System and
   RHI/push-constant chains — no new Asset Catalog, Material Graph,
   Serialization module, or third-party dependency.

## Non-Goals

IBL/environment reflection/HDR environment map; shadows of any kind;
normal maps or a tangent vertex attribute (no tangent field exists in
this codebase's mesh layout today); ambient occlusion, emissive,
clear-coat, transmission; tone mapping/exposure/HDR intermediate target
(the existing hard-clamp contract is reused unchanged — D14/ADR-0067
D-6); mipmapping/compression/streaming; bindless rendering/descriptor
indexing; a second base-color/metallic-roughness texture (see D5; an
explicit escape hatch if real evidence proves this insufficient — see
Risks); Android/iOS/Linux implementation (Android Platform's own
Candidate Order 1 is unaffected); any Editor/Material Graph/authoring
UI; re-capturing or modifying any of the five existing goldens.

Registered future candidates (none designed here): PBR Texture Set /
Multi-Texture Binding; Tangent Attribute + Normal Mapping; IBL / HDR
Environment; Shadow Foundation; Tone Mapping / Post-processing.

## Requirements

### Functional

- A `kind: pbr_direct_lit` Material must cook/load/realize/render via a
  distinct `pbr_direct_lit.slang` pair — never silently falling back to
  another kind's shader.
- `baseColorFactor`/`metallicFactor`/`roughnessFactor` survive a full
  cook→artifact→metadata→load round trip byte-for-byte and are
  independently re-validated at load time (ADR-0066 item 5).
- A `PbrDirectLit` material's texture must be rejected at scene-load
  time if not `Rgba8Srgb` (ADR-0066 item 6) — a real, functioning
  validation.
- `UnlitTextured`/`LitTextured` render identically before/after —
  verified by the five existing goldens staying unchanged.
- A scene mixing all three `MaterialKind`s renders correctly in one
  frame through the existing, unmodified per-`DrawItem` bind/push loop.

### Non-functional

- **Performance:** no stated budget beyond "renders correctly" — no
  currently-shipped scene stresses fragment ALU cost.
- **Memory:** the material artifact widens by exactly 24 bytes (56
  total, ADR-0066 item 3); the push-constant range widens to 96 bytes
  for `PbrDirectLit` Pipelines only (ADR-0067 D-3); the shared Camera
  buffer widens by 16 bytes, to 320 total (ADR-0062 Amendment) — all
  fixed, small, one-time.
- **Portability:** every new Vulkan surface is core 1.0, sized against
  Vulkan's own guaranteed minimum (ADR-0067 D-3/D-7) — no new
  extension, device feature, or platform branch.
- **Other:** zero new third-party dependency; zero change to
  `Atlantis::AssetSystem`'s Core-only link closure; zero change to
  `Atlantis::RHI`'s public API (ADR-0067 D-4 adds no new RHI type,
  field, or method); `Atlantis::Renderer`'s public API gains exactly
  three new accessors on `Material` (D9).

## Real-code evidence (summary — see linked ADRs for full citations)

| Claim | Evidence |
|---|---|
| `MaterialAssetData` has exactly 4 fields today, unchanged since ADR-0059 | `material_types.h:40-45` |
| `Material` supports exactly one optional texture+sampler pair | `renderer/material.h:34-54` |
| `CommandList::pushConstant()` has no offset/stage param; Vulkan impl hardcodes vertex-only | `command_list.h:73`; `vulkan_command_list.cpp:320-324` |
| Existing push constant = 64 bytes; Vulkan guarantees ≥128 bytes across all stages | `material_realization.cpp:176`; ADR-0025 |
| `lit_textured.slang` is pure N·L diffuse, no ambient/specular/tone-mapping | full file read, `shaders/lit_textured/lit_textured.slang` |
| RenderTarget default format is UNORM; no gamma-encode anywhere in this engine | `vulkan_presentation.cpp:107-112`; repo-wide grep |
| Mesh vertex layout has position/color/UV0/normal, no tangent | `mesh_source.h:23-35`; `mesh_artifact.h:25-46` |
| No sphere mesh or procedural mesh generation exists anywhere | repo-wide search |
| Descriptor set layout: binding 0 = uniform (always), binding 1 = ≤1 combined sampler | `vulkan_device.cpp:984-1011` |
| This engine has zero alpha-blending capability (`blendEnable = VK_FALSE`, every Pipeline) | `vulkan_device.cpp:1125` |
| `extractCameraMatrices()` already computes camera world position (`eye`), currently discarded | `scene_extraction.cpp:107` |
| `bindUniformBuffer()` binds with `VK_WHOLE_SIZE` — real mechanism letting Unlit (128B)/Lit (304B) share one buffer today | `vulkan_command_list.cpp:250` |
| Real `slangc` compile of `textured_quad.slang`/`lit_textured.slang` (unmodified) reflects 128/304-byte blocks respectively | this Spec's own review, real reflection JSON |
| Real `slangc`+MSVC probes confirm the candidate push-constant block is 96 bytes, not 88 | this Spec's own review, real reflection JSON + `cl.exe` layout probe |

No unresolvable architectural conflict was found.

## Proposed Design

```
Author .material.txt (kind: pbr_direct_lit, texture: <Rgba8Srgb path>,
                       base_color_factor/metallic_factor/roughness_factor)
  -> cookMaterial() (ADR-0066 items 1-5)
  -> loadMaterialAsset() -> MaterialAssetData
  -> Runtime scene-load: resolve texture, REJECT if not Rgba8Srgb (ADR-0066 item 6)
  -> Runtime per-frame realization: selectShaderPair(PbrDirectLit),
     createMaterial(pushConstantSizeBytes = 96, hasSampledTextureBinding = true)
  -> Renderer::drawFrame()'s existing, unmodified per-DrawItem loop:
     bindPipeline -> bindVertexBuffer -> bindIndexBuffer ->
     bindUniformBuffer (320B Camera+Lighting+CameraWorldPosition,
       ADR-0062 Amendment) -> bindTexture (unchanged) ->
     pushConstant (96B, VERTEX|FRAGMENT visible, ADR-0067 D-4) -> drawIndexed
  -> pbr_direct_lit.slang fragment: BRDF (ADR-0067 D-1/D-2)
```

Every step is either an existing, unmodified mechanism or a narrow
widening of one existing mechanism at exactly one new call site — no
new subsystem, resource type, or binding.

## Architectural Impact

**Yes** — three decisions, kept apart: **ADR-0066** (`Accepted` — Asset
System data-format/validation, no Vulkan content); **ADR-0067**
(`Accepted` — RHI/rendering-math, no Asset System content); **ADR-0062's
own Accepted Amendment** (extends its already-`Accepted` buffer layout
by 16 tail bytes, recorded there because that ADR already owns the
decision — not folded into ADR-0067). Matches this codebase's own "one
ADR, one responsibility" and amendment-over-new-ADR conventions
(ADR-0061/ADR-0062 split; ADR-0041/ADR-0042 amendments).

## Alternatives Considered

- **A second base-color/metallic-roughness texture from the outset.**
  Rejected for this round (Goal 2) — would reopen Spec 0021's
  descriptor-pool-capacity proof; registered as a future candidate.
- **Extending `LitTextured` in place** instead of a new `MaterialKind`.
  Rejected — its own shader/goldens must stay unchanged (Goal 4); a new
  `MaterialKind` is this codebase's established mechanism for a
  genuinely different shading behavior (ADR-0059 Decision 3).

## Decisions for Human Review

Twenty-five items, matching the human-directed drafting brief. Every
recommendation is a proposal for Human Review to accept, amend, or
reject.

| # | Question | Recommendation | Authoritative source |
|---|---|---|---|
| D1 | New `MaterialKind` name/semantics | `PbrDirectLit`, third closed enum value | ADR-0066 item 1 |
| D2 | PBR parameter set | `baseColorFactor` (RGBA), `metallicFactor`, `roughnessFactor` | ADR-0066 item 1 |
| D3 | Range/finiteness, dual validation | `[0,1]`, finite, validated at cook and decode | ADR-0066 item 5 |
| D4 | Numerical stability near `roughness=0` | Shader-internal `alpha` clamp only, never reject authored value | ADR-0067 D-2 |
| D5 | Single texture + scalars vs. multi-texture | Single texture + scalars (Goal 2) — keeps Spec 0021's pool proof intact; see Risks for the escape hatch | Spec Goal 2 |
| D6 | Base-color texture sRGB requirement | Required `Rgba8Srgb`; `PbrDirectLit`-only Runtime validation; existing Unlit/Lit and Spec 0016's own fixture unaffected | ADR-0066 items 6/7 |
| D7 | Per-material parameter transmission | Extended push constants (96 bytes total, 32 of guaranteed 128 remaining) | ADR-0067 D-3 |
| D8 | Exact push-constant layout/stage visibility | See ADR-0067 D-3/D-4 — `objectToWorld` unchanged at offset 0/64; three new fields end at 88; real total 96 (`alignas(16)`, explicit tail pad); every Pipeline's `stageFlags` uniformly widened to `VERTEX\|FRAGMENT` | ADR-0067 D-3/D-4 |
| D9 | `Material`/Renderer/API ownership of PBR params | `Material` gains three new, `const`, borrowed-value fields (mirrors `sampledTexture_`/`sampler_`); `DrawItem` unchanged; `AssetSystem` DTOs never reach `Renderer` directly | this Spec |
| D10 | `DrawItem` shape | Unchanged — three fields suffice (D9) | this Spec |
| D11 | Runtime dispatch/format-change rebuild | `PbrDirectLit` becomes a third arm of `selectShaderPair()` and the existing realize/rebuild mechanism; parameters copied fresh from `MaterialAssetData` every rebuild, never reset | this Spec |
| D12 | Exact BRDF | See ADR-0067 D-1 (GGX/Trowbridge-Reitz, Schlick-GGX with Karis's correctly-cited direct-lighting `k=(roughness+1)²/8`, Schlick Fresnel, energy-conserving diffuse/specular split) | ADR-0067 D-1 |
| D13 | Coordinate/view/light conventions | `N`/`L` conventions unchanged from `lit_textured.slang`; `V` uses camera world position (D-in ADR-0062 Amendment) | ADR-0067 D-1/D-5 |
| D14 | Output color space | No new transform — existing clamp-only contract, with a binding visual-distinguishability condition on the new golden's own human review | ADR-0067 D-6 |
| D15 | Direct-light cap / `FrameLightingData` | Unchanged — 176 bytes, 1 Directional + 4 Point, offsets untouched | ADR-0067 D-1 |
| D16 | Validation mesh | New, hand-authored, non-shared-vertex UV-sphere `.mesh.txt` (offline-generated, never committed as tooling code); reused via 4 scene-node transforms, not 4 mesh files; exact topology/attribute/winding/pole/determinism requirements below | this Spec |
| D17 | PBR validation scene/golden | Asymmetric 4-sphere layout spanning dielectric/metallic × rough/smooth, both light kinds independently visible; golden per ADR-0042's bootstrap process; required negative/mutation tests below | this Spec |
| D18 | Material schema/artifact version bump | Yes — version 1→2 grammar, 56-byte artifact | ADR-0066 items 2/3 |
| D19 | Re-cook strategy for old assets | Every existing `.material.txt` re-cooked (mechanical version-line bump); old 32-byte artifacts rejected outright | ADR-0066 |
| D20 | Scene artifact change | None needed — `Renderable::materialAsset` already references by opaque `AssetId` | ADR-0060 (unmodified) |
| D21 | Descriptor-pool proof still holds | Yes, by construction (Goal 2); re-confirmed against real `vulkan_device.cpp:984-1011`; push-constant fields structurally cannot become a descriptor binding (Slang separates `descriptorTableSlot`/`pushConstantBuffer` kinds) | Spec 0021 D4 |
| D22 | Windows/Android portability | Core Vulkan 1.0 only; Android Candidate Order 1 unaffected | ADR-0067 D-7 |
| D23 | Error domain / C4062 | New `selectShaderPair()` arm; `/w14062`/`/WX` already enforced | ADR-0067 D-7 |
| D24 | Thread/ownership/lifetime | Unchanged — ADR-0060's existing two-phase model, Phase 1 single-threaded baseline | ADR-0067 D-7 |
| D25 | Non-Goals / future-candidate split | See Non-Goals above | this Spec |

### D16 detail — sphere mesh requirements

Non-shared-vertex UV-sphere (Plan-time-fixed segment count, e.g. 16×24
bands); position/color(inert placeholder)/UV0(equirectangular)/normal
(exact analytic unit radial — no tangent); fixed radius/local-origin
center; counter-clockwise winding matching existing assets; poles
duplicated per-longitude-segment, never collapsed. GPU-independent
tests confirm vertex count, non-shared topology, unit normal length,
UV0 range, winding — proving the checked-in asset's real properties,
never assuming the generation script was correct.

### D17 detail — golden discriminating power

Explicit requirement: the layout must let a human reviewer and a real
GPU test confirm dielectric vs. metallic, rough vs. smooth,
`baseColorFactor` reaching the shader, and both light kinds
contributing. Required negative/mutation tests, each confirmed to make
the corresponding test/golden comparison actually fail before being
accepted as coverage: `metallicFactor` forced to 0; `roughnessFactor`
forced constant; camera-position sign flipped; Fresnel metal/dielectric
blend disabled; Point attenuation bypassed. This golden proves this
Spec's own direct-lighting baseline is correctly parameterized and
wired — it is not evidence of, and does not claim, professional-
renderer (e.g. Filament-grade) visual quality; IBL, shadows, and a real
display/output transform remain separate, unimplemented future work
(Non-Goals).

## Testing & Verification Plan

- **Material source/artifact/cook/decode:** fixed-byte round-trip for
  the exact 56-byte artifact (ADR-0066 item 3); version-bump rejection;
  per-parameter range-error tests at both cook and decode time; the new
  `Rgba8Srgb` rejection test.
- **Independent CPU BRDF reference tests** (never sharing production's
  own implementation): dielectric/metallic at low/high roughness;
  Directional at several `N·L` including grazing; Point at/beyond
  `range`; multi-light accumulation; the `roughness=0`/`kMinAlpha` and
  `NdotL≤0` guard paths, confirmed NaN/Inf-free.
- **Shader reflection vs. C++ layout:** real Slang reflection JSON
  cross-checked against the C++ `PbrPushConstants`/camera-buffer structs
  via `static_assert` — never a shared literal trusted from one side
  only.
- **Real GPU parameter transmission:** two otherwise-identical draws
  differing only in one PBR parameter produce different captured pixels.
- **Runtime realization/format-change:** first realization; a rebuild
  mixing `PbrDirectLit` with other kinds, confirming the existing
  all-or-nothing candidate-batch contract holds.
- **Mixed-material scenes:** `UnlitTextured` + `LitTextured` +
  `PbrDirectLit` together in one frame.
- **Spec 0021 regression:** existing N=2/N=6 tests unmodified, plus one
  new case mixing a `PbrDirectLit` material into an N-material
  format-change scenario.
- **Spec 0022 regression:** a `PbrDirectLit` material reflects a runtime
  `Light`/Transform change on the next frame, exactly like `LitTextured`.
- **New PBR golden:** D17's negative/mutation tests; ADR-0042's
  "Initial baseline bootstrap" (Implementation first, clean-commit
  capture, human review, separate PNG/sidecar commit).
- **Existing five goldens:** confirmed byte-for-byte/pixel-for-pixel
  unchanged — required specifically because ADR-0067 D-4's `stageFlags`
  widening is a real, uniform Vulkan object change.
- **Full suite:** Debug/Release builds; `ctest -LE gpu`/`-L gpu` both
  configs, Validation Layers clean; `ATLANTIS_BUILD_TESTS=OFF` build;
  confirmation of the module/link-graph claims above; `git diff --check`.

## Risks & Open Questions

- **D5's single-texture-plus-scalars scope is a real, disclosed bet.**
  If the new golden shows the result unsatisfying without a genuine
  metallic-roughness *texture*, that is a new architectural decision (a
  second descriptor binding, reopening Spec 0021's proof) requiring its
  own Spec/ADR/Human Review — never silently added.
- **D14's output-color-space contract carries a binding golden-review
  condition** (ADR-0067 D-6): if the four corner cases prove
  indistinguishable under the hard-clip contract, Implementation must
  stop for a dedicated Output Transfer Function Spec, not a local patch.
- **The Schlick-GGX vs. height-correlated Smith choice** (ADR-0067
  Alternatives) remains a legitimate, revisitable judgment call, not
  blocking this Spec's own approval.
- **ADR-0062's own Amendment required acceptance alongside this Spec,
  ADR-0066, and ADR-0067** for Plan-drafting to proceed — all four were
  accepted together in this Spec's own Human Review Approval above.

## Out of Scope / Future Work

See Non-Goals above for the full, registered future-candidate list.

## Cross-cutting note (governance, not scope)

Drafted at explicit human direction, ahead of Android Platform
(`specs/README.md` Section B, Candidate Order 1, unaffected) and ahead
of Shadow, IBL, and Post-processing — see `specs/README.md`'s own
updated Section B note.

## Accepted Correction — 2026-08-30 (D9's own push-constant-layout discriminator)

**Status: formally accepted by Human Review on 2026-08-30** — see
"Human Review — Correction Acceptance (2026-08-30)" immediately below.
**Does not change this Spec's own top-level Status (`Approved`) or
rewrite any text above, all of which is preserved verbatim**, matching
this codebase's own established correction discipline (e.g. Spec 0020's
own "Human Review Correction" precedent).

### Human Review — Correction Acceptance (2026-08-30)

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-08-30, in direct response to the implementability
gap [PR #109](https://github.com/slmao/Atlantis/pull/109) (Plan 0023's
own drafting) surfaced in D9's original text. The correction below is
accepted exactly as specified: a new, closed, Renderer-owned
`MaterialPushConstantLayout` enum (`ObjectToWorldOnly`/`PbrDirectLit`),
`Material`-private storage with a `const`-qualified accessor and no
setter (never a `const` data member), Runtime choosing the layout
explicitly at Material-construction time, an exhaustive `switch` with
no `default:` label in `Renderer::drawFrame()`, zero RHI public API
change, `DrawItem` unchanged, no value-based inference, `Material`'s
existing move-construction/move-assignment contract preserved, and the
Renderer public accessor count corrected from three to four. **No ADR
Amendment is required**, per this correction's own reasoning below,
accepted as stated. **This acceptance authorizes, once this PR merges,
resuming and continuing review of Plan 0023 (revised to reference this
corrected mechanism) — it does not itself authorize any
Implementation.** No further Human Review is pending for this
correction.

**What prompted this correction.** D9's own original text says
`Material` gains three new, "`const`", borrowed-value fields and that
`Renderer::drawFrame()` builds either payload "keyed off the bound
`Material`'s own `pipeline().pushConstantSizeBytes()` (or an equivalent
already-available signal)" — language that, during Plan 0023's own
drafting (PR #109), was found to name a mechanism that does not exist
and could not be implemented as written. Confirmed directly against
real, current source:

- `atlantis::rhi::Pipeline` (`src/rhi/include/atlantis/rhi/pipeline.h`)
  is deliberately, explicitly opaque — its own class comment states "no
  accessor beyond the destructor," mirroring `SubmissionSignal`'s own
  identical precedent (Spec 0006) — there is no
  `pushConstantSizeBytes()` accessor, and adding one would reopen
  `Pipeline`'s own already-`Accepted` (ADR-0022) no-accessor contract, a
  real RHI public API change this Spec's own Requirements explicitly
  rule out ("zero change to `Atlantis::RHI`'s public API").
- `Atlantis::Renderer` (`src/renderer/CMakeLists.txt`) links only
  `Atlantis::Core`, `Atlantis::RHI`, `Atlantis::RenderGraph` — never
  `Atlantis::AssetSystem` — confirmed by a comment in that same file
  stating linking `Atlantis::Platform`/`Atlantis::VulkanBackend`/
  `Vulkan::Vulkan` would itself violate Spec 0007's own Acceptance
  Criteria; the identical module-boundary discipline applies to
  `Atlantis::AssetSystem`. `Renderer::drawFrame()` therefore cannot read
  `atlantis::asset_system::MaterialKind` to decide a payload shape.
- `atlantis::renderer::Material`'s own move-assignment operator is
  `= default` (`material.h`) — a `const`-qualified, non-pointer data
  member (e.g. a `const` scalar or `const std::array`) would cause the
  compiler to implicitly delete that operator, breaking every existing
  caller that moves a `Material` today. (This is distinct from
  `Material`'s own existing `const atlantis::rhi::SampledTexture*
  sampledTexture_` field — a pointer *to* `const` data, whose own
  pointer value remains freely reassignable and does not block move-
  assignment; the risk is specifically a `const`-qualified member
  itself, not a pointer that happens to point at `const` data.)

Given these three facts, `Renderer::drawFrame()` has no way to query
`Pipeline` and no way to read `MaterialKind` — the choice of push-
constant payload shape must be a signal `Material` itself carries,
decided once, explicitly, by Runtime, at construction time.

**Corrected D9, replacing D9's own "keyed off ... `pipeline().push
ConstantSizeBytes()`" language and its "`const` ... fields" wording —
the rest of D9 (three PBR parameter values live on `Material`;
`DrawItem` unchanged; `AssetSystem` DTOs never reach `Renderer`
directly) is unchanged and reaffirmed:**

A new, closed, `Atlantis::Renderer`-owned enum,
`MaterialPushConstantLayout { ObjectToWorldOnly, PbrDirectLit }`, is added
alongside `Material`. `Material` stores it as a plain (non-`const`)
private value, set exactly once in the constructor and never reassigned
afterward — "immutable" by encapsulation (a private value plus a
`const`-qualified accessor returning it by value, with no setter),
never by a `const` data member, exactly matching the reasoning above.
Runtime's own `createMaterial()` call sites (one per `MaterialKind`,
already the single place that already knows which kind is being
realized) pass the correct enum value explicitly — `ObjectToWorldOnly`
for `UnlitTextured`/`LitTextured`, `PbrDirectLit` for `PbrDirectLit`
— never inferred from `baseColorFactor`/`metallicFactor`/
`roughnessFactor`'s own values (which cannot reliably distinguish the
two: ADR-0066 item 1's own defaults give every non-PBR `Material` the
identical inert `baseColorFactor=(1,1,1,1)`/`metallic=1`/`roughness=1`
values a real `PbrDirectLit` material could equally have authored).
`Renderer::drawFrame()`'s existing per-`DrawItem` loop switches
exhaustively on `item.material->pushConstantLayout()` — a closed
`switch` with no `default:` label, exercising this repository's own
`/w14062`/`/WX` C4062 discipline exactly as `selectShaderPair()`'s own
`MaterialKind` switch already does (ADR-0067 D-7) — building the
existing 64-byte `objectToWorld`-only payload for `ObjectToWorldOnly` and
the 96-byte payload (ADR-0067 D-3's own exact layout) for
`PbrDirectLit`, before calling the existing, unmodified
`CommandList::pushConstant(const void*, std::size_t)`.

```cpp
// atlantis::renderer, material.h — additive to the existing class.
enum class MaterialPushConstantLayout { ObjectToWorldOnly, PbrDirectLit };

class Material {
 public:
  explicit Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline,
                     MaterialPushConstantLayout pushConstantLayout,
                     const atlantis::rhi::SampledTexture* sampledTexture = nullptr,
                     const atlantis::rhi::Sampler* sampler = nullptr,
                     std::array<float, 4> baseColorFactor = {1.0f, 1.0f, 1.0f, 1.0f},
                     float metallicFactor = 1.0f, float roughnessFactor = 1.0f) noexcept;
  // ~Material(), copy/move members unchanged from today.
  [[nodiscard]] MaterialPushConstantLayout pushConstantLayout() const noexcept { return pushConstantLayout_; }
  [[nodiscard]] const std::array<float, 4>& baseColorFactor() const noexcept { return baseColorFactor_; }
  [[nodiscard]] float metallicFactor() const noexcept { return metallicFactor_; }
  [[nodiscard]] float roughnessFactor() const noexcept { return roughnessFactor_; }
  // pipeline()/sampledTexture()/sampler() unchanged from today.

 private:
  // pipeline_/sampledTexture_/sampler_ unchanged from today.
  MaterialPushConstantLayout pushConstantLayout_;         // NOT const -- see reasoning above
  std::array<float, 4> baseColorFactor_{1.0f, 1.0f, 1.0f, 1.0f};  // NOT const
  float metallicFactor_ = 1.0f;                    // NOT const
  float roughnessFactor_ = 1.0f;                   // NOT const
};
```

**Alternatives considered and rejected, real evidence for each:**

- **`std::optional<PbrMaterialParameters>`, presence as the
  discriminator.** Rejected: conflates "does this Material have PBR
  parameter *values*" with "which push-constant *size* this Pipeline
  expects" — two questions this Spec's own Goal 2/D5 already treats as
  independent (a future kind could want scalar parameters without
  wanting a 96-byte layout). Does not extend to a third future layout
  without a second, independent optional, and Renderer would then test
  presence via an `if`-chain, not a single exhaustive `switch` — no
  compiler-enforced (C4062) coverage the way a closed enum gives today.
- **A bare `Material::isPbrDirectLit()` boolean/mode accessor.**
  Rejected: the same reasoning as above, one level blunter — a bool
  does not scale to a third layout without accreting further,
  non-exhaustive booleans; an enum with a `switch` is the smallest
  change that stays C4062-protected as this Spec's own Non-Goals list
  of future candidates (a second texture, tone mapping, etc.) is
  eventually drawn on.
- **Extending the opaque RHI `Pipeline` accessor.** Rejected — see the
  three confirmed facts above; this is a real RHI public API change
  this Spec's own Requirements already rule out, and would require its
  own ADR revisiting ADR-0022's own no-accessor contract, disproportionate
  to a Renderer-internal bookkeeping need.
- **Modifying `DrawItem`.** Rejected: directly reopens D10's own
  already-`Approved` "unchanged" decision with no new evidence — the
  three confirmed facts above are about *how Renderer decides*, not
  about needing new per-draw data `DrawItem` doesn't already carry.
- **Uniformly pushing 96 bytes for every Pipeline.** Rejected, and not
  merely undesirable: `UnlitTextured`/`LitTextured`'s own
  `VkPushConstantRange::size` stays exactly `64` (ADR-0067 D-3/D-8) —
  calling `vkCmdPushConstants` with `size = 96` against a Pipeline whose
  own declared range is `64` is a real Vulkan Validation Layers
  violation, not a stylistic mismatch.
- **Inferring the layout from `baseColorFactor`/`metallicFactor`/
  `roughnessFactor`'s own values.** Rejected — see the defaults
  collision named above: a real, concrete counter-example, not a
  theoretical concern.

**Consequences of this correction, stated precisely:**

- The Requirements section's own "Other" bullet above ("`Material` gains
  exactly three new accessors") is superseded by this correction — the
  real count is **four** (`pushConstantLayout()` plus the three PBR
  parameter accessors). The original bullet's text is left unedited
  above; this correction is the authoritative count going forward.
- **No ADR Amendment is needed.** This mechanism is a
  Renderer/`Material`-internal implementation detail — no new RHI type,
  field, or public method (`CommandList::pushConstant()`'s own
  signature is untouched); no new dependency; no new Vulkan object or
  `VkPushConstantRange` value beyond what ADR-0067 D-3 already defines.
  It does not cross this repository's own "new public API/module
  boundary/dependency" bar (`AGENTS.md`) any more than `Material`
  gaining `sampledTexture_`/`sampler_` in Spec 0016 did without its own
  dedicated ADR. ADR-0067 D-7's own "Error domain" bullet (the
  `selectShaderPair()` C4062 discipline) is extended in kind, not
  contradicted, by this second, Renderer-side closed switch.
- **Plan 0023 (PR #109) itself is not corrected by this section** — its
  own Milestone 5 currently repeats D9's own now-superseded
  `pipeline().pushConstantSizeBytes()` language. Updating it is a
  separate, later action once this correction is itself accepted, not
  performed as part of this correction.
- Goal 4, D5, D10, D21, and every other Decision this correction does
  not name are unaffected and unreopened — `DrawItem` stays unchanged
  (D10 reaffirmed), the RHI public API stays unchanged, and the
  single-texture/no-new-descriptor-binding design (D5/D21) is untouched.
