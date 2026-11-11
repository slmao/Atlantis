# ADR 0151: Shared Offscreen Static Mesh Preview

- **Status:** Accepted
- **Date:** 2026-11-11
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-11-11, at revision `b4d71608` of [PR #345](https://github.com/slmao/Atlantis/pull/345).
- **Related Spec:** [Spec 0091](../specs/0091-static-mesh-preview.md)
- **Related ADRs:** Extends [ADR-0143](0143-shared-cooked-asset-catalog-queries.md)
  and [ADR-0106](0106-attachable-runtime-transport-and-control.md) for one
  operation. Retains the [uniform client rule](0125-out-of-process-editor-platform-and-uniform-client-interface.md),
  [offscreen target ownership](0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  and [readback boundary](0040-gpu-to-cpu-readback-rhi-capability.md).

## Context

An asset GUID identifies catalog content, not a World entity or a GPU
resource already loaded in the scene. The existing `control.step` capture
advances the Runtime's control state, captures the current scene, and uses
its mutable camera and render targets. Asset preview needs the same drawing
and readback machinery, but independent input, ownership and failure state.

## Decision

1. Add a read-only `AssetConnection` operation carried additively over the
   existing protocol. Runtime resolves the GUID from the same retained
   catalog used by asset discovery. It returns bounded, owned image bytes;
   all clients use this result, with no path or renderer handles. The
   Editor owns presentation and stale-result handling. See Spec R1/R4 for
   the contract and compatibility behavior.
2. The executable-private Assets host admits work within the existing
   service interval. The Runtime composition root uses a private offscreen
   helper with request-owned mutable resources, including pipelines whose
   Vulkan descriptor sets are written during recording. It reuses
   `Renderer::drawFrame`, a RenderGraph readback pass and the capture PNG
   encoder. Nothing edits World, authoring, the live viewport or Runtime
   control state. Asset System stays CPU-only. Renderer/RHI, the normal
   frame path and the public scene-capture contract do not change.
3. Serve synchronously on the owning thread, at most once across the
   existing Remote poll and `--exec` dispatch, with the limits in Spec R3.
   Qualify ADR-0106 D1 for this handler only: it can wait for GPU completion
   during dispatch, while socket I/O remains nonblocking and the existing
   bounded buffers and disconnect rules still apply. Drain GPU use before
   releasing resources, with terminal teardown for unconfirmed completion.
   This avoids a job system, queue, cross-thread GPU ownership, GPU cache
   and new idle render work. A separate ordinary Editor SDK connection
   isolates preview transport faults from editing; both still share the
   Runtime and its responsiveness.

## Consequences

The same result is accessible from the Editor, scripts and agents and can
be tested with the existing headless Vulkan stack. A private mutable
render context prevents the preview from contaminating the next main frame.
Lazy snapshot retention can extend the lifetime of CPU catalog data even
when rebuild is disabled. A request incurs setup, submission and readback on
the serving thread, so other work can wait for it. The first image is a
geometry aid with fixed framing and no scene appearance fidelity.

## Alternatives Considered

- **Insert a temporary entity or call scene capture.** Both depend on and
  can affect the scene or control state. The capture's readback and encoding
  can be reused separately.
- **Render in the Editor or ship a local preview file.** Both give the
  Editor a second engine or data path. Files also introduce cleanup and path
  authority without helping the small bounded response.
- **Use a preview process, job, or persistent thumbnail cache.** These can
  reduce active-request stalls but add lifecycle, invalidation and GPU
  ownership decisions before they are needed.
- **Resolve the mesh's original material or light it synthetically.** A
  catalog mesh does not choose a scene material. Even synthetic lighting
  would require a preview material and light setup; the existing unlit
  path is sufficient to review shape and cooked vertex colors first.
