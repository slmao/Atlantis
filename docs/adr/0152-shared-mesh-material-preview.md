# ADR 0152: Shared Offscreen Mesh and Material Preview

- **Status:** Proposed
- **Date:** 2026-11-12
- **Deciders:** Pending joint review
- **Acceptance:** Pending
- **Related Spec:** [Spec 0092](../specs/0092-mesh-material-preview.md)
- **Related ADRs:** Extends [ADR-0151](0151-shared-offscreen-mesh-preview.md)
  for an explicit pair. Retains the catalog and generation boundaries in
  [ADR-0143](0143-shared-cooked-asset-catalog-queries.md) and
  [ADR-0146](0146-managed-asset-rebuild-and-generation-publication.md).

## Context

A catalog mesh has no single material. The Inspector knows a candidate
material's type, but neither that fact nor the current scene capture shows
the candidate on a particular mesh before an edit. Rendering a pair adds
dependency validation, texture upload and mutable descriptor state to the
offscreen work introduced in 0091.

## Decision

1. Add a separate shared operation whose request and success identify both
   the mesh and the material. Reuse 0091's bounded PNG representation and
   exact legacy fallback. Both modes resolve against one retained catalog
   and use its cooked records for all dependency reads. A result is
   correlated by both roles, even when a client has since changed only one
   input.
2. Use one fixed, direct-lit base PBR recipe. Its single authored texture
   comes from the decoded material and must agree with the catalog
   dependency. The existing material, mesh and texture formats remain the
   source of rendering values. Restrict the supported subset and bound
   opened files before allocating or uploading. The preview is evidence of
   this pair under the chosen recipe. Scene compatibility remains decided
   by authoring and Runtime apply.
3. Use the same between-frame execution boundary, admission permit and
   terminal drain as geometry preview. Reuse the Renderer, existing texture
   upload and RenderGraph readback in one submission. Keep upload staging,
   textures and mutable GPU state together through settlement or terminal
   drain. The realization path stays separate from live scene resources.
4. The two Editor surfaces use one preview coordinator and the typed SDK.
   The material reference chooser owns the preview surface while open
   and uses the labeled mesh read when opened. Its preview does not
   participate in the existing field confirmation guard.

The wire shape, recipe, limits and Editor invalidation rules are specified
in [Spec 0092](../specs/0092-mesh-material-preview.md#requirements).

## Consequences

The same pair can be reproduced through each shared client without editing
a scene. The first recipe covers common opaque PBR materials while keeping
dependency and memory costs bounded. Synchronous upload and drawing can
delay the serving Runtime. Materials that depend on other slots, alpha or
environment lighting require a separately reviewed recipe. The chooser's
mesh remains a visible snapshot of the opening read until the user reopens it.

## Alternatives Considered

- **Extend the geometry request with an optional material.** A separate
  operation gives older Runtime versions an explicit capability refusal and
  keeps the existing image identity and contract stable.
- **Use `LitTextured` only or enable every PBR variant.** The first option
  omits the base PBR materials emitted by common glTF imports. The second
  introduces more dependencies and pipelines than the first slice needs.
- **Preview through the current scene or implement a temporary edit.**
  That couples inspection to authoring, scene lighting, and apply state.
- **Read the companion mesh repeatedly while the chooser is open.** A
  labeled opening value gives the image a stable input and preserves the
  existing chooser's read and confirmation behavior.
