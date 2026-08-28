# ADR 0062: Runtime Frame Lighting Data, Its One-Time Capture Contract, RHI Uniform Buffer Stage Visibility, and Lighting Math Conventions

- **Status:** Accepted
- **Date:** 2026-08-29
- **Deciders:** slmao
- **Related Spec:** [specs/0019-lighting-foundation.md](../specs/0019-lighting-foundation.md)

## Context

Spec 0019's `LitTextured` Material needs an array of active lights
available to its own fragment shader, and a defined convention for how
that data is transformed and combined into a final pixel color.
Confirmed directly against real, current source (Spec 0019's own
Pre-draft verification and this ADR's own final review round, not
repeated here in full): `RuntimeApplication` already creates one
host-visible, persistently-mapped uniform `Buffer` for the camera's own
view/projection matrices; `Buffer::mappedData()`'s own real, current
contract confirms this memory is "mapped once, at construction — never
remapped," writable "at any time," with "no explicit flush/invalidate
call required" — confirming directly that both a one-time write and a
per-frame write are already fully supported by the existing RHI
contract, with zero new API either way; the choice between them is a
pure Runtime-side design decision. `PipelineCreateParams` describes
exactly one uniform-buffer descriptor binding (always present) and one
optional combined-image-sampler binding, with no mechanism for a second
uniform buffer binding; the existing uniform binding's own Vulkan
`stageFlags`, set inside `Device::createPipeline()`, is hardcoded to
`VK_SHADER_STAGE_VERTEX_BIT` only — a fragment shader cannot read it
today. `World` has no matrix-inverse function anywhere in its own
public API. `Rgba8Srgb` sampling already performs real, hardware
sRGB→linear decode at sample time; no tone-mapping or HDR intermediate
target exists anywhere in this engine. The existing `Varying` struct
every built-in shader pair uses today carries only clip-space position
and UV — no world-space position or world-space normal is passed from
vertex to fragment stage anywhere in this codebase yet.

## Decision

1. **The active-light array is captured exactly once per scene load —
   a static snapshot, not a per-frame-updated value.** `RuntimeApplication`
   computes it on the first successful frame (immediately after
   `World::updateTransforms()` first runs, alongside Phase 2 material
   realization) and writes it once into the tail bytes of the existing
   camera uniform `Buffer`, appended after the existing 32 camera
   floats, in a fixed, `std140`-compatible layout:

   ```cpp
   struct FrameLightingData {
     std::uint32_t directionalLightCount = 0;  // 0 or 1
     std::uint32_t pointLightCount = 0;        // 0..4
     struct DirectionalLightGpu { float direction[3]; float _pad0; float color[3]; float intensity; } directionalLights[1];
     struct PointLightGpu { float position[3]; float range; float color[3]; float intensity; } pointLights[4];
   };
   ```

   It is **never rewritten again** for that `RuntimeApplication`
   instance's own lifetime, guarded by a boolean flag. A subsequent
   `World::setLight()` call changes `World`'s own state but is **not**
   reflected in any rendered frame — reloading the scene, or restarting
   Runtime, is required. This is a real, deliberate, disclosed Phase 1
   boundary (reversing this ADR's own first-draft "rewrite every frame"
   design, found during this ADR's own final review round to have never
   honestly committed to either answer) — not an RHI limitation; RHI
   already supports either model trivially.

   The camera view/projection portion of the *same* buffer is entirely
   unaffected by this decision — it continues its own existing,
   unrelated per-frame rewrite, including its own existing resize/
   aspect-ratio recomputation, exactly as it already does today; this
   ADR neither touches nor depends on that behavior.

   No new `Buffer`, no new descriptor binding, no `PipelineCreateParams`
   shape change. `Material` (of either kind) holds no reference to this
   buffer — the binding is a function of the `Pipeline`'s own
   descriptor set, bound generically by `Renderer::drawFrame()`'s own
   existing, unmodified logic, introducing no new borrow relationship
   anywhere in the `Material`/`SampledTexture`/`Sampler` ownership graph
   Spec 0016/0018 already established. A color-format change never
   touches this data (format-independent, matching the camera data
   beside it) and never requires recapture; Spec 0018's own
   submit-safe old-`Pipeline` rebuild contract is otherwise completely
   unchanged.

2. **The existing uniform-buffer descriptor binding's own Vulkan
   `stageFlags` widens, unconditionally, from `VK_SHADER_STAGE_VERTEX_BIT`
   to `VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT`, for
   every `Pipeline` this engine creates** — the one real RHI-internal
   change this Spec requires. `PipelineCreateParams`'s own public shape
   is unchanged; every existing shader (`minimal_mesh`, `textured_quad`)
   simply continues not referencing this binding from its own fragment
   stage, unaffected, and must continue passing its own existing tests
   unchanged. The `lit_textured` shader pair's own expected descriptor
   contract (`litTexturedExpectedDescriptorContract()`,
   `descriptor_contract.h`) reflects this binding twice — once per
   stage's own separately-loaded reflection JSON — plus the unchanged
   combined-image-sampler binding, three entries total.

3. **The `lit_textured` vertex shader gains two new varyings, computed
   from the already-existing `objectToWorld` push constant — no new
   push-constant range:** `worldPosition` (`objectToWorld · position`)
   and `worldNormal` (`(float3x3)objectToWorld · normal`, per Decision 4
   below), both consumed by the fragment shader.

4. **A vertex normal transforms by the object-to-world matrix's own
   upper-left 3×3 submatrix directly, not an inverse-transpose normal
   matrix.** This is mathematically correct exactly when that 3×3
   submatrix is a conformal (angle-preserving) linear map — equivalently,
   its three columns are mutually orthogonal and equal in length,
   satisfied by uniform scale of either sign and by rotation/reflection,
   violated by non-uniform scale and shear. Checked on the fully
   composed world matrix (not an entity's own local
   `Transform.localScale`, since a parent's own non-uniform scale can
   introduce shear a child's own local transform never had) — a cheap,
   per-entity, per-frame check (three column lengths, three pairwise dot
   products, no matrix inverse). A `LitTextured`-bound entity whose
   current world matrix fails this check is skipped for that frame's
   own `DrawItem` list, logged once
   (`SceneExtractionError::NonConformalNormalTransform`) — never
   silently rendered with an incorrect normal.

5. **The full lighting algorithm, exact, no free parameters left to
   Implementation's own judgment:**

   ```
   N = normalize(worldNormal)
   accumulated = float3(0, 0, 0)                       // no ambient term
   for each active Directional light i:
     L = -directionalLights[i].direction
     accumulated += directionalLights[i].color * directionalLights[i].intensity * max(dot(N, L), 0)
   for each active Point light j:
     toLight = pointLights[j].position - worldPosition
     dist = max(length(toLight), 1e-4)                 // kPointLightDistanceEpsilon, named
     L = toLight / dist
     atten = clamp(1 - dist / pointLights[j].range, 0, 1)  // explicitly non-physical, linear
     accumulated += pointLights[j].color * pointLights[j].intensity * max(dot(N, L), 0) * atten
   finalRgb = clamp(texColor.rgb * accumulated, 0, 1)   // the one and only clamp
   return float4(finalRgb, texColor.a)
   ```

   `color` components are each authored in `[0, 1]`; `intensity` is
   authored finite and `>= 0`, with no upper bound enforced at authoring
   time (the final clamp bounds the visible effect). No tone-mapping,
   gamma-encode, or HDR intermediate target is added — the final clamp
   above is the only transformation applied before the display format's
   own write.

## Consequences

### Positive

- The one-time capture contract is a real, explicit, testable
  commitment — not an unstated assumption — closing a real honesty gap
  this ADR's own first draft left open.
- Zero new GPU resource, zero new descriptor binding, zero new
  `Device`/`Renderer` public API surface.
- The one real RHI change (Decision 2) is a single, disclosed,
  unconditional widening with no behavioral effect on any existing
  shader.
- Every lighting-math convention is a literal, complete formula, not a
  named-but-undefined technique — directly unit-testable against
  hand-computed values.

### Negative / Trade-offs

- The static-snapshot model (Decision 1) is a real, disclosed Phase 1
  limitation, not a permanent design ceiling — a scene author cannot
  see a runtime `Light` change reflected without a reload/restart; named
  explicitly as future "Dynamic Frame Uniform Updates" work.
- Widening the uniform binding's own stage visibility for *every*
  `Pipeline`, not only `lit_textured` ones, is a small, permanent change
  to already-shipped, `Approved` RHI/Vulkan Backend behavior — accepted
  as strictly additive, never narrowing any existing shader's own
  correctness.
- The linear-falloff Point-light attenuation and the non-conformal-
  transform detect-and-skip normal-transform limitation are both real,
  Phase-1-only simplifications, named explicitly as deferred, real
  future work (real inverse-square attenuation; a real normal-matrix
  inverse), not claimed as a permanent design ceiling.
- No ambient term means an unlit-facing surface renders pure black — an
  explicit, disclosed choice, not an oversight; no tone-mapping means a
  scene author can author lights whose combined contribution clips
  unpleasantly — an explicit, disclosed Phase 1 limitation.

## Alternatives Considered

- **Per-frame re-derivation and rewrite of the frame lighting data,
  matching the camera data's own existing pattern.** This ADR's own
  first draft. Rejected during this ADR's own final review round: not
  unsafe (RHI already supports either model), but never honestly
  committed to whether runtime light mutation would be reflected, and a
  static, one-time, explicitly-tested boundary is the smaller, more
  honest commitment for this round's own minimal scope.
- **A second, independent uniform buffer binding for lighting data.**
  Rejected as the default: would require widening `PipelineCreateParams`
  with a new field and extending `Device::createPipeline()`'s own
  descriptor-set-layout/pool-size construction to a three-binding case
  — a real, larger RHI change, when reusing the existing single binding
  is structurally sufficient.
- **Physically-based inverse-square Point-light attenuation.** Rejected
  for this round: has no natural, finite cutoff, in direct tension with
  Spec 0019's own authored, testable `range` field.
- **A real inverse-transpose normal matrix, computed via a new
  Core/World 3×3-inverse function.** Rejected as this round's own
  default: introduces new math capability this codebase does not yet
  have anywhere, for a correctness case this round's own verification
  scene does not require; the detect-and-skip alternative (Decision 4)
  is smaller and non-silent. Human Review may prefer the
  inverse-transpose approach instead if non-conformal transforms on lit
  entities are expected to be common — a real, disclosed, open
  alternative, not foreclosed by this ADR.
