# ADR 0048: World/Scene Module Boundary and Ownership

- **Status:** Accepted
- **Date:** 2026-08-22
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-22 as part of Spec 0014's Human Review Approval
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)
- **Acceptance Record (2026-08-22):** Accepted by Human Review as Human
  Review Decision Table item 1 (new top-level module) and item 9
  (`AssetId` reference boundary) of
  [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)'s
  own Human Review Approval (2026-08-22) — see that Spec's own approval
  note for the full record of what was accepted. This record does not
  change this ADR's own Decision, Consequences, or Alternatives
  Considered below.

## Context

Atlantis's ten top-level modules (Core, Platform, RHI, Vulkan Backend,
RenderGraph, Renderer, Shader System, Asset System, Runtime, Tools) are
named in [AGENTS.md](../AGENTS.md) and detailed in
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
(`PROPOSED`, not `Accepted`). None of them owns, or is positioned to own,
an in-memory, multi-entity scene representation — `Atlantis Renderer`
([specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)) and
`Atlantis Runtime`
([specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md))
both explicitly excluded "a scene graph, ECS, World, or any entity/
component model" from their own scope, and `specs/README.md`'s own
Candidate Spec Backlog has named this gap since the backlog's own
creation as "World/ECS Foundation."

[ADR-0033](0033-runtime-authority-and-client-boundary.md) (`Accepted`)
already commits Atlantis to a long-term principle — "Runtime, once it
exists as a real module, is the sole authoritative owner of engine world
state" — but explicitly deferred "Entity/handle representation... ECS
storage layout, or any other World/ECS implementation detail... entirely
[to] Candidate 6 (World/ECS Foundation)." This ADR, alongside
[specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md),
is that deferred decision.

Two shapes are available, both with real precedent in this repository:

1. **A private submodule of `src/runtime/`** — the scene representation
   lives entirely inside `Atlantis::RuntimeHost`, with no independent
   CMake target, mirroring how `RuntimeLifecycleState` (Spec 0013) is
   private to Runtime today.
2. **A new, independent top-level module** — `Atlantis::World`, a
   separate CMake target with its own public headers, depended on by
   Runtime rather than folded into it. This mirrors Atlantis Asset
   System's own already-`Accepted` shape
   ([ADR-0043](0043-asset-system-module-boundary.md)): a Core-only, CPU-
   side data module that a composition root (Runtime) depends on,
   instantiates, and owns — not a service Runtime provides to others, but
   also not code buried inside Runtime's own private library.

The real, current shape of Renderer's public API is directly relevant to
this choice: `atlantis::renderer::DrawItem` (`src/renderer/include/atlantis/renderer/draw_item.h`)
already carries a raw `std::array<float, 16> objectToWorld` — its own
header comment states plainly that "Atlantis Core has no public math type
yet (not part of this spec's scope to add one)." Confirmed by direct
inspection: `src/core/include/atlantis/` contains only `assert.h`,
`log.h`, and `result.h` — no `Vec3`, no `Mat4`, no quaternion type exists
anywhere in this codebase today. Every existing windowed composition root
(`examples/minimal_renderer_demo/main.cpp`) hand-rolls its own private
`Mat4`/`identityMatrix()`/`multiply()`/`lookAt()`/`perspective()` helpers
locally, duplicated per file, because no shared math type exists to reuse.
A World/Scene module computing hierarchical Transform composition (parent
world matrix × child local matrix) is the first consumer that needs this
math shared across more than one call site within the same module.

## Decision

**World/Scene becomes a new, eleventh top-level module: `Atlantis World`
(CMake target/alias `Atlantis::World`, C++ namespace `atlantis::world`,
directory `src/world/`).** It is not a Runtime-private submodule. This
follows Asset System's own precedent (ADR-0043) rather than Runtime's own
internal-only `RuntimeLifecycleState` precedent, because World's own
scene representation — unlike a lifecycle state machine that only makes
sense wired to a real frame loop — is plain CPU data with real value to
tests, tools, and (per [ADR-0035](0035-authoring-runtime-data-separation-as-a-long-term-principle.md))
a future authoring-side consumer, independent of any real window, Device,
or Platform session.

- **Depends on:** `Atlantis::Core` (`Result<T,E>`, logging, assertions)
  and `Atlantis::AssetSystem` — **for `atlantis::asset_system::AssetId`
  only** (`src/asset_system/include/atlantis/asset_system/asset_id.h`, an
  already-`Accepted`, already-implemented, dependency-free `uint64_t`
  alias). World does not include, call, or link against any other Asset
  System header — not `load.h`, not `StaticMeshAssetData`, not the cooker
  or validation surfaces. World never depends on Atlantis RHI, Vulkan
  Backend, RenderGraph, Renderer, Shader System, Platform, Runtime, or
  Tools — no GPU, windowing, or graphics-API concept appears anywhere in
  its public surface or its implementation. This is the same Core-only-
  plus-one-narrow-type-dependency shape Shader System's own
  `rhi_integration` split and Asset System's own module boundary already
  establish as reusable patterns, applied here at its narrowest: reusing
  one existing identifier type, not a data-loading or GPU-construction
  API.
- **World owns its own minimal math primitives** (a `Vec3`, a rotation
  representation, and 4×4 matrix composition/multiplication — see
  [ADR-0050](0050-transform-hierarchy-composition-and-update-model.md)
  for the exact shape and why), hand-rolled with no third-party
  dependency, scoped as **`atlantis::world`'s own public types** — not a
  new, general-purpose `Atlantis::Math` top-level module. This is a
  deliberately narrow choice: `docs/architecture/module_boundaries.md`'s
  own (`PROPOSED`, not `Accepted`) Core section already anticipates Core
  eventually owning "math (vectors, matrices, quaternions)," but
  promoting math into Core today — a module every other module already
  depends on — would hand a new public capability surface to all ten
  other modules on the strength of exactly one real consumer (World's own
  Transform hierarchy). Every existing composition root's own hand-rolled
  `Mat4` helpers (`examples/minimal_renderer_demo/main.cpp`) remain
  untouched and unconsolidated by this decision; a future Spec is free to
  promote a shared math library into Core once a second genuine consumer
  (e.g., Runtime's own camera view/projection math) makes the
  duplication cost real rather than speculative — not decided or
  scaffolded here.
- **Depended on by:** `Atlantis Runtime` only, for now — the composition
  root that constructs, owns, mutates, and drives one `World` instance per
  process, per [ADR-0033](0033-runtime-authority-and-client-boundary.md)'s
  "Runtime is the sole authoritative owner of engine world state"
  principle. No second consumer (a test fixture aside — see Testing &
  Verification Plan in the related Spec) exists yet. Renderer, RHI, Vulkan
  Backend, RenderGraph, Shader System, Platform, and Tools gain no new
  dependency, in either direction; none of their existing public APIs
  changes.
- **World never depends on, or is depended on by, Atlantis Runtime in the
  reverse direction.** The dependency runs strictly `Runtime → World`,
  matching every other data/logic module's own relationship to Runtime in
  this codebase (Asset System, Shader System).
- **ADR-0033 compliance, satisfied the same way Spec 0013 already
  satisfied it for its own bootstrap state:** Runtime owns the one real
  `World` instance; nothing outside Runtime observes or mutates it in this
  round's scope (no Editor, no second process, no Client). The public,
  cross-module access-category question ADR-0033 raises (query/command/
  event surfaces, no raw internal pointer across a Client boundary) has no
  real second Client to design against yet and is not designed here —
  consistent with ADR-0033's own Alternatives Considered. World's own
  public API (get/set by value, `Result`-returning, index+generation
  `EntityId` handles — see
  [ADR-0049](0049-entity-identity-and-handle-invalidation.md)) is already
  shaped compatibly with that eventual boundary (no raw pointer/reference
  to an internally-owned record ever crosses World's own public surface —
  see ADR-0049), without this ADR claiming to have built the boundary
  itself.
- **ADR-0032 compliance — five-layer placement:** `Atlantis::World` sits
  in the **Authoritative Runtime** conceptual layer, alongside the
  as-yet-largely-private `Atlantis Runtime` module itself — a non-binding,
  illustrative placement only, per ADR-0032's own terms. In the
  authoritative eleven-module source-ownership view, it depends on Core
  and (narrowly) Asset System, and is depended on by Runtime — a leaf
  module below Runtime, like Asset System, not a service Runtime exposes
  to anything else.
- **ADR-0035 compliance — authoring/runtime representation.** World's own
  `Transform`/`Camera`/`Renderable`/hierarchy representation defined by
  this Spec **is** the runtime-execution representation; this ADR does
  not introduce a distinct authoring-facing representation or a bake/
  compile step, because no authoring tool or Editor consumes World data
  yet (matching Spec 0014's own Non-Goals — no scene file format, no
  cooker). This is an explicit, considered choice, not a silent default:
  a future Scene Asset/Serialization Spec (the very next Candidate Backlog
  item after this one) is the one expected to introduce an authoring-side
  representation and a bake step feeding this same runtime `World`
  structure, per ADR-0035's own procedural requirement that this question
  be addressed explicitly rather than left silent.

`docs/architecture/module_boundaries.md`'s own current text does not yet
describe World at all (it predates this ADR). Per this repository's own
established precedent (Spec 0012/Spec 0013's identical treatment of the
same document), reconciling that document is deferred to a future Plan or
docs-sync, not performed by this ADR itself, and is not a blocker to this
ADR's own approval.

## Consequences

### Positive

- Gives World the same, already-proven, already-`Accepted` module shape
  Asset System successfully used: a narrow, Core-adjacent dependency
  surface, trivially unit-testable with no GPU, no window, and no Device
  present.
- Keeps Renderer, RHI, Vulkan Backend, RenderGraph, Shader System, and
  Platform entirely untouched — zero risk to any already-`Accepted`/
  implemented public API, in either dependency direction.
- A World instance is independently constructible and testable outside a
  real Runtime process (no `atlantis_runtime_host` link dependency
  required), the same practical benefit Asset System's and Shader
  System's own Core-only libraries already provide — directly useful to
  this Spec's own GPU-independent test suite (see the related Spec's
  Testing & Verification Plan).
- Reusing `atlantis::asset_system::AssetId` rather than inventing a
  parallel identifier type keeps exactly one source of truth for "what
  identifies an asset" — a Renderable's asset reference and Asset
  System's own cooked-artifact identity are, by construction, the same
  value with no conversion step.

### Negative / Trade-offs

- A new, eleventh top-level module is a real, permanent structural
  commitment — once `Atlantis::World` exists as a module other code links
  against, folding it back into Runtime later would be a breaking,
  disruptive change, not a free refactor.
- World's own minimal math primitives are a second, independent
  hand-rolled matrix-math implementation in this codebase (alongside every
  existing demo's own private `Mat4` helpers) — a real, disclosed
  duplication cost, accepted here rather than promoting math into Core
  ahead of a second genuine consumer. A future consolidation is possible
  but not designed or promised by this ADR.
- Reusing `atlantis::asset_system::AssetId` directly, rather than a
  World-owned opaque identifier type, couples World's own Renderable
  representation to Asset System's current identity scheme
  ([ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md),
  a path-derived, not rename-durable, hash) — if a future Serialization
  and Stable Identity Spec (Candidate Backlog, depends on this Spec)
  changes how assets are identified, World's own `Renderable` type
  inherits that change directly rather than being insulated behind a
  conversion layer.

## Alternatives Considered

- **A private submodule of `src/runtime/`** (no independent CMake
  target). Rejected: World's own CPU-only scene data has real,
  independent value to unit tests and a future authoring/Editor consumer
  that a Runtime-private library — not a public dependency surface any
  other module may consume, per Spec 0013's own explicit design for
  `Atlantis::RuntimeHost` — would foreclose without a later, disruptive
  boundary reopening. Asset System's own ADR-0043 already rejected the
  symmetric "fold into the existing leaf module" option for exactly this
  reason.
- **Promote a general `Atlantis::Math` module into (or alongside) Core
  now**, since `module_boundaries.md`'s own `PROPOSED` text already
  anticipates it. Rejected for this round: no second real consumer exists
  yet to validate a shared library's shape against (Runtime's own camera
  math remains its own hand-rolled code, unchanged by this Spec — see
  [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)) —
  exactly the speculative-abstraction risk AGENTS.md's Golden Rule warns
  against, and it would hand a new capability surface to all ten existing
  modules on the strength of one consumer.
- **Have World depend on Atlantis Renderer directly**, so it could vend
  ready-made `atlantis::renderer::DrawItem` values itself. Rejected: this
  would transitively pull RHI (`Mesh`/`Material` own RHI `Buffer`/
  `Pipeline` handles) into a module this Spec requires to stay CPU-only
  and backend-independent — see
  [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  for the extraction boundary this alternative would have collapsed.
- **Invent a World-owned opaque asset-handle type distinct from
  `asset_system::AssetId`**, converted at the Runtime/adapter boundary.
  Considered as the more strictly decoupled option (see Negative/
  Trade-offs above) but rejected for this round: no real second identity
  scheme exists yet to justify the indirection, and it would duplicate a
  value Asset System already computes deterministically — a future
  Serialization/Stable-Identity Spec remains free to introduce this
  indirection later if it finds a real need, without this ADR
  foreclosing that option.
