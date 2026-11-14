# ADR 0158: Occlusion Texture Role and IBL Material Binding

- **Status:** Proposed
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0099](../specs/0099-occlusion-texture.md)

## Context

The material importer drops glTF occlusion. Direct-lit materials have
no indirect contribution to attenuate. The IBL pipeline has explicit
diffuse and specular environment terms. The ordinary IBL pass uses five
sampled image bindings and the normal-mapped IBL pass uses six. The
current Vulkan implementation admits six, and the packed
metallic–roughness path is direct-lit. The shared texture workflow
already commits registered source separately from generation publishing
and validates both sides of that boundary.

## Decision

1. Register a standalone 8-bit linear PNG under the canonical managed
   Occlusion leaf and the shared Texture GUID namespace. Use a one-mip
   RGBA8 UNORM artifact. Manifest v7 adds the role to the existing frozen
   evidence vocabulary. Only views with that role emit v7. Locking,
   receipts and explicit activation follow the shared contract. Resume
   is source-preserving and requires the sole ahead change. Use the same
   frozen checks before durable writes and when settling an uncertain
   publication destination.
2. Apply linear red to the sum of diffuse and specular IBL at fixed
   strength 1. Adopt UV0 and the existing material sampler. Require
   opaque `PbrDirectLit` and empty normal and metallic–roughness slots
   for a bound recipe. Normal-mapped IBL already uses all six bindings
   and packed materials have no IBL variant. Use binding 6 in one opt-in
   IBL fragment permutation. With no environment use the current
   direct-lit path. Clear restores the current unoccluded path. Preserve
   the existing scalar factors and the direct and emissive terms.
3. Serialize a bound AO GUID as source v12 and its key as artifact and
   sidecar v11. Keep prior compatible formats when AO is absent. Catalog,
   cooker, frozen generation, scene load and preview agree on the
   canonical role and dependency. Ship an additive complete recipe and
   role-specific import and status to the shared clients. Older writers
   refuse an unrepresentable recipe before mutation and leave ahead
   committed bytes recoverable through compatible Resume.
4. Reuse the bounded synchronous PNG contract and one admission per
   service interval across preview methods. Load the fixed built-in
   studio bake under fixed byte and shape limits. Use request-owned IBL
   realization in AO-bound cooked and AO-compatible new draft previews,
   including a null-AO comparison for the latter. Keep prior lighting
   for unbound cooked pairs and older draft methods. Preserve the
   background and keep the live environment out of the preview. Use the
   existing RenderGraph submission, completion and terminal drain.

## Consequences

### Positive

- Existing linear import and recovery supply stable identity and
  repeatable named-generation activation.
- Direct and ambient contributions stay distinct and the GPU changes
  fit the existing binding budget.
- Editors and other clients share one recipe and compatible peers can
  continue operating on earlier formats.

### Trade-offs

- The map has visible scene effect only with configured IBL.
- A material needs an explicit clear of a conflicting map before AO
  can be bound. AO with normal mapping or packed metallic–roughness
  requires another review.
- New generations and bound materials need compatible tools. Studio
  preview incurs bounded environment work only on qualifying requests.

## Alternatives Considered

- **Multiply base color or final shading.** This changes direct
  light, and final shading also includes emission. Both lose the
  indirect-light meaning of the authored map.
- **Pack ORM or lift the Vulkan slot limit.** This would couple the
  feature to packed-map IBL and extra permutations or descriptor
  changes. The single-slot IBL case fits the current layout.
- **Use the active scene lighting in every preview.** Results would
  vary with Runtime startup and a scene without IBL would hide AO.
  Fixed studio lighting gives an AO comparison without mutating the
  active scene or changing older preview methods.
