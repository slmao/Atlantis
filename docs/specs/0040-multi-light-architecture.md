# Spec: Multi-Light Architecture

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-21
- **Related Plan(s):** None yet — Plan drafting may now begin (this Spec and
  its ADR have cleared Human Review, below).
  **Implementation still awaits its own, separate Joint Human Review** of
  Spec + Plan together, per AGENTS.md's own workflow — this Approval
  authorizes drafting Plan 0040 only.
- **Approval:** slmao, 2026-09-21 (chat confirmation, no reviewing PR —
  authorizes drafting Plan 0040; Implementation itself still awaits its own,
  separate Joint Human Review of Spec + Plan together). The same review ruled
  all five open questions; see Risks & Open Questions below.
- **Related ADR(s):**
  [ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md)
  (`Accepted`) — `FrameLightingData`'s successor structure
  (widened-fixed-cap vs. clustered/froxel), its buffer-binding strategy
  (uniform vs. storage), the chosen capacity and its basis, and the
  resulting shader-side iteration cost. Drafted alongside this Spec and
  accepted 2026-09-21 alongside this Spec's own Approval.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

This Spec implements [Spec 0036](0036-bistro-parity-roadmap.md) workflow ②.
Atlantis can light a scene with at most **one directional and four point
lights** — a hard, `ATLANTIS_CHECK_MSG`-enforced structural cap running from
the `.scene.txt` grammar through `FrameLightingData` to eleven shaders.
Bistro's defining look is dozens of string-light bulbs and neon signage. This
Spec widens point-light capacity to a single, larger fixed cap, keeps the
existing single-uniform-buffer binding, and leaves the clustered/froxel
structure Filament uses explicitly on the table as rejected-for-now Future
Work rather than as an unexamined omission.

## Motivation / Problem Statement

Spec 0036 workflow ② names the ceiling and its evidence
(`docs/specs/0036-bistro-parity-roadmap.md:347-385`): `FrameLightingData` is a
fixed 176-byte uniform struct holding `directionalLights[1]` and
`pointLights[4]`, re-extracted every frame since Spec 0022's correction, and
enforced as a programmer error rather than a soft convention. Four point
lights cannot express "string lights + neon."

The roadmap deliberately left the data-structure choice to this Spec, naming
two real options and requiring a quantified decision rather than a default:
widening the fixed array, or a froxel/clustered structure on Filament's
precedent (`filament/src/Froxelizer.h`, "256 lights max", cited as calibration
data at `0036:129-131` and `:367-370`, explicitly *not* adopted as a numeric
decision by that roadmap).

Workflow ② is a soft dependency of ③, ④ and ⑤ and a sequencing input to ⑥, and
⑦ cannot reproduce the reference image without it.

## Goals

- Raise the point-light capacity from 4 to a single, named constant N large
  enough for a hand-authored Bistro-scale lighting set, with the choice of N
  justified by measurement rather than taste.
- Keep one source of truth for the capacity: the `.scene.txt` grammar, the
  artifact decoder, the extraction path, and every shader must agree, and a
  scene exceeding N must fail with the existing named error at the existing
  gate rather than silently dropping lights at runtime.
- Leave the `uint16`-era guarantees of the rest of the frame uniform intact:
  the camera matrices, camera world position, irradiance SH and light-space
  tail keep their meaning, and every existing golden stays green.
- Decide the binding strategy (uniform vs. storage buffer) on measured
  budget, not on assumption.
- Say plainly what this does *not* solve — silent truncation is not removed,
  only re-thresholded — rather than implying a scalability property the
  chosen design does not have.

## Non-Goals

- **Per-light shadows.** Spec 0036 already pins this: the single shadow map
  serves the one directional light, and no point light gets one
  (`0036:153`). Nothing here changes the shadow path.
- **Spot lights.** Ruled by Plan 0037 Ruling 8 as a named `UnsupportedContent`
  importer error; `world::LightKind` stays `{Directional, Point}`.
- **A second directional light.** The directional cap stays 1: the shadow
  path, the light-space tail and `computeShadowLightSpaceMatrices()` all
  assume exactly one, and widening it is a shadow decision, not a capacity
  one.
- **Clustered/froxel light culling.** Evaluated and quantified below;
  recommended for rejection in this round and recorded as Future Work with
  the measurement that would reopen it — not silently omitted.
- **Physical light units and a new attenuation model.** The current
  `clamp(1 - d/range, 0, 1)` falloff and the raw glTF-value intensity
  semantics (Plan 0037 Ruling 8) are carried forward unchanged. Both deserve
  their own decision; neither is a capacity question.
- **Emissive surfaces** (workflow ③) and every other Spec 0036 workflow.
- **Dynamic per-frame light *culling* of any kind**, including a simple
  distance sort. Every authored light in range is uploaded every frame.

## Requirements

### Functional

1. **Point-light capacity becomes a single named constant N**, applied
   identically at all four existing gates: `parseSceneSource()`
   (`scene_source.cpp:356`), `decodeSceneArtifact()`
   (`scene_artifact.cpp:281`), `extractFrameLightingData()`'s
   `ATLANTIS_CHECK_MSG` (`scene_extraction.cpp:302-308`), and the shader-side
   array declaration. A scene with more than N point lights keeps failing at
   the first two with the existing `TooManyLights` error.
2. **`FrameLightingData`'s successor keeps its element layout.** The 32-byte
   `PointLightGpu` and `DirectionalLightGpu` elements, their field offsets and
   their std140 alignment are unchanged; only the array extent and the struct's
   total size change. This is what keeps the change reviewable and keeps every
   existing offset assertion meaningful.
3. **The binding stays a single uniform buffer**, per ADR-0088's measured
   budget — no storage buffer, no second descriptor, no change to
   ADR-0062's stage-visibility contract.
4. **The frame uniform buffer's allocated size is derived, not hand-written,
   and is correct.** See Investigation 4: the Runtime currently allocates 464
   bytes for a layout its own shaders declare as 592, and widening the light
   array moves that tail further. The successor must compute the buffer size
   from the same constants the layout is built from.
5. **All eleven shaders that declare the camera uniform block are updated**,
   not only the five that iterate lights — every one of them declares the full
   block inline and would otherwise disagree with the buffer layout
   (Investigation 3).
6. **Shader per-pixel cost stays proportional to the actual light count, not
   to N.** The existing `for (j < camera.pointLightCount)` loops already have
   this property; the Plan must not introduce a fixed-N loop.
7. **A multi-light demo scene and its golden exist**, with at least 8 point
   lights visibly contributing, so the widened capacity is proven to reach the
   shader rather than merely to compile.

### Non-functional

- **Performance:** no new per-frame CPU pass and no new GPU pass. The
  per-frame uniform write grows from 592 bytes to 592 + 32·(N−4) bytes; at
  N = 64 that is 2,512 bytes per frame, ~151 KB/s at 60 fps.
- **Memory:** one uniform buffer per frame-in-flight, 2,512 bytes at N = 64 —
  15.3% of the Vulkan-guaranteed `maxUniformBufferRange` floor of 16,384
  bytes.
- **Portability (within the Vulkan-only Phase 1 constraint):** the design must
  fit the *guaranteed* limit, not this machine's measured one, because Android
  is a primary target and its devices report far lower values than desktop.
  Investigation 2 shows the guaranteed floor accommodates ~497 point lights in
  this buffer, so N = 64 is portable by a wide margin.
- **Other:** no new dependency; a `.scene.txt` grammar constant changes but
  the grammar's shape does not, so no schema version bump is required (the
  version records format shape, and no line format changes).

## Pre-drafting Investigation (required conclusions, cited against real files)

### Investigation 1 — `FrameLightingData` as it stands

- `src/runtime/include/atlantis/runtime/scene_extraction.h:77-100` — the
  struct: `directionalLightCount` at 0, `pointLightCount` at 4, `_pad1[2]` at
  8, `directionalLights[1]` at 16 (32 B), `pointLights[4]` at 48 (128 B),
  `sizeof == 176`, `alignof == 16`, each offset pinned by a `static_assert`
  (`:101-122`). **32 bytes per point light** is the number every capacity
  calculation below uses.
- `src/runtime/src/scene_extraction.cpp:258-320` — `extractFrameLightingData()`
  walks the caller's light vector once and writes in place. The caps are
  `ATLANTIS_CHECK_MSG(false, ...)` at `:275-280` (a second directional) and
  `:303-308` (a fifth point), each followed by a defensive `continue` so that
  even a test that replaces the failure handler cannot write out of bounds.
  Both messages state the reasoning: the real gates already cap this, so
  reaching here is a programmer error.
- `src/runtime/src/runtime_application.cpp:1369-1370` — the whole 176-byte
  struct is assigned through the mapped pointer every frame, deliberately in
  full so a decreased light count leaves no stale trailing slot.

### Investigation 2 — the uniform budget, measured

- **Guaranteed floor.** The Vulkan specification's required
  `maxUniformBufferRange` is **16,384 bytes**; `maxStorageBufferRange` is
  **134,217,728**. These are the numbers a portable design must respect.
- **Measured on this machine** (`vulkaninfo`, Intel Arc B370, driver 101.8509,
  loader 1.4.357): `maxUniformBufferRange = 134,217,724` (~128 MB),
  `maxStorageBufferRange = 1,073,741,820`. Desktop imposes no constraint at
  all; the guarantee is therefore the only binding number, and Android — a
  primary target with no device measured here — is precisely why.
- **Capacity of a single UBO under the guarantee.** The frame uniform's
  non-point-light content is 464 bytes (Investigation 4's layout table), so
  `(16,384 − 464) / 32` = **497 point lights** fit in one uniform buffer on
  *any* conformant Vulkan device. Filament's own froxel cap of 256 would
  occupy 8,656 bytes — 52.8% of the guaranteed floor — and also fits.
- **Storage buffers need no feature bit.** An SSBO *read* in a fragment shader
  is core Vulkan 1.0; only stores/atomics from fragment shaders require
  `fragmentStoresAndAtomics`. So option (b) is available, and is not rejected
  below for lack of capability.

**Conclusion:** the uniform-buffer route is not the constraint anyone might
assume it to be. The binding-strategy question ADR-0062 left open resolves
comfortably in favour of staying with a uniform buffer.

### Investigation 3 — the shader consumption surface

Eleven `.slang` files declare the camera uniform block with `pointLights[4]`
inline; **five** of them actually iterate it:

| Iterates the point-light loop | Declares only (layout must still match) |
|---|---|
| `lit_textured.slang:94-102` | `pbr_clearcoat_ibl`, `pbr_clearcoat_ibl_normal_map` |
| `pbr_direct_lit.slang:200-209` | `pbr_sheen_ibl`, `pbr_sheen_ibl_normal_map` |
| `pbr_direct_lit_normal_map` | `pbr_anisotropic_ibl`, `pbr_anisotropic_ibl_normal_map` |
| `pbr_ibl`, `pbr_ibl_normal_map` | |

**This corrects a plausible assumption:** Spec 0035's three BRDF kinds
(clearcoat, sheen, anisotropy) are *not* zero-touch. They are zero-touch in
**logic** — none contains a point-light loop — but each declares the entire
`CameraUniform` block itself (e.g. `pbr_sheen_ibl.slang:53-60`), so the array
extent appears in all eleven files and all eleven must change together or the
block layouts diverge. Meanwhile `pbr_ibl`/`pbr_ibl_normal_map` — which one
might file under "IBL-only" — *do* iterate point lights and are full
participants.

Genuinely zero-touch: `shadow_cast.slang`, which binds its own separate
`LightSpaceUniform` (`:27`), and the sky shaders, which name no lights.

### Investigation 4 — the upload path, and a defect it exposes

Spec 0022's corrected design needs no new RHI API: the windowed path is
already safe because `VulkanPresentation::acquireNextTarget()`'s Step 0 drains
the retained submission before any mapped write
(`docs/adr/0065-...md:14-35`, which records ADR-0065's own **rejection** on
exactly this ground). Widening the struct changes the number of bytes
memcpy'd per frame and nothing about that safety argument.

The real constraint is the buffer's *size*, and tracing it surfaced a defect:

| Bytes | Content | Written at |
|---|---|---|
| 0–127 | view + projection | `runtime_application.cpp:1306-1307` |
| 128–303 | `FrameLightingData` (176) | `:1369-1370` |
| 304–319 | `CameraWorldPositionData` (16) | `:1379-1380` |
| 320–463 | irradiance SH9, 36 floats (144) | `:1386-1393` |
| 464–591 | light-space view + projection (128) | `:1419-1421` |

Total **592 bytes**, which is exactly what `pbr_direct_lit.slang:43-70`
declares and exactly what every image-regression fixture allocates
(`pbr_material_demo_fixture.cpp:314`, `integrated_showcase_demo_fixture.cpp:295`
and six others: `.sizeBytes = 592`).

**`runtime_application.cpp:790` allocates 464.** The Runtime therefore writes
128 bytes past its own camera uniform buffer every frame, and binds a
592-byte uniform block to a 464-byte buffer. It has not been observed failing
— the underlying `vkAllocateMemory` is page-granular, so the write lands in
mapped memory that happens to exist, and the Validation Layers do not police
host writes through a mapped pointer. It is nonetheless a real out-of-bounds
write on the Runtime path only; the fixtures, and therefore every golden, are
correct, which is why no test has ever caught it.

This is pre-existing (it dates from the light-space tail's addition, Plan 0027
Milestone 9) and is **not** caused by this Spec. It is in scope because
widening the point-light array moves that tail from 464 to 464 + 32·(N−4) —
at N = 64 the overrun would grow from 128 to 2,048 bytes. Hence Requirement 4.
Whether to land the fix ahead of this Spec as its own small change is Open
Question O1.

### Investigation 5 — the scene-syntax cap

Two independent gates, each a literal `4`:
`src/asset_system/src/scene_source.cpp:356` (`directionalCount > 1 ||
pointCount > 4` → `SceneSourceParseError::TooManyLights`) and
`src/asset_system/src/scene_artifact.cpp:281` (the same test →
`SceneArtifactDecodeError::TooManyLights`). `scene_types.h:56` fixes
`DecodedLightKind { Directional, Point }`. The `.scene.txt` grammar is at
version 4 and its *line format* does not change here — only the count these
two gates accept — so no grammar version bump is implied.

The richest scene in the repository today is
`assets/scenes/lighting_demo.scene.txt`: **one directional and one point
light** (`:6-7`). Every other committed scene has zero. There is no existing
asset that would exercise a widened cap, which is why Requirement 7 calls for
a new one.

### Investigation 6 — what capacity is actually needed

The decisive fact is one Spec 0037 already measured and this Spec inherits:
**the recommended Bistro glTF defines zero lights.**
`docs/specs/0037-gltf-importer.md:258` — "`KHR_lights_punctual`: **Not
present** — 0 lights defined anywhere in this file" — and `:274-279` confirms
Filament's own bistro is a separately modified file whose maintainers "simply
added light sources and emissive properties."

This **closes Spec 0036's own disclosed risk** ("the real visual compromise is
unknown until ① measures Bistro's own real light count", `0036:826-831`) with
an answer the roadmap did not anticipate: there is no upstream-imposed light
count to satisfy. Every Bistro light in workflow ⑦ will be hand-authored, on
Filament's own precedent.

So N cannot be derived from a measurement of the asset. It is an
authoring-headroom decision, and the honest basis is:

- the reference image shows string-light runs plus neon signage — "dozens" of
  visible sources, call it 24–48 authored emitters;
- **N = 64** gives at least 1.3× headroom over the top of that range, is
  13.0% of the single-UBO capacity the Vulkan *guarantee* allows (497), and is
  a quarter of Filament's own froxel cap;
- it is a power of two, so the array occupies exactly 2,048 bytes.

N = 64 is this Spec's recommendation; ADR-0088 records it as the decision.

## Proposed Design

**Widen the fixed array to N = 64 point lights, keep the single uniform
buffer, change nothing else about the shape of the data.**

```
inline constexpr std::uint32_t kMaxDirectionalLights = 1;   // unchanged
inline constexpr std::uint32_t kMaxPointLights = 64;        // was a literal 4 in four places

struct alignas(16) FrameLightingData {          // 176 -> 2,096 bytes
  std::uint32_t directionalLightCount;          // offset 0    unchanged
  std::uint32_t pointLightCount;                // offset 4    unchanged
  std::uint32_t _pad1[2];                       // offset 8    unchanged
  DirectionalLightGpu directionalLights[kMaxDirectionalLights];  // offset 16, 32 B, unchanged
  PointLightGpu       pointLights[kMaxPointLights];              // offset 48, 2,048 B
};
```

Every element layout, offset and `static_assert` above `pointLights` survives
unchanged, which is the property that makes this reviewable: the diff is one
array extent plus the constants that must follow it.

Downstream, the frame uniform buffer grows from 592 to 2,512 bytes and its
light-space tail moves from 464 to 2,384. Requirement 4's derived size is what
keeps those two in step — and fixes the 464/592 defect in the same motion.

The five iterating shaders are unchanged in logic: their loops are already
bounded by `camera.pointLightCount`, so a scene with three lights costs
exactly what it costs today. The six declaring-only shaders change by one
array extent each.

The `ATLANTIS_CHECK_MSG` in `extractFrameLightingData()` stays exactly as it
is, in kind and in wording — the cap is still a programmer error reachable
only by bypassing both real gates, and Requirement 1 keeps all four in
agreement.

## Architectural Impact

**Yes** — one decision, recorded in
[ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md):
`FrameLightingData`'s successor structure (widened fixed cap versus
clustered/froxel), its binding strategy (uniform versus storage buffer), the
capacity value and its basis, and the shader-side iteration cost. That is
precisely the ADR obligation Spec 0036 recorded for this workflow
(`0036:382-386`, `:749`), discharged in one ADR because the three parts are
one coupled choice: the structure determines the binding, and the binding's
measured budget determines the capacity.

No module boundary moves, no dependency is added, no threading or
memory-ownership model changes, and ADR-0062's stage-visibility contract is
untouched.

### Option comparison (the quantified basis for ADR-0088)

| | **(a) Widened fixed array, N = 64** | **(b) Froxel / clustered** |
|---|---|---|
| Capacity | 64; single-UBO ceiling is 497 under the Vulkan guarantee | Filament ships 256; architecturally thousands |
| Buffers | 1 uniform buffer, 2,512 B total (15.3% of the 16 KB guaranteed floor) | uniform + ≥2 storage buffers (froxel grid, light-index list) |
| New GPU/CPU pass | none | froxelization per frame — CPU pass or compute pass, plus a RenderGraph resource |
| Per-frame upload | one memcpy, 2,512 B (~151 KB/s at 60 fps) | grid + index lists rebuilt per frame; cost scales with lights × froxels touched |
| Per-pixel cost | unchanged: dynamic loop over the *actual* count | froxel lookup + indirection, then a loop over that froxel's list; cheaper only when lights are many and local |
| Shader edits | 11 files, array extent; 5 loops unchanged | the same 11, plus indirection logic in the 5 |
| Silent truncation | **not solved** — threshold moves 4 → 64 | effectively solved up to its own cap |
| Wasted bandwidth | 2,048 B of point-light array uploaded whether 1 light or 64 | proportional to real light count |
| New failure modes | none beyond today's | depth-range/frustum dependence, froxel assignment correctness, a new pass to schedule |
| Implementation size | small — a constant, eleven extents, a derived buffer size, one new scene + golden | large — new spatial structure, new pass, new buffers, new tests |
| Reversibility | trivial (change the constant) | a structural commitment |

**Recommendation: (a), N = 64.** Under AGENTS.md's proportionality
expectation, (b) buys scalability that nothing in Phase 1 needs: the target
scene defines zero lights and will be hand-authored to a "dozens" scale, a
single uniform buffer holds 497 of them on the weakest conformant device, and
(b)'s own real advantage — per-pixel cost that does not grow with total light
count — only begins to matter at counts well past what (a) accommodates. (b)
also introduces a RenderGraph pass-insertion decision, which is a second
architectural surface this workflow does not otherwise need to open.

The honest cost of (a), stated rather than glossed: 2 KB of uniform bandwidth
per frame is uploaded regardless of how many lights are lit, and silent
truncation is not eliminated — a scene author who needs 65 point lights gets a
hard `TooManyLights` failure, which is a better outcome than dropped lights
but is still a ceiling.

## Alternatives Considered

- **Froxel/clustered (option b).** Quantified above and recommended for
  rejection this round, with the reopening condition recorded in Future Work.
  Rejected on proportionality, not on capability.
- **Storage buffer for the light array, keeping a fixed CPU-side cap.** Would
  remove the uniform-size question entirely and allow a runtime-sized array.
  Rejected: Investigation 2 shows the uniform budget is not a constraint at
  this capacity, so this would add a second descriptor, a second binding in
  eleven shaders, and a change to ADR-0062's contract to solve a problem that
  does not exist at N = 64. It becomes the right answer if N ever needs to
  exceed ~497, which is also the condition that would justify (b).
- **A dynamically sized uniform buffer, reallocated when the scene's light
  count changes.** Rejected: it trades a 2 KB fixed cost for buffer lifetime
  complexity across frames in flight, against ADR-0023's stateless-creation
  model, and reintroduces the drain question Spec 0022 settled.
- **A smaller N (8 or 16).** Rejected: it would have to be revisited during
  workflow ⑦ itself, which is exactly the sequencing mistake Spec 0036 exists
  to prevent. 64 is 1.3× beyond the top of the estimated authored range at a
  cost of 2 KB.
- **A larger N (256, matching Filament's froxel cap).** Fits (8,656 B, 52.8%
  of the guaranteed floor) and is defensible, but it quadruples the per-frame
  upload for headroom nothing in the roadmap asks for, and the constant is
  trivially raised later. Recorded because it is the natural counter-proposal.
- **Sorting lights by influence on the CPU and uploading the best N.** A real
  technique, and a way to make truncation graceful rather than fatal.
  Rejected here as scope: it is a culling policy, and this Spec's Non-Goals
  exclude culling of every kind. Named in Future Work.

## Testing & Verification Plan

Satisfies Spec 0036's contract row for this workflow (`0036:801`):
GPU-independent tests over the new light-count/data-structure logic mirroring
`extractFrameLightingData()`'s existing unit-test precedent; image-regression
goldens proving the widened count actually reaches the shader; Validation
Layers clean.

**T1 — GPU-independent extraction tests** (Requirements 1, 2, 6).
Extending `tests/runtime/`'s existing `extractFrameLightingData()` coverage:
N point lights all extract with correct positions/colours/ranges and
`pointLightCount == N`; N−1 and 1 both behave; the N+1th light trips the
`ATLANTIS_CHECK_MSG` with its named message, verified through
`assertions::setFailureHandler()` and asserting the defensive `continue` left
no out-of-bounds write; the directional cap still trips at 2. The struct's
offsets and size are pinned by `static_assert`s as they are today, extended to
the new total.

**T2 — Gate agreement** (Requirement 1). `parseSceneSource()` and
`decodeSceneArtifact()` each accept exactly N point lights and reject N+1 with
`TooManyLights`, asserted against the same constant the struct uses, so the
four gates cannot drift apart.

**T3 — Buffer-size correctness** (Requirement 4). A test that the frame
uniform buffer's allocated size equals the offset+size of its last written
region. This is the test whose absence let the 464/592 defect live, so it is
written to fail against today's code before the fix.

**T4 — Multi-light golden** (Requirement 7, Initial baseline). A new
`multi_light_demo` scene with **at least 8 point lights** placed so each one's
contribution is individually visible (distinct colours, non-overlapping
falloff regions), rendered through the existing image-regression fixture
pattern and captured under ADR-0042's Initial-baseline-bootstrap rules — the
two-commit ordering and all four evidence items, exactly as Spec 0039's own
golden was handled. A count above 4 is the point: the golden is meaningless
unless it could not have been produced by the old ceiling.

**T5 — Every existing golden unchanged.** The 123 GPU-labelled tests and all
existing goldens stay green. This is the real proof that widening an array
extent changed no existing scene's pixels — every current scene has at most
one point light, so any golden movement would mean the layout change leaked.

**T6 — Validation Layers clean**, both configurations. Particularly meaningful
here because the uniform block grows: a mismatch between the declared block
and the bound buffer is exactly what the layers check at descriptor-update
time.

**T7 — Dual-platform regression.** Windows Debug and Release full builds plus
`ctest` in both; Android `assembleDebug` green, inheriting Plan 0034/0035's
disclosed emulator Validation-Layer gap.

## Risks & Open Questions

**All five open questions were ruled by Human Review on 2026-09-21 (chat
confirmation), alongside this Spec's own Approval.** Each ruling below is
binding on Plan 0040; the reasoning that led to it stays in ADR-0088 and in
the Investigations above.

- **O1 — should the 464/592 buffer-size defect be fixed ahead of this Spec?**
  It is pre-existing, latent, and Runtime-only: `runtime_application.cpp:790`
  allocates 464 bytes for a layout its own shaders declare as 592, and the
  light-space tail write at `:1419-1421` lands past the end every frame.
  **Ruled 2026-09-21: fix it first, as Plan 0040's own Milestone 0
  prerequisite** — a standalone, independently bisectable commit carrying the
  buffer-size test (T3), landing before any capacity work touches the layout.
  The capacity milestones then build on a correct baseline, and the fix stays
  reviewable as the correctness change it is rather than disappearing into a
  feature diff.
- **O2 — is N = 64 the right number?** It rests on an estimate of a
  hand-authored light count for a scene nobody has authored yet, because the
  asset defines zero lights (Investigation 6). **Ruled 2026-09-21: N = 64 as
  proposed.** The estimate is accepted with its stated basis; the mitigation
  stands on the record — N is one constant, each further light costs 32
  bytes, and the single-UBO ceiling under the Vulkan guarantee is 497.
- **O3 — does anything besides the roadmap want more than 64?** **Ruled
  2026-09-21: accepted** — nothing in Phase 1 does. If workflow ⑦'s own
  authoring finds 64 short, the reopening path is O2's constant first, then
  option (b).
- **O4 — `_shadowPad[9]` and the hand-kept layout duplication.** Eleven
  shaders and one C++ header describe the same buffer independently, kept in
  sync by hand (the same disclosed class of duplication
  `kPointLightDistanceEpsilon` already carries,
  `scene_extraction.h:182-189`). **Ruled 2026-09-21: the Slang reflection
  JSON cross-check is a mandatory verification item, not a suggestion** —
  Plan 0040 must verify that all twelve descriptions of the block agree,
  via the mechanism Plan 0019 P7 established, and record the result. A
  generated shared layout header remains a separate, undecided question;
  this ruling closes the verification gap, not the duplication itself.
- **O5 — this Spec declines an obligation Plan 0037 assigned to it.** Plan
  0037 Ruling 8 (2026-09-19) states: "Intensity is recorded as the raw glTF
  value; **what it means belongs to workflow ②'s ADR**"
  (`docs/plans/0037-gltf-importer.md:712-713`). ADR-0088 is that ADR, and this
  Spec's Non-Goals decline the question: light-intensity units and the
  attenuation model are a *photometric* decision — what a value means
  physically, and how it falls off — while this workflow is a *capacity*
  decision about how many such values fit. Bundling them would put two
  unrelated arguments in one ADR and would silently change the appearance of
  every existing lit scene and its golden. The same applies to the imported
  point-light `range` placeholder (Plan 0037's
  `kImportedPointLightRangePlaceholder`, 10000.0), which exists only because
  the grammar demands a positive range.
  **Ruled 2026-09-21: the obligation is reassigned to a future, separate
  photometric Spec** — not to workflow ③, and not to this ADR. The reasoning
  recorded with the ruling: the real requirements for a light-unit and
  attenuation model will be forced out concretely during workflow ⑦'s own
  hand-authored lighting pass, when someone is actually placing lights against
  the reference image, and a Spec drafted before that would be guessing. Plan
  0037 Ruling 8's intent is preserved — the question is still owed an ADR —
  only its owner moves. ADR-0088 records the same reassignment in its own
  "Declined-and-reassigned" note, and Spec 0036's ② section carries the dated
  pointer, so Ruling 8 is not left pointing at an ADR that declined it.

- **R1 — golden drift from an unrelated layout slip.** If any of the eleven
  shaders is missed, its block silently misaligns and its scenes render wrong.
  T5 is the gate, and every affected kind has an existing golden.
- **R2 — Android uniform limits are unmeasured here.** No Android device's
  `maxUniformBufferRange` was read; the design rests on the 16,384-byte
  guarantee, which is why the margin is 6.5×. If the Plan can cheaply log the
  limit on the emulator during its Android run, it should.

## Out of Scope / Future Work

- **Clustered/froxel light culling**, with its reopening condition stated: a
  scene needing more simultaneous point lights than a single uniform buffer
  holds (~497 under the guarantee), or measured per-pixel cost from iterating
  many lights that a per-froxel list would avoid. Either is a measurement, not
  a preference.
- **CPU-side light culling/prioritization**, which would turn the N+1 case
  from a hard error into a graceful degradation.
- **Per-light shadows** for point lights (Spec 0036 pins this out of the whole
  roadmap).
- **Physically based light units and a real attenuation model**, including
  what glTF's raw intensity values should mean and whether the imported
  `range` placeholder should survive — declined here and, per O5's ruling,
  reassigned to a future separate photometric Spec, to be drafted once
  workflow ⑦'s own hand-authored lighting pass forces out its requirements.
- **A generated single-source layout header** shared by C++ and Slang (O4).
- **A second directional light** and spot lights.
