# ADR 0088: FrameLightingData Successor Structure, Binding Strategy and Point-Light Capacity

- **Status:** Proposed
- **Date:** 2026-09-21
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0040](../specs/0040-multi-light-architecture.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
fixed `FrameLightingData` as a fixed-size, uniform-buffer-friendly struct when
the engine's lighting need was one directional and a handful of point lights.
It holds `directionalLights[1]` and `pointLights[4]` in 176 bytes, 32 bytes
per light, with every offset pinned by `static_assert`
(`src/runtime/include/atlantis/runtime/scene_extraction.h:77-122`). The cap is
structural, not conventional: two `.scene.txt` gates reject a fifth point
light (`scene_source.cpp:356`, `scene_artifact.cpp:281`) and
`extractFrameLightingData()` treats one reaching it as a programmer error
(`scene_extraction.cpp:303-308`).

[Spec 0036](../specs/0036-bistro-parity-roadmap.md) workflow ② requires more,
and deliberately left the structure open, naming two options and recording
Filament's froxel ceiling ("256 lights max", `filament/src/Froxelizer.h`) as
calibration data rather than as a decision (`0036:347-386`). It assigned this
workflow three coupled obligations: the successor structure, its binding
strategy (uniform versus storage), and the resulting shader-side iteration
cost (`0036:382-386`, `:749`).

The forces, as measured by Spec 0040's own investigation rather than assumed:

- **The uniform budget is not the constraint it is often taken to be.**
  Vulkan guarantees `maxUniformBufferRange >= 16,384`. The frame uniform's
  non-point-light content is 464 bytes, so **497 point lights fit in a single
  uniform buffer on any conformant device**. Even Filament's 256 fits, at
  52.8% of the guaranteed floor. The measured desktop device reports
  134,217,724 and imposes nothing.
- **Storage buffers are available and are not the deciding factor.** An SSBO
  read in a fragment shader is core Vulkan 1.0; no feature bit is needed. The
  choice is therefore about fit, not capability.
- **There is no measured light-count requirement to satisfy.** Spec 0037
  Investigation 1 established that the recommended Bistro glTF defines **zero
  lights** (`docs/specs/0037-gltf-importer.md:258`), and that Filament's own
  bistro is a separately modified file whose authors added lights by hand.
  Every Bistro light will be hand-authored, so capacity is an authoring-
  headroom judgement, not a fact to be read off the asset.
- **The shader surface is wider than the iteration surface.** Eleven `.slang`
  files declare the camera uniform block inline; only five iterate the point
  lights. An array-extent change touches all eleven regardless.
- **Per-pixel cost already tracks the real light count**, not the array
  extent: every loop is bounded by `camera.pointLightCount`
  (e.g. `pbr_direct_lit.slang:200`). Widening the declared array costs nothing
  per pixel in a scene with three lights.
- **Proportionality.** AGENTS.md's Phase 1 constraints and this repository's
  consistent practice favour the smallest change that meets the real need,
  with the larger option named rather than silently foreclosed.

## Decision

**`FrameLightingData`'s successor is the same structure with a wider point-
light array: a fixed cap of 64 point lights, bound in the same single uniform
buffer, with the existing dynamic shader loop unchanged.**

1. **Structure: widened fixed array, not clustered/froxel.** The struct keeps
   its identity — the 32-byte `PointLightGpu` and `DirectionalLightGpu`
   elements, their field offsets, their std140 alignment and the count header
   at offsets 0/4 are all unchanged. Only `pointLights`' extent changes, from
   4 to 64, taking the struct from 176 to 2,096 bytes. No spatial structure,
   no per-frame froxelization, no new RenderGraph pass.
2. **Capacity: N = 64 point lights; directional stays 1.** The basis is
   recorded rather than asserted: the reference image's "dozens" of authored
   emitters (24–48) plus at least 1.3× headroom; 13.0% of the 497-light
   single-UBO capacity the Vulkan guarantee allows; a quarter of Filament's
   froxel cap; a power of two occupying exactly 2,048 bytes. The directional
   cap stays 1 because the shadow path, the light-space tail and
   `computeShadowLightSpaceMatrices()` each assume exactly one — widening it
   is a shadow decision, not a capacity one.
3. **Binding: one uniform buffer, unchanged.** No storage buffer, no second
   descriptor, no change to ADR-0062's stage-visibility contract. At N = 64
   the whole frame uniform is 2,512 bytes, 15.3% of the guaranteed floor.
4. **Capacity is one constant with four consumers.** `kMaxPointLights` is
   defined once and applied at both `.scene.txt` gates, at
   `extractFrameLightingData()`'s `ATLANTIS_CHECK_MSG`, and in the shader
   array extent. Exceeding it stays a `TooManyLights` error at the two real
   gates and a programmer error at the extraction cap — the cap's *kind* does
   not change, only its threshold.
5. **Shader-side iteration cost is unchanged.** The loops stay bounded by
   `camera.pointLightCount`, so per-pixel cost remains proportional to the
   lights a scene actually has. All eleven declaring shaders are updated for
   layout; the five iterating ones change no logic.
6. **The frame uniform buffer's size is derived from the layout constants**,
   not written as a literal. This is a correctness requirement, not a style
   preference: the Runtime currently allocates 464 bytes for a 592-byte layout
   (Spec 0040 Investigation 4), and widening the array moves that tail
   further.

This ADR does **not** decide what a light's intensity value means physically,
nor the attenuation model — see Spec 0040 Open Question O5, which asks for
that obligation (assigned here by Plan 0037 Ruling 8) to be reassigned.

## Consequences

### Positive

- **Small, reviewable, reversible.** The diff is one constant, eleven array
  extents, a derived buffer size, and a new scene with its golden. Changing
  the capacity later is one number.
- **Portable by a wide margin.** The design fits the Vulkan-guaranteed
  16 KB uniform range 6.5× over, which matters because Android is a primary
  target and no Android device's limit was measured.
- **No new architectural surface.** No RenderGraph pass, no second buffer
  binding, no new descriptor, no change to ADR-0062 or ADR-0023.
- **No per-pixel cost for scenes that do not use the capacity.** A
  one-light scene renders exactly as it does today, which is also why every
  existing golden must stay byte-identical — a strong regression signal.
- **The element layout is preserved**, so every existing offset
  `static_assert` keeps its meaning and the Slang reflection cross-check that
  validated the original layout still applies.
- **It forces a latent defect into the open.** Deriving the buffer size fixes
  a 128-byte out-of-bounds write the Runtime has been performing every frame.

### Negative / Trade-offs

- **Silent truncation is not eliminated, only re-thresholded.** A scene
  needing 65 point lights fails; it does not degrade gracefully. That is a
  better failure than dropping lights, but it is still a ceiling, and this
  ADR does not pretend otherwise.
- **2,048 bytes of point-light array are uploaded every frame regardless of
  how many lights are lit** — 11.6× today's light payload for a scene with one
  light. At ~151 KB/s it is not a measurable cost, but it is real waste and it
  is the price of a fixed array.
- **Eleven shaders must stay in lockstep with one C++ header**, by hand. The
  duplication already exists and is already disclosed
  (`scene_extraction.h:182-189` records the same class of risk for
  `kPointLightDistanceEpsilon`), but a wider array raises the cost of missing
  one.
- **The capacity rests on an estimate, not a measurement**, because the target
  asset defines zero lights. If the estimate is wrong, N must change and the
  goldens recapture.
- **Future scenes that genuinely need hundreds of simultaneous local lights
  will need this decision reopened**, and at that point the froxel work is
  larger than it would have been to do now.

## Alternatives Considered

- **Froxel/clustered light structure (Filament's approach).** The option Spec
  0036 named alongside this one. Rejected for this round on proportionality,
  with the full quantified comparison in Spec 0040's Architectural Impact
  section: it requires a per-frame spatial structure, at least two storage
  buffers, shader-side indirection and most likely a new RenderGraph pass —
  and its real advantage, per-pixel cost that does not grow with total light
  count, only begins to pay at counts far beyond what a single uniform buffer
  already holds. Rejected on cost/benefit, never on capability, and Spec
  0040's Future Work records the two measurements that would reopen it.
- **Storage buffer with a runtime-sized light array.** Would remove the fixed
  cap entirely and end the truncation question. Rejected because the uniform
  budget is not binding at this capacity, so it would add a descriptor, a
  binding in eleven shaders and a change to ADR-0062's contract to solve a
  problem that does not exist at N = 64. It becomes the right answer at the
  same threshold that would justify froxels.
- **A dynamically sized, reallocated uniform buffer.** Trades 2 KB for buffer
  lifetime management across frames in flight, against ADR-0023's stateless
  creation model, and reopens the write-timing question Spec 0022 settled via
  the existing acquire-time drain. Rejected.
- **N = 8 or 16.** Cheaper, and adequate for anything in the repository today,
  but near-certain to need revisiting during workflow ⑦ — the sequencing
  mistake Spec 0036 exists to prevent.
- **N = 256, matching Filament's froxel cap.** Fits the guaranteed floor at
  52.8% and is defensible. Rejected as headroom nothing in the roadmap asks
  for, at 4× the per-frame upload; the constant is trivially raised if
  workflow ⑦'s authoring proves 64 short.
- **CPU-side importance sorting, uploading the best N lights.** Would turn the
  over-capacity case from a hard error into graceful degradation. Rejected as
  a culling policy, which Spec 0040's Non-Goals exclude wholesale; recorded as
  Future Work rather than dismissed.
- **Splitting the light array into its own uniform buffer**, leaving the
  camera buffer untouched. Would avoid moving the light-space tail and would
  make the two lifetimes independent. Rejected: it adds a binding to eleven
  shaders and a descriptor to every pipeline in order to avoid a layout move
  that is a mechanical, statically-asserted change — and the buffer-size
  derivation (Decision item 6) is needed either way.
