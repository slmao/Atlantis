# ADR 0192: Render Look Capture Descriptor Ownership

- **Status:** Accepted
- **Date:** 2026-11-26
- **Deciders:** Atlantis maintainer
- **Acceptance:** [Joint approval of 56207a64](https://github.com/slmao/Atlantis/pull/456#issuecomment-7196813749)
- **Related Spec:** [Spec 0146](../specs/0146-render-look-capture-descriptors.md)
- **Supplements:** [ADR-0191](0191-render-look-preview-and-adoption.md), decision 2

## Context

The main viewport owns the steady descriptor budget through
[ADR-0064](0064-vulkan-backend-descriptor-pool-growth-ownership-model.md).
Between-frame capture can reuse the immutable shader programs and layouts.
It needs independent mutable bindings for its own camera, shadow, HDR and Bloom
resources. RenderGraph still records the GPU passes and Runtime submits them
through the existing RHI path.

## Decision

1. **Explicit and fallible recording.** Add an opt-in RHI command-list
   factory for isolated bindings. The caller supplies the complete list
   of distinct pipelines for one offscreen image. Vulkan creates the
   private pool and allocates one compatible set per pipeline before
   recording. The existing Renderer calls bind the declared private sets
   using the existing pipeline layouts. Repeated draws with a shared
   material reuse the set for identical bindings in that recording.
   Every required binding is written before use. Reject an undeclared pipeline
   or a change to a set's already-bound contents before writing descriptors.
   Either error makes that recording ineligible for submission.
   The recording outcome is checked before queue submission. The existing
   ordinary factory and pipeline-local descriptor path remain the default.
2. **Bounded, private allocation.** Cap one pool at 320 sets with up to
   one uniform buffer and eight combined image samplers per set. Bistro
   requires at most 270 sets for 254 material pipelines, one fallback,
   one shadow, one sky, one output and twelve Bloom pipelines. The cap
   leaves 50 sets of headroom for this bounded workload. Validate distinct
   set demand and layout consumption for both variants before any rendering.
   Record one image at a time. Allocate a fresh pool for the next recording
   only after retiring the previous one. The capture-only pipeline creation
   option skips the ordinary set allocation for missing Bloom pipelines.
   Those pipelines keep the same shader contract and are legal only in the
   isolated recording mode. The normal viewport and ordinary capture use
   existing live pipelines, pool growth and profiling. An isolated recording
   contributes only to its requested capture report.
3. **GPU lifetime.** The command list owns its pool and sets. Runtime owns
   the request scratch and pins borrowed dependencies for the submission.
   On successful submit, the device retains the command list through the
   existing submission fence. Release or reset only after confirmed GPU
   completion. Free the command buffer before its descriptor pool, then
   retire scratch after readback or discard. A pre-submit error releases its
   unsubmitted resources. A known rejected submission needs no GPU wait for
   that recording. Cancel or disconnect discards the caller's result and
   scratch files only when safe, while an admitted recording settles.
   If a wait fails, retain the list, pool and dependent resources and stop
   the capture service. Resume ordinary rendering only after proven
   completion. Otherwise use Runtime's terminal teardown, releasing child
   objects while their device is valid after a successful drain or
   confirmed device loss. Where neither is established, abandon the run
   with ownership held through process exit. Failure never authorizes
   recycling the pool or reusing its bound resources.

## Consequences

Peak descriptor and scratch memory are bounded per active request. Captures
allocate and settle during the existing serialized Runtime service interval.
The design adds one RHI opt-in and a Vulkan implementation of that ownership.
It leaves steady rendering, presentation scheduling and the pipeline pool
schedule intact.

## Alternatives Considered

- Borrow the existing sets and restore their contents. Both writes would
  touch live bindings and would require proving restoration across failures.
- Duplicate all pipelines in the ordinary pools. Bistro's two material sets
  alone consume their full budget. Raising that budget would still mix
  capture allocation and lifetime into ordinary rendering.
- Add a second device or a general descriptor allocator. Independent devices
  would require new resource realization and scheduling. Only one private
  allocation lifetime is required for this capture.
