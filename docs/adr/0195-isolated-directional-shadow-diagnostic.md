# ADR 0195 — Isolated Directional Shadow Diagnostic

- **Status:** Accepted
- **Date:** 2026-11-28
- **Deciders:** Atlantis maintainer
- **Acceptance:** [Maintainer joint approval of `51c0a4d1`](https://github.com/slmao/Atlantis/pull/461#issuecomment-7236151632) on 2026-11-28.
- **Related Spec:** [Spec 0149](../specs/0149-directional-shadow-diagnostics.md)
- **Related decisions:** [ADR-0072](0072-directional-shadow-map-resource-pass-and-pbr-integration.md), [ADR-0192](0192-render-look-capture-descriptor-ownership.md), [ADR-0193](0193-bounded-render-look-capture-extent.md), [ADR-0194](0194-isolated-base-color-diagnostic-capture.md)

## Context

The existing shadow pass separates its caster span from the main draw
span. It writes a depth attachment cleared to 1.0, which the main pass
samples. The current PBR shadow receivers return full visibility outside
the light volume and compare in-volume depth minus the existing positive
bias against the sampled depth. An untouched clear therefore provides a
fully visible directional-light input. The isolated pair path already
creates its own depth, HDR, shadow, Bloom and output resources.

## Decision

1. **Use a distinct, shared pair request.** On the existing protocol version,
   advertise optional true `supportsDirectionalShadowDiagnosticCapture`
   on `control.status` and expose `control.captureDirectionalShadowDiagnostic`
   to all shared clients. Require the existing `document`, `applyId`,
   `generation`, `camera`, `extent` shapes and two fresh absolute
   `outputs`, in normal then unshadowed order. Accept the existing optional
   hosted `viewport` stamp. If supplied, the stamp must match the presented
   default policy. Report the exact document, Apply, generation, active
   scene camera, and distinct `normal` and `withoutDirectionalShadows` frame
   reports with paths, dimensions, matrices and observed lighting/exposure.
   Validate identities, paths, extent and matrices before delivery. Reuse the
   capture refusal vocabulary, including `Busy`, `StaleProjection`,
   `InvalidRequest`, `Unavailable`, `BudgetExceeded` and `CaptureFailed`.
   An absent or invalid capability gives upgrade guidance without sending
   the new request. Existing requests, responses and capability handling
   remain compatible. The protocol carries no bookmark or editor state.
2. **Freeze one live basis and vary its caster span only.** At serialized
   between-frame admission, match the hand-authored token and settled Apply
   receipt to the live projection. Resolve and pin the draw data, light
   data, active scene camera appearance and realized assets once. Refuse
   missing dependencies and a missing or non-contributing directional light.
   Reuse the observation lens and pose for both projections. Reuse the
   existing isolated normal-frame function with the same complete draw span,
   passing the ordinary casters for the first image and an empty caster span
   for the second. Retain the existing RenderGraph depth clear, transition
   and sampling. The second clear is mandatory even when reusing the first
   shadow allocation. This remains within the existing Renderer call and
   material/RHI interfaces. No new shader or descriptor layout is required.
3. **Reuse existing isolated ownership and bounds.** One pair occupies the
   shared slot, with at most one active private pool and one in-flight
   recording. Validate distinct pipeline demand before allocating capture
   scratch. Use ADR-0192's limit of 320 sets with up to one uniform and eight
   combined samplers per set, and ADR-0193's 128–1920 per axis and
   2,073,600-pixel area limits. Reuse one shadow extent, image scratch set
   and RGBA8 readback with two encoded outputs bounded at eight bytes per
   pixel each. Confirm completion before reusing the shadow map or other
   mutable scratch. Preserve the same failed-wait and device-teardown rules.
   Reuse fresh-file pair publication, cleaning only owned partial outputs
   after a known failure. Preserve the transport error category on a lost
   or malformed receipt and require explicit retry. Never publish a partial
   pair as success or switch a live viewport or renderer setting.

## Consequences and alternatives

The result shows the effect of the currently implemented directional shadow
map at the requested view. It can include downstream fog and Bloom effects.
It costs two offscreen recordings and shares the existing capture budget.
Reusing the normal pipeline preserves material, environment and camera
semantics for both images.

Removing a light would remove direct illumination. A programmable material
override would broaden the shader ABI and pass behavior. A live toggle or
temporary scene edit would expose intermediate state to other clients and
would need restore semantics. The private pass clear supplies the required
control without any of those dependencies.
