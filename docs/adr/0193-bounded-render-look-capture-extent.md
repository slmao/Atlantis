# ADR 0193: Bounded Render Look Capture Extent

- **Status:** Accepted
- **Date:** 2026-11-27
- **Deciders:** Atlantis maintainer
- **Acceptance:** [Joint approval of 517e597c](https://github.com/slmao/Atlantis/pull/457#issuecomment-7218716662).
- **Related Spec:** [Spec 0147](../specs/0147-scene-view-bookmarks.md)
- **Supplements:** [ADR-0191](0191-render-look-preview-and-adoption.md), decision 2

## Context

[0131](../specs/0131-bistro-reference-views.md) can confirm a fixed-size
embedded drawable with the existing camera override and frame report. Both
[0145](../specs/0145-render-look-presets.md) variants use a caller-supplied
camera, but they always render at 960 × 540. The stored physical aspect is
part of a custom view's projection. [ADR-0192](0192-render-look-capture-descriptor-ownership.md)
already bounds and isolates descriptor recording for the pair.

## Decision

1. **One optional extent on the shared pair request.** Add an optional
   `extent` object with exactly two integer members, `width` and `height`,
   to `control.captureRenderLook`. Omission is exactly 960 × 540. Reject
   null, partial, extra-member, non-integer, out-of-range and overflowing
   extents. Require each axis in [128, 1920] and product at most 2,073,600.
   Validate at the shared decode and Runtime admission boundaries before
   cooking or allocating capture resources. Compare using checked arithmetic.
   Both variants derive the projection aspect, scratch dimensions, readback
   size and PNG extent from the same validated pair of values. A successful
   reply uses the existing `CapturedImage` fields and clients require the
   effective requested dimensions on both images.
2. **Explicit compatibility.** Advertise an additional optional
   `supportsRenderLookCaptureExtent` on `control.status`. Missing or invalid
   capability counts as unsupported. With the old capture capability only,
   send the original request shape for a default-size pair. Refuse a nondefault
   extent with upgrade guidance. Updated Runtime retains the old omission
   behavior. Native and managed SDKs, Remote codecs, CLI and MCP expose the
   same request. The CLI accepts one optional `extent <width> <height>` row
   after `camera` in the existing manifest. MCP extends the existing exact
   tool with the same optional object. Ordinary viewport step/capture,
   protocol version, authoring guard and 0145 outcome types stay as they are.
3. **Bounded scratch.** Preserve one admitted pair, one active private
   descriptor pool, the 0146 descriptor budget and settle-before-reuse
   rule. Reuse the existing RenderGraph and Vulkan resources. Bound each
   readback at four bytes per requested pixel and each encoded PNG at twice
   that size. Charge the two outputs and one scratch set to that one pair.
   Image-dependent HDR, depth, Bloom and output allocations use the admitted
   extent and existing Bloom levels. Leave the shadow extent fixed. A failure
   publishes no successful pair. Validation and review report the largest
   accepted allocation case with Bloom enabled, including peak descriptor
   use and explicit scratch allocations. No recurring observation is added.

## Consequences

The Editor owns the local names, scene scope and reference selection. The
shared contract carries only a camera and admissible frame dimensions in
addition to the existing look inputs. Newly admitted larger images can use
more GPU memory and take longer to capture. Callers receive a refusal if
the view is outside the published budget or a compatible operation fails.

## Alternatives Considered

- Keep the fixed 960 × 540 path for all views. A custom aspect would need
  to be changed before saving or produce a differently framed A/B image.
- Add a general render-to-size operation. The existing guarded pair and
  explicit ownership already cover the required offscreen use.
- Infer dimensions from the live window or rescale PNGs. Neither preserves
  a saved projection independently of window state.
