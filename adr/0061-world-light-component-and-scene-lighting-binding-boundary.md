# ADR 0061: World Light Component, Scene Lighting Binding, and Material's LitTextured Kind

- **Status:** Proposed
- **Date:** 2026-08-29
- **Deciders:** slmao
- **Related Spec:** [specs/0019-lighting-foundation.md](../specs/0019-lighting-foundation.md)

## Context

Spec 0019 adds Atlantis's first real lighting capability. Three real
module-boundary/format decisions fall out of that: what shape `World`'s
new third optional component (`Light`) takes; how the Scene Asset
authoring/artifact format expresses a light node; and how Material's
existing, closed `MaterialKind` enum grows to include a lit variant.

Confirmed directly against real, current source (Spec 0019's own
Pre-draft verification, not repeated here in full): `World` has exactly
two optional per-entity components today, `Camera` and `Renderable`,
each a flat struct with its own `set/remove/get` triplet; `Camera`
stores no direction of its own, deriving it fresh from the owning
entity's own world matrix every frame; the scene authoring grammar's
per-node trailing token group is one of exactly three mutually
exclusive shapes today (mesh-only, mesh+material, camera), dispatched
by a hard `tokens.size()` check with no mechanism for a node to carry
two trailing groups at once; `MaterialKind` is a closed, one-enumerator
enum with an already-`Accepted`, disclosed growth path (ADR-0059) for
exactly the situation this ADR now exercises.

## Decision

1. **`World` gains a third optional per-entity component, `Light`** — a
   flat struct with a closed `LightKind` tag (`Directional`, `Point`),
   never a `std::variant`, never two separate components. It stores no
   direction or position of its own — both are re-derived every frame
   from the owning entity's own current world matrix, in Runtime's own
   extraction step, following `Camera`'s own exact precedent. `World`
   gains `setLight()`/`removeLight()`/`getLight()` and a
   `lightEntities()` deterministic accessor mirroring
   `renderableEntities()`'s own exact shape (ascending slot-index
   order, a fresh snapshot per call). `World`'s own dependency closure
   (`Atlantis::Core` + `Atlantis::AssetSystem`) is unchanged.

2. **The Scene Asset authoring grammar and artifact format gain a
   fourth, mutually-exclusive per-node trailing group, `light=`** —
   version bumped (3), version 2 rejected outright, no dual-version
   reader. A light node is standalone this round: it cannot share a
   trailing group with a `mesh=`/`camera_*=` group on the same node,
   matching the real, confirmed structural shape of today's parser.
   Composing a visible light fixture (a lit mesh at the same location
   as its own light) is achieved through the existing parent/child
   hierarchy, not a same-node grammar extension. Cook-time and
   decode-time validation (finite, non-negative `color`/`intensity`;
   finite, strictly-positive `range` required for `Point` and rejected
   outright for `Directional`) is independent at each stage, never
   trusting the cooker, matching every existing Scene Asset validation
   precedent.

3. **Material gains exactly one new enumerator, `MaterialKind::LitTextured`**
   — reusing `MaterialAssetData`'s own existing, unchanged shape
   (`{kind; textureAsset; filter; addressMode;}`), requiring no new DTO
   field and no material artifact schema-version bump, since the
   32-byte record's own `kind` field's *valid value set* simply widens
   from `{0}` to `{0, 1}`; every existing `kind=0` artifact continues
   decoding byte-for-byte identically. No second, untextured `LitColored`
   kind is added this round — no real consumer names one yet.

## Consequences

### Positive

- Every new piece follows an exact, already-`Accepted` precedent in
  this exact codebase (`Camera`'s own direction-derivation shape,
  `MaterialKind`'s own disclosed one-enumerator-at-a-time growth path,
  the Scene Asset format's own version-bump-and-reject discipline) — no
  new kind of mechanism is introduced anywhere.
- Zero material artifact schema change keeps this the smallest possible
  Material-side extension; zero World dependency-closure change keeps
  `World`'s own module boundary exactly as narrow as it already is.
- The light-cannot-share-a-node-with-mesh/camera constraint is a real,
  disclosed limitation with a real, already-available workaround
  (parent/child composition), not a silent gap.

### Negative / Trade-offs

- A scene author cannot express "this one node is both a visible mesh
  and a light source" directly — two nodes and a parent/child
  relationship are required. This is accepted as this round's own
  explicit scope limit (Spec 0019 D3), not solved here.
- `LightKind` being a closed, non-extensible-without-a-new-enumerator
  tag (rather than a more general light-description mechanism) means a
  future light type (spot, area, etc.) is itself a new, disclosed
  extension, not something this shape anticipates or reserves room for
  — a deliberate choice under AGENTS.md's "no speculative abstraction"
  principle, not an oversight.

## Alternatives Considered

- **Two separate components, `DirectionalLight`/`PointLight`.**
  Rejected: doubles `World`'s own component-slot count for a
  mutually-exclusive property a single closed-enum tag already
  expresses correctly, and diverges from `MaterialKind`'s own directly
  analogous, already-`Accepted` precedent for "one of a small, fixed,
  closed set."
  
- **`std::variant<DirectionalLightData, PointLightData>` instead of a
  flat, tagged struct.** Rejected: would be the first use of
  `std::variant` for this shape anywhere in `World` or Asset System;
  every existing closed-kind DTO (`MaterialAssetData`, `TextureAssetData`)
  uses a plain enum plus flat fields instead, with fields irrelevant to
  the current tag simply unread — matching that established pattern
  costs nothing and introduces no new pattern.

- **A same-node, combinable light+mesh/camera grammar (a fourth manifest-
  style column, or a general multi-group, self-describing token
  parser).** Rejected: the real, confirmed shape of today's parser has
  no mechanism for two independent trailing groups on one line;
  building one would replace the grammar's entire dispatch mechanism
  for a use case (a lit, visible fixture) the existing parent/child
  hierarchy already solves with zero grammar change.

- **A second, untextured `LitColored` MaterialKind alongside
  `LitTextured`.** Rejected: no real consumer this round names one;
  adding it now would be exactly the kind of speculative, no-consumer-
  yet addition ADR-0059 D2 already rejected once for a color-tint
  field. A small, additive follow-up if a real need appears later,
  matching `MaterialKind`'s own already-disclosed growth path.
