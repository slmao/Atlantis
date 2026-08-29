# Spec: Lighting Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-08-29
- **Related Plan(s):** [plans/0019-lighting-foundation.md](../plans/0019-lighting-foundation.md)
  (`In Review`, drafted 2026-08-29). D1's own governance gate is
  satisfied — [Spec 0020](0020-mesh-normal-attribute-foundation.md)'s
  own Implementation PR ([PR #93](https://github.com/slmao/Atlantis/pull/93))
  merged 2026-08-29, so this Plan may now be drafted. **Reaching `In
  Review` on this Plan does not itself authorize Implementation** — a
  separate Human Review of the Spec+Plan pair together is still
  required before Implementation may begin, per
  [AGENTS.md](../AGENTS.md)'s own Golden Rule.
- **Related ADR(s):** [ADR-0061](../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md) (`Accepted`), [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md) (`Accepted`)
- **Human Review Approval (2026-08-29):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-29, accepting this document's
  own "Decisions for Human Review" section in full, per its own final,
  corrected recommendations produced during one final, targeted review
  round (below), and accepting [ADR-0061](../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md)/[ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
  (both `Proposed` → `Accepted`) in the same pass. **This approval
  authorizes drafting Spec 0020 ("Mesh Normal Attribute Foundation")
  only. It does not authorize Plan 0019 or any Implementation.** Plan
  0019 may begin only after Spec 0020's own Implementation PR has
  merged — see D1's own governance-gate text, which this approval
  accepts verbatim, not as a placeholder to be revisited later.

## Final Review Round (2026-08-29) — closed findings, recorded before approval

A single, targeted final review round examined twelve specific areas of
this Spec's own real-code implementability and honesty, focused on the
mesh-normal governance gate, GPU resource lifetime/visibility, and
whether this Spec's own claims about runtime light mutation matched what
its own design actually does. Every item below was closed at the Spec
level; two items (D1, D5-adjacent frame-data ownership) produced a real,
disclosed *design change* from this Spec's own first draft, not merely
clarified wording — recorded here so the change is visible, not silently
folded in:

1. **D1's own dependency wording corrected from "Approved" to a real
   governance gate.** The first draft said this Spec's own Plan is
   blocked once its prerequisite reaches `Approved`; that is
   insufficient — an `Approved`-but-not-yet-`Implemented` prerequisite
   Spec has no real, buildable API yet, so a Plan drafted against it
   would depend on a contract that does not exist in source. Corrected:
   Plan 0019 is blocked until Spec 0020's own **Implementation PR
   merges** (Spec 0020 both `Approved` and its Plan `Approved`, real
   code landed and verified) — not merely until Spec 0020 itself reaches
   `Approved`. This Spec's own header and D1 now state this precisely,
   and `specs/README.md` registers `Spec 0020 — Mesh Normal Attribute
   Foundation` explicitly as the next, already-identified, not-yet-
   drafted spec (a deliberate, disclosed exception to Section B's own
   general "no number pre-assigned before drafting" rule, made because
   Spec 0019's own approval is real and needs a stable, named
   dependency to point at — see `specs/README.md`'s own note recording
   this exception).
2. **The frame lighting data's own update model was not honestly
   closed — a real design change, not a wording fix.** The first draft
   re-derived and rewrote the light array into the shared uniform
   buffer unconditionally every frame, mirroring the camera data's own
   existing per-frame write, without ever stating plainly whether a
   runtime change to a `Light` component would or would not reach the
   GPU, and without any test proving either answer. Corrected: this
   Spec now commits explicitly to the **static-snapshot** model —
   `RuntimeApplication` captures the frame lighting array **exactly
   once**, on the first successful frame (immediately after
   `World::updateTransforms()` first runs, alongside Phase 2 material
   realization), and never rewrites it again for that
   `RuntimeApplication` instance's own lifetime. A runtime change to any
   `Light` component (via `World::setLight()`) after that point is
   **not** reflected in any rendered frame — reloading the scene (or
   restarting Runtime) is required. This is now stated as an explicit
   Requirement, a Non-Goal, a Known Limitation, and has its own,
   dedicated negative test (D9/D10, Known Limitations, below). A real
   future "Dynamic Frame Uniform Updates" capability is named as its own
   disclosed future candidate, not solved here. The camera view/
   projection portion of the *same* buffer is completely unaffected —
   still recomputed and rewritten every frame exactly as it already is
   today, including its own existing resize/aspect-ratio behavior,
   which this Spec does not touch (see D9's own explicit "resize is
   unaffected" note).
3. **World `Light` semantics were underspecified — now fixed
   precisely.** D2 is rewritten with an exact field list, an exact
   direction/position derivation formula (matching `Camera`'s own real
   extraction code, cited by line), an exact, evidence-based statement
   of which transform properties (parent composition, negative scale,
   non-uniform scale, shear) are safe for light direction/position
   extraction specifically and why (a real linear-algebra argument, not
   an assertion), an exact error-precedence order (matching `World::validate()`'s
   own real, current code), and an exact degenerate-transform rejection
   (mirroring `DegenerateCameraForward`) so Runtime never guesses a
   direction or silently normalizes an near-zero vector.
4. **Over-limit lights: corrected from deterministic truncation to a
   hard cook/decode-time error.** The first draft's own truncation
   recommendation risked silently masking a real scene-authoring
   mistake (an author who did not realize a cap exists gets a
   dimmer-than-intended, silently-incomplete scene with no visible
   signal beyond a log line). Corrected, matching Spec 0018 D4's own
   "no silent fallback for a genuinely present-but-broken reference"
   reasoning: a scene declaring more lights of a given kind than the
   fixed maximum is a **cook-time and decode-time structural error** —
   the scene never cooks (or, for a hand-corrupted artifact, never
   decodes) rather than silently rendering with fewer lights than
   authored.
5. **Lighting math was named but not written out — now a complete,
   exact, per-value-testable specification** (D6): the diffuse term,
   the directional-light sign convention, the point-light vector/
   epsilon/attenuation/range formula, the color/intensity value ranges,
   the explicit absence of an ambient term, the exact clamp location,
   and the no-tone-mapping boundary are all now stated as literal
   formulas, not prose description.
6. **Normal transform's own safety condition was too narrow.**
   The first draft's "uniform scale" check missed that a *uniform
   negative* scale (a point reflection) is also mathematically safe
   under a direct (non-inverse-transpose) 3×3 transform, while a
   *single-axis* negative scale is not simply "non-uniform" in the same
   sense — both cases needed a real linear-algebra check, not an
   intensity-of-belief guess. Corrected: D7 now uses the exact, general,
   provably-sufficient condition — the world matrix's own upper-left 3×3
   columns are mutually orthogonal and equal in length (a "conformal"/
   similarity transform, which includes uniform scale of either sign and
   pure rotation/reflection, and excludes non-uniform scale and shear)
   — checked on the *fully composed* world matrix (not merely an
   entity's own local `Transform.localScale`), since a parent's own
   non-uniform scale can introduce shear a child's own local transform
   never had. This is a Runtime-extraction-time, per-entity, per-frame
   check (not a scene-validation-time one), since it depends on the
   full, composed hierarchy only `World` can resolve.
7. **Material/shader boundary detail was named but not specified** —
   D8 (formerly D4's own material scope, renumbered for clarity in this
   round) now states the exact CMake target shape, the exact expected
   descriptor contract (three binding entries, not two — the shared
   uniform binding reflects from *both* stages), the exact vertex-input
   schema shape (deferred only on Spec 0020's own exact byte offsets,
   never on its existence), and confirms, precisely, that no material
   artifact schema-version bump is required.
8. **Verification was missing CPU-level math tests and negative
   coverage for the two new hard-error/skip conditions** (D10): now
   requires hand-computed-expected-value unit tests for every formula
   in D6, and dedicated negative tests for the over-limit hard error
   (finding 4) and the non-conformal-transform skip (finding 6).
9. **ADR scope re-checked: neither ADR-0061 nor ADR-0062 decides
   anything about Spec 0020's own eventual mesh-normal schema, stride,
   offset, or migration** — confirmed by re-reading both ADRs in full
   during this round; both were already scoped correctly and needed no
   change on this point, stated here as a closed check, not a silent
   assumption.

No unresolvable architectural conflict was found. Every finding above
was closed with a real, evidenced fix within this Spec's own existing
scope — none required inventing new capability this codebase does not
already have, beyond the one, already-disclosed RHI stage-visibility
widening (D5/ADR-0062, unchanged by this round).

## Summary

Close the loop Spec 0018 named as its own successor: a `World`-loaded
scene can bind a real, asset-sourced textured `Material` per entity
(Spec 0018), but nothing in this codebase computes or applies any actual
*lighting* — every drawn pixel today is either a raw vertex color or an
unmodified sampled texel, with no light source, no surface normal, and
no lighting math anywhere in the render path. This Spec adds the
smallest real, scene-driven lighting closed loop: `World` gains a third
optional per-entity component, `Light` (Directional or Point, one
closed, tagged shape — not a variant type, not a generic ECS registry);
the Scene Asset format gains an optional light node; Material gains one
new, minimal kind, `LitTextured`, whose fragment shader reads a
Runtime-computed, **one-time-captured** array of active lights and
applies minimal Lambertian diffuse shading against a real, asset-sourced
vertex normal. No PBR, no shadows, no IBL, no post-processing, no
runtime light mutation — a closed, honest, minimal proof that a
scene-authored light actually changes what a Runtime-rendered pixel
looks like, end to end, for the lifetime of one scene load.

**Two load-bearing, evidence-grounded findings this Spec's own drafting
and final review made, stated up front rather than buried:**

1. This codebase has **no vertex normal attribute anywhere** — Asset
   System's static mesh format (schema version 2, Spec 0017) is
   position(3)+color(3)+UV0(2), 32 bytes, full stop. Genuine, non-faked
   diffuse lighting is mathematically impossible without a real
   per-vertex normal. D1 addresses this directly: **this Spec does not
   itself add one.** A separate, prerequisite Spec 0020 ("Mesh Normal
   Attribute Foundation") must be drafted, approved, planned,
   implemented, and merged before this Spec's own Plan may begin.
2. The engine's own existing frame uniform buffer is trivially
   writable/mappable at any point in a frame (`Buffer::mappedData()`,
   host-visible, host-coherent, mapped once at construction) — but
   *when* Runtime chooses to write light data into it is a real design
   choice this Spec must commit to, not leave implicit. This Spec
   commits to a **one-time, static snapshot**, captured once per scene
   load — see D1 of the Final Review Round above and D9 below for the
   full contract.

## Goals

1. `World` gains a third optional per-entity component, `Light` — one
   closed, tagged shape (Directional or Point), following the exact
   flat-struct precedent `Camera`/`Renderable` already establish; no
   generic ECS registry; at most one `Light` per entity.
2. The Scene Asset authoring/artifact format gains an optional light
   node, following the exact version-bump/no-dual-version-reader
   discipline every prior Scene Asset extension already used, with a
   real, structural (not merely conventional) cap on the number of
   lights of each kind a scene may declare.
3. Material gains exactly one new, minimal kind, `MaterialKind::LitTextured`
   — a closed, Runtime-resolved shader-pair mapping, matching
   `UnlitTextured`'s own exact precedent, reusing the existing Material
   DTO shape with zero new fields and zero material artifact
   schema-version bump.
4. Runtime computes one real, one-time array of active lights
   (captured once per scene load, not re-derived every frame) and makes
   it available to a `LitTextured` Material's own fragment shader,
   applying minimal, exactly-specified Lambertian diffuse shading
   against a real vertex normal.
5. A real, new, minimal scene+light+material image-regression fixture
   proves the whole path end to end, distinguishing a Directional
   light's own contribution from a Point light's own contribution in
   the captured pixels.
6. Zero change to any existing, committed golden (`minimal_cube`,
   `world_scene`, `textured_quad`, `material_demo`).

## Non-Goals

- **Any mesh normal authoring, DTO, artifact, or loader change of any
  kind.** This Spec consumes Spec 0020's own final, `Approved` and
  implemented normal contract; it does not itself define, approve, or
  implement any part of it, and does not pre-decide Spec 0020's own
  stride, byte offset, schema version, or migration strategy.
- **Any position-derived, derivative-derived, or shader-hardcoded/
  assumed-flat normal**, at any point, for any reason, including as a
  temporary stand-in — explicitly forbidden, not merely discouraged.
- **Any runtime reflection of a `Light` component change after its
  scene's own frame lighting data has been captured.** A change to any
  `Light`'s own color, intensity, range, or owning-entity transform,
  made after the one-time capture point, is not reflected in any
  rendered frame for that `RuntimeApplication` instance — reloading the
  scene (or restarting Runtime) is required to see the change. See D1
  of the Final Review Round, D9, and "Known Limitations" below.
- PBR, metallic/roughness, or any physically-based shading model.
- Shadows or shadow mapping of any kind.
- Image-based lighting (IBL) or environment maps.
- Normal mapping or a tangent vertex attribute — a later phase beyond
  even Spec 0020's own scope.
- An ambient/fill light term of any kind — see D6; an unlit-facing
  surface renders pure black this round, by explicit design, not
  omission.
- Emissive or transparent materials, or any second `LitColored`
  (untextured) material kind — see D8.
- Clustered, Forward+, or deferred rendering, or any light-culling
  strategy beyond a fixed, small, one-time-computed active-light array.
- GPU-driven light culling of any kind.
- Tone mapping, gamma-encode, HDR intermediate targets, or any other
  post-processing.
- Animation of any kind.
- A material graph, shader graph, or any user-composable shading system.
- Hot-reload of any kind, an editor, or runtime asset mutation.
- A distributable, cross-session Asset Catalog/Registry, or any
  rename-stable identity beyond the existing path-derived `AssetId`.
- Android, iOS, or Linux implementation.
- A new third-party dependency or a new top-level module.
- Multiple lights sharing one GPU resource "dedup" concept — unlike a
  shared texture/material (Spec 0018 D10), two light entities are never
  interchangeable.
- Prewiring any interface, field, or abstraction for Shadow Foundation,
  PBR Material, IBL, Post-processing, or Dynamic Frame Uniform Updates —
  each remains its own, independent, unblocked future Spec (see D12,
  Out of Scope / Future Work).

## Requirements

### Functional

- `atlantis::world::LightKind` (`Directional`, `Point`) and
  `atlantis::world::Light` — a flat struct (exact field list: D2).
  `World::setLight()`/`removeLight()`/`getLight()` (returning by value,
  error precedence per D2), and `lightEntities()`, a deterministic
  accessor mirroring `renderableEntities()` exactly (ascending
  slot-index order, a fresh snapshot per call). At most one `Light` per
  entity — the same fixed-slot storage `Camera`/`Renderable` already
  use.
- A scene node may optionally declare a light (D3's own exact grammar
  shape). `cookScene()`/`decodeScene()` resolve and validate it with
  the same discipline every other node-level field already has
  (non-finite/negative rejection, independent decode-time
  re-validation, never trusting the cooker) **plus a hard,
  structural cap on declared light count per kind** (D3/finding 4,
  above) — a scene declaring more than the fixed maximum of either kind
  fails to cook (and, independently, fails to decode if hand-corrupted
  past the cap), never silently truncates.
- `atlantis::asset_system::MaterialKind::LitTextured` — a new
  enumerator on the existing closed `MaterialKind` enum, requiring
  **no new `MaterialAssetData` field and no material artifact
  schema-version bump** (D8). Runtime maps it to a new, fixed, built-in
  `lit_textured` shader pair, following `UnlitTextured`'s own exact
  CMake-unconditional-production-shader precedent.
- A new Runtime-private extraction function computes the active-light
  array **exactly once**, on the first successful frame (immediately
  after `World::updateTransforms()` first runs), from `World`'s own
  `lightEntities()` and each entity's own current world matrix — never
  re-derived on any subsequent frame, and never storing a redundant,
  separately-tracked direction/position field anywhere.
- This one-time light array is packed into the *same* existing uniform
  buffer the camera view/projection matrices already occupy (D9) — no
  new descriptor binding, no new `Buffer`, no new RHI resource-creation
  call — written once via the same `mappedData()`-write pattern the
  camera data already uses each frame, then never rewritten again for
  that `RuntimeApplication` instance's own lifetime. The camera portion
  of the same buffer is unaffected and continues its own existing,
  unchanged per-frame rewrite (including its own existing resize/
  aspect-ratio behavior).
- The one RHI-level change this Spec requires (D5, unchanged from this
  Spec's own first draft; see ADR-0062): the existing, single
  uniform-buffer descriptor binding's own Vulkan `stageFlags` widens
  from vertex-only to vertex-and-fragment, unconditionally, for every
  `Pipeline` — a disclosed, minimal, one-line `vulkan_device.cpp`
  change; `PipelineCreateParams`'s own public shape is unchanged; every
  existing shader continues to pass unaffected.
- A new `lit_textured` built-in shader pair samples the material's own
  texture (unchanged from `UnlitTextured`), reads the one-time light
  array from the shared uniform binding, computes minimal Lambertian
  diffuse shading per active light using a real, Spec-0020-sourced
  vertex normal (transformed per D7), and writes the summed, clamped
  result as the final fragment color — the exact formulas are fixed in
  D6, not left to Implementation's own judgment.
- A new, minimal, independent scene + light + material image-regression
  fixture and its own first golden (ADR-0042's "Initial baseline
  bootstrap" category), proving the real end-to-end path, with a
  captured-pixel-level distinction between the Directional light's own
  contribution and the Point light's own contribution (D10).

### Non-functional

- **Performance:** not a goal of this round. The one-time light-array
  capture costs one extra, single-frame CPU computation at scene-load
  time — strictly cheaper than the per-frame re-derivation this Spec's
  own first draft proposed, not merely equally cheap.
- **Memory:** the frame lighting data adds a small, fixed number of
  bytes to the existing camera uniform buffer (exact byte layout fixed
  at Plan time against D5/D6's own std140-alignment convention and
  fixed maximum counts, below) — no unbounded growth, no per-light heap
  allocation.
- **Portability (within the Vulkan-only Phase 1 constraint):** the
  frame lighting struct's own CPU-side layout is unconditionally
  little-endian/std140-compatible, matching every other Asset-System/
  Runtime binary contract's own discipline.
- **Other:** zero new error enumerator reused where an existing one
  already fits; new enumerators are added only where no existing one
  covers a genuinely new failure mode (D3, D11).

## Known Limitations (stated explicitly, not left implicit)

- **No runtime light mutation is reflected in any rendered frame.**
  `World::setLight()` remains callable at any time (it is ordinary
  public `World` API, unrestricted), and a caller that calls it after
  the one-time frame-lighting-data capture will see the mutation
  reflected in `World`'s own state — but **not** in any subsequent
  rendered frame, since Runtime never re-reads `World`'s own light state
  after that one capture. This is a real, deliberate Phase 1 boundary
  (Final Review Round finding 2), not an oversight, and has its own
  dedicated negative test (D10).
- **Resize does not affect, and is not affected by, this Spec.** The
  existing camera projection matrix's own per-frame recomputation from
  the current window/target extent (unrelated to lighting, already
  shipped, Spec 0013/0014's own behavior) continues completely
  unchanged — this Spec neither reads nor writes anything related to
  extent/aspect, and does not attempt to fix or alter that existing
  behavior, approved or not, as part of this Spec's own scope.
- **A `LitTextured`-bound entity whose fully composed world transform is
  not a conformal (orthogonal-columns, equal-length) transform is
  skipped for drawing, every frame, logged once** (D7) — never rendered
  with an incorrect normal.
- **No ambient/fill light** — a surface with no active light facing it
  renders pure black (D6).
- **No tone-mapping or gamma-encode** — a scene whose combined light
  contribution exceeds the display format's own representable range
  clips with no rolloff (unchanged from every prior Spec's own existing
  "no HDR target, no post-processing" boundary).

## Pre-draft verification against real, current source

Confirmed directly against `main` at Spec-drafting time (2026-08-29) and
re-confirmed during this Spec's own final review round, by reading full
files, not from memory:

- **Mesh vertex layout, `World`'s component model, Camera's direction
  extraction, the scene grammar's per-node dispatch, `MaterialKind`'s
  closed shape, RHI's single uniform-buffer binding and its hardcoded
  vertex-only `stageFlags`, `ShaderStage`'s single-value reflection
  shape, and the camera buffer's existing per-frame update pattern** —
  all confirmed as originally recorded in this Spec's own first-draft
  verification pass; unchanged by this round, re-checked, not
  re-derived from scratch.
- **`WorldError`'s own real, current enumerator list and `validate()`'s
  own real precedence** (`world_error.h`, `world.cpp`, both read in
  full this round): `enum class WorldError { InvalidEntity,
  WouldCreateCycle, NoCameraComponent, WrongWorld, NoRenderableComponent
  };` — `World::validate(EntityId)` checks `WrongWorld` (identity
  mismatch) **first**, then `InvalidEntity` (stale generation,
  out-of-range index, or a dead slot) **second**; every accessor
  (`getCamera()`, `getRenderable()`) calls `validate()` first and only
  then checks its own component-presence condition
  (`NoCameraComponent`/`NoRenderableComponent`) as a **third**, later
  step. This fixes the exact, real precedence D2's own `getLight()`/
  `NoLightComponent` design follows — not invented, transcribed.
- **`Buffer`'s own real, current public contract** (`buffer.h`, read in
  full this round): `mappedData()` returns a pointer to host-visible,
  host-coherent memory, "mapped once, at construction — never
  remapped," directly writable "at any time" with "no explicit
  flush/invalidate call required." Confirms directly, not by inference,
  that a *static, one-time* write and a *per-frame, unconditional*
  write are **both** already fully supported by the existing RHI
  `Buffer` contract with zero new API either way — the choice between
  them (Final Review Round finding 2) is a pure Runtime-side design
  decision, not gated by any RHI capability gap.
- **`Renderer`'s real vertex-input/varying data flow** (`textured_quad.slang`,
  `material_realization.cpp`'s own `Vertex`/schema construction, both
  re-read this round): confirmed the existing `Varying` struct carries
  only clip-space `position` and `uv` — **no world-space position or
  world-space normal is passed from vertex to fragment stage today**,
  confirming D6/D8's own requirement that the new `lit_textured` vertex
  shader must compute and pass both as new varyings (using the
  already-existing `objectToWorld` push constant — no new push-constant
  range is required, only new vertex-shader-side computation from the
  one that already exists).

## Proposed Design

### The minimal closed loop

```
scene authoring (.scene.txt)
  node: ... light=directional color=<r,g,b> intensity=<f>
  node: ... light=point color=<r,g,b> intensity=<f> range=<f>
        |
        v
cookScene() -- validates finite/non-negative/in-range fields, resolves
  LightKind token, rejects the whole scene outright if declared light
  count per kind exceeds the fixed maximum (finding 4)
        |
        v
scene artifact (version 3) -- new light slot, decode-time re-validated
  independently of the cooker, including the same per-kind count cap
        |
        v
World::Light (via fromValidatedSceneData(), infallible, unchanged in
  kind) -- direction/position NOT stored, re-derived from the owning
  entity's own world matrix at the ONE point Runtime ever reads it
        |
        v
Runtime, first successful frame only: a new Runtime-private
  light-extraction function walks World::lightEntities() in its own
  deterministic order, reads each light's own current world matrix +
  Light component, produces a fixed-size array of active lights' own
  {direction-or-position, color, intensity, range} -- captured ONCE,
  never again
        |
        v
packed into the SAME existing camera uniform Buffer, appended after the
  existing view/projection floats -- written ONCE (unlike the camera
  portion of the same buffer, which continues its own existing,
  unrelated per-frame rewrite)
        |
        v
MaterialKind::LitTextured's own built-in lit_textured shader pair
  (fragment stage now ALSO declared against binding (0,0), per
  ADR-0062's own RHI stage-visibility widening) -- vertex stage computes
  world-space position/normal as new varyings from the existing
  objectToWorld push constant; fragment stage samples the material's
  own texture, reads the one-time light array, computes the exact D6
  formula per active light, sums, clamps, writes final color
        |
        v
headless golden (new lighting_demo fixture, directly linking
  Atlantis::RuntimeHost per Spec 0018's own D12 precedent, exercising
  the real, shared extraction/realization functions -- never a
  fixture-private reimplementation) + windowed Runtime (bootstrap-scene
  switch left unswitched, D10, matching Spec 0018's own P15 precedent)
```

## Decisions for Human Review

Numbered to match this Spec's own governing questions one to one — the
twelve areas named for this Spec's own drafting and final review.

### D1. Mesh normal prerequisite — a real governance gate, not a soft recommendation

**Decision (revised, Final Review Round finding 1):** a separate,
prerequisite Spec, **"Spec 0020 — Mesh Normal Attribute Foundation,"**
must be drafted, reach `Approved`, have its own Plan reach `Approved`,
and have its own **Implementation PR merge** before this Spec's own Plan
0019 may be drafted. Reaching `Approved` on Spec 0020 alone is **not**
sufficient — an approved-but-unimplemented spec has no real, buildable
API; a Plan drafted against it would depend on a contract that does not
yet exist in source, exactly the kind of "Plan depends on real, current
code" discipline every prior Plan in this codebase (Spec 0018's own
Pre-draft verification most recently) already insists on.

**This Spec (0019) never itself contains:** any normal authoring
grammar field, DTO field, artifact byte, schema version, or migration
step for Spec 0020's own mesh-normal work. It never pre-decides Spec
0020's own stride, byte offset, or version number. Everywhere this Spec
references "a real, asset-sourced vertex normal," it means *whatever
Spec 0020's own final, `Accepted` contract turns out to be* — this
Spec's own Plan (0019, drafted only after that contract exists and is
merged) consumes it as a fixed, given input, the same way Spec 0018's
own Plan consumed Spec 0017's already-merged UV0 contract without
redeciding it.

**Explicitly forbidden, restated:** no position-derived normal, no
derivative-derived (screen-space or tangent-approximated) normal, and
no shader-hardcoded/assumed-flat constant normal, at any point, for any
reason, including as a temporary stand-in pending Spec 0020. If Spec
0020 is delayed or rejected, this Spec's own Plan simply does not begin
— it is not "worked around."

**Registered explicitly in `specs/README.md`, as a disclosed exception
to that document's own general numbering rule:** Section B's own
maintenance rule states "no formal spec number is pre-assigned to any
entry; numbers are assigned only when a real spec is drafted." This
Spec's own approval creates a real, named dependency on a spec that does
not yet exist as a file — `specs/README.md` registers `Spec 0020 —
Mesh Normal Attribute Foundation` explicitly, by number, ahead of its
own drafting, as a stated, disclosed, one-time exception made because
this Spec's own `Approved` status needs a stable name to point its own
blocking dependency at, not a silent renumbering risk. See
`specs/README.md`'s own note recording this exception in full.

**Why (b) [a separate Spec] over (a) [bundling normal work directly
into this Spec] and (c) [a faked normal], restated from this Spec's own
first-draft reasoning, unchanged by this round:** option (a) would add
a full mesh-format version bump, a repository-wide sweep, a disclosed
migration decision, and two ADR amendments (ADR-0045, ADR-0058) on top
of this Spec's own already-substantial remaining scope — comparable in
size to Spec 0018's own 17-Milestone unit, and this Spec's own final
review reconfirmed that combining both remains harder to review, plan,
and verify as one coherent delivery than keeping them separate. Option
(c) is independently rejected on the merits, not merely because this
Spec's own drafting brief forbids it: either approach would make the
resulting "lighting" pixels not actually driven by asset-authored
geometry data, undermining this Spec's own central claim.

### D2. World `Light` component shape and semantics — precise, complete, evidence-grounded

**Decision, exact shape:**

```cpp
enum class LightKind { Directional, Point };

struct Light {
  LightKind kind = LightKind::Directional;
  Vec3 color{1.0f, 1.0f, 1.0f};  // each component in [0, 1] -- D6
  float intensity = 1.0f;         // finite, >= 0 -- D6
  float range = 0.0f;             // Point only; ignored for Directional,
                                   // and rejected outright if authored on
                                   // a Directional node -- D3
};
```

`World::setLight(EntityId, Light) -> Result<monostate, WorldError>`,
`removeLight(EntityId) -> Result<monostate, WorldError>`,
`getLight(EntityId) const -> Result<Light, WorldError>` (returns by
value, matching `getCamera()`/`getRenderable()`'s own identical
contract exactly — `World` never returns a reference or pointer into
its own internal storage, unchanged invariant). `lightEntities() const
-> std::vector<EntityId>` — a fresh snapshot in ascending slot-index
order, verbatim-mirroring `renderableEntities()`'s own exact doc
comment and implementation shape.

**At most one `Light` per entity** — the same fixed-type, single-slot
storage `Camera`/`Renderable` already use; `Light` is a third such slot,
not a list, not a generic component registry.

**Direction/position — never stored, always re-derived, exact formula:**
for a `Directional` light, the world-space light direction is
`normalize(-column2 of the owning entity's own current world matrix)`
— the **identical formula and sign convention** `extractCameraMatrices()`
already uses for Camera's own forward vector (`scene_extraction.cpp`,
confirmed by direct citation), meaning a light is authored/aimed exactly
like a camera is aimed (rotate the entity; the light "shines toward"
its own local -Z axis in world space). For a `Point` light, the
world-space position is `column3 (translation) of the owning entity's
own current world matrix` — again identical to Camera's own eye
extraction. Neither value is ever stored on the `Light` component
itself.

**Parent transform, negative scale, non-uniform scale, and shear — all
unrestricted for light direction/position extraction specifically, with
a real argument, not an assertion:** extracting a single matrix column
(either `-column2`, normalized, or the raw `column3`) never requires the
matrix's *other* columns to be orthogonal or uniformly scaled — this is
exactly the property `scene_extraction.cpp`'s own existing comment
already states for Camera ("eye/forward-only camera extraction, never a
right/up column, so it stays correct under a sheared hierarchy"). A
light entity under a non-uniformly-scaled or sheared parent still
produces a mathematically well-defined direction/position by this same
argument. **This is explicitly distinct from D7's own mesh-normal-
transform restriction** — a per-fragment surface property, not a
light-source property — and this Spec states the distinction explicitly
so the two are never confused with each other.

**The one real restriction: a degenerate transform is rejected, never
silently normalized.** If a `Directional` light's own `-column2` has
near-zero length (a degenerate transform, e.g. zero scale on the
relevant axis), Runtime's extraction function returns a new,
dedicated `SceneExtractionError::DegenerateLightDirection` (mirroring
`DegenerateCameraForward`'s own exact precedent) rather than normalizing
a near-zero vector to an arbitrary or NaN result. `Point` lights need no
equivalent check — a raw translation column is always well-defined,
regardless of any other degeneracy in the matrix.

**Entity destroy/cascade:** a `Light` component is destroyed exactly
like `Camera`/`Renderable` already are on `destroyEntity()`'s own
existing cascading-delete mechanism — no new logic, no special-casing.

**Error precedence, exact, matching `World::validate()`'s own real,
current code:** `WrongWorld` (the handle's own identity belongs to a
different, live `World` instance) is checked **first**; `InvalidEntity`
(a stale generation, an out-of-range index, or a dead slot) is checked
**second**; only once both pass does `getLight()` check the
component-presence condition, returning the new
`WorldError::NoLightComponent` (mirroring `NoCameraComponent`/
`NoRenderableComponent`'s own exact naming precedent) as the **third
and final** check.

**Traversal order:** `lightEntities()`'s own ascending slot-index order
is the sole source of deterministic light ordering anywhere in this
Spec's own design — never an `std::unordered_map`, matching every prior
Spec's own identical discipline.

### D3. Scene authoring/artifact — a standalone light node, a hard structural cap, independent cook/decode validation

**Decision:** `atlantis_scene_source_version: 2 → 3`. A node's own
trailing token group gains a fourth, mutually exclusive shape,
`light=<directional|point> color=<r> <g> <b> intensity=<f> [range=<f>]`
— present only when the node carries no `mesh=`/`camera_*=` group,
matching the real, confirmed structural shape of today's parser (a new
`tokens.size()` case, disjoint from 11/12/13/14, added as a fourth
`else if` branch). Version 2 sources/artifacts are rejected outright —
no dual-version reader.

**Co-location with mesh/camera on one node — unchanged from this Spec's
own first draft:** not supported this round; a visible light fixture is
composed via the existing parent/child hierarchy (a `Renderable`-only
node and a `Light`-only node, parented together), achieving the one real
use case a same-node combination would provide with zero grammar
change.

**Validation, cook-time and decode-time, independent of each other,
including the hard structural cap (Final Review Round finding 4):**

- `color`'s three components must each be finite and in `[0.0, 1.0]`;
  `intensity` must be finite and `>= 0.0` (D6). `range` must be finite
  and strictly `> 0.0` for `Point`, and is rejected outright (a distinct
  grammar-level error, not silently ignored) if present on a
  `directional` light line.
- **A scene declaring more than 1 `directional`-kind light node, or more
  than 4 `point`-kind light nodes (D5's own fixed maximum, below), fails
  to cook outright** — a new `SceneSourceParseError::TooManyLights`
  (one shared enumerator covering both kinds, matching this Spec's own
  "reuse where identical in kind" discipline: exceeding either cap is
  the identical *kind* of failure, only the specific kind/count differs,
  which the error's own accompanying log message states). This is a
  cook-time, whole-scene, post-node-collection check, run once all
  nodes have been parsed — never a per-node check.
- **Decode-time independently re-validates the identical cap**, never
  trusting the cooker, mirroring `MaterialWithoutRenderable`'s own
  precedent exactly: a new `SceneArtifactDecodeError::TooManyLights`,
  driven by a real, hand-corrupted artifact byte buffer in its own test,
  not merely a reachable-in-theory path.
- **Why a hard error, not deterministic truncation (Final Review Round
  finding 4, restated with its own full reasoning here):** a scene
  author who declares more lights than the fixed maximum has almost
  certainly made a real content mistake (accidental duplication, or an
  unrealized cap) — truncating silently (even in a fully deterministic
  order) would let that scene "successfully" cook and render, dimmer or
  differently lit than authored, with no signal beyond a log line
  nothing in this Spec's own verification path checks. This mirrors
  Spec 0018 D4's own reasoning for why a present-but-unloadable material
  reference is scene-load-fatal, not a silent fallback: a genuinely
  broken/exceeded reference should stop the scene from reaching
  `Running`, not degrade quietly.
- `intensity == 0` is accepted (not itself an error) — a light
  contributing nothing is a valid, if pointless, authoring choice (a
  disable-without-delete convenience), not a validation failure.

**Renderable/Camera/Light co-existence at the `World` level:**
unrestricted — nothing in D2's own component model prevents an entity
from carrying `Renderable` and `Light` simultaneously; the *grammar's*
own one-trailing-group-per-node limit above is the only real
restriction, and it applies to node authoring, not to `World`'s own
representable state.

### D4. (renumbered — see D8 for Material scope; D4 folded into D3/D8 above, not left as a placeholder)

This Spec's own original ten-decision numbering placed Material scope at
D4; this round's revision folds that content into D8 (below), alongside
the additional CMake/descriptor-contract/vertex-input detail the final
review requested for the same topic, so the full Material/shader
boundary decision is stated once, completely, in one place rather than
split across two sections.

### D5. Frame lighting data — the existing single uniform buffer binding, its own fixed maximum counts, and the one real RHI stage-visibility decision

**Decision, unchanged in its own RHI-boundary reasoning from this
Spec's own first draft, re-confirmed this round:** the one-time light
array (D9) is packed into the *same* uniform buffer the camera view/
projection matrices already occupy — appended after the existing 32
floats, in a fixed, `std140`-compatible layout. No new descriptor
binding, no `PipelineCreateParams` shape change, no new `Device`
resource-creation call.

**The exact CPU/GPU contract, fixed here, not left open (Final Review
Round finding 4's own "written into the contract" requirement):**

```cpp
// CPU-side layout, std140-compatible, appended after the existing
// 32-float camera view+projection block. Exact byte offsets (subject to
// std140's own vec3-padded-to-16-bytes alignment rule) are fixed at
// Plan time against this exact field list -- not redecided, only
// laid out precisely.
struct FrameLightingData {
  std::uint32_t directionalLightCount = 0;  // 0 or 1 -- D3's own fixed cap
  std::uint32_t pointLightCount = 0;        // 0..4 -- D3's own fixed cap
  // (std140 padding to a 16-byte boundary here)
  struct DirectionalLightGpu {
    float direction[3];  // world-space, normalized -- D2
    float _pad0;
    float color[3];      // D6
    float intensity;     // D6
  } directionalLights[1];
  struct PointLightGpu {
    float position[3];   // world-space -- D2
    float range;         // D6
    float color[3];      // D6
    float intensity;     // D6
  } pointLights[4];
};
```

Only `directionalLights[0..directionalLightCount)` and
`pointLights[0..pointLightCount)` are ever meaningful; unused tail slots
are never read by the shader (bounded by the count fields, not by any
sentinel value in the unused slots themselves).

**Fixed maximum count, restated as a hard contract, not a suggestion:**
**exactly 1 `Directional` slot and exactly 4 `Point` slots** — chosen to
comfortably exceed this Spec's own stated minimum coverage (one of
each) with modest headroom for the verification scene's own two-light-
kind-distinguishing proof (D10), without speculatively over-
provisioning. A scene exceeding either cap is a hard cook/decode-time
error (D3) — this fixed array size is never dynamically resized, and
Implementation may not silently widen it without a Spec amendment.

**The one real RHI change (unchanged from this Spec's own first draft;
see ADR-0062 for the full Decision/Consequences record):** the existing
uniform binding's own Vulkan `stageFlags` widens, unconditionally, from
`VK_SHADER_STAGE_VERTEX_BIT` to `VK_SHADER_STAGE_VERTEX_BIT |
VK_SHADER_STAGE_FRAGMENT_BIT`, for every `Pipeline` this engine creates
— not merely `lit_textured` ones. Legal, zero-cost Vulkan; every
existing shader (`minimal_mesh`, `textured_quad`) continues not
referencing this binding from its own fragment stage, unaffected and
required to continue passing its own existing tests unchanged (D10).
`PipelineCreateParams`'s own public shape is unchanged; no `Material`/
`Renderer` public API changes.

### D6. Lighting math — exact, complete, per-value-testable formulas (no "standard Lambert," no undocumented constants)

**Inputs available in the `lit_textured` fragment shader** (D8's own
vertex-shader contract makes these available as new varyings, computed
from the already-existing `objectToWorld` push constant — no new push
constant):

- `worldPosition` (`float3`) — the fragment's own interpolated
  world-space position.
- `worldNormal` (`float3`, **not yet normalized** — interpolation across
  a triangle does not preserve unit length) — the fragment's own
  interpolated world-space normal, transformed per D7.
- `texColor` (`float4`) — `texturedSampler.Sample(uv)`, unchanged from
  `UnlitTextured`.
- The one-time `FrameLightingData` (D5), read from the shared uniform
  binding.

**The exact algorithm, in order:**

```
N = normalize(worldNormal)
accumulated = float3(0, 0, 0)   // no ambient term -- explicit, see below

for i in [0, directionalLightCount):
    L = -directionalLights[i].direction   // vector FROM the surface
                                            // TOWARD the light source is
                                            // the negation of the
                                            // light's own "shines
                                            // toward" direction (D2)
    ndotl = max(dot(N, L), 0.0)
    accumulated += directionalLights[i].color * directionalLights[i].intensity * ndotl

for j in [0, pointLightCount):
    toLight = pointLights[j].position - worldPosition
    dist = max(length(toLight), kPointLightDistanceEpsilon)  // = 1e-4,
                                                                // guards
                                                                // the
                                                                // zero-
                                                                // distance
                                                                // case
                                                                // with no
                                                                // branch
    L = toLight / dist
    ndotl = max(dot(N, L), 0.0)
    atten = clamp(1.0 - dist / pointLights[j].range, 0.0, 1.0)  // D6's
                                                                   // own
                                                                   // Point-
                                                                   // light
                                                                   // attenuation,
                                                                   // linear-
                                                                   // in-
                                                                   // distance,
                                                                   // explicitly
                                                                   // non-
                                                                   // physical
    accumulated += pointLights[j].color * pointLights[j].intensity * ndotl * atten

finalRgb = clamp(texColor.rgb * accumulated, 0.0, 1.0)   // the one and
                                                            // only clamp,
                                                            // applied
                                                            // here,
                                                            // after
                                                            // combining
                                                            // texture and
                                                            // light, never
                                                            // per-light
return float4(finalRgb, texColor.a)
```

**No ambient/fill term, explicitly:** `accumulated` starts at zero and
is only ever increased by an active light's own contribution — a
surface with no active light facing it (every `ndotl` term zero or every
light too far/wrong-facing) renders pure black. This is a stated,
deliberate Non-Goal (see above), not an omission Implementation should
"helpfully" fix with an undocumented constant.

**Color/intensity value ranges, exact, enforced at cook and decode time
(D3):** each `color` component `∈ [0.0, 1.0]` (a normalized tint, not an
HDR value); `intensity ∈ [0.0, +∞)`, finite, no upper bound enforced at
authoring time (the final `clamp(..., 0, 1)` above bounds the visible
effect regardless of how large `intensity` is authored).

**Point-light attenuation, restated as the one formula, no
alternative:** `clamp(1.0 - dist / range, 0.0, 1.0)` — a documented,
explicitly non-physical linear falloff, chosen because it is the
smallest function that is bounded, monotonic, exactly zero at `range`,
exactly one at `dist = 0`, and requires no transcendental function or
division by a value that can legitimately be zero (guarded by
`kPointLightDistanceEpsilon = 1e-4`, a named, documented constant, not a
bare literal). Real inverse-square attenuation is explicitly deferred
(Non-Goals) — has no natural, finite cutoff, in direct tension with this
Spec's own authored, testable `range` field.

**No tone-mapping, no gamma-encode, restated:** the final `clamp(...,
0, 1)` above is the *only* transformation applied before writing to the
target's own `Rgba8Unorm`/`Rgba8Srgb` color attachment — no curve, no
exposure, no highlight rolloff.

### D7. Normal transform — the exact, general, provably-sufficient safety condition; a Runtime-extraction-time check, not a scene-validation-time or silent one

**Decision:** a vertex normal transforms by the object-to-world
matrix's own upper-left 3×3 submatrix directly (`worldNormal = mul(
(float3x3)pushConstants.objectToWorld, input.normal)` in the vertex
shader, matching D6's own stated vertex-shader contract), **not** an
inverse-transpose normal matrix — computed once per vertex, interpolated,
then re-normalized in the fragment shader (D6's own `N = normalize(worldNormal)`).

**The exact, general condition under which this is mathematically
correct, proven, not asserted:** direct transformation by a matrix `M`
(here, the world matrix's own upper-left 3×3) preserves a normal's own
correct direction if and only if `M` is a **conformal (angle-preserving)
linear map** — equivalently, `M`'s own three columns are mutually
orthogonal and equal in length. This condition is satisfied by pure
rotation, by uniform scale of *either* sign (including a full point
reflection), and by any composition of the two; it is violated by
non-uniform scale and by shear. (Proof sketch, for the record: for `M =
s·R` with scalar `s ≠ 0` and orthogonal `R`, the mathematically correct
transform `(M⁻¹)ᵀ = (1/s)·R`, which is a positive-scalar multiple of `M`
itself whenever `s` and `1/s` share a sign — true for every nonzero real
`s` — so normalizing `M·n` and normalizing `(M⁻¹)ᵀ·n` give the identical
direction. For `M` exactly orthogonal, `(M⁻¹)ᵀ = M` exactly. Neither
property holds when `M`'s own columns have unequal length or are not
mutually perpendicular.)

**Checked on the fully composed world matrix, not an entity's own local
`Transform.localScale`:** a parent's own non-uniform scale can introduce
shear into a child's own effective world-space transform even when the
child's own local transform is a pure rotation with uniform local
scale — only the actual, composed `getWorldMatrix()` result can be
checked correctly. The check itself: the three columns of the world
matrix's own upper-left 3×3 have equal length (within a small epsilon)
and are pairwise orthogonal (each pairwise dot product near zero, within
a small epsilon) — a cheap, per-entity, per-frame computation (three
lengths, three dot products), no matrix inverse required.

**Where this check runs, and its own severity, exactly:** this is a
**Runtime-extraction-time**, per-entity, per-frame check — not a scene-
validation-time one, since it depends on the fully composed hierarchy
`World` alone can resolve, and it is evaluated fresh each frame at the
same point Spec 0018's own per-entity `DrawItem`-resolution loop already
runs. An entity bound to a `LitTextured` material whose own current
world matrix fails this check is **skipped for that frame's own
`DrawItem` list**, logged once (not per-frame-spammed), via a new
`SceneExtractionError::NonConformalNormalTransform` (mirroring the
severity of Spec 0018's own present-but-unresolvable-material skip
exactly — recoverable, per-entity, never scene-load-fatal) — **never**
silently rendered with an incorrect normal, and never a scene-load-time
rejection (since, unlike the light-count cap, this condition genuinely
depends on live, composed, potentially-parent-chain-dependent state, not
something knowable from the scene artifact alone).

**Alternative, disclosed, not chosen:** a real inverse-transpose normal
matrix (requiring a new 3×3-inverse function `World`/Core does not have
today). Human Review may prefer this over the detect-and-skip approach
if non-uniform-scale-or-sheared lit entities are expected to be common;
this Spec's own recommendation optimizes for the smallest correct thing
this round's own verification scene needs, with the real limitation
disclosed and non-silent, over implementing more math than is currently
justified.

### D8. Material/shader boundary — exactly one new `MaterialKind`, its exact CMake/descriptor/vertex-input contract, and confirmation of zero material-artifact schema change

**Decision, MaterialKind scope (unchanged from this Spec's own first
draft, restated in full here since this round folds D4's own prior
content into this section):** exactly one new enumerator,
`MaterialKind::LitTextured`, reusing `MaterialAssetData`'s own existing,
unchanged shape (`{kind; textureAsset; filter; addressMode;}`) — a
`LitTextured` material still names exactly one texture/sampler pair,
identically to `UnlitTextured`. **No `LitColored` (untextured lit)
kind this round** — no real consumer names one; adding it now would be
exactly the kind of speculative, no-consumer-yet addition ADR-0059 D2
already rejected once. No `metallic`, `roughness`, normal-map, or
emissive parameter of any kind.

**No material artifact schema-version bump:** the 32-byte record's own
`kind` field is already a plain `u32` whose *decoded, valid* value set
widens from `{0}` to `{0, 1}`; every existing, already-cooked `kind=0`
artifact continues decoding identically, byte for byte;
`decodeMaterialArtifact()`'s own existing `UnknownMaterialKind`
rejection path is unaffected in shape.

**`UnlitTextured` and the colored fallback are byte-unchanged:** neither
shares any code path, shader, or Runtime-side mapping entry with
`LitTextured` — confirmed structurally (a wholly additive enumerator
value, a wholly separate, newly-added shader-pair mapping entry), not
merely asserted.

**Exact CMake/build contract:** a new `shaders/lit_textured/` directory,
`shaders/lit_textured/lit_textured.slang` plus
`shaders/lit_textured/CMakeLists.txt` calling
`atlantis_add_slang_shader_pair(NAME lit_textured ... EXPECTED_CONTRACT
lit-textured)`, mirroring `shaders/textured_quad/`'s own exact shape.
`add_subdirectory(shaders/lit_textured)` is added **unconditionally**
(production, per Spec 0018 D3's own precedent — no `tests/` dependency
in an `ATLANTIS_BUILD_TESTS=OFF` build tree), placed immediately after
`shaders/textured_quad` and before `src/runtime` in the root
`CMakeLists.txt`.

**Exact expected descriptor contract, three entries (not two — this is
the one place D5's own stage-visibility widening becomes directly
observable at the Shader-System/Tools level):**
`litTexturedExpectedDescriptorContract()` (new, `descriptor_contract.h`/
`.cpp`, mirroring `texturedMaterialExpectedDescriptorContract()`'s own
shape) returns `{set 0, binding 0, UniformBuffer, Vertex}`, `{set 0,
binding 0, UniformBuffer, Fragment}` (the *same* binding, reflected
separately from each stage's own separately-loaded reflection JSON,
confirmed achievable per this Spec's own Pre-draft verification of
`ShaderStage`/`DescriptorBinding`'s real shape), and `{set 0, binding 1,
Sampler, Fragment}` (unchanged from the textured contract).

**Exact vertex-input schema shape, deferred only on Spec 0020's own
final byte offsets, never on its existence:** the `lit_textured` vertex
shader's own `VertexInput` needs `position` (existing), a real vertex
`normal` (Spec 0020's own contract — location and byte offset fixed
only once that Spec is `Approved` and merged), and `uv` (existing, for
texture sampling) — three attributes, one more than `UnlitTextured`'s
own two. The exact `MeshVertexAttributeSchema`/`VertexInputLayout`
construction is a direct, mechanical Plan-time closure against Spec
0020's own final field order, not an open design question this Spec
itself leaves unresolved in kind.

**Exact frame-uniform layout:** D5's own `FrameLightingData` struct,
appended after the existing camera block in the same buffer — no
separate contract here beyond D5's own.

### D9. Runtime integration — one-time extraction and capture, its own ownership, Material's non-borrowing of the frame buffer, and non-interaction with format rebuild

**Decision:** light extraction is a new, Runtime-private,
GPU-independent, independently-testable function (mirroring
`extractCameraMatrices()`/`resolveMaterialAsset()`'s own exact
precedent), called **exactly once** — on the first successful frame,
immediately after `World::updateTransforms()` first runs (the same
frame Phase 2 material realization / the format-known check first
succeeds) — never on any subsequent frame. `World` gains zero new
dependency (still `Core` + `AssetSystem` only).

**Ownership:** the captured `FrameLightingData` is written directly into
the tail bytes of the same, already-`RuntimeApplication`-owned camera
`Buffer` — no new GPU resource, no new member, no new destruction-order
concern anywhere in `RuntimeApplication`'s own member layout. A boolean
flag (`lightingDataCaptured_` or an equivalent Plan-time-named member)
guards against ever re-running the capture on a later frame.

**Material's own relationship to the frame buffer — explicitly, not a
new borrow:** a `LitTextured` `Material` does **not** itself hold any
reference to the frame lighting buffer — exactly like `UnlitTextured`
`Material` does not hold a reference to the camera buffer today. The
binding is entirely a function of the `Pipeline`'s own descriptor set
(bound generically, by `Renderer::drawFrame()`'s own existing,
unmodified per-draw binding logic), not something `Material` itself
owns or borrows. This Spec introduces **no new borrow relationship**
anywhere in the `Material`/`SampledTexture`/`Sampler` ownership graph
Spec 0016/0018 already established.

**Format-change rebuild — explicitly unaffected, no new interaction:**
the frame lighting data's own byte layout is entirely
`colorFormat`-independent (matching the camera data it sits beside) —
a color-format change never touches it, never requires it to be
recaptured, and Spec 0018's own submit-safe old-`Pipeline`-destruction-
timing contract (build a candidate batch read-only, swap only after
`submit()` returns `Ok`) is completely unchanged by this Spec, since
`lit_textured` `Pipeline`s participate in that exact same rebuild
mechanism with no special-casing.

**Resize — explicitly unaffected, restated from "Known Limitations"
above:** the existing camera projection matrix's own per-frame
recomputation from the current extent/aspect ratio is entirely
unrelated to, and untouched by, this Spec — this Spec does not attempt
to inspect, fix, or alter that existing, already-shipped behavior.

**No new global light manager:** the captured light data lives entirely
within `RuntimeApplication`'s own existing member layout (the shared
buffer plus one boolean flag) — no singleton, no second, parallel
tracking structure.

### D10. Verification boundary — real Spec 0020 normals, real scene light components, an asymmetric distinguishing scene, CPU math tests, and negative coverage for both new hard-stop conditions

**Decision:** a new, minimal, independent scene authored specifically
for this Spec (not a reuse of `material_demo_scene`/`world_scene`),
consuming **Spec 0020's own final, merged, real, asset-sourced vertex
normal** (this Spec's own fixture may not exist, let alone pass, before
Spec 0020 has merged — this is the direct, concrete consequence of D1's
own governance gate), a real scene-authored `Light` (Directional and
Point, both present), in an **asymmetric layout** — the Point light
positioned close to one distinct part of the scene's own geometry, the
Directional light illuminating the whole scene roughly uniformly from a
fixed, known world direction — so a direction-sign error, a
position error, or an attenuation-formula error each produce a visibly
different, wrong-looking frame, never a coincidentally-still-plausible
one.

**CPU-level math tests, required, not optional (Final Review Round
finding 8):** every formula in D6 (the diffuse `ndotl` term, the
directional sign convention, the point-light vector/epsilon/attenuation/
range formula) gets its own GPU-independent unit test, driven by
hand-computed expected values for known inputs — not merely exercised
incidentally by the golden's own end-to-end pixel comparison. D2's own
direction/position extraction (including the new
`DegenerateLightDirection` case) and D7's own conformal-transform check
(including the new `NonConformalNormalTransform` case) get the identical
treatment.

**Negative tests, required, for both of this round's own new hard-stop
conditions:**

- A scene declaring more than 1 `directional` or more than 4 `point`
  lights fails to cook (`SceneSourceParseError::TooManyLights`) and,
  independently, a hand-corrupted artifact past the cap fails to decode
  (`SceneArtifactDecodeError::TooManyLights`) — both driven by real
  inputs, matching every prior Spec's own discipline.
- A `LitTextured`-bound entity given a deliberately non-conformal
  (non-uniform-scale or sheared) world transform is confirmed skipped
  for that frame's own `DrawItem` list, logged once, never
  scene-load-fatal — a real, executed GPU test, not merely an inspection
  claim.
- **A dedicated test proving the static-snapshot boundary itself
  (Final Review Round finding 2):** calling `World::setLight()` on an
  already-loaded scene's own light entity, *after* the one-time frame
  lighting capture has already run, and confirming the next rendered
  frame's own captured pixels are **unchanged** from before the
  mutation — the direct, executed proof that this Spec's own "no
  runtime light mutation is reflected" limitation is real and enforced,
  not merely claimed in prose.

**GPU-required tests:** the new `LitTextured` material realizes
correctly through Runtime's existing Phase 2 pipeline (reusing, not
duplicating, Spec 0018's own `realizePendingMaterials()`); the widened
uniform binding's own fragment-stage visibility is Validation-Layers-
clean, for both `lit_textured` shaders (which use it) and every
existing shader (which continues not to, unaffected); the new fixture's
own capture-compare test against its own new golden; a real, isolated
proof that removing either light from the scene changes the captured
frame from the golden (mirroring Spec 0018's own D12 negative-proof
precedent), demonstrating each light's own contribution is real, not
coincidental.

**The four existing goldens** (`minimal_cube`, `world_scene`,
`textured_quad`, `material_demo`) confirmed byte-for-byte unchanged
throughout Implementation.

**Runtime's default bootstrap scene — explicit decision, restated:**
does **not** switch — `atlantis_runtime.exe` keeps loading `world_scene`,
matching Spec 0018's own P15 precedent exactly. **Because the bootstrap
scene does not switch, Runtime's own real integration of this Spec's
own new code paths must be proven by a real GPU test calling the same,
shared `Atlantis::RuntimeHost` functions `runFrame()` itself would call
— never a fixture-private reimplementation, and never inferred merely
from the new fixture's own, separately-linked capture-compare test
passing.** This mirrors Spec 0018's own PR #88 final-review finding
exactly (a real, previously-undiscovered gap was found only because no
test had ever exercised `rebuildMaterialsForFormatChange()`/certain
`scene_load.cpp` material-loop paths directly) — this Spec's Plan must
name, explicitly, which real, shared functions each new GPU test calls,
not merely claim coverage.

**Fixture shape:** directly links and calls `Atlantis::RuntimeHost`'s
real Phase 1/Phase 2-equivalent functions (the new light-extraction
function, the one-time uniform-buffer write, the `LitTextured`
material-realization call), following Spec 0018 D12's own precedent
exactly.

### D11. Error domain, module boundaries, threading, ownership, C4062

- **Error domain, complete list, new enumerators only where genuinely
  new (restated with this round's own additions):**
  `SceneSourceParseError::TooManyLights` (D3),
  `SceneArtifactDecodeError::TooManyLights` (D3),
  `WorldError::NoLightComponent` (D2),
  `SceneExtractionError::DegenerateLightDirection` (D2),
  `SceneExtractionError::NonConformalNormalTransform` (D7). Every other
  new failure mode (a malformed `light=` token, an out-of-`[0,1]`-range
  color component, a negative intensity, a `range` on a `directional`
  light) reuses an existing enumerator shaped identically in kind
  (`InvalidComponentGroup`/`MissingField`/`MalformedNumber`, matching
  every existing node-field validation precedent), per this Spec's own
  stated discipline.
- **Module boundaries:** `World` remains `Core` + `AssetSystem`-only,
  verified by the existing include-scanning test, unmodified in
  mechanism. `Atlantis::AssetSystem` remains `Core`-only. Light
  extraction stays Runtime-private, never a public Runtime API.
- **Threading:** unchanged — Phase 1's single-logical-thread baseline
  (ADR-0004) applies identically.
- **Ownership:** the frame lighting data introduces **no new GPU
  resource** (D9) — no new destruction-order concern anywhere.
- **C4062:** every new closed `switch` this Spec's own Implementation
  introduces (`LightKind` translation in the extraction function,
  `MaterialKind::LitTextured` in Runtime's shader-pair mapping) gets its
  own `/w14062` positive-and-negative build probe.

### D12. Boundary with future lighting/material work

**Decision, unchanged, with one addition (Final Review Round finding
2):** Shadow Foundation, PBR Material, IBL, and Post-processing all
remain independent, unblocked future Specs. **Dynamic Frame Uniform
Updates** — a real per-frame (or event-driven) update mechanism for the
frame lighting buffer, resolving this Spec's own static-snapshot
limitation, including the full Material-borrow-and-prior-frame-GPU-
lifetime analysis such a mechanism would require — is named here as its
own, real, independent, unblocked future candidate, not solved or
prewired for by this Spec.

## Architectural Impact

This Spec introduces real architectural decisions, filed as two
`Accepted` ADRs (both `Proposed` → `Accepted` in this round's own Human
Review Approval), mirroring Spec 0018's own module-boundary/artifact-
format-and-RHI-boundary split:

- [ADR-0061](../adr/0061-world-light-component-and-scene-lighting-binding-boundary.md)
  — `World`'s own `Light` component shape and boundary (D2), the Scene
  Asset format's light-node extension and versioning/cap contract (D3),
  and Material's new `LitTextured` kind (D8).
- [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
  — the frame lighting data's own layout, fixed maximum counts, and
  one-time-capture ownership contract (D5, D9), the one real RHI
  decision this Spec requires (the uniform-binding Vulkan
  stage-visibility widening, D5), Point-light attenuation and the full
  lighting math (D6), normal transform (D7).

**Neither ADR decides anything about Spec 0020's own eventual
mesh-normal schema, stride, byte offset, version, or migration** —
confirmed by re-reading both in full during this round; that remains
exclusively Spec 0020's own, future ADR/ADR-Amendment territory. Neither
ADR proposes a change to any already-`Accepted` ADR's own Decision text.

## Alternatives Considered

- **Bundle the normal attribute into this Spec (D1).** Rejected — see
  D1's own full analysis; reconfirmed, with a stricter governance gate,
  by this round's own final review.
- **A second uniform buffer binding for lighting data, instead of
  widening the existing one (D5).** Rejected as the default — a real,
  larger RHI change, when reusing the existing single binding is
  structurally sufficient.
- **Two Light components instead of one tagged `Light` (D2).**
  Rejected — matches `MaterialKind`'s own precedent over `Camera`+
  `Renderable`'s own "genuinely independent components" precedent.
- **A same-node, combinable light+mesh/camera grammar (D3).** Rejected
  — the existing parent/child hierarchy already achieves the one real
  use case with zero grammar change.
- **Deterministic truncation of over-limit lights, instead of a hard
  cook/decode-time error (D3, Final Review Round finding 4).** Rejected
  this round, reversing this Spec's own first-draft recommendation — a
  real risk of silently masking a genuine scene-authoring mistake,
  disclosed and reasoned through above; a hard, structural error is the
  smaller, more honest failure mode.
- **Per-frame re-derivation of the frame lighting data, instead of a
  one-time static snapshot (D9, Final Review Round finding 2).**
  Rejected this round, reversing this Spec's own first-draft design —
  not because it is unsafe (RHI already supports it trivially) but
  because this Spec's own first draft never honestly committed to
  whether runtime light mutation would or would not be reflected, and a
  static, one-time, explicitly-tested boundary is the smaller, more
  honest commitment for this round's own minimal scope. Dynamic updates
  remain a real, named future candidate (D12).
- **Physically-based inverse-square Point-light attenuation (D6).**
  Rejected for this round — no natural, finite cutoff.
- **A real inverse-transpose normal matrix (D7).** Rejected as this
  round's own default — introduces new math capability this codebase
  does not yet have, for a correctness case this round's own
  verification scene does not require.

## Testing & Verification Plan

See D10 for the complete, itemized record (CPU math tests per D6/D2/D7
formula; negative tests for the light-count cap, the non-conformal-
transform skip, and the static-snapshot boundary itself; GPU-required
realization/Validation-Layers/golden/negative-light-removal tests; the
four existing goldens confirmed byte-for-byte unchanged;
`/w14062` C4062 probes; manual, human-performed Runtime windowed
verification, Debug and Release, scoped identically to every prior
Spec's own D13-shaped requirement).

## Risks & Open Questions

- **D1's own prerequisite remains a real, disclosed scheduling risk:**
  if Spec 0020 is not drafted, does not reach `Approved`, or its own
  Implementation does not merge, this Spec's own Plan 0019 cannot begin
  at all — this Spec's own approval does not change that; it only
  authorizes drafting Spec 0020 next.
- **The exact std140 byte offsets for `FrameLightingData` (D5)** are a
  real, disclosed Plan-time closure against the exact field list already
  fixed here — a mechanical detail, not an open architectural question.
- **The exact file location for the new Runtime-private extraction
  function** (a new `light_extraction.h`/`.cpp`, or an addition to the
  existing `scene_extraction.h`/`.cpp`) remains a Plan-time
  file-organization choice, not an architectural one.

## Out of Scope / Future Work

Repeating this Spec's own Non-Goals for visibility, plus the named
successor candidates this Spec's own closure unblocks:

- PBR/metallic-roughness material model — **"PBR Material,"** a real,
  independent future Spec.
- Shadow mapping of any kind — **"Shadow Foundation."**
- Image-based lighting / environment maps — **"IBL Foundation."**
- Tone mapping, gamma-encode, HDR intermediate targets, or any other
  post-processing — **"Post-processing Foundation."**
- A real, per-frame (or event-driven) update mechanism for the frame
  lighting buffer, resolving this Spec's own static-snapshot limitation
  — **"Dynamic Frame Uniform Updates."**
- Normal mapping / a tangent vertex attribute — a distinct, later
  extension beyond even Spec 0020's own scope.
- A material graph, shader graph, hot-reload, editor, runtime asset
  mutation, a distributable Asset Catalog, or Android/iOS/Linux
  implementation.

None of the above is designed, scaffolded, or interface-reserved for by
this Spec, per AGENTS.md's own "no speculative abstraction" principle.
