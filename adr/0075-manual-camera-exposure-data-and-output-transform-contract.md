# ADR 0075: Manual Camera Exposure Data and Output-Transform Contract

- **Status:** Proposed
- **Date:** 2026-09-08
- **Deciders:** slmao (drafted by Claude Code at explicit human
  direction; pending Human Review)
- **Related Spec:** [specs/0031-manual-camera-exposure-foundation.md](../specs/0031-manual-camera-exposure-foundation.md)

## Context

[ADR-0068](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
D-5 fixed the output-transform pass's tone-mapping exposure at a single,
named, shader-compile-time constant (`kBaselineExposure = 1.0`), and
explicitly named its own anticipated successor: *"a future spec may add
one [an exposure value], replacing `kBaselineExposure`'s fixed value
with a computed one without changing this Decision's own curve."*
D-10, in the same ADR, fixed the output-transform `Pipeline`'s
descriptor contract as exactly one sampler binding, "no uniform buffer
... and no push constant," a decision stated as a direct consequence of
D-5's then-fixed constant, not as an independent invariant.

Spec 0031 asks for exactly that successor: a per-`Camera` exposure
compensation scalar, threaded from scene data through Runtime into this
same pass. That touches four things at once — `atlantis::world::Camera`'s
own data shape, the scene source/artifact schema
([ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
versioning policy), `Renderer::drawFrame()`'s public signature, and the
output-transform `Pipeline`'s push-constant contract (a real, already-
generic RHI mechanism this codebase's `MaterialKind` pipelines already
use — [ADR-0067](0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-3/D-4 — but the output-transform pass itself never has). All four
changes exist only to serve one real, single decision — "exposure
compensation is per-`Camera` data, reaching the output-transform pass
through a push constant" — so this ADR aggregates them rather than
splitting into four. No second, independent architectural surface was
found while investigating the real code for this decision (confirmed
against `Camera`, scene source/artifact, `Renderer::drawFrame()`, both
output-transform `.slang` files, `descriptor_contract.h`,
`compile_and_validate.cpp`, and every real `drawFrame()` call site).

## Decision

1. **`atlantis::world::Camera` gains `float exposureCompensationEv =
   0.0f`.** No new component, no new World API beyond the existing
   `setCamera()`/`getCamera()` taking/returning the now-4-field struct
   by value (unchanged signatures).
2. **The scene source (`.scene.txt`) camera-node grammar gains one
   optional, trailing token**, `camera_exposure_ev=<f>` — 14 tokens
   (today's grammar) means `0.0`; 15 tokens sets it explicitly. No
   existing scene source file requires editing.
3. **The scene artifact binary schema version bumps 3 → 4**
   (`kSceneArtifactSchemaVersion`), gaining one trailing 4-byte
   little-endian float per node's camera block (112 → 116 bytes per
   node record), written and independently re-validated (finite, fixed
   range) on both cook and decode — matching every other field in this
   format's own established pattern. Per ADR-0045, an unrecognized
   schema version is rejected outright; no migration mechanism is
   introduced.
4. **A fixed, numerically-derived EV range, `[-16.0, +16.0]`**, enforced
   independently at both cook and decode time. Derived from real
   binary16/binary32 facts of this exact pipeline (the `Rgba16Float` HDR
   intermediate's own max representable value, `65504`, and binary32's
   own max finite value) — not a photographic-convention guess. See
   Spec 0031's own Proposed Design section for the full arithmetic; the
   short form: the real overflow ceiling for this pipeline's own
   `linearColor * exp2(ev)` math is `ev < ~112.0`, and `±16` is chosen
   with a `~7.9×10^28`-times safety margin below it, while also literally
   matching `Rgba16Float`'s own `16`-bit width.
5. **`Renderer::drawFrame()` gains one new parameter,
   `float outputTransformExposureCompensationEv`**, positioned
   immediately after the existing `outputTransformPipeline`/
   `outputTransformSampler` parameters. Every other parameter is
   unchanged. Every existing call site (Runtime, both example binaries,
   every image-regression fixture, every direct-call test) is updated to
   pass `0.0f` explicitly.
6. **The output-transform `Pipeline` gains a 4-byte, Fragment-stage-read
   push constant** (`ExposurePushConstants { float exposureCompensationEv;
   }`), populated once per frame inside the existing "output_transform"
   RenderGraph pass's execute lambda via the existing, already-generic
   `CommandList::pushConstant()`/`PipelineCreateParams::pushConstantSizeBytes`
   mechanism — the same mechanism every `MaterialKind` `Pipeline`
   already uses for its own, larger push constant, applied here for the
   first time to the output-transform `Pipeline`. No RHI change. The raw
   EV value is pushed; `exp2()` is evaluated once, in the fragment
   shader — the one place the Reinhard formula already lives (ADR-0068
   D-5) — never duplicated in Runtime/C++.
7. **Reinhard tone-mapping, the sRGB OETF, the HDR intermediate format,
   light-intensity semantics, and every `Material`/descriptor-binding
   contract are unchanged.** Only the value multiplied into
   `exposedColor` before Reinhard changes, from a fixed `1.0` literal to
   `exp2(exposureCompensationEv)`; at `exposureCompensationEv = 0.0`
   this is exactly `1.0`, identically.

## Consequences

### Positive

- Exposure becomes real, per-scene, per-`Camera` authorable data instead
  of a value only a shader-source edit and recompile can change —
  closing exactly the gap ADR-0068 D-5 itself flagged as open.
- Zero new RHI capability, zero new descriptor binding, zero new GPU
  resource — the entire mechanism reuses infrastructure this codebase
  already ships and validates (`pushConstantSizeBytes`,
  `CommandList::pushConstant()`, the per-`MaterialKind` push-constant
  precedent).
- `exposureCompensationEv = 0.0`'s exact byte-identical-goldens guarantee
  is a mathematical consequence of `exp2(0.0) == 1.0`, not merely tested
  behavior — the lowest-risk possible way to introduce this change
  alongside 10 already-committed, human-approved goldens.
- The fixed, derived `[-16, +16]` range makes "can this ever produce
  Inf/NaN" a closed, checkable question, independent of scene content —
  consistent with this codebase's existing NaN/Inf-avoidance discipline
  (ADR-0067 D-1, ADR-0068 D-5's own numeric-edge-cases note).

### Negative / Trade-offs

- `Renderer::drawFrame()`'s already-long parameter list (17 parameters
  today) grows to 18 — a real, if mechanical, widening of a public API
  surface every existing call site must update. No parameter-object
  refactor is proposed here; that would be unrelated, opportunistic
  restructuring outside this Spec's own scope.
- The scene artifact format's 4th schema-version bump in this
  codebase's history (mesh and material formats already bumped for
  Spec 0029's tangent/normal-map work) continues the established, no-
  migration-mechanism policy (ADR-0045) — every future format addition
  keeps paying this same "unrecognized version rejected outright, no
  automatic upgrade" cost, a known, already-accepted trade-off, not a
  new one this ADR introduces.
- The exact vertex-stage push-constant-reflection expectation
  (`compile_and_validate.cpp`) cannot be fixed with certainty without a
  real `slangc` compile (see Spec 0031's own Risks & Open Questions) —
  left as explicit Plan-stage verification work rather than guessed
  here.

## Alternatives Considered

See Spec 0031's own Alternatives Considered section (camera uniform
buffer instead of push constant; required vs. optional scene-source
token; CPU-computed multiplier instead of shader-computed;
alternative EV ranges) — reproduced there rather than duplicated here,
since every alternative was rejected using evidence gathered while
answering this Spec's own Requirements, not a separate architectural
trade-off this ADR needs to re-litigate independently.

One additional, ADR-scoped alternative: **amending
[ADR-0068](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
directly** (editing D-5/D-10 in place, or appending an Accepted
Amendment to it now) instead of drafting this new, separate ADR-0075.
Rejected for this round: ADR-0068 is `Accepted`, and its D-5/D-10 text
is still factually accurate today (`kBaselineExposure` is still a fixed
literal; no push constant exists yet) — amending it now, before this
ADR itself reaches `Accepted` and is implemented, would describe a
decision that has not actually been made yet. Once ADR-0075 is
`Accepted` and implemented, a short, cross-referencing Accepted
Amendment to ADR-0068 D-10 (noting the output-transform `Pipeline` now
carries a push constant, decided by ADR-0075) is the correct follow-up —
recorded here as required future work, not performed by this ADR.
