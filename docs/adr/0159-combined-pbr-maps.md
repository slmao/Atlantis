# ADR 0159: Independent Occlusion and Metallic–Roughness Bindings in PBR IBL

- **Status:** Proposed
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending
- **Related Spec:** [Spec 0101](../specs/0101-combined-pbr-maps.md)

## Context

[ADR-0157](0157-metallic-roughness-texture.md) adds a packed G/B texture
to the direct-light path. [ADR-0158](0158-occlusion-texture.md) adds an AO
texture to one IBL variant and excludes normal mapping and packed G/B.
Normal-mapped IBL uses the Vulkan backend's six sampled bindings.
Runtime realization and Renderer material binding also share one
data-map slot between packed G/B and AO. Existing closed wire methods
and material schemas encode this restriction.

## Decision

1. **Keep two independent data-map bindings.** Retain existing ordinary,
   normal and packed-only routes, and the existing AO-only IBL route.
   Add IBL fragment variants matched to the existing IBL vertex
   layout for each row. The existing base, environment, DFG and shadow
   bindings stay at 1–4. Binding 0 remains the uniform buffer.

   | IBL variant | Normal | Emissive | Packed G/B | AO | Samplers |
   | --- | --- | --- | --- | --- | --- |
   | AO + normal | 5 | 6 | Absent | 7 | 7 |
   | AO + packed G/B | Absent | 5 | 6 | 7 | 7 |
   | AO + normal + packed G/B | 5 | 6 | 7 | 8 | 8 |

   The Vulkan backend must admit eight sampled bindings, memoize through
   binding 8 and size its descriptor pools for eight combined image
   samplers per set. Keep legacy layouts unchanged. Check reflected
   binding types, indices and Pipeline counts against the layouts above.

2. **Keep one owner and a deterministic fallback.** Runtime realizes
   distinct packed and AO textures, uploads each through RenderGraph,
   and retains each resource through scene reuse, retirement and
   preview completion. Renderer owns the binding order. With a usable
   environment, AO coexistence selects the combined IBL path. With no
   environment the existing direct variant uses the currently bound
   normal and packed maps. The direct path keeps the checked AO
   dependency without an AO upload or descriptor bind. Clearing AO
   from a packed recipe returns to the existing packed direct-lit
   routing. AO-bound cooked pairs and drafts use fixed studio IBL in
   the existing bounded preview service. A normal-bound studio request
   uses the IBL normal-map vertex layout and tangent attribute even
   when live IBL is disabled. Preserve prior caller and AO-free lighting.
   Retire both request-owned maps and staging on confirmed completion
   or the existing terminal drain.

3. **Version coexistence at the material boundary.** Use material
   source v13, cooked artifact v12 and sidecar v12 only when AO and
   at least one of normal or packed G/B coexist. Keep the existing
   fields and canonical order. Source and sidecar retain their existing
   normal field and append the packed G/B field when present, followed
   by the required AO field. In the artifact,
   keep the legacy prefix and append separate 64-bit asset keys in that
   order, using zero for absent packed G/B. Require AO and at least
   one of normal and packed G/B in this version. Validate version, keys
   and unique dependency edges across source, artifact, sidecar and
   catalog. Retain the existing import roles, texture formats and
   generation manifest v7. The manifest already carries exact
   material bytes and frozen texture evidence. Named activation
   must fail closed on an old Runtime through material source or
   artifact validation. Verify that failure on the same profile
   alongside a known-compatible old generation.

4. **Add a closed recipe operation version.** Add
   `assets.materialReadWithCombinedMaps`,
   `assets.materialSaveWithCombinedMaps` and
   `assets.previewMaterialDraftWithCombinedMaps`. Reuse the exact
   WithOcclusion value fields and request/response envelopes, including
   complete Edit `expected`, the existing Save modes and the draft
   echo. The method grants the new combination domain. Separate general
   recipe validity from each version's representable subset. Keep
   WithOcclusion and older methods strict. An unsupported active Read
   reports `UnsupportedMaterial`. An admitted legacy Copy, Edit or
   Resume with an unrepresentable recipe fails at `Source/InvalidRecipe`
   on the supporting Runtime and keeps the committed state where
   applicable. Stale and ahead guards still apply. Keep material
   receipts, job status, texture status, and the cooked GUID-pair PNG
   contract. Use existing correlated unsupported-method detection on
   peers without the extension. No package or protocol-major change.

## Consequences

The same assets remain selectable and reusable across existing scenes.
Old recipes keep their versions and render paths. Coexistence needs a
supporting client and build. The added variants and descriptors increase
work for combined IBL materials and demand preview and live resource
lifetime verification. Returning to packed-only lighting can change the
look after Clear, so the Editor needs to describe the transition.

## Alternatives Considered

- **Keep six samplers by packing ORM.** This couples independently
  registered assets, image sizes and edits and adds a producer or a
  user-facing import operation. It also leaves packed-only IBL routing
  as a separate decision.
- **Enable packed IBL globally.** This changes existing packed
  materials and their image baselines, including after Clear. Limit the
  new routing to the approved coexistence scope.
- **Widen WithOcclusion in place.** Old strict clients already reject
  combinations in that shape. Separate versioned methods provide
  capability detection and keep legacy read/write behavior explicit.
