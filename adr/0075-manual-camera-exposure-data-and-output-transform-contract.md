# ADR 0075: Manual Camera Exposure Data and Output-Transform Contract

- **Status:** Proposed
- **Date:** 2026-09-08
- **Deciders:** slmao (drafted by Claude Code at explicit human
  direction; pending Human Review)
- **Related Spec:** [specs/0031-manual-camera-exposure-foundation.md](../specs/0031-manual-camera-exposure-foundation.md)
- **Related ADR:** amends [ADR-0068](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
  D-10 via that ADR's own new [Proposed Amendment — 2026-09-08](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md#proposed-amendment--2026-09-08)
  section — a separate document, reviewed independently (see Context)

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
same pass. That touches several things at once — `atlantis::world::Camera`'s
own data shape, the scene source/artifact schema
([ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
versioning policy), `Renderer::drawFrame()`'s public signature, and the
output-transform `Pipeline`'s push-constant contract (a real, already-
generic RHI mechanism this codebase's `MaterialKind` pipelines already
use — [ADR-0067](0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
D-3/D-4 — but the output-transform pass itself never has). All of these
changes exist only to serve one real, single decision — "exposure
compensation is per-`Camera` data, reaching the output-transform pass
through a push constant" — so this ADR aggregates them rather than
splitting into several. No second, independent architectural surface
was found while investigating the real code for this decision
(confirmed against `Camera`, scene source/artifact,
`Renderer::drawFrame()`, both output-transform `.slang` files,
`descriptor_contract.h`, `compile_and_validate.cpp`, and every real
`drawFrame()` call site).

**Why the push-constant *contract* change is a separate document from
this ADR, not folded in here:** [ADR-0068](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
is `Accepted`, and its D-10 text ("no push constant") is real,
Accepted-Decision text this ADR must not silently rewrite (AGENTS.md:
"ADRs are... not silently rewritten"). This repository's own
established convention (used repeatedly for ADR-0072, ADR-0073) is to
append a new, explicitly-titled section to the target ADR rather than
edit its Decision text in place. ADR-0068 therefore gains its own
`Proposed Amendment` section, submitted alongside this ADR and Spec
0031, reviewed and `Accepted` independently — not written after
Implementation to describe what was already built.

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
   introduced. Both checks reuse the existing `NonFiniteValue`
   enumerator (`SceneCookError`/`SceneArtifactDecodeError`) for an
   out-of-range value, the same way this codebase's own light
   color/intensity domain checks already reuse it for a finite-but-out-
   of-domain value — no new enumerator.
4. **A fixed, finite EV authoring range, `[-16.0, +16.0]`**, enforced
   independently at cook time, decode time, *and* inside
   `Renderer::drawFrame()`'s own public entry point (via `ATLANTIS_CHECK`,
   since direct, non-asset callers — examples, fixtures, tests —
   bypass cook/decode entirely). This is an **explicit, chosen
   authoring-domain policy**, not a claim of the unique mathematically
   possible safe range: the real binary32-overflow ceiling for this
   pipeline's own `linearColor * exposureMultiplier` math (worst-case
   `linearColor` bounded by `Rgba16Float`'s own max, `65504`) is
   `exposureMultiplier < ~2^112`; `[2^-16, 2^16]` sits enormously below
   that (a computed margin of roughly `7.9×10^28`), which is why this
   policy is safe to fix now, not why `16` specifically was derived —
   see Spec 0031's own Proposed Design for the full arithmetic and the
   explicit statement that no unique value is being claimed.
5. **`Renderer::drawFrame()` gains one new parameter,
   `float outputTransformExposureCompensationEv`**, positioned
   immediately after the existing `outputTransformPipeline`/
   `outputTransformSampler` parameters, carrying the raw EV. Every
   other parameter is unchanged. Every existing call site (Runtime,
   both example binaries, every image-regression fixture, every
   direct-call test) is updated to pass `0.0f` explicitly.
6. **`Renderer::drawFrame()` computes the exposure multiplier exactly
   once, in C++, per call** — `computeExposureMultiplier(ev) { return
   std::exp2(ev); }`, a small, named, unit-tested free function in a
   new private Renderer header — immediately after the Decision-4
   precondition check and before the RenderGraph is built. This is the
   **sole** implementation of the EV→multiplier mapping in production
   code; the shader performs no `exp2` of its own.
7. **The output-transform `Pipeline` gains a 4-byte, Fragment-stage-read
   push constant** (`ExposurePushConstants { float exposureMultiplier;
   }`, carrying the *already-computed* multiplier from Decision 6, never
   the raw EV), populated once per frame inside the existing
   "output_transform" RenderGraph pass's execute lambda via the
   existing, already-generic `CommandList::pushConstant()`/
   `PipelineCreateParams::pushConstantSizeBytes` mechanism — the same
   mechanism every `MaterialKind` `Pipeline` already uses for its own,
   larger push constant, applied here for the first time to the
   output-transform `Pipeline`. No RHI change.
8. **`compile_and_validate.cpp`'s exact push-constant contract for
   `output-transform-unorm`/`output-transform-srgb` is fixed by a real
   `slangc` compile, run during this ADR's own drafting (temporary
   files, never committed):** adding the final `ExposurePushConstants`
   declaration and confirming both output-transform `.slang` files
   compile cleanly under this repository's own toolchain shows the
   `pushConstantBuffer` binding present, identically, in **both** the
   vertex and fragment stage's own reflected JSON, at `{offset: 0,
   size: 4}`, with **no `"used"` field on either** (the same "a
   `pushConstantBuffer` entry carries no `used` field, present
   regardless of real per-stage usage" behavior `slang_json_transform.cpp`'s
   own top comment already documented for the unrelated PBR shader
   pair). `validatePushConstantsForVertexStage()`'s output-transform
   branch therefore moves from "expects empty" to "expects one stray,
   unread, `{offset:0, size:4, Vertex}` entry"; a fragment-stage check
   (mirroring the existing PBR-only `validatePushConstantsForFragmentStage()`)
   expects the real, genuinely-used `{offset:0, size:4, Fragment}`
   entry. Not left open for Plan-stage.
9. **Reinhard tone-mapping, the sRGB OETF, the HDR intermediate format,
   light-intensity semantics, and every `Material`/descriptor-binding
   contract are unchanged.** Only the value multiplied into
   `exposedColor` before Reinhard changes, from a fixed `1.0` literal to
   a computed multiplier; at `exposureCompensationEv = 0.0` that
   multiplier is exactly `1.0`, identically — confirmed by a real
   capture-compare run against all existing goldens at Implementation
   time, not asserted from the arithmetic alone.

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
- The EV→multiplier mapping has exactly one implementation
  (`computeExposureMultiplier()`), directly unit-testable without a
  GPU, called by both production code and its own GPU-independent test
  — no drift risk between a shader-side and a C++-side copy, because
  there is no shader-side copy.
- The push-constant contract (Decision 8) is fixed by a real compile
  now, not guessed or deferred — Plan-stage inherits a closed question,
  not an open one.
- The `[-16, +16]` authoring range makes "can this ever produce
  Inf/NaN" a closed, checkable question, independent of scene content,
  enforced at three independent points (cook, decode, and
  `drawFrame()`'s own precondition) — consistent with this codebase's
  existing NaN/Inf-avoidance discipline (ADR-0067 D-1, ADR-0068 D-5's
  own numeric-edge-cases note).

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
- `kExposureCompensationEvMin`/`Max` are necessarily duplicated (same
  values, independent definitions) in both Atlantis::AssetSystem and
  Atlantis::Renderer, because those modules must not depend on each
  other — a small, deliberate, comment-linked duplication, not a new
  shared-constants module (which this ADR does not introduce, matching
  AGENTS.md's "no speculative abstraction").
- Three separate Human Review approvals are required before
  Implementation (this ADR, Spec 0031, and ADR-0068's own new
  Amendment) rather than one — more governance overhead than a single
  approval, accepted here because AGENTS.md requires an `Accepted`
  ADR's text to never be silently rewritten, and this decision touches
  one such ADR's own Decision clause.

## Alternatives Considered

See Spec 0031's own Alternatives Considered section (camera uniform
buffer instead of push constant; required vs. optional scene-source
token; a dedicated new error enumerator instead of reusing
`NonFiniteValue`; alternative EV-range framings and values) —
reproduced there rather than duplicated here, since every alternative
was rejected using evidence gathered while answering this Spec's own
Requirements, not a separate architectural trade-off this ADR needs to
re-litigate independently.

One additional, ADR-scoped alternative: **editing
[ADR-0068](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
D-10's own text in place**, instead of appending a new `Proposed
Amendment` section to it. Rejected — this repository's own established
convention (ADR-0072, ADR-0073) is append-only for an `Accepted` ADR;
editing D-10's historical text in place would erase the record of what
was actually decided in 2026-09-01 and why, which AGENTS.md's own "ADRs
are... not silently rewritten" rule exists to prevent.

A second alternative: **folding the D-10 amendment directly into this
ADR (ADR-0075) instead of a separate Amendment section on ADR-0068**.
Rejected — this ADR is a new, `Proposed` document; ADR-0068 is
`Accepted`. Recording the D-10 change here alone would leave ADR-0068's
own text describing a contract ("no push constant") that Implementation
would silently make false, discoverable only by reading ADR-0075 and
noticing the connection — worse for a future reader than ADR-0068
carrying its own, explicit, cross-referenced amendment.
