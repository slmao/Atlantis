# ADR 0054: Scene Loading Transactional Instantiation Contract

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)

## Context

[ADR-0052](0052-scene-asset-module-boundary-and-ownership.md) put
`DecodedScene → real World` instantiation on `Atlantis::World` itself.
[ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)
guarantees a `DecodedScene` that reached this step is *structurally*
consistent (every parent/active-camera index in range, no cycle, no
duplicate) — the loader already rejected anything short of that. What
remains for instantiation itself to define: what "all-or-nothing" means
concretely, in what order nodes come to exist, where a `Renderable`'s
`AssetId` gets checked against assets Runtime actually has loaded (a
fact `Atlantis::World` and `Atlantis::AssetSystem` cannot know — only a
composition root does), and what happens when the artifact's own
active-camera node was legitimately decoded with no `Camera` (a
semantic condition, not a structural one, so ADR-0053's own decode-time
checks do not — and should not — catch it).

## Decision

**1. Asset resolution is a Runtime-side pre-check, not part of `World`'s
own instantiation call.** Before calling `World`'s new
`DecodedScene`-consuming entry point at all, Runtime walks the decoded
node array and confirms every `Renderable`'s `AssetId` matches an asset
Runtime has actually loaded — extending
`resolveMeshAsset()`'s own existing per-`AssetId` check
(`src/runtime/include/atlantis/runtime/scene_extraction.h`) to a
whole-scene pass. **If any reference is unresolved, Runtime fails the
load before a temporary `World` is ever constructed** — classified the
same way Runtime already classifies every other startup failure
(`RuntimeInitError`, matching `AssetLoadFailed`/`SceneConstructionFailed`'s
own existing precedent in `initializeSteps()`). `Atlantis::World` and
`Atlantis::AssetSystem` never need to know which assets Runtime has
actually loaded — that knowledge stays exactly where it already lives.

**2. `World`'s own new entry point builds directly into a fresh
`World` instance and returns `atlantis::Result<World, WorldError>` —
no new error enum.** Every one of its own internal calls
(`createEntity()`, `setLocalTransform()`, `setCamera()`,
`setRenderable()`, `setParent()`) operates against data ADR-0053's own
decode-time validation already guaranteed structurally sound for a
brand-new, single-owner `World` — no `WrongWorld` (nothing else holds a
handle into this `World` yet), no `InvalidEntity` (every reference is
an already-range-checked array index, not a caller-supplied handle), no
`WouldCreateCycle` (the decoder already proved the graph acyclic). Each
of those is therefore an `ATLANTIS_CHECK_MSG`-guarded "should never
happen in correct operation" call, matching
`RuntimeApplication::runFrame()`'s own existing convention for
identically-reasoned invariants (`getWorldMatrix()`/`getRenderable()`
against a handle just returned by `renderableEntities()`). **Exactly
one case remains genuinely reachable and is propagated as a real
`Result::Err`, not asserted away: the active-camera node was decoded
with no `Camera` component.** This is a semantic, not structural,
condition — reusing `World::setActiveCamera()`'s own already-`Accepted`,
already-tested `Err(WorldError::NoCameraComponent)` path
([Plan 0014](../plans/0014-world-scene-foundation.md)'s own Human
Review Correction) is the sole enforcement point; ADR-0053's own
cook-time validation deliberately does not duplicate this check.
Reusing `WorldError` (rather than a new, scene-loading-specific error
type) means this genuinely new failure surface is exactly as large as
it needs to be — one enumerator, already `Accepted`, already exercised
by every existing `WorldError`-consuming call site's own exhaustiveness
discipline (Plan 0014's own V28).

**3. Nodes are instantiated in two passes, in the `DecodedScene`'s own
array order — never node-then-immediately-parent in one pass.** Pass
one: `createEntity()` + `setLocalTransform()` + (if present) `setCamera()`
+ (if present) `setRenderable()`, for every node in ascending array-index
order, recording a `decoded index → EntityId` mapping as it goes. Pass
two: `setParent()` for every node that has one, using the mapping from
pass one — a parent's own `EntityId` is guaranteed to already exist,
regardless of whether it was declared before or after its child in the
authoring source, because pass one already created every node before
pass two links any of them. This exactly mirrors
`buildValidationScene()`'s own existing shape in
`src/runtime/src/runtime_application.cpp` (every cube entity created
first via its own `makeCubeEntity()` closure; `world.setParent(*d, *c)`
called only afterward) — not a new pattern, a generalization of the one
already shipped. Instantiation order is deterministic by construction:
the same `DecodedScene` always produces the same sequence of
`createEntity()` calls against a freshly-constructed `World`, and
`World`'s own already-`Accepted` slot-assignment rule
([ADR-0049](0049-entity-identity-and-handle-invalidation.md)) makes the
resulting `index()`/`generation()` values on each entity deterministic
in turn.

**4. All-or-nothing means: instantiation happens entirely inside one
freshly-constructed `World` value that no caller has published
anywhere yet.** `atlantis::Result<World, WorldError>::Err(...)` returns
before that `World` value is ever handed anywhere — its own destructor
(already `Accepted`, already move-only, RAII-based) tears down whatever
partial entity/component state existed inside it, exactly as
destroying any other `World` instance already does. **No explicit
rollback code is required or written** — this is a property of `World`
already being a plain, owning, RAII value type, not a new mechanism
this ADR invents. On success, Runtime replaces its own (for this Spec's
scope: not-yet-existing) `World` member with the returned value —
matching `RuntimeApplication`'s own existing "construct once during
`initializeSteps()`, never touched again by `runFrame()`" ownership
shape (Plan 0014 Section D9), unchanged. **This Spec introduces no
runtime scene replacement, hot-reload, or streaming capability** — the
transactional contract is stated generally enough to remain correct if
a future Spec adds one, but this Spec's own scope only ever exercises
it once, at Runtime startup.

**5. Camera aspect ratio is unaffected — still computed per-frame from
the live swapchain extent**, exactly as
`extractCameraMatrices()`/`runFrame()` already do
([ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)).
Nothing about scene loading authors, cooks, or instantiates an aspect
ratio; only `fovYRadians`/`nearZ`/`farZ` come from the scene's own
`Camera` component, unchanged from Spec 0014's own existing shape.

## Consequences

### Positive

- Reusing `WorldError::NoCameraComponent` rather than inventing a new
  error type keeps `World`'s own public error surface exactly as large
  as its genuinely reachable failure modes — no speculative
  enumerators for conditions ADR-0053's own decode-time validation
  already forecloses.
- The two-pass instantiation order is not a new algorithm invented for
  this Spec — it is the existing `buildValidationScene()` shape,
  generalized from six hand-written calls to a loop over a decoded
  array, which is itself strong evidence the shape is sufficient (it
  already produces exactly the scene this Spec's own first scene asset
  reuses byte-for-byte).
- Asset resolution living entirely on the Runtime side means
  `Atlantis::World`'s own new entry point has zero dependency on "what
  Runtime happens to have loaded" — it takes a `DecodedScene`, returns
  a `Result<World, WorldError>`, and nothing else.
- "No explicit rollback code" is not an aspiration — it falls directly
  out of `World` already being RAII/move-only from Spec 0014's own
  Decision; this ADR adds no new ownership mechanism.

### Negative / Trade-offs

- Splitting asset-resolution validation (Runtime) from structural
  validation (`AssetSystem`'s own decode) from component-semantic
  validation (`World`'s own instantiation) means a single "why did my
  scene fail to load" question can have its answer in one of three
  different places, depending on what kind of mistake it was — accepted
  because each place is already the sole authority on its own kind of
  fact (Runtime: what's loaded; `AssetSystem`: is the artifact
  well-formed; `World`: is the component data internally coherent), and
  conflating them would mean one of those modules learning a fact it
  has no other reason to know.
- The two-pass instantiation loop touches every node twice — a real,
  small, `O(2n)` cost, `n` this Spec's own scene sizes (single digits),
  accepted without further justification given the scale.

## Alternatives Considered

- **A new, scene-loading-specific error enum on `World`'s own new entry
  point**, instead of reusing `WorldError`. Rejected: every reachable
  failure this entry point can genuinely produce is already
  `WorldError::NoCameraComponent`; inventing a parallel enum with one
  member that means the same thing adds a translation step for no
  benefit.
- **Validate `Renderable` `AssetId` resolution inside `World`'s own
  instantiation call**, via a resolver callback/set parameter Runtime
  supplies. Rejected: would give `Atlantis::World` a parameter shaped
  entirely around a Runtime-side concern (which assets are loaded),
  without `World` gaining any capability it could use that knowledge
  for beyond immediately failing — cheaper and cleaner as a Runtime-side
  pre-check that never constructs a `World` at all when it would fail.
- **Single-pass instantiation, deferring only forward-referenced
  parents.** Rejected: more complex than "create everything, then link
  everything," for no correctness or performance benefit at this
  Spec's own scale, and it would diverge from
  `buildValidationScene()`'s own already-shipped, already-correct
  two-pass shape for no reason.
- **Publish a partially-instantiated `World` and let the caller decide
  whether to use it.** Rejected outright — directly contradicts this
  Spec's own explicit requirement (a failed load must leave no partial
  state observable to anything outside the failed call), and would
  reintroduce exactly the "silent partial state" hazard
  [ADR-0049](0049-entity-identity-and-handle-invalidation.md)'s own
  atomicity guarantees were written to close for every other `World`
  mutation.
