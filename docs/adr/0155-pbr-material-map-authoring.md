# ADR 0155: Author Existing PBR Map Slots Through the Shared Asset Interface

- **Status:** Proposed
- **Date:** 2026-11-14
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0096](../specs/0096-pbr-texture-editing.md)

## Context

[ADR-0153](0153-managed-material-authoring-and-draft-preview.md) defines a
closed base-color recipe. Adding fields to its existing replies would
break strict clients. Project-owned materials can already persist their
normal and emissive slots in the ordinary material format. The Vulkan
scene path has both slots, while [ADR-0152](0152-shared-mesh-material-preview.md)
limits offscreen pair preview to the base recipe.

## Decision

1. Add parallel closed material Read, Save and Draft Preview operations on
   the shared asset interface for the complete authoring recipe. The two
   wire shapes feed the same retained catalog and managed worker. Reuse
   0094's material receipts, status history, ownership checks and recovery.
   Legacy mutations refuse any extended material they cannot represent,
   including Resume. Extend ADR-0153 for the extra fields.
2. Extend the existing opaque direct-lit pair realization for cooked and
   draft recipes, partially superseding ADR-0152's one-texture restriction.
   Resolve each declared slot from the same snapshot. Use a bounded
   request-local set, deduplicate shared textures and keep the existing
   admission and terminal drain. Reuse
   [ADR-0074](0074-pbr-normal-map-material-descriptor-and-shader-contract.md)
   and [ADR-0096](0096-emissive-texture-always-bound-slot.md) rendering rules
   and the existing RenderGraph. The cooked GUID-pair contract is stable.
3. Keep material format and generation publication under the existing
   producer. Preserve the GUID on Edit and validate all role assignments
   and unique dependency edges against the staged catalog. The existing
   sRGB PNG import continues supplying color textures. Normal mapping
   uses an existing cooked UNORM asset.

## Consequences

- All clients reach the same role validation, preview and recovery behavior.
  Existing clients keep their representable subset and receipt handling.
- A supporting UI needs two version-specific paths for basic and complete
  editing. Additional bounded texture loads and allocations can increase
  explicit preview latency. There is no additional idle work.
- Operators can reuse cooked normal maps from the catalog. Registering a
  new standalone linear-data texture requires a separately reviewed import
  capability.

## Alternatives Considered

- Add optional keys to the legacy recipe. Older strict clients would
  reject new responses or risk saving a projection of the content.
- Add a role-specific asset query or editor-only path. Existing discovery
  supplies identity and the shared cooker-backed service can determine
  eligibility when the user previews or saves.
- Ship all requested PBR texture types together. Metallic/roughness and
  occlusion require a new material and shader contract. The available
  optional slots already form a coherent editing and verification slice.
