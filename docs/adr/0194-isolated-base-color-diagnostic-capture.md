# ADR 0194 — Isolated Base-Color Diagnostic Capture

- **Status:** Accepted
- **Date:** 2026-11-28
- **Deciders:** Atlantis maintainer
- **Acceptance:** [Maintainer joint approval of 5144fe7a](https://github.com/slmao/Atlantis/pull/459#issuecomment-7223673579) on 2026-11-28.
- **Related Spec:** [Spec 0148](../specs/0148-material-lighting-diagnostics.md)
- **Related decisions:** [ADR-0186](0186-shared-material-binding-pick.md), [ADR-0192](0192-render-look-capture-descriptor-ownership.md), [ADR-0193](0193-bounded-render-look-capture-extent.md)

## Context

Material pick resolves a specific opaque slot. Existing PNG capture uses
the normal output transform, and Render Looks renders two appearance
variants. A base-color diagnostic requires the actual resolved texture and
factor before shading and post processing. The normal and diagnostic images
need one scene basis even when the user or another client is editing.

## Decision

1. **Add one capability and one shared capture.** An optional true
   `supportsMaterialDiagnosticCapture` on `control.status` advertises
   `control.captureMaterialDiagnostic` on `atlantis.remote/1`. Missing or
   invalid capability is unsupported. Reuse the existing `DocumentToken`,
   `ApplyId`, capture camera and extent types. Require `document`, `applyId`,
   `generation`, `camera`, `extent`, `target` (entity, mesh and material GUIDs)
   and an ordered `outputs` array (normal and base-color paths, both fresh
   and absolute as in Render Looks). An optional
   `viewport` carries the 0136 stamp for a hosted caller. Both SDKs, CLI and
   an exact MCP tool expose the same request and result. Older servers retain
   ordinary captures and the Editor explains that diagnosis requires an
   upgrade. Existing clients and capture requests keep their formats.
2. **Settle once, capture twice.** Runtime admits the request between
   frames. Verify the current hand-authored document token, its settled Apply
   receipt and generation, the live entity's explicit mesh/material binding
   against the document, and typed resources in the Runtime catalog. When a
   viewport stamp is supplied it must match the presented default policy.
   The Editor refuses ambiguous location under 0138 before sending. The
   shared request always names an explicit target. At admission, refuse a
   changed binding, stale projection, unresolved resource or unsupported
   target. Freeze the current live projection's draw data and
   active camera appearance for the duration of the pair. Render normal from
   that snapshot with the supplied observation camera. Render base color
   of the target with the same matrices and transform. The reply identifies
   the document, Apply, projection, target, bound base-color texture GUID,
   linear RGB factor, normal exposure, both
   labeled paths and extents, and their view/projection matrices. Clients
   reject mismatched identities, paths, sizes or matrices. The exact tool
   uses the existing PNG path conventions. Use the existing capture refusal
   vocabulary (`Unsupported`, `Busy`, `InvalidRequest`, `StaleProjection`,
   `TargetChanged`, `Unavailable`, `BudgetExceeded`, `CaptureFailed`). A
   refusal returns no successful pair. Preserve transport error categories
   and report an unknown result after loss of a sent request's receipt.
3. **Keep resources capture-owned.** Share one admitted pair slot with
   Render Looks and reuse its private command list, descriptor limits and
   GPU completion rules. Render and read back normal, then base color. Reuse
   or release normal phase scratch before allocating diagnostic-specific
   scratch after completion. The diagnostic RenderGraph pass needs only
   target geometry, existing base-color texture and sampler, a local camera
   uniform, one depth target and an `Rgba8Srgb` color target. Write linear
   RGB to that target. sRGB conversion occurs on color write, and readback
   keeps the mask in alpha. No tone mapper or active-camera appearance data
   is bound to the diagnostic pass. Gate the diagnostic shader and host
   registration to the Windows capture executable. Use ADR-0193's 128–1920 per axis and
   2,073,600-pixel cap, one RGBA8 readback at a time, and an encoded limit
   of eight bytes per pixel per image. Preflight before GPU allocation.
   Reuse fresh-file publication and clean only owned partial outputs after
   a known failure. Keep GPU-owned memory until completion is confirmed,
   following ADR-0192 on failed waits and shutdown. Do not install new
   ordinary viewport bindings or allocate diagnostic resources in `runFrame`.

## Consequences and alternatives

The pair provides a material-input comparison with the same viewpoint.
Its isolated silhouette may include geometry occluded by other objects in
normal shading. It costs one explicit isolated pair and shares capture
admission with A/B. The ordinary window and scene have no diagnostic mode.
Reusing the regular output transform at zero EV would still tone-map the
data. Extending every material or drawing an alternate live viewport would
broaden scope, introduce coverage dependencies and expose another client to
diagnostic state. Separating this capture from render-look substitution keeps
the existing preset and adoption contract intact.
