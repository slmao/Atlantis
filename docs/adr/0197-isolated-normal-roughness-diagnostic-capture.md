# ADR 0197 — Isolated Normal and Roughness Diagnostic Capture

- **Status:** Proposed
- **Date:** 2026-11-28
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending
- **Related Spec:** [Spec 0151](../specs/0151-normal-roughness-diagnostics.md)
- **Related decisions:** [ADR-0194](0194-isolated-base-color-diagnostic-capture.md), [ADR-0192](0192-render-look-capture-descriptor-ownership.md), [ADR-0193](0193-bounded-render-look-capture-extent.md), [ADR-0134](0134-lit-normal-transform-by-cofactor.md)

## Context

The 0148 comparison pins one applied material, re-renders the ordinary scene
and draws the isolated base-color input. Its isolated pass binds only base
color and cannot show PBR's shading normal or effective roughness. A new
three-image result needs explicit output roles and a bounded lifetime without
changing the existing exact two-image client contract.

## Decision

1. **Add a shared operation.** An optional true
   `supportsNormalRoughnessDiagnosticCapture` on `control.status` advertises
   `control.captureNormalRoughnessDiagnostic` on `atlantis.remote/1`.
   Keep the 0148 capability, method and exact shapes intact. The new closed
   request uses its `document`, `applyId`, `generation`, `camera`,
   `extent`, `target` and optional hosted `viewport` stamp. Require
   exactly three fresh absolute output paths in order: shaded scene,
   world shading normal, linear roughness. The reply echoes the document,
   Apply, generation and target and returns the two nullable texture GUIDs,
   realized roughness factor, the ordinary frame report and three ordered
   image descriptions. Each image carries its role, requested path,
   dimensions and view/projection matrices. Clients validate identities,
   order, paths, extents and consistent matrices against the request
   before accepting the set. Missing or malformed capability is
   unsupported. Native and C# SDKs, CLI and an exact MCP tool use the same
   contract and existing capture refusal and transport categories.
2. **One resolved snapshot and unchanged shader semantics.** Reuse 0148's
   admission and explicit binding resolution. Validate the settled
   projection, document, camera and viewport policy before holding the
   current scene draw data and resolved sampled resources between frames.
   Use the ordinary normal-shading capture for the first image, including
   settled IBL. Derive the diagnostic inputs with the same normal and
   roughness math and sampling used by the four opaque PBR shader sources
   (DirectLit/IBL with and without normal mapping). Include variants that
   compile those sources for packed textures and AO. Share the narrow
   Slang input math with the diagnostic or use the same production
   expressions with a source-equivalence check. Preserve the normal
   transform contract and its existing reference guard. Diagnostic shaders
   bind only the resolved material inputs they need, plus capture-local
   draw data. They output directly to `Rgba8Unorm`, without the regular
   output transform. Use a RenderGraph draw and readback. No live shader
   debug branch, persistent viewport mode, or change to ordinary pipeline
   layouts or material bindings is required.
3. **Bound and retire the set as a unit.** Use the existing serialized
   capture interval with at most one admitted request. Reuse the
   ADR-0193 extent caps (128–1920 per axis and 2,073,600 pixels), the
   ADR-0192 descriptor budgets, one recording/readback at a time, and
   the eight-bytes-per-pixel encoded cap per image. At maximum extent,
   retained encoded bytes across all three PNGs are bounded by 49,766,400.
   Preflight path and pipeline budgets. Render ordinary first, then the
   two isolated images sequentially. Reuse or release scratch and isolated
   descriptors after GPU completion before allocating the next recording.
   Retain in-flight bindings and resources on failed waits under ADR-0192.
   Encode the complete set before creating output files. Use exclusive
   create and reject aliased, duplicate or existing destinations. Return
   success only after all three writes close successfully. On a known
   failure clean only still-owned partial outputs. This is a complete
   result contract, not a filesystem-wide atomic commit for observers.
   Keep the capture host and diagnostic shader deployment in the existing
   Windows scope. Ordinary shaders shared with Android keep their
   original behavior and packaging.

## Consequences and alternatives

One explicit request adds two isolated draws and readbacks while the main
viewport keeps its usual bindings and passes. The normal and roughness maps
can be interpreted per pixel with a defined numeric encoding. The isolated
silhouette can extend behind another object in the shaded image.

Extending the exact base-color request with optional files would create a
second interpretation of an existing method. Three separate requests could
mix projections during Apply. Borrowing ordinary viewport descriptor sets
would expose capture state to regular drawing. The dedicated triplet keeps
existing clients compatible and pins one scene basis with private resources.
