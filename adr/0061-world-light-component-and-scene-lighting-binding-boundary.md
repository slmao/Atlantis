# ADR 0061: World Light Component, Scene Lighting Binding, and Material's LitTextured Kind

- **Status:** Accepted
- **Date:** 2026-08-29
- **Deciders:** slmao
- **Related Spec:** [specs/0019-lighting-foundation.md](../specs/0019-lighting-foundation.md)

## Context

Spec 0019 adds Atlantis's first real lighting capability. Three real
module-boundary/format decisions fall out of that: what shape `World`'s
new third optional component (`Light`) takes, including its own exact
semantics under parent transforms, entity destruction, and error
handling; how the Scene Asset authoring/artifact format expresses a
light node, including a real, structural cap on how many lights of each
kind a scene may declare; and how Material's existing, closed
`MaterialKind` enum grows to include a lit variant.

Confirmed directly against real, current source (Spec 0019's own
Pre-draft verification and this ADR's own final review round, not
repeated here in full): `World` has exactly two optional per-entity
components today, `Camera` and `Renderable`; `Camera` stores no
direction of its own, deriving it fresh from the owning entity's own
world matrix every frame (`-column2`, normalized, for forward;
`column3` for eye); `World::validate()`'s own real, current code checks
`WrongWorld` before `InvalidEntity`, and every component accessor
(`getCamera()`) checks component presence only after `validate()`
succeeds; `WorldError`'s own real enumerator list is
`{InvalidEntity, WouldCreateCycle, NoCameraComponent, WrongWorld,
NoRenderableComponent}`; the scene authoring grammar's per-node
trailing token group is one of exactly three mutually exclusive shapes
today, dispatched by a hard `tokens.size()` check; `MaterialKind` is a
closed, one-enumerator enum with an already-`Accepted`, disclosed growth
path (ADR-0059) for exactly the situation this ADR now exercises.

This ADR's own scope is deliberately narrow: it decides `World`'s own
`Light` component, the Scene Asset format's own light-node/cap contract,
and Material's `LitTextured` kind. It decides **nothing** about a real
mesh vertex normal's own authoring format, DTO shape, artifact byte
layout, schema version, or migration — that remains exclusively the
scope of a separate, prerequisite Spec 0020 ("Mesh Normal Attribute
Foundation," not yet drafted) and its own future ADR/ADR-Amendment.

## Decision

1. **`World` gains a third optional per-entity component, `Light`** — a
   flat struct with a closed `LightKind` tag (`Directional`, `Point`),
   never a `std::variant`, never two separate components, **at most one
   per entity**:

   ```cpp
   enum class LightKind { Directional, Point };
   struct Light {
     LightKind kind = LightKind::Directional;
     Vec3 color{1.0f, 1.0f, 1.0f};  // each component in [0, 1]
     float intensity = 1.0f;          // finite, >= 0
     float range = 0.0f;              // Point only
   };
   ```

   It stores no direction or position of its own — both are re-derived,
   on the one occasion Runtime ever reads them (ADR-0062's own
   one-time-capture contract), from the owning entity's own current
   world matrix: `normalize(-column2)` for a `Directional` light's own
   direction, `column3` (translation) for a `Point` light's own
   position — the identical formula and sign convention `Camera`'s own
   forward/eye extraction already uses. Extracting a single matrix
   column never requires the matrix's other columns to be orthogonal or
   uniformly scaled, so this extraction is unrestricted under parent
   composition, negative scale, non-uniform scale, or shear — the
   identical robustness argument `scene_extraction.cpp`'s own existing
   comment already states for Camera. A degenerate `Directional`
   transform (near-zero-length `-column2`) is rejected with a new
   `SceneExtractionError::DegenerateLightDirection`, mirroring
   `DegenerateCameraForward` exactly — never silently normalized.

   `World` gains `setLight()`/`removeLight()`/`getLight()` (returning by
   value, matching every existing accessor) and `lightEntities()`, a
   deterministic accessor mirroring `renderableEntities()`'s own exact
   shape (ascending slot-index order, a fresh snapshot per call). A new
   `WorldError::NoLightComponent` mirrors `NoCameraComponent`/
   `NoRenderableComponent`'s own naming exactly, checked with the
   identical precedence every existing accessor already uses:
   `WrongWorld` first, `InvalidEntity` (stale generation/out-of-range/
   dead slot) second, component-presence third. A `Light` component is
   destroyed automatically on `destroyEntity()`'s own existing cascading
   mechanism, with no new logic. `World`'s own dependency closure
   (`Atlantis::Core` + `Atlantis::AssetSystem`) is unchanged.

2. **The Scene Asset authoring grammar and artifact format gain a
   fourth, mutually-exclusive per-node trailing group, `light=`** —
   version bumped (3), version 2 rejected outright, no dual-version
   reader. A light node is standalone this round: it cannot share a
   trailing group with a `mesh=`/`camera_*=` group on the same node.
   Composing a visible light fixture is achieved through the existing
   parent/child hierarchy, not a same-node grammar extension.

   **A real, structural cap on declared light count, enforced
   independently at cook time and decode time:** a scene declaring more
   than 1 `directional`-kind light node, or more than 4 `point`-kind
   light nodes (the same fixed maximum ADR-0062's own `FrameLightingData`
   contract fixes), fails to cook outright (`SceneSourceParseError::TooManyLights`)
   and, independently, fails to decode if hand-corrupted past the cap
   (`SceneArtifactDecodeError::TooManyLights`) — never a silent,
   deterministic truncation. A scene exceeding the cap is treated as a
   real content error, not a degraded-but-successful load, matching
   Spec 0018 D4's own "a genuinely broken/exceeded reference stops the
   scene from reaching `Running`" reasoning exactly. `color`'s three
   components must each be finite and in `[0.0, 1.0]`; `intensity` must
   be finite and `>= 0.0`; `range` must be finite and strictly `> 0.0`
   for `Point`, and is rejected outright if present on a `directional`
   light line — every check independent at both cook time and decode
   time, never trusting the cooker.

3. **Material gains exactly one new enumerator,
   `MaterialKind::LitTextured`** — reusing `MaterialAssetData`'s own
   existing, unchanged shape (`{kind; textureAsset; filter;
   addressMode;}`), requiring no new DTO field and no material artifact
   schema-version bump, since the 32-byte record's own `kind` field's
   *valid value set* simply widens from `{0}` to `{0, 1}`; every
   existing `kind=0` artifact continues decoding byte-for-byte
   identically. No second, untextured `LitColored` kind is added this
   round — no real consumer names one yet.

## Consequences

### Positive

- Every new piece follows an exact, already-`Accepted` precedent in
  this exact codebase (`Camera`'s own direction-derivation shape and
  its own real, cited error-precedence order; `MaterialKind`'s own
  disclosed one-enumerator-at-a-time growth path; the Scene Asset
  format's own version-bump-and-reject discipline) — no new kind of
  mechanism is introduced anywhere.
- The hard, structural light-count cap closes a real, disclosed risk
  (a scene silently rendering with fewer lights than authored) at the
  earliest possible point — cook time — rather than deferring it to a
  runtime condition Implementation might otherwise be tempted to
  degrade quietly.
- Zero material artifact schema change keeps this the smallest possible
  Material-side extension; zero `World` dependency-closure change keeps
  `World`'s own module boundary exactly as narrow as it already is.

### Negative / Trade-offs

- A scene author cannot express "this one node is both a visible mesh
  and a light source" directly — two nodes and a parent/child
  relationship are required, an accepted, explicit scope limit.
- The fixed light-count cap (1 Directional, 4 Point — ADR-0062's own
  exact contract) is a real ceiling; a scene genuinely needing more
  lights of a given kind must wait for a future Spec revisiting this
  cap, not silently exceed it.
- `LightKind` being a closed, non-extensible-without-a-new-enumerator
  tag means a future light type (spot, area, etc.) is itself a new,
  disclosed extension — a deliberate choice under AGENTS.md's "no
  speculative abstraction" principle, not an oversight.

## Alternatives Considered

- **Two separate components, `DirectionalLight`/`PointLight`.**
  Rejected: doubles `World`'s own component-slot count for a
  mutually-exclusive property a single closed-enum tag already
  expresses correctly.
- **`std::variant<DirectionalLightData, PointLightData>` instead of a
  flat, tagged struct.** Rejected: would be the first use of
  `std::variant` for this shape anywhere in `World` or Asset System;
  every existing closed-kind DTO uses a plain enum plus flat fields
  instead.
- **A same-node, combinable light+mesh/camera grammar.** Rejected: the
  real, confirmed shape of today's parser has no mechanism for two
  independent trailing groups on one line; the existing parent/child
  hierarchy already solves the one real use case with zero grammar
  change.
- **Deterministic truncation of over-limit lights, instead of a hard
  cook/decode-time error.** Considered and rejected during this ADR's
  own final review round: truncation (even in a fully deterministic
  order) risks silently masking a genuine scene-authoring mistake with
  no signal beyond a log line; a hard, structural error is the smaller,
  more honest failure mode, matching this codebase's own established
  "a genuinely broken/exceeded reference is fatal, not degraded"
  precedent.
- **A second, untextured `LitColored` MaterialKind alongside
  `LitTextured`.** Rejected: no real consumer this round names one;
  matches `MaterialKind`'s own already-disclosed growth path instead.
