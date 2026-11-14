# ADR 0157: Packed Metallic–Roughness Texture Contract

- **Status:** Proposed
- **Date:** 2026-11-14
- **Deciders:** Pending joint review
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0098](../specs/0098-metallic-roughness-texture.md)

## Context

The managed PNG path already preserves RGBA8 UNORM samples, but the
opaque direct-lit material stores and shades with only scalar metallic
and roughness. Existing color and normal registrations have distinct
meanings under [ADR-0156](0156-managed-linear-normal-map-import.md).
The two shared recipe shapes in
[ADR-0153](0153-managed-material-authoring-and-draft-preview.md) and
[ADR-0155](0155-pbr-material-map-authoring.md) are closed. A new recipe
also changes persisted and GPU-visible material data.

## Decision

1. Extend ADR-0156 with the canonical packed role and private v6
   generation evidence only when the role occurs. The leaf/source key
   carries the role through the existing producer, worker, freeze and
   Resume. Both linear roles retain their own interpretation. Keep the
   normal and base-color wire methods and earlier manifest shapes.
2. Use one optional packed slot in opaque `PbrDirectLit`. Follow the
   [Khronos channel convention](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture).
   Multiply the existing scalars by G roughness and B metallic. Clear
   restores the scalar path. Persist only bound recipes in the new
   material source, artifact and metadata versions. Pair the artifact and
   metadata checks with the catalog's declared version. Load the existing
   formats with an empty slot. Keep the existing no-map descriptor and
   shader path. Add one sampled binding at the next free slot in each
   opt-in variant, with or without normal mapping. Reuse Runtime scene/preview ownership and
   RenderGraph, including texture upload and retirement. This extends the
   [direct-lit descriptor contract](0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
   for the new mapped variants.
3. Add a third exact recipe shape and role-specific import/status over
   the shared surface. Use the same worker, material status, settlement
   and cooked pair preview. Guard all older recipe paths against
   projection or mutation of a recipe they cannot represent. Reads use
   the active snapshot. The ahead registry guards writes, and Resume
   validates the durable recipe against the calling method's shape.
   Keep Runtime scene activation and separate Saved Scenes Build/Open
   governed by
   [ADR-0150](0150-local-saved-scene-build-launch.md).

## Consequences

- The packed map can round-trip via the shared asset system with stable
  role, bytes and GUID. Opaque direct-lit maps can coexist with the normal
  and emissive inputs already exposed to the material editor.
- A mapped material needs one additional sampled input and explicit asset
  realization work. The design adds two pipeline variants and requires
  platform shader deployment and descriptor verification.
- Old runtimes and tools remain usable on compatible projects and
  snapshots. Mixed workspaces and bound recipes require the new
  versions.

## Alternatives Considered

- General linear data import or reusing a normal GUID. Neither preserves
  the intended role or prevents accidental cross-slot use by old clients.
- Add an optional wire field to 0096. Strict clients would reject the
  response or could project the recipe to a lossy older write.
- Always bind a neutral texture in every PBR variant. That would change
  descriptor/realization costs for existing assets and expand testing
  to PBR models outside the first slice.
- Start with occlusion, IBL and glTF import together. Each adds a separate
  material/rendering or importer contract beyond this direct-lit flow.
