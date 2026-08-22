# ADR 0050: Transform Hierarchy, Composition, and Update Model

- **Status:** Proposed
- **Date:** 2026-08-22
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)

## Context

[specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)'s
own minimum capability set requires local **and** world Transform, a
parent/child hierarchy, and multi-entity traversal. None of this is free:
computing a child's world transform requires composing its own local
transform with its parent's world transform, which requires actual matrix
(or equivalent) math World does not yet have — confirmed by inspection
(see [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)'s own
Context) that Atlantis Core has no public math type at all today, and
every existing composition root hand-rolls its own private `Mat4`
helpers, never shared. A hierarchy also introduces two hazards a flat
entity list does not have: **cycles** (an entity becoming its own
ancestor, directly or transitively, which would make a naive top-down
traversal loop forever) and **parent destruction** (what happens to a
subtree when the entity at its root is destroyed).

`atlantis::renderer::DrawItem::objectToWorld`
(`src/renderer/include/atlantis/renderer/draw_item.h`) already fixes the
*output* shape this ADR's own composition math must ultimately produce: a
raw, column-major `std::array<float, 16>`, with no Core math type
involved on Renderer's side. This ADR is about what World stores and how
it gets from stored local transforms to that raw array, not about
changing what Renderer accepts.

## Decision

**World stores a minimal, hand-rolled position/rotation/scale
representation per entity, composes it into a 4×4 matrix using
hand-rolled matrix math scoped to the `atlantis::world` namespace (per
[ADR-0048](0048-world-scene-module-boundary-and-ownership.md)), and
recomputes every entity's world matrix via one explicit, caller-invoked,
single-threaded traversal per update — not incrementally, and not
automatically on every mutation.**

### Transform representation

```cpp
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct Transform {
  Vec3 localPosition{};
  Vec3 localEulerAnglesRadians{};   // pitch, yaw, roll — see rotation choice below
  Vec3 localScale{1.0f, 1.0f, 1.0f};
};
```

- **Rotation is stored as Euler angles (radians), not a quaternion.**
  World never needs to *decompose* a composed world matrix back into a
  single rotation value (each entity always stores and edits its own
  **local** rotation directly; only the resulting matrices are composed,
  never the angles themselves) — the one property that would make Euler
  angles unsafe (ambiguous composition of rotation *values* across a
  hierarchy) never arises here, since composition happens entirely at the
  matrix level. This avoids introducing quaternion normalization,
  multiplication, and (unused, since this Spec excludes animation)
  interpolation math for a consumer this Spec does not have. A future
  Spec introducing animation/interpolation, where gimbal lock and
  rotation-value blending genuinely matter, may reconsider — not decided
  here as a permanent commitment beyond this Spec's own scope.
- **Local-to-world composition is the conventional
  `worldMatrix = parentWorldMatrix × T × R × S`** (translation, then
  rotation — pitch, then yaw, then roll, exact axis-multiplication order
  a Plan-stage detail — then scale, matching every existing composition
  root's own already-verified `Mat4` convention in
  `examples/minimal_renderer_demo/main.cpp`). A root entity (no parent)
  uses an implicit identity parent matrix, so its world matrix equals its
  own local matrix.
- **World's own `Vec3`/matrix-multiply helpers are private implementation
  detail exposed only insofar as `Transform`'s own fields are public** —
  no general-purpose `add`/`normalize`/`cross`/`dot` vector API surface is
  designed or exposed beyond what composing a TRS matrix requires. This is
  not a math library; it is the minimum arithmetic one module's own
  Transform hierarchy needs.

### Hierarchy and cycle prevention

- **Parent/child links are stored per-entity** (each entity holds its own
  parent `EntityId`, defaulting to the invalid sentinel for a root
  entity) — not a separate, independent graph structure. `setParent(child,
  newParent)` is the only mutator.
- **Cycle prevention happens at `setParent()` time, not at traversal
  time.** Before mutating any state, `setParent()` walks `newParent`'s own
  ancestor chain up to its root; if `child` appears in that chain (or
  `newParent == child`, the degenerate one-entity cycle), the call returns
  `Result::Err(WorldError::WouldCreateCycle)` and changes nothing. A
  stale `child`/`newParent` handle returns `WorldError::InvalidEntity`
  instead, checked first. This makes an invalid hierarchy structurally
  unreachable through World's own public API, rather than something the
  update pass must detect and recover from.
- **The update traversal additionally asserts (`ATLANTIS_CHECK`) that it
  never revisits an already-visited entity in the same pass**, as a
  defense-in-depth invariant check, per
  [AGENTS.md](../AGENTS.md)'s "violated precondition/invariant fails
  fast" rule — not a second, silently-recovering cycle-handling path. If
  this assertion ever fires, it means `setParent()`'s own prevention has a
  bug, not that a legitimate cycle needs graceful handling.

### Parent destruction

- **`destroyEntity(id)` cascades: `id` and every transitive descendant are
  destroyed together, in one call.** No orphaned-but-still-alive subtree
  state exists after this call returns — this is the simplest semantics
  to reason about for a minimal scene model, and matches this Spec's own
  "do not build for hypothetical future" instruction better than
  reparenting orphans to the destroyed entity's own parent (or to a root)
  would, since no consumer of this Spec needs subtrees to outlive their
  root.
  If the destroyed entity or any destroyed descendant was the current
  active camera (see
  [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)),
  the active-camera reference is cleared automatically as part of the same
  call — never left pointing at a freed slot.

### Update model

- **World performs no automatic, eager recomputation on every
  `setLocalTransform()`/`setParent()` call.** World exposes one explicit
  method, `updateTransforms()`, that recomputes every entity's world
  matrix in a single traversal, visiting every entity strictly after its
  own parent (guaranteeing each entity's parent world matrix is already
  current when that entity's own composition runs). A world matrix
  returned by `getWorldMatrix()` reflects the state as of the most recent
  `updateTransforms()` call, not necessarily the entity's current local
  transform if it was mutated afterward without a following
  `updateTransforms()` call — this is a documented, explicit contract, not
  an implicit assumption.
- **Runtime calls `updateTransforms()` exactly once per frame**, before
  extraction (see
  [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)),
  matching this codebase's existing single-logical-frame-thread baseline
  ([ADR-0004](0004-phase1-threading-baseline.md)) exactly — no
  concurrent mutation during traversal is possible or supported.
- **No dirty-flag/incremental update optimization.** A full O(N) traversal
  every call is the entire update strategy — at this Spec's own validation
  scale (a handful of entities), a dirty-propagation scheme would be
  speculative optimization with no measured need, exactly what
  [AGENTS.md](../AGENTS.md) warns against. A future Spec may introduce one
  if entity counts genuinely make O(N) traversal a real cost — not
  designed or scaffolded here.

## Consequences

### Positive

- A concrete, minimal, dependency-free TRS-and-matrix-multiply
  implementation is enough to satisfy this Spec's own hierarchy
  requirement — no third-party math library, no new Core dependency, no
  speculative quaternion/interpolation machinery for a consumer that does
  not exist yet.
- Cycle prevention at mutation time means every other World API
  (traversal, extraction) can assume a valid forest structure
  unconditionally, simplifying every consumer downstream of `setParent()`.
- Cascading destruction removes an entire class of "orphaned subtree, now
  what" bugs and matching special-case code, at the cost of a semantics
  this Spec's own validation scene does not need to work around (nothing
  in this Spec's own scope destroys a parent while wanting its children to
  survive).
- An explicit, single, once-per-frame `updateTransforms()` call keeps the
  entire update model easy to reason about and to unit-test as a pure
  function of a fixed set of local transforms and parent links, with no
  hidden recomputation triggered by an unrelated setter call.

### Negative / Trade-offs

- Reading a world matrix without having called `updateTransforms()` since
  the last relevant mutation silently returns stale data (not an error) —
  a real footgun this ADR accepts as a documented contract rather than
  guarding against with an automatic dirty check, matching this Spec's own
  "explicit, single-threaded mutation" instruction. A future Spec may
  reconsider if this proves error-prone in practice.
- Cascading destruction is a real, disclosed behavior a Plan-stage caller
  must understand before calling `destroyEntity()` on any entity that
  might have children — no "detach children first" escape hatch is
  provided by this Spec.
- Euler-angle local rotation is a real, disclosed constraint: any future
  Spec adding rotation interpolation (animation) will likely need to
  revisit this choice rather than build directly on it.
- O(N) full-traversal update has no headroom built in for a much larger
  future entity count — an explicit, disclosed non-goal of this ADR, not
  an oversight.

## Alternatives Considered

- **Eager, per-setter propagation** (every `setLocalTransform()`/
  `setParent()` call immediately recomputes the affected subtree's world
  matrices). Rejected: more complex to implement and reason about (each
  setter must itself perform a bounded traversal, and multiple setters
  called in sequence before a frame's real "current" state is needed do
  redundant work) for no benefit at this Spec's own scale, where a single
  once-per-frame pass is already cheap and simple.
- **A dirty-flag/incremental update scheme** (mark a subtree dirty on
  mutation; recompute lazily on next read or at a bounded scope).
  Rejected for this round as premature optimization with no measured
  need — see Negative/Trade-offs; may be revisited by a future Spec if
  entity counts grow enough to make O(N) traversal a real, measured cost.
- **Quaternion rotation representation.** Rejected for this round: no
  consumer needs rotation interpolation or is at risk of gimbal-lock
  accumulation across repeated *value* composition (this Spec never
  composes rotation values, only resulting matrices) — see Decision's own
  rotation-choice reasoning. Revisit when animation is specced.
- **Cycle detection only at traversal time** (e.g., a visited-set check
  during `updateTransforms()`, silently skipping or breaking a detected
  cycle). Rejected: leaves World in a state where an invalid hierarchy can
  exist between mutation and the next traversal, and turns a caller
  programming error into a silent data-quality problem (which subtree gets
  arbitrarily dropped?) instead of a clear, immediate `Result::Err` at the
  point of the actual mistake (`setParent()`).
- **Reparent orphaned children to the destroyed entity's own parent (or to
  World root) instead of cascading destruction.** Rejected for this round:
  no real consumer in this Spec's own scope needs a subtree to outlive its
  root, and "keep children alive, silently reparented" is a real semantic
  choice with its own trade-offs (a Plan or future Spec may want this for
  a real authoring workflow) better decided when a concrete need for it
  exists.
