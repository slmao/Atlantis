# Spec: Bloom

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-24
- **Related Plan(s):** none yet — Plan 0044 drafting is authorized by the
  Approval below. **Implementation still awaits its own, separate Joint Human
  Review** of Spec + Plan together, per AGENTS.md's own workflow.
- **Approval:** slmao, 2026-09-24 (chat confirmation, no reviewing PR —
  authorizes drafting Plan 0044; Implementation itself still awaits its own,
  separate Joint Human Review of Spec + Plan together). The same review ruled
  all five open questions. See Risks & Open Questions below.
- **Related ADR(s):** [ADR-0092](../adr/0092-bloom-pass-insertion-blur-strategy-targets-and-parameter-source.md)
  (`Accepted` 2026-09-24, alongside this Spec's own Approval) — the pass
  insertion point, the blur strategy, the intermediate targets and the
  parameter source.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Make bright HDR sources — Bistro's string lights and bulbs, specular
glints, lamp-lit haze — glow. This is [Spec 0036](0036-bistro-parity-roadmap.md)
workflow ⑥ and the engine's first post-process pass beyond ADR-0068's
draw → output-transform pair. After the draw pass, a bright-pass keeps
what exceeds a threshold; a six-level downsample/upsample chain blurs it;
and a composite pass adds `strength ×` the blur back onto the HDR image.
The output transform then reads that composite instead of the raw HDR
target, with its own descriptor contract unchanged. Parameters come from
optional tokens on the scene's camera node, the ⑤ precedent. Absent
bloom declares no bloom pass at all, so every existing golden stays
byte-identical. ADR-0092 records the four coupled decisions.

## Motivation / Problem Statement

Spec 0036 ⑥ (`0036:555-587`) records the ceiling: two passes, a
geometry pass writing the `HdrColorTarget` and one output transform
reading it, with bloom a named Non-Goal of Specs 0024–0027. Energy above
1.0 already exists and is lost to the tone curve alone: Spec 0041 made
emissive factors HDR (Bistro's string lights sit at 8–20, bulbs at
30–100), and Spec 0043 made fog colour HDR (ruling Q7). Bistro's night
reference reads its lamps through their halos. ⑥ follows ②/③ (it needs
bright sources) and ⑤ (it must see fogged radiance, the constraint
Spec 0043 recorded at `0043:256-270`).

## Goals

- Visible halos around HDR sources above a threshold, with a wide,
  smooth falloff (a tight core and a broad haze from one effect).
- Fog is bloomed like any other radiance: a haze brighter than the
  threshold glows.
- Bloom off is exact: no pass declared, the frame bit-identical to today.
- No RHI, Vulkan Backend or RenderGraph change.
- Per-scene control, from the scene file.

## Non-Goals

- **Dirt / lens-dirt texture** (Filament `dirt`, `dirtStrength`).
- **Lens flare, ghosts, halo ring, starburst, chromatic aberration**
  (Filament's `lensFlare` family).
- **Anamorphic streaks** or any direction-dependent blur.
- **Interaction with auto-exposure.** There is no auto-exposure; the
  fixed `camera_exposure_ev` applies after the composite (Requirement 6).
- **`INTERPOLATE` blend mode.** Only Filament's default, `ADD`.
- **Bloom on the sky or clear colour as a special case.** They are HDR
  pixels like any other: bloomed if above the threshold, not otherwise.
- **An authored level count, resolution, or highlight clamp.** Fixed in
  the Renderer (ruling Q3).
- **Compute-shader blur.** The RHI has no compute path; adding one is its
  own Spec.

## Requirements

### Functional

1. **Where it runs (ADR-0092 Decision 1).**
   - Between the draw pass and the output transform, as ordinary
     RenderGraph passes in `Renderer::drawFrame()`'s one graph.
   - The bloom passes read `hdr_color` after the draw pass has written
     it, so they see fogged radiance; they never touch fog or the draw
     pass.
   - The output transform reads the composite target when bloom is on,
     `hdr_color` when it is off. Its shader pair and ADR-0068 D-10
     descriptor contract (one sampler) are unchanged.
2. **The chain (ADR-0092 Decision 2).** With `L = 6` levels:
   - **Downsample** `D1…D6`: `D1` at half the HDR extent, each next level
     half the previous, each dimension clamped to ≥ 1. `D1` reads
     `hdr_color` and applies the bright-pass (Requirement 3); `D2…D6`
     each read the level above.
   - **Upsample** `U5…U1`: `U_i = D_i + upsample(U_{i+1})`, with
     `U_6 ≡ D6`.
   - **Composite:** at the full HDR extent,
     `rgb = hdr + strength · upsample(U1) / L`, alpha = `hdr` alpha.
     Dividing by `L` keeps `strength` independent of the level count.
   - The downsample and upsample filters are the Plan's (a 13-tap
     downsample and a 9-tap tent upsample are the reference shape).
3. **Bright-pass (at `D1` only).** Per channel,
   `bright = max(c − threshold, 0)`, then highlight compression
   `bright · 1 / (1 + max3(bright) / 1000)`. This is Filament's formula
   with the knee as a parameter (Filament hard-codes 1.0) and its fixed
   `highlight = 1000`. A firefly weight `1 / (1 + max3)` on the `D1`
   taps is the Plan's to confirm.
4. **Parameters (ADR-0092 Decision 4).**
   - Two, on the camera node in `.scene.txt`: `strength` and `threshold`.
   - Absent means bloom off. `strength == 0` also means off.
   - They sit after the fog group, as one prefixed group in the `fog=`
     style (ruling Q1); the exact token spelling is the Plan's.
   - They travel as plain data through the scene source/artifact (v5 →
     v6), World's `Camera`, and the Runtime, to `drawFrame()`.
5. **Validation, at cook time and again on decode.**
   - `strength`: finite, in `[0, 1]` (Filament's range).
   - `threshold`: finite, `≥ 0`.
6. **Exposure.** The output transform's exposure multiply (Spec 0031)
   applies after the composite, so bloom scales with the scene. The
   threshold is therefore in scene-referred units, before exposure
   (ruling Q4).
7. **Exact off, and exact below-threshold.**
   - Bloom off declares no bloom pass; the graph and every pixel equal
     today's.
   - Bloom on with every pixel at or below the threshold makes every
     level exactly 0, and `hdr + strength · 0 / L` is `hdr` bit for bit.
8. **Resize.** The bloom targets follow the HDR target: recreated in the
   same resize branch (`runtime_application.cpp:1194-1221`), with the same
   keep-and-retry failure handling.

### Non-functional

- **Passes:** 12 when on (6 down, 5 up, 1 composite); 0 when off.
- **Memory** (`Rgba16Float`, 8 B/px): the down chain is ≈ 1/3 of the
  HDR target and the up chain the same, plus one full-size composite —
  ≈ 1.67× the HDR target. At 1920×1080: ≈ 27.6 MB (16.6 MB of it the
  composite). At the 512×512 fixtures: ≈ 3.5 MB.
- **Bandwidth/ALU:** ≈ 3.45 M pixels shaded at 1080p (1.38 M in the
  chain, 2.07 M in the composite), ~10–13 texture taps each. The
  composite dominates.
- **Portability:** `R16G16B16A16_SFLOAT` with
  `SAMPLED_IMAGE_FILTER_LINEAR` is a mandatory Vulkan format feature, so
  the linear-filtered chain runs on every conformant device, Android
  included — the same reliance the output transform's linear sampler
  already has today. `hasRequiredHdrColorTargetFeatures()` checks only
  attachment + sampled (`hdr_color_target_capability.cpp:5-9`) and is
  left unchanged.
- **Determinism:** fixed pass order, no atomics, no temporal state.

## Pre-drafting Investigation (required conclusions, cited against real files)

### Investigation 1 — the `HdrColorTarget` and the output transform today

- **Created per frame-size, sampleable, single-mip.**
  `HdrColorTargetCreateParams` is `{extent, format = Rgba16Float}`
  (`src/rhi/include/atlantis/rhi/types.h:236-239`). The image has
  `mipLevels = 1` and usage `COLOR_ATTACHMENT | SAMPLED`
  (`vulkan_device.cpp:1545,1551`). So it can be sampled, but it has no
  mip chain, and the RHI exposes no per-mip view.
- **The output-transform pass** reads `hdr_color` as `ShaderRead`,
  writes `final_color`, pushes the 4-byte exposure multiplier, and binds
  the target at binding 0 through a linear, clamp-to-edge sampler
  (`renderer.cpp:295-309`; sampler `runtime_application.cpp:838-839`).
- **Its contract is exactly one sampler, no uniform buffer**
  (ADR-0068 D-10, `hasCameraUniformBinding = false`). Output-transform
  Pipelines are created in **22 files**. `drawFrame()` has **38 call
  sites in 24 files**.
- **Consequence:** bloom needs its own Pipelines and its own targets.
  Folding its composite into the output transform would change D-10 and
  all 22 Pipeline sites (Investigation 3, option (i)).

### Investigation 2 — RenderGraph mechanics

- **Declaration.** `drawFrame()` declares four resources and three passes
  on one builder (`renderer.cpp:94-118,295-297`), compiles once
  (`:314`), binds (`:316-339`) and executes once (`:340`). The builder is
  already N-pass (ADR-0068 D-3); order comes from each resource's
  write-then-read, and the single-producer rule allows one writer per
  resource.
- **Barriers.** `execute()` tracks each resource's state from
  `Undefined` every frame and records a transition only when the state
  changes (`execution.cpp:128-146`). So **`ShaderRead → ShaderRead`
  records nothing**: bloom's `D1` and the composite can both read
  `hdr_color` with no extra barrier, the multi-reader case the brief
  asks about.
- **The transition table has no `ShaderRead → ColorAttachmentOutput`**
  (`resource_state_mapping.cpp:195-217`). A target read earlier in the
  frame cannot be written again. So each chain level must be its own
  resource, written once then read: `Undefined → ColorAttachmentOutput →
  ShaderRead`, both of which exist.
- **One color attachment per pass**, chosen from the pass's
  `ColorAttachmentOutput` usage; a pass may be bound to an
  `hdrColorTarget` (`execution.cpp:153-193`).
- **Everything a bloom pass needs already exists:**
  - viewport and scissor follow the attachment's own extent
    (`vulkan_command_list.cpp:287-304`), so smaller targets just work;
  - `bindTexture(binding, const HdrColorTarget&, ...)` takes any binding
    index (`command_list.h:146`);
  - a Pipeline may omit the camera uniform and take up to five samplers,
    which then start at binding 0 (`vulkan_device.cpp:1023-1025,
    1083-1092`), with `HdrFormat` color and `hasDepthAttachment = false`
    (`types.h:302,352`).
- **Conclusion:** bloom is new passes, resources and Pipelines — no RHI,
  Vulkan Backend or RenderGraph change.

### Investigation 3 — blur strategy (quantified)

Counted for the recommended placement (separate composite pass), at
1920×1080, `Rgba16Float`. "Taps" are texture samples per output pixel.

| | (a) Mip-chain down/up, L = 6 | (b) Separable Gaussian, fixed ¼ res | (c) Single-pass large kernel |
|---|---|---|---|
| Passes | 12 (6 down, 5 up, 1 composite) | 4 per iteration (bright, H, V, composite) | 2 (bright+blur, composite) |
| Pipelines | 3 (down, up, composite) | 3 (bright, blur H/V by push constant, composite) | 2 |
| Intermediate targets | 12 (6 down, 5 up, 1 full-size) | 3 + 2 per extra iteration (no rewrite, Inv. 2) | 2 |
| Memory | ≈ 27.6 MB (1.67× HDR) | ≈ 19.7 MB (1.19× HDR) | ≈ 17.6 MB (1.06× HDR, ¼-res blur) |
| Blur radius | ~2⁶ px at ½ res ≈ 128 px full-res and beyond, wide tail | ~13-tap ≈ 26 px full-res; wider costs linearly | any, at O(R²) taps |
| Cost for a ~128 px halo | ≈ 3.45 M px × ~10–13 taps | ≈ 5 iterations or a 65-tap kernel | ≈ 4,000+ taps/px — prohibitive in a fragment shader |
| Look | multi-scale: tight core + broad haze (Filament, UE, CoD) | one scale: a narrow glow or a blocky wide one | one scale |
| Implementation | 3 small shaders; a target-chain owner | 3 small shaders | 1 shader, but a compute path needs new RHI |
| Precedent | Filament `levels = 6` | classic | — |

**Recommendation: (a).** It is the only option with Bistro's wide
lamp haze at a bounded, resolution-proportional cost. It is Filament's
own shape (a reference, not an adopted value). Its larger target count
is bookkeeping, not architecture: the targets are ordinary
`HdrColorTarget`s owned by one bundle (Decision 3).

### Investigation 4 — where the bright-pass runs, and Filament's threshold

- **Filament's `threshold` is a boolean, not a value** (`Options.h`
  `BloomOptions`: `threshold = true`, `highlight = 1000.0f`,
  `strength = 0.10f`, `levels = 6`, `resolution = 384`). When on, its
  first 2× downsample computes `c = max(c − 1.0, 0)` then
  `c *= 1 / (1 + max3(c) · invHighlight)` (`bloomDownsample2x.mat`). Its
  knee is fixed at 1.0.
- **Placement: inside `D1`** — the first downsample, before any blur. It
  costs nothing extra and the chain then carries only bright energy.
  Before `D1` would need a full-size extra pass; after it would blur
  sub-threshold energy into the halo.
- **This Spec keeps Filament's formula but makes the knee a parameter**
  (default 1.0 in the golden scenes, not an adopted Bistro value), so a
  night scene with emissive at 8–100 can raise it above glints.

### Investigation 5 — target sizes

- **The `HdrColorTarget` is the window size in the Runtime**, recreated
  on every extent change (`runtime_application.cpp:1194-1221`), and
  512×512 in the image-regression fixtures.
- **Bloom levels are relative to it:** `D1` at ½, halving to `D6` at
  1/64 (30×16 at 1080p, 8×8 at 512²). Halo size is then a fixed fraction
  of the screen at any resolution, and the goldens are cheap.
- **Filament fixes the first level's height at 384** instead; relative
  sizing is chosen here because the fixtures are 512² and the Runtime
  resizes (Q3).

### Investigation 6 — what Bistro needs

- **Sources** (Spec 0041, `0041:251-260`): 21 materials with a non-zero
  emissive factor, max component 100. The importer maps the factor only
  for the **10 texture-less string lights and bulbs** (8–20 and 30–100);
  the 11 textured signs are dropped until an emissive texture lands.
  So ⑥'s visible sources today are those lamps, plus specular glints and
  sky above the knee.
- **Magnitude:** with a knee of 1 and Filament's `strength = 0.1`, a
  factor-20 bulb puts ≈ 19 × 0.1 / 6 ≈ 0.3 of its excess into the halo
  energy before spreading. That is visible, but not blinding. Bistro's
  own tuning is ⑦'s, against its reference image.
- **Fog:** a lamp-lit haze whose HDR fog colour (Q7 of Spec 0043) exceeds
  the knee blooms too — atmospheric glow, intended. Fog below the knee
  does not.

### Reference: Filament `BloomOptions` (shape only)

`strength = 0.10`, `levels = 6` (1–11), `resolution = 384`,
`threshold = true`, `highlight = 1000`, `blendMode = ADD`,
`dirtStrength = 0.2`, plus lens-flare fields. Adopted as shape: `ADD`,
six levels, the knee formula and highlight constant. Not adopted as
Bistro values; not adopted: dirt, lens flare, fixed 384 resolution,
`INTERPOLATE`.

## Proposed Design

1. **Renderer.** A caller-owned RAII bundle, `renderer::BloomResources`
   (name the Plan's), holds the 12 targets and a linear clamp sampler,
   and borrows the three bloom Pipelines, which the composition root
   creates like every other Pipeline (ruling Q5). It
   is created from a `Device`, an extent and the three shader pairs, and
   recreated on resize.
2. **`drawFrame()`** gains one trailing optional parameter carrying the
   bundle and `{strength, threshold}` — `nullptr`/absent or
   `strength == 0` declares no bloom pass, so none of the 38 call sites
   changes (the Plan 0042 `cameraWorldPosition` precedent).
3. **Graph when on:** `draw` → `bloom_down_1..6` → `bloom_up_5..1` →
   `bloom_composite` → `output_transform`. Each writes one new resource;
   `bloom_down_1` and `bloom_composite` also read `hdr_color`;
   `output_transform` reads `bloom_composite`'s target.
4. **Shaders:** three new `.slang` files (downsample with the bright-pass
   switch, upsample-add, composite) with new descriptor contracts in the
   Shader System: one or two samplers, no uniform buffer, a small push
   constant block (texel size, threshold, strength, weights).
5. **Scene/World/Runtime:** camera-node bloom group, scene v5 → v6,
   `DecodedCamera`/World `Camera` trailing members (the ⑤ pattern), and
   the Runtime passes the active camera's values each frame. The fixtures
   that exercise bloom create the bundle; the rest do not.
6. **Tests:** a `bloom_reference.h` (the bright-pass and level-extent
   maths), bloom goldens, and the fog + bloom combination.

## Architectural Impact

**Yes — [ADR-0092](../adr/0092-bloom-pass-insertion-blur-strategy-targets-and-parameter-source.md)
(`Accepted` 2026-09-24).** It records four coupled decisions:

1. **Insertion:** separate passes between draw and output transform,
   with a separate composite pass; the output transform reads the
   composite. ADR-0068 D-10 is untouched.
2. **Strategy:** a six-level downsample/upsample chain starting at half
   resolution.
3. **Targets:** twelve `Rgba16Float` `HdrColorTarget`s in one
   caller-owned Renderer bundle, recreated on resize; a new optional
   trailing `drawFrame()` parameter.
4. **Parameters:** camera-node scene tokens (scene v5 → v6), absent =
   off.

Public API changes: `drawFrame()` (additive, defaulted), a new Renderer
type, new Shader System descriptor contracts, the scene format. No RHI,
Vulkan Backend, RenderGraph or Platform change.

## Alternatives Considered

- **Composite inside the output-transform shader (Filament's shape).**
  Saves the full-size composite target (16.6 MB at 1080p) and one pass.
  But it changes ADR-0068 D-10's one-sampler contract, which is
  `Accepted` and needs a superseding ADR. It also touches all 22
  output-transform Pipeline sites, and forces a bound bloom texture (a
  dummy when off) at all 38 `drawFrame()` calls. Rejected for this
  Spec; a later cost-driven Spec can revisit.
- **Additive blending onto the down chain (Filament's upsample).**
  Halves the target count. But it needs a new `ColorBlendMode::Additive`
  and a `ShaderRead → ColorAttachmentOutput` transition — RHI and
  Backend changes. Rejected: separate `U` targets cost ≈ 5.5 MB at 1080p
  and no API.
- **A real mip chain on one image.** It needs per-mip image views and
  per-mip render/sample in the RHI. Rejected for the same reason.
- **(b) and (c)** — Investigation 3.
- **Parameters from `BootstrapConfig`.** Global per executable; every
  `--scene` would get Bistro's bloom. Rejected, the ⑤ reasoning.

## Testing & Verification Plan

Spec 0036 ⑥ is the contract: goldens proving a bright source produces a
visible halo; Validation Layers clean.

- **GPU-independent** (`atlantis_image_regression_tests` or the Renderer's
  own tests):
  - `bloom_reference.h`: bright-pass (below knee → 0 exactly; above →
    `c − threshold`; highlight compression; threshold 0);
  - level extents (halving, ≥ 1 clamp, 1×1 and odd sizes, 1080p and
    512² tables);
  - parameter range checks; scene v6 parse/cook/decode/World carriage;
    v5 rejected.
- **GPU, goldens** (ADR-0042 Initial baseline, one `test:` commit):
  - `bloom_demo`: a dark emissive scene — spheres above the knee, one
    below it, the black control. Halo pixels outside the bright
    spheres' silhouettes rise; the sub-threshold sphere's surroundings
    stay byte-identical. Discriminator: bloom off must fail.
  - `bloom_fog_demo`: ⑤ + ⑥ — a lamp in HDR fog whose colour exceeds the
    knee, so the haze glows. Discriminator: bloom off must fail.
- **GPU, properties:**
  - off ≡ today, byte for byte (no pass declared);
  - on with every pixel below the knee ≡ off, byte for byte (R7);
  - a halo is radially monotone around an isolated source, and grows
    with `strength`;
  - bloom reads fogged radiance: the same scene with fog on vs off gives
    different halos.
- **Regression:** Windows Debug + Release, every existing golden
  byte-identical; Validation Layers clean (a new Pipeline and resource
  surface); Android `assembleDebug`.

## Risks & Open Questions

**All five open questions were ruled by Human Review on 2026-09-24 (chat
confirmation), alongside this Spec's own Approval.** Each ruling below is
binding on Plan 0044; the reasoning that led to it stays in ADR-0092 and in
the Investigations above.

- **Q1 — the grammar.** Recommend one prefixed group after the fog group,
  `bloom=<strength> <threshold>`, all-or-nothing, found by prefix before
  the token-count gate (Plan 0043 P1). The alternative is two separate
  tokens.
  **Ruled 2026-09-24: a `bloom=` group following the `fog=` precedent.**
- **Q2 — composite placement.** Recommend a separate composite pass
  (D-10 untouched, +16.6 MB at 1080p). The alternative is compositing in
  the output transform via a superseding ADR for D-10.
  **Ruled 2026-09-24: a separate composite pass**; D-10 untouched.
- **Q3 — fixed or authored level count and resolution.** Recommend fixed:
  `L = 6`, `D1` at half resolution. The alternatives are an authored
  level count (1–11, as Filament) or Filament's fixed 384-px first level.
  **Ruled 2026-09-24: fixed six levels**, `D1` at half resolution.
- **Q4 — threshold before or after exposure.** Recommend before
  (scene-referred), so the knee means "radiance above X" in every scene.
  The alternative multiplies by the exposure first, making the knee
  display-relative.
  **Ruled 2026-09-24: the threshold applies before exposure.**
- **Q5 — Pipelines in the bundle or at the composition root.** Recommend
  the composition root creates them (every other Pipeline's precedent)
  and the bundle borrows them.
  **Ruled 2026-09-24: the caller (composition root) creates the bloom
  Pipelines.**
- **Risk — memory on Android.** ≈ 1.67× the HDR target when on; off by
  default. ⑦ measures it on the real device.
- **Risk — golden sensitivity.** Twelve filtered passes accumulate
  rounding; goldens are exact on one machine (ADR-0042), as today.

## Out of Scope / Future Work

- Emissive textures (Spec 0041's deferral) — they would give Bistro's
  signs their glow.
- Lens flare, dirt, streaks; auto-exposure; a compute-path blur.
- Compositing inside the output transform, if memory ever matters more
  than D-10's stability.
