# ADR 0089: Emissive Material Parameter — Range, Composition Point and Push-Constant Placement

- **Status:** Proposed
- **Date:** 2026-09-22
- **Deciders:** slmao
- **Related Spec:** [Spec 0041](../specs/0041-emissive-materials.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

[Spec 0036](../specs/0036-bistro-parity-roadmap.md) workflow ③ asks for
self-lit surfaces, and predicted "likely no ADR" (`0036:765`), to be confirmed
by the workflow's own Spec. Spec 0041's investigation found most of the change
to be a straight application of existing precedent, and two parts that are
not.

**The precedent part.** [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
fixed the material parameter set and its linear-space convention;
[ADR-0081](0081-pbr-material-brdf-extension-clearcoat-sheen-anisotropy.md)
established how a new material field travels: a `MaterialAssetData` field, a
schema bump with no dual-version reader, and per-kind push-constant structs
grown and confirmed by real Slang reflection plus an MSVC `static_assert`
(ADR-0067 D-3). An `emissiveFactor` follows that path unchanged.

**What is not precedent.**

1. **Range.** Every material factor to date is linear-space `[0, 1]`
   (`isValidFactor()`, `src/asset_system/src/cook_material.cpp:62`). Bistro's
   emissive factors, measured from the asset itself, reach **100**: string
   lights at 8–20, bulbs at 30–100, signage at 0.05–1. The asset uses no
   `KHR_materials_emissive_strength`; it simply writes HDR values into a field
   glTF core clamps to `[0, 1]`. The engine's HDR target is `Rgba16Float`
   (`src/rhi/include/atlantis/rhi/types.h:45`), whose largest finite value is
   65,504.
2. **Composition point.** Where the term enters the frame determines what
   workflow ⑥'s bloom bright-pass sees and whether a surface in shadow still
   glows. All ten PBR fragment shaders already end in the identical
   `return float4(accumulated, alphaOut);`, where `accumulated` is the full
   scene-referred linear HDR lighting result and exposure and tonemapping
   happen later, in the output-transform pass.
3. **Budget.** Measured with real `slangc -reflection-json` and a real MSVC
   probe, appending a `float3` to each of the four PBR push-constant structs
   gives:

   | Kind | Offset | Last byte | Block / `sizeof` | Left of 128 |
   |---|---|---|---|---|
   | `PbrDirectLit` | 96 | 108 | 112 | 16 |
   | `PbrClearcoat` | 96 | 108 | 112 | 16 |
   | `PbrAnisotropic` | 96 | 108 | 112 | 16 |
   | `PbrSheen` | 112 | 124 | **128** | **0** |

   Slang and MSVC agree on every value. The declared push-constant range is
   the Slang block size (`slang_json_transform.cpp:262-274`), so the range and
   the C++ `sizeof` match. Sheen fits within Vulkan's guaranteed 128-byte
   `maxPushConstantsSize` exactly, and has no vector room left.

## Decision

1. **Parameter.** Materials carry `emissiveFactor`: linear-space RGB, default
   `(0, 0, 0)`, one value per material, **no emissive texture** in this
   decision. It is honoured by the four PBR kinds, and rejected at parse time
   on `LitTextured` and `UnlitTextured`.
2. **Range — a deliberate departure from ADR-0066's factor convention.**
   Each component must be finite and within `[0, 65504]`, enforced at cook
   time with its own named error. The colour and its intensity live in one
   unbounded vector; there is no separate strength scalar. The lower bound
   keeps the term purely additive. The upper bound is the `Rgba16Float`
   finite maximum, so no material can write `inf` into the HDR target.
3. **Composition point.** The shader output is
   `float4(accumulated + emissiveFactor, alphaOut)`: added once, after every
   lighting term — direct, shadowed, image-based — and before the output
   transform. It is not multiplied by base colour, N·L, attenuation, shadow or
   the environment. It *is* subject to exposure and tonemapping, like every
   other scene-referred value. Alpha is unchanged. Workflow ⑥ may rely on
   this: emissive energy arrives in the HDR target at full strength, whatever
   the lighting.
4. **Placement.** Each PBR push-constant struct appends
   `float emissiveFactor[3]` after its existing fields, at the offsets in the
   table above. Existing fields do not move. Each layout is confirmed by Slang
   reflection and an MSVC `static_assert`, per ADR-0067 D-3.
5. **Formats.** The material source grammar (6 → 7), artifact schema (6 → 7,
   96 → 108 bytes, field at offset 96) and metadata (5 → 6) bump together, with
   no dual-version reader, per ADR-0066.

This ADR extends ADR-0066 (range, for one named field only) and ADR-0081
(placement), and supersedes neither: every existing factor keeps `[0, 1]`, and
every existing push-constant field keeps its offset.

## Consequences

### Positive

- One field, one line per shader. Every existing material renders identically
  (`x + 0.0` is exact), so all goldens are a strong regression signal.
- Bistro's string lights and bulbs, 10 of its 21 emissive materials, become
  expressible exactly, including their HDR energy.
- Workflow ⑥ has a stated contract for its input rather than a behaviour to
  infer.
- Light-independence is structural: no light, shadow or environment code path
  can touch the term.

### Negative / Trade-offs

- **`PbrSheen` has exhausted its push-constant budget.** It sits at exactly
  128 bytes. Twelve bytes of *scalar* room remain (an 8-byte pad at 88 and 4
  bytes at 124), but the next *vector* parameter on Sheen forces ADR-0081's
  deferred per-material uniform buffer. That decision is now the next step on
  that path, not a distant option.
- **Two range conventions now coexist.** Importers and tooling must know that
  `emissiveFactor` is unbounded while every other factor is `[0, 1]`. The cost
  is one named validator and this ADR.
- **The 11 textured Bistro emissive materials are not expressible.** Adding
  their factor alone would make whole signs and lanterns glow uniformly, so
  they stay dark until an emissive texture is decided.
- **The units are unspecified.** The arithmetic is fixed; the photometric
  meaning is not. That belongs with the photometric Spec to which Spec 0040
  ruling O5 reassigned light intensity.

## Alternatives Considered

- **Clamp to, or validate at, `[0, 1]`** — ADR-0066 convention and glTF core.
  Rejects or dims every Bistro light fixture by up to 100×. Rejected on the
  measured asset.
- **`[0, 1]` colour plus a strength scalar** (`KHR_materials_emissive_strength`
  shape). Would keep the convention and would even fit (4 bytes; Sheen's spare
  4 at 124). Rejected: two sources of truth for one quantity, a further field
  and a further multiply, and nothing expressed that one unbounded vector does
  not.
- **Finite with no upper bound.** Lets a material write `inf` into the HDR
  target, which the bloom bright-pass will then read. Rejected.
- **Emissive before shadowing, or scaled by base colour.** Contrary to glTF's
  model and to what "self-lit" means; a bulb in shadow still glows. Rejected.
- **Reorder `PbrSheen` so emissive sits at offset 96 in every kind.** Gains no
  bytes, moves the proven `sheenColor` layout, and serves only cosmetic
  uniformity. Rejected.
- **A per-material uniform buffer now.** Everything fits without it. Recorded
  above as the forced next step for Sheen, not taken ahead of need.
- **Emissive texture now.** Measured in Spec 0041 Investigation 4: it roughly
  doubles the surface (20 shader pairs, or an always-bound slot with a default
  texture plus every binding-count arm) for 11 materials whose right treatment
  workflow ⑦ can judge from the assembled scene. Deferred to that point.
- **Emissive on `LitTextured`.** That path has never carried a per-material
  factor (its push constants are `objectToWorld` only); adding one would be a
  new layout for a legacy kind used by one asset. Rejected.
