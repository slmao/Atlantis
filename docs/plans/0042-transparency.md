# Plan: Transparency

- **Spec:** [Spec 0042: Transparency](../specs/0042-transparency.md)
  (`Approved`, 2026-09-23) —
  [ADR-0090](../adr/0090-transparency-blend-state-draw-order-and-depth-write.md)
  (`Accepted`)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0042 in full — Spec 0036 workflow ④ — the alpha-test
(`MASK`) and alpha-blend (`BLEND`) paths on the four PBR kinds, with
blended draws ordered back-to-front inside `Renderer::drawFrame()` and never
writing depth. Spec 0042's rulings O1–O5 and ADR-0090's four decisions are
binding and are not reopened here; each is cited where it lands.

## Pre-drafting reading (cited, not restated)

Spec 0042's six investigations were re-walked at Plan granularity against
`origin/main` at `d528677` (which also carries PR #177, the per-process
test scratch-directory fix — the parallel-run flakes seen during Plan 0041's
own regression are gone, so both full suites must now be clean). Every
conclusion holds. Three things are sharper, and one of them is a real
obstacle the Spec did not see.

1. **Schema v7 → v8, and its exact bytes.** The v7 record is 108 bytes,
   ending with `emissive_factor(96,12)`
   (`material_artifact.h:28-36`, `:66-67`). v8 appends
   **`alpha_mode(108,4)`** (a `std::uint32_t` enumerator, the `kind` field's
   own encoding shape) and **`alpha_cutoff(112,4)`** (a float), for
   **116 bytes** — the brief's arithmetic confirmed against the real table.
   The bump's reach is the Spec 0041 one exactly: 51 `.material.txt` files
   (version line only), the same 6 C++ files' source strings, the metadata
   fixtures, and the artifact golden-byte test. **The version traps are
   now at 8:** `material_source_tests.cpp` uses 8 as its unknown source
   version and `material_artifact_tests.cpp` patches the schema byte to
   `0x08`; both move to 9 **by hand**, as Plan 0041 moved them from 7 to 8.
2. **The parser's optional-line pre-pass handles exactly one line.** Plan
   0041's mechanism (`material_source.cpp`, the block beginning "the
   optional `emissive_factor:` line is recognised by its prefix") hardcodes
   one prefix at one legal index (8, or 10 after a kind pair). Two more
   optional lines need it generalized — P1.
3. **The RHI and Vulkan sites are symmetric with `depthWriteEnabled`.**
   `PipelineCreateParams` ends at `types.h:360` (`hasColorAttachment`), with
   `depthWriteEnabled` at `:351`; the blend attachment is built at
   `vulkan_device.cpp:1228-1241`, where `blendEnable = VK_FALSE` (`:1231`)
   becomes the enum's switch and everything else (`colorWriteMask`, the
   attachment count) stays. The depth half needs no RHI change at all.
4. **`drawFrame()` and the ordering.** The signature is
   `renderer.h:120-131`, ending with `shadowCasterDrawItems`; an optional
   camera world position appends after it with a default, so all 23 call
   sites compile unchanged. The draw loop is `renderer.cpp:113`, after the
   sky at `:100-111`.
   **A pure sort function cannot take `DrawItem`s** and still be
   GPU-independent: a `DrawItem` holds a `const Material*`, and constructing
   a `Material` needs a real `Pipeline` from a real `Device`. The ordering
   function therefore takes a projected, plain-data view — P3.
5. **The shaders are ready.** Each of the ten computes
   `alphaOut = texColor.a * baseColorFactor.a` well before its return
   (alphaOut/return lines: 177/241, 143/202, 144/199, 138/188, 160/248,
   125/209, 153/239, 147/239, 148/189, 135/176), so the `discard` goes
   immediately after that line and skips the lighting work. All four
   push-constant structs carry `_padEmissive` at 108/108/108/124
   (`pbr_push_constants.h:40`, `pbr_clearcoat_push_constants.h:45`,
   `pbr_anisotropic_push_constants.h:34`, `pbr_sheen_push_constants.h:40`) —
   renaming that pad to `alphaCutoff` is the whole layout change, and Spec
   0042's slangc measurement (108/124, blocks 112/128) already proved it.
6. **The sort point is the one real obstacle.** ADR-0090 Decision 2 says
   `createMesh()` computes the mesh's local bounds centre "from the
   positions it already receives". It does receive them — but as
   `const void* vertexData` plus a `VertexInputLayout`
   (`mesh.h:47-52`), and `mesh.cpp:17` states as an invariant that
   "layout is not otherwise inspected by `createMesh()` itself". The layout
   does carry what a scan needs (`strideBytes`, and each attribute's
   `offsetBytes`/`format`, `types.h:244-255`), so the scan is possible —
   but it makes the Renderer interpret vertex bytes for the first time.
   That is a disclosed invariant change, not a free one — P4 and Q2. The 31
   `createMesh()` call sites need no change either way.

## Plan-stage decisions

**P1 — one ordered table of optional source lines, not three ad-hoc
pre-passes.** The v8 grammar has three optional lines, all after the
kind-specific pair and before `normal_map`, in a fixed relative order:
`emissive_factor:`, `alpha_mode:`, `alpha_cutoff:`. The Plan 0041 pre-pass
generalizes to a small table walked at a cursor: each entry is recognized by
its prefix at the cursor, parsed, and erased; a recognized prefix out of
order is `FieldOrderMismatch`; anything else ends the walk and the legacy
line-count machine runs on what remains, byte-for-byte as today. The maximum
line count becomes 14 (8 base + 2 kind pair + 3 optional + `normal_map`).
The gate stays what it was in Plan 0041: **every pre-existing
`material_source_tests` case passes with only its version line changed.**

**P2 — the alpha lines are rejected outright on `lit_textured` and
`unlit_textured`,** with a new `AlphaModeNotSupportedForKind`, exactly as
`EmissiveNotSupportedForKind` works. Spec 0042 R2 says `Opaque` is legal
everywhere; that is about the *value* every material without the line
already has, not about writing the line on a kind whose shaders cannot
honour it. Recorded because it is a narrowing of R2's letter — Q3.

**P3 — the ordering function takes plain data, not `DrawItem`s.**

```
struct DrawSortInput {          // one per draw item, in caller order
  bool blended = false;
  std::array<float, 3> worldSortPoint{};
};
[[nodiscard]] std::vector<std::uint32_t> computeDrawOrder(
    std::span<const DrawSortInput> items,
    const std::optional<std::array<float, 3>>& cameraWorldPosition);
```

It returns indices into the caller's list: non-blended first in input order,
then blended farthest-first, ties in input order. `drawFrame()` projects its
`DrawItem`s into this view (reading `Material::alphaMode()` and the mesh's
sort point), calls it, and issues draws through the returned indices. The
unit tests construct `DrawSortInput`s directly — no `Device`, no `Pipeline`,
no GPU (reading item 4).

**P4 — the bounds centre is computed inside `createMesh()`, by scanning
positions through the layout.** The alternative (a new `DrawItem` or
`createMesh` parameter supplied by callers) spreads one geometric fact over
19 draw-list builders. The cost is the disclosed invariant change at
`mesh.cpp:17`, which this Plan updates in the same commit: the position
attribute is located by `location == 0`, and a layout without it, or with a
non-`Float3` position, is a checked programmer error rather than a silent
origin.

**P5 — the cutout test texture is a new, committed 64×64 RGBA PNG** with a
hard-edged alpha pattern (opaque disc, fully transparent outside, one flat
RGB), generated by a one-off uncommitted script — the
`normal_map_tilted_source_unorm` precedent (Plan 0029 P19). No committed
texture has real alpha today (Spec 0042 verification), and the pattern must
be hard-edged so the golden shows a crisp cutout rather than a filtering
gradient.

## Milestones / Task Breakdown

**Three milestones, five commits** — the brief suggested two; the two
transparency paths are independent, each with its own golden and its own
gate, and keeping them apart keeps each gate meaningful (Q1).

### Milestone 1 — schema v8, the blend state, and the importer (`feat:`, R1–R5, R9)

No rendering change: every existing material is `Opaque`, so every Pipeline
is still created with blending disabled and depth write on.

1. `alphaMode`/`alphaCutoff` on `MaterialAssetData`, `ParsedMaterialSource`,
   `DecodedMaterialArtifact` and the metadata struct.
2. P1's generalized optional-line table; P2's rejection on non-PBR kinds;
   the serializer writes each line only when non-default.
3. Cook-time validation: the cutoff via the existing factor validator
   (finite, `[0, 1]`); decode-time re-validation plus a new
   `UnknownAlphaMode` for an out-of-range mode byte.
4. Artifact v8 (116 bytes, the reading item 1 layout), metadata v7 (both
   lines mandatory, the Plan 0041 P2 discipline), the load agreement check.
5. The version bumps, including the two traps moved to 9 by hand.
6. RHI: `ColorBlendMode` in `types.h` beside the other pipeline enums, the
   defaulted `colorBlendMode` field, and the Vulkan mapping at
   `vulkan_device.cpp:1231` (straight-alpha "over", ADR-0090 Decision 1).
7. Realization: `alphaMode` chooses `colorBlendMode` and `depthWriteEnabled`;
   `renderer::Material` gains the mode and an accessor (defaulted trailing
   parameter, the ADR-0081/0089 shape).
8. Importer (R9, ruling O1): `MASK` → `Mask` + cutoff, `BLEND` → `Blend`,
   replacing the drop-and-report; transmission materials stay `Opaque`
   (ruling O3). Its report wording moves to v8, and
   `gltf_material_tests.cpp`'s pinned line updates with it.

**Risk gates.**
- **Zero rendering change**: full Debug suite green with every golden
  byte-identical. A moved golden means a Pipeline changed state for an
  `Opaque` material — stop and report.
- **Parser error parity** (P1's gate). Any pre-existing parser test needing
  more than its version line changed means the pre-pass changed behaviour —
  stop and report.
- A GPU-independent test asserts each `alphaMode` maps to the expected
  `colorBlendMode`/`depthWriteEnabled` pair, so M1's contribution is
  verified before anything renders differently.

### Milestone 2 — the alpha-test path (`feat:` + `test:`, R6)

1. Rename `_padEmissive` to `alphaCutoff` in all four push-constant structs
   (offsets 108/108/108/124 unchanged, sizes 112/112/112/128 unchanged);
   the renderer payload copies it (0 unless `Mask`).
2. All ten PBR fragment shaders: `float alphaCutoff;` as the last
   `PushConstants` field, and one `discard` immediately after the existing
   `alphaOut` line (reading item 5), guarded by `alphaOut < alphaCutoff`.
3. Extend the 10/10 push-constant reflection cross-check with
   `alphaCutoff`'s offset, asserting the block sizes are unchanged.
4. The P5 cutout texture, a `cutout_demo` scene and its golden: a lit quad
   or sphere with the alpha texture, `alpha_mode: mask`, cutoff 0.5, over an
   opaque backdrop, on an existing fixture (Q4).
5. The golden in its own `test:` commit (ADR-0042 Initial-baseline
   procedure, four evidence items), with a discriminator: the same scene at
   `alpha_mode: opaque` fails against it.

**Risk gates.**
- **Every existing golden byte-identical.** The cutoff is 0 for every
  existing material, and `alphaOut >= 0` always, so no fragment is ever
  discarded. A moved golden means the guard is wrong — stop and report.
- Sizes must not move: the cross-check is the gate, as in Plan 0041 M2.
- The cutout golden must show crisp holes; a soft gradient means the
  texture or its sampling is wrong, not the alpha test.

### Milestone 3 — the blend path and ordering (`feat:` + `test:`, R7, R8)

1. `Mesh` gains its local sort point, computed in `createMesh()` (P4), and
   `mesh.cpp:17`'s invariant comment is corrected in the same commit.
2. `computeDrawOrder()` (P3) in the Renderer's public headers, plus its
   GPU-independent unit tests: blended last, farthest-first, stable ties,
   non-blended order preserved, and the checked error when a blended item
   has no camera position.
3. `drawFrame()` takes the optional camera world position, projects its
   items, and issues draws through the computed order; the shadow pass skips
   blended items (R8). The Runtime passes the position it already computes
   for `CameraWorldPositionData`.
4. A `transparency_demo` scene: two overlapping translucent spheres of
   different colours in front of an opaque backdrop, plus a second,
   committed scene identical except that the two translucent nodes are
   declared in the opposite order (Q5).
5. Tests: the golden for the first scene; **the swap-order scene must render
   byte-identically to it** (Spec 0036's "real back-to-front ordering test");
   a test that a blended item contributes nothing to the shadow map.
6. The golden in its own `test:` commit, with its four evidence items and a
   discriminator (the same scene with sorting disabled, or with the spheres
   opaque, fails).
7. Full regression: Windows Debug and Release, all goldens, Validation
   Layers clean (the new blend-state Pipeline is the surface to watch),
   Android `assembleDebug` (ASProxy first, then local Gradle 9.5.1
   `--offline`).

**Risk gates.**
- **The swap-order test is the load-bearing one.** If the two scenes differ
  by even one byte, the sort is not deciding the order — stop and report.
- Validation Layers must be clean at Pipeline creation with
  `AlphaBlend`, and at every draw.
- Every pre-existing golden byte-identical in both configurations.

## Files / Modules Touched (expected)

- **Asset System:** `material_types.h`, `material_source.{h,cpp}`,
  `cook_material.cpp`, `material_artifact.{h,cpp}`,
  `material_metadata.{h,cpp}`, `load_material.cpp`, `errors.h`, and their
  five test files.
- **Assets:** 51 `.material.txt` version lines; M2's cutout texture, scene
  and material; M3's two transparency scenes and their materials; the
  `assets/CMakeLists.txt` registrations.
- **RHI:** `types.h` (`ColorBlendMode`, the field) — public API, ADR-0090
  Decision 1.
- **Vulkan Backend:** `vulkan_device.cpp`, the blend-attachment block only.
- **Renderer:** the four `pbr*_push_constants.h`, `material.{h,cpp}`,
  `mesh.{h,cpp}`, `renderer.{h,cpp}`, and the new draw-order header/source.
- **Runtime:** `material_realization.cpp` (pipeline params from
  `alphaMode`), `runtime_application.cpp` (pass the camera position).
- **Importer:** `material_import.cpp`; `gltf_material_tests.cpp`.
- **Shaders:** the ten `shaders/pbr_*/*.slang`.
- **Tests:** the reflection cross-check, the new draw-order unit tests, the
  two new GPU test files, their goldens and generators.

**Not touched** — needing any of these is a stop-and-report:
- RenderGraph (ADR-0090 Decision 2 keeps ordering inside the draw pass),
  Platform, World, the Asset System's mesh/scene/texture formats;
- `lit_textured`, `textured_quad`, `sky`, `shadow_cast` and the
  output-transform shaders;
- any existing golden;
- the depth compare op, the cull mode, and `hasDepthAttachment` — the
  transparency paths use the existing `depthWriteEnabled` only.

## Sequencing & Dependencies

M1 → M2 → M3. M1 is independent of the GPU and bisectable on its own; M2
needs M1's `alphaCutoff` data; M3 needs M1's `alphaMode` on
`renderer::Material`. M2 and M3 are independent of each other and could swap
order; the Plan runs alpha test first because it is the smaller path and its
golden does not depend on ordering. Each golden commit depends on its own
milestone's `feat:` commit being on a clean tree.

## Verification Checklist

- [ ] R1–R3 (M1, GPU-independent): parse (present/absent/malformed/wrong
      kind, all three optional lines, every legal shape); cook (cutoff
      range); artifact v8 round-trip at 116 bytes and v7 rejected; metadata
      round-trip and agreement; serializer round-trip.
- [ ] R4–R5 (M1, GPU-independent): each `alphaMode` maps to the expected
      `colorBlendMode`/`depthWriteEnabled`.
- [ ] R9 (M1): the importer test covering `MASK`, `BLEND`, and a
      transmission material staying `Opaque`.
- [ ] R6 (M2): the 10/10 reflection cross-check including `alphaCutoff`;
      the cutout golden and its opaque discriminator.
- [ ] R7 (M3, GPU-independent): `computeDrawOrder()`'s unit tests.
- [ ] R7 (M3, GPU): the transparency golden; **the swap-order scene renders
      byte-identically**.
- [ ] R8 (M3): a blended item contributes nothing to the shadow map.
- [ ] Every pre-existing golden byte-identical at M1, M2 and M3, Debug and
      Release.
- [ ] Validation Layers clean (Debug, fatal), including `AlphaBlend`
      Pipeline creation.
- [ ] Android `assembleDebug` green.

## Open points (for Joint Human Review)

- **Q1 — three milestones, not the brief's two.** The alpha-test and blend
  paths share only the schema; each has its own golden, its own
  discriminator and its own gate. **Recommend three**; two would put two
  goldens and two independent risks in one commit.
- **Q2 — the sort point inside `createMesh()` (P4).** It makes the Renderer
  interpret vertex bytes for the first time, against `mesh.cpp:17`'s stated
  invariant. **Recommend it anyway** — the alternative spreads the same
  computation over 19 draw-list builders — with the invariant comment
  corrected in the same commit. If review prefers the invariant kept, the
  fallback is an explicit `createMesh()` parameter at 31 call sites.
- **Q3 — the alpha lines rejected outright on non-PBR kinds (P2).**
  **Recommend rejecting**, mirroring emissive; the alternative is accepting
  a no-op `alpha_mode: opaque` on kinds whose shaders ignore it.
- **Q4 — which fixture hosts the cutout golden?** It needs real lighting to
  be legible, so the emissive dark-scene fixture is wrong.
  **Recommend `PbrMaterialDemoFixture`** (lit, no environment, the
  `hdr_roll_off` alias precedent) with a new scene.
- **Q5 — the swap-order test as a second committed scene, or a runtime
  reordering?** **Recommend the second scene**: it exercises the real
  asset path and is deterministic, at the cost of one more small committed
  asset.

## Rollback Plan

- M3 reverts to a tree where blended materials are expressible and render
  blended, but in caller order — visibly wrong for overlapping transparency,
  correct for everything else.
- M2 reverts to a tree where `Mask` materials carry their cutoff but render
  opaque.
- M1's revert restores schema v7 in all three formats; artifacts are
  build-time outputs and recook. No committed asset holds a v8-only field
  except M2's and M3's, which revert first.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas: the push-constant cross-check result and the two goldens' four
evidence items each go in the PR, and the swap-order result is quoted
explicitly.
