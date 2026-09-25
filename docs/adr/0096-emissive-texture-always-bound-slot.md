# ADR 0096: Emissive Texture — One Always-Bound Slot in the PBR Pipelines

- **Status:** Accepted
- **Date:** 2026-09-25
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-09-25 (chat confirmation, ruling Q4 "add";
  reviewed in this branch's own PR, alongside Spec 0046's Approval)
- **Related Spec:** [Spec 0046: Bistro Finale](../specs/0046-bistro-finale.md) (`Approved`)
- **Related ADR(s):** extends [ADR-0089](0089-emissive-material-parameter-range-composition-and-push-constant-placement.md)
  (the emissive factor, its range and its composition point — unchanged);
  widens the Vulkan Backend limits ADR-0072 D-7 and its 2026-09-06
  amendment set, the same way they were widened for the shadow map and the
  normal map.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- ADR-0089 gave PBR materials `emissiveFactor` (linear RGB, `[0, 65504]`),
  added after all lighting and before the output transform. Spec 0041 kept
  it factor-only and deferred the texture to ⑦ (ruling O3); the importer
  drops the factor of the 11 Bistro materials that also carry an
  `emissiveTexture`.
- Spec 0046 measured the need: those 11 materials cover 55 of Bistro's 118
  emissive instances, including the 28 streetlight bulbs (factor 100), the
  lanterns, the wall and ceiling lamps and four shop signs. Their emissive
  images are separate masks (`*_emi.dds`, `*_emt.dds`); only one reuses its
  base-colour image, so the existing base-colour binding cannot stand in.
- Spec 0041 named the two ways to add a texture: a variant dimension
  (10 → 20 PBR shader pairs, as the normal map doubled them) or one slot
  bound for every PBR material, with a default texture when a material has
  none.
- The ten PBR variants use sampler bindings 1–5 at most (`pbr_ibl_normal_map`:
  base colour, environment, DFG, shadow map, normal map). The Vulkan Backend
  sizes its per-set bind memo at 6 slots (`vulkan_command_list.h:207`) and
  its descriptor pools at 5 combined image samplers per set
  (`vulkan_device.cpp:463-470`).

## Decision

1. **One always-bound emissive slot** in all ten PBR shader variants (the
   four PBR kinds × their IBL / normal-map arms), at each variant's next
   free sampler binding. The shader adds `emissiveFactor × sample(emissive
   texture, uv)` where it adds `emissiveFactor` today (ADR-0089's
   composition point, unchanged). No new shader variant.
2. **Default texture.** A material with no emissive texture binds one
   shared 1×1 opaque white `Rgba8Unorm` texture that the Runtime (and each
   fixture that realizes PBR materials) creates once. Its texel samples as
   exactly 1.0, so `factor × 1.0` reproduces every existing material —
   every existing golden stays byte-identical.
3. **Material schema v9.** One optional `emissive_texture: <logical path>`
   line (the normal map's precedent), valid on the four PBR kinds only,
   resolved through the scene dependency manifest like every other texture;
   v8 is not read (no dual-version reader). The emissive texture shares the
   material's one sampler; Spec 0045's derived `maxLod` includes it.
4. **Importer.** The 11 textured emissive materials map their factor and
   their `emissiveTexture` (colour space from the DDS, Spec 0037 Ruling 4);
   a texture with a zero factor stays inert, as glTF defines.
5. **Vulkan Backend limits.** The bind memo widens from 6 to 7 slots and
   the pool budget from 5 to 6 combined image samplers per set —
   backend-internal constants, as ADR-0072 D-7 widened them before. No RHI
   public type or signature changes.

## Consequences

### Positive

- Bistro's bulbs, lanterns, lamps and signs glow in their authored shapes.
- One code path: every PBR material samples the slot; no variant explosion.
- Existing materials and goldens are untouched (Decision 2).

### Negative / Trade-offs

- Every PBR draw binds and samples one more texture, even for the default.
- A material schema bump (v9) re-cooks every material.
- Ten shaders, their descriptor contracts and binding-count arms change
  together, and the backend limits grow by one slot.

## Alternatives Considered

- **A variant dimension** (`hasEmissiveMap`, 10 → 20 shader pairs). No cost
  for materials without the texture, but doubles pipelines, contracts and
  tests. Rejected.
- **Emissive = factor × base-colour texture.** No new binding, but wrong for
  10 of the 11 materials, whose masks are separate images. Rejected.
- **Factor-only for the textured 11.** No format change, but signs become
  solid glowing plates. Rejected by ruling Q4.
- **Keep dropping them** and imitate with point lights. Lights illuminate
  other surfaces; they cannot make a bulb look lit. Rejected.
