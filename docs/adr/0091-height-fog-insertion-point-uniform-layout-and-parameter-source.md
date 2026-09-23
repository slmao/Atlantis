# ADR 0091: Height Fog — Insertion Point, Uniform Layout and Parameter Source

- **Status:** Proposed
- **Date:** 2026-09-24
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0043](../specs/0043-height-fog.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

[Spec 0036](../specs/0036-bistro-parity-roadmap.md) workflow ⑤ owes an ADR
for where fog blending happens: "a per-pixel term added to every existing
forward fragment shader … vs. a depth-reconstruction-based RenderGraph
post-process pass" (`0036:533-538`, `:803`). Choosing the insertion point
also fixes where the fog parameters live in GPU memory and where they
come from, so the three are decided together.

Facts this ADR rests on, measured by Spec 0043:

- **The frame has three passes today** — shadow, draw, output transform
  (`src/renderer/src/renderer.cpp`, ADR-0068 D-3, ADR-0072). The draw pass
  writes the `R16G16B16A16_SFLOAT` `HdrColorTarget`; the output-transform
  pass samples only that target and pushes a 4-byte exposure constant.
  Its descriptor contract is one sampler and **no uniform buffer**
  (ADR-0068 D-10; `hasCameraUniformBinding = false` at all 20 sites that
  create an output-transform Pipeline).
- **The scene depth buffer cannot be sampled.** The depth `Texture` is
  created with `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` only
  (`vulkan_device.cpp:890`), and the RHI `Texture` has no sampled-bind
  overload and no `ShaderRead` state. The one sampleable depth resource is
  the separate `ShadowMap` type (`:1642`, ADR-0072), which proves the
  barrier (`depthAttachmentReadWriteToShaderRead`) but is not the scene
  depth.
- **Every PBR fragment already has what height fog needs.** All ten PBR
  shaders interpolate `worldPosition` and declare the full `CameraUniform`
  block through its 2512-byte tail, including `cameraWorldPosition` at
  2224–2239. The push-constant blocks cannot take more: `PbrSheen` is at
  exactly 128 bytes, and the other three have no free tail after Spec 0042.
- **Blended draws do not write depth** (ADR-0090 Decision 3). Anything
  reconstructing position from depth sees the surface behind a
  transparent one.
- **⑥ Bloom will insert its pass(es) between the draw pass and the output
  transform** (Spec 0036 ⑥), and a bloom bright-pass should see fogged
  radiance.
- **Per-scene look parameters already live in the scene file.** Spec 0031
  put `camera_exposure_ev=` on the camera node (scene source v4); the
  environment, by contrast, is one `BootstrapConfig` path shared by every
  `--scene` entry (`src/runtime/main.cpp:125`). Filament also treats fog
  as a per-view option (`FogOptions` on `View`).

## Decision

1. **Insertion point — a per-fragment term in the ten PBR fragment
   shaders, in HDR space.**
   - Applied after the emissive term and before the return, to RGB only:
     `rgb = lit * (1 − f) + fogColor * f`, where `f` is Decision 2's fog
     factor. Alpha is unchanged. The Spec 0042 `discard` still comes first.
   - Not applied in `lit_textured`, `textured_quad`, `sky`, `shadow_cast`
     or the output-transform shaders.
   - **No RenderGraph, RHI, Pipeline or `drawFrame()` change.** The fogged
     radiance is in the `HdrColorTarget` before any later pass reads it,
     so ⑥'s passes see fog by construction.
2. **The fog model — analytic exponential height fog**, Filament's
   parameter shape (reference only; no Filament value is adopted). For a
   fragment at world position `p` seen from camera `c`:
   - `d = |p − c|`, `Δy = p.y − c.y`, `h = c.y − fogHeight`;
   - optical depth `τ = density · d · e^(−heightFalloff · h) · g(heightFalloff · Δy)`,
     with `g(x) = (1 − e^(−x)) / x` and `g(0) = 1`;
   - `f = min(1 − e^(−τ), maxOpacity)`.

   **`density == 0` means disabled.** The shader skips the term on that
   uniform condition, so the output is the unfogged value bit for bit.
   This keeps every existing golden byte-identical and avoids a `0 · ∞`.
3. **The parameters live in a new `FogData` tail on the `CameraUniform`
   block, not in push constants.**
   - 32 bytes at offset 2512: `float3 color`, `float density`,
     `float height`, `float heightFalloff`, `float maxOpacity`,
     `float _pad`.
   - The buffer grows from 2512 to 2544 bytes.
   - A C++ struct is added beside `CameraWorldPositionData`, with offset
     `static_assert`s. The 11/11 `CameraUniform` reflection cross-check is
     extended to the 10 PBR shaders that reach the tail.
   - Every composition root that writes the camera uniform writes
     `FogData` every frame. A frame with no fog writes density 0; the tail
     is never left uninitialised.
4. **The parameters come from the scene file, on the camera node.**
   - The camera node takes optional fog tokens, beside
     `camera_exposure_ev=` (scene source v4 → v5, with the matching
     artifact and metadata bumps).
   - They are carried on World's `Camera` component as plain data (the
     `exposureCompensationEv` precedent). The Runtime extracts them per
     frame from the active camera into `FogData`.
   - Absent tokens mean density 0, which is off.
   - The exact token grammar and the range limits are Spec 0043's.

## Consequences

### Positive

- **Smallest architectural surface of the three options.** No new pass,
  Pipeline, RHI type, resource state, descriptor contract or `drawFrame()`
  parameter. ADR-0068's three-pass structure and its D-10 contract are
  untouched.
- **Correct for transparent surfaces.** Each blended fragment is fogged at
  its own distance before blending; a depth-based pass would fog it at the
  distance of whatever is behind it.
- **Bloom-ready.** Fog lives in HDR before any post pass, so ⑥ needs no
  fog-specific ordering rule beyond "insert after the draw pass" — which
  its own ADR obligation already assumes.
- **Byte-identical regression signal.** Density 0 is an exact identity.
- **Per-scene and per-test control.** Fog follows the scene: Bistro gets
  fog without every other `--scene` entry inheriting it, and a fixture can
  set it on the World's camera for a parameter sweep.

### Negative / Trade-offs

- **Ten shaders change, again.** The same fog function is duplicated in
  all ten PBR variants, the Spec 0041/0042 precedent. The reflection
  cross-check is what keeps the declarations aligned.
- **Fog is evaluated per shaded fragment, not per pixel.** Overdrawn
  fragments pay for it too. The cost is two `exp` and a handful of ALU per
  fragment, and nothing when fog is off.
- **Unfogged surfaces:** the sky, `lit_textured`/`unlit_textured`
  materials and the clear colour. A fogged horizon would need the sky
  shader, or a pass, to take part — deferred.
- **A scene-format bump** (v4 → v5), rippling through every `.scene.txt`
  version line and World's `Camera` component.
- **Transparent layers slightly double-count fog colour:** the fog in front
  of a blended surface is applied to it and again to what lies behind.
  This is the standard forward-fog approximation.

## Alternatives Considered

- **A post-process pass that reads HDR and depth, reconstructs world
  position and writes a fogged copy.**
  - Needs a sampleable scene depth (new usage flag, `ShaderRead` state and
    `bindTexture` overload on `Texture`) and a second `HdrColorTarget`
    (the single-producer rule forbids writing the one it reads).
  - Needs inverse camera matrices, a new Pipeline at every composition
    root, and new `drawFrame()` parameters across 38 call sites.
  - Fogs blended surfaces at the wrong depth and fogs the sky unless it is
    masked.
  - Its one real advantage — once-per-pixel cost — is not a Phase 1
    concern. Rejected.
- **A term inside the output-transform shader.**
  - Same depth and inverse-matrix surface as the pass, with no new pass.
  - Changes ADR-0068 D-10's descriptor contract at 20 Pipeline sites,
    which needs a superseding ADR.
  - Fogs after any bloom pass would have read the HDR target, inverting
    the fog → bloom order ⑥ needs.
  - Same transparent-surface error. Rejected.
- **Fog parameters in push constants.** There is no room: `PbrSheen` is at
  the 128-byte limit. Rejected.
- **Parameters from `BootstrapConfig`.** It is global to the executable, so
  every whitelisted scene would get Bistro's fog, and golden sweeps would
  need code changes. Rejected.
- **A new scene-level fog line or a new World component.** Heavier than a
  few camera-node tokens and further from Filament's per-view model.
  Recorded as Spec 0043's open question Q2.
- **An implicit "fog enabled" flag.** Redundant with density 0, which
  already disables the term exactly. Rejected.
