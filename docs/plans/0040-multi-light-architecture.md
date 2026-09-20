# Plan: Multi-Light Architecture

- **Spec:** [Spec 0040: Multi-Light Architecture](../specs/0040-multi-light-architecture.md)
  (`Approved`, 2026-09-21) —
  [ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md)
  (`Accepted`)
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-09-21 — reviewed this Plan and
  [Spec 0040](../specs/0040-multi-light-architecture.md) together (chat
  confirmation; document set carried by this branch's PR) and explicitly
  authorized Implementation from Milestone 0. The four open points were
  ruled in the same review: Q1 the multi-light golden models on
  lighting_demo_fixture (constrained, not fixed); Q2 two constants
  (Asset-System kMaxPointLightsPerScene + Runtime kMaxPointLights) tied by
  static_assert; Q3 Milestone 2 stays whole; Q4 the 14 hardcoded-592
  conversions move into Milestone 0 with the constant's birth. P1
  (over-capacity is a named error, never truncation) and P2 (the derived
  buffer-size constant with static_assert) confirmed.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0040 in full — Spec 0036 workflow ② — widening point-light
capacity from 4 to 64 in the existing single uniform buffer, after first
repairing the frame uniform buffer's under-allocation that widening would
otherwise make 16× worse. Spec 0040's rulings O1–O5 (2026-09-21) are binding
and are not reopened here; each is cited where it lands.

## Pre-drafting reading (cited, not restated)

Spec 0040's six investigations were re-walked at Plan granularity against
`origin/main` at `a019e13`. Every conclusion holds. Four things are larger or
sharper than the Spec recorded, and one of them changes a milestone's shape.

**1. `FrameLightingData` and its assertion chain.**
`src/runtime/include/atlantis/runtime/scene_extraction.h:77-100` is the struct;
`:101-122` is the twenty-two-line `static_assert` chain. Of those twenty-two,
**exactly one changes**: `:120` (`sizeof(FrameLightingData) == 176` → 2,096).
`:109` (`offsetof(pointLights) == 48`) and every element-level offset and size
assertion stay true by construction, which is the property that makes
Requirement 2 checkable rather than merely claimed.

**2. The two `TooManyLights` gates.** `src/asset_system/src/scene_source.cpp:356`
and `src/asset_system/src/scene_artifact.cpp:281`, each the literal expression
`directionalCount > 1 || pointCount > 4`. Both return a named error. Confirmed
there is no third gate.

**3. The extraction caps.** `src/runtime/src/scene_extraction.cpp:262-281` (the
directional branch, `ATLANTIS_CHECK_MSG` at `:275`) and `:299-311` (the point
branch, `ATLANTIS_CHECK_MSG` at `:303`), each followed by a defensive
`continue` so a test that replaces the failure handler still cannot write out
of bounds. The point-branch message names "a fifth Point light" and "caps this
at four" in prose — both must move with the constant, or the message lies.

**4. The shader declaration surface, and a size split the Spec did not
record.** Eleven `.slang` files declare the camera uniform block inline. They
are **not all the same size**:

| Block total | Shaders | Tail |
|---|---|---|
| **304 B** | `lit_textured` | ends at `pointLights[4]` (`lit_textured.slang:32-40`) |
| **592 B** | the ten PBR variants | `cameraWorldPosition`/`_pad2`, then 144 B of `irradianceSh[9]` (eight IBL kinds) or `_shadowPad[9]` (`pbr_direct_lit`, `pbr_direct_lit_normal_map`), then the 128-byte light-space pair |

Verified by reading each block's tail; the two `_shadowPad`/`irradianceSh`
spellings occupy the identical 144 bytes, deliberately, so the light-space pair
lands at 464 in both (`pbr_direct_lit.slang:56-62` states exactly this). After
widening, the two totals become **2,224** and **2,512**.

**5. The 464/592 defect, and the existing test that already pins the right
number.** `src/runtime/src/runtime_application.cpp:790` allocates 464;
the writes run to byte 592 (`:1306-1307`, `:1369-1370`, `:1379-1380`,
`:1386-1393`, `:1419-1421`). **The correct value is not my arithmetic** —
`tests/runtime/pbr_reflection_cross_check_tests.cpp:231-246` already asserts,
against a freshly generated `slangc` reflection of `pbr_ibl.slang`, that the
light-space pair sits at offset 464 and the block totals 592.

**6. The allocation blast radius is 14 sites, not eight.** The Spec said
"eight fixtures". Recounted:

| | Count | Where |
|---|---|---|
| Hardcoded `592` | **14**, in 8 files | 7 image-regression fixtures (`integrated_showcase_demo`, `pbr_anisotropic_demo`, `pbr_clearcoat_demo`, `pbr_materials_showcase`, `pbr_material_demo`, `pbr_normal_map_demo`, `pbr_sheen_demo`), `shadow_gpu_tests.cpp:365`, and six in `pbr_render_gpu_tests.cpp` (`:504`, `:640`, `:737`, `:810`, `:882`, `:1058`) |
| Derived, already correct | 1 | `lighting_demo_fixture.cpp:253` — `sizeof(float)*32 + sizeof(FrameLightingData)` = 304, the right size for `lit_textured`, and it stays right after widening because it is derived |
| Wrong | 1 | `runtime_application.cpp:790` |

**7. Tests that hardcode the current size and will break (by design).**
`tests/runtime/scene_extraction_tests.cpp:1054`
(`static_assert(sizeof(FrameLightingData) == 176)`),
`tests/runtime/runtime_smoke_gpu_tests.cpp:84`
(`static_assert(kLightingByteOffset + sizeof(FrameLightingData) == 304)`), and
`tests/runtime/pbr_reflection_cross_check_tests.cpp:139-143` (which computes
304/320 from `sizeof(FrameLightingData)`). These are load-bearing: they are the
tripwires that prove the widening was propagated, so M1 updates them
deliberately rather than treating them as collateral.

**8. O4's mechanism already exists, at 2/11 coverage.**
`tests/runtime/pbr_reflection_cross_check_tests.cpp` invokes real `slangc`
with `-reflection-json` at test time (`:80-108`) and cross-checks field
offsets against the C++ structs — GPU-independent, no stale committed JSON.
It currently covers **`pbr_direct_lit` and `pbr_ibl` only**. O4's mandatory
cross-check is therefore not new infrastructure to invent but existing
infrastructure to widen from 2 shaders to 11.

**9. No golden exercises the defective allocation.** No image-regression
fixture constructs a `RuntimeApplication`; the only references are comments
(`integrated_showcase_demo_fixture.cpp:603`, `material_demo_fixture.cpp:500`).
Every fixture allocates its own camera buffer at 592 or the derived 304. This
sharpens M0's risk gate — see there.

## Plan-stage decisions

**P1 — over-capacity behaviour is a named error, never truncation.** Spec 0040
Requirement 1 is explicit: the two gates keep returning `TooManyLights`, and
the extraction cap stays a programmer error. Recording the reconciliation
because the Spec says two things: its Summary and Goals inherit Spec 0036's
generic description of option (a) as one that "silently truncates", and
ADR-0088's Consequences repeat "silent truncation is re-thresholded, not
eliminated". That phrase describes the *technique* in general. **In Atlantis it
does not apply**: both real gates reject, so no light is ever silently dropped,
before or after this Plan. What is re-thresholded is the point at which a scene
is *refused*. Requirement 1 governs; no code becomes truncating.

**P2 — M0's correct value is 592, and it becomes a derived named constant.**
Requirement 4 forbids a hand-written size. The constant is expressed from named
parts, not as a literal:

```
inline constexpr std::size_t kCameraMatricesBytes        = 128;   // view + projection
inline constexpr std::size_t kEnvironmentIrradianceShBytes = 144; // float4[9] / the _shadowPad twin
inline constexpr std::size_t kLightSpaceMatricesBytes    = 128;   // light-space view + projection
inline constexpr std::size_t kCameraUniformBufferSizeBytes =
    kCameraMatricesBytes + sizeof(FrameLightingData) + sizeof(CameraWorldPositionData) +
    kEnvironmentIrradianceShBytes + kLightSpaceMatricesBytes;
static_assert(kCameraUniformBufferSizeBytes == 592);   // M0; becomes 2512 in M1
```

It lives in `scene_extraction.h` beside the structs whose sizes it sums.
**This is the one place M0 exceeds "change the allocation line"**, and it is
required by Requirement 4 and by T3 (a test cannot assert a literal buried in
`initializeSteps()`). Flagged here so it is not a surprise at review; anything
beyond this is the stop-and-report condition below.

**P3 — the constant for capacity.** `kMaxPointLights = 64` and
`kMaxDirectionalLights = 1` in `scene_extraction.h`, consumed by the struct,
the extraction caps and their message text, and — by value, not by include —
the two Asset System gates. Asset System may not include a Runtime header
(ADR-0043's direction of dependency), so the gates take their own
`kMaxPointLightsPerScene` in `scene_types.h`, and a `static_assert` in the
Runtime header ties the two together. Two constants, one asserted equality,
rather than a silent duplicated literal.

**P4 — the multi-light scene runs on a point-light-iterating shader.** The new
`multi_light_demo` must use one of the five shaders that actually loop
(`lit_textured`, `pbr_direct_lit`, `pbr_direct_lit_normal_map`, `pbr_ibl`,
`pbr_ibl_normal_map`) — a golden on one of the six declare-only kinds would
prove nothing about capacity. **Recommendation: model it on
`lighting_demo_fixture`** (`lit_textured`): it is the lighting-focused fixture,
its camera buffer allocation is already derived rather than hardcoded, and its
304-byte block keeps the golden's own variables to the light count. The Plan
does not forbid a PBR-based fixture if implementation finds `lit_textured`'s
look inadequate for showing eight distinct contributions; the constraint is the
looping-shader requirement plus a derived allocation.

**P5 — no `.scene.txt` grammar version bump.** The line format does not
change; only the count the two gates accept. Spec 0040's Non-functional
section already rules this, and the artifact's own decoder check is a count
comparison, not a format branch.

## Milestones / Task Breakdown

Four milestones. M0 is a prerequisite ruled by O1, not a convenience.

### Milestone 0 — repair the frame uniform under-allocation (`fix:`, Spec Req 4, ruling O1)

A standalone, independently bisectable commit that lands **before** anything
touches the light array.

1. Add P2's derived `kCameraUniformBufferSizeBytes` (and the three named part
   constants) to `scene_extraction.h`, with the `static_assert(== 592)`.
2. `runtime_application.cpp:790`: `464` → the constant. This is the whole
   behavioural change.
3. **T3, the test whose absence let this live:** a GPU-independent test
   asserting that the constant equals the offset-plus-size of the last written
   region, plus that it matches the block size the existing reflection
   cross-check derives from real `slangc` output for `pbr_ibl`. Written so it
   fails against the pre-fix literal.

**Risk gates.**
- **Scope.** If the fix needs more than the allocation line plus P2's
  constants — for example if some other consumer turns out to depend on the
  464 value — **stop and report**. That dependency would mean the layout is
  read inconsistently somewhere, which is a different and larger finding.
- **Goldens must not move**, and here the Plan can be precise rather than
  hopeful: re-read item 9 established that **no image-regression fixture
  constructs a `RuntimeApplication`**, so no golden touches the defective
  allocation and none *can* move. If one does, the premise is wrong — stop and
  report, because it would mean a golden path reads the over-run region.
- **The meaningful M0 signal is the Runtime GPU tests**
  (`runtime_smoke_gpu_tests`, `material_realization_gpu_tests`,
  `environment_realization_gpu_tests`), which do drive the real allocation.
  They must stay green and unchanged in behaviour: the over-run bytes
  previously landed inside the page-granular `vkAllocateMemory` region, so
  correcting the size should change no rendered byte. If any of them changes
  behaviour, the over-run was being read — a more serious finding than the
  write itself. **Stop and report.**

### Milestone 1 — widen `FrameLightingData` and the two gates (`feat:`, Req 1, 2, 6)

1. `scene_extraction.h`: P3's constants; `pointLights[kMaxPointLights]`;
   `static_assert(sizeof(FrameLightingData) == 2096)` at `:120`; the other
   twenty-one assertions unchanged. `kCameraUniformBufferSizeBytes` follows
   automatically to 2,512 — the payoff of M0's derivation.
2. `scene_extraction.cpp:299-311`: the bound becomes `kMaxPointLights` and the
   `ATLANTIS_CHECK_MSG` prose stops saying "a fifth" and "four". The defensive
   `continue` and the check's *kind* are unchanged (P1).
3. `scene_source.cpp:356` and `scene_artifact.cpp:281`: `4` →
   `kMaxPointLightsPerScene`, with P3's cross-module `static_assert`.
4. Update the three tripwire tests (re-read item 7) to the new values, and
   extend `tests/runtime/scene_extraction_tests.cpp` with the capacity
   boundary: N lights all extract with correct values and
   `pointLightCount == N`; the N+1th trips the named check via
   `setFailureHandler()` and writes nothing out of bounds; the directional cap
   still trips at 2.
5. Asset System tests: both gates accept exactly N and reject N+1 with
   `TooManyLights`, asserted against the shared constant so the gates cannot
   drift.

**Risk gates.** The eleven shaders still declare `pointLights[4]` at the end of
this milestone, so the C++ and shader layouts **disagree by design** until M2.
Therefore: M1 does not run the GPU suite as a pass criterion, and its commit
message must say so explicitly. If M1 is somehow green on GPU tests, that is
suspicious, not reassuring — it would mean nothing validates the block size.
The GPU-independent suite must be fully green. If any non-GPU test outside the
three tripwires changes behaviour, **stop and report**.

### Milestone 2 — shaders and the mandatory reflection cross-check (`feat:`, Req 5, ruling O4)

1. All eleven `.slang` files: `pointLights[4]` → `pointLights[64]`. Nothing
   else in any of them; the five loops are already bounded by
   `pointLightCount` (Req 6).
2. **Widen `pbr_reflection_cross_check_tests.cpp` from 2 shaders to 11** — the
   O4 ruling's mandatory item. For each shader, a real `slangc
   -reflection-json` run cross-checked against the C++ constants: the
   `pointLights` array's offset (48) and stride (32), the block total (2,224
   for `lit_textured`, 2,512 for the ten PBR variants), and for the ten, the
   light-space pair's new offset. That is the twelfth description — the C++
   header — checked against all eleven.
3. The 14 hardcoded `592` allocations (re-read item 6) become the derived
   constant. `lighting_demo_fixture.cpp:253` already derives and needs no
   change.

**Risk gates.**
- **The cross-check is the gate, and its result goes in the commit message and
  the PR**, per O4. Any shader whose reflected block disagrees with the C++
  constants stops the milestone.
- At the end of M2 the GPU suite must be fully green **and every existing
  golden byte-identical** — every committed scene has at most one point light,
  so any golden movement means a layout change leaked. **Stop and report.**
- If a shader's reflected total is neither 2,224 nor 2,512, the size split in
  re-read item 4 is wrong and the Plan's file list is incomplete. Stop.

### Milestone 3 — the multi-light golden and the regression (`feat:` + `test:`, Req 7)

1. A new `multi_light_demo` scene asset (P4) with **at least 8 point lights**,
   placed with distinct colours and non-overlapping falloff so each
   contribution is individually identifiable, plus its fixture and its
   registration in `assets/CMakeLists.txt`.
2. A GPU test rendering it, with a non-degeneracy assertion that does not
   depend on the golden: at least 8 distinguishable coloured contributions
   reach the framebuffer. A golden alone would not distinguish "8 lights work"
   from "one light and a lucky image".
3. **The golden, in its own separate `test:` commit**, captured against a
   clean already-committed tree — ADR-0042's Initial-baseline-bootstrap rule,
   item 4 — with that category's four evidence items (item 5): human visual
   inspection recorded in the PR, a capture-compare cycle at zero channel
   difference, a real GPU run with Validation Layers clean, and a citation of
   the fixture's channel-tolerance calibration. The Spec 0039 golden is the
   worked precedent.
4. Full regression: Windows Debug and Release builds plus `ctest` in both;
   all existing goldens green; Validation Layers clean in both configurations;
   Android `assembleDebug` — check ASProxy first, and fall back to the locally
   installed Gradle with `--offline`, the Plan 0037/0039 precedent, since the
   wrapper's distribution is still an unfetched `.part` on this machine.
5. If the Android run can cheaply log the device's `maxUniformBufferRange`,
   record it (Spec 0040 R2) — the design rests on the 16,384-byte guarantee
   and no Android device has been measured.

**Risk gates.** A golden that cannot be visually confirmed to show eight
distinct contributions fails item 3's evidence (a), regardless of whether the
compare passes — **stop and report** rather than blessing an image nobody can
read. Validation Layers must be clean specifically at descriptor-update time,
where a block/buffer size mismatch surfaces.

## Files / Modules Touched (expected)

**Created**
- `assets/scenes/multi_light_demo.scene.txt` and its `assets/CMakeLists.txt` entry
- `tests/image_regression/fixture/multi_light_demo_fixture.{h,cpp}`
- `tests/image_regression/multi_light_demo_gpu_tests.cpp`
- `tests/image_regression/golden_generator/multi_light_demo_main.cpp`
- `tests/image_regression/goldens/multi_light_demo/…{png,sidecar.txt}` (own commit)

**Changed**
- `src/runtime/include/atlantis/runtime/scene_extraction.h` — constants, array extent, one `static_assert`
- `src/runtime/src/scene_extraction.cpp` — the point-branch bound and message
- `src/runtime/src/runtime_application.cpp` — **one line** (M0), the allocation
- `src/asset_system/include/atlantis/asset_system/scene_types.h` — the gate constant
- `src/asset_system/src/scene_source.cpp`, `scene_artifact.cpp` — one comparison each
- the eleven `.slang` files — one array extent each
- `tests/runtime/pbr_reflection_cross_check_tests.cpp` — 2 → 11 shaders
- `tests/runtime/scene_extraction_tests.cpp`, `runtime_smoke_gpu_tests.cpp` — the tripwires, plus new capacity tests
- `tests/asset_system/` — the two gates' boundary tests
- the 14 hardcoded `592` sites in 8 test/fixture files
- `tests/image_regression/CMakeLists.txt`, `fixture/CMakeLists.txt`, `golden_generator/CMakeLists.txt`

**Explicitly not touched**
- **RHI public API.** No `rhi/` header changes: no new buffer purpose, no new
  binding, no descriptor change. ADR-0062's stage-visibility contract and
  ADR-0088 Decision item 3 both depend on this.
- **RenderGraph** — no pass, resource or execution change.
- **Platform** and **World** — `world::Light` and `LightKind` are unchanged;
  the cap is not a World concept.
- **Runtime internals beyond the two named lines** — the frame loop,
  lifecycle, manifests, material/environment realization and `scene_load.cpp`
  are untouched. **If M0 needs any Runtime change beyond
  `runtime_application.cpp:790` and the header constants, stop and report.**
- **The shadow path** — `shadow_cast.slang`, its separate `LightSpaceUniform`,
  and `computeShadowLightSpaceMatrices()`. The directional cap stays 1.
- **The `.scene.txt` grammar version** (P5) and the `.ascene` format shape.
- **Light semantics** — intensity units, the attenuation model and the
  imported `range` placeholder, all reassigned by ruling O5.
- **The six declare-only shaders' logic** — their array extent changes; no
  loop is added.

## Sequencing & Dependencies

M0 → M1 → M2 → M3, strictly, and the order is load-bearing rather than
conventional:

- **M0 before M1** by ruling O1, so the correctness fix is bisectable on its
  own and so M1's widening inherits a derived size instead of multiplying a
  wrong literal by 16 (the over-run would grow from 128 to 2,048 bytes).
- **M1 before M2** puts the C++ and shader layouts in deliberate disagreement
  for exactly one commit. The alternative — shaders first — has the same
  property mirrored, with no advantage. What matters is that the window is one
  commit wide, is stated in the commit message, and is closed by M2's
  cross-check.
- **M2 before M3** because the golden is meaningless until the layouts agree.
- Within M3: scene and fixture, then the non-degeneracy test, then the golden's
  own commit against a clean tree, then the dual-platform regression.

No external dependency. Workflow ① has merged; nothing here waits on ③–⑦.

## Verification Checklist

- [ ] **Unit tests (GPU-independent):** the capacity boundary in
      `extractFrameLightingData()` (N extract correctly; N+1 trips the named
      check with no out-of-bounds write; directional still caps at 2); both
      Asset System gates accept N and reject N+1 with `TooManyLights` against
      the shared constant; the derived buffer-size constant equals the
      layout's end (T3, M0).
- [ ] **Reflection cross-check (ruling O4, mandatory):** real `slangc`
      reflection for **all eleven** shaders agrees with the C++ constants on
      the `pointLights` offset and stride and on each block's total; the
      result is recorded in the M2 commit message and in the PR.
- [ ] **Image regression:** the new multi-light golden, captured under
      ADR-0042's Initial-baseline-bootstrap rules in its own commit with all
      four evidence items; the independent ≥8-contribution assertion; **every
      existing golden byte-identical**.
- [ ] **Vulkan Validation Layers clean**, Debug and Release, with attention to
      descriptor-update-time block/buffer size validation.
- [ ] **Runtime GPU tests** unchanged in behaviour across M0 specifically
      (`runtime_smoke_gpu_tests`, `material_realization_gpu_tests`,
      `environment_realization_gpu_tests`).
- [ ] **Other:** Windows Debug + Release full builds and full `ctest` in both;
      Android `assembleDebug` green; `/W4 /WX` clean; the Android
      `maxUniformBufferRange` logged if cheap (R2).

## Open points (for Joint Human Review)

- **Q1 — which fixture backs the multi-light golden?** P4 recommends modelling
  it on `lighting_demo_fixture` (`lit_textured`, derived allocation,
  lighting-focused); a PBR-based fixture would exercise the production
  shading path instead, at the cost of more variables in the image. The Plan
  constrains the choice (a looping shader, a derived allocation) rather than
  fixing it.
- **Q2 — one constant or two?** P3 proposes an Asset System
  `kMaxPointLightsPerScene` and a Runtime `kMaxPointLights` tied by a
  `static_assert`, because ADR-0043 forbids Asset System from including a
  Runtime header. The alternative — a single constant in a Core header both
  include — is cleaner but puts a lighting number in Core, which no other
  lighting constant does today. Recommendation: two plus the assertion.
- **Q3 — should M2 be split per shader family?** Eleven shader edits plus a
  nine-shader cross-check extension is the largest single commit in this Plan.
  Splitting it (the five looping shaders, then the six declare-only) would
  leave the layouts inconsistent across two commits instead of one.
  Recommendation: keep M2 whole.
- **Q4 — the 14 hardcoded `592` sites.** M2 converts them to the derived
  constant, which is the right end state but adds churn to a milestone that is
  already the largest. They could move to M0 instead, where the constant is
  born and where converting them is purely mechanical. **Recommendation: move
  them to M0** — it makes M0 slightly bigger but purely mechanical, and leaves
  M2 to the shaders alone. Raised because it contradicts the milestone split
  as the task framed it.

## Rollback Plan

Each milestone is one revertable commit (M3 is two: code, then golden), and
the order makes reverting safe from the top: reverting M3 removes a scene, a
fixture and a golden that nothing else references; reverting M2 returns the
shaders to `[4]` and leaves an over-provisioned C++ array that nothing reads
past index 3; reverting M1 returns the capacity to 4; reverting M0 restores the
under-allocation, which is why it is the one revert that should not be done
alone. Nothing here changes persisted data — no artifact format, no cooked
bytes, no `.scene.txt` grammar — so no revert requires a re-cook or content
migration.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- The PR records the eleven-shader reflection cross-check result explicitly
  (ruling O4 makes it a verification item, not a note).
- The PR carries ADR-0042's "Initial baseline bootstrap" category and all four
  evidence items for the new golden, in the two-commit order.
- The PR states the M0 fix as its own disclosed correctness change, with the
  finding that no golden covered it and the new test that now does.
- The PR states the one-commit window in which the C++ and shader layouts
  disagree (M1 → M2), and that no release artifact is built from it.
- The PR records the recount corrections in this Plan's re-read items 4, 6 and
  8 against Spec 0040's own numbers, so the Spec's figures are not silently
  contradicted.
