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
