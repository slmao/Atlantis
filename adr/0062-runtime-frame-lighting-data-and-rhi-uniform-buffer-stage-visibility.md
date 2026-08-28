# ADR 0062: Runtime Frame Lighting Data, RHI Uniform Buffer Stage Visibility, and Lighting Math Conventions

- **Status:** Proposed
- **Date:** 2026-08-29
- **Deciders:** slmao
- **Related Spec:** [specs/0019-lighting-foundation.md](../specs/0019-lighting-foundation.md)

## Context

Spec 0019's `LitTextured` Material needs a per-frame array of active
lights available to its own fragment shader, and a defined convention
for how that data is transformed and combined into a final pixel color.
Confirmed directly against real, current source (Spec 0019's own
Pre-draft verification, not repeated here in full): `RuntimeApplication`
already creates one host-visible, persistently-mapped uniform `Buffer`
for the camera's own view/projection matrices and overwrites it,
unconditionally, every frame via a direct `memcpy`-shaped write into
`mappedData()` — no RHI "update" API exists or is needed for this.
`PipelineCreateParams` describes exactly one uniform-buffer descriptor
binding (always present) and one optional combined-image-sampler
binding, with no mechanism for a second uniform buffer binding; the
existing uniform binding's own Vulkan `stageFlags`, set inside
`Device::createPipeline()`, is hardcoded to `VK_SHADER_STAGE_VERTEX_BIT`
only — a fragment shader cannot read it today. `World` has no
matrix-inverse function anywhere in its own public API. `Rgba8Srgb`
sampling already performs real, hardware sRGB→linear decode at sample
time; no tone-mapping or HDR intermediate target exists anywhere in
this engine.

## Decision

1. **The per-frame light array is packed into the existing camera
   uniform `Buffer`**, appended after the existing view/projection
   floats, in a fixed, `std140`-compatible layout, capped at a small,
   fixed maximum count (1 Directional + 4 Point, Plan-time-adjustable).
   No new `Buffer`, no new descriptor binding, no `PipelineCreateParams`
   shape change. It is written unconditionally, every frame, via the
   same direct `memcpy`-into-`mappedData()` pattern the camera data
   already uses — no dirty-tracking, no new synchronization concern.
   Over-limit lights are deterministically truncated (first N in
   `World::lightEntities()`'s own ascending-slot-index order), logged
   once at scene-load time, never a per-frame cost and never silently
   unlogged.

2. **The existing uniform-buffer descriptor binding's own Vulkan
   `stageFlags` widens, unconditionally, from `VK_SHADER_STAGE_VERTEX_BIT`
   to `VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT`, for
   every `Pipeline` this engine creates** — the one real RHI-internal
   change this Spec requires. `PipelineCreateParams`'s own public shape
   is unchanged; every existing shader (`minimal_mesh`, `textured_quad`)
   simply continues not referencing this binding from its own fragment
   stage, unaffected.

3. **Point-light attenuation is `clamp(1.0 - distance / range, 0.0,
   1.0)`**, `distance` floored at a small positive epsilon before
   division — an explicitly, permanently (for Phase 1) non-physical,
   unitless linear falloff, not inverse-square, not any physically
   calibrated unit system.

4. **A vertex normal transforms by the object-to-world matrix's own
   upper-left 3×3 submatrix directly, not an inverse-transpose normal
   matrix.** Correct under pure rotation and uniform scale; a
   `LitTextured`-bound entity whose current `Transform.localScale` is
   non-uniform is detected (a per-entity, per-frame check, cheap) and
   skipped for that frame, logged — never silently rendered with an
   incorrect normal passed off as correct.

5. **No tone-mapping, gamma-encode, or HDR intermediate target is
   added.** Lighting math operates on already-linear sampled texture
   values (`Rgba8Srgb`'s own existing hardware decode) and the
   authored, linear-by-convention light `color`/`intensity`; the summed
   result is written directly to the existing `Rgba8Unorm`/`Rgba8Srgb`
   color attachment, clipping above 1.0 with no rolloff.

## Consequences

### Positive

- Zero new GPU resource, zero new descriptor binding, zero new
  `Device`/`Renderer` public API surface — the smallest RHI-adjacent
  change this Spec's own lighting capability can be built on.
- The one real RHI change (item 2) is a single, disclosed, unconditional
  widening with no behavioral effect on any existing shader — legal,
  zero-cost Vulkan, not a speculative or partial fix.
- Every convention this ADR fixes (attenuation formula, normal
  transform, color-space handling) is stated explicitly, with its own
  real limitation disclosed, rather than left as an implicit assumption
  Implementation would otherwise have to invent unreviewed.

### Negative / Trade-offs

- Widening the uniform binding's own stage visibility for *every*
  `Pipeline`, not only `LitTextured` ones, is a small, permanent change
  to already-shipped, `Approved` RHI/Vulkan Backend behavior — accepted
  here as strictly additive (broader visibility than any existing
  shader uses, never narrower), not reopening any existing shader's own
  correctness.
- The linear-falloff Point-light attenuation and the non-uniform-scale
  detect-and-skip normal-transform limitation are both real,
  Phase-1-only simplifications, not physically or generally correct —
  each is named explicitly as deferred, real future work (real
  inverse-square attenuation; a real normal-matrix inverse), not
  claimed as a permanent design ceiling.
- No tone-mapping means a scene author can author lights whose combined
  contribution clips unpleasantly; accepted as an explicit, disclosed
  Phase 1 limitation, matching this engine's own existing "no HDR
  target, no post-processing" boundary exactly.

## Alternatives Considered

- **A second, independent uniform buffer binding for lighting data.**
  Rejected as the default: would require widening `PipelineCreateParams`
  with a new field, extending `Device::createPipeline()`'s own
  descriptor-set-layout/pool-size construction to a three-binding case,
  and a new Shader-System expected-contract shape — a real, larger RHI
  change, when reusing the existing single binding (this ADR's own
  Decision 1) is structurally sufficient given both camera and lighting
  data are written by the identical composition root at the identical
  point in `runFrame()`.
- **Physically-based inverse-square Point-light attenuation.** Rejected
  for this round: has no natural, finite cutoff, in direct tension with
  Spec 0019's own authored, testable `range` field; named explicitly as
  future PBR-adjacent work.
- **A real inverse-transpose normal matrix, computed via a new
  Core/World 3×3-inverse function.** Rejected as this round's own
  default: introduces new math capability this codebase does not yet
  have anywhere, for a correctness case (non-uniform scale on a lit
  entity) this round's own verification scene does not require: the
  detect-and-skip alternative (this ADR's own Decision 4) is smaller
  and non-silent. Human Review may prefer the inverse-transpose
  approach instead if non-uniform scale on lit entities is expected to
  be common — a real, disclosed, open alternative, not foreclosed by
  this ADR.
