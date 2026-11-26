# Spec 0146: Isolated Descriptors for Render Look Capture

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-26
- **Design baseline:** `8bf5fba6` on `main`
- **Related Plan:** [Plan 0146](../plans/0146-render-look-capture-descriptors.md)
- **Related ADR:** [ADR-0192](../adr/0192-render-look-capture-descriptor-ownership.md) (Accepted)
- **Approval:** [Joint approval of 56207a64](https://github.com/slmao/Atlantis/pull/456#issuecomment-7196813749)

## Summary

Enable [Spec 0145](0145-render-look-presets.md) to record each A/B image with
its own Vulkan descriptors while borrowing the settled scene's immutable
pipeline and asset inputs. This supplement addresses the capture blocker in
[implementation PR #455](https://github.com/slmao/Atlantis/pull/455).

## Motivation

The current Vulkan command list updates the descriptor set owned by the
bound pipeline. Borrowing that set for capture would change live bindings.
Cloning all material pipelines through the existing factory would use 508
sets for the two copies of the 254 Bistro material pipelines alone. The
ordinary pool budget is 508, and the fallback, shadow, output, and other
stages also require sets.

## Requirements

- **R1 — Isolation.** Only the explicit look-pair capture opts in. Its
  RenderGraph recording binds capture-owned descriptors and resources.
  It may borrow immutable pipelines, layouts, meshes, textures and samplers.
  When live Bloom is off, the capture can create the existing Bloom
  pipelines without allocating their sets from the ordinary pools. A
  compare creates no main-viewport descriptor writes, pool growth, frame
  profiling samples, or scene edits. Ordinary viewport rendering and
  regular captures retain their existing behavior.
- **R2 — Budget and refusal.** Allow one admitted pair per Runtime and one
  capture pool live at a time. Each image recording may use up to 320 sets,
  320 uniform-buffer descriptors and 2,560 combined image samplers.
  Preflight both variants before recording, including required Bloom
  bindings. An unsupported layout, budget refusal, or allocation failure
  fails the pair with a diagnostic through the existing 0145 outcome.
  Publish a pair only after both images succeed. The capture never grows
  the pool or falls back to the ordinary pools.
- **R3 — Resource lifetime.** Record and settle A before recording B.
  A rejected or unsubmitted recording can release immediately. On successful
  submission, retain its command list, pool, scratch resources and borrowed
  dependencies until GPU completion is confirmed. Cancel retires the
  comparison result and leaves GPU ownership intact through settlement.
  A failed completion wait suppresses the result and prevents reuse or a
  second submission while completion remains uncertain.
- **R4 — Acceptance.** Retain the [Plan 0145](../plans/0145-render-look-presets.md)
  Windows 100% matrix at both 0131 Bistro views, the off-to-on Bloom case,
  second-client isolation, and all existing applicable gates. Verify that
  ordinary rendering and capture still see the live scene after a success,
  a refusal and cancellation. Keep the original Adopt, Undo/Redo, Save and
  independent Build/Open checks.

## Architectural Impact

[ADR-0192](../adr/0192-render-look-capture-descriptor-ownership.md) supplements
ADR-0191 with an opt-in RHI recording mode and explicit Vulkan ownership.
It preserves the existing RenderGraph, render algorithms, scene formats,
editor workflow and shared protocol. It adds no backend, dependency or
worker thread.

## Verification

[Plan 0146](../plans/0146-render-look-capture-descriptors.md) maps the pool
budget, submission lifecycle and isolation to focused Vulkan and Runtime
checks and the existing Bistro workflow.
